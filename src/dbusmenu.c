/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "dbusmenu.h"
#include "device.h"
#include "plugins/plugins.h"
#include "clipboard.h"

#include <gtk/gtk.h>

#define MENU_IFACE "com.canonical.dbusmenu"

static const char menu_xml[] =
  "<node>"
  "  <interface name='com.canonical.dbusmenu'>"
  "    <property name='Version' type='u' access='read'/>"
  "    <property name='TextDirection' type='s' access='read'/>"
  "    <property name='Status' type='s' access='read'/>"
  "    <property name='IconThemePath' type='as' access='read'/>"
  "    <method name='GetLayout'>"
  "      <arg type='i' name='parentId' direction='in'/>"
  "      <arg type='i' name='recursionDepth' direction='in'/>"
  "      <arg type='as' name='propertyNames' direction='in'/>"
  "      <arg type='u' name='revision' direction='out'/>"
  "      <arg type='(ia{sv}av)' name='layout' direction='out'/>"
  "    </method>"
  "    <method name='GetGroupProperties'>"
  "      <arg type='ai' name='ids' direction='in'/>"
  "      <arg type='as' name='propertyNames' direction='in'/>"
  "      <arg type='a(ia{sv})' name='properties' direction='out'/>"
  "    </method>"
  "    <method name='GetProperty'>"
  "      <arg type='i' name='id' direction='in'/>"
  "      <arg type='s' name='name' direction='in'/>"
  "      <arg type='v' name='value' direction='out'/>"
  "    </method>"
  "    <method name='Event'>"
  "      <arg type='i' name='id' direction='in'/>"
  "      <arg type='s' name='eventId' direction='in'/>"
  "      <arg type='v' name='data' direction='in'/>"
  "      <arg type='u' name='timestamp' direction='in'/>"
  "    </method>"
  "    <method name='EventGroup'>"
  "      <arg type='a(isvu)' name='events' direction='in'/>"
  "      <arg type='ai' name='idErrors' direction='out'/>"
  "    </method>"
  "    <method name='AboutToShow'>"
  "      <arg type='i' name='id' direction='in'/>"
  "      <arg type='b' name='needUpdate' direction='out'/>"
  "    </method>"
  "    <method name='AboutToShowGroup'>"
  "      <arg type='ai' name='ids' direction='in'/>"
  "      <arg type='ai' name='updatesNeeded' direction='out'/>"
  "      <arg type='ai' name='idErrors' direction='out'/>"
  "    </method>"
  "    <signal name='ItemsPropertiesUpdated'>"
  "      <arg type='a(ia{sv})' name='updatedProps'/>"
  "      <arg type='a(ias)' name='removedProps'/>"
  "    </signal>"
  "    <signal name='LayoutUpdated'>"
  "      <arg type='u' name='revision'/>"
  "      <arg type='i' name='parent'/>"
  "    </signal>"
  "    <signal name='ItemActivationRequested'>"
  "      <arg type='i' name='id'/>"
  "      <arg type='u' name='timestamp'/>"
  "    </signal>"
  "  </interface>"
  "</node>";

typedef enum
{
  ACTION_NONE,
  ACTION_SHOW,
  ACTION_QUIT,
  ACTION_PING,
  ACTION_RING,
  ACTION_SHARE,
  ACTION_CLIPBOARD,
  ACTION_LOCK,
  ACTION_UNPAIR,
  ACTION_PAIR,
} Action;

typedef struct
{
  int id;
  char *label;
  char *icon;
  gboolean separator;
  gboolean enabled;
  Action action;
  TandemDevice *device;
  GPtrArray *children;
} Item;

struct _TandemDbusMenu
{
  GObject parent_instance;

  GDBusConnection *connection;
  TandemManager *manager;
  guint registration;
  guint32 revision;
  Item *root;
  GHashTable *by_id;
  int next_id;
};

G_DEFINE_FINAL_TYPE (TandemDbusMenu, tandem_dbus_menu, G_TYPE_OBJECT)

enum { SHOW, QUIT, N_SIGNALS };
static guint signals[N_SIGNALS];
static GDBusNodeInfo *menu_info;

