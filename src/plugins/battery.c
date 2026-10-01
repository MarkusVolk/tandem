/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "../device.h"
#include "../notify.h"
#include "../plugin.h"

#include <adwaita.h>

#define LOW_THRESHOLD 15

#define TANDEM_TYPE_BATTERY_PLUGIN (tandem_battery_plugin_get_type ())
G_DECLARE_FINAL_TYPE (TandemBatteryPlugin, tandem_battery_plugin, TANDEM, BATTERY_PLUGIN, TandemPlugin)

struct _TandemBatteryPlugin
{
  TandemPlugin parent_instance;

  int charge;
  gboolean charging;
  gboolean low_notified;
  GDBusProxy *upower;
  int last_sent_charge;
  gboolean last_sent_charging;
};

G_DEFINE_FINAL_TYPE (TandemBatteryPlugin, tandem_battery_plugin, TANDEM_TYPE_PLUGIN)

static const char *const battery_incoming[] = { "kdeconnect.battery", "kdeconnect.battery.request", NULL };
static const char *const battery_outgoing[] = { "kdeconnect.battery", "kdeconnect.battery.request", NULL };

int
tandem_battery_plugin_get_charge (TandemPlugin *plugin)
{
  return TANDEM_BATTERY_PLUGIN (plugin)->charge;
}

gboolean
tandem_battery_plugin_is_charging (TandemPlugin *plugin)
{
  return TANDEM_BATTERY_PLUGIN (plugin)->charging;
}

static void
send_local_state (TandemBatteryPlugin *self, gboolean force)
{
  g_autoptr(GVariant) percentage = NULL;
  g_autoptr(GVariant) state = NULL;
  g_autoptr(GVariant) present = NULL;
  g_autoptr(TandemPacket) packet = NULL;
  int charge;
  gboolean charging;
  guint32 state_value;

  if (self->upower == NULL)
    return;
  present = g_dbus_proxy_get_cached_property (self->upower, "IsPresent");
  percentage = g_dbus_proxy_get_cached_property (self->upower, "Percentage");
  state = g_dbus_proxy_get_cached_property (self->upower, "State");
  if (present == NULL || !g_variant_get_boolean (present) || percentage == NULL || state == NULL)
    return;
  charge = (int) g_variant_get_double (percentage);
  state_value = g_variant_get_uint32 (state);
  charging = state_value == 1 || state_value == 4;
  if (!force && charge == self->last_sent_charge && charging == self->last_sent_charging)
    return;
  self->last_sent_charge = charge;
  self->last_sent_charging = charging;

  packet = tandem_packet_new ("kdeconnect.battery");
  tandem_packet_set_int (packet, "currentCharge", charge);
  tandem_packet_set_boolean (packet, "isCharging", charging);
  tandem_packet_set_int (packet, "thresholdEvent", (!charging && charge < LOW_THRESHOLD) ? 1 : 0);
  tandem_plugin_send (TANDEM_PLUGIN (self), packet);
}

static void
upower_changed_cb (GDBusProxy *proxy, GVariant *changed, GStrv invalidated, gpointer user_data)
{
  send_local_state (TANDEM_BATTERY_PLUGIN (user_data), FALSE);
}

static void
upower_ready_cb (GObject *source, GAsyncResult *result, gpointer user_data)
{
  g_autoptr(TandemBatteryPlugin) self = user_data;
  g_autoptr(GError) error = NULL;

  self->upower = g_dbus_proxy_new_for_bus_finish (result, &error);
  if (self->upower == NULL)
    {
      g_debug ("battery: no UPower: %s", error->message);
      return;
    }
  g_signal_connect (self->upower, "g-properties-changed", G_CALLBACK (upower_changed_cb), self);
  send_local_state (self, TRUE);
}

static void
battery_handle_packet (TandemPlugin *plugin, TandemPacket *packet)
{
  TandemBatteryPlugin *self = TANDEM_BATTERY_PLUGIN (plugin);

  if (tandem_packet_is_type (packet, "kdeconnect.battery.request"))
    {
      send_local_state (self, TRUE);
      return;
    }

  self->charge = (int) tandem_packet_get_int (packet, "currentCharge", -1);
  self->charging = tandem_packet_get_boolean (packet, "isCharging", FALSE);
  if (tandem_packet_get_int (packet, "thresholdEvent", 0) == 1 && !self->charging)
    {
      if (!self->low_notified)
        {
          TandemDevice *device = tandem_plugin_get_device (plugin);
          g_autofree char *summary = g_strdup_printf ("%s battery low", tandem_device_get_name (device));
          g_autofree char *body = g_strdup_printf ("%d %% remaining", self->charge);
          g_autoptr(TandemNotification) notification = tandem_notification_new (summary, body);

          tandem_notification_set_icon (notification, "battery-caution-symbolic");
          tandem_notification_send (notification);
          self->low_notified = TRUE;
        }
    }
  else
    {
      self->low_notified = FALSE;
    }
  tandem_plugin_changed (plugin);
}

