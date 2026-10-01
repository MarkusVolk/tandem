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
           "  list            devices known to the running service\n"
           "  my-id           this machine's device id\n"
           "  refresh         broadcast the identity again\n"
           "  show            open the window\n"
           "  quit            stop the service\n"
           "  --smoke-test    check the build and the host without a phone\n");
}

static GVariant *
call_manager (const char *method, GVariant *parameters, const char *reply_type, GError **error)
{
  g_autoptr(GDBusConnection) bus = g_bus_get_sync (G_BUS_TYPE_SESSION, NULL, error);

  if (bus == NULL)
    return NULL;
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

static int
cmd_list (void)
{
  g_autoptr(GError) error = NULL;
  g_autoptr(GVariant) reply = call_manager ("ListDevices", NULL, "(a{sa{sv}})", &error);
  g_autoptr(GVariant) devices = NULL;
  GVariantIter iter;
  const char *id;
  GVariant *props;

  if (reply == NULL)
    return fail (error);
  devices = g_variant_get_child_value (reply, 0);
  g_variant_iter_init (&iter, devices);
  while (g_variant_iter_loop (&iter, "{&s@a{sv}}", &id, &props))
    {
      const char *name = NULL;
      const char *state = NULL;

      g_variant_lookup (props, "name", "&s", &name);
      g_variant_lookup (props, "state", "&s", &state);
      printf ("%s\t%s\t%s\n", id, name != NULL ? name : "", state != NULL ? state : "");
    }
  return 0;
}

static int
cmd_simple (const char *method)
{
  g_autoptr(GError) error = NULL;
  g_autoptr(GVariant) reply = call_manager (method, NULL, NULL, &error);

  if (reply == NULL)
    return fail (error);
  return 0;
}

int
tandem_cli_run (int argc, char **argv)
{
  const char *command = argv[1];

  if (g_str_equal (command, "my-id"))
    return cmd_my_id ();
  if (g_str_equal (command, "list"))
    return cmd_list ();
  if (g_str_equal (command, "refresh"))
    return cmd_simple ("Refresh");
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
