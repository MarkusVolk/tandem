/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "gui.h"
#include "tandem-build.h"

#include <adwaita.h>

static void
download_dir_chosen_cb (GObject *source, GAsyncResult *result, gpointer user_data)
{
  GSettings *settings = user_data;
  g_autoptr(GFile) dir = gtk_file_dialog_select_folder_finish (GTK_FILE_DIALOG (source), result, NULL);

  if (dir != NULL)
    {
      g_autofree char *path = g_file_get_path (dir);

      g_settings_set_string (settings, "download-dir", path);
    }
}

static void
download_dir_activated_cb (AdwActionRow *row, gpointer user_data)
{
  g_autoptr(GtkFileDialog) dialog = gtk_file_dialog_new ();
  GtkRoot *root = gtk_widget_get_root (GTK_WIDGET (row));

  gtk_file_dialog_set_title (dialog, "Folder for received files");
  gtk_file_dialog_select_folder (dialog, GTK_IS_WINDOW (root) ? GTK_WINDOW (root) : NULL, NULL,
                                 download_dir_chosen_cb, user_data);
}

static gboolean
download_dir_to_label (GValue *value, GVariant *variant, gpointer user_data)
{
  const char *path = g_variant_get_string (variant, NULL);

  g_value_set_string (value, *path != '\0' ? path : g_get_user_special_dir (G_USER_DIRECTORY_DOWNLOAD));
  return TRUE;
}

static gboolean
choice_to_index (GValue *value, GVariant *variant, gpointer user_data)
{
  const char *const *choices = user_data;
  const char *current = g_variant_get_string (variant, NULL);

  for (guint i = 0; choices[i] != NULL; i++)
    if (g_str_equal (choices[i], current))
      {
        g_value_set_uint (value, i);
        return TRUE;
      }
  g_value_set_uint (value, 0);
  return TRUE;
}

static GVariant *
index_to_choice (const GValue *value, const GVariantType *type, gpointer user_data)
{
  const char *const *choices = user_data;
  guint index = g_value_get_uint (value);

  return g_variant_new_string (choices[index] != NULL ? choices[index] : choices[0]);
}

static GtkWidget *
combo_row (GSettings *settings, const char *key, const char *title, const char *const *choices, const char *const *labels)
{
  GtkWidget *row = adw_combo_row_new ();
  g_autoptr(GtkStringList) model = gtk_string_list_new (labels);

  adw_preferences_row_set_title (ADW_PREFERENCES_ROW (row), title);
  adw_combo_row_set_model (ADW_COMBO_ROW (row), G_LIST_MODEL (model));
  g_settings_bind_with_mapping (settings, key, row, "selected", G_SETTINGS_BIND_DEFAULT,
                                choice_to_index, index_to_choice, (gpointer) choices, NULL);
  return row;
}

