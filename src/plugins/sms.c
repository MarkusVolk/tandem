/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "sms.h"

#include "../device.h"
#include "../notify.h"
#include "../transfer.h"
#include "../util.h"
#include "../vcard.h"
#include "contacts.h"

#include <adwaita.h>
#include <string.h>

#define MESSAGE_PAGE 50

struct _TandemSmsMessage
{
  GObject parent_instance;

  gint64 id;
  gint64 thread_id;
  gint64 date;
  gint64 type;
  gint64 sub_id;
  gboolean read;
  char *body;
  GStrv addresses;
  GPtrArray *attachments;
};

G_DEFINE_FINAL_TYPE (TandemSmsMessage, tandem_sms_message, G_TYPE_OBJECT)

struct _TandemSmsThread
{
  GObject parent_instance;

  gint64 id;
  GStrv addresses;
  GListStore *messages;
  gboolean loaded;
};

G_DEFINE_FINAL_TYPE (TandemSmsThread, tandem_sms_thread, G_TYPE_OBJECT)

struct _TandemSmsPlugin
{
  TandemPlugin parent_instance;

  GListStore *threads;
  char *attachment_dir;
  gboolean requested;
};

G_DEFINE_FINAL_TYPE (TandemSmsPlugin, tandem_sms_plugin, TANDEM_TYPE_PLUGIN)

enum { THREADS_CHANGED, THREAD_UPDATED, N_SIGNALS };
static guint signals[N_SIGNALS];

static const char *const sms_incoming[] = { "kdeconnect.sms.messages", "kdeconnect.sms.attachment_file", NULL };
static const char *const sms_outgoing[] = {
  "kdeconnect.sms.request",
  "kdeconnect.sms.request_conversations",
  "kdeconnect.sms.request_conversation",
  "kdeconnect.sms.request_attachment",
  NULL
};

static void
attachment_free (TandemSmsAttachment *a)
{
  g_free (a->mime_type);
  g_free (a->unique_identifier);
  g_clear_pointer (&a->thumbnail, g_bytes_unref);
  g_free (a);
}

static void
tandem_sms_message_finalize (GObject *object)
{
  TandemSmsMessage *self = TANDEM_SMS_MESSAGE (object);

  g_free (self->body);
  g_strfreev (self->addresses);
  g_ptr_array_unref (self->attachments);
  G_OBJECT_CLASS (tandem_sms_message_parent_class)->finalize (object);
}

static void
tandem_sms_message_class_init (TandemSmsMessageClass *klass)
{
  G_OBJECT_CLASS (klass)->finalize = tandem_sms_message_finalize;
}

static void
tandem_sms_message_init (TandemSmsMessage *self)
{
  self->attachments = g_ptr_array_new_with_free_func ((GDestroyNotify) attachment_free);
}

gint64 tandem_sms_message_get_id (TandemSmsMessage *self) { return self->id; }
const char *tandem_sms_message_get_body (TandemSmsMessage *self) { return self->body != NULL ? self->body : ""; }
gint64 tandem_sms_message_get_date (TandemSmsMessage *self) { return self->date; }
gboolean tandem_sms_message_is_outgoing (TandemSmsMessage *self) { return self->type != 1; }
gboolean tandem_sms_message_is_read (TandemSmsMessage *self) { return self->read; }
const char *const *tandem_sms_message_get_addresses (TandemSmsMessage *self) { return (const char *const *) self->addresses; }
GPtrArray *tandem_sms_message_get_attachments (TandemSmsMessage *self) { return self->attachments; }

static void
tandem_sms_thread_finalize (GObject *object)
{
  TandemSmsThread *self = TANDEM_SMS_THREAD (object);

  g_strfreev (self->addresses);
  g_object_unref (self->messages);
  G_OBJECT_CLASS (tandem_sms_thread_parent_class)->finalize (object);
}

static void
tandem_sms_thread_class_init (TandemSmsThreadClass *klass)
{
  G_OBJECT_CLASS (klass)->finalize = tandem_sms_thread_finalize;
}

static void
tandem_sms_thread_init (TandemSmsThread *self)
{
  self->messages = g_list_store_new (TANDEM_TYPE_SMS_MESSAGE);
}

gint64 tandem_sms_thread_get_id (TandemSmsThread *self) { return self->id; }
const char *const *tandem_sms_thread_get_addresses (TandemSmsThread *self) { return (const char *const *) self->addresses; }
GListModel *tandem_sms_thread_get_messages (TandemSmsThread *self) { return G_LIST_MODEL (self->messages); }
gboolean tandem_sms_thread_is_loaded (TandemSmsThread *self) { return self->loaded; }

