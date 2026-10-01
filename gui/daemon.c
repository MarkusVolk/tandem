/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "daemon.h"

struct _TandemGuiDevice
{
  GObject parent_instance;

  char *id;
  char *path;
  GDBusProxy *proxy;
  GSettings *settings;
};

G_DEFINE_FINAL_TYPE (TandemGuiDevice, tandem_gui_device, G_TYPE_OBJECT)

enum { DEV_CHANGED, DEV_PLUGIN_CHANGED, N_DEV_SIGNALS };
static guint dev_signals[N_DEV_SIGNALS];

struct _TandemGuiDaemon
{
  GObject parent_instance;

  GDBusConnection *bus;
  GListStore *devices;
  guint name_watch;
  guint signal_subscription;
  gboolean running;
};

G_DEFINE_FINAL_TYPE (TandemGuiDaemon, tandem_gui_daemon, G_TYPE_OBJECT)

enum { RUNNING_CHANGED, N_SIGNALS };
static guint signals[N_SIGNALS];

static char *
object_path_for_id (const char *id)
{
  GString *path = g_string_new (TANDEM_GUI_DAEMON_PATH "/devices/d_");

  for (const char *p = id; *p != '\0'; p++)
    g_string_append_c (path, g_ascii_isalnum (*p) ? *p : '_');
  return g_string_free (path, FALSE);
}

static void
properties_changed_cb (GDBusProxy *proxy, GVariant *changed, GStrv invalidated, gpointer user_data)
{
  g_signal_emit (user_data, dev_signals[DEV_CHANGED], 0);
}

static void
device_signal_cb (GDBusProxy *proxy, const char *sender, const char *signal_name, GVariant *parameters, gpointer user_data)
{
  if (g_str_equal (signal_name, "PluginChanged"))
    {
      const char *plugin;

      g_variant_get (parameters, "(&s)", &plugin);
      g_signal_emit (user_data, dev_signals[DEV_PLUGIN_CHANGED], 0, plugin);
    }
}

static TandemGuiDevice *
device_new (GDBusConnection *bus, const char *id)
{
  TandemGuiDevice *self = g_object_new (TANDEM_TYPE_GUI_DEVICE, NULL);
  g_autoptr(GError) error = NULL;

  self->id = g_strdup (id);
  self->path = object_path_for_id (id);
  self->proxy = g_dbus_proxy_new_sync (bus, G_DBUS_PROXY_FLAGS_NONE, NULL, TANDEM_GUI_DAEMON_NAME, self->path,
                                       TANDEM_GUI_DEVICE_IFACE, NULL, &error);
  if (self->proxy == NULL)
    {
      g_warning ("tandem: no proxy for %s: %s", id, error->message);
      g_object_unref (self);
      return NULL;
    }
  g_signal_connect (self->proxy, "g-properties-changed", G_CALLBACK (properties_changed_cb), self);
  g_signal_connect (self->proxy, "g-signal", G_CALLBACK (device_signal_cb), self);
  return self;
}

static void
tandem_gui_device_dispose (GObject *object)
{
  TandemGuiDevice *self = TANDEM_GUI_DEVICE (object);

  if (self->proxy != NULL)
    g_signal_handlers_disconnect_by_data (self->proxy, self);
  g_clear_object (&self->proxy);
  g_clear_object (&self->settings);
  G_OBJECT_CLASS (tandem_gui_device_parent_class)->dispose (object);
}

static void
tandem_gui_device_finalize (GObject *object)
{
  TandemGuiDevice *self = TANDEM_GUI_DEVICE (object);

  g_free (self->id);
  g_free (self->path);
  G_OBJECT_CLASS (tandem_gui_device_parent_class)->finalize (object);
}

