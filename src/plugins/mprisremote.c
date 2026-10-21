/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "../device.h"
#include "../plugin.h"
#include "../transfer.h"
#include "tandem-build.h"

#include <string.h>

#define MAX_EXPORTS 8
#define MPRIS_PATH "/org/mpris/MediaPlayer2"

#define TANDEM_TYPE_MPRISREMOTE_PLUGIN (tandem_mprisremote_plugin_get_type ())
G_DECLARE_FINAL_TYPE (TandemMprisremotePlugin, tandem_mprisremote_plugin, TANDEM, MPRISREMOTE_PLUGIN, TandemPlugin)

typedef struct
{
  gint ref_count;
  TandemMprisremotePlugin *plugin;
  char *name;
  char *bus_name;
  GDBusConnection *connection;
  guint root_registration;
  guint player_registration;
  guint name_owner_id;
  char *title;
  char *artist;
  char *album;
  char *art_url;
  char *art_file;
  gint64 length_ms;
  gint64 pos_ms;
  gint64 pos_updated;
  gboolean playing;
  gboolean can_pause, can_play, can_next, can_previous, can_seek;
  char *loop_status;
  gboolean shuffle;
  int volume;
} Export;

struct _TandemMprisremotePlugin
{
  TandemPlugin parent_instance;

  GHashTable *exports;
  char *art_dir;
};

G_DEFINE_FINAL_TYPE (TandemMprisremotePlugin, tandem_mprisremote_plugin, TANDEM_TYPE_PLUGIN)

static const char *const mprisremote_incoming[] = { "kdeconnect.mpris", NULL };
static const char *const mprisremote_outgoing[] = { "kdeconnect.mpris.request", NULL };

static const char root_xml[] =
  "<node>"
  "  <interface name='org.mpris.MediaPlayer2'>"
  "    <method name='Raise'/><method name='Quit'/>"
  "    <property name='CanQuit' type='b' access='read'/>"
  "    <property name='CanRaise' type='b' access='read'/>"
  "    <property name='HasTrackList' type='b' access='read'/>"
  "    <property name='Identity' type='s' access='read'/>"
  "    <property name='DesktopEntry' type='s' access='read'/>"
  "    <property name='SupportedUriSchemes' type='as' access='read'/>"
  "    <property name='SupportedMimeTypes' type='as' access='read'/>"
  "  </interface>"
  "  <interface name='org.mpris.MediaPlayer2.Player'>"
  "    <method name='Next'/><method name='Previous'/><method name='Pause'/><method name='PlayPause'/>"
  "    <method name='Stop'/><method name='Play'/>"
  "    <method name='Seek'><arg type='x' name='Offset' direction='in'/></method>"
  "    <method name='SetPosition'><arg type='o' name='TrackId' direction='in'/><arg type='x' name='Position' direction='in'/></method>"
  "    <method name='OpenUri'><arg type='s' name='Uri' direction='in'/></method>"
  "    <signal name='Seeked'><arg type='x' name='Position'/></signal>"
  "    <property name='PlaybackStatus' type='s' access='read'/>"
  "    <property name='LoopStatus' type='s' access='readwrite'/>"
  "    <property name='Rate' type='d' access='readwrite'/>"
  "    <property name='Shuffle' type='b' access='readwrite'/>"
  "    <property name='Metadata' type='a{sv}' access='read'/>"
  "    <property name='Volume' type='d' access='readwrite'/>"
  "    <property name='Position' type='x' access='read'/>"
  "    <property name='MinimumRate' type='d' access='read'/>"
  "    <property name='MaximumRate' type='d' access='read'/>"
  "    <property name='CanGoNext' type='b' access='read'/>"
  "    <property name='CanGoPrevious' type='b' access='read'/>"
  "    <property name='CanPlay' type='b' access='read'/>"
  "    <property name='CanPause' type='b' access='read'/>"
  "    <property name='CanSeek' type='b' access='read'/>"
  "    <property name='CanControl' type='b' access='read'/>"
  "  </interface>"
  "</node>";

