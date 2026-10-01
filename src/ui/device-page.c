/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "device-page.h"

void tandem_ping_plugin_send (TandemPlugin *plugin, const char *message);

struct _TandemDevicePage
{
  AdwNavigationPage parent_instance;

  TandemDevice *device;
  GtkWidget *banner;
  GtkWidget *status_row;
  GtkWidget *key_row;
  GtkWidget *pair_button;
  GtkWidget *unpair_row;
  GtkWidget *actions_group;
  GtkWidget *plugins_group;
  GtkWidget *plugin_groups_box;
};

G_DEFINE_FINAL_TYPE (TandemDevicePage, tandem_device_page, ADW_TYPE_NAVIGATION_PAGE)

static void
update (TandemDevicePage *self)
{
  TandemDevice *device = self->device;
  TandemPairState state = tandem_device_get_pair_state (device);
  gboolean reachable = tandem_device_is_reachable (device);
  g_autofree char *key = tandem_device_get_verification_key (device);

  adw_navigation_page_set_title (ADW_NAVIGATION_PAGE (self), tandem_device_get_name (device));
  adw_action_row_set_subtitle (ADW_ACTION_ROW (self->status_row),
                               reachable ? "Connected" : (tandem_device_is_paired (device) ? "Paired, not reachable" : "Not reachable"));
  adw_action_row_set_subtitle (ADW_ACTION_ROW (self->key_row), key != NULL ? key : "");
  gtk_widget_set_visible (self->key_row, state == TANDEM_PAIR_REQUESTED || state == TANDEM_PAIR_REQUESTED_BY_PEER);

  switch (state)
    {
    case TANDEM_PAIR_REQUESTED_BY_PEER:
      adw_banner_set_title (ADW_BANNER (self->banner), "This device asks to pair");
      adw_banner_set_button_label (ADW_BANNER (self->banner), "Accept");
      adw_banner_set_revealed (ADW_BANNER (self->banner), TRUE);
      break;
    case TANDEM_PAIR_REQUESTED:
      adw_banner_set_title (ADW_BANNER (self->banner), "Waiting for the device to accept");
      adw_banner_set_button_label (ADW_BANNER (self->banner), "Cancel");
      adw_banner_set_revealed (ADW_BANNER (self->banner), TRUE);
      break;
    case TANDEM_PAIR_NONE:
      adw_banner_set_title (ADW_BANNER (self->banner), "Not paired");
      adw_banner_set_button_label (ADW_BANNER (self->banner), reachable ? "Pair" : NULL);
      adw_banner_set_revealed (ADW_BANNER (self->banner), TRUE);
      break;
    default:
      adw_banner_set_revealed (ADW_BANNER (self->banner), FALSE);
      break;
    }

  gtk_widget_set_visible (self->unpair_row, tandem_device_is_paired (device));
  gtk_widget_set_visible (self->actions_group, tandem_device_is_paired (device));
  gtk_widget_set_sensitive (self->actions_group, reachable);
}

static void
banner_clicked_cb (AdwBanner *banner, gpointer user_data)
{
  TandemDevicePage *self = user_data;

  switch (tandem_device_get_pair_state (self->device))
    {
    case TANDEM_PAIR_REQUESTED_BY_PEER:
      tandem_device_accept_pair (self->device);
      break;
    case TANDEM_PAIR_REQUESTED:
      tandem_device_reject_pair (self->device);
      break;
    case TANDEM_PAIR_NONE:
      tandem_device_request_pair (self->device);
      break;
    default:
      break;
    }
}

static void
reject_clicked_cb (GtkButton *button, gpointer user_data)
{
  TandemDevicePage *self = user_data;

  tandem_device_reject_pair (self->device);
}

static void
unpair_activated_cb (AdwActionRow *row, gpointer user_data)
{
  TandemDevicePage *self = user_data;

  tandem_device_unpair (self->device);
}

static void
ping_activated_cb (AdwActionRow *row, gpointer user_data)
{
  TandemDevicePage *self = user_data;
  TandemPlugin *plugin = tandem_device_get_plugin (self->device, "ping");

  if (plugin != NULL)
    tandem_ping_plugin_send (plugin, NULL);
}

