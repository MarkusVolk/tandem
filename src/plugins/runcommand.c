/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "../device.h"
#include "../plugin.h"

#include <adwaita.h>

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
load_commands (TandemPlugin *plugin)
{
  g_autofree char *json = g_settings_get_string (tandem_plugin_get_settings (plugin), "commands");
  g_autoptr(JsonParser) parser = json_parser_new ();
  JsonNode *root;

  if (json_parser_load_from_data (parser, json, -1, NULL))
    {
      root = json_parser_get_root (parser);
      if (root != NULL && JSON_NODE_HOLDS_OBJECT (root))
        return json_object_ref (json_node_get_object (root));
    }
  return json_object_new ();
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

typedef struct
{
  TandemPlugin *plugin;
  GtkWidget *name_row;
  GtkWidget *command_row;
} AddDialog;

static void
add_response_cb (AdwAlertDialog *dialog, const char *response, gpointer user_data)
{
  AddDialog *data = user_data;
  const char *name = gtk_editable_get_text (GTK_EDITABLE (data->name_row));
  const char *command = gtk_editable_get_text (GTK_EDITABLE (data->command_row));

  if (g_str_equal (response, "add") && *name != '\0' && *command != '\0')
    tandem_runcommand_plugin_add (data->plugin, name, command);
  g_free (data);
}

static void
add_activated_cb (AdwButtonRow *row, gpointer user_data)
{
  TandemPlugin *plugin = user_data;
  AdwDialog *dialog = adw_alert_dialog_new ("Add command", "The device will be able to run this command here.");
  GtkWidget *group = adw_preferences_group_new ();
  AddDialog *data = g_new0 (AddDialog, 1);

  data->plugin = plugin;
  data->name_row = adw_entry_row_new ();
  adw_preferences_row_set_title (ADW_PREFERENCES_ROW (data->name_row), "Name");
  data->command_row = adw_entry_row_new ();
  adw_preferences_row_set_title (ADW_PREFERENCES_ROW (data->command_row), "Command");
  adw_preferences_group_add (ADW_PREFERENCES_GROUP (group), data->name_row);
  adw_preferences_group_add (ADW_PREFERENCES_GROUP (group), data->command_row);
  adw_alert_dialog_set_extra_child (ADW_ALERT_DIALOG (dialog), group);
  adw_alert_dialog_add_responses (ADW_ALERT_DIALOG (dialog), "cancel", "Cancel", "add", "Add", NULL);
  adw_alert_dialog_set_response_appearance (ADW_ALERT_DIALOG (dialog), "add", ADW_RESPONSE_SUGGESTED);
  adw_alert_dialog_set_default_response (ADW_ALERT_DIALOG (dialog), "add");
  g_signal_connect (dialog, "response", G_CALLBACK (add_response_cb), data);
  adw_dialog_present (dialog, GTK_WIDGET (row));
}

static void
remove_clicked_cb (GtkButton *button, gpointer user_data)
{
  TandemPlugin *plugin = user_data;
  const char *key = g_object_get_data (G_OBJECT (button), "command-key");

  tandem_runcommand_plugin_remove (plugin, key);
}

static void
fill_group (TandemPlugin *plugin, GtkWidget *group)
{
  g_autoptr(JsonObject) commands = load_commands (plugin);
  GList *members = json_object_get_members (commands);
  GtkWidget *add_row;
  GtkWidget *child;
  GtkWidget *listbox = g_object_get_data (G_OBJECT (group), "rows");

  if (listbox != NULL)
    {
      while ((child = gtk_widget_get_first_child (listbox)) != NULL)
        gtk_list_box_remove (GTK_LIST_BOX (listbox), child);
    }
  else
    {
      listbox = gtk_list_box_new ();
      gtk_list_box_set_selection_mode (GTK_LIST_BOX (listbox), GTK_SELECTION_NONE);
      gtk_widget_add_css_class (listbox, "boxed-list");
      g_object_set_data (G_OBJECT (group), "rows", listbox);
      adw_preferences_group_add (ADW_PREFERENCES_GROUP (group), listbox);
    }

  for (GList *l = members; l != NULL; l = l->next)
    {
      const char *key = l->data;
      JsonObject *entry = json_object_get_object_member (commands, key);
      GtkWidget *row = adw_action_row_new ();
      GtkWidget *remove = gtk_button_new_from_icon_name ("user-trash-symbolic");

      adw_preferences_row_set_title (ADW_PREFERENCES_ROW (row),
                                     json_object_has_member (entry, "name") ? json_object_get_string_member (entry, "name") : key);
      adw_action_row_set_subtitle (ADW_ACTION_ROW (row),
                                   json_object_has_member (entry, "command") ? json_object_get_string_member (entry, "command") : "");
      gtk_widget_set_valign (remove, GTK_ALIGN_CENTER);
      gtk_widget_add_css_class (remove, "flat");
      g_object_set_data_full (G_OBJECT (remove), "command-key", g_strdup (key), g_free);
      g_signal_connect_object (remove, "clicked", G_CALLBACK (remove_clicked_cb), plugin, 0);
      adw_action_row_add_suffix (ADW_ACTION_ROW (row), remove);
      gtk_list_box_append (GTK_LIST_BOX (listbox), row);
    }
  g_list_free (members);

  add_row = adw_button_row_new ();
  adw_preferences_row_set_title (ADW_PREFERENCES_ROW (add_row), "Add command");
  adw_button_row_set_start_icon_name (ADW_BUTTON_ROW (add_row), "list-add-symbolic");
  g_signal_connect_object (add_row, "activated", G_CALLBACK (add_activated_cb), plugin, 0);
  gtk_list_box_append (GTK_LIST_BOX (listbox), add_row);
}

static void
runcommand_changed_cb (TandemPlugin *plugin, gpointer user_data)
{
  fill_group (plugin, GTK_WIDGET (user_data));
}

static GtkWidget *
runcommand_create_group (TandemPlugin *plugin)
{
  GtkWidget *group = adw_preferences_group_new ();

  adw_preferences_group_set_title (ADW_PREFERENCES_GROUP (group), "Commands the device may run here");
  fill_group (plugin, group);
  g_signal_connect_object (plugin, "changed", G_CALLBACK (runcommand_changed_cb), group, 0);
  return group;
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
  plugin_class->create_group = runcommand_create_group;
}

static void
tandem_runcommand_plugin_init (TandemRuncommandPlugin *self)
{
}
