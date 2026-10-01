/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "../device.h"
#include "../kodi.h"
#include "../plugin.h"

#include <stdarg.h>

#define PLAYER_NAME "Kodi"

#define TANDEM_TYPE_KODICONTROL_PLUGIN (tandem_kodicontrol_plugin_get_type ())
G_DECLARE_FINAL_TYPE (TandemKodicontrolPlugin, tandem_kodicontrol_plugin, TANDEM, KODICONTROL_PLUGIN, TandemPlugin)

struct _TandemKodicontrolPlugin
{
  TandemPlugin parent_instance;

  gint64 player_id;
  gboolean playing;
  gint64 pos_ms;
  gint64 length_ms;
  gboolean can_seek;
  char *title;
  char *artist;
  char *album;
  gint64 volume;
  guint refresh_id;
  int outstanding;
  gulong notify_id;
  gulong connected_id;
};

G_DEFINE_FINAL_TYPE (TandemKodicontrolPlugin, tandem_kodicontrol_plugin, TANDEM_TYPE_PLUGIN)

static const char *const kodicontrol_incoming[] = { "kdeconnect.mpris.request", NULL };
static const char *const kodicontrol_outgoing[] = { "kdeconnect.mpris", NULL };

static JsonNode *
params_new (const char *first_key, ...)
{
  JsonObject *obj = json_object_new ();
  JsonNode *node = json_node_new (JSON_NODE_OBJECT);
  va_list ap;
  const char *key;

  va_start (ap, first_key);
  for (key = first_key; key != NULL; key = va_arg (ap, const char *))
    {
      JsonNode *value = va_arg (ap, JsonNode *);

      json_object_set_member (obj, key, value);
    }
  va_end (ap);
  json_node_take_object (node, obj);
  return node;
}

static JsonNode *
int_node (gint64 v)
{
  JsonNode *n = json_node_new (JSON_NODE_VALUE);

  json_node_set_int (n, v);
  return n;
}

static JsonNode *
string_node (const char *s)
{
  JsonNode *n = json_node_new (JSON_NODE_VALUE);

  json_node_set_string (n, s);
  return n;
}

static JsonNode *
strv_node (const char *const *values)
{
  JsonArray *array = json_array_new ();
  JsonNode *n = json_node_new (JSON_NODE_ARRAY);

  for (; *values != NULL; values++)
    json_array_add_string_element (array, *values);
  json_node_take_array (n, array);
  return n;
}

static gint64
time_to_ms (JsonObject *t)
{
  if (t == NULL)
    return 0;
  return ((json_object_get_int_member_with_default (t, "hours", 0) * 60 +
           json_object_get_int_member_with_default (t, "minutes", 0)) * 60 +
          json_object_get_int_member_with_default (t, "seconds", 0)) * 1000 +
         json_object_get_int_member_with_default (t, "milliseconds", 0);
}

static void
send_player_list (TandemPlugin *plugin)
{
  g_autoptr(TandemPacket) packet = tandem_packet_new ("kdeconnect.mpris");
  JsonArray *array = json_array_new ();

  if (tandem_kodi_is_connected (tandem_kodi_get ()))
    json_array_add_string_element (array, PLAYER_NAME);
  json_object_set_array_member (packet->body, "playerList", array);
  tandem_packet_set_boolean (packet, "supportAlbumArtPayload", FALSE);
  tandem_plugin_send (plugin, packet);
}

