/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "lan.h"
#include "handshake.h"
#include "util.h"

#include <errno.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <netinet/in.h>
#include <string.h>
#include <sys/socket.h>

#define UDP_BUFFER_SIZE 65536
#define CONNECT_DEBOUNCE_MS 500
#define PERIODIC_SECONDS 30
#define NETWORK_CHANGE_DELAY_SECONDS 2

struct _TandemLan
{
  GObject parent_instance;

  TandemConfig *config;
  GSocket *udp;
  GSource *udp_source;
  GSocketListener *listener;
  GCancellable *cancellable;
  int tcp_port;
  GStrv incoming;
  GStrv outgoing;
  GHashTable *in_flight;
  GHashTable *last_attempt;
  guint periodic_id;
  guint network_change_id;
  GNetworkMonitor *monitor;
  gulong monitor_handler;
};

enum {
  LINK_ESTABLISHED,
  N_SIGNALS
};

static guint signals[N_SIGNALS];

G_DEFINE_FINAL_TYPE (TandemLan, tandem_lan, G_TYPE_OBJECT)

static void accept_next (TandemLan *self);

static void
tandem_lan_dispose (GObject *object)
{
  TandemLan *self = TANDEM_LAN (object);

  tandem_lan_stop (self);
  g_clear_object (&self->config);
  g_clear_pointer (&self->incoming, g_strfreev);
  g_clear_pointer (&self->outgoing, g_strfreev);
  g_clear_pointer (&self->in_flight, g_hash_table_unref);
  g_clear_pointer (&self->last_attempt, g_hash_table_unref);

  G_OBJECT_CLASS (tandem_lan_parent_class)->dispose (object);
}

static void
tandem_lan_class_init (TandemLanClass *klass)
{
  G_OBJECT_CLASS (klass)->dispose = tandem_lan_dispose;

  signals[LINK_ESTABLISHED] = g_signal_new ("link-established", TANDEM_TYPE_LAN, G_SIGNAL_RUN_LAST,
                                            0, NULL, NULL, NULL, G_TYPE_NONE, 1, TANDEM_TYPE_LINK);
}

static void
tandem_lan_init (TandemLan *self)
{
  self->in_flight = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);
  self->last_attempt = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_free);
  self->tcp_port = -1;
}

TandemLan *
tandem_lan_new (TandemConfig *config)
{
  TandemLan *self = g_object_new (TANDEM_TYPE_LAN, NULL);

  self->config = g_object_ref (config);
  return self;
}

void
tandem_lan_set_capabilities (TandemLan *self, const char *const *incoming, const char *const *outgoing)
{
  g_clear_pointer (&self->incoming, g_strfreev);
  g_clear_pointer (&self->outgoing, g_strfreev);
  self->incoming = g_strdupv ((char **) incoming);
  self->outgoing = g_strdupv ((char **) outgoing);
}

TandemPacket *
tandem_lan_build_identity (TandemLan *self)
{
  TandemPacket *packet = tandem_packet_new ("kdeconnect.identity");
  g_autofree char *name = tandem_config_get_device_name (self->config);
  const char *empty[] = { NULL };

  tandem_packet_set_string (packet, "deviceId", tandem_config_get_device_id (self->config));
  tandem_packet_set_string (packet, "deviceName", name);
  tandem_packet_set_string (packet, "deviceType", tandem_config_get_device_type (self->config));
  tandem_packet_set_int (packet, "protocolVersion", TANDEM_PROTOCOL_VERSION);
  tandem_packet_set_strv (packet, "incomingCapabilities",
                          self->incoming != NULL ? (const char *const *) self->incoming : empty);
  tandem_packet_set_strv (packet, "outgoingCapabilities",
                          self->outgoing != NULL ? (const char *const *) self->outgoing : empty);
  return packet;
}

static TandemPacket *
identity_func (gpointer user_data)
{
  return tandem_lan_build_identity (TANDEM_LAN (user_data));
}

