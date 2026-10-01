/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "../device.h"
#include "../mpris-watch.h"
#include "../plugin.h"

#include <string.h>

#define TANDEM_TYPE_MPRISCONTROL_PLUGIN (tandem_mpriscontrol_plugin_get_type ())
G_DECLARE_FINAL_TYPE (TandemMpriscontrolPlugin, tandem_mpriscontrol_plugin, TANDEM, MPRISCONTROL_PLUGIN, TandemPlugin)

struct _TandemMpriscontrolPlugin
{
  TandemPlugin parent_instance;

  gboolean connected;
  guint throttle_id;
  GHashTable *pending_players;
};

G_DEFINE_FINAL_TYPE (TandemMpriscontrolPlugin, tandem_mpriscontrol_plugin, TANDEM_TYPE_PLUGIN)

static const char *const mpriscontrol_incoming[] = { "kdeconnect.mpris.request", NULL };
static const char *const mpriscontrol_outgoing[] = { "kdeconnect.mpris", NULL };

static void
send_player_list (TandemPlugin *plugin)
{
  TandemMprisWatch *watch = tandem_mpris_watch_get ();
  g_autoptr(GList) keys = tandem_mpris_watch_list (watch);
  g_autoptr(TandemPacket) packet = tandem_packet_new ("kdeconnect.mpris");
  JsonArray *array = json_array_new ();

  for (GList *l = keys; l != NULL; l = l->next)
    json_array_add_string_element (array, l->data);
  json_object_set_array_member (packet->body, "playerList", array);
  tandem_packet_set_boolean (packet, "supportAlbumArtPayload", TRUE);
  tandem_plugin_send (plugin, packet);
}

static const char *
metadata_string (GVariant *metadata, const char *key)
{
  const char *value = NULL;

  if (metadata != NULL)
    g_variant_lookup (metadata, key, "&s", &value);
  return value != NULL ? value : "";
}

static char *
metadata_artist (GVariant *metadata)
{
  g_autoptr(GVariant) artists = metadata != NULL ? g_variant_lookup_value (metadata, "xesam:artist", G_VARIANT_TYPE_STRING_ARRAY) : NULL;
  g_autofree const char **list = NULL;

  if (artists == NULL)
    return g_strdup (metadata_string (metadata, "xesam:artist"));
  list = g_variant_get_strv (artists, NULL);
  return g_strjoinv (", ", (char **) list);
}

static void
send_now_playing (TandemPlugin *plugin, const char *key)
{
  TandemMprisWatch *watch = tandem_mpris_watch_get ();
  TandemMprisPlayer *player = tandem_mpris_watch_lookup (watch, key);
  g_autoptr(GVariant) metadata = NULL;
  g_autofree char *artist = NULL;
  g_autoptr(TandemPacket) packet = NULL;
  const char *status;
  gint64 length_us = 0;
  const char *title;

  if (player == NULL)
    return;
  metadata = tandem_mpris_player_get_metadata (player);
  if (metadata != NULL)
    g_variant_lookup (metadata, "mpris:length", "x", &length_us);
  artist = metadata_artist (metadata);
  title = metadata_string (metadata, "xesam:title");
  status = tandem_mpris_player_get_string (player, "PlaybackStatus", "Stopped");

  packet = tandem_packet_new ("kdeconnect.mpris");
  tandem_packet_set_string (packet, "player", key);
  tandem_packet_set_string (packet, "title", title);
  tandem_packet_set_string (packet, "artist", artist);
  tandem_packet_set_string (packet, "album", metadata_string (metadata, "xesam:album"));
  tandem_packet_set_string (packet, "albumArtUrl", metadata_string (metadata, "mpris:artUrl"));
  if (*artist != '\0' && *title != '\0')
    {
      g_autofree char *now = g_strdup_printf ("%s - %s", artist, title);

      tandem_packet_set_string (packet, "nowPlaying", now);
    }
  else
    {
      tandem_packet_set_string (packet, "nowPlaying", *title != '\0' ? title : artist);
    }
  tandem_packet_set_boolean (packet, "isPlaying", g_strcmp0 (status, "Playing") == 0);
  tandem_packet_set_boolean (packet, "canPause", tandem_mpris_player_get_boolean (player, "CanPause", FALSE));
  tandem_packet_set_boolean (packet, "canPlay", tandem_mpris_player_get_boolean (player, "CanPlay", FALSE));
  tandem_packet_set_boolean (packet, "canGoNext", tandem_mpris_player_get_boolean (player, "CanGoNext", FALSE));
  tandem_packet_set_boolean (packet, "canGoPrevious", tandem_mpris_player_get_boolean (player, "CanGoPrevious", FALSE));
  tandem_packet_set_boolean (packet, "canSeek", tandem_mpris_player_get_boolean (player, "CanSeek", FALSE));
  tandem_packet_set_string (packet, "loopStatus", tandem_mpris_player_get_string (player, "LoopStatus", "None"));
  tandem_packet_set_boolean (packet, "shuffle", tandem_mpris_player_get_boolean (player, "Shuffle", FALSE));
  tandem_packet_set_int (packet, "length", length_us / 1000);
  tandem_packet_set_int (packet, "pos", tandem_mpris_player_get_position (player) / 1000);
  tandem_packet_set_int (packet, "volume", (gint64) (tandem_mpris_player_get_double (player, "Volume", 1.0) * 100));
  tandem_plugin_send (plugin, packet);
}

