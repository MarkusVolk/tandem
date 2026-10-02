/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "gui.h"

typedef struct
{
  const char *name;
  const char *title;
} PluginTitle;

static const PluginTitle titles[] = {
  { "ping", "Ping" },
  { "battery", "Battery" },
  { "connectivity", "Mobile network" },
  { "findmyphone", "Find my phone" },
  { "lock", "Lock" },
  { "runcommand", "Run commands" },
  { "notifications", "Notifications" },
  { "share", "Share" },
  { "mpriscontrol", "Media control" },
  { "systemvolume", "System volume" },
  { "mprisremote", "Remote media" },
  { "mousepad", "Remote input" },
  { "presenter", "Presenter" },
  { "contacts", "Contacts" },
  { "telephony", "Telephony" },
  { "sms", "Messages" },
  { "clipboard", "Clipboard" },
  { "sendnotifications", "Send notifications" },
  { "remoteinput", "Remote keyboard" },
};

static const char *plugin_names[G_N_ELEMENTS (titles) + 1];

const char *const *
tandem_gui_plugin_names (void)
{
  if (plugin_names[0] == NULL)
    for (guint i = 0; i < G_N_ELEMENTS (titles); i++)
      plugin_names[i] = titles[i].name;
  return plugin_names;
}

const char *
tandem_gui_plugin_title (const char *plugin)
{
  for (guint i = 0; i < G_N_ELEMENTS (titles); i++)
    if (g_str_equal (titles[i].name, plugin))
      return titles[i].title;
  return plugin;
}

typedef void (*RefreshFunc) (GtkWidget *group, TandemGuiDevice *device, GVariant *state);

static void
plugin_changed_cb (TandemGuiDevice *device, const char *plugin, gpointer user_data)
{
  GtkWidget *group = user_data;
  RefreshFunc refresh = g_object_get_data (G_OBJECT (group), "refresh");
  const char *name = g_object_get_data (G_OBJECT (group), "plugin");

  if (refresh != NULL && g_str_equal (plugin, name))
    {
      g_autoptr(GVariant) state = tandem_gui_device_get_plugin_state (device, plugin);

      if (state != NULL)
        refresh (group, device, state);
    }
}

static GtkWidget *
group_new (TandemGuiDevice *device, const char *plugin, const char *title, RefreshFunc refresh)
{
  GtkWidget *group = adw_preferences_group_new ();

  adw_preferences_group_set_title (ADW_PREFERENCES_GROUP (group), title);
  g_object_set_data (G_OBJECT (group), "plugin", (gpointer) plugin);
  g_object_set_data (G_OBJECT (group), "refresh", refresh);
  g_object_set_data_full (G_OBJECT (group), "device", g_object_ref (device), g_object_unref);
  if (refresh != NULL)
    g_signal_connect_object (device, "plugin-changed", G_CALLBACK (plugin_changed_cb), group, 0);
  return group;
}

static TandemGuiDevice *
group_device (GtkWidget *widget)
{
  GtkWidget *group = gtk_widget_get_ancestor (widget, ADW_TYPE_PREFERENCES_GROUP);

  return group != NULL ? g_object_get_data (G_OBJECT (group), "device") : NULL;
}

static GtkWidget *
listbox_in (GtkWidget *group)
{
  GtkWidget *listbox = g_object_get_data (G_OBJECT (group), "rows");

  if (listbox == NULL)
    {
      listbox = gtk_list_box_new ();
      gtk_list_box_set_selection_mode (GTK_LIST_BOX (listbox), GTK_SELECTION_NONE);
      gtk_widget_add_css_class (listbox, "boxed-list");
      adw_preferences_group_add (ADW_PREFERENCES_GROUP (group), listbox);
      g_object_set_data (G_OBJECT (group), "rows", listbox);
    }
  else
    {
      GtkWidget *child;

      while ((child = gtk_widget_get_first_child (listbox)) != NULL)
        gtk_list_box_remove (GTK_LIST_BOX (listbox), child);
    }
  return listbox;
}

static void
run_refresh (GtkWidget *group, TandemGuiDevice *device, const char *plugin)
{
  RefreshFunc refresh = g_object_get_data (G_OBJECT (group), "refresh");
  g_autoptr(GVariant) state = tandem_gui_device_get_plugin_state (device, plugin);

  if (refresh != NULL && state != NULL)
    refresh (group, device, state);
}

/* battery */