TandemSmsMessage *
tandem_sms_thread_get_last (TandemSmsThread *self)
{
  guint n = g_list_model_get_n_items (G_LIST_MODEL (self->messages));
  g_autoptr(TandemSmsMessage) last = NULL;

  if (n == 0)
    return NULL;
  last = g_list_model_get_item (G_LIST_MODEL (self->messages), n - 1);
  return last;
}

char *
tandem_sms_thread_get_title (TandemSmsThread *self, TandemDevice *device)
{
  GString *title = g_string_new (NULL);

  for (char **a = self->addresses; a != NULL && *a != NULL; a++)
    {
      TandemContact *contact = tandem_device_lookup_contact (device, *a);

      if (title->len > 0)
        g_string_append (title, ", ");
      g_string_append (title, contact != NULL ? contact->name : *a);
    }
  if (title->len == 0)
    g_string_append (title, "Unknown");
  return g_string_free (title, FALSE);
}

static int
compare_messages (gconstpointer a, gconstpointer b, gpointer user_data)
{
  gint64 da = TANDEM_SMS_MESSAGE ((gpointer) a)->date;
  gint64 db = TANDEM_SMS_MESSAGE ((gpointer) b)->date;

  return da < db ? -1 : (da > db ? 1 : 0);
}

static int
compare_threads (gconstpointer a, gconstpointer b, gpointer user_data)
{
  TandemSmsMessage *la = tandem_sms_thread_get_last (TANDEM_SMS_THREAD ((gpointer) a));
  TandemSmsMessage *lb = tandem_sms_thread_get_last (TANDEM_SMS_THREAD ((gpointer) b));
  gint64 da = la != NULL ? la->date : 0;
  gint64 db = lb != NULL ? lb->date : 0;

  return da > db ? -1 : (da < db ? 1 : 0);
}

static TandemSmsThread *
find_thread (TandemSmsPlugin *self, gint64 id, guint *position)
{
  guint n = g_list_model_get_n_items (G_LIST_MODEL (self->threads));

  for (guint i = 0; i < n; i++)
    {
      g_autoptr(TandemSmsThread) thread = g_list_model_get_item (G_LIST_MODEL (self->threads), i);

      if (thread->id == id)
        {
          if (position != NULL)
            *position = i;
          return thread;
        }
    }
  return NULL;
}

static GStrv
addresses_from_array (JsonArray *array)
{
  g_autoptr(GStrvBuilder) builder = g_strv_builder_new ();

  for (guint i = 0; array != NULL && i < json_array_get_length (array); i++)
    {
      JsonNode *node = json_array_get_element (array, i);

      if (JSON_NODE_HOLDS_OBJECT (node))
        {
          JsonObject *obj = json_node_get_object (node);

          if (json_object_has_member (obj, "address"))
            g_strv_builder_add (builder, json_object_get_string_member (obj, "address"));
        }
      else if (JSON_NODE_HOLDS_VALUE (node))
        {
          g_strv_builder_add (builder, json_node_get_string (node));
        }
    }
  return g_strv_builder_end (builder);
}

