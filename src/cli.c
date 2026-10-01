/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "cli.h"
#include "config.h"
#include "dbus-api.h"
#include "util.h"

#include <gio/gio.h>
#include <stdio.h>
#include <stdlib.h>
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
           "  pair <dev>        request pairing, or accept a pending request\n"
           "  accept <dev>      accept a pairing request\n"
           "  reject <dev>      reject a pairing request\n"
           "  unpair <dev>      forget a paired device\n"
           "  ping <dev> [msg]  send a ping\n"
           "  key <dev>         show the verification key during pairing\n"
           "  ring <dev>        make the device ring\n"
           "  share <dev> <file>... | <url>   send files or a URL\n"
           "  text <dev> <text> send a text snippet\n"
           "  clipboard <dev>   send the clipboard\n"
           "  lock|unlock <dev> lock or unlock the device\n"
           "  commands <dev>    list the commands the device may run here\n"
           "  add-command <dev> <name> <shell>  add such a command\n"
           "  remove-command <dev> <key>\n"
           "  run <dev> <key>   run one of the device's commands here\n"
           "  sms <dev> <number> <text>  send an SMS\n"
           "  keys <dev> <text> type on the device\n"
           "  special-key <dev> <n>  send a special key (1 Backspace, 12 Enter)\n"
           "  notifications <dev>   list the device's notifications\n"
           "  dismiss <dev> <id>    dismiss one of them\n"
           "  players <dev>     list local media players\n"
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

static char *
resolve_device (GDBusConnection *bus, const char *wanted, GError **error)
{
  g_autoptr(GVariant) reply = call_manager (bus, "ListDevices", NULL, "(a{sa{sv}})", error);
  g_autoptr(GVariant) devices = NULL;
  GVariantIter iter;
  const char *id;
  GVariant *props;
  char *match = NULL;

  if (reply == NULL)
    return NULL;
  devices = g_variant_get_child_value (reply, 0);
  g_variant_iter_init (&iter, devices);
  while (g_variant_iter_loop (&iter, "{&s@a{sv}}", &id, &props))
    {
      const char *name = "";

      g_variant_lookup (props, "name", "&s", &name);
      if (g_str_equal (id, wanted))
        {
          g_free (match);
          match = g_strdup (id);
          break;
        }
      if (g_str_has_prefix (id, wanted) || g_str_equal (name, wanted))
        {
          g_free (match);
          match = g_strdup (id);
        }
    }
  if (match == NULL)
    g_set_error (error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND, "No device matches %s", wanted);
  return match;
}

static int
cmd_device (const char *wanted, const char *method, GVariant *parameters, const char *property)
{
  g_autoptr(GError) error = NULL;
  g_autoptr(GDBusConnection) bus = session_bus (&error);
  g_autofree char *id = NULL;
  g_autofree char *path = NULL;
  g_autoptr(GVariant) reply = NULL;

  if (parameters != NULL)
    g_variant_ref_sink (parameters);
  if (bus == NULL)
    {
      g_clear_pointer (&parameters, g_variant_unref);
      return fail (error);
    }
  id = resolve_device (bus, wanted, &error);
  if (id == NULL)
    {
      g_clear_pointer (&parameters, g_variant_unref);
      return fail (error);
    }
  path = tandem_object_path_for_id (id);
  if (method != NULL)
    {
      reply = g_dbus_connection_call_sync (bus, TANDEM_DBUS_NAME, path, TANDEM_DBUS_DEVICE_IFACE, method,
                                           parameters, NULL, G_DBUS_CALL_FLAGS_NONE, 30000, NULL, &error);
      g_clear_pointer (&parameters, g_variant_unref);
      if (reply == NULL)
        return fail (error);
    }
  if (property != NULL)
    {
      g_autoptr(GVariant) value = NULL;

      reply = g_dbus_connection_call_sync (bus, TANDEM_DBUS_NAME, path, "org.freedesktop.DBus.Properties", "Get",
                                           g_variant_new ("(ss)", TANDEM_DBUS_DEVICE_IFACE, property),
                                           G_VARIANT_TYPE ("(v)"), G_DBUS_CALL_FLAGS_NONE, 30000, NULL, &error);
      if (reply == NULL)
        return fail (error);
      g_variant_get (reply, "(v)", &value);
      if (g_variant_is_of_type (value, G_VARIANT_TYPE_STRING))
        printf ("%s\n", g_variant_get_string (value, NULL));
      else
        {
          g_autofree char *text = g_variant_print (value, FALSE);

          printf ("%s\n", text);
        }
    }
  return 0;
}

