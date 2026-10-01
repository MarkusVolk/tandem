/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "handshake.h"
#include "cert.h"
#include "util.h"

#include <netinet/in.h>
#include <netinet/tcp.h>
#include <string.h>

#define IDENTITY_MAX_SIZE 8192
#define HANDSHAKE_TIMEOUT_SECONDS 15

typedef struct
{
  TandemConfig *config;
  GSocketConnection *connection;
  GIOStream *tls;
  GDataInputStream *reader;
  TandemPacket *identity;
  TandemIdentityFunc identity_func;
  gpointer identity_data;
  gboolean we_connected;
  gboolean trusted;
  GCancellable *cancellable;
  GCancellable *outer_cancellable;
  gulong cancel_handler;
  guint timeout_id;
} Handshake;

static void
handshake_free (Handshake *hs)
{
  if (hs->timeout_id != 0)
    g_source_remove (hs->timeout_id);
  if (hs->cancel_handler != 0)
    g_cancellable_disconnect (hs->outer_cancellable, hs->cancel_handler);
  g_clear_object (&hs->outer_cancellable);
  g_clear_object (&hs->cancellable);
  g_clear_object (&hs->reader);
  g_clear_object (&hs->tls);
  g_clear_object (&hs->connection);
  g_clear_object (&hs->config);
  g_clear_pointer (&hs->identity, tandem_packet_unref);
  g_free (hs);
}

static void
handshake_fail (GTask *task, GError *error)
{
  Handshake *hs = g_task_get_task_data (task);
  const char *id = hs->identity != NULL ? tandem_packet_get_string (hs->identity, "deviceId", "?") : "?";

  g_debug ("handshake with %s failed: %s", id, error->message);
  if (hs->tls != NULL)
    g_io_stream_close_async (hs->tls, G_PRIORITY_DEFAULT, NULL, NULL, NULL);
  else if (hs->connection != NULL)
    g_io_stream_close_async (G_IO_STREAM (hs->connection), G_PRIORITY_DEFAULT, NULL, NULL, NULL);
  g_task_return_error (task, error);
  g_object_unref (task);
}

static void
outer_cancelled_cb (GCancellable *outer, gpointer inner)
{
  g_cancellable_cancel (G_CANCELLABLE (inner));
}

static gboolean
handshake_timeout (gpointer user_data)
{
  Handshake *hs = user_data;

  hs->timeout_id = 0;
  g_cancellable_cancel (hs->cancellable);
  return G_SOURCE_REMOVE;
}

gboolean
tandem_identity_is_valid (TandemPacket *packet)
{
  const char *name;

  if (!tandem_packet_is_type (packet, "kdeconnect.identity"))
    return FALSE;
  if (!tandem_device_id_is_valid (tandem_packet_get_string (packet, "deviceId", NULL)))
    return FALSE;
  name = tandem_packet_get_string (packet, "deviceName", NULL);
  if (name == NULL)
    return FALSE;
  while (g_ascii_isspace (*name))
    name++;
  if (*name == '\0')
    return FALSE;
  if (tandem_packet_get_int (packet, "protocolVersion", 0) < TANDEM_PROTOCOL_VERSION_MIN)
    return FALSE;
  return TRUE;
}

void
tandem_configure_socket (GSocket *socket)
{
  g_socket_set_keepalive (socket, TRUE);
  g_socket_set_option (socket, IPPROTO_TCP, TCP_KEEPIDLE, 30, NULL);
  g_socket_set_option (socket, IPPROTO_TCP, TCP_KEEPINTVL, 10, NULL);
  g_socket_set_option (socket, IPPROTO_TCP, TCP_KEEPCNT, 3, NULL);
  g_socket_set_option (socket, IPPROTO_TCP, TCP_NODELAY, 1, NULL);
  g_socket_set_timeout (socket, 0);
}

static gboolean
accept_certificate_cb (GTlsConnection *connection, GTlsCertificate *peer, GTlsCertificateFlags errors, gpointer user_data)
{
  return TRUE;
}