static void
plugin_switch_cb (GObject *row, GParamSpec *pspec, gpointer user_data)
{
  TandemDevicePage *self = user_data;
  const char *name = g_object_get_data (row, "plugin-name");
  gboolean active = adw_switch_row_get_active (ADW_SWITCH_ROW (row));

  if (tandem_device_plugin_is_enabled (self->device, name) != active)
    tandem_device_set_plugin_enabled (self->device, name, active);
}

static void
rebuild_plugin_groups (TandemDevicePage *self)
{
  GPtrArray *plugins = tandem_device_get_plugins (self->device);
  GtkWidget *child;

  while ((child = gtk_widget_get_first_child (self->plugin_groups_box)) != NULL)
    gtk_box_remove (GTK_BOX (self->plugin_groups_box), child);

  for (guint i = 0; i < plugins->len; i++)
    {
      TandemPlugin *plugin = g_ptr_array_index (plugins, i);
      TandemPluginClass *klass = TANDEM_PLUGIN_GET_CLASS (plugin);
      GtkWidget *group;

      if (klass->create_group == NULL)
        continue;
      group = klass->create_group (plugin);
      if (group != NULL)
        gtk_box_append (GTK_BOX (self->plugin_groups_box), group);
    }
}

static void
build_plugin_switches (TandemDevicePage *self)
{
  guint n_plugins;
  const TandemPluginInfo *registry = tandem_plugin_registry (&n_plugins);

  for (guint i = 0; i < n_plugins; i++)
    {
      GtkWidget *row = adw_switch_row_new ();
      TandemPluginClass *klass = g_type_class_ref (registry[i].get_type ());

      adw_preferences_row_set_title (ADW_PREFERENCES_ROW (row), klass->title != NULL ? klass->title : klass->name);
      adw_switch_row_set_active (ADW_SWITCH_ROW (row), tandem_device_plugin_is_enabled (self->device, registry[i].name));
      g_object_set_data (G_OBJECT (row), "plugin-name", (gpointer) registry[i].name);
      g_signal_connect (row, "notify::active", G_CALLBACK (plugin_switch_cb), self);
      adw_preferences_group_add (ADW_PREFERENCES_GROUP (self->plugins_group), row);
      g_type_class_unref (klass);
    }
}

static void
device_changed_cb (TandemDevice *device, gpointer user_data)
{
  update (TANDEM_DEVICE_PAGE (user_data));
}

static void
plugins_changed_cb (TandemDevice *device, gpointer user_data)
{
  rebuild_plugin_groups (TANDEM_DEVICE_PAGE (user_data));
}

static void
tandem_device_page_dispose (GObject *object)
{
  TandemDevicePage *self = TANDEM_DEVICE_PAGE (object);

  if (self->device != NULL)
    g_signal_handlers_disconnect_by_data (self->device, self);
  g_clear_object (&self->device);

  G_OBJECT_CLASS (tandem_device_page_parent_class)->dispose (object);
}

static void
tandem_device_page_class_init (TandemDevicePageClass *klass)
{
  G_OBJECT_CLASS (klass)->dispose = tandem_device_page_dispose;
}

static void
tandem_device_page_init (TandemDevicePage *self)
{
}