static void
send_now_playing (TandemKodicontrolPlugin *self)
{
  g_autoptr(TandemPacket) packet = tandem_packet_new ("kdeconnect.mpris");
  const char *title = self->title != NULL ? self->title : "";
  const char *artist = self->artist != NULL ? self->artist : "";
  gboolean active = self->player_id >= 0;

  tandem_packet_set_string (packet, "player", PLAYER_NAME);
  tandem_packet_set_string (packet, "title", title);
  tandem_packet_set_string (packet, "artist", artist);
  tandem_packet_set_string (packet, "album", self->album != NULL ? self->album : "");
  if (*artist != '\0' && *title != '\0')
    {
      g_autofree char *now = g_strdup_printf ("%s - %s", artist, title);

      tandem_packet_set_string (packet, "nowPlaying", now);
    }
  else
    tandem_packet_set_string (packet, "nowPlaying", *title != '\0' ? title : artist);
  tandem_packet_set_boolean (packet, "isPlaying", active && self->playing);
  tandem_packet_set_boolean (packet, "canPause", active);
  tandem_packet_set_boolean (packet, "canPlay", active);
  tandem_packet_set_boolean (packet, "canGoNext", active);
  tandem_packet_set_boolean (packet, "canGoPrevious", active);
  tandem_packet_set_boolean (packet, "canSeek", active && self->can_seek);
  tandem_packet_set_int (packet, "length", self->length_ms);
  tandem_packet_set_int (packet, "pos", self->pos_ms);
  tandem_packet_set_int (packet, "volume", self->volume);
  tandem_plugin_send (TANDEM_PLUGIN (self), packet);
}

static void
part_done (TandemKodicontrolPlugin *self)
{
  if (--self->outstanding == 0)
    {
      send_now_playing (self);
      g_object_unref (self);
    }
}

static void
volume_cb (JsonNode *result, gpointer user_data)
{
  TandemKodicontrolPlugin *self = user_data;

  if (result != NULL && JSON_NODE_HOLDS_OBJECT (result))
    self->volume = json_object_get_int_member_with_default (json_node_get_object (result), "volume", self->volume);
  part_done (self);
}

static void
item_cb (JsonNode *result, gpointer user_data)
{
  TandemKodicontrolPlugin *self = user_data;
  JsonObject *item = NULL;

  g_clear_pointer (&self->title, g_free);
  g_clear_pointer (&self->artist, g_free);
  g_clear_pointer (&self->album, g_free);
  if (result != NULL && JSON_NODE_HOLDS_OBJECT (result) && json_object_has_member (json_node_get_object (result), "item"))
    item = json_object_get_object_member (json_node_get_object (result), "item");
  if (item != NULL)
    {
      const char *show = json_object_get_string_member_with_default (item, "showtitle", "");

      self->title = g_strdup (json_object_get_string_member_with_default (item, "title", ""));
      if (*self->title == '\0')
        {
          g_free (self->title);
          self->title = g_strdup (json_object_get_string_member_with_default (item, "label", ""));
        }
      if (json_object_has_member (item, "artist") && JSON_NODE_HOLDS_ARRAY (json_object_get_member (item, "artist")))
        {
          JsonArray *artists = json_object_get_array_member (item, "artist");

          if (json_array_get_length (artists) > 0)
            self->artist = g_strdup (json_array_get_string_element (artists, 0));
        }
      if (self->artist == NULL && *show != '\0')
        self->artist = g_strdup (show);
      self->album = g_strdup (json_object_get_string_member_with_default (item, "album", ""));
    }
  part_done (self);
}

static void
properties_cb (JsonNode *result, gpointer user_data)
{
  TandemKodicontrolPlugin *self = user_data;

  if (result != NULL && JSON_NODE_HOLDS_OBJECT (result))
    {
      JsonObject *p = json_node_get_object (result);

      self->playing = json_object_get_int_member_with_default (p, "speed", 0) != 0;
      self->can_seek = json_object_get_boolean_member_with_default (p, "canseek", FALSE);
      self->pos_ms = json_object_has_member (p, "time") ? time_to_ms (json_object_get_object_member (p, "time")) : 0;
      self->length_ms = json_object_has_member (p, "totaltime") ? time_to_ms (json_object_get_object_member (p, "totaltime")) : 0;
    }
  part_done (self);
}

