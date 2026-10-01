/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "../device.h"
#include "../plugin.h"


#define TANDEM_TYPE_RUNCOMMAND_PLUGIN (tandem_runcommand_plugin_get_type ())
G_DECLARE_FINAL_TYPE (TandemRuncommandPlugin, tandem_runcommand_plugin, TANDEM, RUNCOMMAND_PLUGIN, TandemPlugin)

struct _TandemRuncommandPlugin
{
  TandemPlugin parent_instance;
};

G_DEFINE_FINAL_TYPE (TandemRuncommandPlugin, tandem_runcommand_plugin, TANDEM_TYPE_PLUGIN)

static const char *const runcommand_incoming[] = { "kdeconnect.runcommand.request", NULL };
static const char *const runcommand_outgoing[] = { "kdeconnect.runcommand", NULL };

static JsonObject *
parse_commands (const char *json)
{
  g_autoptr(JsonParser) parser = json_parser_new ();
  JsonNode *root;

  if (json != NULL && json_parser_load_from_data (parser, json, -1, NULL))
    {
      root = json_parser_get_root (parser);
      if (root != NULL && JSON_NODE_HOLDS_OBJECT (root))
        return json_object_ref (json_node_get_object (root));
    }
  return json_object_new ();
}

static void save_commands (TandemPlugin *plugin, JsonObject *commands);

static JsonObject *
load_commands (TandemPlugin *plugin)
{
  GSettings *settings = tandem_plugin_get_settings (plugin);
  g_autofree char *json = g_settings_get_string (settings, "commands");
  JsonObject *commands = parse_commands (json);

  if (json_object_get_size (commands) == 0 && !g_settings_get_boolean (settings, "commands-seeded"))
    {
      TandemDevice *device = tandem_plugin_get_device (plugin);
      g_autoptr(GSettings) global = g_settings_new ("de.flk.Tandem");
      g_autofree char *defaults = g_settings_get_string (global, "default-commands");

      json_object_unref (commands);
      commands = parse_commands (defaults);
      g_settings_set_boolean (settings, "commands-seeded", TRUE);
      if (json_object_get_size (commands) > 0)
        {
          g_debug ("runcommand: seeding %s with %u default commands", tandem_device_get_id (device),
                   json_object_get_size (commands));
          save_commands (plugin, commands);
        }
    }
  return commands;
}

static void
save_commands (TandemPlugin *plugin, JsonObject *commands)
{
  g_autoptr(JsonGenerator) generator = json_generator_new ();
  g_autoptr(JsonNode) root = json_node_init_object (json_node_alloc (), commands);
  g_autofree char *json = NULL;

  json_generator_set_root (generator, root);
  json = json_generator_to_data (generator, NULL);
  g_settings_set_string (tandem_plugin_get_settings (plugin), "commands", json);
}

static void
send_command_list (TandemPlugin *plugin)
{
  g_autoptr(JsonObject) commands = load_commands (plugin);
  g_autoptr(JsonGenerator) generator = json_generator_new ();
  g_autoptr(JsonNode) root = json_node_init_object (json_node_alloc (), commands);
  g_autofree char *json = NULL;
  g_autoptr(TandemPacket) packet = tandem_packet_new ("kdeconnect.runcommand");

  json_generator_set_root (generator, root);
  json = json_generator_to_data (generator, NULL);
  tandem_packet_set_string (packet, "commandList", json);
  tandem_packet_set_boolean (packet, "canAddCommand", TRUE);
  tandem_plugin_send (plugin, packet);
}

static void
run_command (TandemPlugin *plugin, const char *key)
{
  g_autoptr(JsonObject) commands = load_commands (plugin);
  JsonObject *entry;
  const char *command;
  g_autoptr(GError) error = NULL;
  const char *argv[] = { "/bin/sh", "-c", NULL, NULL };

  if (!json_object_has_member (commands, key) || !JSON_NODE_HOLDS_OBJECT (json_object_get_member (commands, key)))
    {
      g_warning ("runcommand: unknown command %s", key);
      return;
    }
  entry = json_object_get_object_member (commands, key);
  command = json_object_has_member (entry, "command") ? json_object_get_string_member (entry, "command") : NULL;
  if (command == NULL || *command == '\0')
    return;
  argv[2] = command;
  g_message ("runcommand: %s", command);
  if (!g_spawn_async (NULL, (char **) argv, NULL, G_SPAWN_SEARCH_PATH, NULL, NULL, NULL, &error))
    g_warning ("runcommand: %s failed: %s", command, error->message);
}