GtkWidget *
tandem_device_page_new (TandemDevice *device)
{
  TandemDevicePage *self = g_object_new (TANDEM_TYPE_DEVICE_PAGE, NULL);
  GtkWidget *toolbar = adw_toolbar_view_new ();
  GtkWidget *header = adw_header_bar_new ();
  GtkWidget *page = adw_preferences_page_new ();
  GtkWidget *status_group = adw_preferences_group_new ();
  GtkWidget *reject_button;
  GtkWidget *ping_row;
  GtkWidget *content = gtk_box_new (GTK_ORIENTATION_VERTICAL, 0);

  self->device = g_object_ref (device);

  self->banner = adw_banner_new ("");
  g_signal_connect (self->banner, "button-clicked", G_CALLBACK (banner_clicked_cb), self);

  self->status_row = adw_action_row_new ();
  adw_preferences_row_set_title (ADW_PREFERENCES_ROW (self->status_row), "Status");
  adw_preferences_group_add (ADW_PREFERENCES_GROUP (status_group), self->status_row);

  self->key_row = adw_action_row_new ();
  adw_preferences_row_set_title (ADW_PREFERENCES_ROW (self->key_row), "Verification key");
  gtk_widget_add_css_class (self->key_row, "monospace");
  reject_button = gtk_button_new_with_label ("Reject");
  gtk_widget_set_valign (reject_button, GTK_ALIGN_CENTER);
  g_signal_connect (reject_button, "clicked", G_CALLBACK (reject_clicked_cb), self);
  adw_action_row_add_suffix (ADW_ACTION_ROW (self->key_row), reject_button);
  adw_preferences_group_add (ADW_PREFERENCES_GROUP (status_group), self->key_row);

  self->unpair_row = adw_button_row_new ();
  adw_preferences_row_set_title (ADW_PREFERENCES_ROW (self->unpair_row), "Unpair");
  gtk_widget_add_css_class (self->unpair_row, "destructive-action");
  g_signal_connect (self->unpair_row, "activated", G_CALLBACK (unpair_activated_cb), self);
  adw_preferences_group_add (ADW_PREFERENCES_GROUP (status_group), self->unpair_row);
  adw_preferences_page_add (ADW_PREFERENCES_PAGE (page), ADW_PREFERENCES_GROUP (status_group));

  self->actions_group = adw_preferences_group_new ();
  adw_preferences_group_set_title (ADW_PREFERENCES_GROUP (self->actions_group), "Actions");
  ping_row = adw_button_row_new ();
  adw_preferences_row_set_title (ADW_PREFERENCES_ROW (ping_row), "Send ping");
  g_signal_connect (ping_row, "activated", G_CALLBACK (ping_activated_cb), self);
  adw_preferences_group_add (ADW_PREFERENCES_GROUP (self->actions_group), ping_row);
  adw_preferences_page_add (ADW_PREFERENCES_PAGE (page), ADW_PREFERENCES_GROUP (self->actions_group));

  self->plugin_groups_box = gtk_box_new (GTK_ORIENTATION_VERTICAL, 24);
  {
    GtkWidget *holder = adw_preferences_group_new ();

    adw_preferences_group_add (ADW_PREFERENCES_GROUP (holder), self->plugin_groups_box);
    adw_preferences_page_add (ADW_PREFERENCES_PAGE (page), ADW_PREFERENCES_GROUP (holder));
  }

  self->plugins_group = adw_preferences_group_new ();
  adw_preferences_group_set_title (ADW_PREFERENCES_GROUP (self->plugins_group), "Plugins");
  build_plugin_switches (self);
  adw_preferences_page_add (ADW_PREFERENCES_PAGE (page), ADW_PREFERENCES_GROUP (self->plugins_group));

  gtk_box_append (GTK_BOX (content), self->banner);
  gtk_box_append (GTK_BOX (content), page);
  gtk_widget_set_vexpand (page, TRUE);
  adw_toolbar_view_add_top_bar (ADW_TOOLBAR_VIEW (toolbar), header);
  adw_toolbar_view_set_content (ADW_TOOLBAR_VIEW (toolbar), content);
  adw_navigation_page_set_child (ADW_NAVIGATION_PAGE (self), toolbar);
  adw_navigation_page_set_tag (ADW_NAVIGATION_PAGE (self), tandem_device_get_id (device));

  g_signal_connect (device, "changed", G_CALLBACK (device_changed_cb), self);
  g_signal_connect (device, "plugins-changed", G_CALLBACK (plugins_changed_cb), self);
  update (self);
  rebuild_plugin_groups (self);
  return GTK_WIDGET (self);
}

TandemDevice *
tandem_device_page_get_device (TandemDevicePage *self)
{
  return self->device;
}
