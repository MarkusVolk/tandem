/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "input.h"

#include <string.h>

#define PORTAL_NAME "org.freedesktop.portal.Desktop"
#define PORTAL_PATH "/org/freedesktop/portal/desktop"
#define REMOTE_IFACE "org.freedesktop.portal.RemoteDesktop"
#define REQUEST_IFACE "org.freedesktop.portal.Request"
#define SESSION_IFACE "org.freedesktop.portal.Session"

typedef struct
{
  GDBusConnection *bus;
  char *session;
  gboolean started;
  gboolean starting;
  GQueue *queued;
  guint closed_subscription;
} Portal;

typedef struct
{
  char *method;
  GVariant *parameters;
} QueuedCall;

static void
queued_call_free (QueuedCall *call)
{
  g_free (call->method);
  g_variant_unref (call->parameters);
  g_free (call);
}

static gboolean
portal_available (void)
{
  g_autoptr(GDBusConnection) bus = g_bus_get_sync (G_BUS_TYPE_SESSION, NULL, NULL);
  g_autoptr(GVariant) reply = NULL;
  g_autoptr(GVariant) value = NULL;
  g_autoptr(GVariant) types = NULL;
  guint32 version = 0;
  guint32 device_types = 0;

  if (bus == NULL)
    return FALSE;
  reply = g_dbus_connection_call_sync (bus, PORTAL_NAME, PORTAL_PATH, "org.freedesktop.DBus.Properties", "Get",
                                       g_variant_new ("(ss)", REMOTE_IFACE, "version"), G_VARIANT_TYPE ("(v)"),
                                       G_DBUS_CALL_FLAGS_NONE, 2000, NULL, NULL);
  if (reply == NULL)
    return FALSE;
  g_variant_get (reply, "(v)", &value);
  version = g_variant_get_uint32 (value);
  g_clear_pointer (&reply, g_variant_unref);
  reply = g_dbus_connection_call_sync (bus, PORTAL_NAME, PORTAL_PATH, "org.freedesktop.DBus.Properties", "Get",
                                       g_variant_new ("(ss)", REMOTE_IFACE, "AvailableDeviceTypes"),
                                       G_VARIANT_TYPE ("(v)"), G_DBUS_CALL_FLAGS_NONE, 2000, NULL, NULL);
  if (reply != NULL)
    {
      g_variant_get (reply, "(v)", &types);
      device_types = g_variant_get_uint32 (types);
    }
  return version >= 1 && (device_types & 3) != 0;
}

typedef void (*ResponseFunc) (Portal *portal, guint32 response, GVariant *results);

typedef struct
{
  Portal *portal;
  ResponseFunc func;
  guint subscription;
} PendingRequest;

static void
response_cb (GDBusConnection *connection,
             const char      *sender,
             const char      *object_path,
             const char      *interface_name,
             const char      *signal_name,
             GVariant        *parameters,
             gpointer         user_data)
{
  PendingRequest *pending = user_data;
  guint32 response;
  g_autoptr(GVariant) results = NULL;

  g_variant_get (parameters, "(u@a{sv})", &response, &results);
  g_dbus_connection_signal_unsubscribe (connection, pending->subscription);
  pending->func (pending->portal, response, results);
  g_free (pending);
}

static char *
request_path (GDBusConnection *bus, const char *token)
{
  g_autofree char *sender = g_strdup (g_dbus_connection_get_unique_name (bus) + 1);

  for (char *p = sender; *p != '\0'; p++)
    if (*p == '.')
      *p = '_';
  return g_strdup_printf ("%s/request/%s/%s", PORTAL_PATH, sender, token);
}

static void
portal_request (Portal *portal, const char *method, GVariant *arguments, GVariantBuilder *options, ResponseFunc func)
{
  static guint counter;
  g_autofree char *token = g_strdup_printf ("tandem%u", ++counter);
  g_autofree char *path = request_path (portal->bus, token);
  PendingRequest *pending = g_new0 (PendingRequest, 1);
  GVariantBuilder args;

  pending->portal = portal;
  pending->func = func;
  pending->subscription = g_dbus_connection_signal_subscribe (portal->bus, PORTAL_NAME, REQUEST_IFACE, "Response",
                                                              path, NULL, G_DBUS_SIGNAL_FLAGS_NO_MATCH_RULE,
                                                              response_cb, pending, NULL);
  g_variant_builder_add (options, "{sv}", "handle_token", g_variant_new_string (token));

  g_variant_builder_init (&args, G_VARIANT_TYPE_TUPLE);
  if (arguments != NULL)
    {
      GVariantIter iter;
      GVariant *child;

      g_variant_iter_init (&iter, arguments);
      while ((child = g_variant_iter_next_value (&iter)) != NULL)
        {
          g_variant_builder_add_value (&args, child);
          g_variant_unref (child);
        }
      g_variant_unref (g_variant_ref_sink (arguments));
    }
  g_variant_builder_add_value (&args, g_variant_builder_end (options));
  g_dbus_connection_call (portal->bus, PORTAL_NAME, PORTAL_PATH, REMOTE_IFACE, method, g_variant_builder_end (&args),
                          NULL, G_DBUS_CALL_FLAGS_NONE, 30000, NULL, NULL, NULL);
}

