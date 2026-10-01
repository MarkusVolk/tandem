/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "cli.h"
#include "config.h"
#include "dbus-api.h"

#include <gio/gio.h>
#include <stdio.h>
#include <string.h>

static void
usage (void)
{
  fprintf (stderr,
           "Usage: tandem [<command> [args]]\n"
           "\n"
           "Without a command the window is opened.\n"
           "\n"
           "Commands:\n"
           "  list [--id-only]  devices known to the running service\n"
           "  my-id             this machine's device id\n"
           "  refresh           broadcast the identity again\n"
           "  monitor           print device changes until interrupted\n"
           "  show              open the window\n"
           "  quit              stop the service\n"
           "  --smoke-test      check the build and the host without a phone\n");
}

static GDBusConnection *
session_bus (GError **error)
{
  return g_bus_get_sync (G_BUS_TYPE_SESSION, NULL, error);
}

static GVariant *
call_manager (GDBusConnection *bus, const char *method, GVariant *parameters,
              const char *reply_type, GError **error)
{
  return g_dbus_connection_call_sync (bus, TANDEM_DBUS_NAME, TANDEM_DBUS_PATH, TANDEM_DBUS_MANAGER_IFACE,
                                      method, parameters,
                                      reply_type != NULL ? G_VARIANT_TYPE (reply_type) : NULL,
                                      G_DBUS_CALL_FLAGS_NONE, 30000, NULL, error);
}

static int
fail (GError *error)
{
  fprintf (stderr, "tandem: %s\n", error->message);
  return 1;
}

static int
cmd_my_id (void)
{
  g_autoptr(GError) error = NULL;
  g_autoptr(TandemConfig) config = tandem_config_new (NULL, &error);

  if (config == NULL)
    return fail (error);
  printf ("%s\n", tandem_config_get_device_id (config));
  return 0;
}

static void
print_device (const char *id, GVariant *props, gboolean id_only)
{
  const char *name = "";
  const char *state = "";
  const char *type = "";

  if (id_only)
    {
      printf ("%s\n", id);
      return;
    }
  g_variant_lookup (props, "name", "&s", &name);
  g_variant_lookup (props, "state", "&s", &state);
  g_variant_lookup (props, "type", "&s", &type);
  printf ("%s\t%s\t%s\t%s\n", id, name, type, state);
}

static int
cmd_list (gboolean id_only)
{
  g_autoptr(GError) error = NULL;
  g_autoptr(GDBusConnection) bus = session_bus (&error);
  g_autoptr(GVariant) reply = NULL;
  g_autoptr(GVariant) devices = NULL;
  GVariantIter iter;
  const char *id;
  GVariant *props;

  if (bus == NULL)
    return fail (error);
  reply = call_manager (bus, "ListDevices", NULL, "(a{sa{sv}})", &error);
  if (reply == NULL)
    return fail (error);
  devices = g_variant_get_child_value (reply, 0);
  g_variant_iter_init (&iter, devices);
  while (g_variant_iter_loop (&iter, "{&s@a{sv}}", &id, &props))
    print_device (id, props, id_only);
  return 0;
}

static int
cmd_simple (const char *method)
{
  g_autoptr(GError) error = NULL;
  g_autoptr(GDBusConnection) bus = session_bus (&error);
  g_autoptr(GVariant) reply = NULL;

  if (bus == NULL)
    return fail (error);
  reply = call_manager (bus, method, NULL, NULL, &error);
  if (reply == NULL)
    return fail (error);
  return 0;
}

static void
monitor_signal_cb (GDBusConnection *connection,
                   const char      *sender,
                   const char      *object_path,
                   const char      *interface_name,
                   const char      *signal_name,
                   GVariant        *parameters,
                   gpointer         user_data)
{
  const char *id;
  g_autoptr(GError) error = NULL;
  g_autoptr(GVariant) reply = NULL;

  g_variant_get (parameters, "(&s)", &id);
  reply = call_manager (connection, "ListDevices", NULL, "(a{sa{sv}})", &error);
  if (reply != NULL)
    {
      g_autoptr(GVariant) devices = g_variant_get_child_value (reply, 0);
      g_autoptr(GVariant) props = g_variant_lookup_value (devices, id, G_VARIANT_TYPE ("a{sv}"));
      const char *name = "";
      const char *state = "gone";

      if (props != NULL)
        {
          g_variant_lookup (props, "name", "&s", &name);
          g_variant_lookup (props, "state", "&s", &state);
        }
      printf ("%s\t%s\t%s\t%s\n", signal_name, id, name, state);
    }
  else
    {
      printf ("%s\t%s\n", signal_name, id);
    }
  fflush (stdout);
}

static int
cmd_monitor (void)
{
  g_autoptr(GError) error = NULL;
  g_autoptr(GDBusConnection) bus = session_bus (&error);
  g_autoptr(GMainLoop) loop = NULL;
  g_autoptr(GVariant) reply = NULL;

  if (bus == NULL)
    return fail (error);
  reply = call_manager (bus, "SelfId", NULL, "(s)", &error);
  if (reply == NULL)
    return fail (error);
  g_dbus_connection_signal_subscribe (bus, TANDEM_DBUS_NAME, TANDEM_DBUS_MANAGER_IFACE, NULL,
                                      TANDEM_DBUS_PATH, NULL, G_DBUS_SIGNAL_FLAGS_NONE,
                                      monitor_signal_cb, NULL, NULL);
  loop = g_main_loop_new (NULL, FALSE);
  g_main_loop_run (loop);
  return 0;
}

int
tandem_cli_run (int argc, char **argv)
{
  const char *command = argv[1];

  if (g_str_equal (command, "my-id"))
    return cmd_my_id ();
  if (g_str_equal (command, "list"))
    return cmd_list (argc > 2 && g_str_equal (argv[2], "--id-only"));
  if (g_str_equal (command, "refresh"))
    return cmd_simple ("Refresh");
  if (g_str_equal (command, "monitor"))
    return cmd_monitor ();
  if (g_str_equal (command, "show"))
    return cmd_simple ("Show");
  if (g_str_equal (command, "quit"))
    return cmd_simple ("Quit");
  if (g_str_equal (command, "help"))
    {
      usage ();
      return 0;
    }

  fprintf (stderr, "tandem: unknown command %s\n", command);
  usage ();
  return 2;
}