static GDBusNodeInfo *node_info;

static void
send_request (Export *ex, const char *member, GVariant *value_variant)
{
  g_autoptr(TandemPacket) packet = tandem_packet_new ("kdeconnect.mpris.request");

  tandem_packet_set_string (packet, "player", ex->name);
  if (value_variant == NULL)
    tandem_packet_set_boolean (packet, member, TRUE);
  else if (g_variant_is_of_type (value_variant, G_VARIANT_TYPE_STRING))
    tandem_packet_set_string (packet, member, g_variant_get_string (value_variant, NULL));
  else if (g_variant_is_of_type (value_variant, G_VARIANT_TYPE_BOOLEAN))
    tandem_packet_set_boolean (packet, member, g_variant_get_boolean (value_variant));
  else if (g_variant_is_of_type (value_variant, G_VARIANT_TYPE_INT64))
    tandem_packet_set_int (packet, member, g_variant_get_int64 (value_variant));
  else if (g_variant_is_of_type (value_variant, G_VARIANT_TYPE_DOUBLE))
    tandem_packet_set_double (packet, member, g_variant_get_double (value_variant));
  tandem_plugin_send (TANDEM_PLUGIN (ex->plugin), packet);
  if (value_variant != NULL)
    g_variant_unref (g_variant_ref_sink (value_variant));
}

static void
send_action (Export *ex, const char *action)
{
  g_autoptr(TandemPacket) packet = tandem_packet_new ("kdeconnect.mpris.request");

  tandem_packet_set_string (packet, "player", ex->name);
  tandem_packet_set_string (packet, "action", action);
  tandem_plugin_send (TANDEM_PLUGIN (ex->plugin), packet);
}

static gint64
current_position_us (Export *ex)
{
  gint64 pos = ex->pos_ms;

  if (ex->playing && ex->pos_updated > 0)
    pos += (g_get_monotonic_time () - ex->pos_updated) / 1000;
  return pos * 1000;
}

static GVariant *
build_metadata (Export *ex)
{
  GVariantBuilder builder;
  const char *artists[] = { ex->artist != NULL ? ex->artist : "", NULL };

  g_variant_builder_init (&builder, G_VARIANT_TYPE ("a{sv}"));
  g_variant_builder_add (&builder, "{sv}", "mpris:trackid", g_variant_new_object_path ("/de/flk/Tandem/track/0"));
  g_variant_builder_add (&builder, "{sv}", "mpris:length", g_variant_new_int64 (ex->length_ms * 1000));
  g_variant_builder_add (&builder, "{sv}", "xesam:title", g_variant_new_string (ex->title != NULL ? ex->title : ""));
  g_variant_builder_add (&builder, "{sv}", "xesam:album", g_variant_new_string (ex->album != NULL ? ex->album : ""));
  g_variant_builder_add (&builder, "{sv}", "xesam:artist", g_variant_new_strv (artists, -1));
  if (ex->art_file != NULL)
    {
      g_autofree char *uri = g_filename_to_uri (ex->art_file, NULL, NULL);

      g_variant_builder_add (&builder, "{sv}", "mpris:artUrl", g_variant_new_string (uri));
    }
  return g_variant_builder_end (&builder);
}

