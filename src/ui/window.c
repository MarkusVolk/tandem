/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "window.h"
#include "../app.h"
#include "tandem-build.h"

struct _TandemWindow
{
  AdwApplicationWindow parent_instance;
};

G_DEFINE_FINAL_TYPE (TandemWindow, tandem_window, ADW_TYPE_APPLICATION_WINDOW)

static void
tandem_window_class_init (TandemWindowClass *klass)
{
}

static void
tandem_window_init (TandemWindow *self)
{
  GtkWidget *toolbar = adw_toolbar_view_new ();
  GtkWidget *header = adw_header_bar_new ();
  GtkWidget *menu_button = gtk_menu_button_new ();
  GtkWidget *status = adw_status_page_new ();
  g_autoptr(GMenu) menu = g_menu_new ();

  gtk_window_set_title (GTK_WINDOW (self), "Tandem");
  gtk_window_set_default_size (GTK_WINDOW (self), 820, 560);
  gtk_window_set_hide_on_close (GTK_WINDOW (self), TRUE);

  g_menu_append (menu, "Quit", "app.quit");
  gtk_menu_button_set_icon_name (GTK_MENU_BUTTON (menu_button), "open-menu-symbolic");
  gtk_menu_button_set_menu_model (GTK_MENU_BUTTON (menu_button), G_MENU_MODEL (menu));
  adw_header_bar_pack_end (ADW_HEADER_BAR (header), menu_button);
  adw_toolbar_view_add_top_bar (ADW_TOOLBAR_VIEW (toolbar), header);

  adw_status_page_set_icon_name (ADW_STATUS_PAGE (status), TANDEM_APP_ID "-symbolic");
  adw_status_page_set_title (ADW_STATUS_PAGE (status), "No Devices");
  adw_status_page_set_description (ADW_STATUS_PAGE (status),
                                   "Devices running KDE Connect on this network appear here");
  adw_toolbar_view_set_content (ADW_TOOLBAR_VIEW (toolbar), status);

  adw_application_window_set_content (ADW_APPLICATION_WINDOW (self), toolbar);
}

GtkWidget *
tandem_window_new (TandemApp *app)
{
  return g_object_new (TANDEM_TYPE_WINDOW, "application", app, NULL);
}
