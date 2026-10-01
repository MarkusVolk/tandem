/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "smoke.h"
#include "cert.h"
#include "config.h"
#include "handshake.h"
#include "link.h"
#include "packet.h"
#include "util.h"

#include <glib/gstdio.h>
#include <stdio.h>
#include <string.h>

static int failures;

static void
remove_tree (const char *path)
{
  g_autoptr(GFile) file = g_file_new_for_path (path);
  g_autoptr(GFileEnumerator) children = g_file_enumerate_children (file, "standard::name,standard::type", 0, NULL, NULL);
  GFileInfo *info;

  while (children != NULL && (info = g_file_enumerator_next_file (children, NULL, NULL)) != NULL)
    {
      g_autofree char *child = g_build_filename (path, g_file_info_get_name (info), NULL);

      if (g_file_info_get_file_type (info) == G_FILE_TYPE_DIRECTORY)
        remove_tree (child);
      else
        g_unlink (child);
      g_object_unref (info);
    }
  g_rmdir (path);
}

static void
report (gboolean ok, const char *check, const char *detail)
{
  if (!ok)
    failures++;
  if (detail != NULL)
    printf ("%s %s (%s)\n", ok ? "PASS" : "FAIL", check, detail);
  else
    printf ("%s %s\n", ok ? "PASS" : "FAIL", check);
}

static void
check_config (const char *dir)
{
  g_autoptr(GError) error = NULL;
  g_autoptr(TandemConfig) config = tandem_config_new (dir, &error);
  g_autoptr(TandemConfig) again = NULL;
  g_autofree char *cn = NULL;
  g_autofree char *algorithm = NULL;
  unsigned int bits = 0;
  gint64 not_before = 0;
  gint64 not_after = 0;
  gint64 now = g_get_real_time () / G_USEC_PER_SEC;

  report (config != NULL, "config: create", error != NULL ? error->message : dir);
  if (config == NULL)
    return;

  report (tandem_device_id_is_valid (tandem_config_get_device_id (config)), "config: device id",
          tandem_config_get_device_id (config));

  cn = tandem_cert_common_name (tandem_config_get_certificate (config));
  report (g_strcmp0 (cn, tandem_config_get_device_id (config)) == 0, "cert: common name is the device id", cn);

  tandem_cert_describe_key (tandem_config_get_certificate (config), &algorithm, &bits);
  report (algorithm != NULL && g_str_has_prefix (algorithm, "EC") && bits == 256, "cert: EC P-256 key", algorithm);

  tandem_cert_validity (tandem_config_get_certificate (config), &not_before, &not_after);
  report (not_before < now - 360 * 86400 && not_before > now - 370 * 86400 &&
          not_after > now + 9 * 365 * 86400,
          "cert: validity from a year ago for ten years", NULL);

  again = tandem_config_new (dir, &error);
  report (again != NULL &&
          g_strcmp0 (tandem_config_get_device_id (again), tandem_config_get_device_id (config)) == 0 &&
          g_tls_certificate_is_same (tandem_config_get_certificate (again),
                                     tandem_config_get_certificate (config)),
          "config: reload keeps id and certificate", NULL);
}

