/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "notify.h"
#include "tandem-build.h"

#include <string.h>

#define FDO_NAME "org.freedesktop.Notifications"
#define FDO_PATH "/org/freedesktop/Notifications"
#define FDO_IFACE "org.freedesktop.Notifications"

struct _TandemNotification
{
  char *summary;
  char *body;
  char *icon;
  guint32 replaces_id;
  gint32 timeout_ms;
  GPtrArray *actions;
  GVariantBuilder hints;
  gboolean hints_started;
  TandemNotifyActionFunc on_action;
  TandemNotifyClosedFunc on_closed;
  gpointer user_data;
  GDestroyNotify destroy;
};

typedef struct
{
  TandemNotifyActionFunc on_action;
  TandemNotifyClosedFunc on_closed;
  gpointer user_data;
  GDestroyNotify destroy;
} Pending;

static GDBusConnection *bus;
static GHashTable *pending;
static GStrv capabilities;
static guint action_subscription;
static guint closed_subscription;
static guint replied_subscription;

static void
pending_free (Pending *p)
{
  if (p->destroy != NULL)
    p->destroy (p->user_data);
  g_free (p);
}

static void
signal_cb (GDBusConnection *connection,
           const char      *sender,
           const char      *object_path,
           const char      *interface_name,
           const char      *signal_name,
           GVariant        *parameters,
           gpointer         user_data)
{
  guint32 id = 0;
  Pending *p;

  if (g_str_equal (signal_name, "ActionInvoked"))
    {
      const char *key;

      g_variant_get (parameters, "(u&s)", &id, &key);
      p = g_hash_table_lookup (pending, GUINT_TO_POINTER (id));
      if (p != NULL && p->on_action != NULL)
        p->on_action (id, key, NULL, p->user_data);
    }
  else if (g_str_equal (signal_name, "NotificationReplied"))
    {
      const char *text;

      g_variant_get (parameters, "(u&s)", &id, &text);
      p = g_hash_table_lookup (pending, GUINT_TO_POINTER (id));
      if (p != NULL && p->on_action != NULL)
        p->on_action (id, "inline-reply", text, p->user_data);
    }
  else if (g_str_equal (signal_name, "NotificationClosed"))
    {
      guint32 reason;

      g_variant_get (parameters, "(uu)", &id, &reason);
      p = g_hash_table_lookup (pending, GUINT_TO_POINTER (id));
      if (p != NULL)
        {
          if (p->on_closed != NULL)
            p->on_closed (id, reason, p->user_data);
          g_hash_table_remove (pending, GUINT_TO_POINTER (id));
        }
    }
}

static void
capabilities_cb (GObject *source, GAsyncResult *result, gpointer user_data)
{
  g_autoptr(GVariant) reply = g_dbus_connection_call_finish (G_DBUS_CONNECTION (source), result, NULL);

  if (reply != NULL)
    {
      g_strfreev (capabilities);
      g_variant_get (reply, "(^as)", &capabilities);
    }
}

void
tandem_notify_init (GDBusConnection *connection)
{
  if (bus != NULL)
    return;
  bus = g_object_ref (connection);
  pending = g_hash_table_new_full (g_direct_hash, g_direct_equal, NULL, (GDestroyNotify) pending_free);
  action_subscription = g_dbus_connection_signal_subscribe (bus, FDO_NAME, FDO_IFACE, "ActionInvoked", FDO_PATH,
                                                            NULL, G_DBUS_SIGNAL_FLAGS_NONE, signal_cb, NULL, NULL);
  closed_subscription = g_dbus_connection_signal_subscribe (bus, FDO_NAME, FDO_IFACE, "NotificationClosed", FDO_PATH,
                                                            NULL, G_DBUS_SIGNAL_FLAGS_NONE, signal_cb, NULL, NULL);
  replied_subscription = g_dbus_connection_signal_subscribe (bus, FDO_NAME, FDO_IFACE, "NotificationReplied", FDO_PATH,
                                                             NULL, G_DBUS_SIGNAL_FLAGS_NONE, signal_cb, NULL, NULL);
  g_dbus_connection_call (bus, FDO_NAME, FDO_PATH, FDO_IFACE, "GetCapabilities", NULL,
                          G_VARIANT_TYPE ("(as)"), G_DBUS_CALL_FLAGS_NONE, 5000, NULL, capabilities_cb, NULL);
}

static gboolean
ensure_bus (void)
{
  if (bus == NULL)
    {
      g_autoptr(GError) error = NULL;
      GDBusConnection *connection = g_bus_get_sync (G_BUS_TYPE_SESSION, NULL, &error);

      if (connection == NULL)
        {
          g_warning ("no session bus for notifications: %s", error->message);
          return FALSE;
        }
      tandem_notify_init (connection);
      g_object_unref (connection);
    }
  return TRUE;
}

gboolean
tandem_notify_has_capability (const char *capability)
{
  return capabilities != NULL && g_strv_contains ((const char *const *) capabilities, capability);
}

TandemNotification *
tandem_notification_new (const char *summary, const char *body)
{
  TandemNotification *self = g_new0 (TandemNotification, 1);

  self->summary = g_strdup (summary != NULL ? summary : "");
  self->body = g_strdup (body != NULL ? body : "");
  self->icon = g_strdup (TANDEM_APP_ID);
  self->timeout_ms = -1;
  self->actions = g_ptr_array_new_with_free_func (g_free);
  g_variant_builder_init (&self->hints, G_VARIANT_TYPE ("a{sv}"));
  self->hints_started = TRUE;
  g_variant_builder_add (&self->hints, "{sv}", "desktop-entry", g_variant_new_string (TANDEM_APP_ID));
  return self;
}

