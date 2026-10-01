/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "transfer.h"
#include "handshake.h"

#include <string.h>

#define CHUNK_SIZE (64 * 1024)
#define ACCEPT_TIMEOUT_SECONDS 30

struct _TandemTransfer
{
  GObject parent_instance;

  TandemConfig *config;
  GTlsCertificate *peer_certificate;
  GInetAddress *address;
  guint16 port;
  gint64 size;
  gint64 done;

  GInputStream *source;
  GOutputStream *sink;
  GSocketListener *listener;
  GIOStream *tls;
  GSocketConnection *connection;
  GCancellable *cancellable;
  guint timeout_id;
  GTask *task;
  GByteArray *collected;
  guint8 *buffer;
  gboolean upload;
};

enum {
  PROGRESS,
  FINISHED,
  N_SIGNALS
};

static guint signals[N_SIGNALS];

G_DEFINE_FINAL_TYPE (TandemTransfer, tandem_transfer, G_TYPE_OBJECT)

static void
tandem_transfer_dispose (GObject *object)
{
  TandemTransfer *self = TANDEM_TRANSFER (object);

  g_clear_handle_id (&self->timeout_id, g_source_remove);
  if (self->listener != NULL)
    g_socket_listener_close (self->listener);
  g_clear_object (&self->listener);
  g_clear_object (&self->tls);
  g_clear_object (&self->connection);
  g_clear_object (&self->source);
  g_clear_object (&self->sink);
  g_clear_object (&self->cancellable);
  g_clear_object (&self->config);
  g_clear_object (&self->peer_certificate);
  g_clear_object (&self->address);
  g_clear_pointer (&self->collected, g_byte_array_unref);
  g_clear_pointer (&self->buffer, g_free);

  G_OBJECT_CLASS (tandem_transfer_parent_class)->dispose (object);
}

static void
tandem_transfer_class_init (TandemTransferClass *klass)
{
  G_OBJECT_CLASS (klass)->dispose = tandem_transfer_dispose;

  signals[PROGRESS] = g_signal_new ("progress", TANDEM_TYPE_TRANSFER, G_SIGNAL_RUN_LAST, 0, NULL, NULL, NULL,
                                    G_TYPE_NONE, 2, G_TYPE_INT64, G_TYPE_INT64);
  signals[FINISHED] = g_signal_new ("finished", TANDEM_TYPE_TRANSFER, G_SIGNAL_RUN_LAST, 0, NULL, NULL, NULL,
                                    G_TYPE_NONE, 1, G_TYPE_ERROR);
}

static void
tandem_transfer_init (TandemTransfer *self)
{
  self->cancellable = g_cancellable_new ();
  self->buffer = g_malloc (CHUNK_SIZE);
}

static void
finish (TandemTransfer *self, GError *error)
{
  g_object_ref (self);
  g_clear_handle_id (&self->timeout_id, g_source_remove);
  if (self->listener != NULL)
    g_socket_listener_close (self->listener);
  if (self->tls != NULL)
    g_io_stream_close_async (self->tls, G_PRIORITY_DEFAULT, NULL, NULL, NULL);
  if (self->task != NULL)
    {
      GTask *task = g_steal_pointer (&self->task);

      if (error != NULL)
        g_task_return_error (task, g_error_copy (error));
      else if (self->collected != NULL)
        g_task_return_pointer (task, g_byte_array_free_to_bytes (g_steal_pointer (&self->collected)),
                               (GDestroyNotify) g_bytes_unref);
      else
        g_task_return_boolean (task, TRUE);
      g_object_unref (task);
    }
  g_signal_emit (self, signals[FINISHED], 0, error);
  g_clear_error (&error);
  g_object_unref (self);
}

static gboolean
accept_certificate_cb (GTlsConnection *connection, GTlsCertificate *peer, GTlsCertificateFlags errors, gpointer user_data)
{
  TandemTransfer *self = user_data;

  return self->peer_certificate == NULL || g_tls_certificate_is_same (peer, self->peer_certificate);
}

static void pump (TandemTransfer *self);

static void
write_cb (GObject *source, GAsyncResult *result, gpointer user_data)
{
  TandemTransfer *self = user_data;
  GError *error = NULL;

  if (!g_output_stream_write_all_finish (G_OUTPUT_STREAM (source), result, NULL, &error))
    {
      finish (self, error);
      return;
    }
  g_signal_emit (self, signals[PROGRESS], 0, self->done, self->size);
  if (self->size >= 0 && self->done >= self->size)
    {
      if (self->sink != NULL && !self->upload)
        g_output_stream_close (self->sink, NULL, NULL);
      finish (self, NULL);
      return;
    }
  pump (self);
}

