/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "../device.h"
#include "../plugin.h"


#define TANDEM_TYPE_CONNECTIVITY_PLUGIN (tandem_connectivity_plugin_get_type ())
G_DECLARE_FINAL_TYPE (TandemConnectivityPlugin, tandem_connectivity_plugin, TANDEM, CONNECTIVITY_PLUGIN, TandemPlugin)

struct _TandemConnectivityPlugin
{
  TandemPlugin parent_instance;

  char *summary;
};

G_DEFINE_FINAL_TYPE (TandemConnectivityPlugin, tandem_connectivity_plugin, TANDEM_TYPE_PLUGIN)

static const char *const connectivity_incoming[] = { "kdeconnect.connectivity_report", NULL };
static const char *const connectivity_outgoing[] = { "kdeconnect.connectivity_report.request", NULL };

static void
connectivity_handle_packet (TandemPlugin *plugin, TandemPacket *packet)
{
  TandemConnectivityPlugin *self = TANDEM_CONNECTIVITY_PLUGIN (plugin);
  JsonObject *strengths = tandem_packet_get_object (packet, "signalStrengths");
  g_autoptr(GString) text = g_string_new (NULL);
  JsonObjectIter iter;
  const char *key;
  JsonNode *node;

  if (strengths != NULL)
    {
      json_object_iter_init (&iter, strengths);
      while (json_object_iter_next (&iter, &key, &node))
        {
          JsonObject *signal;
          const char *type;
          gint64 strength;

          if (!JSON_NODE_HOLDS_OBJECT (node))
            continue;
          signal = json_node_get_object (node);
          type = json_object_has_member (signal, "networkType") ? json_object_get_string_member (signal, "networkType") : "?";
          strength = json_object_has_member (signal, "signalStrength") ? json_object_get_int_member (signal, "signalStrength") : -1;
          if (text->len > 0)
            g_string_append (text, ", ");
          if (strength < 0)
            g_string_append_printf (text, "%s", type);
          else
            g_string_append_printf (text, "%s %" G_GINT64_FORMAT "/4", type, strength);
        }
    }
  g_free (self->summary);
  self->summary = g_string_free (g_steal_pointer (&text), FALSE);
  tandem_plugin_changed (plugin);
}

static void
connectivity_connected (TandemPlugin *plugin)
{
  g_autoptr(TandemPacket) request = tandem_packet_new ("kdeconnect.connectivity_report.request");

  tandem_plugin_send (plugin, request);
}

static GVariant *
connectivity_get_state (TandemPlugin *plugin)
{
  TandemConnectivityPlugin *self = TANDEM_CONNECTIVITY_PLUGIN (plugin);
  GVariantBuilder builder;

  g_variant_builder_init (&builder, G_VARIANT_TYPE ("a{sv}"));
  g_variant_builder_add (&builder, "{sv}", "summary", g_variant_new_string (self->summary != NULL ? self->summary : ""));
  return g_variant_builder_end (&builder);
}

static void
tandem_connectivity_plugin_finalize (GObject *object)
{
  g_free (TANDEM_CONNECTIVITY_PLUGIN (object)->summary);
  G_OBJECT_CLASS (tandem_connectivity_plugin_parent_class)->finalize (object);
}

static void
tandem_connectivity_plugin_class_init (TandemConnectivityPluginClass *klass)
{
  TandemPluginClass *plugin_class = TANDEM_PLUGIN_CLASS (klass);

  G_OBJECT_CLASS (klass)->finalize = tandem_connectivity_plugin_finalize;
  plugin_class->name = "connectivity";
  plugin_class->title = "Mobile network";
  plugin_class->incoming = connectivity_incoming;
  plugin_class->outgoing = connectivity_outgoing;
  plugin_class->handle_packet = connectivity_handle_packet;
  plugin_class->connected = connectivity_connected;
  plugin_class->get_state = connectivity_get_state;
}

static void
tandem_connectivity_plugin_init (TandemConnectivityPlugin *self)
{
}
