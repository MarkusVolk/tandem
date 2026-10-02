/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "gui.h"
#include "tandem-build.h"

struct _TandemGuiWindow
{
  AdwApplicationWindow parent_instance;

  TandemGuiDaemon *daemon;
  GtkWidget *split;
  GtkWidget *list;
  GtkWidget *content;
  GtkWidget *empty_page;
  GtkWidget *empty_status;
  GtkSingleSelection *selection;
};

G_DEFINE_FINAL_TYPE (TandemGuiWindow, tandem_gui_window, ADW_TYPE_APPLICATION_WINDOW)

static const char *
device_icon (TandemGuiDevice *device)
{
  const char *type = tandem_gui_device_get_type_name (device);

  if (g_strcmp0 (type, "phone") == 0)
    return "phone-symbolic";
  if (g_strcmp0 (type, "tablet") == 0)
    return "tablet-symbolic";
  if (g_strcmp0 (type, "laptop") == 0)
    return "laptop-symbolic";
  if (g_strcmp0 (type, "tv") == 0)
    return "tv-symbolic";
  return "computer-symbolic";
}

static const char *
device_subtitle (TandemGuiDevice *device)
{
  const char *state = tandem_gui_device_get_state (device);

  if (g_str_equal (state, "paired-reachable"))
    return "Connected";
  if (g_str_equal (state, "paired"))
    return tandem_gui_device_is_paused (device) ? "Disconnected" : "Paired, not reachable";
  if (g_str_equal (state, "requested"))
    return "Pairing requested";
  if (g_str_equal (state, "requested-by-peer"))
    return "Wants to pair";
  return "Available";
}

static void
row_update (GtkWidget *row, TandemGuiDevice *device)
{
  gtk_image_set_from_icon_name (GTK_IMAGE (g_object_get_data (G_OBJECT (row), "image")), device_icon (device));
  gtk_label_set_text (GTK_LABEL (g_object_get_data (G_OBJECT (row), "title")), tandem_gui_device_get_name (device));
  gtk_label_set_text (GTK_LABEL (g_object_get_data (G_OBJECT (row), "subtitle")), device_subtitle (device));
}

static void
row_device_changed_cb (TandemGuiDevice *device, gpointer user_data)
{
  row_update (GTK_WIDGET (user_data), device);
}

static void
factory_setup_cb (GtkListItemFactory *factory, GtkListItem *item, gpointer user_data)
{
  GtkWidget *box = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 12);
  GtkWidget *image = gtk_image_new ();
  GtkWidget *labels = gtk_box_new (GTK_ORIENTATION_VERTICAL, 2);
  GtkWidget *title = gtk_label_new ("");
  GtkWidget *subtitle = gtk_label_new ("");

  gtk_widget_set_margin_top (box, 8);
  gtk_widget_set_margin_bottom (box, 8);
  gtk_widget_set_margin_start (box, 6);
  gtk_widget_set_margin_end (box, 6);
  gtk_image_set_icon_size (GTK_IMAGE (image), GTK_ICON_SIZE_LARGE);
  gtk_label_set_xalign (GTK_LABEL (title), 0);
  gtk_label_set_xalign (GTK_LABEL (subtitle), 0);
  gtk_label_set_ellipsize (GTK_LABEL (title), PANGO_ELLIPSIZE_END);
  gtk_widget_add_css_class (subtitle, "dim-label");
  gtk_widget_add_css_class (subtitle, "caption");
  gtk_box_append (GTK_BOX (labels), title);
  gtk_box_append (GTK_BOX (labels), subtitle);
  gtk_box_append (GTK_BOX (box), image);
  gtk_box_append (GTK_BOX (box), labels);
  g_object_set_data (G_OBJECT (box), "image", image);
  g_object_set_data (G_OBJECT (box), "title", title);
  g_object_set_data (G_OBJECT (box), "subtitle", subtitle);
  gtk_list_item_set_child (item, box);
}

static void
factory_bind_cb (GtkListItemFactory *factory, GtkListItem *item, gpointer user_data)
{
  GtkWidget *row = gtk_list_item_get_child (item);
  TandemGuiDevice *device = gtk_list_item_get_item (item);

  row_update (row, device);
  g_signal_connect (device, "changed", G_CALLBACK (row_device_changed_cb), row);
}