static void
item_free (Item *item)
{
  g_free (item->label);
  g_free (item->icon);
  g_clear_object (&item->device);
  g_clear_pointer (&item->children, g_ptr_array_unref);
  g_free (item);
}

static Item *
add_item (TandemDbusMenu *self, Item *parent, const char *label, const char *icon, Action action, TandemDevice *device)
{
  Item *item = g_new0 (Item, 1);

  item->id = parent == NULL ? 0 : self->next_id++;
  item->label = g_strdup (label);
  item->icon = g_strdup (icon);
  item->enabled = TRUE;
  item->action = action;
  item->device = device != NULL ? g_object_ref (device) : NULL;
  item->children = g_ptr_array_new_with_free_func ((GDestroyNotify) item_free);
  if (parent != NULL)
    g_ptr_array_add (parent->children, item);
  g_hash_table_insert (self->by_id, GINT_TO_POINTER (item->id), item);
  return item;
}

static void
add_separator (TandemDbusMenu *self, Item *parent)
{
  Item *item = add_item (self, parent, NULL, NULL, ACTION_NONE, NULL);

  item->separator = TRUE;
}

static void
rebuild (TandemDbusMenu *self)
{
  GListModel *devices = tandem_manager_get_devices (self->manager);
  guint n = g_list_model_get_n_items (devices);
  gboolean any = FALSE;

  g_hash_table_remove_all (self->by_id);
  g_clear_pointer (&self->root, item_free);
  self->next_id = 1;
  self->root = add_item (self, NULL, NULL, NULL, ACTION_NONE, NULL);

  for (guint i = 0; i < n; i++)
    {
      g_autoptr(TandemDevice) device = g_list_model_get_item (devices, i);
      Item *sub;
      g_autofree char *label = NULL;

      if (!tandem_device_is_paired (device) && !tandem_device_is_reachable (device))
        continue;
      any = TRUE;
      label = g_strdup_printf ("%s%s", tandem_device_get_name (device),
                               tandem_device_is_reachable (device) ? "" : " (offline)");
      sub = add_item (self, self->root, label,
                      g_str_equal (tandem_device_get_type_name (device), "phone") ? "phone-symbolic" : "computer-symbolic",
                      ACTION_NONE, device);
      if (!tandem_device_is_paired (device))
        {
          add_item (self, sub, "Pair", "insert-link-symbolic", ACTION_PAIR, device);
          continue;
        }
      if (tandem_device_is_reachable (device))
        {
          if (tandem_device_get_plugin (device, "ping") != NULL)
            add_item (self, sub, "Send ping", "network-transmit-symbolic", ACTION_PING, device);
          if (tandem_device_get_plugin (device, "findmyphone") != NULL)
            add_item (self, sub, "Ring", "audio-volume-high-symbolic", ACTION_RING, device);
          if (tandem_device_get_plugin (device, "share") != NULL)
            add_item (self, sub, "Send files…", "document-send-symbolic", ACTION_SHARE, device);
          if (tandem_device_get_plugin (device, "clipboard") != NULL)
            add_item (self, sub, "Send clipboard", "edit-paste-symbolic", ACTION_CLIPBOARD, device);
          if (tandem_device_get_plugin (device, "lock") != NULL)
            add_item (self, sub, "Lock", "system-lock-screen-symbolic", ACTION_LOCK, device);
          add_separator (self, sub);
        }
      add_item (self, sub, "Unpair", "edit-delete-symbolic", ACTION_UNPAIR, device);
    }
  if (!any)
    {
      Item *item = add_item (self, self->root, "No devices", NULL, ACTION_NONE, NULL);

      item->enabled = FALSE;
    }
  add_separator (self, self->root);
  add_item (self, self->root, "Open Tandem", "window-new-symbolic", ACTION_SHOW, NULL);
  add_item (self, self->root, "Quit", "application-exit-symbolic", ACTION_QUIT, NULL);

  self->revision++;
  g_dbus_connection_emit_signal (self->connection, NULL, TANDEM_MENU_PATH, MENU_IFACE, "LayoutUpdated",
                                 g_variant_new ("(ui)", self->revision, 0), NULL);
}