static void
battery_refresh (GtkWidget *group, TandemGuiDevice *device, GVariant *state)
{
  GtkWidget *row = g_object_get_data (G_OBJECT (group), "row");
  gint32 charge = -1;
  gboolean charging = FALSE;
  g_autofree char *text = NULL;
  const char *icon;

  g_variant_lookup (state, "charge", "i", &charge);
  g_variant_lookup (state, "charging", "b", &charging);
  if (charge < 0)
    {
      text = g_strdup ("Unknown");
      icon = "battery-missing-symbolic";
    }
  else
    {
      text = g_strdup_printf ("%d %%%s", charge, charging ? ", charging" : "");
      icon = charging ? "battery-good-charging-symbolic" : charge < 15 ? "battery-caution-symbolic"
                      : charge < 50 ? "battery-low-symbolic" : "battery-good-symbolic";
    }
  adw_action_row_set_subtitle (ADW_ACTION_ROW (row), text);
  gtk_image_set_from_icon_name (GTK_IMAGE (g_object_get_data (G_OBJECT (row), "icon")), icon);
}

static GtkWidget *
battery_group (TandemGuiDevice *device)
{
  GtkWidget *group = group_new (device, "battery", "Battery", battery_refresh);
  GtkWidget *row = adw_action_row_new ();
  GtkWidget *icon = gtk_image_new ();

  adw_preferences_row_set_title (ADW_PREFERENCES_ROW (row), "Charge");
  adw_action_row_add_prefix (ADW_ACTION_ROW (row), icon);
  g_object_set_data (G_OBJECT (row), "icon", icon);
  g_object_set_data (G_OBJECT (group), "row", row);
  adw_preferences_group_add (ADW_PREFERENCES_GROUP (group), row);
  return group;
}

/* connectivity */

static void
connectivity_refresh (GtkWidget *group, TandemGuiDevice *device, GVariant *state)
{
  const char *summary = "";

  g_variant_lookup (state, "summary", "&s", &summary);
  adw_action_row_set_subtitle (ADW_ACTION_ROW (g_object_get_data (G_OBJECT (group), "row")),
                               *summary != '\0' ? summary : "No signal");
}

static GtkWidget *
connectivity_group (TandemGuiDevice *device)
{
  GtkWidget *group = group_new (device, "connectivity", "Mobile network", connectivity_refresh);
  GtkWidget *row = adw_action_row_new ();

  adw_preferences_row_set_title (ADW_PREFERENCES_ROW (row), "Signal");
  g_object_set_data (G_OBJECT (group), "row", row);
  adw_preferences_group_add (ADW_PREFERENCES_GROUP (group), row);
  return group;
}

/* simple button rows */

static void
button_row_cb (AdwButtonRow *row, gpointer user_data)
{
  TandemGuiDevice *device = group_device (GTK_WIDGET (row));
  const char *plugin = g_object_get_data (G_OBJECT (row), "plugin");
  const char *method = g_object_get_data (G_OBJECT (row), "method");

  if (device != NULL)
    tandem_gui_device_call_plugin (device, plugin, method, NULL);
}

static GtkWidget *
button_row (const char *title, const char *icon, const char *plugin, const char *method)
{
  GtkWidget *row = adw_button_row_new ();

  adw_preferences_row_set_title (ADW_PREFERENCES_ROW (row), title);
  if (icon != NULL)
    adw_button_row_set_start_icon_name (ADW_BUTTON_ROW (row), icon);
  g_object_set_data (G_OBJECT (row), "plugin", (gpointer) plugin);
  g_object_set_data (G_OBJECT (row), "method", (gpointer) method);
  g_signal_connect (row, "activated", G_CALLBACK (button_row_cb), NULL);
  return row;
}

static GtkWidget *
findmyphone_group (TandemGuiDevice *device)
{
  GtkWidget *group = group_new (device, "findmyphone", "Find my phone", NULL);

  adw_preferences_group_add (ADW_PREFERENCES_GROUP (group),
                             button_row ("Ring the device", "audio-volume-high-symbolic", "findmyphone", "ring"));
  return group;
}

/* lock */

static void
lock_switch_cb (GObject *row, GParamSpec *pspec, gpointer user_data)
{
  TandemGuiDevice *device = group_device (GTK_WIDGET (row));
  g_autoptr(GVariantBuilder) args = tandem_gui_args ();

  if (device == NULL || g_object_get_data (row, "updating") != NULL)
    return;
  g_variant_builder_add (args, "{sv}", "locked", g_variant_new_boolean (adw_switch_row_get_active (ADW_SWITCH_ROW (row))));
  tandem_gui_device_call_plugin (device, "lock", "lock", g_variant_builder_end (args));
}