static void
battery_connected (TandemPlugin *plugin)
{
  TandemBatteryPlugin *self = TANDEM_BATTERY_PLUGIN (plugin);
  g_autoptr(TandemPacket) request = tandem_packet_new ("kdeconnect.battery.request");

  tandem_packet_set_boolean (request, "request", TRUE);
  tandem_plugin_send (plugin, request);

  if (self->upower == NULL)
    g_dbus_proxy_new_for_bus (G_BUS_TYPE_SYSTEM, G_DBUS_PROXY_FLAGS_NONE, NULL, "org.freedesktop.UPower",
                              "/org/freedesktop/UPower/devices/DisplayDevice",
                              "org.freedesktop.UPower.Device", NULL, upower_ready_cb, g_object_ref (self));
  else
    send_local_state (self, TRUE);
}

static void
battery_row_update (GtkWidget *row, TandemPlugin *plugin)
{
  TandemBatteryPlugin *self = TANDEM_BATTERY_PLUGIN (plugin);
  g_autofree char *text = NULL;
  const char *icon;

  if (self->charge < 0)
    {
      text = g_strdup ("Unknown");
      icon = "battery-missing-symbolic";
    }
  else
    {
      text = g_strdup_printf ("%d %%%s", self->charge, self->charging ? ", charging" : "");
      if (self->charging)
        icon = "battery-good-charging-symbolic";
      else if (self->charge < LOW_THRESHOLD)
        icon = "battery-caution-symbolic";
      else if (self->charge < 50)
        icon = "battery-low-symbolic";
      else
        icon = "battery-good-symbolic";
    }
  adw_action_row_set_subtitle (ADW_ACTION_ROW (row), text);
  gtk_image_set_from_icon_name (GTK_IMAGE (g_object_get_data (G_OBJECT (row), "icon")), icon);
}

static void
battery_changed_cb (TandemPlugin *plugin, gpointer user_data)
{
  battery_row_update (GTK_WIDGET (user_data), plugin);
}

static GtkWidget *
battery_create_group (TandemPlugin *plugin)
{
  GtkWidget *group = adw_preferences_group_new ();
  GtkWidget *row = adw_action_row_new ();
  GtkWidget *icon = gtk_image_new ();

  adw_preferences_group_set_title (ADW_PREFERENCES_GROUP (group), "Battery");
  adw_preferences_row_set_title (ADW_PREFERENCES_ROW (row), "Charge");
  adw_action_row_add_prefix (ADW_ACTION_ROW (row), icon);
  g_object_set_data (G_OBJECT (row), "icon", icon);
  adw_preferences_group_add (ADW_PREFERENCES_GROUP (group), row);
  battery_row_update (row, plugin);
  g_signal_connect_object (plugin, "changed", G_CALLBACK (battery_changed_cb), row, 0);
  return group;
}

static void
tandem_battery_plugin_dispose (GObject *object)
{
  TandemBatteryPlugin *self = TANDEM_BATTERY_PLUGIN (object);

  if (self->upower != NULL)
    g_signal_handlers_disconnect_by_data (self->upower, self);
  g_clear_object (&self->upower);
  G_OBJECT_CLASS (tandem_battery_plugin_parent_class)->dispose (object);
}

static void
tandem_battery_plugin_class_init (TandemBatteryPluginClass *klass)
{
  TandemPluginClass *plugin_class = TANDEM_PLUGIN_CLASS (klass);

  G_OBJECT_CLASS (klass)->dispose = tandem_battery_plugin_dispose;
  plugin_class->name = "battery";
  plugin_class->title = "Battery";
  plugin_class->incoming = battery_incoming;
  plugin_class->outgoing = battery_outgoing;
  plugin_class->handle_packet = battery_handle_packet;
  plugin_class->connected = battery_connected;
  plugin_class->create_group = battery_create_group;
}

static void
tandem_battery_plugin_init (TandemBatteryPlugin *self)
{
  self->charge = -1;
  self->last_sent_charge = -1;
}
