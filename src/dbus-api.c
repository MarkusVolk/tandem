/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "dbus-api.h"
#include "app.h"
#include "config.h"
#include "device.h"
#include "util.h"

void tandem_ping_plugin_send (TandemPlugin *plugin, const char *message);

static const char manager_xml[] =
  "<node>"
  "  <interface name='de.flk.Tandem.Manager'>"
  "    <method name='ListDevices'>"
  "      <arg type='a{sa{sv}}' name='devices' direction='out'/>"
  "    </method>"
  "    <method name='Refresh'/>"
  "    <method name='SelfId'>"
  "      <arg type='s' name='id' direction='out'/>"
  "    </method>"
  "    <method name='Show'/>"
  "    <method name='Quit'/>"
  "    <signal name='DeviceAdded'><arg type='s' name='id'/></signal>"
  "    <signal name='DeviceRemoved'><arg type='s' name='id'/></signal>"
  "    <signal name='DeviceChanged'><arg type='s' name='id'/></signal>"
  "  </interface>"
  "</node>";

static const char device_xml[] =
  "<node>"
  "  <interface name='de.flk.Tandem.Device'>"
  "    <property name='Id' type='s' access='read'/>"
  "    <property name='Name' type='s' access='read'/>"
  "    <property name='Type' type='s' access='read'/>"
  "    <property name='Paired' type='b' access='read'/>"
  "    <property name='Reachable' type='b' access='read'/>"
  "    <property name='PairState' type='s' access='read'/>"
  "    <property name='VerificationKey' type='s' access='read'/>"
  "    <property name='Plugins' type='as' access='read'/>"
  "    <method name='Pair'/>"
  "    <method name='AcceptPair'/>"
  "    <method name='RejectPair'/>"
  "    <method name='Unpair'/>"
  "    <method name='Ping'>"
  "      <arg type='s' name='message' direction='in'/>"
  "    </method>"
  "    <method name='SetPluginEnabled'>"
  "      <arg type='s' name='plugin' direction='in'/>"
  "      <arg type='b' name='enabled' direction='in'/>"
  "    </method>"
  "  </interface>"
  "</node>";

static GDBusNodeInfo *manager_info;
static GDBusNodeInfo *device_info;
static guint manager_registration;
static GDBusConnection *api_connection;
static GHashTable *device_registrations;

static void
manager_method_call (GDBusConnection       *connection,
                     const char            *sender,
                     const char            *object_path,
                     const char            *interface_name,
                     const char            *method_name,
                     GVariant              *parameters,
                     GDBusMethodInvocation *invocation,
                     gpointer               user_data)
{
  TandemApp *app = user_data;

  if (g_str_equal (method_name, "ListDevices"))
    {
      GVariantBuilder builder;

      g_variant_builder_init (&builder, G_VARIANT_TYPE ("a{sa{sv}}"));
      tandem_app_describe_devices (app, &builder);
      g_dbus_method_invocation_return_value (invocation, g_variant_new ("(a{sa{sv}})", &builder));
    }
  else if (g_str_equal (method_name, "Refresh"))
    {
      tandem_app_refresh (app);
      g_dbus_method_invocation_return_value (invocation, NULL);
    }
  else if (g_str_equal (method_name, "SelfId"))
    {
      TandemConfig *config = tandem_app_get_config (app);

      g_dbus_method_invocation_return_value (invocation,
                                             g_variant_new ("(s)", tandem_config_get_device_id (config)));
    }
  else if (g_str_equal (method_name, "Show"))
    {
      tandem_app_show_window (app);
      g_dbus_method_invocation_return_value (invocation, NULL);
    }
  else if (g_str_equal (method_name, "Quit"))
    {
      g_dbus_method_invocation_return_value (invocation, NULL);
      tandem_app_quit (app);
    }
  else
    {
      g_dbus_method_invocation_return_error (invocation, G_DBUS_ERROR, G_DBUS_ERROR_UNKNOWN_METHOD,
                                             "Unknown method %s", method_name);
    }
}

static const GDBusInterfaceVTable manager_vtable = {
  .method_call = manager_method_call,
};