static void
tandem_gui_device_class_init (TandemGuiDeviceClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->dispose = tandem_gui_device_dispose;
  object_class->finalize = tandem_gui_device_finalize;
  dev_signals[DEV_CHANGED] = g_signal_new ("changed", TANDEM_TYPE_GUI_DEVICE, G_SIGNAL_RUN_LAST, 0, NULL, NULL, NULL,
                                           G_TYPE_NONE, 0);
  dev_signals[DEV_PLUGIN_CHANGED] = g_signal_new ("plugin-changed", TANDEM_TYPE_GUI_DEVICE, G_SIGNAL_RUN_LAST, 0,
                                                  NULL, NULL, NULL, G_TYPE_NONE, 1, G_TYPE_STRING);
}

static void
tandem_gui_device_init (TandemGuiDevice *self)
{
}

static const char *
prop_string (TandemGuiDevice *self, const char *name, const char *fallback)
{
  g_autoptr(GVariant) value = g_dbus_proxy_get_cached_property (self->proxy, name);
  const char *s = value != NULL ? g_variant_get_string (value, NULL) : NULL;

  return s != NULL ? g_intern_string (s) : fallback;
}

static gboolean
prop_bool (TandemGuiDevice *self, const char *name)
{
  g_autoptr(GVariant) value = g_dbus_proxy_get_cached_property (self->proxy, name);

  return value != NULL && g_variant_get_boolean (value);
}

const char *tandem_gui_device_get_id (TandemGuiDevice *self) { return self->id; }
const char *tandem_gui_device_get_name (TandemGuiDevice *self) { return prop_string (self, "Name", self->id); }
const char *tandem_gui_device_get_type_name (TandemGuiDevice *self) { return prop_string (self, "Type", "desktop"); }
const char *tandem_gui_device_get_state (TandemGuiDevice *self) { return prop_string (self, "PairState", "unpaired"); }
gboolean tandem_gui_device_is_paired (TandemGuiDevice *self) { return prop_bool (self, "Paired"); }
gboolean tandem_gui_device_is_reachable (TandemGuiDevice *self) { return prop_bool (self, "Reachable"); }
gboolean tandem_gui_device_is_paused (TandemGuiDevice *self) { return prop_bool (self, "Paused"); }

char *
tandem_gui_device_get_key (TandemGuiDevice *self)
{
  g_autoptr(GVariant) value = g_dbus_proxy_get_cached_property (self->proxy, "VerificationKey");

  return value != NULL ? g_variant_dup_string (value, NULL) : NULL;
}

GStrv
tandem_gui_device_get_plugins (TandemGuiDevice *self)
{
  g_autoptr(GVariant) value = g_dbus_proxy_get_cached_property (self->proxy, "Plugins");

  return value != NULL ? g_variant_dup_strv (value, NULL) : g_new0 (char *, 1);
}

gboolean
tandem_gui_device_has_plugin (TandemGuiDevice *self, const char *plugin)
{
  g_auto(GStrv) plugins = tandem_gui_device_get_plugins (self);

  return g_strv_contains ((const char *const *) plugins, plugin);
}

GSettings *
tandem_gui_device_get_settings (TandemGuiDevice *self)
{
  if (self->settings == NULL)
    {
      g_autofree char *path = g_strdup_printf ("/de/flk/Tandem/devices/%s/", self->id);

      self->settings = g_settings_new_with_path ("de.flk.Tandem.Device", path);
    }
  return self->settings;
}

static void
call_done_cb (GObject *source, GAsyncResult *result, gpointer user_data)
{
  g_autoptr(GError) error = NULL;
  g_autoptr(GVariant) reply = g_dbus_proxy_call_finish (G_DBUS_PROXY (source), result, &error);

  if (reply == NULL)
    g_warning ("tandem: %s", error->message);
}

void
tandem_gui_device_call (TandemGuiDevice *self, const char *method, GVariant *parameters)
{
  g_dbus_proxy_call (self->proxy, method, parameters, G_DBUS_CALL_FLAGS_NONE, 30000, NULL, call_done_cb, NULL);
}

GVariant *
tandem_gui_device_call_sync (TandemGuiDevice *self, const char *method, GVariant *parameters, GError **error)
{
  return g_dbus_proxy_call_sync (self->proxy, method, parameters, G_DBUS_CALL_FLAGS_NONE, 30000, NULL, error);
}