static void
factory_unbind_cb (GtkListItemFactory *factory, GtkListItem *item, gpointer user_data)
{
  g_signal_handlers_disconnect_by_func (gtk_list_item_get_item (item), row_device_changed_cb, gtk_list_item_get_child (item));
}

static void
show_device (TandemGuiWindow *self, TandemGuiDevice *device)
{
  AdwNavigationPage *page;

  if (device == NULL)
    {
      adw_navigation_view_replace (ADW_NAVIGATION_VIEW (self->content), (AdwNavigationPage *[]) { ADW_NAVIGATION_PAGE (self->empty_page) }, 1);
      return;
    }
  page = adw_navigation_view_find_page (ADW_NAVIGATION_VIEW (self->content), tandem_gui_device_get_id (device));
  if (page == NULL)
    page = ADW_NAVIGATION_PAGE (tandem_gui_device_page_new (device));
  adw_navigation_view_replace (ADW_NAVIGATION_VIEW (self->content), &page, 1);
  adw_navigation_split_view_set_show_content (ADW_NAVIGATION_SPLIT_VIEW (self->split), TRUE);
}

static void
selection_changed_cb (GObject *selection, GParamSpec *pspec, gpointer user_data)
{
  TandemGuiWindow *self = user_data;

  show_device (self, gtk_single_selection_get_selected_item (GTK_SINGLE_SELECTION (selection)));
}

static void
update_empty (TandemGuiWindow *self)
{
  gboolean running = tandem_gui_daemon_is_running (self->daemon);

  adw_status_page_set_title (ADW_STATUS_PAGE (self->empty_status), running ? "No Devices" : "Service not running");
  adw_status_page_set_description (ADW_STATUS_PAGE (self->empty_status),
                                   running ? "Devices running KDE Connect on this network appear in the sidebar"
                                           : "tandemd is not running; start it with systemctl --user start tandem");
}

static void
items_changed_cb (GListModel *model, guint position, guint removed, guint added, gpointer user_data)
{
  TandemGuiWindow *self = user_data;

  if (g_list_model_get_n_items (model) == 0)
    show_device (self, NULL);
}

static void
running_changed_cb (TandemGuiDaemon *daemon, gpointer user_data)
{
  update_empty (user_data);
}

void
tandem_gui_window_show_device (TandemGuiWindow *self, const char *id)
{
  GListModel *devices = tandem_gui_daemon_get_devices (self->daemon);
  guint n = g_list_model_get_n_items (devices);

  for (guint i = 0; i < n; i++)
    {
      g_autoptr(TandemGuiDevice) device = g_list_model_get_item (devices, i);

      if (g_str_equal (tandem_gui_device_get_id (device), id))
        {
          gtk_single_selection_set_selected (self->selection, i);
          return;
        }
    }
}

static void
tandem_gui_window_dispose (GObject *object)
{
  TandemGuiWindow *self = TANDEM_GUI_WINDOW (object);

  if (self->daemon != NULL)
    {
      g_signal_handlers_disconnect_by_data (tandem_gui_daemon_get_devices (self->daemon), self);
      g_signal_handlers_disconnect_by_data (self->daemon, self);
    }
  g_clear_object (&self->daemon);
  G_OBJECT_CLASS (tandem_gui_window_parent_class)->dispose (object);
}

static void
tandem_gui_window_class_init (TandemGuiWindowClass *klass)
{
  G_OBJECT_CLASS (klass)->dispose = tandem_gui_window_dispose;
}

static void
tandem_gui_window_init (TandemGuiWindow *self)
{
  gtk_window_set_title (GTK_WINDOW (self), "Tandem");
  gtk_window_set_default_size (GTK_WINDOW (self), 860, 600);
}