static void
send_identity_datagram (TandemLan *self, GSocketAddress *to)
{
  g_autoptr(TandemPacket) identity = tandem_lan_build_identity (self);
  g_autoptr(GError) error = NULL;
  g_autofree char *line = NULL;
  gsize len;

  tandem_packet_set_int (identity, "tcpPort", self->tcp_port);
  line = tandem_packet_to_line (identity, &len);
  if (g_socket_send_to (self->udp, to, line, len, NULL, &error) < 0)
    {
      if (g_error_matches (error, G_IO_ERROR, G_IO_ERROR_MESSAGE_TOO_LARGE))
        {
          const char *empty[] = { NULL };
          g_autofree char *small = NULL;

          g_clear_error (&error);
          tandem_packet_set_strv (identity, "incomingCapabilities", empty);
          tandem_packet_set_strv (identity, "outgoingCapabilities", empty);
          g_free (line);
          small = tandem_packet_to_line (identity, &len);
          g_socket_send_to (self->udp, to, small, len, NULL, &error);
          line = NULL;
        }
      if (error != NULL)
        g_debug ("cannot send identity: %s", error->message);
    }
}

void
tandem_lan_send_identity_to (TandemLan *self, GInetAddress *address)
{
  g_autoptr(GSocketAddress) to = NULL;

  if (self->udp == NULL || address == NULL)
    return;
  to = g_inet_socket_address_new (address, TANDEM_UDP_PORT);
  send_identity_datagram (self, to);
}

void
tandem_lan_broadcast (TandemLan *self)
{
  struct ifaddrs *addrs = NULL;
  g_autoptr(GInetAddress) any = g_inet_address_new_from_string ("255.255.255.255");

  if (self->udp == NULL)
    return;

  g_debug ("broadcasting identity");
  tandem_lan_send_identity_to (self, any);

  if (getifaddrs (&addrs) != 0)
    return;
  for (struct ifaddrs *ifa = addrs; ifa != NULL; ifa = ifa->ifa_next)
    {
      g_autoptr(GInetAddress) address = NULL;

      if (ifa->ifa_broadaddr == NULL || ifa->ifa_broadaddr->sa_family != AF_INET ||
          !(ifa->ifa_flags & IFF_BROADCAST) || (ifa->ifa_flags & IFF_LOOPBACK) || !(ifa->ifa_flags & IFF_UP))
        continue;
      address = g_inet_address_new_from_bytes (
          (const guint8 *) &((struct sockaddr_in *) ifa->ifa_broadaddr)->sin_addr, G_SOCKET_FAMILY_IPV4);
      tandem_lan_send_identity_to (self, address);
    }
  freeifaddrs (addrs);
}

static gboolean
periodic_cb (gpointer user_data)
{
  tandem_lan_broadcast (TANDEM_LAN (user_data));
  return G_SOURCE_CONTINUE;
}

void
tandem_lan_set_periodic (TandemLan *self, gboolean enabled)
{
  if (enabled && self->periodic_id == 0)
    self->periodic_id = g_timeout_add_seconds (PERIODIC_SECONDS, periodic_cb, self);
  else if (!enabled && self->periodic_id != 0)
    g_clear_handle_id (&self->periodic_id, g_source_remove);
}

static gboolean
network_changed_delayed_cb (gpointer user_data)
{
  TandemLan *self = user_data;

  self->network_change_id = 0;
  tandem_lan_broadcast (self);
  return G_SOURCE_REMOVE;
}

static void
network_changed_cb (GNetworkMonitor *monitor, gboolean available, gpointer user_data)
{
  TandemLan *self = user_data;

  if (!available)
    return;
  if (self->network_change_id != 0)
    g_source_remove (self->network_change_id);
  self->network_change_id = g_timeout_add_seconds (NETWORK_CHANGE_DELAY_SECONDS, network_changed_delayed_cb, self);
}

typedef struct
{
  TandemLan *lan;
  char *device_id;
  GInetAddress *sender;
} Attempt;

static void
attempt_free (Attempt *attempt)
{
  g_clear_object (&attempt->sender);
  g_free (attempt->device_id);
  g_free (attempt);
}

