/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "device.h"
#include "util.h"

#include <string.h>

struct _TandemDevice
{
  GObject parent_instance;

  TandemConfig *config;
  char *id;
  char *name;
  char *type;
  gint64 protocol_version;
  GStrv incoming;
  GStrv outgoing;
  GTlsCertificate *certificate;
  TandemLink *link;
  GInetAddress *last_address;
  TandemPairState pair_state;
};

enum {
  PACKET,
  CHANGED,
  N_SIGNALS
};

static guint signals[N_SIGNALS];

G_DEFINE_FINAL_TYPE (TandemDevice, tandem_device, G_TYPE_OBJECT)

static void
tandem_device_dispose (GObject *object)
{
  TandemDevice *self = TANDEM_DEVICE (object);

  tandem_device_set_link (self, NULL);
  g_clear_object (&self->config);
  g_clear_object (&self->certificate);
  g_clear_object (&self->last_address);

  G_OBJECT_CLASS (tandem_device_parent_class)->dispose (object);
}

static void
tandem_device_finalize (GObject *object)
{
  TandemDevice *self = TANDEM_DEVICE (object);

  g_free (self->id);
  g_free (self->name);
  g_free (self->type);
  g_strfreev (self->incoming);
  g_strfreev (self->outgoing);

  G_OBJECT_CLASS (tandem_device_parent_class)->finalize (object);
}

static void
tandem_device_class_init (TandemDeviceClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->dispose = tandem_device_dispose;
  object_class->finalize = tandem_device_finalize;

  signals[PACKET] = g_signal_new ("packet", TANDEM_TYPE_DEVICE, G_SIGNAL_RUN_LAST, 0, NULL, NULL, NULL,
                                  G_TYPE_NONE, 1, TANDEM_TYPE_PACKET);
  signals[CHANGED] = g_signal_new ("changed", TANDEM_TYPE_DEVICE, G_SIGNAL_RUN_LAST, 0, NULL, NULL, NULL,
                                   G_TYPE_NONE, 0);
}

static void
tandem_device_init (TandemDevice *self)
{
}

static GStrv
strv_from_array (JsonArray *array)
{
  g_autoptr(GStrvBuilder) builder = g_strv_builder_new ();

  for (guint i = 0; array != NULL && i < json_array_get_length (array); i++)
    {
      JsonNode *node = json_array_get_element (array, i);

      if (JSON_NODE_HOLDS_VALUE (node) && json_node_get_value_type (node) == G_TYPE_STRING)
        g_strv_builder_add (builder, json_node_get_string (node));
    }
  return g_strv_builder_end (builder);
}

static void
apply_identity (TandemDevice *self, TandemPacket *identity)
{
  g_free (self->name);
  g_free (self->type);
  g_strfreev (self->incoming);
  g_strfreev (self->outgoing);
  self->name = g_strdup (tandem_packet_get_string (identity, "deviceName", self->id));
  self->type = g_strdup (tandem_packet_get_string (identity, "deviceType", "desktop"));
  self->protocol_version = tandem_packet_get_int (identity, "protocolVersion", TANDEM_PROTOCOL_VERSION_MIN);
  self->incoming = strv_from_array (tandem_packet_get_array (identity, "incomingCapabilities"));
  self->outgoing = strv_from_array (tandem_packet_get_array (identity, "outgoingCapabilities"));
}

static void
save_trusted_info (TandemDevice *self)
{
  g_autoptr(GKeyFile) info = g_key_file_new ();
  g_autoptr(GError) error = NULL;
  g_autofree char *address = NULL;

  g_key_file_set_string (info, "device", "name", self->name);
  g_key_file_set_string (info, "device", "type", self->type);
  g_key_file_set_int64 (info, "device", "protocolVersion", self->protocol_version);
  g_key_file_set_string_list (info, "device", "incoming", (const char *const *) self->incoming,
                              g_strv_length (self->incoming));
  g_key_file_set_string_list (info, "device", "outgoing", (const char *const *) self->outgoing,
                              g_strv_length (self->outgoing));
  if (self->last_address != NULL)
    {
      address = g_inet_address_to_string (self->last_address);
      g_key_file_set_string (info, "device", "lastAddress", address);
    }
  if (!tandem_config_update_trusted (self->config, self->id, info, &error))
    g_warning ("cannot save %s: %s", self->id, error->message);
}

TandemDevice *
tandem_device_new_trusted (TandemConfig *config, const char *device_id)
{
  g_autoptr(GKeyFile) info = NULL;
  g_autoptr(GTlsCertificate) certificate = tandem_config_load_trusted (config, device_id, &info);
  TandemDevice *self;
  g_autofree char *address = NULL;

  if (certificate == NULL)
    return NULL;

  self = g_object_new (TANDEM_TYPE_DEVICE, NULL);
  self->config = g_object_ref (config);
  self->id = g_strdup (device_id);
  self->certificate = g_steal_pointer (&certificate);
  self->pair_state = TANDEM_PAIR_PAIRED;
  self->name = g_key_file_get_string (info, "device", "name", NULL);
  if (self->name == NULL)
    self->name = g_strdup (device_id);
  self->type = g_key_file_get_string (info, "device", "type", NULL);
  if (self->type == NULL)
    self->type = g_strdup ("phone");
  self->protocol_version = g_key_file_get_int64 (info, "device", "protocolVersion", NULL);
  self->incoming = g_key_file_get_string_list (info, "device", "incoming", NULL, NULL);
  self->outgoing = g_key_file_get_string_list (info, "device", "outgoing", NULL, NULL);
  if (self->incoming == NULL)
    self->incoming = g_new0 (char *, 1);
  if (self->outgoing == NULL)
    self->outgoing = g_new0 (char *, 1);
  address = g_key_file_get_string (info, "device", "lastAddress", NULL);
  if (address != NULL)
    self->last_address = g_inet_address_new_from_string (address);
  return self;
}