static void
read_cb (GObject *source, GAsyncResult *result, gpointer user_data)
{
  TandemTransfer *self = user_data;
  GError *error = NULL;
  gssize n = g_input_stream_read_finish (G_INPUT_STREAM (source), result, &error);

  if (n < 0)
    {
      finish (self, error);
      return;
    }
  if (n == 0)
    {
      if (self->size >= 0 && self->done < self->size)
        finish (self, g_error_new (G_IO_ERROR, G_IO_ERROR_PARTIAL_INPUT,
                                   "Transfer ended after %" G_GINT64_FORMAT " of %" G_GINT64_FORMAT " bytes",
                                   self->done, self->size));
      else
        {
          if (self->sink != NULL && !self->upload)
            g_output_stream_close (self->sink, NULL, NULL);
          finish (self, NULL);
        }
      return;
    }
  self->done += n;
  if (self->collected != NULL)
    {
      g_byte_array_append (self->collected, self->buffer, n);
      g_signal_emit (self, signals[PROGRESS], 0, self->done, self->size);
      if (self->size >= 0 && self->done >= self->size)
        finish (self, NULL);
      else
        pump (self);
      return;
    }
  g_output_stream_write_all_async (self->sink, self->buffer, n, G_PRIORITY_DEFAULT, self->cancellable,
                                   write_cb, self);
}

static void
pump (TandemTransfer *self)
{
  gsize want = CHUNK_SIZE;

  if (self->size >= 0 && self->size - self->done < (gint64) want)
    want = (gsize) (self->size - self->done);
  if (want == 0)
    {
      if (self->sink != NULL && !self->upload)
        g_output_stream_close (self->sink, NULL, NULL);
      finish (self, NULL);
      return;
    }
  g_input_stream_read_async (self->source, self->buffer, want, G_PRIORITY_DEFAULT, self->cancellable, read_cb, self);
}

static void
download_handshake_cb (GObject *source, GAsyncResult *result, gpointer user_data)
{
  TandemTransfer *self = user_data;
  GError *error = NULL;

  if (!g_tls_connection_handshake_finish (G_TLS_CONNECTION (source), result, &error))
    {
      finish (self, error);
      return;
    }
  self->source = g_object_ref (g_io_stream_get_input_stream (self->tls));
  pump (self);
}

static void
download_connected_cb (GObject *source, GAsyncResult *result, gpointer user_data)
{
  TandemTransfer *self = user_data;
  GError *error = NULL;

  self->connection = g_socket_client_connect_finish (G_SOCKET_CLIENT (source), result, &error);
  if (self->connection == NULL)
    {
      finish (self, error);
      return;
    }
  self->tls = g_tls_client_connection_new (G_IO_STREAM (self->connection), NULL, &error);
  if (self->tls == NULL)
    {
      finish (self, error);
      return;
    }
  g_tls_connection_set_certificate (G_TLS_CONNECTION (self->tls), tandem_config_get_certificate (self->config));
  G_GNUC_BEGIN_IGNORE_DEPRECATIONS
  g_tls_client_connection_set_validation_flags (G_TLS_CLIENT_CONNECTION (self->tls), 0);
  G_GNUC_END_IGNORE_DEPRECATIONS
  g_tls_connection_set_require_close_notify (G_TLS_CONNECTION (self->tls), FALSE);
  g_signal_connect (self->tls, "accept-certificate", G_CALLBACK (accept_certificate_cb), self);
  g_tls_connection_handshake_async (G_TLS_CONNECTION (self->tls), G_PRIORITY_DEFAULT, self->cancellable,
                                    download_handshake_cb, self);
}

static void
start_download (TandemTransfer *self, GTask *task, GCancellable *cancellable)
{
  g_autoptr(GSocketClient) client = g_socket_client_new ();
  g_autoptr(GSocketAddress) address = g_inet_socket_address_new (self->address, self->port);

  self->task = task;
  if (cancellable != NULL)
    g_signal_connect_object (cancellable, "cancelled", G_CALLBACK (g_cancellable_cancel), self->cancellable,
                             G_CONNECT_SWAPPED);
  g_socket_client_set_timeout (client, 30);
  g_socket_client_set_enable_proxy (client, FALSE);
  g_socket_client_connect_async (client, G_SOCKET_CONNECTABLE (address), self->cancellable,
                                 download_connected_cb, self);
}

TandemTransfer *
tandem_transfer_new_download (TandemConfig    *config,
                              GTlsCertificate *peer_certificate,
                              GInetAddress    *address,
                              guint16          port,
                              gint64           size)
{
  TandemTransfer *self = g_object_new (TANDEM_TYPE_TRANSFER, NULL);

  self->config = g_object_ref (config);
  self->peer_certificate = peer_certificate != NULL ? g_object_ref (peer_certificate) : NULL;
  self->address = g_object_ref (address);
  self->port = port;
  self->size = size;
  return self;
}

