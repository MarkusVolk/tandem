/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "../device.h"
#include "../plugin.h"
#include "../util.h"
#include "tandem-build.h"

#include <adwaita.h>

#define TANDEM_TYPE_SENDNOTIFICATIONS_PLUGIN (tandem_sendnotifications_plugin_get_type ())
G_DECLARE_FINAL_TYPE (TandemSendnotificationsPlugin, tandem_sendnotifications_plugin, TANDEM, SENDNOTIFICATIONS_PLUGIN, TandemPlugin)

struct _TandemSendnotificationsPlugin
{
  TandemPlugin parent_instance;
};

G_DEFINE_FINAL_TYPE (TandemSendnotificationsPlugin, tandem_sendnotifications_plugin, TANDEM_TYPE_PLUGIN)

static const char *const sendnotifications_outgoing[] = { "kdeconnect.notification", NULL };

typedef struct
{
  char *app_name;
  guint32 replaces_id;
  char *app_icon;
  char *summary;
  char *body;
  char *desktop_entry;
  gboolean transient;
  guint32 serial;
  char *sender;
} Captured;

static GDBusConnection *monitor;
static GPtrArray *listeners;
static guint next_id = 1;

static void
captured_free (Captured *c)
{
  g_free (c->app_name);
  g_free (c->app_icon);
  g_free (c->summary);
  g_free (c->body);
  g_free (c->desktop_entry);
  g_free (c->sender);
  g_free (c);
}

static char *
strip_markup (const char *text)
{
  GString *out = g_string_new (NULL);
  gboolean in_tag = FALSE;

  for (const char *p = text != NULL ? text : ""; *p != '\0'; p++)
    {
      if (*p == '<')
        in_tag = TRUE;
      else if (*p == '>')
        in_tag = FALSE;
      else if (!in_tag)
        g_string_append_c (out, *p);
    }
  return g_string_free (out, FALSE);
}

static gboolean
dispatch_cb (gpointer user_data)
{
  Captured *c = user_data;
  g_autofree char *text = strip_markup (c->body);
  g_autofree char *id = g_strdup_printf ("%s-%u", c->sender != NULL ? c->sender : "local", c->replaces_id != 0 ? c->replaces_id : next_id++);
  g_autofree char *ticker = g_strdup_printf ("%s: %s", c->summary != NULL ? c->summary : "", text);

  if (g_strcmp0 (c->desktop_entry, TANDEM_APP_ID) == 0 || c->transient)
    {
      captured_free (c);
      return G_SOURCE_REMOVE;
    }
  for (guint i = 0; listeners != NULL && i < listeners->len; i++)
    {
      TandemPlugin *plugin = g_ptr_array_index (listeners, i);
      g_autoptr(TandemPacket) packet = tandem_packet_new ("kdeconnect.notification");

      if (!g_settings_get_boolean (tandem_plugin_get_settings (plugin), "send-notifications"))
        continue;
      tandem_packet_set_string (packet, "id", id);
      tandem_packet_set_string (packet, "appName", c->app_name != NULL ? c->app_name : "");
      tandem_packet_set_string (packet, "title", c->summary != NULL ? c->summary : "");
      tandem_packet_set_string (packet, "text", text);
      tandem_packet_set_string (packet, "ticker", ticker);
      tandem_packet_set_boolean (packet, "isClearable", TRUE);
      tandem_packet_set_int (packet, "time", tandem_now_ms ());
      g_debug ("sendnotifications: forwarding %s from %s", id, c->app_name);
      tandem_plugin_send (plugin, packet);
    }
  captured_free (c);
  return G_SOURCE_REMOVE;
}

static GDBusMessage *
filter_cb (GDBusConnection *connection, GDBusMessage *message, gboolean incoming, gpointer user_data)
{
  Captured *c;
  g_autoptr(GVariant) body = NULL;
  g_autoptr(GVariant) hints = NULL;
  const char *app_name, *app_icon, *summary, *text;
  guint32 replaces_id;
  const char *desktop_entry = NULL;

  if (!incoming || g_dbus_message_get_message_type (message) != G_DBUS_MESSAGE_TYPE_METHOD_CALL ||
      g_strcmp0 (g_dbus_message_get_interface (message), "org.freedesktop.Notifications") != 0 ||
      g_strcmp0 (g_dbus_message_get_member (message), "Notify") != 0)
    return message;
  body = g_variant_ref (g_dbus_message_get_body (message));
  if (body == NULL || !g_variant_is_of_type (body, G_VARIANT_TYPE ("(susssasa{sv}i)")))
    return message;
  g_variant_get (body, "(&su&s&s&s@as@a{sv}i)", &app_name, &replaces_id, &app_icon, &summary, &text, NULL, &hints, NULL);
  c = g_new0 (Captured, 1);
  c->app_name = g_strdup (app_name);
  c->replaces_id = replaces_id;
  c->app_icon = g_strdup (app_icon);
  c->summary = g_strdup (summary);
  c->body = g_strdup (text);
  c->sender = g_strdup (g_dbus_message_get_sender (message));
  if (g_variant_lookup (hints, "desktop-entry", "&s", &desktop_entry))
    c->desktop_entry = g_strdup (desktop_entry);
  g_variant_lookup (hints, "transient", "b", &c->transient);
  g_main_context_invoke (NULL, dispatch_cb, c);
  return message;
}

