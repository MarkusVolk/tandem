/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "../device.h"
#include "../plugin.h"

#include <adwaita.h>

#define TANDEM_TYPE_LOCK_PLUGIN (tandem_lock_plugin_get_type ())
G_DECLARE_FINAL_TYPE (TandemLockPlugin, tandem_lock_plugin, TANDEM, LOCK_PLUGIN, TandemPlugin)

struct _TandemLockPlugin
{
  TandemPlugin parent_instance;

  GDBusProxy *session;
  gboolean remote_locked;
  gboolean last_sent;
  gboolean sent_once;
};

G_DEFINE_FINAL_TYPE (TandemLockPlugin, tandem_lock_plugin, TANDEM_TYPE_PLUGIN)

static const char *const lock_incoming[] = { "kdeconnect.lock", "kdeconnect.lock.request", NULL };
static const char *const lock_outgoing[] = { "kdeconnect.lock", "kdeconnect.lock.request", NULL };

static gboolean
local_locked (TandemLockPlugin *self)
{
  g_autoptr(GVariant) hint = NULL;

  if (self->session == NULL)
    return FALSE;
  hint = g_dbus_proxy_get_cached_property (self->session, "LockedHint");
  return hint != NULL && g_variant_get_boolean (hint);
}

static void
send_local_state (TandemLockPlugin *self, gboolean force)
{
  gboolean locked = local_locked (self);
  g_autoptr(TandemPacket) packet = NULL;

  if (!force && self->sent_once && locked == self->last_sent)
    return;
  self->sent_once = TRUE;
  self->last_sent = locked;
  packet = tandem_packet_new ("kdeconnect.lock");
  tandem_packet_set_boolean (packet, "isLocked", locked);
  tandem_plugin_send (TANDEM_PLUGIN (self), packet);
}

static void
session_changed_cb (GDBusProxy *proxy, GVariant *changed, GStrv invalidated, gpointer user_data)
{
  send_local_state (TANDEM_LOCK_PLUGIN (user_data), FALSE);
}

static void
session_ready_cb (GObject *source, GAsyncResult *result, gpointer user_data)
{
  g_autoptr(TandemLockPlugin) self = user_data;
  g_autoptr(GError) error = NULL;

  self->session = g_dbus_proxy_new_for_bus_finish (result, &error);
  if (self->session == NULL)
    {
      g_debug ("lock: no logind session: %s", error->message);
      return;
    }
  g_signal_connect (self->session, "g-properties-changed", G_CALLBACK (session_changed_cb), self);
  send_local_state (self, TRUE);
}

static void
lock_handle_packet (TandemPlugin *plugin, TandemPacket *packet)
{
  TandemLockPlugin *self = TANDEM_LOCK_PLUGIN (plugin);

  if (tandem_packet_is_type (packet, "kdeconnect.lock"))
    {
      self->remote_locked = tandem_packet_get_boolean (packet, "isLocked", FALSE);
      tandem_plugin_changed (plugin);
      return;
    }
  if (tandem_packet_has (packet, "setLocked") && self->session != NULL)
    {
      gboolean lock = tandem_packet_get_boolean (packet, "setLocked", TRUE);

      if (lock)
        g_dbus_proxy_call (self->session, "Lock", NULL, G_DBUS_CALL_FLAGS_NONE, -1, NULL, NULL, NULL);
      else if (g_settings_get_boolean (tandem_plugin_get_settings (plugin), "allow-unlock"))
        g_dbus_proxy_call (self->session, "Unlock", NULL, G_DBUS_CALL_FLAGS_NONE, -1, NULL, NULL, NULL);
    }
  if (tandem_packet_has (packet, "requestLocked") || tandem_packet_has (packet, "setLocked"))
    send_local_state (self, TRUE);
}

static void
lock_connected (TandemPlugin *plugin)
{
  TandemLockPlugin *self = TANDEM_LOCK_PLUGIN (plugin);
  g_autoptr(TandemPacket) request = tandem_packet_new ("kdeconnect.lock.request");

  tandem_packet_set_boolean (request, "requestLocked", TRUE);
  tandem_plugin_send (plugin, request);
  if (self->session == NULL)
    g_dbus_proxy_new_for_bus (G_BUS_TYPE_SYSTEM, G_DBUS_PROXY_FLAGS_NONE, NULL, "org.freedesktop.login1",
                              "/org/freedesktop/login1/session/auto", "org.freedesktop.login1.Session",
                              NULL, session_ready_cb, g_object_ref (self));
  else
    send_local_state (self, TRUE);
}

void
tandem_lock_plugin_set_remote (TandemPlugin *plugin, gboolean locked)
{
  g_autoptr(TandemPacket) packet = tandem_packet_new ("kdeconnect.lock.request");

  tandem_packet_set_boolean (packet, "setLocked", locked);
  tandem_plugin_send (plugin, packet);
}

static void
lock_switch_cb (GObject *row, GParamSpec *pspec, gpointer user_data)
{
  TandemLockPlugin *self = TANDEM_LOCK_PLUGIN (user_data);
  gboolean active = adw_switch_row_get_active (ADW_SWITCH_ROW (row));

  if (active != self->remote_locked)
    tandem_lock_plugin_set_remote (TANDEM_PLUGIN (self), active);
}

static void
lock_changed_cb (TandemPlugin *plugin, gpointer user_data)
{
  TandemLockPlugin *self = TANDEM_LOCK_PLUGIN (plugin);

  if (adw_switch_row_get_active (ADW_SWITCH_ROW (user_data)) != self->remote_locked)
    adw_switch_row_set_active (ADW_SWITCH_ROW (user_data), self->remote_locked);
}

static GtkWidget *
lock_create_group (TandemPlugin *plugin)
{
  GtkWidget *group = adw_preferences_group_new ();
  GtkWidget *row = adw_switch_row_new ();

  adw_preferences_group_set_title (ADW_PREFERENCES_GROUP (group), "Lock");
  adw_preferences_row_set_title (ADW_PREFERENCES_ROW (row), "Device locked");
  adw_switch_row_set_active (ADW_SWITCH_ROW (row), TANDEM_LOCK_PLUGIN (plugin)->remote_locked);
  g_signal_connect_object (row, "notify::active", G_CALLBACK (lock_switch_cb), plugin, 0);
  g_signal_connect_object (plugin, "changed", G_CALLBACK (lock_changed_cb), row, 0);
  adw_preferences_group_add (ADW_PREFERENCES_GROUP (group), row);
  return group;
}

static void
tandem_lock_plugin_dispose (GObject *object)
{
  TandemLockPlugin *self = TANDEM_LOCK_PLUGIN (object);

  if (self->session != NULL)
    g_signal_handlers_disconnect_by_data (self->session, self);
  g_clear_object (&self->session);
  G_OBJECT_CLASS (tandem_lock_plugin_parent_class)->dispose (object);
}

static void
tandem_lock_plugin_class_init (TandemLockPluginClass *klass)
{
  TandemPluginClass *plugin_class = TANDEM_PLUGIN_CLASS (klass);

  G_OBJECT_CLASS (klass)->dispose = tandem_lock_plugin_dispose;
  plugin_class->name = "lock";
  plugin_class->title = "Lock";
  plugin_class->incoming = lock_incoming;
  plugin_class->outgoing = lock_outgoing;
  plugin_class->handle_packet = lock_handle_packet;
  plugin_class->connected = lock_connected;
  plugin_class->create_group = lock_create_group;
}

static void
tandem_lock_plugin_init (TandemLockPlugin *self)
{
}