static GVariant *
plugin_names (TandemDevice *device)
{
  GPtrArray *plugins = tandem_device_get_plugins (device);
  GVariantBuilder builder;

  g_variant_builder_init (&builder, G_VARIANT_TYPE ("as"));
  for (guint i = 0; i < plugins->len; i++)
    g_variant_builder_add (&builder, "s", tandem_plugin_get_name (g_ptr_array_index (plugins, i)));
  return g_variant_builder_end (&builder);
}

static GVariant *
device_get_property (GDBusConnection  *connection,
                     const char       *sender,
                     const char       *object_path,
                     const char       *interface_name,
                     const char       *property_name,
                     GError          **error,
                     gpointer          user_data)
{
  TandemDevice *device = user_data;

  if (g_str_equal (property_name, "Id"))
    return g_variant_new_string (tandem_device_get_id (device));
  if (g_str_equal (property_name, "Name"))
    return g_variant_new_string (tandem_device_get_name (device));
  if (g_str_equal (property_name, "Type"))
    return g_variant_new_string (tandem_device_get_type_name (device));
  if (g_str_equal (property_name, "Paired"))
    return g_variant_new_boolean (tandem_device_is_paired (device));
  if (g_str_equal (property_name, "Reachable"))
    return g_variant_new_boolean (tandem_device_is_reachable (device));
  if (g_str_equal (property_name, "PairState"))
    return g_variant_new_string (tandem_device_get_state_name (device));
  if (g_str_equal (property_name, "VerificationKey"))
    {
      g_autofree char *key = tandem_device_get_verification_key (device);

      return g_variant_new_string (key != NULL ? key : "");
    }
  if (g_str_equal (property_name, "Plugins"))
    return plugin_names (device);
  g_set_error (error, G_DBUS_ERROR, G_DBUS_ERROR_UNKNOWN_PROPERTY, "Unknown property %s", property_name);
  return NULL;
}

static void
device_method_call (GDBusConnection       *connection,
                    const char            *sender,
                    const char            *object_path,
                    const char            *interface_name,
                    const char            *method_name,
                    GVariant              *parameters,
                    GDBusMethodInvocation *invocation,
                    gpointer               user_data)
{
  TandemDevice *device = user_data;

  if (g_str_equal (method_name, "Pair"))
    tandem_device_request_pair (device);
  else if (g_str_equal (method_name, "AcceptPair"))
    tandem_device_accept_pair (device);
  else if (g_str_equal (method_name, "RejectPair"))
    tandem_device_reject_pair (device);
  else if (g_str_equal (method_name, "Unpair"))
    tandem_device_unpair (device);
  else if (g_str_equal (method_name, "Ping"))
    {
      const char *message;
      TandemPlugin *plugin = tandem_device_get_plugin (device, "ping");

      g_variant_get (parameters, "(&s)", &message);
      if (plugin == NULL)
        {
          g_dbus_method_invocation_return_error (invocation, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED,
                                                 "Device is not paired and reachable with the ping plugin");
          return;
        }
      tandem_ping_plugin_send (plugin, message);
    }
  else if (g_str_equal (method_name, "SetPluginEnabled"))
    {
      const char *name;
      gboolean enabled;

      g_variant_get (parameters, "(&sb)", &name, &enabled);
      tandem_device_set_plugin_enabled (device, name, enabled);
    }
  else
    {
      g_dbus_method_invocation_return_error (invocation, G_DBUS_ERROR, G_DBUS_ERROR_UNKNOWN_METHOD,
                                             "Unknown method %s", method_name);
      return;
    }
  g_dbus_method_invocation_return_value (invocation, NULL);
}

static const GDBusInterfaceVTable device_vtable = {
  .method_call = device_method_call,
  .get_property = device_get_property,
};

