/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "frontend.h"
#include "tandem-build.h"

#define FRONTEND_PATH "/de/flk/Tandem"

static void
call_done_cb (GObject *source, GAsyncResult *result, gpointer user_data)
{
  g_autoptr(GError) error = NULL;
  g_autoptr(GVariant) reply = g_dbus_connection_call_finish (G_DBUS_CONNECTION (source), result, &error);

  if (reply == NULL)
    g_warning ("frontend: %s", error->message);
}

static void
call (const char *method, GVariant *parameters)
{
  g_autoptr(GDBusConnection) bus = g_bus_get_sync (G_BUS_TYPE_SESSION, NULL, NULL);

  if (bus == NULL)
    {
      g_variant_unref (g_variant_ref_sink (parameters));
      return;
    }
  g_dbus_connection_call (bus, TANDEM_APP_ID, FRONTEND_PATH, "org.freedesktop.Application", method, parameters,
                          NULL, G_DBUS_CALL_FLAGS_NONE, 30000, NULL, call_done_cb, NULL);
}

void
tandem_frontend_activate (void)
{
  GVariantBuilder platform;

  g_variant_builder_init (&platform, G_VARIANT_TYPE ("a{sv}"));
  call ("Activate", g_variant_new ("(a{sv})", &platform));
}

void
tandem_frontend_action (const char *action, GVariant *parameter)
{
  GVariantBuilder platform;
  GVariantBuilder params;

  g_variant_builder_init (&platform, G_VARIANT_TYPE ("a{sv}"));
  g_variant_builder_init (&params, G_VARIANT_TYPE ("av"));
  if (parameter != NULL)
    g_variant_builder_add (&params, "v", parameter);
  call ("ActivateAction", g_variant_new ("(sava{sv})", action, &params, &platform));
}