GVariant *
tandem_gui_device_get_plugin_state (TandemGuiDevice *self, const char *plugin)
{
  g_autoptr(GVariant) reply = g_dbus_proxy_call_sync (self->proxy, "GetPluginState", g_variant_new ("(s)", plugin),
                                                      G_DBUS_CALL_FLAGS_NONE, 5000, NULL, NULL);

  if (reply == NULL)
    return NULL;
  return g_variant_get_child_value (reply, 0);
}

GVariantBuilder *
tandem_gui_args (void)
{
  return g_variant_builder_new (G_VARIANT_TYPE ("a{sv}"));
}

void
tandem_gui_device_call_plugin (TandemGuiDevice *self, const char *plugin, const char *method, GVariant *args)
{
  if (args == NULL)
    {
      GVariantBuilder builder;

      g_variant_builder_init (&builder, G_VARIANT_TYPE ("a{sv}"));
      args = g_variant_builder_end (&builder);
    }
  tandem_gui_device_call (self, "CallPlugin", g_variant_new ("(ss@a{sv})", plugin, method, args));
}

GVariant *
tandem_gui_device_call_plugin_sync (TandemGuiDevice *self, const char *plugin, const char *method, GVariant *args, GError **error)
{
  g_autoptr(GVariant) reply = NULL;

  if (args == NULL)
    {
      GVariantBuilder builder;

      g_variant_builder_init (&builder, G_VARIANT_TYPE ("a{sv}"));
      args = g_variant_builder_end (&builder);
    }
  reply = tandem_gui_device_call_sync (self, "CallPlugin", g_variant_new ("(ss@a{sv})", plugin, method, args), error);
  if (reply == NULL)
    return NULL;
  return g_variant_get_child_value (reply, 0);
}

static TandemGuiDevice *
find_device (TandemGuiDaemon *self, const char *id, guint *position)
{
  guint n = g_list_model_get_n_items (G_LIST_MODEL (self->devices));

  for (guint i = 0; i < n; i++)
    {
      g_autoptr(TandemGuiDevice) device = g_list_model_get_item (G_LIST_MODEL (self->devices), i);

      if (g_str_equal (device->id, id))
        {
          if (position != NULL)
            *position = i;
          return device;
        }
    }
  return NULL;
}

static void
add_device (TandemGuiDaemon *self, const char *id)
{
  g_autoptr(TandemGuiDevice) device = NULL;

  if (find_device (self, id, NULL) != NULL)
    return;
  device = device_new (self->bus, id);
  if (device != NULL)
    g_list_store_append (self->devices, device);
}

static void
reload (TandemGuiDaemon *self)
{
  g_autoptr(GError) error = NULL;
  g_autoptr(GVariant) reply = g_dbus_connection_call_sync (self->bus, TANDEM_GUI_DAEMON_NAME, TANDEM_GUI_DAEMON_PATH,
                                                           TANDEM_GUI_MANAGER_IFACE, "ListDevices", NULL,
                                                           G_VARIANT_TYPE ("(a{sa{sv}})"), G_DBUS_CALL_FLAGS_NONE,
                                                           30000, NULL, &error);
  g_autoptr(GVariant) dict = NULL;
  GVariantIter iter;
  const char *id;

  if (reply == NULL)
    {
      g_warning ("tandem: daemon not reachable: %s", error->message);
      return;
    }
  dict = g_variant_get_child_value (reply, 0);
  g_variant_iter_init (&iter, dict);
  while (g_variant_iter_next (&iter, "{&s@a{sv}}", &id, NULL))
    add_device (self, id);
}

static void
manager_signal_cb (GDBusConnection *connection,
                   const char      *sender,
                   const char      *object_path,
                   const char      *interface_name,
                   const char      *signal_name,
                   GVariant        *parameters,
                   gpointer         user_data)
{
  TandemGuiDaemon *self = user_data;
  const char *id;
  guint position;

  g_variant_get (parameters, "(&s)", &id);
  if (g_str_equal (signal_name, "DeviceAdded"))
    add_device (self, id);
  else if (g_str_equal (signal_name, "DeviceRemoved") && find_device (self, id, &position) != NULL)
    g_list_store_remove (self->devices, position);
}