static void
item_properties (Item *item, GVariantBuilder *builder)
{
  if (item->separator)
    {
      g_variant_builder_add (builder, "{sv}", "type", g_variant_new_string ("separator"));
      return;
    }
  if (item->label != NULL)
    g_variant_builder_add (builder, "{sv}", "label", g_variant_new_string (item->label));
  if (item->icon != NULL)
    g_variant_builder_add (builder, "{sv}", "icon-name", g_variant_new_string (item->icon));
  g_variant_builder_add (builder, "{sv}", "enabled", g_variant_new_boolean (item->enabled));
  if (item->children->len > 0)
    g_variant_builder_add (builder, "{sv}", "children-display", g_variant_new_string ("submenu"));
}

static GVariant *
layout_for (Item *item, int depth)
{
  GVariantBuilder props;
  GVariantBuilder children;

  g_variant_builder_init (&props, G_VARIANT_TYPE ("a{sv}"));
  item_properties (item, &props);
  g_variant_builder_init (&children, G_VARIANT_TYPE ("av"));
  if (depth != 0)
    {
      for (guint i = 0; i < item->children->len; i++)
        g_variant_builder_add (&children, "v", layout_for (g_ptr_array_index (item->children, i), depth > 0 ? depth - 1 : depth));
    }
  return g_variant_new ("(ia{sv}av)", item->id, &props, &children);
}

static void
files_chosen_cb (GObject *source, GAsyncResult *result, gpointer user_data)
{
  g_autoptr(TandemDevice) device = user_data;
  g_autoptr(GListModel) files = gtk_file_dialog_open_multiple_finish (GTK_FILE_DIALOG (source), result, NULL);
  TandemPlugin *plugin = tandem_device_get_plugin (device, "share");

  if (files != NULL && plugin != NULL)
    tandem_share_plugin_send_files (plugin, files);
}

static void
activate_item (TandemDbusMenu *self, Item *item)
{
  TandemPlugin *plugin;

  switch (item->action)
    {
    case ACTION_SHOW:
      g_signal_emit (self, signals[SHOW], 0);
      break;
    case ACTION_QUIT:
      g_signal_emit (self, signals[QUIT], 0);
      break;
    case ACTION_PING:
      if ((plugin = tandem_device_get_plugin (item->device, "ping")) != NULL)
        tandem_ping_plugin_send (plugin, NULL);
      break;
    case ACTION_RING:
      if ((plugin = tandem_device_get_plugin (item->device, "findmyphone")) != NULL)
        tandem_findmyphone_plugin_ring (plugin);
      break;
    case ACTION_SHARE:
      {
        g_autoptr(GtkFileDialog) dialog = gtk_file_dialog_new ();

        gtk_file_dialog_set_title (dialog, "Send files");
        gtk_file_dialog_open_multiple (dialog, NULL, NULL, files_chosen_cb, g_object_ref (item->device));
      }
      break;
    case ACTION_CLIPBOARD:
      if ((plugin = tandem_device_get_plugin (item->device, "clipboard")) != NULL)
        tandem_clipboard_plugin_send (plugin, tandem_clipboard_get_text (tandem_clipboard_get ()));
      break;
    case ACTION_LOCK:
      if ((plugin = tandem_device_get_plugin (item->device, "lock")) != NULL)
        tandem_lock_plugin_set_remote (plugin, TRUE);
      break;
    case ACTION_UNPAIR:
      tandem_device_unpair (item->device);
      break;
    case ACTION_PAIR:
      tandem_device_request_pair (item->device);
      break;
    case ACTION_NONE:
      break;
    }
}