static void
lock_refresh (GtkWidget *group, TandemGuiDevice *device, GVariant *state)
{
  GtkWidget *row = g_object_get_data (G_OBJECT (group), "row");
  gboolean locked = FALSE;

  g_variant_lookup (state, "locked", "b", &locked);
  g_object_set_data (G_OBJECT (row), "updating", GINT_TO_POINTER (1));
  adw_switch_row_set_active (ADW_SWITCH_ROW (row), locked);
  g_object_set_data (G_OBJECT (row), "updating", NULL);
}

static GtkWidget *
lock_group (TandemGuiDevice *device)
{
  GtkWidget *group = group_new (device, "lock", "Lock", lock_refresh);
  GtkWidget *row = adw_switch_row_new ();

  adw_preferences_row_set_title (ADW_PREFERENCES_ROW (row), "Device locked");
  g_object_set_data (G_OBJECT (group), "row", row);
  g_signal_connect (row, "notify::active", G_CALLBACK (lock_switch_cb), NULL);
  adw_preferences_group_add (ADW_PREFERENCES_GROUP (group), row);
  return group;
}

/* runcommand */

typedef struct
{
  TandemGuiDevice *device;
  GtkWidget *name_row;
  GtkWidget *command_row;
} AddData;

static void
add_response_cb (AdwAlertDialog *dialog, const char *response, gpointer user_data)
{
  AddData *data = user_data;
  const char *name = gtk_editable_get_text (GTK_EDITABLE (data->name_row));
  const char *command = gtk_editable_get_text (GTK_EDITABLE (data->command_row));

  if (g_str_equal (response, "add") && *name != '\0' && *command != '\0')
    {
      g_autoptr(GVariantBuilder) args = tandem_gui_args ();

      g_variant_builder_add (args, "{sv}", "name", g_variant_new_string (name));
      g_variant_builder_add (args, "{sv}", "command", g_variant_new_string (command));
      tandem_gui_device_call_plugin (data->device, "runcommand", "add", g_variant_builder_end (args));
    }
  g_object_unref (data->device);
  g_free (data);
}

static void
add_activated_cb (AdwButtonRow *row, gpointer user_data)
{
  TandemGuiDevice *device = group_device (GTK_WIDGET (row));
  AdwDialog *dialog = adw_alert_dialog_new ("Add command", "The device will be able to run this command here.");
  GtkWidget *group = adw_preferences_group_new ();
  AddData *data = g_new0 (AddData, 1);

  data->device = g_object_ref (device);
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
  TandemGuiDevice *device = group_device (GTK_WIDGET (button));
  g_autoptr(GVariantBuilder) args = tandem_gui_args ();

  g_variant_builder_add (args, "{sv}", "key", g_variant_new_string (g_object_get_data (G_OBJECT (button), "key")));
  tandem_gui_device_call_plugin (device, "runcommand", "remove", g_variant_builder_end (args));
}

static void
runcommand_refresh (GtkWidget *group, TandemGuiDevice *device, GVariant *state)
{
  GtkWidget *listbox = listbox_in (group);
  g_autoptr(GVariant) commands = g_variant_lookup_value (state, "commands", G_VARIANT_TYPE ("a(sss)"));
  GVariantIter iter;
  const char *key, *name, *command;
  GtkWidget *add_row;

  if (commands != NULL)
    {
      g_variant_iter_init (&iter, commands);
      while (g_variant_iter_next (&iter, "(&s&s&s)", &key, &name, &command))
        {
          GtkWidget *row = adw_action_row_new ();
          GtkWidget *remove = gtk_button_new_from_icon_name ("user-trash-symbolic");

          adw_preferences_row_set_title (ADW_PREFERENCES_ROW (row), name);
          adw_action_row_set_subtitle (ADW_ACTION_ROW (row), command);
          gtk_widget_set_valign (remove, GTK_ALIGN_CENTER);
          gtk_widget_add_css_class (remove, "flat");
          g_object_set_data_full (G_OBJECT (remove), "key", g_strdup (key), g_free);
          g_signal_connect (remove, "clicked", G_CALLBACK (remove_clicked_cb), NULL);
          adw_action_row_add_suffix (ADW_ACTION_ROW (row), remove);
          gtk_list_box_append (GTK_LIST_BOX (listbox), row);
        }
    }
  add_row = adw_button_row_new ();
  adw_preferences_row_set_title (ADW_PREFERENCES_ROW (add_row), "Add command");
  adw_button_row_set_start_icon_name (ADW_BUTTON_ROW (add_row), "list-add-symbolic");
  g_signal_connect (add_row, "activated", G_CALLBACK (add_activated_cb), NULL);
  gtk_list_box_append (GTK_LIST_BOX (listbox), add_row);
}

