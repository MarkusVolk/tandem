/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "../device.h"
#include "../notify.h"
#include "../plugin.h"

#define TANDEM_TYPE_PING_PLUGIN (tandem_ping_plugin_get_type ())
G_DECLARE_FINAL_TYPE (TandemPingPlugin, tandem_ping_plugin, TANDEM, PING_PLUGIN, TandemPlugin)

struct _TandemPingPlugin
{
  TandemPlugin parent_instance;
};

G_DEFINE_FINAL_TYPE (TandemPingPlugin, tandem_ping_plugin, TANDEM_TYPE_PLUGIN)

static const char *const ping_types[] = { "kdeconnect.ping", NULL };

static void
ping_handle_packet (TandemPlugin *plugin, TandemPacket *packet)
{
  TandemDevice *device = tandem_plugin_get_device (plugin);
  const char *message = tandem_packet_get_string (packet, "message", NULL);
  g_autofree char *summary = g_strdup_printf ("Ping from %s", tandem_device_get_name (device));
  g_autoptr(TandemNotification) notification = tandem_notification_new (summary, message);

  tandem_notification_set_icon (notification, "phone-symbolic");
  tandem_notification_send (notification);
}

static void
tandem_ping_plugin_class_init (TandemPingPluginClass *klass)
{
  TandemPluginClass *plugin_class = TANDEM_PLUGIN_CLASS (klass);

  plugin_class->name = "ping";
  plugin_class->title = "Ping";
  plugin_class->incoming = ping_types;
  plugin_class->outgoing = ping_types;
  plugin_class->handle_packet = ping_handle_packet;
}

static void
tandem_ping_plugin_init (TandemPingPlugin *self)
{
}

void
tandem_ping_plugin_send (TandemPlugin *plugin, const char *message)
{
  g_autoptr(TandemPacket) packet = tandem_packet_new ("kdeconnect.ping");

  if (message != NULL && *message != '\0')
    tandem_packet_set_string (packet, "message", message);
  tandem_plugin_send (plugin, packet);
}