void
tandem_notification_free (TandemNotification *self)
{
  if (self == NULL)
    return;
  if (self->hints_started)
    g_variant_builder_clear (&self->hints);
  g_ptr_array_unref (self->actions);
  g_free (self->summary);
  g_free (self->body);
  g_free (self->icon);
  g_free (self);
}

void
tandem_notification_set_icon (TandemNotification *self, const char *icon)
{
  g_free (self->icon);
  self->icon = g_strdup (icon != NULL ? icon : "");
}

void
tandem_notification_set_image (TandemNotification *self, const char *path)
{
  tandem_notification_set_hint (self, "image-path", g_variant_new_string (path));
}

void
tandem_notification_set_replaces (TandemNotification *self, guint32 id)
{
  self->replaces_id = id;
}

void
tandem_notification_set_timeout (TandemNotification *self, gint32 timeout_ms)
{
  self->timeout_ms = timeout_ms;
}

void
tandem_notification_set_urgency (TandemNotification *self, guint8 urgency)
{
  tandem_notification_set_hint (self, "urgency", g_variant_new_byte (urgency));
}

void
tandem_notification_set_category (TandemNotification *self, const char *category)
{
  tandem_notification_set_hint (self, "category", g_variant_new_string (category));
}

void
tandem_notification_set_resident (TandemNotification *self, gboolean resident)
{
  tandem_notification_set_hint (self, "resident", g_variant_new_boolean (resident));
}

void
tandem_notification_set_hint (TandemNotification *self, const char *key, GVariant *value)
{
  g_variant_builder_add (&self->hints, "{sv}", key, value);
}

void
tandem_notification_add_action (TandemNotification *self, const char *key, const char *label)
{
  g_ptr_array_add (self->actions, g_strdup (key));
  g_ptr_array_add (self->actions, g_strdup (label));
}

void
tandem_notification_set_callbacks (TandemNotification     *self,
                                   TandemNotifyActionFunc  on_action,
                                   TandemNotifyClosedFunc  on_closed,
                                   gpointer                user_data,
                                   GDestroyNotify          destroy)
{
  self->on_action = on_action;
  self->on_closed = on_closed;
  self->user_data = user_data;
  self->destroy = destroy;
}

static void
notify_reply_cb (GObject *source, GAsyncResult *result, gpointer user_data)
{
  g_autoptr(GTask) task = user_data;
  Pending *p = g_task_get_task_data (task);
  g_autoptr(GError) error = NULL;
  g_autoptr(GVariant) reply = g_dbus_connection_call_finish (G_DBUS_CONNECTION (source), result, &error);
  guint32 id = 0;

  if (reply == NULL)
    {
      g_task_return_error (task, g_steal_pointer (&error));
      return;
    }
  g_variant_get (reply, "(u)", &id);
  if (p != NULL && id != 0)
    {
      Pending *stored = g_new0 (Pending, 1);

      *stored = *p;
      p->destroy = NULL;
      g_hash_table_insert (pending, GUINT_TO_POINTER (id), stored);
    }
  g_task_return_int (task, id);
}

void
tandem_notification_send_async (TandemNotification *self, GAsyncReadyCallback callback, gpointer user_data)
{
  GTask *task = g_task_new (NULL, NULL, callback, user_data);
  GVariantBuilder actions;
  Pending *p = NULL;

  if (!ensure_bus ())
    {
      g_task_return_new_error (task, G_IO_ERROR, G_IO_ERROR_FAILED, "No session bus");
      g_object_unref (task);
      return;
    }

  if (self->on_action != NULL || self->on_closed != NULL)
    {
      p = g_new0 (Pending, 1);
      p->on_action = self->on_action;
      p->on_closed = self->on_closed;
      p->user_data = self->user_data;
      p->destroy = self->destroy;
      self->destroy = NULL;
      g_task_set_task_data (task, p, (GDestroyNotify) pending_free);
    }

  g_variant_builder_init (&actions, G_VARIANT_TYPE ("as"));
  for (guint i = 0; i < self->actions->len; i++)
    g_variant_builder_add (&actions, "s", (const char *) g_ptr_array_index (self->actions, i));

  if (self->replaces_id != 0)
    g_hash_table_remove (pending, GUINT_TO_POINTER (self->replaces_id));

  g_dbus_connection_call (bus, FDO_NAME, FDO_PATH, FDO_IFACE, "Notify",
                          g_variant_new ("(susssasa{sv}i)", "Tandem", self->replaces_id, self->icon,
                                         self->summary, self->body, &actions, &self->hints, self->timeout_ms),
                          G_VARIANT_TYPE ("(u)"), G_DBUS_CALL_FLAGS_NONE, 10000, NULL, notify_reply_cb, task);
  self->hints_started = FALSE;
}

guint32
tandem_notification_send_finish (GAsyncResult *result, GError **error)
{
  gssize id = g_task_propagate_int (G_TASK (result), error);

  return id < 0 ? 0 : (guint32) id;
}

static void
fire_and_forget_cb (GObject *source, GAsyncResult *result, gpointer user_data)
{
  g_autoptr(GError) error = NULL;

  if (tandem_notification_send_finish (result, &error) == 0 && error != NULL)
    g_debug ("notification failed: %s", error->message);
}

void
tandem_notification_send (TandemNotification *self)
{
  tandem_notification_send_async (self, fire_and_forget_cb, NULL);
}

void
tandem_notify_close (guint32 id)
{
  if (id == 0 || !ensure_bus ())
    return;
  g_dbus_connection_call (bus, FDO_NAME, FDO_PATH, FDO_IFACE, "CloseNotification", g_variant_new ("(u)", id),
                          NULL, G_DBUS_CALL_FLAGS_NONE, 5000, NULL, NULL, NULL);
}
