/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "gui.h"

struct _TandemGuiDevicePage
{
  AdwNavigationPage parent_instance;

  TandemGuiDevice *device;
  GtkWidget *banner;
  GtkWidget *status_row;
  GtkWidget *key_row;
  GtkWidget *unpair_row;
  GtkWidget *connect_row;
  GtkWidget *actions_group;
  GtkWidget *ring_row;
  GtkWidget *plugins_group;
  GtkWidget *stack;
  GtkWidget *tab_boxes[5];
  char *shown_plugins;
};

typedef struct
{
  const char *name;
  const char *title;
  const char *icon;
} Tab;

static const Tab tabs[] = {
  { "device", "Device", "phone-symbolic" },
  { "messages", "Messages", "mail-unread-symbolic" },
  { "sharing", "Sharing", "document-send-symbolic" },
  { "control", "Control", "input-keyboard-symbolic" },
  { "plugins", "Plugins", "preferences-other-symbolic" },
};

static int
tab_for_plugin (const char *plugin)
{
  static const struct { const char *plugin; int tab; } map[] = {
    { "battery", 0 }, { "connectivity", 0 }, { "lock", 0 },
    { "sms", 1 }, { "notifications", 1 }, { "contacts", 1 }, { "telephony", 1 },
    { "share", 2 }, { "clipboard", 2 }, { "sendnotifications", 2 },
    { "remoteinput", 3 }, { "runcommand", 3 }, { "mousepad", 3 },
  };

  for (guint i = 0; i < G_N_ELEMENTS (map); i++)
    if (g_str_equal (map[i].plugin, plugin))
      return map[i].tab;
  return -1;
}

G_DEFINE_FINAL_TYPE (TandemGuiDevicePage, tandem_gui_device_page, ADW_TYPE_NAVIGATION_PAGE)

static void
update (TandemGuiDevicePage *self)
{
  TandemGuiDevice *device = self->device;
  const char *state = tandem_gui_device_get_state (device);
  gboolean reachable = tandem_gui_device_is_reachable (device);
  gboolean paired = tandem_gui_device_is_paired (device);
  g_autofree char *key = tandem_gui_device_get_key (device);

  adw_navigation_page_set_title (ADW_NAVIGATION_PAGE (self), tandem_gui_device_get_name (device));
  adw_action_row_set_subtitle (ADW_ACTION_ROW (self->status_row),
                               !paired ? (reachable ? "Available, not paired" : "Not reachable")
                               : reachable ? "Connected"
                               : tandem_gui_device_is_paused (device) ? "Disconnected" : "Paired, not reachable");
  adw_preferences_row_set_title (ADW_PREFERENCES_ROW (self->connect_row),
                                 tandem_gui_device_is_paused (device) ? "Connect" : "Disconnect");
  adw_button_row_set_start_icon_name (ADW_BUTTON_ROW (self->connect_row),
                                      tandem_gui_device_is_paused (device) ? "network-transmit-receive-symbolic" : "network-offline-symbolic");
  gtk_widget_set_visible (self->connect_row, paired);
  adw_action_row_set_subtitle (ADW_ACTION_ROW (self->key_row), key != NULL ? key : "");
  gtk_widget_set_visible (self->key_row, g_str_equal (state, "requested") || g_str_equal (state, "requested-by-peer"));

  if (g_str_equal (state, "requested-by-peer"))
    {
      adw_banner_set_title (ADW_BANNER (self->banner), "This device asks to pair");
      adw_banner_set_button_label (ADW_BANNER (self->banner), "Accept");
      adw_banner_set_revealed (ADW_BANNER (self->banner), TRUE);
    }
  else if (g_str_equal (state, "requested"))
    {
      adw_banner_set_title (ADW_BANNER (self->banner), "Waiting for the device to accept");
      adw_banner_set_button_label (ADW_BANNER (self->banner), "Cancel");
      adw_banner_set_revealed (ADW_BANNER (self->banner), TRUE);
    }
  else if (!paired)
    {
      adw_banner_set_title (ADW_BANNER (self->banner), "Not paired");
      adw_banner_set_button_label (ADW_BANNER (self->banner), reachable ? "Pair" : NULL);
      adw_banner_set_revealed (ADW_BANNER (self->banner), TRUE);
    }
  else
    adw_banner_set_revealed (ADW_BANNER (self->banner), FALSE);

  gtk_widget_set_visible (self->unpair_row, paired);
  gtk_widget_set_visible (self->actions_group, paired);
  gtk_widget_set_visible (self->ring_row, tandem_gui_device_has_plugin (device, "findmyphone"));
  gtk_widget_set_sensitive (self->actions_group, reachable);
}