static GtkWidget *
runcommand_group (TandemGuiDevice *device)
{
  return group_new (device, "runcommand", "Commands the device may run here", runcommand_refresh);
}

static GtkWidget *switch_row (GSettings *settings, const char *key, const char *title);

/* notifications */

static void
dismiss_clicked_cb (GtkButton *button, gpointer user_data)
{
  TandemGuiDevice *device = group_device (GTK_WIDGET (button));
  g_autoptr(GVariantBuilder) args = tandem_gui_args ();

  g_variant_builder_add (args, "{sv}", "id", g_variant_new_string (g_object_get_data (G_OBJECT (button), "id")));
  tandem_gui_device_call_plugin (device, "notifications", "dismiss", g_variant_builder_end (args));
}

static void
reply_clicked_cb (GtkButton *button, gpointer user_data)
{
  TandemGuiDevice *device = group_device (GTK_WIDGET (button));

  tandem_gui_reply_dialog (device, g_object_get_data (G_OBJECT (button), "id"),
                           g_object_get_data (G_OBJECT (button), "title"), GTK_WIDGET (button));
}

static void
expander_toggled_cb (GObject *expander, GParamSpec *pspec, gpointer user_data)
{
  g_object_set_data (G_OBJECT (user_data), "expanded",
                     adw_expander_row_get_expanded (ADW_EXPANDER_ROW (expander)) ? GINT_TO_POINTER (1) : NULL);
}

static void
notifications_refresh (GtkWidget *group, TandemGuiDevice *device, GVariant *state)
{
  GtkWidget *listbox = listbox_in (group);
  GtkWidget *expander = adw_expander_row_new ();
  g_autoptr(GVariant) list = g_variant_lookup_value (state, "notifications", G_VARIANT_TYPE ("aa{sv}"));
  GVariantIter iter;
  GVariant *item;
  guint count = 0;
  g_autofree char *title_text = NULL;
  gboolean expanded = g_object_get_data (G_OBJECT (group), "expanded") != NULL;

  if (list != NULL)
    {
      g_variant_iter_init (&iter, list);
      while ((item = g_variant_iter_next_value (&iter)) != NULL)
        {
          const char *id = "", *app = "", *title = "", *text = "";
          gboolean replyable = FALSE;
          GtkWidget *row = adw_action_row_new ();
          GtkWidget *dismiss = gtk_button_new_from_icon_name ("window-close-symbolic");
          g_autofree char *subtitle = NULL;

          g_variant_lookup (item, "id", "&s", &id);
          g_variant_lookup (item, "app", "&s", &app);
          g_variant_lookup (item, "title", "&s", &title);
          g_variant_lookup (item, "text", "&s", &text);
          g_variant_lookup (item, "replyable", "b", &replyable);
          subtitle = g_strdup (app);
          adw_preferences_row_set_title (ADW_PREFERENCES_ROW (row), *title != '\0' ? title : text);
          adw_preferences_row_set_use_markup (ADW_PREFERENCES_ROW (row), FALSE);
          adw_action_row_set_subtitle (ADW_ACTION_ROW (row), subtitle);
          adw_action_row_set_subtitle_lines (ADW_ACTION_ROW (row), 1);
          adw_action_row_set_title_lines (ADW_ACTION_ROW (row), 1);
          gtk_widget_set_tooltip_text (row, text);
          if (replyable)
            {
              GtkWidget *reply = gtk_button_new_from_icon_name ("mail-reply-sender-symbolic");

              gtk_widget_set_valign (reply, GTK_ALIGN_CENTER);
              gtk_widget_add_css_class (reply, "flat");
              g_object_set_data_full (G_OBJECT (reply), "id", g_strdup (id), g_free);
              g_object_set_data_full (G_OBJECT (reply), "title", g_strdup (title), g_free);
              g_signal_connect (reply, "clicked", G_CALLBACK (reply_clicked_cb), NULL);
              adw_action_row_add_suffix (ADW_ACTION_ROW (row), reply);
            }
          gtk_widget_set_valign (dismiss, GTK_ALIGN_CENTER);
          gtk_widget_add_css_class (dismiss, "flat");
          g_object_set_data_full (G_OBJECT (dismiss), "id", g_strdup (id), g_free);
          g_signal_connect (dismiss, "clicked", G_CALLBACK (dismiss_clicked_cb), NULL);
          adw_action_row_add_suffix (ADW_ACTION_ROW (row), dismiss);
          adw_expander_row_add_row (ADW_EXPANDER_ROW (expander), row);
          g_variant_unref (item);
          count++;
        }
    }
  title_text = count == 1 ? g_strdup ("1 notification") : g_strdup_printf ("%u notifications", count);
  adw_preferences_row_set_title (ADW_PREFERENCES_ROW (expander), title_text);
  adw_expander_row_set_enable_expansion (ADW_EXPANDER_ROW (expander), count > 0);
  adw_expander_row_set_expanded (ADW_EXPANDER_ROW (expander), expanded && count > 0);
  g_signal_connect_object (expander, "notify::expanded", G_CALLBACK (expander_toggled_cb), group, 0);
  gtk_list_box_append (GTK_LIST_BOX (listbox), expander);
}