GtkWidget *
tandem_gui_preferences_dialog_new (void)
{
  g_autoptr(GSettings) settings = g_settings_new ("de.flk.Tandem");
  AdwDialog *dialog = adw_preferences_dialog_new ();
  GtkWidget *page = adw_preferences_page_new ();
  GtkWidget *identity = adw_preferences_group_new ();
  GtkWidget *name_row = adw_entry_row_new ();
  GtkWidget *behaviour = adw_preferences_group_new ();
  GtkWidget *download_row = adw_action_row_new ();
  GtkWidget *download_label = gtk_label_new (NULL);
  GtkWidget *mdns_row = adw_switch_row_new ();
  GtkWidget *tray_row = adw_switch_row_new ();
  GtkWidget *passwords_row = adw_switch_row_new ();
  static const char *const types[] = { "desktop", "laptop", NULL };
  static const char *const type_labels[] = { "Desktop", "Laptop", NULL };
  static const char *const backends[] = { "auto", "portal", "wlr", "uinput", NULL };
  static const char *const backend_labels[] = { "Automatic", "RemoteDesktop portal", "wlroots protocols", "uinput", NULL };
  g_autofree char *self_id = tandem_gui_daemon_self_id (tandem_gui_daemon_get ());
  g_autofree char *id_text = g_strdup_printf ("Device id %s", self_id != NULL ? self_id : "unknown, tandemd not running");

  adw_dialog_set_title (ADW_DIALOG (dialog), "Preferences");
  adw_preferences_group_set_title (ADW_PREFERENCES_GROUP (identity), "This device");
  adw_preferences_group_set_description (ADW_PREFERENCES_GROUP (identity), id_text);
  adw_preferences_row_set_title (ADW_PREFERENCES_ROW (name_row), "Name shown to other devices");
  g_settings_bind (settings, "device-name", name_row, "text", G_SETTINGS_BIND_DEFAULT);
  adw_preferences_group_add (ADW_PREFERENCES_GROUP (identity), name_row);
  adw_preferences_group_add (ADW_PREFERENCES_GROUP (identity), combo_row (settings, "device-type", "Device type", types, type_labels));
  adw_preferences_page_add (ADW_PREFERENCES_PAGE (page), ADW_PREFERENCES_GROUP (identity));

  adw_preferences_group_set_title (ADW_PREFERENCES_GROUP (behaviour), "Behaviour");
  adw_preferences_row_set_title (ADW_PREFERENCES_ROW (download_row), "Folder for received files");
  gtk_widget_add_css_class (download_label, "dim-label");
  gtk_label_set_ellipsize (GTK_LABEL (download_label), PANGO_ELLIPSIZE_MIDDLE);
  g_settings_bind_with_mapping (settings, "download-dir", download_label, "label", G_SETTINGS_BIND_GET,
                                download_dir_to_label, NULL, NULL, NULL);
  adw_action_row_add_suffix (ADW_ACTION_ROW (download_row), download_label);
  adw_action_row_add_suffix (ADW_ACTION_ROW (download_row), gtk_image_new_from_icon_name ("folder-open-symbolic"));
  gtk_list_box_row_set_activatable (GTK_LIST_BOX_ROW (download_row), TRUE);
  g_signal_connect_object (download_row, "activated", G_CALLBACK (download_dir_activated_cb), settings, 0);
  adw_preferences_group_add (ADW_PREFERENCES_GROUP (behaviour), download_row);
  adw_preferences_row_set_title (ADW_PREFERENCES_ROW (mdns_row), "Discover devices with mDNS");
  adw_action_row_set_subtitle (ADW_ACTION_ROW (mdns_row), "Takes effect after a restart");
  g_settings_bind (settings, "mdns", mdns_row, "active", G_SETTINGS_BIND_DEFAULT);
  adw_preferences_group_add (ADW_PREFERENCES_GROUP (behaviour), mdns_row);
  adw_preferences_row_set_title (ADW_PREFERENCES_ROW (tray_row), "Show a tray icon");
  adw_action_row_set_subtitle (ADW_ACTION_ROW (tray_row), "Takes effect after a restart");
  g_settings_bind (settings, "show-tray", tray_row, "active", G_SETTINGS_BIND_DEFAULT);
  adw_preferences_group_add (ADW_PREFERENCES_GROUP (behaviour), tray_row);
  adw_preferences_group_add (ADW_PREFERENCES_GROUP (behaviour),
                             combo_row (settings, "input-backend", "Remote input backend", backends, backend_labels));
  adw_preferences_row_set_title (ADW_PREFERENCES_ROW (passwords_row), "Share clipboard contents marked as passwords");
  g_settings_bind (settings, "send-passwords", passwords_row, "active", G_SETTINGS_BIND_DEFAULT);
  adw_preferences_group_add (ADW_PREFERENCES_GROUP (behaviour), passwords_row);
  adw_preferences_page_add (ADW_PREFERENCES_PAGE (page), ADW_PREFERENCES_GROUP (behaviour));

  adw_preferences_dialog_add (ADW_PREFERENCES_DIALOG (dialog), ADW_PREFERENCES_PAGE (page));
  return GTK_WIDGET (dialog);
}

void
tandem_gui_show_about_dialog (GtkWidget *parent)
{
  const char *developers[] = { "Markus Volk", NULL };
  AdwDialog *about = adw_about_dialog_new ();

  adw_about_dialog_set_application_name (ADW_ABOUT_DIALOG (about), "Tandem");
  adw_about_dialog_set_application_icon (ADW_ABOUT_DIALOG (about), TANDEM_APP_ID);
  adw_about_dialog_set_version (ADW_ABOUT_DIALOG (about), TANDEM_VERSION);
  adw_about_dialog_set_developers (ADW_ABOUT_DIALOG (about), developers);
  adw_about_dialog_set_license_type (ADW_ABOUT_DIALOG (about), GTK_LICENSE_GPL_2_0);
  adw_about_dialog_set_website (ADW_ABOUT_DIALOG (about), "https://github.com/MarkusVolk/tandem");
  adw_about_dialog_set_comments (ADW_ABOUT_DIALOG (about), "A KDE Connect client for the GTK desktop");
  adw_dialog_present (about, parent);
}
