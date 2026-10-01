/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "kodi.h"

#include <string.h>

#define KODI_HOST "127.0.0.1"
#define KODI_PORT 9090
#define RECONNECT_SECONDS 5
#define MAX_MESSAGE (4 * 1024 * 1024)

typedef struct
{
  TandemKodiReply callback;
  gpointer user_data;
} Pending;

struct _TandemKodi
{
  GObject parent_instance;

  GSocketClient *client;
  GSocketConnection *connection;
  GCancellable *cancellable;
  GString *buffer;
  char read_buf[8192];
  int depth;
  gboolean in_string;
  gboolean escaped;
  gsize scan_pos;
  guint next_id;
  GHashTable *pending;
  guint reconnect_id;
  gboolean connecting;
  GQueue *outgoing;
  gboolean writing;
};

G_DEFINE_FINAL_TYPE (TandemKodi, tandem_kodi, G_TYPE_OBJECT)

enum { NOTIFICATION, CONNECTED_CHANGED, N_SIGNALS };
static guint signals[N_SIGNALS];

static void connect_now (TandemKodi *self);

static void
pending_free (Pending *p)
{
  if (p->callback != NULL)
    p->callback (NULL, p->user_data);
  g_free (p);
}

static void
schedule_reconnect (TandemKodi *self)
{
  if (self->reconnect_id == 0)
    self->reconnect_id = g_timeout_add_seconds_once (RECONNECT_SECONDS, (GSourceOnceFunc) connect_now, self);
}

static void
drop_connection (TandemKodi *self)
{
  gboolean was_connected = self->connection != NULL;

  g_cancellable_cancel (self->cancellable);
  g_clear_object (&self->cancellable);
  g_clear_object (&self->connection);
  g_string_truncate (self->buffer, 0);
  self->depth = 0;
  self->in_string = FALSE;
  self->escaped = FALSE;
  self->scan_pos = 0;
  g_hash_table_remove_all (self->pending);
  g_queue_clear_full (self->outgoing, g_free);
  self->writing = FALSE;
  if (was_connected)
    {
      g_debug ("kodi: connection closed");
      g_signal_emit (self, signals[CONNECTED_CHANGED], 0);
    }
  schedule_reconnect (self);
}

static void
handle_message (TandemKodi *self, const char *text, gsize len)
{
  g_autoptr(JsonParser) parser = json_parser_new ();
  JsonObject *obj;

  if (!json_parser_load_from_data (parser, text, len, NULL))
    return;
  if (!JSON_NODE_HOLDS_OBJECT (json_parser_get_root (parser)))
    return;
  obj = json_node_get_object (json_parser_get_root (parser));
  if (json_object_has_member (obj, "id") && !json_object_get_null_member (obj, "id"))
    {
      guint id = (guint) json_object_get_int_member (obj, "id");
      Pending *p = g_hash_table_lookup (self->pending, GUINT_TO_POINTER (id));

      if (p == NULL)
        return;
      if (p->callback != NULL)
        p->callback (json_object_has_member (obj, "result") ? json_object_get_member (obj, "result") : NULL, p->user_data);
      p->callback = NULL;
      g_hash_table_remove (self->pending, GUINT_TO_POINTER (id));
    }
  else if (json_object_has_member (obj, "method"))
    {
      g_signal_emit (self, signals[NOTIFICATION], 0, json_object_get_string_member (obj, "method"));
    }
}

static void
scan_buffer (TandemKodi *self)
{
  gsize start = 0;

  for (gsize i = self->scan_pos; i < self->buffer->len; i++)
    {
      char c = self->buffer->str[i];

      if (self->in_string)
        {
          if (self->escaped)
            self->escaped = FALSE;
          else if (c == '\\')
            self->escaped = TRUE;
          else if (c == '"')
            self->in_string = FALSE;
          continue;
        }
      if (c == '"')
        self->in_string = TRUE;
      else if (c == '{')
        {
          if (self->depth == 0)
            start = i;
          self->depth++;
        }
      else if (c == '}' && self->depth > 0)
        {
          self->depth--;
          if (self->depth == 0)
            {
              handle_message (self, self->buffer->str + start, i - start + 1);
              g_string_erase (self->buffer, 0, i + 1);
              i = (gsize) -1;
              start = 0;
              if (self->connection == NULL)
                return;
            }
        }
    }
  if (self->depth == 0)
    g_string_truncate (self->buffer, 0);
  self->scan_pos = self->buffer->len;
  if (self->buffer->len > MAX_MESSAGE)
    drop_connection (self);
}

static void read_more (TandemKodi *self);

static void
read_cb (GObject *source, GAsyncResult *result, gpointer user_data)
{
  TandemKodi *self = user_data;
  g_autoptr(GError) error = NULL;
  gssize n = g_input_stream_read_finish (G_INPUT_STREAM (source), result, &error);

  if (g_error_matches (error, G_IO_ERROR, G_IO_ERROR_CANCELLED))
    return;
  if (n <= 0)
    {
      drop_connection (self);
      return;
    }
  g_string_append_len (self->buffer, self->read_buf, n);
  scan_buffer (self);
  if (self->connection != NULL)
    read_more (self);
}

static void
read_more (TandemKodi *self)
{
  g_input_stream_read_async (g_io_stream_get_input_stream (G_IO_STREAM (self->connection)), self->read_buf,
                             sizeof self->read_buf, G_PRIORITY_DEFAULT, self->cancellable, read_cb, self);
}