GVariant *
tandem_runcommand_plugin_list (TandemPlugin *plugin)
{
  g_autoptr(JsonObject) commands = load_commands (plugin);
  GVariantBuilder builder;
  JsonObjectIter iter;
  const char *key;
  JsonNode *node;

  g_variant_builder_init (&builder, G_VARIANT_TYPE ("a(sss)"));
  json_object_iter_init (&iter, commands);
  while (json_object_iter_next (&iter, &key, &node))
    {
      JsonObject *entry;

      if (!JSON_NODE_HOLDS_OBJECT (node))
        continue;
      entry = json_node_get_object (node);
      g_variant_builder_add (&builder, "(sss)", key,
                             json_object_has_member (entry, "name") ? json_object_get_string_member (entry, "name") : "",
                             json_object_has_member (entry, "command") ? json_object_get_string_member (entry, "command") : "");
    }
  return g_variant_builder_end (&builder);
}

void
tandem_runcommand_plugin_run (TandemPlugin *plugin, const char *key)
{
  run_command (plugin, key);
}

static void
runcommand_handle_packet (TandemPlugin *plugin, TandemPacket *packet)
{
  if (tandem_packet_has (packet, "key"))
    run_command (plugin, tandem_packet_get_string (packet, "key", ""));
  else if (tandem_packet_has (packet, "requestCommandList") || tandem_packet_has (packet, "setup"))
    send_command_list (plugin);
}

static void
runcommand_connected (TandemPlugin *plugin)
{
  send_command_list (plugin);
}

void
tandem_runcommand_plugin_add (TandemPlugin *plugin, const char *name, const char *command)
{
  g_autoptr(JsonObject) commands = load_commands (plugin);
  g_autofree char *key = g_uuid_string_random ();
  JsonObject *entry = json_object_new ();

  json_object_set_string_member (entry, "name", name);
  json_object_set_string_member (entry, "command", command);
  json_object_set_object_member (commands, key, entry);
  save_commands (plugin, commands);
  send_command_list (plugin);
  tandem_plugin_changed (plugin);
}

void
tandem_runcommand_plugin_remove (TandemPlugin *plugin, const char *key)
{
  g_autoptr(JsonObject) commands = load_commands (plugin);

  json_object_remove_member (commands, key);
  save_commands (plugin, commands);
  send_command_list (plugin);
  tandem_plugin_changed (plugin);
}

static GVariant *
runcommand_get_state (TandemPlugin *plugin)
{
  GVariantBuilder builder;

  g_variant_builder_init (&builder, G_VARIANT_TYPE ("a{sv}"));
  g_variant_builder_add (&builder, "{sv}", "commands", tandem_runcommand_plugin_list (plugin));
  return g_variant_builder_end (&builder);
}

static GVariant *
runcommand_call (TandemPlugin *plugin, const char *method, GVariant *args, GError **error)
{
  const char *key = NULL;
  const char *name = NULL;
  const char *command = NULL;

  tandem_plugin_args_lookup (args, "key", "&s", &key);
  tandem_plugin_args_lookup (args, "name", "&s", &name);
  tandem_plugin_args_lookup (args, "command", "&s", &command);
  if (g_str_equal (method, "add") && name != NULL && command != NULL)
    tandem_runcommand_plugin_add (plugin, name, command);
  else if (g_str_equal (method, "remove") && key != NULL)
    tandem_runcommand_plugin_remove (plugin, key);
  else if (g_str_equal (method, "run") && key != NULL)
    tandem_runcommand_plugin_run (plugin, key);
  else
    g_set_error (error, G_DBUS_ERROR, G_DBUS_ERROR_INVALID_ARGS, "Bad call %s", method);
  return NULL;
}

static void
tandem_runcommand_plugin_class_init (TandemRuncommandPluginClass *klass)
{
  TandemPluginClass *plugin_class = TANDEM_PLUGIN_CLASS (klass);

  plugin_class->name = "runcommand";
  plugin_class->title = "Run commands";
  plugin_class->incoming = runcommand_incoming;
  plugin_class->outgoing = runcommand_outgoing;
  plugin_class->handle_packet = runcommand_handle_packet;
  plugin_class->connected = runcommand_connected;
  plugin_class->get_state = runcommand_get_state;
  plugin_class->call = runcommand_call;
}

static void
tandem_runcommand_plugin_init (TandemRuncommandPlugin *self)
{
}