static void
check_packets (void)
{
  g_autoptr(TandemPacket) identity = tandem_packet_new ("kdeconnect.identity");
  g_autoptr(TandemPacket) parsed = NULL;
  g_autoptr(TandemPacket) payload = NULL;
  g_autoptr(GError) error = NULL;
  g_autofree char *line = NULL;
  const char *caps[] = { "kdeconnect.ping", "kdeconnect.battery", NULL };
  gsize len = 0;

  tandem_packet_set_string (identity, "deviceId", "smoke_test_device_id_0123456789abcd");
  tandem_packet_set_string (identity, "deviceName", "smoke");
  tandem_packet_set_int (identity, "protocolVersion", TANDEM_PROTOCOL_VERSION);
  tandem_packet_set_int (identity, "tcpPort", 1716);
  tandem_packet_set_strv (identity, "incomingCapabilities", caps);
  tandem_packet_set_boolean (identity, "flag", TRUE);

  line = tandem_packet_to_line (identity, &len);
  report (line != NULL && len == strlen (line) && line[len - 1] == '\n' && strchr (line, '\n') == line + len - 1,
          "packet: one line with newline", NULL);

  parsed = tandem_packet_from_line (line, len - 1, &error);
  report (parsed != NULL, "packet: parse own output", error != NULL ? error->message : NULL);
  if (parsed != NULL)
    {
      JsonArray *array = tandem_packet_get_array (parsed, "incomingCapabilities");

      report (tandem_packet_is_type (parsed, "kdeconnect.identity") &&
              parsed->id == identity->id &&
              g_strcmp0 (tandem_packet_get_string (parsed, "deviceName", NULL), "smoke") == 0 &&
              tandem_packet_get_int (parsed, "protocolVersion", 0) == TANDEM_PROTOCOL_VERSION &&
              tandem_packet_get_boolean (parsed, "flag", FALSE) &&
              array != NULL && json_array_get_length (array) == 2 &&
              !tandem_packet_has_payload (parsed),
              "packet: fields survive the round trip", NULL);
    }

  payload = tandem_packet_from_line ("{\"id\":1,\"type\":\"kdeconnect.share.request\","
                                     "\"body\":{\"filename\":\"a.jpg\"},\"payloadSize\":1234,"
                                     "\"payloadTransferInfo\":{\"port\":1740}}", -1, &error);
  report (payload != NULL && tandem_packet_has_payload (payload) &&
          payload->payload_size == 1234 && tandem_packet_get_payload_port (payload) == 1740,
          "packet: payload fields", error != NULL ? error->message : NULL);

  g_clear_error (&error);
  report (tandem_packet_from_line ("{\"type\":\"x\"}", -1, &error) == NULL && error != NULL,
          "packet: reject a packet without body", NULL);
}

static void
check_verification_key (void)
{
  g_autoptr(GBytes) a = g_bytes_new_static ("BBBB", 4);
  g_autoptr(GBytes) b = g_bytes_new_static ("AAAAA", 5);
  g_autofree char *with_ts = tandem_cert_verification_key_from_der (a, b, 1700000000);
  g_autofree char *swapped = tandem_cert_verification_key_from_der (b, a, 1700000000);
  g_autofree char *without = tandem_cert_verification_key_from_der (a, b, 0);

  report (g_strcmp0 (with_ts, "0726AE1E") == 0, "verification key: v8 vector", with_ts);
  report (g_strcmp0 (swapped, with_ts) == 0, "verification key: order independent", swapped);
  report (g_strcmp0 (without, "822F03CD") == 0, "verification key: v7 vector", without);
}

static void
check_util (const char *dir)
{
  g_autofree char *first = NULL;
  g_autofree char *second = NULL;
  g_autofree char *path = NULL;
  g_autofree char *generated = tandem_device_id_generate ();

  report (tandem_device_id_is_valid ("_a1b2c3d4e5f6a7b8c9d0e1f2a3b4c5d6") &&
          tandem_device_id_is_valid (generated) &&
          !tandem_device_id_is_valid ("short") &&
          !tandem_device_id_is_valid ("a1b2c3d4e5f6a7b8c9d0e1f2a3b4c5d6!!") &&
          !tandem_device_id_is_valid (NULL),
          "util: device id validation", generated);

  path = tandem_object_path_for_id ("a-b_c");
  report (g_variant_is_object_path (path), "util: object path", path);

  first = tandem_unique_filename (dir, "photo.jpg");
  g_file_set_contents (first, "x", 1, NULL);
  second = tandem_unique_filename (dir, "photo.jpg");
  report (g_str_has_suffix (first, "/photo.jpg") && g_str_has_suffix (second, "/photo (1).jpg"),
          "util: unique file names", second);
}