static GVariant *
get_property (GDBusConnection  *connection,
              const char       *sender,
              const char       *object_path,
              const char       *interface_name,
              const char       *property_name,
              GError          **error,
              gpointer          user_data)
{
  Export *ex = user_data;
  TandemDevice *device = tandem_plugin_get_device (TANDEM_PLUGIN (ex->plugin));

  if (g_str_equal (property_name, "CanQuit") || g_str_equal (property_name, "CanRaise") ||
      g_str_equal (property_name, "HasTrackList"))
    return g_variant_new_boolean (FALSE);
  if (g_str_equal (property_name, "Identity"))
    {
      g_autofree char *identity = g_strdup_printf ("%s (%s)", ex->name, tandem_device_get_name (device));

      return g_variant_new_string (identity);
    }
  if (g_str_equal (property_name, "DesktopEntry"))
    return g_variant_new_string (TANDEM_APP_ID);
  if (g_str_equal (property_name, "SupportedUriSchemes") || g_str_equal (property_name, "SupportedMimeTypes"))
    return g_variant_new_strv (NULL, 0);
  if (g_str_equal (property_name, "PlaybackStatus"))
    return g_variant_new_string (ex->playing ? "Playing" : "Paused");
  if (g_str_equal (property_name, "LoopStatus"))
    return g_variant_new_string (ex->loop_status != NULL ? ex->loop_status : "None");
  if (g_str_equal (property_name, "Rate") || g_str_equal (property_name, "MinimumRate") ||
      g_str_equal (property_name, "MaximumRate"))
    return g_variant_new_double (1.0);
  if (g_str_equal (property_name, "Shuffle"))
    return g_variant_new_boolean (ex->shuffle);
  if (g_str_equal (property_name, "Metadata"))
    return build_metadata (ex);
  if (g_str_equal (property_name, "Volume"))
    return g_variant_new_double (ex->volume / 100.0);
  if (g_str_equal (property_name, "Position"))
    return g_variant_new_int64 (current_position_us (ex));
  if (g_str_equal (property_name, "CanGoNext"))
    return g_variant_new_boolean (ex->can_next);
  if (g_str_equal (property_name, "CanGoPrevious"))
    return g_variant_new_boolean (ex->can_previous);
  if (g_str_equal (property_name, "CanPlay"))
    return g_variant_new_boolean (ex->can_play);
  if (g_str_equal (property_name, "CanPause"))
    return g_variant_new_boolean (ex->can_pause);
  if (g_str_equal (property_name, "CanSeek"))
    return g_variant_new_boolean (ex->can_seek);
  if (g_str_equal (property_name, "CanControl"))
    return g_variant_new_boolean (TRUE);
  g_set_error (error, G_DBUS_ERROR, G_DBUS_ERROR_UNKNOWN_PROPERTY, "Unknown property %s", property_name);
  return NULL;
}

static gboolean
set_property (GDBusConnection  *connection,
              const char       *sender,
              const char       *object_path,
              const char       *interface_name,
              const char       *property_name,
              GVariant         *value,
              GError          **error,
              gpointer          user_data)
{
  Export *ex = user_data;

  if (g_str_equal (property_name, "Volume"))
    send_request (ex, "setVolume", g_variant_new_int64 ((gint64) (g_variant_get_double (value) * 100)));
  else if (g_str_equal (property_name, "LoopStatus"))
    send_request (ex, "setLoopStatus", g_variant_new_string (g_variant_get_string (value, NULL)));
  else if (g_str_equal (property_name, "Shuffle"))
    send_request (ex, "setShuffle", g_variant_new_boolean (g_variant_get_boolean (value)));
  return TRUE;
}

static void
method_call (GDBusConnection       *connection,
             const char            *sender,
             const char            *object_path,
             const char            *interface_name,
             const char            *method_name,
             GVariant              *parameters,
             GDBusMethodInvocation *invocation,
             gpointer               user_data)
{
  Export *ex = user_data;

  if (g_str_equal (method_name, "Next") || g_str_equal (method_name, "Previous") || g_str_equal (method_name, "Pause") ||
      g_str_equal (method_name, "PlayPause") || g_str_equal (method_name, "Stop") || g_str_equal (method_name, "Play"))
    send_action (ex, method_name);
  else if (g_str_equal (method_name, "Seek"))
    {
      gint64 offset;

      g_variant_get (parameters, "(x)", &offset);
      send_request (ex, "Seek", g_variant_new_int64 (offset));
    }
  else if (g_str_equal (method_name, "SetPosition"))
    {
      const char *track;
      gint64 position;

      g_variant_get (parameters, "(&ox)", &track, &position);
      send_request (ex, "SetPosition", g_variant_new_int64 (position / 1000));
    }
  g_dbus_method_invocation_return_value (invocation, NULL);
}