static int
cmd_device_print (const char *wanted, const char *method, GVariant *parameters)
{
  g_autoptr(GError) error = NULL;
  g_autoptr(GDBusConnection) bus = session_bus (&error);
  g_autofree char *id = NULL;
  g_autofree char *path = NULL;
  g_autoptr(GVariant) reply = NULL;
  g_autoptr(GVariant) child = NULL;
  GVariantIter iter;
  GVariant *row;

  if (bus == NULL)
    return fail (error);
  id = resolve_device (bus, wanted, &error);
  if (id == NULL)
    return fail (error);
  path = tandem_object_path_for_id (id);
  reply = g_dbus_connection_call_sync (bus, TANDEM_DBUS_NAME, path, TANDEM_DBUS_DEVICE_IFACE, method, parameters,
                                       NULL, G_DBUS_CALL_FLAGS_NONE, 30000, NULL, &error);
  if (reply == NULL)
    return fail (error);
  child = g_variant_get_child_value (reply, 0);
  g_variant_iter_init (&iter, child);
  while ((row = g_variant_iter_next_value (&iter)) != NULL)
    {
      if (g_variant_is_of_type (row, G_VARIANT_TYPE_STRING))
        printf ("%s\n", g_variant_get_string (row, NULL));
      else
        {
          gsize n = g_variant_n_children (row);

          for (gsize i = 0; i < n; i++)
            {
              g_autoptr(GVariant) field = g_variant_get_child_value (row, i);

              printf ("%s%s", i > 0 ? "\t" : "", g_variant_get_string (field, NULL));
            }
          printf ("\n");
        }
      g_variant_unref (row);
    }
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
  if (argc > 2 && g_str_equal (command, "pair"))
    return cmd_device (argv[2], "Pair", NULL, NULL);
  if (argc > 2 && g_str_equal (command, "accept"))
    return cmd_device (argv[2], "AcceptPair", NULL, NULL);
  if (argc > 2 && g_str_equal (command, "reject"))
    return cmd_device (argv[2], "RejectPair", NULL, NULL);
  if (argc > 2 && g_str_equal (command, "unpair"))
    return cmd_device (argv[2], "Unpair", NULL, NULL);
  if (argc > 2 && g_str_equal (command, "ping"))
    return cmd_device (argv[2], "Ping", g_variant_new ("(s)", argc > 3 ? argv[3] : ""), NULL);
  if (argc > 2 && g_str_equal (command, "ring"))
    return cmd_device (argv[2], "Ring", NULL, NULL);
  if (argc > 3 && g_str_equal (command, "share"))
    {
      g_autoptr(GPtrArray) paths = g_ptr_array_new ();

      if (argc == 4 && g_uri_is_valid (argv[3], G_URI_FLAGS_NONE, NULL) && !g_file_test (argv[3], G_FILE_TEST_EXISTS))
        return cmd_device (argv[2], "ShareUrl", g_variant_new ("(s)", argv[3]), NULL);
      for (int i = 3; i < argc; i++)
        {
          g_autoptr(GFile) file = g_file_new_for_commandline_arg (argv[i]);

          g_ptr_array_add (paths, g_file_get_path (file));
        }
      g_ptr_array_add (paths, NULL);
      {
        int ret = cmd_device (argv[2], "ShareFiles", g_variant_new ("(^as)", (char **) paths->pdata), NULL);

        for (guint i = 0; i + 1 < paths->len; i++)
          g_free (g_ptr_array_index (paths, i));
        return ret;
      }
    }
  if (argc > 3 && g_str_equal (command, "text"))
    return cmd_device (argv[2], "ShareText", g_variant_new ("(s)", argv[3]), NULL);
  if (argc > 2 && g_str_equal (command, "clipboard"))
    return cmd_device (argv[2], "SendClipboard", NULL, NULL);
  if (argc > 2 && g_str_equal (command, "lock"))
    return cmd_device (argv[2], "Lock", g_variant_new ("(b)", TRUE), NULL);
  if (argc > 2 && g_str_equal (command, "unlock"))
    return cmd_device (argv[2], "Lock", g_variant_new ("(b)", FALSE), NULL);
  if (argc > 2 && g_str_equal (command, "commands"))
    return cmd_device_print (argv[2], "ListCommands", NULL);
  if (argc > 4 && g_str_equal (command, "add-command"))
    return cmd_device (argv[2], "AddCommand", g_variant_new ("(ss)", argv[3], argv[4]), NULL);
  if (argc > 3 && g_str_equal (command, "remove-command"))
    return cmd_device (argv[2], "RemoveCommand", g_variant_new ("(s)", argv[3]), NULL);
  if (argc > 3 && g_str_equal (command, "run"))
    return cmd_device (argv[2], "RunCommand", g_variant_new ("(s)", argv[3]), NULL);
  if (argc > 4 && g_str_equal (command, "sms"))
    {
      const char *addresses[] = { argv[3], NULL };

      return cmd_device (argv[2], "SendSms", g_variant_new ("(^ass)", addresses, argv[4]), NULL);
    }
  if (argc > 3 && g_str_equal (command, "keys"))
    return cmd_device (argv[2], "SendKeys", g_variant_new ("(s)", argv[3]), NULL);
  if (argc > 3 && g_str_equal (command, "special-key"))
    return cmd_device (argv[2], "SendSpecialKey", g_variant_new ("(i)", atoi (argv[3])), NULL);
  if (argc > 2 && g_str_equal (command, "notifications"))
    return cmd_device_print (argv[2], "ListNotifications", NULL);
  if (argc > 3 && g_str_equal (command, "dismiss"))
    return cmd_device (argv[2], "DismissNotification", g_variant_new ("(s)", argv[3]), NULL);
  if (argc > 2 && g_str_equal (command, "players"))
    return cmd_device_print (argv[2], "ListPlayers", NULL);
  if (argc > 2 && g_str_equal (command, "key"))
    return cmd_device (argv[2], NULL, NULL, "VerificationKey");
  if (g_str_equal (command, "help"))
    {
      usage ();
      return 0;
    }

  fprintf (stderr, "tandem: unknown command %s\n", command);
  usage ();
  return 2;
}
