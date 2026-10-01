/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once

#include <gio/gio.h>

G_BEGIN_DECLS

typedef void (*TandemNotifyActionFunc) (guint32     id,
                                        const char *action,
                                        const char *reply,
                                        gpointer    user_data);
typedef void (*TandemNotifyClosedFunc) (guint32  id,
                                        guint32  reason,
                                        gpointer user_data);

typedef struct _TandemNotification TandemNotification;

TandemNotification *tandem_notification_new        (const char *summary, const char *body);
void                tandem_notification_free       (TandemNotification *self);
void                tandem_notification_set_icon   (TandemNotification *self, const char *icon);
void                tandem_notification_set_image  (TandemNotification *self, const char *path);
void                tandem_notification_set_replaces (TandemNotification *self, guint32 id);
void                tandem_notification_set_timeout (TandemNotification *self, gint32 timeout_ms);
void                tandem_notification_set_urgency (TandemNotification *self, guint8 urgency);
void                tandem_notification_set_category (TandemNotification *self, const char *category);
void                tandem_notification_set_resident (TandemNotification *self, gboolean resident);
void                tandem_notification_set_hint   (TandemNotification *self, const char *key, GVariant *value);
void                tandem_notification_add_action (TandemNotification *self, const char *key, const char *label);
void                tandem_notification_set_callbacks (TandemNotification     *self,
                                                       TandemNotifyActionFunc  on_action,
                                                       TandemNotifyClosedFunc  on_closed,
                                                       gpointer                user_data,
                                                       GDestroyNotify          destroy);
void                tandem_notification_send_async (TandemNotification  *self,
                                                    GAsyncReadyCallback  callback,
                                                    gpointer             user_data);
guint32             tandem_notification_send_finish (GAsyncResult *result, GError **error);
void                tandem_notification_send       (TandemNotification *self);

void                tandem_notify_close            (guint32 id);
gboolean            tandem_notify_has_capability   (const char *capability);
void                tandem_notify_init             (GDBusConnection *connection);

G_DEFINE_AUTOPTR_CLEANUP_FUNC (TandemNotification, tandem_notification_free)

G_END_DECLS