static const GDBusInterfaceVTable vtable = {
  .method_call = method_call,
  .get_property = get_property,
  .set_property = set_property,
};

static Export *
export_ref (Export *ex)
{
  ex->ref_count++;
  return ex;
}

static void
export_unref (Export *ex)
{
  if (--ex->ref_count > 0)
    return;
  g_free (ex->name);
  g_free (ex->bus_name);
  g_free (ex->title);
  g_free (ex->artist);
  g_free (ex->album);
  g_free (ex->art_url);
  g_free (ex->art_file);
  g_free (ex->loop_status);
  g_free (ex);
}

static void
export_detach (Export *ex)
{
  if (ex->connection != NULL)
    {
      if (ex->root_registration != 0)
        g_dbus_connection_unregister_object (ex->connection, ex->root_registration);
      if (ex->player_registration != 0)
        g_dbus_connection_unregister_object (ex->connection, ex->player_registration);
      g_dbus_connection_close (ex->connection, NULL, NULL, NULL);
      g_clear_object (&ex->connection);
    }
  ex->root_registration = 0;
  ex->player_registration = 0;
  ex->plugin = NULL;
  export_unref (ex);
}

static char *
sanitize (const char *text)
{
  GString *out = g_string_new (NULL);

  for (const char *p = text; *p != '\0'; p++)
    g_string_append_c (out, g_ascii_isalnum (*p) ? *p : '_');
  if (out->len == 0 || g_ascii_isdigit (out->str[0]))
    g_string_prepend (out, "p_");
  return g_string_free (out, FALSE);
}

static void
emit_properties_changed (Export *ex)
{
  GVariantBuilder changed;
  GVariantBuilder invalidated;

  if (ex->connection == NULL || ex->player_registration == 0)
    return;
  g_variant_builder_init (&changed, G_VARIANT_TYPE ("a{sv}"));
  g_variant_builder_add (&changed, "{sv}", "PlaybackStatus", g_variant_new_string (ex->playing ? "Playing" : "Paused"));
  g_variant_builder_add (&changed, "{sv}", "Metadata", build_metadata (ex));
  g_variant_builder_add (&changed, "{sv}", "Volume", g_variant_new_double (ex->volume / 100.0));
  g_variant_builder_add (&changed, "{sv}", "CanGoNext", g_variant_new_boolean (ex->can_next));
  g_variant_builder_add (&changed, "{sv}", "CanGoPrevious", g_variant_new_boolean (ex->can_previous));
  g_variant_builder_add (&changed, "{sv}", "CanPlay", g_variant_new_boolean (ex->can_play));
  g_variant_builder_add (&changed, "{sv}", "CanPause", g_variant_new_boolean (ex->can_pause));
  g_variant_builder_add (&changed, "{sv}", "CanSeek", g_variant_new_boolean (ex->can_seek));
  g_variant_builder_add (&changed, "{sv}", "LoopStatus", g_variant_new_string (ex->loop_status != NULL ? ex->loop_status : "None"));
  g_variant_builder_add (&changed, "{sv}", "Shuffle", g_variant_new_boolean (ex->shuffle));
  g_variant_builder_init (&invalidated, G_VARIANT_TYPE ("as"));
  g_dbus_connection_emit_signal (ex->connection, NULL, MPRIS_PATH, "org.freedesktop.DBus.Properties",
                                 "PropertiesChanged",
                                 g_variant_new ("(sa{sv}as)", "org.mpris.MediaPlayer2.Player", &changed, &invalidated),
                                 NULL);
  g_dbus_connection_emit_signal (ex->connection, NULL, MPRIS_PATH, "org.mpris.MediaPlayer2.Player", "Seeked",
                                 g_variant_new ("(x)", current_position_us (ex)), NULL);
}