static void
banner_clicked_cb (AdwBanner *banner, gpointer user_data)
{
  TandemGuiDevicePage *self = user_data;
  const char *state = tandem_gui_device_get_state (self->device);

  if (g_str_equal (state, "requested-by-peer"))
    tandem_gui_device_call (self->device, "AcceptPair", NULL);
  else if (g_str_equal (state, "requested"))
    tandem_gui_device_call (self->device, "RejectPair", NULL);
  else
    tandem_gui_device_call (self->device, "Pair", NULL);
}

static void
connect_activated_cb (AdwActionRow *row, gpointer user_data)
{
  TandemGuiDevice *device = TANDEM_GUI_DEVICE_PAGE (user_data)->device;

  tandem_gui_device_call (device, tandem_gui_device_is_paused (device) ? "Connect" : "Disconnect", NULL);
}

static void
unpair_activated_cb (AdwActionRow *row, gpointer user_data)
{
  tandem_gui_device_call (TANDEM_GUI_DEVICE_PAGE (user_data)->device, "Unpair", NULL);
}

static void
ring_activated_cb (AdwActionRow *row, gpointer user_data)
{
  tandem_gui_device_call_plugin (TANDEM_GUI_DEVICE_PAGE (user_data)->device, "findmyphone", "ring", NULL);
}

static void
ping_activated_cb (AdwActionRow *row, gpointer user_data)
{
  tandem_gui_device_call (TANDEM_GUI_DEVICE_PAGE (user_data)->device, "Ping", g_variant_new ("(s)", ""));
}

static void
plugin_switch_cb (GObject *row, GParamSpec *pspec, gpointer user_data)
{
  TandemGuiDevicePage *self = user_data;
  const char *name = g_object_get_data (row, "plugin-name");

  tandem_gui_device_call (self->device, "SetPluginEnabled",
                          g_variant_new ("(sb)", name, adw_switch_row_get_active (ADW_SWITCH_ROW (row))));
}

static void
rebuild_plugin_groups (TandemGuiDevicePage *self)
{
  g_auto(GStrv) plugins = tandem_gui_device_get_plugins (self->device);
  g_autofree char *joined = g_strjoinv (",", plugins);
  GtkWidget *child;

  if (g_strcmp0 (joined, self->shown_plugins) == 0)
    return;
  g_free (self->shown_plugins);
  self->shown_plugins = g_steal_pointer (&joined);

  for (guint t = 0; t + 1 < G_N_ELEMENTS (tabs); t++)
    {
      GtkWidget *keep = t == 0 ? gtk_widget_get_next_sibling (gtk_widget_get_first_child (self->tab_boxes[t])) : NULL;

      while ((child = gtk_widget_get_last_child (self->tab_boxes[t])) != NULL && child != keep)
        gtk_box_remove (GTK_BOX (self->tab_boxes[t]), child);
    }
  for (char **p = plugins; *p != NULL; p++)
    {
      int tab = tab_for_plugin (*p);
      GtkWidget *group = tab >= 0 ? tandem_gui_plugin_group_new (self->device, *p) : NULL;

      if (group != NULL)
        gtk_box_append (GTK_BOX (self->tab_boxes[tab]), group);
    }
  for (guint t = 1; t + 1 < G_N_ELEMENTS (tabs); t++)
    {
      AdwViewStackPage *page = adw_view_stack_get_page (ADW_VIEW_STACK (self->stack),
                                                        gtk_widget_get_ancestor (self->tab_boxes[t], ADW_TYPE_PREFERENCES_PAGE));

      adw_view_stack_page_set_visible (page, gtk_widget_get_first_child (self->tab_boxes[t]) != NULL);
    }
}