void
tandem_transfer_download_to_file_async (TandemTransfer      *self,
                                        GFile               *file,
                                        GCancellable        *cancellable,
                                        GAsyncReadyCallback  callback,
                                        gpointer             user_data)
{
  GTask *task = g_task_new (self, cancellable, callback, user_data);
  GError *error = NULL;
  GFileOutputStream *stream = g_file_replace (file, NULL, FALSE, G_FILE_CREATE_REPLACE_DESTINATION, cancellable, &error);

  if (stream == NULL)
    {
      g_task_return_error (task, error);
      g_object_unref (task);
      return;
    }
  self->sink = G_OUTPUT_STREAM (stream);
  start_download (self, task, cancellable);
}

gboolean
tandem_transfer_download_to_file_finish (TandemTransfer *self, GAsyncResult *result, GError **error)
{
  return g_task_propagate_boolean (G_TASK (result), error);
}

void
tandem_transfer_download_to_bytes_async (TandemTransfer      *self,
                                         GCancellable        *cancellable,
                                         GAsyncReadyCallback  callback,
                                         gpointer             user_data)
{
  GTask *task = g_task_new (self, cancellable, callback, user_data);

  self->collected = g_byte_array_sized_new (self->size > 0 && self->size < (16 << 20) ? (guint) self->size : 4096);
  start_download (self, task, cancellable);
}

GBytes *
tandem_transfer_download_to_bytes_finish (TandemTransfer *self, GAsyncResult *result, GError **error)
{
  return g_task_propagate_pointer (G_TASK (result), error);
}

static void
upload_handshake_cb (GObject *source, GAsyncResult *result, gpointer user_data)
{
  TandemTransfer *self = user_data;
  GError *error = NULL;

  if (!g_tls_connection_handshake_finish (G_TLS_CONNECTION (source), result, &error))
    {
      finish (self, error);
      return;
    }
  self->sink = g_object_ref (g_io_stream_get_output_stream (self->tls));
  pump (self);
}

static void
upload_accepted_cb (GObject *source, GAsyncResult *result, gpointer user_data)
{
  TandemTransfer *self = user_data;
  GError *error = NULL;

  g_clear_handle_id (&self->timeout_id, g_source_remove);
  self->connection = g_socket_listener_accept_finish (G_SOCKET_LISTENER (source), result, NULL, &error);
  if (self->connection == NULL)
    {
      finish (self, error);
      return;
    }
  g_socket_listener_close (self->listener);
  tandem_configure_socket (g_socket_connection_get_socket (self->connection));
  self->tls = g_tls_server_connection_new (G_IO_STREAM (self->connection),
                                           tandem_config_get_certificate (self->config), &error);
  if (self->tls == NULL)
    {
      finish (self, error);
      return;
    }
  g_object_set (self->tls, "authentication-mode", G_TLS_AUTHENTICATION_REQUIRED, NULL);
  g_tls_connection_set_require_close_notify (G_TLS_CONNECTION (self->tls), FALSE);
  g_signal_connect (self->tls, "accept-certificate", G_CALLBACK (accept_certificate_cb), self);
  g_tls_connection_handshake_async (G_TLS_CONNECTION (self->tls), G_PRIORITY_DEFAULT, self->cancellable,
                                    upload_handshake_cb, self);
}

static gboolean
upload_timeout_cb (gpointer user_data)
{
  TandemTransfer *self = user_data;

  self->timeout_id = 0;
  finish (self, g_error_new (G_IO_ERROR, G_IO_ERROR_TIMED_OUT, "The device did not fetch the payload"));
  return G_SOURCE_REMOVE;
}

TandemTransfer *
tandem_transfer_new_upload (TandemConfig    *config,
                            GTlsCertificate *peer_certificate,
                            GInputStream    *stream,
                            gint64           size)
{
  TandemTransfer *self = g_object_new (TANDEM_TYPE_TRANSFER, NULL);

  self->config = g_object_ref (config);
  self->peer_certificate = peer_certificate != NULL ? g_object_ref (peer_certificate) : NULL;
  self->source = g_object_ref (stream);
  self->size = size;
  self->upload = TRUE;
  return self;
}

gboolean
tandem_transfer_upload_start (TandemTransfer *self, guint16 *port, GError **error)
{
  self->listener = g_socket_listener_new ();
  for (int p = TANDEM_PAYLOAD_PORT_MIN; p <= TANDEM_PAYLOAD_PORT_MAX; p++)
    {
      if (g_socket_listener_add_inet_port (self->listener, p, NULL, NULL))
        {
          self->port = p;
          break;
        }
    }
  if (self->port == 0)
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_ADDRESS_IN_USE, "No free payload port");
      return FALSE;
    }
  g_socket_listener_accept_async (self->listener, self->cancellable, upload_accepted_cb, self);
  self->timeout_id = g_timeout_add_seconds (ACCEPT_TIMEOUT_SECONDS, upload_timeout_cb, self);
  *port = self->port;
  return TRUE;
}

gint64
tandem_transfer_get_size (TandemTransfer *self)
{
  return self->size;
}

gint64
tandem_transfer_get_done (TandemTransfer *self)
{
  return self->done;
}

void
tandem_transfer_cancel (TandemTransfer *self)
{
  g_cancellable_cancel (self->cancellable);
}