static void
monitor_ready_cb (GObject *source, GAsyncResult *result, gpointer user_data)
{
  g_autoptr(GError) error = NULL;
  GDBusConnection *connection = g_dbus_connection_new_for_address_finish (result, &error);
  const char *rules[] = { "type='method_call',interface='org.freedesktop.Notifications',member='Notify'", NULL };
  g_autoptr(GVariant) reply = NULL;

  if (connection == NULL)
    {
      g_warning ("sendnotifications: no monitor connection: %s", error->message);
      return;
    }
  g_dbus_connection_add_filter (connection, filter_cb, NULL, NULL);
  reply = g_dbus_connection_call_sync (connection, "org.freedesktop.DBus", "/org/freedesktop/DBus",
                                       "org.freedesktop.DBus.Monitoring", "BecomeMonitor",
                                       g_variant_new ("(^asu)", rules, 0), NULL, G_DBUS_CALL_FLAGS_NONE, 5000, NULL, &error);
  if (reply == NULL)
    {
      g_warning ("sendnotifications: BecomeMonitor failed: %s", error->message);
      g_object_unref (connection);
      return;
    }
  monitor = connection;
}

static void
ensure_monitor (void)
{
  g_autofree char *address = NULL;

  if (monitor != NULL)
    return;
  address = g_dbus_address_get_for_bus_sync (G_BUS_TYPE_SESSION, NULL, NULL);
  if (address == NULL)
    return;
  g_dbus_connection_new_for_address (address,
                                     G_DBUS_CONNECTION_FLAGS_AUTHENTICATION_CLIENT | G_DBUS_CONNECTION_FLAGS_MESSAGE_BUS_CONNECTION,
                                     NULL, NULL, monitor_ready_cb, NULL);
}

static GtkWidget *
sendnotifications_create_group (TandemPlugin *plugin)
{
  GtkWidget *group = adw_preferences_group_new ();
  GtkWidget *row = adw_switch_row_new ();

  adw_preferences_group_set_title (ADW_PREFERENCES_GROUP (group), "Desktop notifications");
  adw_preferences_row_set_title (ADW_PREFERENCES_ROW (row), "Forward notifications to the device");
  g_settings_bind (tandem_plugin_get_settings (plugin), "send-notifications", row, "active", G_SETTINGS_BIND_DEFAULT);
  adw_preferences_group_add (ADW_PREFERENCES_GROUP (group), row);
  return group;
}

static void
tandem_sendnotifications_plugin_constructed (GObject *object)
{
  G_OBJECT_CLASS (tandem_sendnotifications_plugin_parent_class)->constructed (object);
  if (listeners == NULL)
    listeners = g_ptr_array_new ();
  g_ptr_array_add (listeners, object);
  ensure_monitor ();
}

static void
tandem_sendnotifications_plugin_dispose (GObject *object)
{
  if (listeners != NULL)
    g_ptr_array_remove (listeners, object);
  G_OBJECT_CLASS (tandem_sendnotifications_plugin_parent_class)->dispose (object);
}

static void
tandem_sendnotifications_plugin_class_init (TandemSendnotificationsPluginClass *klass)
{
  TandemPluginClass *plugin_class = TANDEM_PLUGIN_CLASS (klass);
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->constructed = tandem_sendnotifications_plugin_constructed;
  object_class->dispose = tandem_sendnotifications_plugin_dispose;
  plugin_class->name = "sendnotifications";
  plugin_class->title = "Send notifications";
  plugin_class->outgoing = sendnotifications_outgoing;
  plugin_class->create_group = sendnotifications_create_group;
}

static void
tandem_sendnotifications_plugin_init (TandemSendnotificationsPlugin *self)
{
}