static void
connect_done_cb (GObject *source, GAsyncResult *result, gpointer user_data)
{
  Attempt *attempt = user_data;
  TandemLan *self = attempt->lan;
  g_autoptr(GError) error = NULL;
  g_autoptr(TandemLink) link = tandem_handshake_finish (result, &error);

  g_hash_table_remove (self->in_flight, attempt->device_id);
  if (link != NULL)
    g_signal_emit (self, signals[LINK_ESTABLISHED], 0, link);
  else if (g_error_matches (error, G_IO_ERROR, G_IO_ERROR_CONNECTION_REFUSED) ||
           g_error_matches (error, G_IO_ERROR, G_IO_ERROR_TIMED_OUT) ||
           g_error_matches (error, G_IO_ERROR, G_IO_ERROR_HOST_UNREACHABLE))
    tandem_lan_send_identity_to (self, attempt->sender);
  attempt_free (attempt);
}

static void
accept_done_cb (GObject *source, GAsyncResult *result, gpointer user_data)
{
  TandemLan *self = user_data;
  g_autoptr(GError) error = NULL;
  g_autoptr(TandemLink) link = tandem_handshake_finish (result, &error);

  if (link != NULL)
    g_signal_emit (self, signals[LINK_ESTABLISHED], 0, link);
  g_object_unref (self);
}

static void
handle_datagram (TandemLan *self, const char *data, gssize len, GSocketAddress *from)
{
  g_autoptr(GError) error = NULL;
  g_autoptr(TandemPacket) identity = NULL;
  const char *device_id;
  gint64 tcp_port;
  gint64 now;
  gint64 *last;
  GInetAddress *sender;
  g_autoptr(GSocketAddress) target = NULL;
  Attempt *attempt;

  if (!G_IS_INET_SOCKET_ADDRESS (from))
    return;
  identity = tandem_packet_from_line (data, len, &error);
  if (identity == NULL || !tandem_identity_is_valid (identity))
    return;
  device_id = tandem_packet_get_string (identity, "deviceId", "");
  if (g_strcmp0 (device_id, tandem_config_get_device_id (self->config)) == 0)
    return;
  tcp_port = tandem_packet_get_int (identity, "tcpPort", 0);
  if (tcp_port < TANDEM_TCP_PORT_MIN || tcp_port > TANDEM_TCP_PORT_MAX)
    return;
  if (g_hash_table_contains (self->in_flight, device_id))
    return;

  now = tandem_now_ms ();
  last = g_hash_table_lookup (self->last_attempt, device_id);
  if (last != NULL && *last + CONNECT_DEBOUNCE_MS > now)
    return;
  g_hash_table_insert (self->last_attempt, g_strdup (device_id), g_memdup2 (&now, sizeof now));

  sender = g_inet_socket_address_get_address (G_INET_SOCKET_ADDRESS (from));
  target = g_inet_socket_address_new (sender, (guint16) tcp_port);
  if (g_getenv ("G_MESSAGES_DEBUG") != NULL)
    {
      g_autofree char *address = g_inet_address_to_string (sender);

      g_debug ("identity from %s (%s) at %s:%" G_GINT64_FORMAT,
               tandem_packet_get_string (identity, "deviceName", "?"), device_id, address, tcp_port);
    }

  attempt = g_new0 (Attempt, 1);
  attempt->lan = self;
  attempt->device_id = g_strdup (device_id);
  attempt->sender = g_object_ref (sender);
  g_hash_table_add (self->in_flight, g_strdup (device_id));
  tandem_handshake_connect_async (self->config, target, identity, identity_func, self,
                                  self->cancellable, connect_done_cb, attempt);
}

static gboolean
udp_readable_cb (GSocket *socket, GIOCondition condition, gpointer user_data)
{
  TandemLan *self = user_data;
  g_autofree char *buffer = g_malloc (UDP_BUFFER_SIZE + 1);
  g_autoptr(GSocketAddress) from = NULL;
  g_autoptr(GError) error = NULL;
  gssize len;

  len = g_socket_receive_from (socket, &from, buffer, UDP_BUFFER_SIZE, NULL, &error);
  if (len < 0)
    {
      if (!g_error_matches (error, G_IO_ERROR, G_IO_ERROR_WOULD_BLOCK))
        g_debug ("udp receive failed: %s", error->message);
      return G_SOURCE_CONTINUE;
    }
  buffer[len] = '\0';
  handle_datagram (self, buffer, len, from);
  return G_SOURCE_CONTINUE;
}