static void
send_album_art (TandemPlugin *plugin, const char *key, const char *url)
{
  TandemMprisWatch *watch = tandem_mpris_watch_get ();
  TandemMprisPlayer *player = tandem_mpris_watch_lookup (watch, key);
  g_autoptr(GVariant) metadata = NULL;
  g_autoptr(GFile) file = NULL;
  g_autoptr(GFileInfo) info = NULL;
  g_autoptr(GFileInputStream) stream = NULL;
  g_autoptr(TandemPacket) packet = NULL;

  if (player == NULL || url == NULL || !g_str_has_prefix (url, "file://"))
    return;
  metadata = tandem_mpris_player_get_metadata (player);
  if (g_strcmp0 (metadata_string (metadata, "mpris:artUrl"), url) != 0)
    return;
  file = g_file_new_for_uri (url);
  info = g_file_query_info (file, "standard::size", G_FILE_QUERY_INFO_NONE, NULL, NULL);
  stream = g_file_read (file, NULL, NULL);
  if (info == NULL || stream == NULL || g_file_info_get_size (info) > (8 << 20))
    return;
  packet = tandem_packet_new ("kdeconnect.mpris");
  tandem_packet_set_string (packet, "player", key);
  tandem_packet_set_boolean (packet, "transferringAlbumArt", TRUE);
  tandem_packet_set_string (packet, "albumArtUrl", url);
  tandem_packet_set_payload (packet, G_INPUT_STREAM (stream), g_file_info_get_size (info));
  tandem_plugin_send (plugin, packet);
}

static void
mpriscontrol_handle_packet (TandemPlugin *plugin, TandemPacket *packet)
{
  TandemMprisWatch *watch = tandem_mpris_watch_get ();
  const char *key = tandem_packet_get_string (packet, "player", NULL);
  TandemMprisPlayer *player = tandem_mpris_watch_lookup (watch, key);
  gboolean changed = FALSE;

  if (tandem_packet_get_boolean (packet, "requestPlayerList", FALSE))
    send_player_list (plugin);
  if (player == NULL)
    return;

  if (tandem_packet_has (packet, "action"))
    {
      const char *action = tandem_packet_get_string (packet, "action", "");

      if (g_str_equal (action, "Next") || g_str_equal (action, "Previous") || g_str_equal (action, "Pause") ||
          g_str_equal (action, "PlayPause") || g_str_equal (action, "Stop") || g_str_equal (action, "Play"))
        tandem_mpris_player_call (player, action, NULL);
      changed = TRUE;
    }
  if (tandem_packet_has (packet, "setVolume"))
    {
      tandem_mpris_player_set_property (player, "Volume",
                                        g_variant_new_double (tandem_packet_get_double (packet, "setVolume", 100) / 100.0));
      changed = TRUE;
    }
  if (tandem_packet_has (packet, "Seek"))
    {
      tandem_mpris_player_call (player, "Seek", g_variant_new ("(x)", tandem_packet_get_int (packet, "Seek", 0)));
      changed = TRUE;
    }
  if (tandem_packet_has (packet, "SetPosition"))
    {
      g_autoptr(GVariant) metadata = tandem_mpris_player_get_metadata (player);
      const char *track = NULL;

      if (metadata != NULL)
        g_variant_lookup (metadata, "mpris:trackid", "&o", &track);
      if (track != NULL)
        tandem_mpris_player_call (player, "SetPosition",
                                  g_variant_new ("(ox)", track, tandem_packet_get_int (packet, "SetPosition", 0) * 1000));
      changed = TRUE;
    }
  if (tandem_packet_has (packet, "setLoopStatus"))
    {
      tandem_mpris_player_set_property (player, "LoopStatus",
                                        g_variant_new_string (tandem_packet_get_string (packet, "setLoopStatus", "None")));
      changed = TRUE;
    }
  if (tandem_packet_has (packet, "setShuffle"))
    {
      tandem_mpris_player_set_property (player, "Shuffle",
                                        g_variant_new_boolean (tandem_packet_get_boolean (packet, "setShuffle", FALSE)));
      changed = TRUE;
    }
  if (tandem_packet_has (packet, "albumArtUrl"))
    send_album_art (plugin, key, tandem_packet_get_string (packet, "albumArtUrl", NULL));
  if (tandem_packet_get_boolean (packet, "requestNowPlaying", FALSE) ||
      tandem_packet_get_boolean (packet, "requestVolume", FALSE) || changed)
    send_now_playing (plugin, key);
}