static void
build_plugin_switches (TandemGuiDevicePage *self)
{
  const char *const *names = tandem_gui_plugin_names ();
  g_auto(GStrv) disabled = g_settings_get_strv (tandem_gui_device_get_settings (self->device), "disabled-plugins");

  for (guint i = 0; names[i] != NULL; i++)
    {
      GtkWidget *row = adw_switch_row_new ();

      adw_preferences_row_set_title (ADW_PREFERENCES_ROW (row), tandem_gui_plugin_title (names[i]));
      adw_switch_row_set_active (ADW_SWITCH_ROW (row), !g_strv_contains ((const char *const *) disabled, names[i]));
      g_object_set_data (G_OBJECT (row), "plugin-name", (gpointer) names[i]);
      g_signal_connect (row, "notify::active", G_CALLBACK (plugin_switch_cb), self);
      adw_preferences_group_add (ADW_PREFERENCES_GROUP (self->plugins_group), row);
    }
}

static void
device_changed_cb (TandemGuiDevice *device, gpointer user_data)
{
  update (user_data);
  rebuild_plugin_groups (user_data);
}

static void
tandem_gui_device_page_dispose (GObject *object)
{
  TandemGuiDevicePage *self = TANDEM_GUI_DEVICE_PAGE (object);

  if (self->device != NULL)
    g_signal_handlers_disconnect_by_data (self->device, self);
  g_clear_object (&self->device);
  g_clear_pointer (&self->shown_plugins, g_free);
  G_OBJECT_CLASS (tandem_gui_device_page_parent_class)->dispose (object);
}

static void
tandem_gui_device_page_class_init (TandemGuiDevicePageClass *klass)
{
  G_OBJECT_CLASS (klass)->dispose = tandem_gui_device_page_dispose;
}

static void
tandem_gui_device_page_init (TandemGuiDevicePage *self)
{
}

static GtkWidget *
tab_page_new (TandemGuiDevicePage *self, guint tab)
{
  GtkWidget *page = adw_preferences_page_new ();
  GtkWidget *holder = adw_preferences_group_new ();

  self->tab_boxes[tab] = gtk_box_new (GTK_ORIENTATION_VERTICAL, 24);
  adw_preferences_group_add (ADW_PREFERENCES_GROUP (holder), self->tab_boxes[tab]);
  adw_preferences_page_add (ADW_PREFERENCES_PAGE (page), ADW_PREFERENCES_GROUP (holder));
  adw_view_stack_add_titled_with_icon (ADW_VIEW_STACK (self->stack), page, tabs[tab].name, tabs[tab].title, tabs[tab].icon);
  return page;
}