typedef struct
{
  TandemConfig *config_a;
  TandemConfig *config_b;
  TandemLink *link_a;
  TandemLink *link_b;
  gboolean ping_received;
  char *error;
  GMainLoop *loop;
  guint16 port;
} Loopback;

static TandemPacket *
loopback_identity (TandemConfig *config)
{
  TandemPacket *packet = tandem_packet_new ("kdeconnect.identity");
  const char *caps[] = { "kdeconnect.ping", NULL };

  tandem_packet_set_string (packet, "deviceId", tandem_config_get_device_id (config));
  tandem_packet_set_string (packet, "deviceName", "smoke");
  tandem_packet_set_string (packet, "deviceType", "desktop");
  tandem_packet_set_int (packet, "protocolVersion", TANDEM_PROTOCOL_VERSION);
  tandem_packet_set_strv (packet, "incomingCapabilities", caps);
  tandem_packet_set_strv (packet, "outgoingCapabilities", caps);
  return packet;
}

static TandemPacket *
identity_a (gpointer user_data)
{
  return loopback_identity (((Loopback *) user_data)->config_a);
}

static TandemPacket *
identity_b (gpointer user_data)
{
  return loopback_identity (((Loopback *) user_data)->config_b);
}

static void
loopback_finish (Loopback *lb, const char *error)
{
  if (lb->error == NULL && error != NULL)
    lb->error = g_strdup (error);
  if (g_main_loop_is_running (lb->loop))
    g_main_loop_quit (lb->loop);
}

static void
loopback_packet_cb (TandemLink *link, TandemPacket *packet, gpointer user_data)
{
  Loopback *lb = user_data;

  if (tandem_packet_is_type (packet, "kdeconnect.ping"))
    {
      lb->ping_received = TRUE;
      loopback_finish (lb, NULL);
    }
}

static void
loopback_maybe_ping (Loopback *lb)
{
  g_autoptr(TandemPacket) ping = NULL;

  if (lb->link_a == NULL || lb->link_b == NULL)
    return;
  g_signal_connect (lb->link_b, "packet", G_CALLBACK (loopback_packet_cb), lb);
  ping = tandem_packet_new ("kdeconnect.ping");
  tandem_packet_set_string (ping, "message", "smoke");
  tandem_link_send (lb->link_a, ping);
}

static void
loopback_connect_cb (GObject *source, GAsyncResult *result, gpointer user_data)
{
  Loopback *lb = user_data;
  g_autoptr(GError) error = NULL;

  lb->link_a = tandem_handshake_finish (result, &error);
  if (lb->link_a == NULL)
    loopback_finish (lb, error->message);
  else
    loopback_maybe_ping (lb);
}

static void
loopback_accept_handshake_cb (GObject *source, GAsyncResult *result, gpointer user_data)
{
  Loopback *lb = user_data;
  g_autoptr(GError) error = NULL;

  lb->link_b = tandem_handshake_finish (result, &error);
  if (lb->link_b == NULL)
    loopback_finish (lb, error->message);
  else
    loopback_maybe_ping (lb);
}

static void
loopback_accepted_cb (GObject *source, GAsyncResult *result, gpointer user_data)
{
  Loopback *lb = user_data;
  g_autoptr(GError) error = NULL;
  g_autoptr(GSocketConnection) connection = NULL;

  connection = g_socket_listener_accept_finish (G_SOCKET_LISTENER (source), result, NULL, &error);
  if (connection == NULL)
    {
      loopback_finish (lb, error->message);
      return;
    }
  tandem_handshake_accept_async (lb->config_b, connection, identity_b, lb, NULL,
                                 loopback_accept_handshake_cb, lb);
}

static gboolean
loopback_timeout_cb (gpointer user_data)
{
  loopback_finish (user_data, "timeout");
  return G_SOURCE_REMOVE;
}

