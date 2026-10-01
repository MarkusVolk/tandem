/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "device.h"
#include "cert.h"
#include "util.h"

#include <stdlib.h>
#include <string.h>

#define PAIR_TIMEOUT_SECONDS 30
#define PAIR_CLOCK_TOLERANCE_SECONDS 1800

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
  GSettings *settings;
  TandemPairState pair_state;
  gint64 pair_timestamp;
  guint pair_timeout_id;
  gboolean rejected_unpaired_packet;
  GPtrArray *plugins;
};

enum {
  PACKET,
  CHANGED,
  PAIR_REQUEST,
  PAIR_FAILED,
  PLUGINS_CHANGED,
  N_SIGNALS
};

static guint signals[N_SIGNALS];

G_DEFINE_FINAL_TYPE (TandemDevice, tandem_device, G_TYPE_OBJECT)

static void load_plugins (TandemDevice *self);
static void unload_plugins (TandemDevice *self);

static void
tandem_device_dispose (GObject *object)
{
  TandemDevice *self = TANDEM_DEVICE (object);

  g_clear_handle_id (&self->pair_timeout_id, g_source_remove);
  unload_plugins (self);
  tandem_device_set_link (self, NULL);
  g_clear_object (&self->settings);
  g_clear_object (&self->config);
  g_clear_object (&self->certificate);
  g_clear_object (&self->last_address);

  G_OBJECT_CLASS (tandem_device_parent_class)->dispose (object);
}

static void
tandem_device_finalize (GObject *object)
{
  TandemDevice *self = TANDEM_DEVICE (object);

  g_clear_pointer (&self->plugins, g_ptr_array_unref);
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
  signals[PAIR_REQUEST] = g_signal_new ("pair-request", TANDEM_TYPE_DEVICE, G_SIGNAL_RUN_LAST, 0, NULL, NULL, NULL,
                                        G_TYPE_NONE, 0);
  signals[PAIR_FAILED] = g_signal_new ("pair-failed", TANDEM_TYPE_DEVICE, G_SIGNAL_RUN_LAST, 0, NULL, NULL, NULL,
                                       G_TYPE_NONE, 1, G_TYPE_STRING);
  signals[PLUGINS_CHANGED] = g_signal_new ("plugins-changed", TANDEM_TYPE_DEVICE, G_SIGNAL_RUN_LAST, 0,
                                           NULL, NULL, NULL, G_TYPE_NONE, 0);
}

