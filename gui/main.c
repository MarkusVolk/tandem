/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "gui.h"
#include "tandem-build.h"

static GtkWindow *
main_window (GtkApplication *app)
{
  GList *windows = gtk_application_get_windows (app);

  for (GList *l = windows; l != NULL; l = l->next)
    if (TANDEM_IS_GUI_WINDOW (l->data))
      return l->data;
  return GTK_WINDOW (tandem_gui_window_new (app));
}

static void
activate_cb (GApplication *app, gpointer user_data)
{
  gtk_window_present (main_window (GTK_APPLICATION (app)));
}

static void
quit_activated (GSimpleAction *action, GVariant *parameter, gpointer user_data)
{
  g_application_quit (G_APPLICATION (user_data));
}

static void
refresh_activated (GSimpleAction *action, GVariant *parameter, gpointer user_data)
{
  tandem_gui_daemon_refresh (tandem_gui_daemon_get ());
}

static void
preferences_activated (GSimpleAction *action, GVariant *parameter, gpointer user_data)
{
  GtkWindow *window = main_window (GTK_APPLICATION (user_data));

  gtk_window_present (window);
  adw_dialog_present (ADW_DIALOG (tandem_gui_preferences_dialog_new ()), GTK_WIDGET (window));
}

static void
about_activated (GSimpleAction *action, GVariant *parameter, gpointer user_data)
{
  GtkWindow *window = main_window (GTK_APPLICATION (user_data));

  gtk_window_present (window);
  tandem_gui_show_about_dialog (GTK_WIDGET (window));
}

static void
share_activated (GSimpleAction *action, GVariant *parameter, gpointer user_data)
{
  TandemGuiDevice *device = tandem_gui_daemon_find (tandem_gui_daemon_get (), g_variant_get_string (parameter, NULL));

  if (device != NULL)
    tandem_gui_share_files (device, NULL);
}

static void
reply_activated (GSimpleAction *action, GVariant *parameter, gpointer user_data)
{
  const char *device_id, *id, *title;
  TandemGuiDevice *device;
  GtkWindow *window;

  g_variant_get (parameter, "(&s&s&s)", &device_id, &id, &title);
  device = tandem_gui_daemon_find (tandem_gui_daemon_get (), device_id);
  if (device == NULL)
    return;
  window = main_window (GTK_APPLICATION (user_data));
  gtk_window_present (window);
  tandem_gui_reply_dialog (device, id, title, GTK_WIDGET (window));
}

static void
presenter_activated (GSimpleAction *action, GVariant *parameter, gpointer user_data)
{
  TandemGuiDevice *device = tandem_gui_daemon_find (tandem_gui_daemon_get (), g_variant_get_string (parameter, NULL));

  if (device != NULL)
    tandem_gui_presenter_show (GTK_APPLICATION (user_data), device);
}

static void
show_device_activated (GSimpleAction *action, GVariant *parameter, gpointer user_data)
{
  GtkWindow *window = main_window (GTK_APPLICATION (user_data));

  tandem_gui_window_show_device (TANDEM_GUI_WINDOW (window), g_variant_get_string (parameter, NULL));
  gtk_window_present (window);
}

static const GActionEntry actions[] = {
  { "quit", quit_activated },
  { "refresh", refresh_activated },
  { "preferences", preferences_activated },
  { "about", about_activated },
  { "share", share_activated, "s" },
  { "reply", reply_activated, "(sss)" },
  { "presenter", presenter_activated, "s" },
  { "show-device", show_device_activated, "s" },
};

static void
startup_cb (GApplication *app, gpointer user_data)
{
  g_action_map_add_action_entries (G_ACTION_MAP (app), actions, G_N_ELEMENTS (actions), app);
  gtk_application_set_accels_for_action (GTK_APPLICATION (app), "app.quit", (const char *[]) { "<Control>q", NULL });
  tandem_gui_daemon_get ();
}

int
main (int argc, char **argv)
{
  g_autoptr(AdwApplication) app = adw_application_new (TANDEM_APP_ID, G_APPLICATION_DEFAULT_FLAGS);

  g_set_prgname ("tandem-gtk");
  g_signal_connect (app, "startup", G_CALLBACK (startup_cb), NULL);
  g_signal_connect (app, "activate", G_CALLBACK (activate_cb), NULL);
  return g_application_run (G_APPLICATION (app), argc, argv);
}