static TandemSmsMessage *
message_from_json (JsonObject *obj)
{
  TandemSmsMessage *message = g_object_new (TANDEM_TYPE_SMS_MESSAGE, NULL);
  JsonArray *attachments;

  message->id = json_object_has_member (obj, "_id") ? json_object_get_int_member (obj, "_id") : 0;
  message->thread_id = json_object_has_member (obj, "thread_id") ? json_object_get_int_member (obj, "thread_id") : 0;
  message->date = json_object_has_member (obj, "date") ? json_object_get_int_member (obj, "date") : 0;
  message->type = json_object_has_member (obj, "type") ? json_object_get_int_member (obj, "type") : 1;
  message->sub_id = json_object_has_member (obj, "sub_id") ? json_object_get_int_member (obj, "sub_id") : -1;
  message->read = json_object_has_member (obj, "read") && json_object_get_int_member (obj, "read") != 0;
  message->body = json_object_has_member (obj, "body") && !json_object_get_null_member (obj, "body")
                    ? g_strdup (json_object_get_string_member (obj, "body")) : g_strdup ("");
  message->addresses = addresses_from_array (json_object_has_member (obj, "addresses")
                                               ? json_object_get_array_member (obj, "addresses") : NULL);
  attachments = json_object_has_member (obj, "attachments") ? json_object_get_array_member (obj, "attachments") : NULL;
  for (guint i = 0; attachments != NULL && i < json_array_get_length (attachments); i++)
    {
      JsonObject *a = json_array_get_object_element (attachments, i);
      TandemSmsAttachment *attachment = g_new0 (TandemSmsAttachment, 1);

      attachment->part_id = json_object_has_member (a, "part_id") ? json_object_get_int_member (a, "part_id") : 0;
      attachment->mime_type = g_strdup (json_object_has_member (a, "mime_type") ? json_object_get_string_member (a, "mime_type") : "");
      attachment->unique_identifier = g_strdup (json_object_has_member (a, "unique_identifier")
                                                  ? json_object_get_string_member (a, "unique_identifier") : "");
      if (json_object_has_member (a, "encoded_thumbnail"))
        {
          gsize len = 0;
          guchar *data = g_base64_decode (json_object_get_string_member (a, "encoded_thumbnail"), &len);

          if (len > 0)
            attachment->thumbnail = g_bytes_new_take (data, len);
          else
            g_free (data);
        }
      g_ptr_array_add (message->attachments, attachment);
    }
  return message;
}

static gboolean
thread_has_message (TandemSmsThread *thread, gint64 id, guint *position)
{
  guint n = g_list_model_get_n_items (G_LIST_MODEL (thread->messages));

  for (guint i = 0; i < n; i++)
    {
      g_autoptr(TandemSmsMessage) m = g_list_model_get_item (G_LIST_MODEL (thread->messages), i);

      if (m->id == id)
        {
          if (position != NULL)
            *position = i;
          return TRUE;
        }
    }
  return FALSE;
}

static void
notify_incoming (TandemSmsPlugin *self, TandemSmsThread *thread, TandemSmsMessage *message)
{
  TandemDevice *device = tandem_plugin_get_device (TANDEM_PLUGIN (self));
  g_autofree char *title = tandem_sms_thread_get_title (thread, device);
  TandemNotification *notification;

  if (message->read || tandem_sms_message_is_outgoing (message))
    return;
  if (tandem_now_ms () - message->date > 60 * 1000)
    return;
  notification = tandem_notification_new (title, message->body);
  tandem_notification_set_icon (notification, "mail-unread-symbolic");
  tandem_notification_set_category (notification, "im.received");
  tandem_notification_send (notification);
}

static void
handle_messages (TandemSmsPlugin *self, TandemPacket *packet)
{
  JsonArray *messages = tandem_packet_get_array (packet, "messages");
  g_autoptr(GHashTable) touched = g_hash_table_new (g_direct_hash, g_direct_equal);
  GHashTableIter iter;
  gpointer key;
  gboolean list_changed = FALSE;

  for (guint i = 0; messages != NULL && i < json_array_get_length (messages); i++)
    {
      JsonNode *node = json_array_get_element (messages, i);
      g_autoptr(TandemSmsMessage) message = NULL;
      TandemSmsThread *thread;
      guint existing;

      if (!JSON_NODE_HOLDS_OBJECT (node))
        continue;
      message = message_from_json (json_node_get_object (node));
      thread = find_thread (self, message->thread_id, NULL);
      if (thread == NULL)
        {
          g_autoptr(TandemSmsThread) new_thread = g_object_new (TANDEM_TYPE_SMS_THREAD, NULL);

          new_thread->id = message->thread_id;
          new_thread->addresses = g_strdupv (message->addresses);
          g_list_store_append (self->threads, new_thread);
          thread = new_thread;
          list_changed = TRUE;
        }
      if (thread_has_message (thread, message->id, &existing))
        {
          g_list_store_remove (thread->messages, existing);
        }
      else if (self->requested)
        {
          notify_incoming (self, thread, message);
        }
      g_list_store_insert_sorted (thread->messages, message, compare_messages, NULL);
      g_hash_table_add (touched, thread);
      list_changed = TRUE;
    }

  if (list_changed)
    g_list_store_sort (self->threads, compare_threads, NULL);
  g_hash_table_iter_init (&iter, touched);
  while (g_hash_table_iter_next (&iter, &key, NULL))
    g_signal_emit (self, signals[THREAD_UPDATED], 0, key);
  if (list_changed)
    g_signal_emit (self, signals[THREADS_CHANGED], 0);
}

