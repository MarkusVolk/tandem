/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "sni.h"
#include "dbusmenu.h"
#include "tandem-build.h"

#define SNI_PATH "/StatusNotifierItem"
#define SNI_IFACE "org.kde.StatusNotifierItem"
#define WATCHER_NAME "org.kde.StatusNotifierWatcher"
#define WATCHER_PATH "/StatusNotifierWatcher"
#define WATCHER_IFACE "org.kde.StatusNotifierWatcher"

static const char sni_xml[] =
  "<node>"
  "  <interface name='org.kde.StatusNotifierItem'>"
  "    <property name='Category' type='s' access='read'/>"
  "    <property name='Id' type='s' access='read'/>"
  "    <property name='Title' type='s' access='read'/>"
  "    <property name='Status' type='s' access='read'/>"
  "    <property name='WindowId' type='i' access='read'/>"
  "    <property name='IconName' type='s' access='read'/>"
  "    <property name='IconThemePath' type='s' access='read'/>"
  "    <property name='OverlayIconName' type='s' access='read'/>"
  "    <property name='AttentionIconName' type='s' access='read'/>"
  "    <property name='ToolTip' type='(sa(iiay)ss)' access='read'/>"
  "    <property name='ItemIsMenu' type='b' access='read'/>"
  "    <property name='Menu' type='o' access='read'/>"
  "    <method name='Activate'><arg type='i' name='x' direction='in'/><arg type='i' name='y' direction='in'/></method>"
  "    <method name='SecondaryActivate'><arg type='i' name='x' direction='in'/><arg type='i' name='y' direction='in'/></method>"
  "    <method name='ContextMenu'><arg type='i' name='x' direction='in'/><arg type='i' name='y' direction='in'/></method>"
  "    <method name='Scroll'><arg type='i' name='delta' direction='in'/><arg type='s' name='orientation' direction='in'/></method>"
  "    <signal name='NewTitle'/>"
  "    <signal name='NewIcon'/>"
  "    <signal name='NewToolTip'/>"
  "    <signal name='NewStatus'><arg type='s' name='status'/></signal>"
  "  </interface>"
  "</node>";

struct _TandemSni
{
  GObject parent_instance;

  GDBusConnection *connection;
  TandemManager *manager;
  guint registration;
  guint watcher_watch;
  char *status;
  char *tooltip;
  TandemDbusMenu *menu;
};

enum {
  ACTIVATE,
  QUIT,
  N_SIGNALS
};

static guint signals[N_SIGNALS];

G_DEFINE_FINAL_TYPE (TandemSni, tandem_sni, G_TYPE_OBJECT)

static GDBusNodeInfo *sni_info;

static void
compute_state (TandemSni *self, char **status, char **tooltip)
{
  GListModel *devices = tandem_manager_get_devices (self->manager);
  guint n = g_list_model_get_n_items (devices);
  g_autoptr(GString) text = g_string_new (NULL);
  gboolean any_reachable = FALSE;

  for (guint i = 0; i < n; i++)
    {
      g_autoptr(TandemDevice) device = g_list_model_get_item (devices, i);

      if (!tandem_device_is_paired (device))
        continue;
      if (text->len > 0)
        g_string_append (text, "\n");
      g_string_append_printf (text, "%s: %s", tandem_device_get_name (device),
                              tandem_device_is_reachable (device) ? "connected" : "not reachable");
      if (tandem_device_is_reachable (device))
        any_reachable = TRUE;
    }
  *status = g_strdup (any_reachable ? "Active" : "Passive");
  *tooltip = text->len > 0 ? g_strdup (text->str) : g_strdup ("No paired devices");
}

static GVariant *
sni_get_property (GDBusConnection  *connection,
                  const char       *sender,
                  const char       *object_path,
                  const char       *interface_name,
                  const char       *property_name,
                  GError          **error,
                  gpointer          user_data)
{
  TandemSni *self = user_data;

  if (g_str_equal (property_name, "Category"))
    return g_variant_new_string ("Communications");
  if (g_str_equal (property_name, "Id"))
    return g_variant_new_string (TANDEM_APP_ID);
  if (g_str_equal (property_name, "Title"))
    return g_variant_new_string ("Tandem");
  if (g_str_equal (property_name, "Status"))
    return g_variant_new_string (self->status);
  if (g_str_equal (property_name, "WindowId"))
    return g_variant_new_int32 (0);
  if (g_str_equal (property_name, "IconName"))
    return g_variant_new_string (TANDEM_APP_ID "-symbolic");
  if (g_str_equal (property_name, "IconThemePath"))
    return g_variant_new_string ("");
  if (g_str_equal (property_name, "OverlayIconName") || g_str_equal (property_name, "AttentionIconName"))
    return g_variant_new_string ("");
  if (g_str_equal (property_name, "ToolTip"))
    {
      GVariantBuilder pixmaps;

      g_variant_builder_init (&pixmaps, G_VARIANT_TYPE ("a(iiay)"));
      return g_variant_new ("(sa(iiay)ss)", TANDEM_APP_ID "-symbolic", &pixmaps, "Tandem", self->tooltip);
    }
  if (g_str_equal (property_name, "ItemIsMenu"))
    return g_variant_new_boolean (FALSE);
  if (g_str_equal (property_name, "Menu"))
    return g_variant_new_object_path (TANDEM_MENU_PATH);
  g_set_error (error, G_DBUS_ERROR, G_DBUS_ERROR_UNKNOWN_PROPERTY, "Unknown property %s", property_name);
  return NULL;
}