static GDataInputStream *
make_reader (GInputStream *base, gsize buffer_size)
{
  GDataInputStream *reader = g_data_input_stream_new (base);

  g_filter_input_stream_set_close_base_stream (G_FILTER_INPUT_STREAM (reader), FALSE);
  g_buffered_input_stream_set_buffer_size (G_BUFFERED_INPUT_STREAM (reader), buffer_size);
  g_data_input_stream_set_newline_type (reader, G_DATA_STREAM_NEWLINE_TYPE_LF);
  return reader;
}

static void
finish_link (GTask *task)
{
  Handshake *hs = g_task_get_task_data (task);
  TandemLink *link = tandem_link_new (hs->tls, hs->connection, hs->reader, hs->identity);

  g_debug ("handshake with %s (%s) done, protocol %" G_GINT64_FORMAT,
           tandem_packet_get_string (hs->identity, "deviceName", "?"),
           tandem_packet_get_string (hs->identity, "deviceId", "?"),
           tandem_packet_get_int (hs->identity, "protocolVersion", 0));
  g_task_return_pointer (task, link, g_object_unref);
  g_object_unref (task);
}

static void
secure_identity_read_cb (GObject *source, GAsyncResult *result, gpointer user_data)
{
  GTask *task = user_data;
  Handshake *hs = g_task_get_task_data (task);
  g_autoptr(GError) error = NULL;
  g_autoptr(TandemPacket) secure = NULL;
  gsize length = 0;
  g_autofree char *line = g_data_input_stream_read_line_finish_utf8 (G_DATA_INPUT_STREAM (source),
                                                                      result, &length, &error);

  if (line == NULL)
    {
      if (error == NULL)
        g_set_error (&error, G_IO_ERROR, G_IO_ERROR_CLOSED, "Connection closed before the secure identity");
      handshake_fail (task, g_steal_pointer (&error));
      return;
    }
  if (length > IDENTITY_MAX_SIZE)
    {
      handshake_fail (task, g_error_new (G_IO_ERROR, G_IO_ERROR_INVALID_DATA, "Secure identity too large"));
      return;
    }
  secure = tandem_packet_from_line (line, length, &error);
  if (secure == NULL)
    {
      handshake_fail (task, g_steal_pointer (&error));
      return;
    }
  if (!tandem_identity_is_valid (secure) ||
      g_strcmp0 (tandem_packet_get_string (secure, "deviceId", NULL),
                 tandem_packet_get_string (hs->identity, "deviceId", NULL)) != 0 ||
      tandem_packet_get_int (secure, "protocolVersion", 0) !=
      tandem_packet_get_int (hs->identity, "protocolVersion", 0))
    {
      handshake_fail (task, g_error_new (G_IO_ERROR, G_IO_ERROR_INVALID_DATA,
                                         "Secure identity does not match the first one"));
      return;
    }

  tandem_packet_unref (hs->identity);
  hs->identity = g_steal_pointer (&secure);
  finish_link (task);
}

static void
secure_identity_written_cb (GObject *source, GAsyncResult *result, gpointer user_data)
{
  GTask *task = user_data;
  Handshake *hs = g_task_get_task_data (task);
  g_autoptr(GError) error = NULL;

  if (!g_output_stream_write_all_finish (G_OUTPUT_STREAM (source), result, NULL, &error))
    {
      handshake_fail (task, g_steal_pointer (&error));
      return;
    }
  g_data_input_stream_read_line_async (hs->reader, G_PRIORITY_DEFAULT, hs->cancellable,
                                       secure_identity_read_cb, task);
}