static void
flush_queue (Portal *portal)
{
  QueuedCall *call;

  while ((call = g_queue_pop_head (portal->queued)) != NULL)
    {
      g_dbus_connection_call (portal->bus, PORTAL_NAME, PORTAL_PATH, REMOTE_IFACE, call->method, call->parameters,
                              NULL, G_DBUS_CALL_FLAGS_NONE, 5000, NULL, NULL, NULL);
      call->parameters = NULL;
      g_free (call->method);
      g_free (call);
    }
}

static void
session_closed_cb (GDBusConnection *connection,
                   const char      *sender,
                   const char      *object_path,
                   const char      *interface_name,
                   const char      *signal_name,
                   GVariant        *parameters,
                   gpointer         user_data)
{
  Portal *portal = user_data;

  g_debug ("portal: remote desktop session closed");
  portal->started = FALSE;
  portal->starting = FALSE;
  g_clear_pointer (&portal->session, g_free);
}

static void
start_response (Portal *portal, guint32 response, GVariant *results)
{
  const char *token = NULL;

  portal->starting = FALSE;
  if (response != 0)
    {
      g_warning ("portal: remote desktop not granted (%u)", response);
      g_queue_clear_full (portal->queued, (GDestroyNotify) queued_call_free);
      g_clear_pointer (&portal->session, g_free);
      return;
    }
  if (g_variant_lookup (results, "restore_token", "&s", &token))
    {
      g_autoptr(GSettings) settings = g_settings_new ("de.flk.Tandem");

      g_settings_set_string (settings, "portal-restore-token", token);
    }
  portal->started = TRUE;
  flush_queue (portal);
}

static void
select_response (Portal *portal, guint32 response, GVariant *results)
{
  GVariantBuilder options;

  if (response != 0)
    {
      start_response (portal, response, results);
      return;
    }
  g_variant_builder_init (&options, G_VARIANT_TYPE ("a{sv}"));
  portal_request (portal, "Start", g_variant_new ("(os)", portal->session, ""), &options, start_response);
}

static void
create_response (Portal *portal, guint32 response, GVariant *results)
{
  const char *session = NULL;
  g_autoptr(GSettings) settings = g_settings_new ("de.flk.Tandem");
  g_autofree char *token = g_settings_get_string (settings, "portal-restore-token");
  GVariantBuilder options;

  if (response != 0 || !g_variant_lookup (results, "session_handle", "&s", &session))
    {
      start_response (portal, response != 0 ? response : 2, results);
      return;
    }
  portal->session = g_strdup (session);
  portal->closed_subscription = g_dbus_connection_signal_subscribe (portal->bus, PORTAL_NAME, SESSION_IFACE, "Closed",
                                                                    session, NULL, G_DBUS_SIGNAL_FLAGS_NONE,
                                                                    session_closed_cb, portal, NULL);
  g_variant_builder_init (&options, G_VARIANT_TYPE ("a{sv}"));
  g_variant_builder_add (&options, "{sv}", "types", g_variant_new_uint32 (3));
  g_variant_builder_add (&options, "{sv}", "persist_mode", g_variant_new_uint32 (2));
  if (token != NULL && *token != '\0')
    g_variant_builder_add (&options, "{sv}", "restore_token", g_variant_new_string (token));
  portal_request (portal, "SelectDevices", g_variant_new ("(o)", portal->session), &options, select_response);
}

static void
start_session (Portal *portal)
{
  static guint counter;
  g_autofree char *session_token = g_strdup_printf ("tandemsession%u", ++counter);
  GVariantBuilder options;

  portal->starting = TRUE;
  g_variant_builder_init (&options, G_VARIANT_TYPE ("a{sv}"));
  g_variant_builder_add (&options, "{sv}", "session_handle_token", g_variant_new_string (session_token));
  portal_request (portal, "CreateSession", NULL, &options, create_response);
}