static void
tandem_device_init (TandemDevice *self)
{
  self->plugins = g_ptr_array_new_with_free_func (g_object_unref);
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

static GKeyFile *
build_trusted_info (TandemDevice *self)
{
  GKeyFile *info = g_key_file_new ();

  g_key_file_set_string (info, "device", "name", self->name);
  g_key_file_set_string (info, "device", "type", self->type);
  g_key_file_set_int64 (info, "device", "protocolVersion", self->protocol_version);
  g_key_file_set_string_list (info, "device", "incoming", (const char *const *) self->incoming,
                              g_strv_length (self->incoming));
  g_key_file_set_string_list (info, "device", "outgoing", (const char *const *) self->outgoing,
                              g_strv_length (self->outgoing));
  if (self->last_address != NULL)
    {
      g_autofree char *address = g_inet_address_to_string (self->last_address);

      g_key_file_set_string (info, "device", "lastAddress", address);
    }
  return info;
}

static void
save_trusted_info (TandemDevice *self)
{
  g_autoptr(GKeyFile) info = build_trusted_info (self);
  g_autoptr(GError) error = NULL;

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

char *
tandem_device_get_verification_key (TandemDevice *self)
{
  GTlsCertificate *peer = tandem_device_get_certificate (self);
  gint64 timestamp = self->protocol_version >= 8 ? self->pair_timestamp : 0;

  if (peer == NULL)
    return NULL;
  return tandem_cert_verification_key (tandem_config_get_certificate (self->config), peer, timestamp);
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

GSettings *
tandem_device_get_settings (TandemDevice *self)
{
  if (self->settings == NULL)
    {
      g_autofree char *path = g_strdup_printf ("/de/flk/Tandem/devices/%s/", self->id);

      self->settings = g_settings_new_with_path ("de.flk.Tandem.Device", path);
    }
  return self->settings;
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
set_pair_state (TandemDevice *self, TandemPairState state)
{
  g_clear_handle_id (&self->pair_timeout_id, g_source_remove);
  if (self->pair_state == state)
    return;
  self->pair_state = state;
  g_signal_emit (self, signals[CHANGED], 0);
}

static void
pairing_done (TandemDevice *self)
{
  GTlsCertificate *peer = self->link != NULL ? tandem_link_get_peer_certificate (self->link) : NULL;
  g_autoptr(GKeyFile) info = NULL;
  g_autoptr(GError) error = NULL;

  if (peer == NULL)
    {
      set_pair_state (self, TANDEM_PAIR_NONE);
      g_signal_emit (self, signals[PAIR_FAILED], 0, "No certificate to trust");
      return;
    }
  g_set_object (&self->certificate, peer);
  info = build_trusted_info (self);
  if (!tandem_config_save_trusted (self->config, self->id, peer, info, &error))
    g_warning ("cannot store the trust for %s: %s", self->id, error->message);
  g_message ("paired with %s (%s)", self->name, self->id);
  set_pair_state (self, TANDEM_PAIR_PAIRED);
  load_plugins (self);
}

static void
unpaired (TandemDevice *self)
{
  gboolean was_paired = self->pair_state == TANDEM_PAIR_PAIRED;

  unload_plugins (self);
  g_clear_object (&self->certificate);
  if (was_paired)
    {
      tandem_config_remove_trusted (self->config, self->id);
      g_message ("unpaired from %s (%s)", self->name, self->id);
    }
  set_pair_state (self, TANDEM_PAIR_NONE);
}

static void
send_pair_packet (TandemDevice *self, gboolean pair, gboolean with_timestamp)
{
  g_autoptr(TandemPacket) packet = tandem_packet_new ("kdeconnect.pair");

  tandem_packet_set_boolean (packet, "pair", pair);
  if (with_timestamp)
    tandem_packet_set_int (packet, "timestamp", self->pair_timestamp);
  tandem_device_send_packet (self, packet);
}

static gboolean
pair_timeout_cb (gpointer user_data)
{
  TandemDevice *self = user_data;

  self->pair_timeout_id = 0;
  if (self->pair_state == TANDEM_PAIR_REQUESTED)
    send_pair_packet (self, FALSE, FALSE);
  set_pair_state (self, TANDEM_PAIR_NONE);
  g_signal_emit (self, signals[PAIR_FAILED], 0, "Timed out");
  return G_SOURCE_REMOVE;
}

static void
start_pair_timeout (TandemDevice *self)
{
  g_clear_handle_id (&self->pair_timeout_id, g_source_remove);
  self->pair_timeout_id = g_timeout_add_seconds (PAIR_TIMEOUT_SECONDS, pair_timeout_cb, self);
}

static void
handle_pair_packet (TandemDevice *self, TandemPacket *packet)
{
  gboolean wants_pair = tandem_packet_get_boolean (packet, "pair", FALSE);

  g_clear_handle_id (&self->pair_timeout_id, g_source_remove);

  if (wants_pair)
    {
      switch (self->pair_state)
        {
        case TANDEM_PAIR_REQUESTED:
          pairing_done (self);
          break;
        case TANDEM_PAIR_REQUESTED_BY_PEER:
          g_debug ("ignoring a second pair request from %s", self->id);
          break;
        case TANDEM_PAIR_PAIRED:
        case TANDEM_PAIR_NONE:
          if (self->pair_state == TANDEM_PAIR_PAIRED)
            {
              g_warning ("pair request from the already trusted device %s", self->id);
              unpaired (self);
            }
          if (self->protocol_version >= 8)
            {
              gint64 now = g_get_real_time () / G_USEC_PER_SEC;

              self->pair_timestamp = tandem_packet_get_int (packet, "timestamp", -1);
              if (self->pair_timestamp == -1)
                {
                  set_pair_state (self, TANDEM_PAIR_NONE);
                  return;
                }
              if (llabs (self->pair_timestamp - now) > PAIR_CLOCK_TOLERANCE_SECONDS)
                {
                  set_pair_state (self, TANDEM_PAIR_NONE);
                  g_signal_emit (self, signals[PAIR_FAILED], 0, "Device clocks are out of sync");
                  return;
                }
            }
          set_pair_state (self, TANDEM_PAIR_REQUESTED_BY_PEER);
          start_pair_timeout (self);
          g_signal_emit (self, signals[PAIR_REQUEST], 0);
          break;
        }
    }
  else
    {
      switch (self->pair_state)
        {
        case TANDEM_PAIR_NONE:
          break;
        case TANDEM_PAIR_REQUESTED:
        case TANDEM_PAIR_REQUESTED_BY_PEER:
          set_pair_state (self, TANDEM_PAIR_NONE);
          g_signal_emit (self, signals[PAIR_FAILED], 0, "Cancelled by the other device");
          break;
        case TANDEM_PAIR_PAIRED:
          unpaired (self);
          break;
        }
    }
}

void
tandem_device_request_pair (TandemDevice *self)
{
  if (self->pair_state == TANDEM_PAIR_PAIRED)
    {
      g_signal_emit (self, signals[PAIR_FAILED], 0, "Already paired");
      return;
    }
  if (self->pair_state == TANDEM_PAIR_REQUESTED_BY_PEER)
    {
      tandem_device_accept_pair (self);
      return;
    }
  if (!tandem_device_is_reachable (self))
    {
      g_signal_emit (self, signals[PAIR_FAILED], 0, "Device not reachable");
      return;
    }
  self->pair_timestamp = g_get_real_time () / G_USEC_PER_SEC;
  set_pair_state (self, TANDEM_PAIR_REQUESTED);
  start_pair_timeout (self);
  send_pair_packet (self, TRUE, TRUE);
}

void
tandem_device_accept_pair (TandemDevice *self)
{
  if (self->pair_state != TANDEM_PAIR_REQUESTED_BY_PEER)
    return;
  send_pair_packet (self, TRUE, FALSE);
  pairing_done (self);
}

void
tandem_device_reject_pair (TandemDevice *self)
{
  if (self->pair_state != TANDEM_PAIR_REQUESTED_BY_PEER && self->pair_state != TANDEM_PAIR_REQUESTED)
    return;
  send_pair_packet (self, FALSE, FALSE);
  set_pair_state (self, TANDEM_PAIR_NONE);
}

void
tandem_device_unpair (TandemDevice *self)
{
  if (self->pair_state == TANDEM_PAIR_NONE)
    return;
  if (tandem_device_is_reachable (self))
    send_pair_packet (self, FALSE, FALSE);
  unpaired (self);
}

static void
unload_plugins (TandemDevice *self)
{
  if (self->plugins == NULL || self->plugins->len == 0)
    return;
  for (guint i = 0; i < self->plugins->len; i++)
    {
      TandemPlugin *plugin = g_ptr_array_index (self->plugins, i);
      TandemPluginClass *klass = TANDEM_PLUGIN_GET_CLASS (plugin);

      if (tandem_device_is_reachable (self) && klass->disconnected != NULL)
        klass->disconnected (plugin);
      g_object_run_dispose (G_OBJECT (plugin));
    }
  g_ptr_array_set_size (self->plugins, 0);
  g_signal_emit (self, signals[PLUGINS_CHANGED], 0);
}

static void
load_plugins (TandemDevice *self)
{
  guint n_plugins;
  const TandemPluginInfo *registry = tandem_plugin_registry (&n_plugins);
  g_auto(GStrv) global_disabled = NULL;
  g_auto(GStrv) device_disabled = NULL;

  if (self->pair_state != TANDEM_PAIR_PAIRED || !tandem_device_is_reachable (self))
    return;
  if (self->plugins->len > 0)
    return;

  global_disabled = g_settings_get_strv (tandem_config_get_settings (self->config) != NULL
                                         ? tandem_config_get_settings (self->config)
                                         : tandem_device_get_settings (self), "disabled-plugins");
  device_disabled = g_settings_get_strv (tandem_device_get_settings (self), "disabled-plugins");

  for (guint i = 0; i < n_plugins; i++)
    {
      GType type = registry[i].get_type ();
      TandemPlugin *plugin;
      TandemPluginClass *klass;

      if (g_strv_contains ((const char *const *) global_disabled, registry[i].name) ||
          g_strv_contains ((const char *const *) device_disabled, registry[i].name))
        continue;
      if (!tandem_plugin_type_supports (type, self))
        continue;
      plugin = g_object_new (type, "device", self, NULL);
      g_ptr_array_add (self->plugins, plugin);
      klass = TANDEM_PLUGIN_GET_CLASS (plugin);
      if (klass->connected != NULL)
        klass->connected (plugin);
    }
  g_debug ("device %s: %u plugins loaded", self->id, self->plugins->len);
  g_signal_emit (self, signals[PLUGINS_CHANGED], 0);
}

GPtrArray *
tandem_device_get_plugins (TandemDevice *self)
{
  return self->plugins;
}

TandemPlugin *
tandem_device_get_plugin (TandemDevice *self, const char *name)
{
  for (guint i = 0; i < self->plugins->len; i++)
    {
      TandemPlugin *plugin = g_ptr_array_index (self->plugins, i);

      if (g_strcmp0 (tandem_plugin_get_name (plugin), name) == 0)
        return plugin;
    }
  return NULL;
}

gboolean
tandem_device_plugin_is_enabled (TandemDevice *self, const char *name)
{
  g_auto(GStrv) disabled = g_settings_get_strv (tandem_device_get_settings (self), "disabled-plugins");

  return !g_strv_contains ((const char *const *) disabled, name);
}

void
tandem_device_set_plugin_enabled (TandemDevice *self, const char *name, gboolean enabled)
{
  g_auto(GStrv) disabled = g_settings_get_strv (tandem_device_get_settings (self), "disabled-plugins");
  g_autoptr(GStrvBuilder) builder = g_strv_builder_new ();
  g_auto(GStrv) updated = NULL;

  for (char **p = disabled; *p != NULL; p++)
    if (g_strcmp0 (*p, name) != 0)
      g_strv_builder_add (builder, *p);
  if (!enabled)
    g_strv_builder_add (builder, name);
  updated = g_strv_builder_end (builder);
  g_settings_set_strv (tandem_device_get_settings (self), "disabled-plugins", (const char *const *) updated);

  unload_plugins (self);
  load_plugins (self);
}

static void
link_packet_cb (TandemLink *link, TandemPacket *packet, gpointer user_data)
{
  TandemDevice *self = user_data;
  gboolean handled = FALSE;

  if (tandem_packet_is_type (packet, "kdeconnect.pair"))
    {
      handle_pair_packet (self, packet);
      return;
    }
  if (self->pair_state != TANDEM_PAIR_PAIRED)
    {
      if (!self->rejected_unpaired_packet)
        {
          self->rejected_unpaired_packet = TRUE;
          send_pair_packet (self, FALSE, FALSE);
        }
      return;
    }
  for (guint i = 0; i < self->plugins->len; i++)
    {
      TandemPlugin *plugin = g_ptr_array_index (self->plugins, i);

      if (tandem_plugin_handles (plugin, packet->type))
        {
          TANDEM_PLUGIN_GET_CLASS (plugin)->handle_packet (plugin, packet);
          handled = TRUE;
        }
    }
  if (!handled)
    g_debug ("device %s: no plugin for %s", self->id, packet->type);
  g_signal_emit (self, signals[PACKET], 0, packet);
}

static void
link_closed_cb (TandemLink *link, gpointer user_data)
{
  TandemDevice *self = user_data;

  if (self->link != link)
    return;
  g_debug ("device %s (%s) unreachable", self->name, self->id);
  for (guint i = 0; i < self->plugins->len; i++)
    {
      TandemPlugin *plugin = g_ptr_array_index (self->plugins, i);
      TandemPluginClass *klass = TANDEM_PLUGIN_GET_CLASS (plugin);

      if (klass->disconnected != NULL)
        klass->disconnected (plugin);
    }
  g_signal_handlers_disconnect_by_data (link, self);
  g_clear_object (&self->link);
  if (self->pair_state == TANDEM_PAIR_REQUESTED || self->pair_state == TANDEM_PAIR_REQUESTED_BY_PEER)
    set_pair_state (self, TANDEM_PAIR_NONE);
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
      gboolean had_plugins = self->plugins->len > 0;

      self->link = g_object_ref (link);
      self->rejected_unpaired_packet = FALSE;
      apply_identity (self, tandem_link_get_identity (link));
      if (address != NULL)
        g_set_object (&self->last_address, address);
      g_signal_connect (link, "packet", G_CALLBACK (link_packet_cb), self);
      g_signal_connect (link, "closed", G_CALLBACK (link_closed_cb), self);
      g_debug ("device %s (%s) reachable", self->name, self->id);
      if (self->pair_state == TANDEM_PAIR_PAIRED)
        {
          save_trusted_info (self);
          if (had_plugins)
            {
              for (guint i = 0; i < self->plugins->len; i++)
                {
                  TandemPlugin *plugin = g_ptr_array_index (self->plugins, i);
                  TandemPluginClass *klass = TANDEM_PLUGIN_GET_CLASS (plugin);

                  if (klass->connected != NULL)
                    klass->connected (plugin);
                }
            }
          else
            {
              load_plugins (self);
            }
        }
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
