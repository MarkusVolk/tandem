/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "../device.h"
#include "../plugin.h"


#define TANDEM_TYPE_REMOTEINPUT_PLUGIN (tandem_remoteinput_plugin_get_type ())
G_DECLARE_FINAL_TYPE (TandemRemoteinputPlugin, tandem_remoteinput_plugin, TANDEM, REMOTEINPUT_PLUGIN, TandemPlugin)

struct _TandemRemoteinputPlugin
{
  TandemPlugin parent_instance;

  gboolean keyboard_ready;
};

G_DEFINE_FINAL_TYPE (TandemRemoteinputPlugin, tandem_remoteinput_plugin, TANDEM_TYPE_PLUGIN)

static const char *const remoteinput_incoming[] = { "kdeconnect.mousepad.keyboardstate", NULL };
static const char *const remoteinput_outgoing[] = { "kdeconnect.mousepad.request", NULL };

void
tandem_remoteinput_plugin_send_text (TandemPlugin *plugin, const char *text)
{
  g_autoptr(TandemPacket) packet = tandem_packet_new ("kdeconnect.mousepad.request");

  tandem_packet_set_string (packet, "key", text);
  tandem_plugin_send (plugin, packet);
}

void
tandem_remoteinput_plugin_send_special (TandemPlugin *plugin, int special)
{
  g_autoptr(TandemPacket) packet = tandem_packet_new ("kdeconnect.mousepad.request");

  tandem_packet_set_int (packet, "specialKey", special);
  tandem_plugin_send (plugin, packet);
}

static void
remoteinput_handle_packet (TandemPlugin *plugin, TandemPacket *packet)
{
  TandemRemoteinputPlugin *self = TANDEM_REMOTEINPUT_PLUGIN (plugin);

  self->keyboard_ready = tandem_packet_get_boolean (packet, "state", FALSE);
  tandem_plugin_changed (plugin);
}

static GVariant *
remoteinput_get_state (TandemPlugin *plugin)
{
  GVariantBuilder builder;

  g_variant_builder_init (&builder, G_VARIANT_TYPE ("a{sv}"));
  g_variant_builder_add (&builder, "{sv}", "keyboard",
                         g_variant_new_boolean (TANDEM_REMOTEINPUT_PLUGIN (plugin)->keyboard_ready));
  return g_variant_builder_end (&builder);
}

static GVariant *
remoteinput_call (TandemPlugin *plugin, const char *method, GVariant *args, GError **error)
{
  const char *text = NULL;
  gint32 key = 0;

  if (g_str_equal (method, "text") && tandem_plugin_args_lookup (args, "text", "&s", &text))
    tandem_remoteinput_plugin_send_text (plugin, text);
  else if (g_str_equal (method, "special") && tandem_plugin_args_lookup (args, "key", "i", &key))
    tandem_remoteinput_plugin_send_special (plugin, key);
  else
    g_set_error (error, G_DBUS_ERROR, G_DBUS_ERROR_INVALID_ARGS, "Bad call %s", method);
  return NULL;
}

static void
tandem_remoteinput_plugin_class_init (TandemRemoteinputPluginClass *klass)
{
  TandemPluginClass *plugin_class = TANDEM_PLUGIN_CLASS (klass);

  plugin_class->name = "remoteinput";
  plugin_class->title = "Remote keyboard";
  plugin_class->incoming = remoteinput_incoming;
  plugin_class->outgoing = remoteinput_outgoing;
  plugin_class->handle_packet = remoteinput_handle_packet;
  plugin_class->get_state = remoteinput_get_state;
  plugin_class->call = remoteinput_call;
}

static void
tandem_remoteinput_plugin_init (TandemRemoteinputPlugin *self)
{
}