static gboolean
throttle_cb (gpointer user_data)
{
  TandemMpriscontrolPlugin *self = user_data;
  GHashTableIter iter;
  gpointer key;

  self->throttle_id = 0;
  g_hash_table_iter_init (&iter, self->pending_players);
  while (g_hash_table_iter_next (&iter, &key, NULL))
    send_now_playing (TANDEM_PLUGIN (self), key);
  g_hash_table_remove_all (self->pending_players);
  return G_SOURCE_REMOVE;
}

static void
player_changed_cb (TandemMprisWatch *watch, const char *key, gpointer user_data)
{
  TandemMpriscontrolPlugin *self = user_data;

  if (!self->connected)
    return;
  g_hash_table_add (self->pending_players, g_strdup (key));
  if (self->throttle_id == 0)
    self->throttle_id = g_timeout_add (250, throttle_cb, self);
}

static void
player_list_changed_cb (TandemMprisWatch *watch, const char *key, gpointer user_data)
{
  TandemMpriscontrolPlugin *self = user_data;

  if (self->connected)
    send_player_list (TANDEM_PLUGIN (self));
}

static void
mpriscontrol_connected (TandemPlugin *plugin)
{
  TandemMpriscontrolPlugin *self = TANDEM_MPRISCONTROL_PLUGIN (plugin);

  self->connected = TRUE;
  send_player_list (plugin);
}

static void
mpriscontrol_disconnected (TandemPlugin *plugin)
{
  TandemMpriscontrolPlugin *self = TANDEM_MPRISCONTROL_PLUGIN (plugin);

  self->connected = FALSE;
  g_clear_handle_id (&self->throttle_id, g_source_remove);
  g_hash_table_remove_all (self->pending_players);
}

static void
tandem_mpriscontrol_plugin_constructed (GObject *object)
{
  TandemMprisWatch *watch = tandem_mpris_watch_get ();

  G_OBJECT_CLASS (tandem_mpriscontrol_plugin_parent_class)->constructed (object);
  g_signal_connect_object (watch, "player-added", G_CALLBACK (player_list_changed_cb), object, 0);
  g_signal_connect_object (watch, "player-removed", G_CALLBACK (player_list_changed_cb), object, 0);
  g_signal_connect_object (watch, "player-changed", G_CALLBACK (player_changed_cb), object, 0);
}

static void
tandem_mpriscontrol_plugin_dispose (GObject *object)
{
  TandemMpriscontrolPlugin *self = TANDEM_MPRISCONTROL_PLUGIN (object);

  g_clear_handle_id (&self->throttle_id, g_source_remove);
  g_clear_pointer (&self->pending_players, g_hash_table_unref);
  G_OBJECT_CLASS (tandem_mpriscontrol_plugin_parent_class)->dispose (object);
}

static void
tandem_mpriscontrol_plugin_class_init (TandemMpriscontrolPluginClass *klass)
{
  TandemPluginClass *plugin_class = TANDEM_PLUGIN_CLASS (klass);
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->constructed = tandem_mpriscontrol_plugin_constructed;
  object_class->dispose = tandem_mpriscontrol_plugin_dispose;
  plugin_class->name = "mpriscontrol";
  plugin_class->title = "Media control";
  plugin_class->incoming = mpriscontrol_incoming;
  plugin_class->outgoing = mpriscontrol_outgoing;
  plugin_class->handle_packet = mpriscontrol_handle_packet;
  plugin_class->connected = mpriscontrol_connected;
  plugin_class->disconnected = mpriscontrol_disconnected;
}

static void
tandem_mpriscontrol_plugin_init (TandemMpriscontrolPlugin *self)
{
  self->pending_players = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);
}