static void
notify (Portal *portal, const char *method, GVariant *tail)
{
  GVariantBuilder args;
  GVariantBuilder options;
  GVariantIter iter;
  GVariant *child;
  GVariant *parameters;

  g_variant_ref_sink (tail);
  if (portal->session == NULL && !portal->starting)
    start_session (portal);

  g_variant_builder_init (&args, G_VARIANT_TYPE_TUPLE);
  g_variant_builder_add (&args, "o", portal->session != NULL ? portal->session : "/");
  g_variant_builder_init (&options, G_VARIANT_TYPE ("a{sv}"));
  g_variant_builder_add_value (&args, g_variant_builder_end (&options));
  g_variant_iter_init (&iter, tail);
  while ((child = g_variant_iter_next_value (&iter)) != NULL)
    {
      g_variant_builder_add_value (&args, child);
      g_variant_unref (child);
    }
  parameters = g_variant_ref_sink (g_variant_builder_end (&args));
  g_variant_unref (tail);

  if (portal->started)
    {
      g_dbus_connection_call (portal->bus, PORTAL_NAME, PORTAL_PATH, REMOTE_IFACE, method, parameters, NULL,
                              G_DBUS_CALL_FLAGS_NONE, 5000, NULL, NULL, NULL);
      g_variant_unref (parameters);
    }
  else
    {
      QueuedCall *call = g_new0 (QueuedCall, 1);

      call->method = g_strdup (method);
      call->parameters = parameters;
      g_queue_push_tail (portal->queued, call);
      if (g_queue_get_length (portal->queued) > 200)
        queued_call_free (g_queue_pop_head (portal->queued));
    }
}

static gboolean
portal_open (TandemInputBackend *backend, GError **error)
{
  Portal *portal = g_new0 (Portal, 1);

  portal->bus = g_bus_get_sync (G_BUS_TYPE_SESSION, NULL, error);
  if (portal->bus == NULL)
    {
      g_free (portal);
      return FALSE;
    }
  portal->queued = g_queue_new ();
  backend->data = portal;
  start_session (portal);
  return TRUE;
}

static void
portal_close (TandemInputBackend *backend)
{
  Portal *portal = backend->data;

  if (portal == NULL)
    return;
  if (portal->session != NULL)
    g_dbus_connection_call (portal->bus, PORTAL_NAME, portal->session, SESSION_IFACE, "Close", NULL, NULL,
                            G_DBUS_CALL_FLAGS_NONE, 2000, NULL, NULL, NULL);
  if (portal->closed_subscription != 0)
    g_dbus_connection_signal_unsubscribe (portal->bus, portal->closed_subscription);
  g_queue_free_full (portal->queued, (GDestroyNotify) queued_call_free);
  g_free (portal->session);
  g_object_unref (portal->bus);
  g_free (portal);
  backend->data = NULL;
}

static void
portal_pointer_motion (TandemInputBackend *backend, double dx, double dy)
{
  notify (backend->data, "NotifyPointerMotion", g_variant_new ("(dd)", dx, dy));
}

static void
portal_pointer_button (TandemInputBackend *backend, guint32 button, gboolean pressed)
{
  notify (backend->data, "NotifyPointerButton", g_variant_new ("(iu)", (gint32) button, pressed ? 1u : 0u));
}

static void
portal_pointer_scroll (TandemInputBackend *backend, double dx, double dy)
{
  notify (backend->data, "NotifyPointerAxis", g_variant_new ("(dd)", dx, dy));
}

static void
portal_keysym (TandemInputBackend *backend, guint32 keysym, gboolean pressed)
{
  notify (backend->data, "NotifyKeyboardKeysym", g_variant_new ("(iu)", (gint32) keysym, pressed ? 1u : 0u));
}

static TandemInputBackend portal_backend = {
  .name = "portal",
  .available = portal_available,
  .open = portal_open,
  .close = portal_close,
  .pointer_motion = portal_pointer_motion,
  .pointer_button = portal_pointer_button,
  .pointer_scroll = portal_pointer_scroll,
  .keysym = portal_keysym,
  .has_keyboard = TRUE,
  .has_pointer = TRUE,
};

TandemInputBackend *
tandem_input_portal_backend (void)
{
  return &portal_backend;
}