static void
connection_ready_cb (GObject *source, GAsyncResult *result, gpointer user_data)
{
  Export *ex = user_data;
  g_autoptr(GError) error = NULL;
  GDBusConnection *connection = g_dbus_connection_new_for_address_finish (result, &error);

  if (connection == NULL)
    {
      g_warning ("mprisremote: no bus connection: %s", error->message);
      export_unref (ex);
      return;
    }
  if (ex->plugin == NULL)
    {
      g_dbus_connection_close (connection, NULL, NULL, NULL);
      g_object_unref (connection);
      export_unref (ex);
      return;
    }
  ex->connection = connection;
  ex->root_registration = g_dbus_connection_register_object (connection, MPRIS_PATH, node_info->interfaces[0],
                                                             &vtable, ex, NULL, NULL);
  ex->player_registration = g_dbus_connection_register_object (connection, MPRIS_PATH, node_info->interfaces[1],
                                                               &vtable, ex, NULL, NULL);
  g_dbus_connection_call (connection, "org.freedesktop.DBus", "/org/freedesktop/DBus", "org.freedesktop.DBus",
                          "RequestName", g_variant_new ("(su)", ex->bus_name, 4), NULL,
                          G_DBUS_CALL_FLAGS_NONE, -1, NULL, NULL, NULL);
  send_request (ex, "requestNowPlaying", NULL);
  send_request (ex, "requestVolume", NULL);
  export_unref (ex);
}

static Export *
export_new (TandemMprisremotePlugin *self, const char *name)
{
  TandemDevice *device = tandem_plugin_get_device (TANDEM_PLUGIN (self));
  Export *ex = g_new0 (Export, 1);
  g_autofree char *device_part = sanitize (tandem_device_get_id (device));
  g_autofree char *player_part = sanitize (name);
  g_autofree char *address = g_dbus_address_get_for_bus_sync (G_BUS_TYPE_SESSION, NULL, NULL);

  ex->ref_count = 1;
  ex->plugin = self;
  ex->name = g_strdup (name);
  ex->bus_name = g_strdup_printf ("org.mpris.MediaPlayer2.tandem.%s.%s", device_part, player_part);
  ex->volume = 100;
  ex->loop_status = g_strdup ("None");
  if (address != NULL)
    g_dbus_connection_new_for_address (address,
                                       G_DBUS_CONNECTION_FLAGS_AUTHENTICATION_CLIENT |
                                       G_DBUS_CONNECTION_FLAGS_MESSAGE_BUS_CONNECTION,
                                       NULL, NULL, connection_ready_cb, export_ref (ex));
  return ex;
}

static void
update_player_list (TandemMprisremotePlugin *self, JsonArray *list)
{
  g_autoptr(GHashTable) wanted = g_hash_table_new (g_str_hash, g_str_equal);
  GHashTableIter iter;
  gpointer key;

  for (guint i = 0; list != NULL && i < json_array_get_length (list); i++)
    {
      JsonNode *node = json_array_get_element (list, i);

      if (JSON_NODE_HOLDS_VALUE (node) && json_node_get_value_type (node) == G_TYPE_STRING)
        g_hash_table_add (wanted, (gpointer) json_node_get_string (node));
    }

  g_hash_table_iter_init (&iter, self->exports);
  while (g_hash_table_iter_next (&iter, &key, NULL))
    {
      if (!g_hash_table_contains (wanted, key))
        g_hash_table_iter_remove (&iter);
    }
  g_hash_table_iter_init (&iter, wanted);
  while (g_hash_table_iter_next (&iter, &key, NULL))
    {
      if (!g_hash_table_contains (self->exports, key) && g_hash_table_size (self->exports) < MAX_EXPORTS)
        g_hash_table_insert (self->exports, g_strdup (key), export_new (self, key));
    }
}

static void
album_art_cb (GObject *source, GAsyncResult *result, gpointer user_data)
{
  Export *ex = user_data;
  g_autoptr(GError) error = NULL;

  if (tandem_transfer_download_to_file_finish (TANDEM_TRANSFER (source), result, &error) && ex->plugin != NULL)
    emit_properties_changed (ex);
  export_unref (ex);
  g_object_unref (source);
}