GtkWidget *
tandem_gui_window_new (GtkApplication *app)
{
  TandemGuiWindow *self = g_object_new (TANDEM_TYPE_GUI_WINDOW, "application", app, NULL);
  GListModel *devices;
  GtkWidget *sidebar_toolbar = adw_toolbar_view_new ();
  GtkWidget *sidebar_header = adw_header_bar_new ();
  GtkWidget *menu_button = gtk_menu_button_new ();
  GtkWidget *refresh_button = gtk_button_new_from_icon_name ("view-refresh-symbolic");
  GtkWidget *scrolled = gtk_scrolled_window_new ();
  GtkListItemFactory *factory = gtk_signal_list_item_factory_new ();
  GtkWidget *sidebar_page;
  GtkWidget *empty_toolbar = adw_toolbar_view_new ();
  g_autoptr(GMenu) menu = g_menu_new ();

  self->daemon = g_object_ref (tandem_gui_daemon_get ());
  devices = tandem_gui_daemon_get_devices (self->daemon);

  g_menu_append (menu, "Refresh", "app.refresh");
  g_menu_append (menu, "Preferences", "app.preferences");
  g_menu_append (menu, "About Tandem", "app.about");
  g_menu_append (menu, "Quit", "app.quit");
  gtk_menu_button_set_icon_name (GTK_MENU_BUTTON (menu_button), "open-menu-symbolic");
  gtk_menu_button_set_menu_model (GTK_MENU_BUTTON (menu_button), G_MENU_MODEL (menu));
  gtk_actionable_set_action_name (GTK_ACTIONABLE (refresh_button), "app.refresh");
  gtk_widget_set_tooltip_text (refresh_button, "Look for devices");
  adw_header_bar_pack_end (ADW_HEADER_BAR (sidebar_header), menu_button);
  adw_header_bar_pack_start (ADW_HEADER_BAR (sidebar_header), refresh_button);

  g_signal_connect (factory, "setup", G_CALLBACK (factory_setup_cb), self);
  g_signal_connect (factory, "bind", G_CALLBACK (factory_bind_cb), self);
  g_signal_connect (factory, "unbind", G_CALLBACK (factory_unbind_cb), self);
  self->selection = gtk_single_selection_new (g_object_ref (devices));
  gtk_single_selection_set_autoselect (self->selection, TRUE);
  self->list = gtk_list_view_new (GTK_SELECTION_MODEL (self->selection), factory);
  gtk_widget_add_css_class (self->list, "navigation-sidebar");
  gtk_scrolled_window_set_child (GTK_SCROLLED_WINDOW (scrolled), self->list);
  gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (scrolled), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
  adw_toolbar_view_add_top_bar (ADW_TOOLBAR_VIEW (sidebar_toolbar), sidebar_header);
  adw_toolbar_view_set_content (ADW_TOOLBAR_VIEW (sidebar_toolbar), scrolled);
  sidebar_page = GTK_WIDGET (adw_navigation_page_new (sidebar_toolbar, "Devices"));

  self->empty_status = adw_status_page_new ();
  adw_status_page_set_icon_name (ADW_STATUS_PAGE (self->empty_status), TANDEM_APP_ID "-symbolic");
  update_empty (self);
  adw_toolbar_view_add_top_bar (ADW_TOOLBAR_VIEW (empty_toolbar), adw_header_bar_new ());
  adw_toolbar_view_set_content (ADW_TOOLBAR_VIEW (empty_toolbar), self->empty_status);
  self->empty_page = GTK_WIDGET (adw_navigation_page_new (empty_toolbar, "Tandem"));
  self->content = adw_navigation_view_new ();
  adw_navigation_view_add (ADW_NAVIGATION_VIEW (self->content), ADW_NAVIGATION_PAGE (self->empty_page));

  self->split = adw_navigation_split_view_new ();
  adw_navigation_split_view_set_sidebar (ADW_NAVIGATION_SPLIT_VIEW (self->split), ADW_NAVIGATION_PAGE (sidebar_page));
  adw_navigation_split_view_set_content (ADW_NAVIGATION_SPLIT_VIEW (self->split),
                                         adw_navigation_page_new (self->content, "Device"));
  adw_navigation_split_view_set_min_sidebar_width (ADW_NAVIGATION_SPLIT_VIEW (self->split), 220);
  adw_application_window_set_content (ADW_APPLICATION_WINDOW (self), self->split);

  g_signal_connect (self->selection, "notify::selected-item", G_CALLBACK (selection_changed_cb), self);
  g_signal_connect (devices, "items-changed", G_CALLBACK (items_changed_cb), self);
  g_signal_connect (self->daemon, "running-changed", G_CALLBACK (running_changed_cb), self);
  show_device (self, gtk_single_selection_get_selected_item (self->selection));
  return GTK_WIDGET (self);
}