static void
attachment_saved_cb (GObject *source, GAsyncResult *result, gpointer user_data)
{
  g_autofree char *path = user_data;
  g_autoptr(GError) error = NULL;

  if (!tandem_transfer_download_to_file_finish (TANDEM_TRANSFER (source), result, &error))
    g_warning ("sms: attachment download failed: %s", error->message);
  else
    {
      g_autoptr(GFile) file = g_file_new_for_path (path);
      g_autoptr(GtkFileLauncher) launcher = gtk_file_launcher_new (file);

      gtk_file_launcher_launch (launcher, NULL, NULL, NULL, NULL);
    }
  g_object_unref (source);
}

static void
handle_attachment_file (TandemSmsPlugin *self, TandemPacket *packet)
{
  const char *filename = tandem_packet_get_string (packet, "filename", "attachment");
  g_autofree char *base = g_path_get_basename (filename);
  g_autofree char *path = g_build_filename (self->attachment_dir, base, NULL);
  g_autoptr(GFile) file = g_file_new_for_path (path);
  TandemTransfer *transfer = tandem_device_download (tandem_plugin_get_device (TANDEM_PLUGIN (self)), packet);

  if (transfer == NULL)
    return;
  tandem_ensure_dir (self->attachment_dir, NULL);
  tandem_transfer_download_to_file_async (transfer, file, NULL, attachment_saved_cb, g_steal_pointer (&path));
}

static void
sms_handle_packet (TandemPlugin *plugin, TandemPacket *packet)
{
  TandemSmsPlugin *self = TANDEM_SMS_PLUGIN (plugin);

  if (tandem_packet_is_type (packet, "kdeconnect.sms.messages"))
    handle_messages (self, packet);
  else if (tandem_packet_is_type (packet, "kdeconnect.sms.attachment_file"))
    handle_attachment_file (self, packet);
}

GListModel *
tandem_sms_plugin_get_threads (TandemSmsPlugin *self)
{
  return G_LIST_MODEL (self->threads);
}

void
tandem_sms_plugin_request_threads (TandemSmsPlugin *self)
{
  g_autoptr(TandemPacket) packet = tandem_packet_new ("kdeconnect.sms.request_conversations");

  tandem_plugin_send (TANDEM_PLUGIN (self), packet);
  self->requested = TRUE;
}

void
tandem_sms_plugin_request_messages (TandemSmsPlugin *self, TandemSmsThread *thread, gint64 before, int count)
{
  g_autoptr(TandemPacket) packet = tandem_packet_new ("kdeconnect.sms.request_conversation");

  tandem_packet_set_int (packet, "threadID", thread->id);
  if (before > 0)
    tandem_packet_set_int (packet, "rangeStartTimestamp", before);
  tandem_packet_set_int (packet, "numberToRequest", count > 0 ? count : MESSAGE_PAGE);
  tandem_plugin_send (TANDEM_PLUGIN (self), packet);
  thread->loaded = TRUE;
}

void
tandem_sms_plugin_send (TandemSmsPlugin *self, const char *const *addresses, const char *body, gint64 sub_id)
{
  g_autoptr(TandemPacket) packet = tandem_packet_new ("kdeconnect.sms.request");
  g_autoptr(JsonArray) array = json_array_new ();

  for (const char *const *a = addresses; a != NULL && *a != NULL; a++)
    {
      JsonObject *obj = json_object_new ();

      json_object_set_string_member (obj, "address", *a);
      json_array_add_object_element (array, obj);
    }
  json_object_set_array_member (packet->body, "addresses", json_array_ref (array));
  tandem_packet_set_string (packet, "messageBody", body);
  tandem_packet_set_int (packet, "version", 2);
  if (sub_id >= 0)
    tandem_packet_set_int (packet, "subID", sub_id);
  tandem_plugin_send (TANDEM_PLUGIN (self), packet);
}

char *
tandem_sms_plugin_attachment_path (TandemSmsPlugin *self, TandemSmsAttachment *attachment)
{
  g_autofree char *base = g_path_get_basename (attachment->unique_identifier);

  return g_build_filename (self->attachment_dir, base, NULL);
}