static void
active_players_cb (JsonNode *result, gpointer user_data)
{
  TandemKodicontrolPlugin *self = user_data;
  TandemKodi *kodi = tandem_kodi_get ();
  static const char *const item_props[] = { "title", "artist", "album", "showtitle", NULL };
  static const char *const player_props[] = { "speed", "time", "totaltime", "canseek", NULL };
  static const char *const app_props[] = { "volume", NULL };

  self->player_id = -1;
  if (result != NULL && JSON_NODE_HOLDS_ARRAY (result) && json_array_get_length (json_node_get_array (result)) > 0)
    self->player_id = json_object_get_int_member_with_default (json_array_get_object_element (json_node_get_array (result), 0), "playerid", -1);

  g_debug ("kodicontrol: active player %" G_GINT64_FORMAT " (%s)", self->player_id,
           result != NULL ? json_node_type_name (result) : "no result");
  self->outstanding = 1;
  tandem_kodi_call (kodi, "Application.GetProperties", params_new ("properties", strv_node (app_props), NULL), volume_cb, self);
  if (self->player_id >= 0)
    {
      self->outstanding += 2;
      tandem_kodi_call (kodi, "Player.GetItem",
                        params_new ("playerid", int_node (self->player_id), "properties", strv_node (item_props), NULL),
                        item_cb, self);
      tandem_kodi_call (kodi, "Player.GetProperties",
                        params_new ("playerid", int_node (self->player_id), "properties", strv_node (player_props), NULL),
                        properties_cb, self);
    }
  else
    {
      self->playing = FALSE;
      self->pos_ms = self->length_ms = 0;
      g_clear_pointer (&self->title, g_free);
      g_clear_pointer (&self->artist, g_free);
      g_clear_pointer (&self->album, g_free);
    }
}

static void
refresh (TandemKodicontrolPlugin *self)
{
  if (self->outstanding > 0 || !tandem_kodi_is_connected (tandem_kodi_get ()))
    return;
  self->outstanding = 1;
  tandem_kodi_call (tandem_kodi_get (), "Player.GetActivePlayers", NULL, active_players_cb, g_object_ref (self));
}

static void
refresh_once (gpointer user_data)
{
  TandemKodicontrolPlugin *self = user_data;

  self->refresh_id = 0;
  refresh (self);
}

static void
refresh_soon (TandemKodicontrolPlugin *self, guint ms)
{
  if (self->refresh_id == 0)
    self->refresh_id = g_timeout_add_once (ms, refresh_once, self);
}

static void
kodi_notification_cb (TandemKodi *kodi, const char *method, gpointer user_data)
{
  if (g_str_has_prefix (method, "Player.") || g_str_equal (method, "Application.OnVolumeChanged"))
    refresh_soon (user_data, 300);
}

static void
kodi_connected_cb (TandemKodi *kodi, gpointer user_data)
{
  send_player_list (TANDEM_PLUGIN (user_data));
  refresh_soon (user_data, 300);
}

static void
player_call (TandemKodicontrolPlugin *self, const char *method, gpointer unused)
{
  tandem_kodi_call (tandem_kodi_get (), method, params_new ("playerid", int_node (self->player_id), NULL), NULL, NULL);
}