static void
tls_handshake_cb (GObject *source, GAsyncResult *result, gpointer user_data)
{
  GTask *task = user_data;
  Handshake *hs = g_task_get_task_data (task);
  g_autoptr(GError) error = NULL;
  GTlsCertificate *peer;
  g_autofree char *cn = NULL;
  const char *device_id = tandem_packet_get_string (hs->identity, "deviceId", "");

  if (!g_tls_connection_handshake_finish (G_TLS_CONNECTION (source), result, &error))
    {
      handshake_fail (task, g_steal_pointer (&error));
      return;
    }

  peer = g_tls_connection_get_peer_certificate (G_TLS_CONNECTION (hs->tls));
  if (peer == NULL)
    {
      handshake_fail (task, g_error_new (G_IO_ERROR, G_IO_ERROR_PERMISSION_DENIED,
                                         "The peer sent no certificate"));
      return;
    }
  cn = tandem_cert_common_name (peer);
  if (g_strcmp0 (cn, device_id) != 0)
    {
      handshake_fail (task, g_error_new (G_IO_ERROR, G_IO_ERROR_PERMISSION_DENIED,
                                         "Certificate CN %s does not match device id %s", cn, device_id));
      return;
    }
  if (hs->trusted)
    {
      g_autoptr(GTlsCertificate) pinned = tandem_config_load_trusted (hs->config, device_id, NULL);

      if (pinned == NULL || !g_tls_certificate_is_same (peer, pinned))
        {
          handshake_fail (task, g_error_new (G_IO_ERROR, G_IO_ERROR_PERMISSION_DENIED,
                                             "Certificate of trusted device %s changed", device_id));
          return;
        }
    }

  hs->reader = make_reader (g_io_stream_get_input_stream (hs->tls), IDENTITY_MAX_SIZE);

  if (tandem_packet_get_int (hs->identity, "protocolVersion", 0) >= 8)
    {
      g_autoptr(TandemPacket) mine = hs->identity_func (hs->identity_data);
      gsize len;
      char *line = tandem_packet_to_line (mine, &len);

      g_object_set_data_full (G_OBJECT (task), "identity-line", line, g_free);
      g_output_stream_write_all_async (g_io_stream_get_output_stream (hs->tls), line, len,
                                       G_PRIORITY_DEFAULT, hs->cancellable,
                                       secure_identity_written_cb, task);
    }
  else
    {
      finish_link (task);
    }
}

static void
start_tls (GTask *task)
{
  Handshake *hs = g_task_get_task_data (task);
  g_autoptr(GError) error = NULL;
  GIOStream *tls;

  if (hs->we_connected)
    tls = g_tls_server_connection_new (G_IO_STREAM (hs->connection),
                                       tandem_config_get_certificate (hs->config), &error);
  else
    tls = g_tls_client_connection_new (G_IO_STREAM (hs->connection), NULL, &error);

  if (tls == NULL)
    {
      handshake_fail (task, g_steal_pointer (&error));
      return;
    }

  if (hs->we_connected)
    {
      g_object_set (tls, "authentication-mode", G_TLS_AUTHENTICATION_REQUIRED, NULL);
    }
  else
    {
      g_tls_connection_set_certificate (G_TLS_CONNECTION (tls), tandem_config_get_certificate (hs->config));
      G_GNUC_BEGIN_IGNORE_DEPRECATIONS
      g_tls_client_connection_set_validation_flags (G_TLS_CLIENT_CONNECTION (tls), 0);
      G_GNUC_END_IGNORE_DEPRECATIONS
    }
  g_tls_connection_set_require_close_notify (G_TLS_CONNECTION (tls), FALSE);
  g_signal_connect (tls, "accept-certificate", G_CALLBACK (accept_certificate_cb), NULL);

  hs->tls = tls;
  g_tls_connection_handshake_async (G_TLS_CONNECTION (tls), G_PRIORITY_DEFAULT, hs->cancellable,
                                    tls_handshake_cb, task);
}

static void
plain_identity_written_cb (GObject *source, GAsyncResult *result, gpointer user_data)
{
  GTask *task = user_data;
  g_autoptr(GError) error = NULL;

  if (!g_output_stream_write_all_finish (G_OUTPUT_STREAM (source), result, NULL, &error))
    {
      handshake_fail (task, g_steal_pointer (&error));
      return;
    }
  start_tls (task);
}