GtkWidget *
tandem_gui_device_page_new (TandemGuiDevice *device)
{
  TandemGuiDevicePage *self = g_object_new (TANDEM_TYPE_GUI_DEVICE_PAGE, "tag", tandem_gui_device_get_id (device), NULL);
  GtkWidget *toolbar = adw_toolbar_view_new ();
  GtkWidget *header = adw_header_bar_new ();
  GtkWidget *switcher = adw_view_switcher_new ();
  GtkWidget *switcher_bar = adw_view_switcher_bar_new ();
  GtkWidget *content = gtk_box_new (GTK_ORIENTATION_VERTICAL, 0);
  GtkWidget *status_group = adw_preferences_group_new ();
  GtkWidget *device_page;
  GtkWidget *plugins_page;
  GtkWidget *ping_row;

  self->device = g_object_ref (device);
  self->stack = adw_view_stack_new ();
  self->banner = adw_banner_new ("");
  g_signal_connect (self->banner, "button-clicked", G_CALLBACK (banner_clicked_cb), self);

  device_page = tab_page_new (self, 0);
  self->status_row = adw_action_row_new ();
  adw_preferences_row_set_title (ADW_PREFERENCES_ROW (self->status_row), "Status");
  adw_preferences_group_add (ADW_PREFERENCES_GROUP (status_group), self->status_row);
  self->key_row = adw_action_row_new ();
  adw_preferences_row_set_title (ADW_PREFERENCES_ROW (self->key_row), "Verification key");
  adw_action_row_set_subtitle_selectable (ADW_ACTION_ROW (self->key_row), TRUE);
  adw_preferences_group_add (ADW_PREFERENCES_GROUP (status_group), self->key_row);
  self->connect_row = adw_button_row_new ();
  g_signal_connect (self->connect_row, "activated", G_CALLBACK (connect_activated_cb), self);
  adw_preferences_group_add (ADW_PREFERENCES_GROUP (status_group), self->connect_row);
  self->unpair_row = adw_button_row_new ();
  adw_preferences_row_set_title (ADW_PREFERENCES_ROW (self->unpair_row), "Unpair");
  g_signal_connect (self->unpair_row, "activated", G_CALLBACK (unpair_activated_cb), self);
  adw_preferences_group_add (ADW_PREFERENCES_GROUP (status_group), self->unpair_row);
  gtk_box_prepend (GTK_BOX (self->tab_boxes[0]), status_group);
  self->actions_group = adw_preferences_group_new ();
  adw_preferences_group_set_title (ADW_PREFERENCES_GROUP (self->actions_group), "Actions");
  self->ring_row = adw_button_row_new ();
  adw_preferences_row_set_title (ADW_PREFERENCES_ROW (self->ring_row), "Ring the device");
  adw_button_row_set_start_icon_name (ADW_BUTTON_ROW (self->ring_row), "audio-volume-high-symbolic");
  g_signal_connect (self->ring_row, "activated", G_CALLBACK (ring_activated_cb), self);
  adw_preferences_group_add (ADW_PREFERENCES_GROUP (self->actions_group), self->ring_row);
  ping_row = adw_button_row_new ();
  adw_preferences_row_set_title (ADW_PREFERENCES_ROW (ping_row), "Send ping");
  adw_button_row_set_start_icon_name (ADW_BUTTON_ROW (ping_row), "network-transmit-symbolic");
  g_signal_connect (ping_row, "activated", G_CALLBACK (ping_activated_cb), self);
  adw_preferences_group_add (ADW_PREFERENCES_GROUP (self->actions_group), ping_row);
  gtk_box_insert_child_after (GTK_BOX (self->tab_boxes[0]), self->actions_group, status_group);
  g_object_set_data (G_OBJECT (device_page), "fixed", GINT_TO_POINTER (2));

  for (guint t = 1; t + 1 < G_N_ELEMENTS (tabs); t++)
    tab_page_new (self, t);

  plugins_page = tab_page_new (self, G_N_ELEMENTS (tabs) - 1);
  self->plugins_group = adw_preferences_group_new ();
  adw_preferences_group_set_title (ADW_PREFERENCES_GROUP (self->plugins_group), "Plugins");
  adw_preferences_group_set_description (ADW_PREFERENCES_GROUP (self->plugins_group),
                                         "Switched-off plugins are not offered to the device");
  build_plugin_switches (self);
  gtk_box_append (GTK_BOX (self->tab_boxes[G_N_ELEMENTS (tabs) - 1]), self->plugins_group);
  (void) plugins_page;

  adw_view_switcher_set_stack (ADW_VIEW_SWITCHER (switcher), ADW_VIEW_STACK (self->stack));
  adw_view_switcher_set_policy (ADW_VIEW_SWITCHER (switcher), ADW_VIEW_SWITCHER_POLICY_WIDE);
  adw_header_bar_set_title_widget (ADW_HEADER_BAR (header), switcher);
  adw_view_switcher_bar_set_stack (ADW_VIEW_SWITCHER_BAR (switcher_bar), ADW_VIEW_STACK (self->stack));

  gtk_box_append (GTK_BOX (content), self->banner);
  gtk_widget_set_vexpand (self->stack, TRUE);
  gtk_box_append (GTK_BOX (content), self->stack);
  adw_toolbar_view_add_top_bar (ADW_TOOLBAR_VIEW (toolbar), header);
  adw_toolbar_view_add_bottom_bar (ADW_TOOLBAR_VIEW (toolbar), switcher_bar);
  adw_toolbar_view_set_content (ADW_TOOLBAR_VIEW (toolbar), content);
  adw_navigation_page_set_child (ADW_NAVIGATION_PAGE (self), toolbar);

  g_signal_connect (device, "changed", G_CALLBACK (device_changed_cb), self);
  update (self);
  rebuild_plugin_groups (self);
  return GTK_WIDGET (self);
}