static void
update_now_playing (TandemMprisremotePlugin *self, Export *ex, TandemPacket *packet)
{
  const char *art_url = tandem_packet_get_string (packet, "albumArtUrl", NULL);

  if (tandem_packet_has (packet, "title"))
    {
      g_free (ex->title);
      ex->title = g_strdup (tandem_packet_get_string (packet, "title", ""));
    }
  else if (tandem_packet_has (packet, "nowPlaying"))
    {
      g_free (ex->title);
      ex->title = g_strdup (tandem_packet_get_string (packet, "nowPlaying", ""));
    }
  if (tandem_packet_has (packet, "artist"))
    {
      g_free (ex->artist);
      ex->artist = g_strdup (tandem_packet_get_string (packet, "artist", ""));
    }
  if (tandem_packet_has (packet, "album"))
    {
      g_free (ex->album);
      ex->album = g_strdup (tandem_packet_get_string (packet, "album", ""));
    }
  if (tandem_packet_has (packet, "isPlaying"))
    ex->playing = tandem_packet_get_boolean (packet, "isPlaying", FALSE);
  if (tandem_packet_has (packet, "pos"))
    {
      ex->pos_ms = tandem_packet_get_int (packet, "pos", 0);
      ex->pos_updated = g_get_monotonic_time ();
    }
  if (tandem_packet_has (packet, "length"))
    ex->length_ms = tandem_packet_get_int (packet, "length", 0);
  if (tandem_packet_has (packet, "volume"))
    ex->volume = (int) tandem_packet_get_int (packet, "volume", 100);
  if (tandem_packet_has (packet, "canPause"))
    ex->can_pause = tandem_packet_get_boolean (packet, "canPause", FALSE);
  if (tandem_packet_has (packet, "canPlay"))
    ex->can_play = tandem_packet_get_boolean (packet, "canPlay", FALSE);
  if (tandem_packet_has (packet, "canGoNext"))
    ex->can_next = tandem_packet_get_boolean (packet, "canGoNext", FALSE);
  if (tandem_packet_has (packet, "canGoPrevious"))
    ex->can_previous = tandem_packet_get_boolean (packet, "canGoPrevious", FALSE);
  if (tandem_packet_has (packet, "canSeek"))
    ex->can_seek = tandem_packet_get_boolean (packet, "canSeek", FALSE);
  if (tandem_packet_has (packet, "loopStatus"))
    {
      g_free (ex->loop_status);
      ex->loop_status = g_strdup (tandem_packet_get_string (packet, "loopStatus", "None"));
    }
  if (tandem_packet_has (packet, "shuffle"))
    ex->shuffle = tandem_packet_get_boolean (packet, "shuffle", FALSE);

  if (art_url != NULL && *art_url != '\0' && g_strcmp0 (art_url, ex->art_url) != 0)
    {
      g_autofree char *hash = g_compute_checksum_for_string (G_CHECKSUM_MD5, art_url, -1);
      g_autofree char *path = g_build_filename (self->art_dir, hash, NULL);

      g_free (ex->art_url);
      ex->art_url = g_strdup (art_url);
      g_clear_pointer (&ex->art_file, g_free);
      if (g_file_test (path, G_FILE_TEST_EXISTS))
        {
          ex->art_file = g_steal_pointer (&path);
        }
      else
        {
          g_autoptr(TandemPacket) request = tandem_packet_new ("kdeconnect.mpris.request");

          tandem_packet_set_string (request, "player", ex->name);
          tandem_packet_set_string (request, "albumArtUrl", art_url);
          tandem_plugin_send (TANDEM_PLUGIN (self), request);
        }
    }
  emit_properties_changed (ex);
}