TandemDevice *
tandem_device_new_from_link (TandemConfig *config, TandemLink *link)
{
  TandemDevice *self = g_object_new (TANDEM_TYPE_DEVICE, NULL);

  self->config = g_object_ref (config);
  self->id = g_strdup (tandem_link_get_device_id (link));
  self->pair_state = TANDEM_PAIR_NONE;
  apply_identity (self, tandem_link_get_identity (link));
  tandem_device_set_link (self, link);
  return self;
}

const char *
tandem_device_get_id (TandemDevice *self)
{
  return self->id;
}

const char *
tandem_device_get_name (TandemDevice *self)
{
  return self->name;
}

const char *
tandem_device_get_type_name (TandemDevice *self)
{
  return self->type;
}

gint64
tandem_device_get_protocol_version (TandemDevice *self)
{
  return self->protocol_version;
}

gboolean
tandem_device_is_reachable (TandemDevice *self)
{
  return self->link != NULL && tandem_link_is_open (self->link);
}

gboolean
tandem_device_is_paired (TandemDevice *self)
{
  return self->pair_state == TANDEM_PAIR_PAIRED;
}

TandemPairState
tandem_device_get_pair_state (TandemDevice *self)
{
  return self->pair_state;
}

const char *
tandem_device_get_state_name (TandemDevice *self)
{
  gboolean reachable = tandem_device_is_reachable (self);

  switch (self->pair_state)
    {
    case TANDEM_PAIR_PAIRED:
      return reachable ? "paired-reachable" : "paired";
    case TANDEM_PAIR_REQUESTED:
      return "requested";
    case TANDEM_PAIR_REQUESTED_BY_PEER:
      return "requested-by-peer";
    default:
      return reachable ? "reachable" : "unreachable";
    }
}

GTlsCertificate *
tandem_device_get_certificate (TandemDevice *self)
{
  if (self->certificate != NULL)
    return self->certificate;
  if (self->link != NULL)
    return tandem_link_get_peer_certificate (self->link);
  return NULL;
}

TandemLink *
tandem_device_get_link (TandemDevice *self)
{
  return self->link;
}

GInetAddress *
tandem_device_get_last_address (TandemDevice *self)
{
  return self->last_address;
}

const char *const *
tandem_device_get_incoming (TandemDevice *self)
{
  return (const char *const *) self->incoming;
}

const char *const *
tandem_device_get_outgoing (TandemDevice *self)
{
  return (const char *const *) self->outgoing;
}

gboolean
tandem_device_has_incoming (TandemDevice *self, const char *type)
{
  return self->incoming != NULL && g_strv_contains ((const char *const *) self->incoming, type);
}

gboolean
tandem_device_has_outgoing (TandemDevice *self, const char *type)
{
  return self->outgoing != NULL && g_strv_contains ((const char *const *) self->outgoing, type);
}

static void
link_packet_cb (TandemLink *link, TandemPacket *packet, gpointer user_data)
{
  TandemDevice *self = user_data;

  g_signal_emit (self, signals[PACKET], 0, packet);
}

static void
link_closed_cb (TandemLink *link, gpointer user_data)
{
  TandemDevice *self = user_data;

  if (self->link != link)
    return;
  g_debug ("device %s (%s) unreachable", self->name, self->id);
  g_signal_handlers_disconnect_by_data (link, self);
  g_clear_object (&self->link);
  g_signal_emit (self, signals[CHANGED], 0);
}

void
tandem_device_set_link (TandemDevice *self, TandemLink *link)
{
  if (self->link == link)
    return;
  if (self->link != NULL)
    {
      TandemLink *old = g_steal_pointer (&self->link);

      g_signal_handlers_disconnect_by_data (old, self);
      tandem_link_close (old);
      g_object_unref (old);
    }
  if (link != NULL)
    {
      g_autoptr(GInetAddress) address = tandem_link_get_remote_address (link);

      self->link = g_object_ref (link);
      apply_identity (self, tandem_link_get_identity (link));
      if (address != NULL)
        g_set_object (&self->last_address, address);
      g_signal_connect (link, "packet", G_CALLBACK (link_packet_cb), self);
      g_signal_connect (link, "closed", G_CALLBACK (link_closed_cb), self);
      if (self->pair_state == TANDEM_PAIR_PAIRED)
        save_trusted_info (self);
      g_debug ("device %s (%s) reachable", self->name, self->id);
    }
  g_signal_emit (self, signals[CHANGED], 0);
}

void
tandem_device_send_packet (TandemDevice *self, TandemPacket *packet)
{
  if (self->link == NULL)
    {
      g_debug ("device %s unreachable, dropping %s", self->id, packet->type);
      return;
    }
  tandem_link_send (self->link, packet);
}