static GtkWidget *
notifications_group (TandemGuiDevice *device)
{
  GtkWidget *group = group_new (device, "notifications", "Notifications", notifications_refresh);

  adw_preferences_group_add (ADW_PREFERENCES_GROUP (group),
                             switch_row (tandem_gui_device_get_settings (device), "show-notifications",
                                         "Show as desktop notifications"));
  return group;
}

/* share */

static void
send_files_cb (AdwButtonRow *row, gpointer user_data)
{
  GtkRoot *root = gtk_widget_get_root (GTK_WIDGET (row));

  tandem_gui_share_files (group_device (GTK_WIDGET (row)), GTK_IS_WINDOW (root) ? GTK_WINDOW (root) : NULL);
}

static void
clipboard_read_cb (GObject *source, GAsyncResult *result, gpointer user_data)
{
  g_autoptr(TandemGuiDevice) device = user_data;
  g_autofree char *text = gdk_clipboard_read_text_finish (GDK_CLIPBOARD (source), result, NULL);
  g_autoptr(GVariantBuilder) args = tandem_gui_args ();

  if (text == NULL || *text == '\0')
    return;
  g_variant_builder_add (args, "{sv}", "text", g_variant_new_string (text));
  tandem_gui_device_call_plugin (device, "share", "text", g_variant_builder_end (args));
}

static void
send_clipboard_cb (AdwButtonRow *row, gpointer user_data)
{
  gdk_clipboard_read_text_async (gtk_widget_get_clipboard (GTK_WIDGET (row)), NULL, clipboard_read_cb,
                                 g_object_ref (group_device (GTK_WIDGET (row))));
}

static gboolean
drop_cb (GtkDropTarget *target, const GValue *value, double x, double y, gpointer user_data)
{
  TandemGuiDevice *device = group_device (gtk_event_controller_get_widget (GTK_EVENT_CONTROLLER (target)));
  g_autoptr(GVariantBuilder) args = tandem_gui_args ();

  if (device == NULL)
    return FALSE;
  if (G_VALUE_HOLDS (value, GDK_TYPE_FILE_LIST))
    {
      GSList *list = g_value_get_boxed (value);
      g_autoptr(GStrvBuilder) paths = g_strv_builder_new ();
      g_auto(GStrv) strv = NULL;

      for (GSList *l = list; l != NULL; l = l->next)
        {
          g_autofree char *path = g_file_get_path (l->data);

          if (path != NULL)
            g_strv_builder_add (paths, path);
        }
      strv = g_strv_builder_end (paths);
      g_variant_builder_add (args, "{sv}", "paths", g_variant_new_strv ((const char *const *) strv, -1));
      tandem_gui_device_call_plugin (device, "share", "files", g_variant_builder_end (args));
      return TRUE;
    }
  if (G_VALUE_HOLDS_STRING (value))
    {
      g_variant_builder_add (args, "{sv}", "text", g_variant_new_string (g_value_get_string (value)));
      tandem_gui_device_call_plugin (device, "share", "text", g_variant_builder_end (args));
      return TRUE;
    }
  return FALSE;
}