static void
check_loopback (const char *dir)
{
  Loopback lb = { 0 };
  g_autoptr(GError) error = NULL;
  g_autofree char *dir_b = g_build_filename (dir, "peer", NULL);
  g_autoptr(GSocketListener) listener = g_socket_listener_new ();
  g_autoptr(GInetAddress) loopback = g_inet_address_new_loopback (G_SOCKET_FAMILY_IPV4);
  g_autoptr(GSocketAddress) bind_to = g_inet_socket_address_new (loopback, 0);
  g_autoptr(GSocketAddress) bound = NULL;
  g_autoptr(GSocketAddress) target = NULL;
  g_autoptr(TandemPacket) peer_identity = NULL;
  guint timeout;

  lb.config_a = tandem_config_new (dir, &error);
  lb.config_b = lb.config_a != NULL ? tandem_config_new (dir_b, &error) : NULL;
  if (lb.config_b == NULL)
    {
      report (FALSE, "loopback: second identity", error->message);
      g_clear_object (&lb.config_a);
      return;
    }

  if (!g_socket_listener_add_address (listener, bind_to, G_SOCKET_TYPE_STREAM, G_SOCKET_PROTOCOL_TCP,
                                      NULL, &bound, &error))
    {
      report (FALSE, "loopback: listener", error->message);
      goto out;
    }
  lb.port = g_inet_socket_address_get_port (G_INET_SOCKET_ADDRESS (bound));
  lb.loop = g_main_loop_new (NULL, FALSE);
  g_socket_listener_accept_async (listener, NULL, loopback_accepted_cb, &lb);

  peer_identity = loopback_identity (lb.config_b);
  tandem_packet_set_int (peer_identity, "tcpPort", lb.port);
  target = g_inet_socket_address_new (loopback, lb.port);
  tandem_handshake_connect_async (lb.config_a, target, peer_identity, identity_a, &lb, NULL,
                                  loopback_connect_cb, &lb);

  timeout = g_timeout_add_seconds (10, loopback_timeout_cb, &lb);
  g_main_loop_run (lb.loop);
  g_source_remove (timeout);

  report (lb.link_a != NULL && lb.link_b != NULL, "loopback: handshake in both roles", lb.error);
  if (lb.link_a != NULL && lb.link_b != NULL)
    {
      g_autoptr(GTlsCertificate) cert_b = g_object_ref (tandem_config_get_certificate (lb.config_b));

      report (g_strcmp0 (tandem_link_get_device_id (lb.link_a), tandem_config_get_device_id (lb.config_b)) == 0 &&
              g_strcmp0 (tandem_link_get_device_id (lb.link_b), tandem_config_get_device_id (lb.config_a)) == 0,
              "loopback: identities exchanged", NULL);
      report (g_tls_certificate_is_same (tandem_link_get_peer_certificate (lb.link_a), cert_b),
              "loopback: peer certificate seen", NULL);
      report (lb.ping_received, "loopback: packet delivered", NULL);
    }

out:
  g_socket_listener_close (listener);
  if (lb.link_a != NULL)
    tandem_link_close (lb.link_a);
  if (lb.link_b != NULL)
    tandem_link_close (lb.link_b);
  g_clear_object (&lb.link_a);
  g_clear_object (&lb.link_b);
  g_clear_pointer (&lb.loop, g_main_loop_unref);
  g_clear_object (&lb.config_a);
  g_clear_object (&lb.config_b);
  g_free (lb.error);
}

int
tandem_smoke_run (void)
{
  g_autoptr(GError) error = NULL;
  g_autofree char *dir = g_dir_make_tmp ("tandem-smoke-XXXXXX", &error);

  if (dir == NULL)
    {
      fprintf (stderr, "cannot create a temporary directory: %s\n", error->message);
      return 1;
    }

  check_config (dir);
  check_packets ();
  check_verification_key ();
  check_util (dir);
  check_loopback (dir);

  remove_tree (dir);

  printf ("%d failure(s)\n", failures);
  return failures;
}