static void
sni_method_call (GDBusConnection       *connection,
                 const char            *sender,
                 const char            *object_path,
                 const char            *interface_name,
                 const char            *method_name,
                 GVariant              *parameters,
                 GDBusMethodInvocation *invocation,
                 gpointer               user_data)
{
  TandemSni *self = user_data;

  if (g_str_equal (method_name, "Activate") || g_str_equal (method_name, "SecondaryActivate"))
    g_signal_emit (self, signals[ACTIVATE], 0);
  g_dbus_method_invocation_return_value (invocation, NULL);
}

static const GDBusInterfaceVTable sni_vtable = {
  .method_call = sni_method_call,
  .get_property = sni_get_property,
};

static void
register_cb (GObject *source, GAsyncResult *result, gpointer user_data)
{
  g_autoptr(GError) error = NULL;
  g_autoptr(GVariant) reply = g_dbus_connection_call_finish (G_DBUS_CONNECTION (source), result, &error);

  if (reply == NULL)
    g_debug ("status notifier registration failed: %s", error->message);
}

static void
watcher_appeared_cb (GDBusConnection *connection, const char *name, const char *owner, gpointer user_data)
{
  g_dbus_connection_call (connection, WATCHER_NAME, WATCHER_PATH, WATCHER_IFACE, "RegisterStatusNotifierItem",
                          g_variant_new ("(s)", g_dbus_connection_get_unique_name (connection)), NULL,
                          G_DBUS_CALL_FLAGS_NONE, 5000, NULL, register_cb, NULL);
}

static void
emit (TandemSni *self, const char *signal_name, GVariant *parameters)
{
  g_dbus_connection_emit_signal (self->connection, NULL, SNI_PATH, SNI_IFACE, signal_name, parameters, NULL);
}

void
tandem_sni_update (TandemSni *self)
{
  g_autofree char *status = NULL;
  g_autofree char *tooltip = NULL;

  compute_state (self, &status, &tooltip);
  if (g_strcmp0 (status, self->status) != 0)
    {
      g_free (self->status);
      self->status = g_steal_pointer (&status);
      emit (self, "NewStatus", g_variant_new ("(s)", self->status));
    }
  if (g_strcmp0 (tooltip, self->tooltip) != 0)
    {
      g_free (self->tooltip);
      self->tooltip = g_steal_pointer (&tooltip);
      emit (self, "NewToolTip", NULL);
    }
}

static void
manager_changed_cb (TandemManager *manager, TandemDevice *device, gpointer user_data)
{
  tandem_sni_update (TANDEM_SNI (user_data));
}

static void
tandem_sni_dispose (GObject *object)
{
  TandemSni *self = TANDEM_SNI (object);

  if (self->watcher_watch != 0)
    g_bus_unwatch_name (self->watcher_watch);
  self->watcher_watch = 0;
  if (self->registration != 0)
    g_dbus_connection_unregister_object (self->connection, self->registration);
  self->registration = 0;
  if (self->manager != NULL)
    g_signal_handlers_disconnect_by_data (self->manager, self);
  g_clear_object (&self->menu);
  g_clear_object (&self->manager);
  g_clear_object (&self->connection);

  G_OBJECT_CLASS (tandem_sni_parent_class)->dispose (object);
}

static void
tandem_sni_finalize (GObject *object)
{
  TandemSni *self = TANDEM_SNI (object);

  g_free (self->status);
  g_free (self->tooltip);
  G_OBJECT_CLASS (tandem_sni_parent_class)->finalize (object);
}

static void
tandem_sni_class_init (TandemSniClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->dispose = tandem_sni_dispose;
  object_class->finalize = tandem_sni_finalize;

  signals[ACTIVATE] = g_signal_new ("activate", TANDEM_TYPE_SNI, G_SIGNAL_RUN_LAST, 0, NULL, NULL, NULL,
                                    G_TYPE_NONE, 0);
  signals[QUIT] = g_signal_new ("quit", TANDEM_TYPE_SNI, G_SIGNAL_RUN_LAST, 0, NULL, NULL, NULL, G_TYPE_NONE, 0);
}

static void
tandem_sni_init (TandemSni *self)
{
}

static void
menu_show_cb (TandemSni *self)
{
  g_signal_emit (self, signals[ACTIVATE], 0);
}

static void
menu_quit_cb (TandemSni *self)
{
  g_signal_emit (self, signals[QUIT], 0);
}

TandemSni *
tandem_sni_new (GDBusConnection *connection, TandemManager *manager)
{
  TandemSni *self = g_object_new (TANDEM_TYPE_SNI, NULL);
  g_autoptr(GError) error = NULL;

  self->connection = g_object_ref (connection);
  self->manager = g_object_ref (manager);
  self->menu = tandem_dbus_menu_new (connection, manager);
  g_signal_connect_swapped (self->menu, "show", G_CALLBACK (menu_show_cb), self);
  g_signal_connect_swapped (self->menu, "quit", G_CALLBACK (menu_quit_cb), self);
  compute_state (self, &self->status, &self->tooltip);

  if (sni_info == NULL)
    sni_info = g_dbus_node_info_new_for_xml (sni_xml, NULL);
  self->registration = g_dbus_connection_register_object (connection, SNI_PATH, sni_info->interfaces[0],
                                                          &sni_vtable, self, NULL, &error);
  if (self->registration == 0)
    {
      g_warning ("cannot export the status notifier item: %s", error->message);
      return self;
    }
  self->watcher_watch = g_bus_watch_name_on_connection (connection, WATCHER_NAME, G_BUS_NAME_WATCHER_FLAGS_NONE,
                                                        watcher_appeared_cb, NULL, self, NULL);
  g_signal_connect (manager, "device-added", G_CALLBACK (manager_changed_cb), self);
  g_signal_connect (manager, "device-removed", G_CALLBACK (manager_changed_cb), self);
  g_signal_connect (manager, "device-changed", G_CALLBACK (manager_changed_cb), self);
  return self;
}