static void
kodicontrol_handle_packet (TandemPlugin *plugin, TandemPacket *packet)
{
  TandemKodicontrolPlugin *self = TANDEM_KODICONTROL_PLUGIN (plugin);
  TandemKodi *kodi = tandem_kodi_get ();
  const char *player = tandem_packet_get_string (packet, "player", NULL);

  if (tandem_packet_get_boolean (packet, "requestPlayerList", FALSE))
    send_player_list (plugin);
  if (g_strcmp0 (player, PLAYER_NAME) != 0)
    return;

  if (tandem_packet_has (packet, "action") && self->player_id >= 0)
    {
      const char *action = tandem_packet_get_string (packet, "action", "");

      if (g_str_equal (action, "PlayPause"))
        player_call (self, "Player.PlayPause", NULL);
      else if (g_str_equal (action, "Play") && !self->playing)
        player_call (self, "Player.PlayPause", NULL);
      else if (g_str_equal (action, "Pause") && self->playing)
        player_call (self, "Player.PlayPause", NULL);
      else if (g_str_equal (action, "Stop"))
        player_call (self, "Player.Stop", NULL);
      else if (g_str_equal (action, "Next") || g_str_equal (action, "Previous"))
        tandem_kodi_call (kodi, "Player.GoTo",
                          params_new ("playerid", int_node (self->player_id),
                                      "to", string_node (g_str_equal (action, "Next") ? "next" : "previous"), NULL),
                          NULL, NULL);
    }
  if (tandem_packet_has (packet, "setVolume"))
    tandem_kodi_call (kodi, "Application.SetVolume",
                      params_new ("volume", int_node (CLAMP (tandem_packet_get_int (packet, "setVolume", 100), 0, 100)), NULL),
                      NULL, NULL);
  if (self->player_id >= 0 && (tandem_packet_has (packet, "Seek") || tandem_packet_has (packet, "SetPosition")))
    {
      gint64 target_ms = tandem_packet_has (packet, "SetPosition") ? tandem_packet_get_int (packet, "SetPosition", 0)
                                                                    : self->pos_ms + tandem_packet_get_int (packet, "Seek", 0) / 1000;
      JsonObject *time = json_object_new ();
      JsonObject *value = json_object_new ();
      JsonNode *value_node = json_node_new (JSON_NODE_OBJECT);

      target_ms = MAX (target_ms, 0);
      json_object_set_int_member (time, "hours", target_ms / 3600000);
      json_object_set_int_member (time, "minutes", (target_ms / 60000) % 60);
      json_object_set_int_member (time, "seconds", (target_ms / 1000) % 60);
      json_object_set_int_member (time, "milliseconds", target_ms % 1000);
      json_object_set_object_member (value, "time", time);
      json_node_take_object (value_node, value);
      tandem_kodi_call (kodi, "Player.Seek", params_new ("playerid", int_node (self->player_id), "value", value_node, NULL),
                        NULL, NULL);
    }
  if (tandem_packet_get_boolean (packet, "requestNowPlaying", FALSE) || tandem_packet_get_boolean (packet, "requestVolume", FALSE))
    refresh (self);
  else
    refresh_soon (self, 500);
}

static void
kodicontrol_connected (TandemPlugin *plugin)
{
  send_player_list (plugin);
  refresh (TANDEM_KODICONTROL_PLUGIN (plugin));
}

static void
tandem_kodicontrol_plugin_constructed (GObject *object)
{
  TandemKodicontrolPlugin *self = TANDEM_KODICONTROL_PLUGIN (object);
  TandemKodi *kodi = tandem_kodi_get ();

  G_OBJECT_CLASS (tandem_kodicontrol_plugin_parent_class)->constructed (object);
  self->notify_id = g_signal_connect (kodi, "notification", G_CALLBACK (kodi_notification_cb), self);
  self->connected_id = g_signal_connect (kodi, "connected-changed", G_CALLBACK (kodi_connected_cb), self);
}

static void
tandem_kodicontrol_plugin_dispose (GObject *object)
{
  TandemKodicontrolPlugin *self = TANDEM_KODICONTROL_PLUGIN (object);

  g_clear_signal_handler (&self->notify_id, tandem_kodi_get ());
  g_clear_signal_handler (&self->connected_id, tandem_kodi_get ());
  g_clear_handle_id (&self->refresh_id, g_source_remove);
  G_OBJECT_CLASS (tandem_kodicontrol_plugin_parent_class)->dispose (object);
}

static void
tandem_kodicontrol_plugin_finalize (GObject *object)
{
  TandemKodicontrolPlugin *self = TANDEM_KODICONTROL_PLUGIN (object);

  g_free (self->title);
  g_free (self->artist);
  g_free (self->album);
  G_OBJECT_CLASS (tandem_kodicontrol_plugin_parent_class)->finalize (object);
}

static void
tandem_kodicontrol_plugin_class_init (TandemKodicontrolPluginClass *klass)
{
  TandemPluginClass *plugin_class = TANDEM_PLUGIN_CLASS (klass);
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->constructed = tandem_kodicontrol_plugin_constructed;
  object_class->dispose = tandem_kodicontrol_plugin_dispose;
  object_class->finalize = tandem_kodicontrol_plugin_finalize;
  plugin_class->name = "kodicontrol";
  plugin_class->title = "Kodi media control";
  plugin_class->incoming = kodicontrol_incoming;
  plugin_class->outgoing = kodicontrol_outgoing;
  plugin_class->handle_packet = kodicontrol_handle_packet;
  plugin_class->connected = kodicontrol_connected;
}

static void
tandem_kodicontrol_plugin_init (TandemKodicontrolPlugin *self)
{
  self->player_id = -1;
  self->volume = 100;
}