static GtkWidget *
share_group (TandemGuiDevice *device)
{
  GtkWidget *group = group_new (device, "share", "Share", NULL);
  GtkWidget *files_row = adw_button_row_new ();
  GtkWidget *clipboard_row = adw_button_row_new ();
  GtkDropTarget *target = gtk_drop_target_new (G_TYPE_INVALID, GDK_ACTION_COPY);
  GType types[] = { GDK_TYPE_FILE_LIST, G_TYPE_STRING };

  adw_preferences_group_set_description (ADW_PREFERENCES_GROUP (group), "Files dropped here are sent to the device");
  adw_preferences_row_set_title (ADW_PREFERENCES_ROW (files_row), "Send files");
  adw_button_row_set_start_icon_name (ADW_BUTTON_ROW (files_row), "document-send-symbolic");
  g_signal_connect (files_row, "activated", G_CALLBACK (send_files_cb), NULL);
  adw_preferences_row_set_title (ADW_PREFERENCES_ROW (clipboard_row), "Send clipboard as text");
  adw_button_row_set_start_icon_name (ADW_BUTTON_ROW (clipboard_row), "edit-paste-symbolic");
  g_signal_connect (clipboard_row, "activated", G_CALLBACK (send_clipboard_cb), NULL);
  adw_preferences_group_add (ADW_PREFERENCES_GROUP (group), files_row);
  adw_preferences_group_add (ADW_PREFERENCES_GROUP (group), clipboard_row);
  gtk_drop_target_set_gtypes (target, types, G_N_ELEMENTS (types));
  g_signal_connect (target, "drop", G_CALLBACK (drop_cb), NULL);
  gtk_widget_add_controller (group, GTK_EVENT_CONTROLLER (target));
  return group;
}

/* clipboard */

static void
clipboard_refresh (GtkWidget *group, TandemGuiDevice *device, GVariant *state)
{
  const char *backend = "none";
  g_autofree char *text = NULL;

  g_variant_lookup (state, "backend", "&s", &backend);
  text = g_strdup_printf ("Clipboard access through %s", backend);
  adw_preferences_group_set_description (ADW_PREFERENCES_GROUP (group), text);
}

static GtkWidget *
clipboard_group (TandemGuiDevice *device)
{
  GtkWidget *group = group_new (device, "clipboard", "Clipboard", clipboard_refresh);
  GtkWidget *auto_row = adw_switch_row_new ();

  adw_preferences_row_set_title (ADW_PREFERENCES_ROW (auto_row), "Share automatically");
  g_settings_bind (tandem_gui_device_get_settings (device), "clipboard-auto", auto_row, "active", G_SETTINGS_BIND_DEFAULT);
  adw_preferences_group_add (ADW_PREFERENCES_GROUP (group), auto_row);
  adw_preferences_group_add (ADW_PREFERENCES_GROUP (group),
                             button_row ("Send clipboard now", "edit-paste-symbolic", "clipboard", "send"));
  return group;
}

/* sendnotifications, telephony: settings only */

static GtkWidget *
switch_row (GSettings *settings, const char *key, const char *title)
{
  GtkWidget *row = adw_switch_row_new ();

  adw_preferences_row_set_title (ADW_PREFERENCES_ROW (row), title);
  g_settings_bind (settings, key, row, "active", G_SETTINGS_BIND_DEFAULT);
  return row;
}

static GtkWidget *
sendnotifications_group (TandemGuiDevice *device)
{
  GtkWidget *group = group_new (device, "sendnotifications", "Desktop notifications", NULL);

  adw_preferences_group_add (ADW_PREFERENCES_GROUP (group),
                             switch_row (tandem_gui_device_get_settings (device), "send-notifications",
                                         "Forward notifications to the device"));
  return group;
}

static GtkWidget *
telephony_group (TandemGuiDevice *device)
{
  GtkWidget *group = group_new (device, "telephony", "Telephony", NULL);
  GSettings *settings = tandem_gui_device_get_settings (device);

  adw_preferences_group_add (ADW_PREFERENCES_GROUP (group), switch_row (settings, "pause-media-on-call", "Pause media during calls"));
  adw_preferences_group_add (ADW_PREFERENCES_GROUP (group), switch_row (settings, "mute-on-call", "Mute the speakers during calls"));
  return group;
}

/* remoteinput */

