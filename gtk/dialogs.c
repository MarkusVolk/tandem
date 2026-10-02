/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "gui.h"

static void
files_chosen_cb (GObject *source, GAsyncResult *result, gpointer user_data)
{
  g_autoptr(TandemGuiDevice) device = user_data;
  g_autoptr(GListModel) files = gtk_file_dialog_open_multiple_finish (GTK_FILE_DIALOG (source), result, NULL);
  g_autoptr(GStrvBuilder) paths = g_strv_builder_new ();
  g_auto(GStrv) strv = NULL;
  g_autoptr(GVariantBuilder) args = tandem_gui_args ();
  guint n;

  if (files == NULL)
    return;
  n = g_list_model_get_n_items (files);
  for (guint i = 0; i < n; i++)
    {
      g_autoptr(GFile) file = g_list_model_get_item (files, i);
      g_autofree char *path = g_file_get_path (file);

      if (path != NULL)
        g_strv_builder_add (paths, path);
    }
  strv = g_strv_builder_end (paths);
  if (strv[0] == NULL)
    return;
  g_variant_builder_add (args, "{sv}", "paths", g_variant_new_strv ((const char *const *) strv, -1));
  tandem_gui_device_call_plugin (device, "share", "files", g_variant_builder_end (args));
}

void
tandem_gui_share_files (TandemGuiDevice *device, GtkWindow *parent)
{
  g_autoptr(GtkFileDialog) dialog = gtk_file_dialog_new ();
  g_autofree char *title = g_strdup_printf ("Send files to %s", tandem_gui_device_get_name (device));

  gtk_file_dialog_set_title (dialog, title);
  gtk_file_dialog_open_multiple (dialog, parent, NULL, files_chosen_cb, g_object_ref (device));
}

typedef struct
{
  TandemGuiDevice *device;
  char *id;
  GtkWidget *entry;
} ReplyData;

static void
reply_response_cb (AdwAlertDialog *dialog, const char *response, gpointer user_data)
{
  ReplyData *data = user_data;
  const char *text = gtk_editable_get_text (GTK_EDITABLE (data->entry));

  if (g_str_equal (response, "send") && *text != '\0')
    {
      g_autoptr(GVariantBuilder) args = tandem_gui_args ();

      g_variant_builder_add (args, "{sv}", "id", g_variant_new_string (data->id));
      g_variant_builder_add (args, "{sv}", "text", g_variant_new_string (text));
      tandem_gui_device_call_plugin (data->device, "notifications", "reply", g_variant_builder_end (args));
    }
  g_object_unref (data->device);
  g_free (data->id);
  g_free (data);
}

void
tandem_gui_reply_dialog (TandemGuiDevice *device, const char *id, const char *title, GtkWidget *parent)
{
  g_autofree char *heading = g_strdup_printf ("Reply to %s", tandem_gui_device_get_name (device));
  AdwDialog *dialog = adw_alert_dialog_new (heading, title);
  ReplyData *data = g_new0 (ReplyData, 1);

  data->device = g_object_ref (device);
  data->id = g_strdup (id);
  data->entry = gtk_entry_new ();
  gtk_entry_set_placeholder_text (GTK_ENTRY (data->entry), "Message");
  gtk_entry_set_activates_default (GTK_ENTRY (data->entry), TRUE);
  adw_alert_dialog_set_extra_child (ADW_ALERT_DIALOG (dialog), data->entry);
  adw_alert_dialog_add_responses (ADW_ALERT_DIALOG (dialog), "cancel", "Cancel", "send", "Send", NULL);
  adw_alert_dialog_set_response_appearance (ADW_ALERT_DIALOG (dialog), "send", ADW_RESPONSE_SUGGESTED);
  adw_alert_dialog_set_default_response (ADW_ALERT_DIALOG (dialog), "send");
  g_signal_connect (dialog, "response", G_CALLBACK (reply_response_cb), data);
  adw_dialog_present (dialog, parent);
}

typedef struct
{
  TandemGuiDevice *device;
  GtkWidget *area;
  double x;
  double y;
  gulong changed_id;
} Presenter;

static void
presenter_draw_cb (GtkDrawingArea *area, cairo_t *cr, int width, int height, gpointer user_data)
{
  Presenter *p = user_data;

  cairo_set_source_rgba (cr, 0.9, 0.1, 0.1, 0.85);
  cairo_arc (cr, p->x * width, p->y * height, 12, 0, 2 * G_PI);
  cairo_fill (cr);
}

static void
presenter_changed_cb (TandemGuiDevice *device, const char *plugin, gpointer user_data)
{
  GtkWindow *window = user_data;
  Presenter *p = g_object_get_data (G_OBJECT (window), "presenter");
  g_autoptr(GVariant) state = NULL;
  gboolean active = FALSE;

  if (!g_str_equal (plugin, "presenter"))
    return;
  state = tandem_gui_device_get_plugin_state (device, "presenter");
  if (state != NULL)
    {
      g_variant_lookup (state, "active", "b", &active);
      g_variant_lookup (state, "x", "d", &p->x);
      g_variant_lookup (state, "y", "d", &p->y);
    }
  if (!active)
    gtk_window_destroy (window);
  else
    gtk_widget_queue_draw (p->area);
}

static void
presenter_free (gpointer data)
{
  Presenter *p = data;

  g_clear_signal_handler (&p->changed_id, p->device);
  g_object_unref (p->device);
  g_free (p);
}

void
tandem_gui_presenter_show (GtkApplication *app, TandemGuiDevice *device)
{
  static GtkCssProvider *provider;
  GtkWidget *window;
  GdkSurface *surface;
  Presenter *p;

  if (provider == NULL)
    {
      provider = gtk_css_provider_new ();
      gtk_css_provider_load_from_string (provider, "window.tandem-presenter { background: transparent; }");
      gtk_style_context_add_provider_for_display (gdk_display_get_default (), GTK_STYLE_PROVIDER (provider),
                                                  GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    }
  p = g_new0 (Presenter, 1);
  p->device = g_object_ref (device);
  p->x = 0.5;
  p->y = 0.5;
  window = gtk_application_window_new (app);
  gtk_window_set_title (GTK_WINDOW (window), "Presenter");
  gtk_window_set_decorated (GTK_WINDOW (window), FALSE);
  gtk_widget_add_css_class (window, "tandem-presenter");
  p->area = gtk_drawing_area_new ();
  gtk_drawing_area_set_draw_func (GTK_DRAWING_AREA (p->area), presenter_draw_cb, p, NULL);
  gtk_widget_set_can_target (p->area, FALSE);
  gtk_widget_set_can_target (window, FALSE);
  gtk_window_set_child (GTK_WINDOW (window), p->area);
  g_object_set_data_full (G_OBJECT (window), "presenter", p, presenter_free);
  p->changed_id = g_signal_connect (device, "plugin-changed", G_CALLBACK (presenter_changed_cb), window);
  gtk_window_fullscreen (GTK_WINDOW (window));
  gtk_window_present (GTK_WINDOW (window));
  surface = gtk_native_get_surface (GTK_NATIVE (window));
  if (surface != NULL)
    {
      cairo_region_t *empty = cairo_region_create ();

      gdk_surface_set_input_region (surface, empty);
      cairo_region_destroy (empty);
    }
}