static void
menu_method_call (GDBusConnection       *connection,
                  const char            *sender,
                  const char            *object_path,
                  const char            *interface_name,
                  const char            *method_name,
                  GVariant              *parameters,
                  GDBusMethodInvocation *invocation,
                  gpointer               user_data)
{
  TandemDbusMenu *self = user_data;

  if (g_str_equal (method_name, "GetLayout"))
    {
      gint32 parent_id, depth;
      Item *item;

      g_variant_get (parameters, "(ii@as)", &parent_id, &depth, NULL);
      item = g_hash_table_lookup (self->by_id, GINT_TO_POINTER (parent_id));
      if (item == NULL)
        {
          g_dbus_method_invocation_return_error (invocation, G_DBUS_ERROR, G_DBUS_ERROR_INVALID_ARGS, "No item %d", parent_id);
          return;
        }
      g_dbus_method_invocation_return_value (invocation, g_variant_new ("(u@(ia{sv}av))", self->revision, layout_for (item, depth)));
    }
  else if (g_str_equal (method_name, "GetGroupProperties"))
    {
      g_autoptr(GVariant) ids = g_variant_get_child_value (parameters, 0);
      GVariantBuilder builder;
      gsize n = g_variant_n_children (ids);

      g_variant_builder_init (&builder, G_VARIANT_TYPE ("a(ia{sv})"));
      for (gsize i = 0; i < n; i++)
        {
          gint32 id;
          Item *item;
          GVariantBuilder props;

          g_variant_get_child (ids, i, "i", &id);
          item = g_hash_table_lookup (self->by_id, GINT_TO_POINTER (id));
          if (item == NULL)
            continue;
          g_variant_builder_init (&props, G_VARIANT_TYPE ("a{sv}"));
          item_properties (item, &props);
          g_variant_builder_add (&builder, "(ia{sv})", id, &props);
        }
      g_dbus_method_invocation_return_value (invocation, g_variant_new ("(a(ia{sv}))", &builder));
    }
  else if (g_str_equal (method_name, "GetProperty"))
    {
      gint32 id;
      const char *name;
      Item *item;

      g_variant_get (parameters, "(i&s)", &id, &name);
      item = g_hash_table_lookup (self->by_id, GINT_TO_POINTER (id));
      if (item != NULL && g_str_equal (name, "label"))
        g_dbus_method_invocation_return_value (invocation, g_variant_new ("(v)", g_variant_new_string (item->label != NULL ? item->label : "")));
      else if (item != NULL && g_str_equal (name, "enabled"))
        g_dbus_method_invocation_return_value (invocation, g_variant_new ("(v)", g_variant_new_boolean (item->enabled)));
      else
        g_dbus_method_invocation_return_error (invocation, G_DBUS_ERROR, G_DBUS_ERROR_INVALID_ARGS, "No property %s", name);
    }
  else if (g_str_equal (method_name, "Event"))
    {
      gint32 id;
      const char *event;
      Item *item;

      g_variant_get (parameters, "(i&svu)", &id, &event, NULL, NULL);
      item = g_hash_table_lookup (self->by_id, GINT_TO_POINTER (id));
      if (item != NULL && g_str_equal (event, "clicked"))
        activate_item (self, item);
      g_dbus_method_invocation_return_value (invocation, NULL);
    }
  else if (g_str_equal (method_name, "EventGroup"))
    {
      g_autoptr(GVariant) events = g_variant_get_child_value (parameters, 0);
      gsize n = g_variant_n_children (events);
      GVariantBuilder errors;

      g_variant_builder_init (&errors, G_VARIANT_TYPE ("ai"));
      for (gsize i = 0; i < n; i++)
        {
          gint32 id;
          const char *event;
          Item *item;

          g_variant_get_child (events, i, "(i&svu)", &id, &event, NULL, NULL);
          item = g_hash_table_lookup (self->by_id, GINT_TO_POINTER (id));
          if (item == NULL)
            g_variant_builder_add (&errors, "i", id);
          else if (g_str_equal (event, "clicked"))
            activate_item (self, item);
        }
      g_dbus_method_invocation_return_value (invocation, g_variant_new ("(ai)", &errors));
    }
  else if (g_str_equal (method_name, "AboutToShow"))
    {
      g_dbus_method_invocation_return_value (invocation, g_variant_new ("(b)", FALSE));
    }
  else if (g_str_equal (method_name, "AboutToShowGroup"))
    {
      GVariantBuilder a, b;

      g_variant_builder_init (&a, G_VARIANT_TYPE ("ai"));
      g_variant_builder_init (&b, G_VARIANT_TYPE ("ai"));
      g_dbus_method_invocation_return_value (invocation, g_variant_new ("(aiai)", &a, &b));
    }
  else
    {
      g_dbus_method_invocation_return_error (invocation, G_DBUS_ERROR, G_DBUS_ERROR_UNKNOWN_METHOD, "Unknown method %s", method_name);
    }
}