static void
keys_apply_cb (AdwEntryRow *row, gpointer user_data)
{
  TandemGuiDevice *device = group_device (GTK_WIDGET (row));
  const char *text = gtk_editable_get_text (GTK_EDITABLE (row));
  g_autoptr(GVariantBuilder) args = tandem_gui_args ();

  if (*text == '\0')
    return;
  g_variant_builder_add (args, "{sv}", "text", g_variant_new_string (text));
  tandem_gui_device_call_plugin (device, "remoteinput", "text", g_variant_builder_end (args));
  gtk_editable_set_text (GTK_EDITABLE (row), "");
}

static void
special_clicked_cb (GtkButton *button, gpointer user_data)
{
  TandemGuiDevice *device = group_device (GTK_WIDGET (button));
  g_autoptr(GVariantBuilder) args = tandem_gui_args ();

  g_variant_builder_add (args, "{sv}", "key",
                         g_variant_new_int32 (GPOINTER_TO_INT (g_object_get_data (G_OBJECT (button), "special"))));
  tandem_gui_device_call_plugin (device, "remoteinput", "special", g_variant_builder_end (args));
}

static GtkWidget *
special_button (const char *label, int special)
{
  GtkWidget *button = gtk_button_new_with_label (label);

  g_object_set_data (G_OBJECT (button), "special", GINT_TO_POINTER (special));
  g_signal_connect (button, "clicked", G_CALLBACK (special_clicked_cb), NULL);
  gtk_widget_set_valign (button, GTK_ALIGN_CENTER);
  return button;
}

static void
remoteinput_refresh (GtkWidget *group, TandemGuiDevice *device, GVariant *state)
{
  gboolean keyboard = FALSE;

  g_variant_lookup (state, "keyboard", "b", &keyboard);
  adw_preferences_group_set_description (ADW_PREFERENCES_GROUP (group),
                                         keyboard ? "The device accepts keyboard input" : "Open a text field on the device first");
}

static GtkWidget *
remoteinput_group (TandemGuiDevice *device)
{
  GtkWidget *group = group_new (device, "remoteinput", "Type on the device", remoteinput_refresh);
  GtkWidget *entry_row = adw_entry_row_new ();
  GtkWidget *keys_row = adw_action_row_new ();
  GtkWidget *keys_box = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 6);

  adw_preferences_row_set_title (ADW_PREFERENCES_ROW (entry_row), "Text to send");
  adw_entry_row_set_show_apply_button (ADW_ENTRY_ROW (entry_row), TRUE);
  g_signal_connect (entry_row, "apply", G_CALLBACK (keys_apply_cb), NULL);
  g_signal_connect (entry_row, "entry-activated", G_CALLBACK (keys_apply_cb), NULL);
  adw_preferences_group_add (ADW_PREFERENCES_GROUP (group), entry_row);
  adw_preferences_row_set_title (ADW_PREFERENCES_ROW (keys_row), "Keys");
  gtk_box_append (GTK_BOX (keys_box), special_button ("Backspace", 1));
  gtk_box_append (GTK_BOX (keys_box), special_button ("Enter", 12));
  gtk_box_append (GTK_BOX (keys_box), special_button ("Tab", 2));
  gtk_box_append (GTK_BOX (keys_box), special_button ("Esc", 14));
  adw_action_row_add_suffix (ADW_ACTION_ROW (keys_row), keys_box);
  adw_preferences_group_add (ADW_PREFERENCES_GROUP (group), keys_row);
  return group;
}

/* mousepad */

static void
mousepad_refresh (GtkWidget *group, TandemGuiDevice *device, GVariant *state)
{
  const char *backend = "none";
  gboolean keyboard = FALSE, pointer = FALSE;
  g_autofree char *text = NULL;

  g_variant_lookup (state, "backend", "&s", &backend);
  g_variant_lookup (state, "keyboard", "b", &keyboard);
  g_variant_lookup (state, "pointer", "b", &pointer);
  if (g_str_equal (backend, "none"))
    text = g_strdup ("No input backend available");
  else
    text = g_strdup_printf ("%s, keyboard %s, pointer %s", backend, keyboard ? "yes" : "no", pointer ? "yes" : "no");
  adw_action_row_set_subtitle (ADW_ACTION_ROW (g_object_get_data (G_OBJECT (group), "row")), text);
}