void
tandem_sms_plugin_request_attachment (TandemSmsPlugin *self, TandemSmsAttachment *attachment)
{
  g_autofree char *path = tandem_sms_plugin_attachment_path (self, attachment);
  g_autoptr(TandemPacket) packet = NULL;

  if (g_file_test (path, G_FILE_TEST_EXISTS))
    {
      g_autoptr(GFile) file = g_file_new_for_path (path);
      g_autoptr(GtkFileLauncher) launcher = gtk_file_launcher_new (file);

      gtk_file_launcher_launch (launcher, NULL, NULL, NULL, NULL);
      return;
    }
  packet = tandem_packet_new ("kdeconnect.sms.request_attachment");
  tandem_packet_set_int (packet, "part_id", attachment->part_id);
  tandem_packet_set_string (packet, "unique_identifier", attachment->unique_identifier);
  tandem_plugin_send (TANDEM_PLUGIN (self), packet);
}

static void
sms_connected (TandemPlugin *plugin)
{
  tandem_sms_plugin_request_threads (TANDEM_SMS_PLUGIN (plugin));
}

static void
sms_disconnected (TandemPlugin *plugin)
{
  TANDEM_SMS_PLUGIN (plugin)->requested = FALSE;
}

GtkWidget *tandem_sms_page_new (TandemSmsPlugin *plugin);

static void
messages_row_activated_cb (AdwActionRow *row, gpointer user_data)
{
  TandemSmsPlugin *self = user_data;
  GtkWidget *view = gtk_widget_get_ancestor (GTK_WIDGET (row), ADW_TYPE_NAVIGATION_VIEW);

  if (view != NULL)
    adw_navigation_view_push (ADW_NAVIGATION_VIEW (view), ADW_NAVIGATION_PAGE (tandem_sms_page_new (self)));
}

static GtkWidget *
sms_create_group (TandemPlugin *plugin)
{
  GtkWidget *group = adw_preferences_group_new ();
  GtkWidget *row = adw_action_row_new ();

  adw_preferences_group_set_title (ADW_PREFERENCES_GROUP (group), "Messages");
  adw_preferences_row_set_title (ADW_PREFERENCES_ROW (row), "Open conversations");
  adw_action_row_add_suffix (ADW_ACTION_ROW (row), gtk_image_new_from_icon_name ("go-next-symbolic"));
  gtk_list_box_row_set_activatable (GTK_LIST_BOX_ROW (row), TRUE);
  g_signal_connect (row, "activated", G_CALLBACK (messages_row_activated_cb), plugin);
  adw_preferences_group_add (ADW_PREFERENCES_GROUP (group), row);
  return group;
}

static void
tandem_sms_plugin_constructed (GObject *object)
{
  TandemSmsPlugin *self = TANDEM_SMS_PLUGIN (object);
  TandemDevice *device = tandem_plugin_get_device (TANDEM_PLUGIN (self));

  G_OBJECT_CLASS (tandem_sms_plugin_parent_class)->constructed (object);
  self->attachment_dir = g_build_filename (g_get_user_cache_dir (), "tandem", tandem_device_get_id (device),
                                           "sms-attachments", NULL);
}

static void
tandem_sms_plugin_finalize (GObject *object)
{
  TandemSmsPlugin *self = TANDEM_SMS_PLUGIN (object);

  g_object_unref (self->threads);
  g_free (self->attachment_dir);
  G_OBJECT_CLASS (tandem_sms_plugin_parent_class)->finalize (object);
}

static void
tandem_sms_plugin_class_init (TandemSmsPluginClass *klass)
{
  TandemPluginClass *plugin_class = TANDEM_PLUGIN_CLASS (klass);
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->constructed = tandem_sms_plugin_constructed;
  object_class->finalize = tandem_sms_plugin_finalize;
  plugin_class->name = "sms";
  plugin_class->title = "Messages";
  plugin_class->incoming = sms_incoming;
  plugin_class->outgoing = sms_outgoing;
  plugin_class->handle_packet = sms_handle_packet;
  plugin_class->connected = sms_connected;
  plugin_class->disconnected = sms_disconnected;
  plugin_class->create_group = sms_create_group;

  signals[THREADS_CHANGED] = g_signal_new ("threads-changed", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST, 0,
                                           NULL, NULL, NULL, G_TYPE_NONE, 0);
  signals[THREAD_UPDATED] = g_signal_new ("thread-updated", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST, 0,
                                          NULL, NULL, NULL, G_TYPE_NONE, 1, TANDEM_TYPE_SMS_THREAD);
}

static void
tandem_sms_plugin_init (TandemSmsPlugin *self)
{
  self->threads = g_list_store_new (TANDEM_TYPE_SMS_THREAD);
}
