/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "link.h"
#include "util.h"

struct _TandemLink
{
  GObject parent_instance;

  GIOStream *stream;
  GSocketConnection *connection;
  GDataInputStream *reader;
  GOutputStream *writer;
  GCancellable *cancellable;
  GQueue *outgoing;
  gboolean writing;
  gboolean closed;
  TandemPacket *identity;
  char *device_id;
};

enum {
  PACKET,
  CLOSED,
  N_SIGNALS
};

static guint signals[N_SIGNALS];

G_DEFINE_FINAL_TYPE (TandemLink, tandem_link, G_TYPE_OBJECT)

static void read_next_line (TandemLink *self);
static void write_next (TandemLink *self);

static void
tandem_link_dispose (GObject *object)
{
  TandemLink *self = TANDEM_LINK (object);

  tandem_link_close (self);
  g_clear_object (&self->cancellable);
  g_clear_object (&self->reader);
  g_clear_object (&self->stream);
  g_clear_object (&self->connection);
  g_clear_pointer (&self->identity, tandem_packet_unref);
  if (self->outgoing != NULL)
    {
      g_queue_free_full (self->outgoing, (GDestroyNotify) g_bytes_unref);
      self->outgoing = NULL;
    }

  G_OBJECT_CLASS (tandem_link_parent_class)->dispose (object);
}

static void
tandem_link_finalize (GObject *object)
{
  TandemLink *self = TANDEM_LINK (object);

  g_free (self->device_id);
  G_OBJECT_CLASS (tandem_link_parent_class)->finalize (object);
}

static void
tandem_link_class_init (TandemLinkClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->dispose = tandem_link_dispose;
  object_class->finalize = tandem_link_finalize;

  signals[PACKET] = g_signal_new ("packet", TANDEM_TYPE_LINK, G_SIGNAL_RUN_LAST, 0, NULL, NULL, NULL,
                                  G_TYPE_NONE, 1, TANDEM_TYPE_PACKET);
  signals[CLOSED] = g_signal_new ("closed", TANDEM_TYPE_LINK, G_SIGNAL_RUN_LAST, 0, NULL, NULL, NULL,
                                  G_TYPE_NONE, 0);
}

static void
tandem_link_init (TandemLink *self)
{
  self->outgoing = g_queue_new ();
  self->cancellable = g_cancellable_new ();
}

static void
link_fail (TandemLink *self, const char *why)
{
  if (self->closed)
    return;
  self->closed = TRUE;
  g_debug ("link %s closed: %s", self->device_id, why);
  g_cancellable_cancel (self->cancellable);
  if (self->stream != NULL)
    g_io_stream_close_async (self->stream, G_PRIORITY_DEFAULT, NULL, NULL, NULL);
  g_signal_emit (self, signals[CLOSED], 0);
}

static void
line_read_cb (GObject *source, GAsyncResult *result, gpointer user_data)
{
  g_autoptr(TandemLink) self = user_data;
  g_autoptr(GError) error = NULL;
  gsize length = 0;
  g_autofree char *line = tandem_read_line_finish (G_BUFFERED_INPUT_STREAM (source), result, &length, &error);

  if (self->closed)
    return;
  if (line == NULL)
    {
      link_fail (self, error != NULL ? error->message : "end of stream");
      return;
    }
  if (length > 0)
    {
      g_autoptr(GError) parse_error = NULL;
      g_autoptr(TandemPacket) packet = tandem_packet_from_line (line, length, &parse_error);

      if (packet == NULL)
        g_debug ("link %s: dropping unparsable packet: %s", self->device_id, parse_error->message);
      else
        g_signal_emit (self, signals[PACKET], 0, packet);
    }
  read_next_line (self);
}

static void
read_next_line (TandemLink *self)
{
  if (self->closed)
    return;
  tandem_read_line_async (G_BUFFERED_INPUT_STREAM (self->reader), TANDEM_MAX_PACKET_SIZE, self->cancellable,
                                       line_read_cb, g_object_ref (self));
}

static void
write_done_cb (GObject *source, GAsyncResult *result, gpointer user_data)
{
  g_autoptr(TandemLink) self = user_data;
  g_autoptr(GError) error = NULL;

  self->writing = FALSE;
  if (!g_output_stream_write_all_finish (G_OUTPUT_STREAM (source), result, NULL, &error))
    {
      link_fail (self, error->message);
      return;
    }
  write_next (self);
}

static void
write_next (TandemLink *self)
{
  GBytes *bytes;

  if (self->closed || self->writing)
    return;
  bytes = g_queue_pop_head (self->outgoing);
  if (bytes == NULL)
    return;
  self->writing = TRUE;
  g_output_stream_write_all_async (self->writer, g_bytes_get_data (bytes, NULL), g_bytes_get_size (bytes),
                                   G_PRIORITY_DEFAULT, self->cancellable, write_done_cb, g_object_ref (self));
  g_object_set_data_full (G_OBJECT (self), "tandem-write-buffer", bytes, (GDestroyNotify) g_bytes_unref);
}

TandemLink *
tandem_link_new (GIOStream         *tls_stream,
                 GSocketConnection *connection,
                 GDataInputStream  *reader,
                 TandemPacket      *identity)
{
  TandemLink *self = g_object_new (TANDEM_TYPE_LINK, NULL);

  self->stream = g_object_ref (tls_stream);
  self->connection = g_object_ref (connection);
  self->reader = g_object_ref (reader);
  self->writer = g_io_stream_get_output_stream (tls_stream);
  self->identity = tandem_packet_ref (identity);
  self->device_id = g_strdup (tandem_packet_get_string (identity, "deviceId", ""));

  g_buffered_input_stream_set_buffer_size (G_BUFFERED_INPUT_STREAM (reader), TANDEM_MAX_PACKET_SIZE);
  g_data_input_stream_set_newline_type (reader, G_DATA_STREAM_NEWLINE_TYPE_LF);
  read_next_line (self);
  return self;
}

const char *
tandem_link_get_device_id (TandemLink *self)
{
  return self->device_id;
}

TandemPacket *
tandem_link_get_identity (TandemLink *self)
{
  return self->identity;
}

GTlsCertificate *
tandem_link_get_peer_certificate (TandemLink *self)
{
  if (!G_IS_TLS_CONNECTION (self->stream))
    return NULL;
  return g_tls_connection_get_peer_certificate (G_TLS_CONNECTION (self->stream));
}

GInetAddress *
tandem_link_get_remote_address (TandemLink *self)
{
  g_autoptr(GSocketAddress) address = g_socket_connection_get_remote_address (self->connection, NULL);

  if (address == NULL || !G_IS_INET_SOCKET_ADDRESS (address))
    return NULL;
  return g_object_ref (g_inet_socket_address_get_address (G_INET_SOCKET_ADDRESS (address)));
}

gboolean
tandem_link_is_open (TandemLink *self)
{
  return !self->closed;
}

void
tandem_link_send (TandemLink *self, TandemPacket *packet)
{
  gsize len;
  char *line;

  if (self->closed)
    return;
  line = tandem_packet_to_line (packet, &len);
  g_queue_push_tail (self->outgoing, g_bytes_new_take (line, len));
  write_next (self);
}

void
tandem_link_close (TandemLink *self)
{
  link_fail (self, "closed locally");
}
