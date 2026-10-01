/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once

#include "../plugin.h"

G_BEGIN_DECLS

#define TANDEM_TYPE_SMS_PLUGIN (tandem_sms_plugin_get_type ())
G_DECLARE_FINAL_TYPE (TandemSmsPlugin, tandem_sms_plugin, TANDEM, SMS_PLUGIN, TandemPlugin)

#define TANDEM_TYPE_SMS_MESSAGE (tandem_sms_message_get_type ())
G_DECLARE_FINAL_TYPE (TandemSmsMessage, tandem_sms_message, TANDEM, SMS_MESSAGE, GObject)

#define TANDEM_TYPE_SMS_THREAD (tandem_sms_thread_get_type ())
G_DECLARE_FINAL_TYPE (TandemSmsThread, tandem_sms_thread, TANDEM, SMS_THREAD, GObject)

typedef struct
{
  gint64 part_id;
  char *mime_type;
  char *unique_identifier;
  GBytes *thumbnail;
} TandemSmsAttachment;

gint64               tandem_sms_message_get_id          (TandemSmsMessage *self);
const char          *tandem_sms_message_get_body        (TandemSmsMessage *self);
gint64               tandem_sms_message_get_date        (TandemSmsMessage *self);
gboolean             tandem_sms_message_is_outgoing     (TandemSmsMessage *self);
gboolean             tandem_sms_message_is_read         (TandemSmsMessage *self);
const char *const   *tandem_sms_message_get_addresses   (TandemSmsMessage *self);
GPtrArray           *tandem_sms_message_get_attachments (TandemSmsMessage *self);

gint64               tandem_sms_thread_get_id           (TandemSmsThread *self);
const char *const   *tandem_sms_thread_get_addresses    (TandemSmsThread *self);
char                *tandem_sms_thread_get_title        (TandemSmsThread *self, TandemDevice *device);
TandemSmsMessage    *tandem_sms_thread_get_last         (TandemSmsThread *self);
GListModel          *tandem_sms_thread_get_messages     (TandemSmsThread *self);
gboolean             tandem_sms_thread_is_loaded        (TandemSmsThread *self);

GListModel      *tandem_sms_plugin_get_threads         (TandemSmsPlugin *self);
void             tandem_sms_plugin_request_threads     (TandemSmsPlugin *self);
void             tandem_sms_plugin_request_messages    (TandemSmsPlugin *self, TandemSmsThread *thread, gint64 before, int count);
void             tandem_sms_plugin_send                (TandemSmsPlugin *self, const char *const *addresses, const char *body, gint64 sub_id);
void             tandem_sms_plugin_request_attachment  (TandemSmsPlugin *self, TandemSmsAttachment *attachment);
char            *tandem_sms_plugin_attachment_path     (TandemSmsPlugin *self, TandemSmsAttachment *attachment);

G_END_DECLS