static void
mprisremote_handle_packet (TandemPlugin *plugin, TandemPacket *packet)
{
  TandemMprisremotePlugin *self = TANDEM_MPRISREMOTE_PLUGIN (plugin);
  const char *name = tandem_packet_get_string (packet, "player", NULL);
  Export *ex;

  if (tandem_packet_has (packet, "playerList"))
    update_player_list (self, tandem_packet_get_array (packet, "playerList"));
  if (name == NULL)
    return;
  ex = g_hash_table_lookup (self->exports, name);
  if (ex == NULL)
    return;

  if (tandem_packet_get_boolean (packet, "transferringAlbumArt", FALSE))
    {
      const char *url = tandem_packet_get_string (packet, "albumArtUrl", "");
      g_autofree char *hash = g_compute_checksum_for_string (G_CHECKSUM_MD5, url, -1);
      g_autofree char *path = g_build_filename (self->art_dir, hash, NULL);
      TandemTransfer *transfer = tandem_device_download (tandem_plugin_get_device (plugin), packet);
      g_autoptr(GFile) file = g_file_new_for_path (path);

      if (transfer == NULL)
        return;
      g_mkdir_with_parents (self->art_dir, 0700);
      g_free (ex->art_file);
      ex->art_file = g_steal_pointer (&path);
      tandem_transfer_download_to_file_async (transfer, file, NULL, album_art_cb, export_ref (ex));
      return;
    }
  update_now_playing (self, ex, packet);
}

static void
mprisremote_connected (TandemPlugin *plugin)
{
  g_autoptr(TandemPacket) request = tandem_packet_new ("kdeconnect.mpris.request");

  tandem_packet_set_boolean (request, "requestPlayerList", TRUE);
  tandem_plugin_send (plugin, request);
}

static void
mprisremote_disconnected (TandemPlugin *plugin)
{
  g_hash_table_remove_all (TANDEM_MPRISREMOTE_PLUGIN (plugin)->exports);
}

static void
tandem_mprisremote_plugin_constructed (GObject *object)
{
  TandemMprisremotePlugin *self = TANDEM_MPRISREMOTE_PLUGIN (object);
  TandemDevice *device = tandem_plugin_get_device (TANDEM_PLUGIN (self));

  G_OBJECT_CLASS (tandem_mprisremote_plugin_parent_class)->constructed (object);
  self->art_dir = g_build_filename (g_get_user_cache_dir (), "tandem", tandem_device_get_id (device), "albumart", NULL);
  if (node_info == NULL)
    node_info = g_dbus_node_info_new_for_xml (root_xml, NULL);
}

static void
tandem_mprisremote_plugin_dispose (GObject *object)
{
  TandemMprisremotePlugin *self = TANDEM_MPRISREMOTE_PLUGIN (object);

  g_clear_pointer (&self->exports, g_hash_table_unref);
  G_OBJECT_CLASS (tandem_mprisremote_plugin_parent_class)->dispose (object);
}

static void
tandem_mprisremote_plugin_finalize (GObject *object)
{
  g_free (TANDEM_MPRISREMOTE_PLUGIN (object)->art_dir);
  G_OBJECT_CLASS (tandem_mprisremote_plugin_parent_class)->finalize (object);
}

static void
tandem_mprisremote_plugin_class_init (TandemMprisremotePluginClass *klass)
{
  TandemPluginClass *plugin_class = TANDEM_PLUGIN_CLASS (klass);
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->constructed = tandem_mprisremote_plugin_constructed;
  object_class->dispose = tandem_mprisremote_plugin_dispose;
  object_class->finalize = tandem_mprisremote_plugin_finalize;
  plugin_class->name = "mprisremote";
  plugin_class->title = "Remote media";
  plugin_class->incoming = mprisremote_incoming;
  plugin_class->outgoing = mprisremote_outgoing;
  plugin_class->handle_packet = mprisremote_handle_packet;
  plugin_class->connected = mprisremote_connected;
  plugin_class->disconnected = mprisremote_disconnected;
}

static void
tandem_mprisremote_plugin_init (TandemMprisremotePlugin *self)
{
  self->exports = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, (GDestroyNotify) export_detach);
}