static void
connected_cb (GObject *source, GAsyncResult *result, gpointer user_data)
{
  GTask *task = user_data;
  Handshake *hs = g_task_get_task_data (task);
  g_autoptr(GError) error = NULL;
  g_autoptr(TandemPacket) mine = NULL;
  gsize len;
  char *line;

  hs->connection = g_socket_client_connect_finish (G_SOCKET_CLIENT (source), result, &error);
  if (hs->connection == NULL)
    {
      handshake_fail (task, g_steal_pointer (&error));
      return;
    }
  tandem_configure_socket (g_socket_connection_get_socket (hs->connection));

  mine = hs->identity_func (hs->identity_data);
  tandem_packet_set_string (mine, "targetDeviceId", tandem_packet_get_string (hs->identity, "deviceId", ""));
  tandem_packet_set_int (mine, "targetProtocolVersion", tandem_packet_get_int (hs->identity, "protocolVersion", 0));
  line = tandem_packet_to_line (mine, &len);
  g_object_set_data_full (G_OBJECT (task), "identity-line", line, g_free);
  g_output_stream_write_all_async (g_io_stream_get_output_stream (G_IO_STREAM (hs->connection)), line, len,
                                   G_PRIORITY_DEFAULT, hs->cancellable, plain_identity_written_cb, task);
}

static GTask *
handshake_task_new (TandemConfig        *config,
                    TandemIdentityFunc   identity_func,
                    gpointer             identity_data,
                    GCancellable        *cancellable,
                    GAsyncReadyCallback  callback,
                    gpointer             user_data)
{
  Handshake *hs = g_new0 (Handshake, 1);
  GTask *task = g_task_new (NULL, cancellable, callback, user_data);

  hs->config = g_object_ref (config);
  hs->identity_func = identity_func;
  hs->identity_data = identity_data;
  hs->cancellable = g_cancellable_new ();
  if (cancellable != NULL)
    {
      hs->outer_cancellable = g_object_ref (cancellable);
      hs->cancel_handler = g_cancellable_connect (cancellable, G_CALLBACK (outer_cancelled_cb),
                                                  g_object_ref (hs->cancellable), g_object_unref);
    }
  hs->timeout_id = g_timeout_add_seconds (HANDSHAKE_TIMEOUT_SECONDS, handshake_timeout, hs);
  g_task_set_task_data (task, hs, (GDestroyNotify) handshake_free);
  return task;
}

static gboolean
check_trust_and_version (Handshake *hs, GError **error)
{
  const char *device_id = tandem_packet_get_string (hs->identity, "deviceId", "");
  gint64 version = tandem_packet_get_int (hs->identity, "protocolVersion", 0);

  hs->trusted = tandem_config_is_trusted (hs->config, device_id);
  if (hs->trusted)
    {
      g_autoptr(GKeyFile) info = NULL;
      g_autoptr(GTlsCertificate) pinned = tandem_config_load_trusted (hs->config, device_id, &info);
      gint64 known = info != NULL ? g_key_file_get_int64 (info, "device", "protocolVersion", NULL) : 0;

      if (pinned != NULL && known > version)
        {
          g_set_error (error, G_IO_ERROR, G_IO_ERROR_PERMISSION_DENIED,
                       "Device %s announces protocol %" G_GINT64_FORMAT " but used %" G_GINT64_FORMAT " before",
                       device_id, version, known);
          return FALSE;
        }
    }
  return TRUE;
}

void
tandem_handshake_connect_async (TandemConfig        *config,
                                GSocketAddress      *address,
                                TandemPacket        *udp_identity,
                                TandemIdentityFunc   identity_func,
                                gpointer             identity_data,
                                GCancellable        *cancellable,
                                GAsyncReadyCallback  callback,
                                gpointer             user_data)
{
  GTask *task = handshake_task_new (config, identity_func, identity_data, cancellable, callback, user_data);
  Handshake *hs = g_task_get_task_data (task);
  g_autoptr(GSocketClient) client = g_socket_client_new ();
  GError *error = NULL;

  hs->we_connected = TRUE;
  hs->identity = tandem_packet_ref (udp_identity);
  if (!check_trust_and_version (hs, &error))
    {
      handshake_fail (task, error);
      return;
    }
  g_socket_client_set_timeout (client, 5);
  g_socket_client_set_enable_proxy (client, FALSE);
  g_socket_client_connect_async (client, G_SOCKET_CONNECTABLE (address), hs->cancellable, connected_cb, task);
}