gboolean
tandem_dbus_api_register (GDBusConnection *connection, TandemApp *app, GError **error)
{
  if (manager_info == NULL)
    manager_info = g_dbus_node_info_new_for_xml (manager_xml, NULL);
  if (device_info == NULL)
    device_info = g_dbus_node_info_new_for_xml (device_xml, NULL);
  if (device_registrations == NULL)
    device_registrations = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);

  manager_registration = g_dbus_connection_register_object (connection, TANDEM_DBUS_PATH,
                                                            manager_info->interfaces[0],
                                                            &manager_vtable, app, NULL, error);
  if (manager_registration == 0)
    return FALSE;
  api_connection = connection;
  return TRUE;
}

void
tandem_dbus_api_unregister (GDBusConnection *connection)
{
  GHashTableIter iter;
  gpointer value;

  if (device_registrations != NULL)
    {
      g_hash_table_iter_init (&iter, device_registrations);
      while (g_hash_table_iter_next (&iter, NULL, &value))
        g_dbus_connection_unregister_object (connection, GPOINTER_TO_UINT (value));
      g_hash_table_remove_all (device_registrations);
    }
  if (manager_registration != 0)
    {
      g_dbus_connection_unregister_object (connection, manager_registration);
      manager_registration = 0;
    }
  api_connection = NULL;
}

void
tandem_dbus_api_add_device (TandemDevice *device)
{
  g_autofree char *path = tandem_object_path_for_id (tandem_device_get_id (device));
  g_autoptr(GError) error = NULL;
  guint id;

  if (api_connection == NULL || g_hash_table_contains (device_registrations, path))
    return;
  id = g_dbus_connection_register_object (api_connection, path, device_info->interfaces[0], &device_vtable,
                                          g_object_ref (device), g_object_unref, &error);
  if (id == 0)
    {
      g_warning ("cannot export %s: %s", path, error->message);
      return;
    }
  g_hash_table_insert (device_registrations, g_steal_pointer (&path), GUINT_TO_POINTER (id));
}

void
tandem_dbus_api_remove_device (TandemDevice *device)
{
  g_autofree char *path = tandem_object_path_for_id (tandem_device_get_id (device));
  gpointer id;

  if (api_connection == NULL || !g_hash_table_lookup_extended (device_registrations, path, NULL, &id))
    return;
  g_dbus_connection_unregister_object (api_connection, GPOINTER_TO_UINT (id));
  g_hash_table_remove (device_registrations, path);
}

void
tandem_dbus_api_device_changed (TandemDevice *device)
{
  g_autofree char *path = tandem_object_path_for_id (tandem_device_get_id (device));
  g_autofree char *key = tandem_device_get_verification_key (device);
  GVariantBuilder changed;
  GVariantBuilder invalidated;

  if (api_connection == NULL || !g_hash_table_contains (device_registrations, path))
    return;
  g_variant_builder_init (&changed, G_VARIANT_TYPE ("a{sv}"));
  g_variant_builder_add (&changed, "{sv}", "Name", g_variant_new_string (tandem_device_get_name (device)));
  g_variant_builder_add (&changed, "{sv}", "Paired", g_variant_new_boolean (tandem_device_is_paired (device)));
  g_variant_builder_add (&changed, "{sv}", "Reachable", g_variant_new_boolean (tandem_device_is_reachable (device)));
  g_variant_builder_add (&changed, "{sv}", "PairState", g_variant_new_string (tandem_device_get_state_name (device)));
  g_variant_builder_add (&changed, "{sv}", "VerificationKey", g_variant_new_string (key != NULL ? key : ""));
  g_variant_builder_add (&changed, "{sv}", "Plugins", plugin_names (device));
  g_variant_builder_init (&invalidated, G_VARIANT_TYPE ("as"));
  g_dbus_connection_emit_signal (api_connection, NULL, path, "org.freedesktop.DBus.Properties", "PropertiesChanged",
                                 g_variant_new ("(sa{sv}as)", TANDEM_DBUS_DEVICE_IFACE, &changed, &invalidated),
                                 NULL);
}

void
tandem_dbus_api_notify_device (const char *signal_name, const char *device_id)
{
  if (api_connection == NULL)
    return;
  g_dbus_connection_emit_signal (api_connection, NULL, TANDEM_DBUS_PATH, TANDEM_DBUS_MANAGER_IFACE,
                                 signal_name, g_variant_new ("(s)", device_id), NULL);
}