static void
accepted_cb (GObject *source, GAsyncResult *result, gpointer user_data)
{
  TandemLan *self = user_data;
  g_autoptr(GError) error = NULL;
  g_autoptr(GSocketConnection) connection = NULL;

  connection = g_socket_listener_accept_finish (G_SOCKET_LISTENER (source), result, NULL, &error);
  if (connection == NULL)
    {
      if (!g_error_matches (error, G_IO_ERROR, G_IO_ERROR_CANCELLED))
        g_warning ("accept failed: %s", error->message);
      g_object_unref (self);
      return;
    }
  tandem_handshake_accept_async (self->config, connection, identity_func, self, self->cancellable,
                                 accept_done_cb, g_object_ref (self));
  accept_next (self);
  g_object_unref (self);
}

static void
accept_next (TandemLan *self)
{
  g_socket_listener_accept_async (self->listener, self->cancellable, accepted_cb, g_object_ref (self));
}

gboolean
tandem_lan_start (TandemLan *self, GError **error)
{
  g_autoptr(GInetAddress) any = g_inet_address_new_any (G_SOCKET_FAMILY_IPV4);
  g_autoptr(GSocketAddress) bind_to = g_inet_socket_address_new (any, TANDEM_UDP_PORT);

  if (self->udp != NULL)
    return TRUE;

  self->cancellable = g_cancellable_new ();

  self->udp = g_socket_new (G_SOCKET_FAMILY_IPV4, G_SOCKET_TYPE_DATAGRAM, G_SOCKET_PROTOCOL_UDP, error);
  if (self->udp == NULL)
    return FALSE;
  g_socket_set_option (self->udp, SOL_SOCKET, SO_REUSEADDR, 1, NULL);
  g_socket_set_broadcast (self->udp, TRUE);
  g_socket_set_blocking (self->udp, FALSE);
  if (!g_socket_bind (self->udp, bind_to, TRUE, error))
    {
      g_clear_object (&self->udp);
      return FALSE;
    }
  self->udp_source = g_socket_create_source (self->udp, G_IO_IN, self->cancellable);
  g_source_set_callback (self->udp_source, G_SOURCE_FUNC (udp_readable_cb), self, NULL);
  g_source_attach (self->udp_source, NULL);

  self->listener = g_socket_listener_new ();
  for (int port = TANDEM_TCP_PORT_MIN; port <= TANDEM_TCP_PORT_MAX; port++)
    {
      if (g_socket_listener_add_inet_port (self->listener, port, NULL, NULL))
        {
          self->tcp_port = port;
          break;
        }
    }
  if (self->tcp_port < 0)
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_ADDRESS_IN_USE,
                   "No free TCP port between %d and %d", TANDEM_TCP_PORT_MIN, TANDEM_TCP_PORT_MAX);
      tandem_lan_stop (self);
      return FALSE;
    }
  accept_next (self);

  self->monitor = g_object_ref (g_network_monitor_get_default ());
  self->monitor_handler = g_signal_connect (self->monitor, "network-changed", G_CALLBACK (network_changed_cb), self);

  g_message ("listening on udp %d and tcp %d", TANDEM_UDP_PORT, self->tcp_port);
  tandem_lan_broadcast (self);
  return TRUE;
}

void
tandem_lan_stop (TandemLan *self)
{
  if (self->cancellable != NULL)
    g_cancellable_cancel (self->cancellable);
  g_clear_handle_id (&self->periodic_id, g_source_remove);
  g_clear_handle_id (&self->network_change_id, g_source_remove);
  if (self->monitor != NULL && self->monitor_handler != 0)
    g_signal_handler_disconnect (self->monitor, self->monitor_handler);
  self->monitor_handler = 0;
  g_clear_object (&self->monitor);
  if (self->udp_source != NULL)
    {
      g_source_destroy (self->udp_source);
      g_clear_pointer (&self->udp_source, g_source_unref);
    }
  if (self->listener != NULL)
    g_socket_listener_close (self->listener);
  g_clear_object (&self->listener);
  if (self->udp != NULL)
    g_socket_close (self->udp, NULL);
  g_clear_object (&self->udp);
  g_clear_object (&self->cancellable);
  self->tcp_port = -1;
}

int
tandem_lan_get_tcp_port (TandemLan *self)
{
  return self->tcp_port;
}
