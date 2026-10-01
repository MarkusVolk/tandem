/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "../device.h"
#include "../plugin.h"
#include "../pulse.h"

#define TANDEM_TYPE_SYSTEMVOLUME_PLUGIN (tandem_systemvolume_plugin_get_type ())
G_DECLARE_FINAL_TYPE (TandemSystemvolumePlugin, tandem_systemvolume_plugin, TANDEM, SYSTEMVOLUME_PLUGIN, TandemPlugin)

struct _TandemSystemvolumePlugin
{
  TandemPlugin parent_instance;

  gulong changed_handler;
};

G_DEFINE_FINAL_TYPE (TandemSystemvolumePlugin, tandem_systemvolume_plugin, TANDEM_TYPE_PLUGIN)

static const char *const systemvolume_incoming[] = { "kdeconnect.systemvolume.request", NULL };
static const char *const systemvolume_outgoing[] = { "kdeconnect.systemvolume", NULL };

static void
send_sink_list (TandemPlugin *plugin)
{
  TandemPulse *pulse = tandem_pulse_get ();
  GPtrArray *sinks = tandem_pulse_get_sinks (pulse);
  g_autoptr(TandemPacket) packet = tandem_packet_new ("kdeconnect.systemvolume");
  JsonArray *array = json_array_new ();

  for (guint i = 0; i < sinks->len; i++)
    {
      TandemSink *sink = g_ptr_array_index (sinks, i);
      JsonObject *entry = json_object_new ();

      json_object_set_string_member (entry, "name", sink->name);
      json_object_set_string_member (entry, "description", sink->description);
      json_object_set_boolean_member (entry, "muted", sink->muted);
      json_object_set_int_member (entry, "volume", sink->volume);
      json_object_set_int_member (entry, "maxVolume", sink->max_volume);
      json_object_set_boolean_member (entry, "enabled", sink->is_default);
      json_array_add_object_element (array, entry);
    }
  json_object_set_array_member (packet->body, "sinkList", array);
  tandem_plugin_send (plugin, packet);
}

static void
pulse_changed_cb (TandemPulse *pulse, gpointer user_data)
{
  send_sink_list (TANDEM_PLUGIN (user_data));
}

static void
systemvolume_handle_packet (TandemPlugin *plugin, TandemPacket *packet)
{
  TandemPulse *pulse = tandem_pulse_get ();
  const char *name;

  if (tandem_packet_get_boolean (packet, "requestSinks", FALSE))
    {
      send_sink_list (plugin);
      return;
    }
  name = tandem_packet_get_string (packet, "name", NULL);
  if (name == NULL)
    return;
  if (tandem_packet_has (packet, "volume"))
    tandem_pulse_set_volume (pulse, name, (guint32) tandem_packet_get_int (packet, "volume", 0));
  if (tandem_packet_has (packet, "muted"))
    tandem_pulse_set_muted (pulse, name, tandem_packet_get_boolean (packet, "muted", FALSE));
  if (tandem_packet_get_boolean (packet, "enabled", FALSE))
    tandem_pulse_set_default (pulse, name);
}

static void
systemvolume_connected (TandemPlugin *plugin)
{
  TandemSystemvolumePlugin *self = TANDEM_SYSTEMVOLUME_PLUGIN (plugin);
  TandemPulse *pulse = tandem_pulse_get ();

  if (self->changed_handler == 0)
    self->changed_handler = g_signal_connect (pulse, "changed", G_CALLBACK (pulse_changed_cb), plugin);
  if (tandem_pulse_is_ready (pulse))
    send_sink_list (plugin);
}

static void
systemvolume_disconnected (TandemPlugin *plugin)
{
  TandemSystemvolumePlugin *self = TANDEM_SYSTEMVOLUME_PLUGIN (plugin);

  if (self->changed_handler != 0)
    g_signal_handler_disconnect (tandem_pulse_get (), self->changed_handler);
  self->changed_handler = 0;
}

static void
tandem_systemvolume_plugin_dispose (GObject *object)
{
  systemvolume_disconnected (TANDEM_PLUGIN (object));
  G_OBJECT_CLASS (tandem_systemvolume_plugin_parent_class)->dispose (object);
}

static void
tandem_systemvolume_plugin_class_init (TandemSystemvolumePluginClass *klass)
{
  TandemPluginClass *plugin_class = TANDEM_PLUGIN_CLASS (klass);

  G_OBJECT_CLASS (klass)->dispose = tandem_systemvolume_plugin_dispose;
  plugin_class->name = "systemvolume";
  plugin_class->title = "System volume";
  plugin_class->incoming = systemvolume_incoming;
  plugin_class->outgoing = systemvolume_outgoing;
  plugin_class->handle_packet = systemvolume_handle_packet;
  plugin_class->connected = systemvolume_connected;
  plugin_class->disconnected = systemvolume_disconnected;
}

static void
tandem_systemvolume_plugin_init (TandemSystemvolumePlugin *self)
{
}