static GVariant *
menu_get_property (GDBusConnection  *connection,
                   const char       *sender,
                   const char       *object_path,
                   const char       *interface_name,
                   const char       *property_name,
                   GError          **error,
                   gpointer          user_data)
{
  if (g_str_equal (property_name, "Version"))
    return g_variant_new_uint32 (3);
  if (g_str_equal (property_name, "TextDirection"))
    return g_variant_new_string ("ltr");
  if (g_str_equal (property_name, "Status"))
    return g_variant_new_string ("normal");
  if (g_str_equal (property_name, "IconThemePath"))
    return g_variant_new_strv (NULL, 0);
  g_set_error (error, G_DBUS_ERROR, G_DBUS_ERROR_UNKNOWN_PROPERTY, "Unknown property %s", property_name);
  return NULL;
}

static const GDBusInterfaceVTable menu_vtable = {
  .method_call = menu_method_call,
  .get_property = menu_get_property,
};

static void
manager_changed_cb (TandemManager *manager, TandemDevice *device, gpointer user_data)
{
  rebuild (TANDEM_DBUS_MENU (user_data));
}

TandemDbusMenu *
tandem_dbus_menu_new (GDBusConnection *connection, TandemManager *manager)
{
  TandemDbusMenu *self = g_object_new (TANDEM_TYPE_DBUS_MENU, NULL);

  self->connection = g_object_ref (connection);
  self->manager = g_object_ref (manager);
  if (menu_info == NULL)
    menu_info = g_dbus_node_info_new_for_xml (menu_xml, NULL);
  self->registration = g_dbus_connection_register_object (connection, TANDEM_MENU_PATH, menu_info->interfaces[0],
                                                          &menu_vtable, self, NULL, NULL);
  g_signal_connect (manager, "device-added", G_CALLBACK (manager_changed_cb), self);
  g_signal_connect (manager, "device-removed", G_CALLBACK (manager_changed_cb), self);
  g_signal_connect (manager, "device-changed", G_CALLBACK (manager_changed_cb), self);
  rebuild (self);
  return self;
}

static void
tandem_dbus_menu_dispose (GObject *object)
{
  TandemDbusMenu *self = TANDEM_DBUS_MENU (object);

  if (self->manager != NULL)
    g_signal_handlers_disconnect_by_func (self->manager, manager_changed_cb, self);
  if (self->registration != 0)
    {
      g_dbus_connection_unregister_object (self->connection, self->registration);
      self->registration = 0;
    }
  g_clear_object (&self->manager);
  g_clear_object (&self->connection);
  G_OBJECT_CLASS (tandem_dbus_menu_parent_class)->dispose (object);
}

static void
tandem_dbus_menu_finalize (GObject *object)
{
  TandemDbusMenu *self = TANDEM_DBUS_MENU (object);

  g_clear_pointer (&self->root, item_free);
  g_hash_table_unref (self->by_id);
  G_OBJECT_CLASS (tandem_dbus_menu_parent_class)->finalize (object);
}

static void
tandem_dbus_menu_class_init (TandemDbusMenuClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->dispose = tandem_dbus_menu_dispose;
  object_class->finalize = tandem_dbus_menu_finalize;
  signals[SHOW] = g_signal_new ("show", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST, 0, NULL, NULL, NULL, G_TYPE_NONE, 0);
  signals[QUIT] = g_signal_new ("quit", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST, 0, NULL, NULL, NULL, G_TYPE_NONE, 0);
}

static void
tandem_dbus_menu_init (TandemDbusMenu *self)
{
  self->by_id = g_hash_table_new (g_direct_hash, g_direct_equal);
}
