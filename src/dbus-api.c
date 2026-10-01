/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "dbus-api.h"
#include "app.h"
#include "config.h"

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

static GDBusNodeInfo *manager_info;
static guint manager_registration;
static GDBusConnection *api_connection;

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

gboolean
tandem_dbus_api_register (GDBusConnection *connection, TandemApp *app, GError **error)
{
  if (manager_info == NULL)
    manager_info = g_dbus_node_info_new_for_xml (manager_xml, NULL);

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
  if (manager_registration != 0)
    {
      g_dbus_connection_unregister_object (connection, manager_registration);
      manager_registration = 0;
    }
  api_connection = NULL;
}

void
tandem_dbus_api_notify_device (const char *signal_name, const char *device_id)
{
  if (api_connection == NULL)
    return;
  g_dbus_connection_emit_signal (api_connection, NULL, TANDEM_DBUS_PATH, TANDEM_DBUS_MANAGER_IFACE,
                                 signal_name, g_variant_new ("(s)", device_id), NULL);
}