static GtkWidget *
mousepad_group (TandemGuiDevice *device)
{
  GtkWidget *group = group_new (device, "mousepad", "Remote input", mousepad_refresh);
  GtkWidget *row = adw_action_row_new ();

  adw_preferences_row_set_title (ADW_PREFERENCES_ROW (row), "Backend");
  g_object_set_data (G_OBJECT (group), "row", row);
  adw_preferences_group_add (ADW_PREFERENCES_GROUP (group), row);
  return group;
}

/* contacts */

static void
contacts_refresh (GtkWidget *group, TandemGuiDevice *device, GVariant *state)
{
  guint32 count = 0;
  gboolean syncing = FALSE;
  g_autofree char *text = NULL;

  g_variant_lookup (state, "count", "u", &count);
  g_variant_lookup (state, "syncing", "b", &syncing);
  text = g_strdup_printf ("%u contacts%s", count, syncing ? ", syncing" : "");
  adw_action_row_set_subtitle (ADW_ACTION_ROW (g_object_get_data (G_OBJECT (group), "row")), text);
}

static void
contacts_sync_cb (AdwActionRow *row, gpointer user_data)
{
  TandemGuiDevice *device = group_device (GTK_WIDGET (row));

  if (device != NULL)
    tandem_gui_device_call_plugin (device, "contacts", "sync", NULL);
}

static GtkWidget *
contacts_group (TandemGuiDevice *device)
{
  GtkWidget *group = group_new (device, "contacts", "Contacts", contacts_refresh);
  GtkWidget *row = adw_action_row_new ();

  adw_preferences_row_set_title (ADW_PREFERENCES_ROW (row), "Synchronise now");
  adw_action_row_add_suffix (ADW_ACTION_ROW (row), gtk_image_new_from_icon_name ("view-refresh-symbolic"));
  gtk_list_box_row_set_activatable (GTK_LIST_BOX_ROW (row), TRUE);
  g_signal_connect (row, "activated", G_CALLBACK (contacts_sync_cb), NULL);
  g_object_set_data (G_OBJECT (group), "row", row);
  adw_preferences_group_add (ADW_PREFERENCES_GROUP (group), row);
  return group;
}

/* sms */

static void
sms_activated_cb (AdwActionRow *row, gpointer user_data)
{
  GtkWidget *view = gtk_widget_get_ancestor (GTK_WIDGET (row), ADW_TYPE_NAVIGATION_VIEW);
  TandemGuiDevice *device = group_device (GTK_WIDGET (row));

  if (view != NULL && device != NULL)
    adw_navigation_view_push (ADW_NAVIGATION_VIEW (view), ADW_NAVIGATION_PAGE (tandem_gui_sms_page_new (device)));
}

static GtkWidget *
sms_group (TandemGuiDevice *device)
{
  GtkWidget *group = group_new (device, "sms", "Messages", NULL);
  GtkWidget *row = adw_action_row_new ();

  adw_preferences_row_set_title (ADW_PREFERENCES_ROW (row), "Open conversations");
  adw_action_row_add_suffix (ADW_ACTION_ROW (row), gtk_image_new_from_icon_name ("go-next-symbolic"));
  gtk_list_box_row_set_activatable (GTK_LIST_BOX_ROW (row), TRUE);
  g_signal_connect (row, "activated", G_CALLBACK (sms_activated_cb), NULL);
  adw_preferences_group_add (ADW_PREFERENCES_GROUP (group), row);
  return group;
}

GtkWidget *
tandem_gui_plugin_group_new (TandemGuiDevice *device, const char *plugin)
{
  static const struct { const char *name; GtkWidget *(*create) (TandemGuiDevice *); } table[] = {
    { "battery", battery_group },
    { "connectivity", connectivity_group },
    { "findmyphone", findmyphone_group },
    { "lock", lock_group },
    { "runcommand", runcommand_group },
    { "notifications", notifications_group },
    { "share", share_group },
    { "clipboard", clipboard_group },
    { "sendnotifications", sendnotifications_group },
    { "telephony", telephony_group },
    { "remoteinput", remoteinput_group },
    { "mousepad", mousepad_group },
    { "contacts", contacts_group },
    { "sms", sms_group },
  };

  for (guint i = 0; i < G_N_ELEMENTS (table); i++)
    {
      if (g_str_equal (table[i].name, plugin))
        {
          GtkWidget *group = table[i].create (device);

          run_refresh (group, device, plugin);
          return group;
        }
    }
  return NULL;
}