static void
connected_cb (GObject *source, GAsyncResult *result, gpointer user_data)
{
  TandemKodi *self = user_data;
  g_autoptr(GError) error = NULL;
  GSocketConnection *connection = g_socket_client_connect_to_host_finish (G_SOCKET_CLIENT (source), result, &error);

  self->connecting = FALSE;
  if (connection == NULL)
    {
      if (!g_error_matches (error, G_IO_ERROR, G_IO_ERROR_CANCELLED))
        schedule_reconnect (self);
      return;
    }
  g_socket_set_timeout (g_socket_connection_get_socket (connection), 0);
  self->connection = connection;
  g_debug ("kodi: connected to JSON-RPC on port %d", KODI_PORT);
  read_more (self);
  g_signal_emit (self, signals[CONNECTED_CHANGED], 0);
}

static void
connect_now (TandemKodi *self)
{
  self->reconnect_id = 0;
  if (self->connection != NULL || self->connecting)
    return;
  self->connecting = TRUE;
  g_clear_object (&self->cancellable);
  self->cancellable = g_cancellable_new ();
  g_socket_client_connect_to_host_async (self->client, KODI_HOST, KODI_PORT, self->cancellable, connected_cb, self);
}

TandemKodi *
tandem_kodi_get (void)
{
  static TandemKodi *instance;

  if (instance == NULL)
    {
      instance = g_object_new (TANDEM_TYPE_KODI, NULL);
      connect_now (instance);
    }
  return instance;
}

gboolean
tandem_kodi_is_connected (TandemKodi *self)
{
  return self->connection != NULL;
}

static void write_next (TandemKodi *self);

static void
write_done_cb (GObject *source, GAsyncResult *result, gpointer user_data)
{
  TandemKodi *self = user_data;
  g_autoptr(GError) error = NULL;

  g_free (g_queue_pop_head (self->outgoing));
  self->writing = FALSE;
  if (!g_output_stream_write_all_finish (G_OUTPUT_STREAM (source), result, NULL, &error))
    {
      if (!g_error_matches (error, G_IO_ERROR, G_IO_ERROR_CANCELLED))
        drop_connection (self);
      return;
    }
  write_next (self);
}

static void
write_next (TandemKodi *self)
{
  const char *text;

  if (self->writing || self->connection == NULL || g_queue_is_empty (self->outgoing))
    return;
  text = g_queue_peek_head (self->outgoing);
  self->writing = TRUE;
  g_output_stream_write_all_async (g_io_stream_get_output_stream (G_IO_STREAM (self->connection)), text, strlen (text),
                                   G_PRIORITY_DEFAULT, self->cancellable, write_done_cb, self);
}

void
tandem_kodi_call (TandemKodi *self, const char *method, JsonNode *params, TandemKodiReply callback, gpointer user_data)
{
  g_autoptr(JsonBuilder) builder = json_builder_new ();
  g_autoptr(JsonGenerator) generator = json_generator_new ();
  g_autoptr(JsonNode) root = NULL;
  char *text;
  guint id;
  Pending *p;

  if (self->connection == NULL)
    {
      if (params != NULL)
        json_node_unref (params);
      if (callback != NULL)
        callback (NULL, user_data);
      return;
    }
  id = ++self->next_id;
  json_builder_begin_object (builder);
  json_builder_set_member_name (builder, "jsonrpc");
  json_builder_add_string_value (builder, "2.0");
  json_builder_set_member_name (builder, "method");
  json_builder_add_string_value (builder, method);
  if (params != NULL)
    {
      json_builder_set_member_name (builder, "params");
      json_builder_add_value (builder, params);
    }
  json_builder_set_member_name (builder, "id");
  json_builder_add_int_value (builder, id);
  json_builder_end_object (builder);
  root = json_builder_get_root (builder);
  json_generator_set_root (generator, root);
  text = json_generator_to_data (generator, NULL);

  p = g_new0 (Pending, 1);
  p->callback = callback;
  p->user_data = user_data;
  g_hash_table_insert (self->pending, GUINT_TO_POINTER (id), p);
  g_queue_push_tail (self->outgoing, text);
  write_next (self);
}

void
tandem_kodi_open (TandemKodi *self, const char *file_or_url)
{
  JsonObject *params = json_object_new ();
  JsonObject *item = json_object_new ();
  JsonNode *node = json_node_new (JSON_NODE_OBJECT);

  json_object_set_string_member (item, "file", file_or_url);
  json_object_set_object_member (params, "item", item);
  json_node_take_object (node, params);
  g_message ("kodi: playing %s", file_or_url);
  tandem_kodi_call (self, "Player.Open", node, NULL, NULL);
}

static void
tandem_kodi_class_init (TandemKodiClass *klass)
{
  signals[NOTIFICATION] = g_signal_new ("notification", TANDEM_TYPE_KODI, G_SIGNAL_RUN_LAST, 0, NULL, NULL, NULL,
                                        G_TYPE_NONE, 1, G_TYPE_STRING);
  signals[CONNECTED_CHANGED] = g_signal_new ("connected-changed", TANDEM_TYPE_KODI, G_SIGNAL_RUN_LAST, 0, NULL, NULL,
                                             NULL, G_TYPE_NONE, 0);
}

static void
tandem_kodi_init (TandemKodi *self)
{
  self->client = g_socket_client_new ();
  g_socket_client_set_timeout (self->client, 5);
  self->buffer = g_string_new (NULL);
  self->outgoing = g_queue_new ();
  self->pending = g_hash_table_new_full (g_direct_hash, g_direct_equal, NULL, (GDestroyNotify) pending_free);
}