static void
daemon_appeared_cb (GDBusConnection *connection, const char *name, const char *owner, gpointer user_data)
{
  TandemGuiDaemon *self = user_data;

  self->running = TRUE;
  reload (self);
  g_signal_emit (self, signals[RUNNING_CHANGED], 0);
}

static void
daemon_vanished_cb (GDBusConnection *connection, const char *name, gpointer user_data)
{
  TandemGuiDaemon *self = user_data;

  self->running = FALSE;
  g_list_store_remove_all (self->devices);
  g_signal_emit (self, signals[RUNNING_CHANGED], 0);
}

TandemGuiDaemon *
tandem_gui_daemon_get (void)
{
  static TandemGuiDaemon *instance;

  if (instance == NULL)
    {
      instance = g_object_new (TANDEM_TYPE_GUI_DAEMON, NULL);
      instance->bus = g_bus_get_sync (G_BUS_TYPE_SESSION, NULL, NULL);
      if (instance->bus != NULL)
        {
          instance->signal_subscription = g_dbus_connection_signal_subscribe (instance->bus, TANDEM_GUI_DAEMON_NAME,
                                                                              TANDEM_GUI_MANAGER_IFACE, NULL,
                                                                              TANDEM_GUI_DAEMON_PATH, NULL,
                                                                              G_DBUS_SIGNAL_FLAGS_NONE,
                                                                              manager_signal_cb, instance, NULL);
          instance->name_watch = g_bus_watch_name_on_connection (instance->bus, TANDEM_GUI_DAEMON_NAME,
                                                                 G_BUS_NAME_WATCHER_FLAGS_AUTO_START,
                                                                 daemon_appeared_cb, daemon_vanished_cb, instance, NULL);
          reload (instance);
        }
    }
  return instance;
}

GListModel *
tandem_gui_daemon_get_devices (TandemGuiDaemon *self)
{
  return G_LIST_MODEL (self->devices);
}

TandemGuiDevice *
tandem_gui_daemon_find (TandemGuiDaemon *self, const char *id)
{
  return find_device (self, id, NULL);
}

gboolean
tandem_gui_daemon_is_running (TandemGuiDaemon *self)
{
  return self->running;
}

void
tandem_gui_daemon_refresh (TandemGuiDaemon *self)
{
  if (self->bus != NULL)
    g_dbus_connection_call (self->bus, TANDEM_GUI_DAEMON_NAME, TANDEM_GUI_DAEMON_PATH, TANDEM_GUI_MANAGER_IFACE,
                            "Refresh", NULL, NULL, G_DBUS_CALL_FLAGS_NONE, 5000, NULL, NULL, NULL);
}

char *
tandem_gui_daemon_self_id (TandemGuiDaemon *self)
{
  g_autoptr(GVariant) reply = NULL;
  char *id = NULL;

  if (self->bus == NULL)
    return NULL;
  reply = g_dbus_connection_call_sync (self->bus, TANDEM_GUI_DAEMON_NAME, TANDEM_GUI_DAEMON_PATH,
                                       TANDEM_GUI_MANAGER_IFACE, "SelfId", NULL, G_VARIANT_TYPE ("(s)"),
                                       G_DBUS_CALL_FLAGS_NONE, 5000, NULL, NULL);
  if (reply != NULL)
    g_variant_get (reply, "(s)", &id);
  return id;
}

static void
tandem_gui_daemon_class_init (TandemGuiDaemonClass *klass)
{
  signals[RUNNING_CHANGED] = g_signal_new ("running-changed", TANDEM_TYPE_GUI_DAEMON, G_SIGNAL_RUN_LAST, 0,
                                           NULL, NULL, NULL, G_TYPE_NONE, 0);
}

static void
tandem_gui_daemon_init (TandemGuiDaemon *self)
{
  self->devices = g_list_store_new (TANDEM_TYPE_GUI_DEVICE);
}