static void
plain_identity_read_cb (GObject *source, GAsyncResult *result, gpointer user_data)
{
  GTask *task = user_data;
  Handshake *hs = g_task_get_task_data (task);
  g_autoptr(GError) error = NULL;
  gsize length = 0;
  g_autofree char *line = g_data_input_stream_read_line_finish_utf8 (G_DATA_INPUT_STREAM (source),
                                                                      result, &length, &error);
  const char *target;
  gint64 target_version;

  if (line == NULL)
    {
      if (error == NULL)
        g_set_error (&error, G_IO_ERROR, G_IO_ERROR_CLOSED, "Connection closed before the identity");
      handshake_fail (task, g_steal_pointer (&error));
      return;
    }
  if (length > IDENTITY_MAX_SIZE)
    {
      handshake_fail (task, g_error_new (G_IO_ERROR, G_IO_ERROR_INVALID_DATA, "Identity too large"));
      return;
    }
  hs->identity = tandem_packet_from_line (line, length, &error);
  if (hs->identity == NULL)
    {
      handshake_fail (task, g_steal_pointer (&error));
      return;
    }
  if (!tandem_identity_is_valid (hs->identity))
    {
      handshake_fail (task, g_error_new (G_IO_ERROR, G_IO_ERROR_INVALID_DATA, "Invalid identity"));
      return;
    }
  target = tandem_packet_get_string (hs->identity, "targetDeviceId", NULL);
  if (target != NULL && g_strcmp0 (target, tandem_config_get_device_id (hs->config)) != 0)
    {
      handshake_fail (task, g_error_new (G_IO_ERROR, G_IO_ERROR_INVALID_DATA,
                                         "Connection meant for device %s", target));
      return;
    }
  target_version = tandem_packet_get_int (hs->identity, "targetProtocolVersion", -1);
  if (target_version != -1 && target_version != TANDEM_PROTOCOL_VERSION)
    {
      handshake_fail (task, g_error_new (G_IO_ERROR, G_IO_ERROR_INVALID_DATA,
                                         "Connection meant for protocol %" G_GINT64_FORMAT, target_version));
      return;
    }
  if (g_strcmp0 (tandem_packet_get_string (hs->identity, "deviceId", ""),
                 tandem_config_get_device_id (hs->config)) == 0)
    {
      handshake_fail (task, g_error_new (G_IO_ERROR, G_IO_ERROR_INVALID_DATA, "Connection from myself"));
      return;
    }
  if (!check_trust_and_version (hs, &error))
    {
      handshake_fail (task, g_steal_pointer (&error));
      return;
    }

  g_clear_object (&hs->reader);
  start_tls (task);
}

void
tandem_handshake_accept_async (TandemConfig        *config,
                               GSocketConnection   *connection,
                               TandemIdentityFunc   identity_func,
                               gpointer             identity_data,
                               GCancellable        *cancellable,
                               GAsyncReadyCallback  callback,
                               gpointer             user_data)
{
  GTask *task = handshake_task_new (config, identity_func, identity_data, cancellable, callback, user_data);
  Handshake *hs = g_task_get_task_data (task);

  hs->we_connected = FALSE;
  hs->connection = g_object_ref (connection);
  tandem_configure_socket (g_socket_connection_get_socket (connection));
  hs->reader = make_reader (g_io_stream_get_input_stream (G_IO_STREAM (connection)), IDENTITY_MAX_SIZE);
  g_data_input_stream_read_line_async (hs->reader, G_PRIORITY_DEFAULT, hs->cancellable,
                                       plain_identity_read_cb, task);
}

TandemLink *
tandem_handshake_finish (GAsyncResult *result, GError **error)
{
  return g_task_propagate_pointer (G_TASK (result), error);
}
