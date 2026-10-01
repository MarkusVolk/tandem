/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "mpris-watch.h"

#include <string.h>

#define MPRIS_PREFIX "org.mpris.MediaPlayer2."
#define MPRIS_PATH "/org/mpris/MediaPlayer2"
#define MPRIS_ROOT_IFACE "org.mpris.MediaPlayer2"
#define MPRIS_PLAYER_IFACE "org.mpris.MediaPlayer2.Player"
#define OWN_PREFIX "org.mpris.MediaPlayer2.tandem."

struct _TandemMprisWatch
{
  GObject parent_instance;

  GDBusConnection *bus;
  GHashTable *players;
  guint owner_subscription;
};

enum {
  PLAYER_ADDED,
  PLAYER_REMOVED,
  PLAYER_CHANGED,
  N_SIGNALS
};

static guint signals[N_SIGNALS];

G_DEFINE_FINAL_TYPE (TandemMprisWatch, tandem_mpris_watch, G_TYPE_OBJECT)

static void
player_free (TandemMprisPlayer *player)
{
  g_free (player->bus_name);
  g_free (player->key);
  g_clear_object (&player->root);
  g_clear_object (&player->player);
  g_free (player);
}

static void
tandem_mpris_watch_dispose (GObject *object)
{
  TandemMprisWatch *self = TANDEM_MPRIS_WATCH (object);

  if (self->owner_subscription != 0)
    g_dbus_connection_signal_unsubscribe (self->bus, self->owner_subscription);
  self->owner_subscription = 0;
  g_clear_pointer (&self->players, g_hash_table_unref);
  g_clear_object (&self->bus);
  G_OBJECT_CLASS (tandem_mpris_watch_parent_class)->dispose (object);
}

static void
tandem_mpris_watch_class_init (TandemMprisWatchClass *klass)
{
  G_OBJECT_CLASS (klass)->dispose = tandem_mpris_watch_dispose;

  signals[PLAYER_ADDED] = g_signal_new ("player-added", TANDEM_TYPE_MPRIS_WATCH, G_SIGNAL_RUN_LAST, 0,
                                        NULL, NULL, NULL, G_TYPE_NONE, 1, G_TYPE_STRING);
  signals[PLAYER_REMOVED] = g_signal_new ("player-removed", TANDEM_TYPE_MPRIS_WATCH, G_SIGNAL_RUN_LAST, 0,
                                          NULL, NULL, NULL, G_TYPE_NONE, 1, G_TYPE_STRING);
  signals[PLAYER_CHANGED] = g_signal_new ("player-changed", TANDEM_TYPE_MPRIS_WATCH, G_SIGNAL_RUN_LAST, 0,
                                          NULL, NULL, NULL, G_TYPE_NONE, 1, G_TYPE_STRING);
}

static void
tandem_mpris_watch_init (TandemMprisWatch *self)
{
  self->players = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, (GDestroyNotify) player_free);
}

static TandemMprisPlayer *
find_by_bus_name (TandemMprisWatch *self, const char *bus_name)
{
  GHashTableIter iter;
  gpointer value;

  g_hash_table_iter_init (&iter, self->players);
  while (g_hash_table_iter_next (&iter, NULL, &value))
    {
      TandemMprisPlayer *player = value;

      if (g_strcmp0 (player->bus_name, bus_name) == 0)
        return player;
    }
  return NULL;
}

static void
player_properties_changed_cb (GDBusProxy *proxy, GVariant *changed, GStrv invalidated, gpointer user_data)
{
  TandemMprisWatch *self = tandem_mpris_watch_get ();
  TandemMprisPlayer *player = user_data;

  if (g_hash_table_lookup (self->players, player->key) == player)
    g_signal_emit (self, signals[PLAYER_CHANGED], 0, player->key);
}

static void
player_signal_cb (GDBusProxy *proxy, const char *sender, const char *signal_name, GVariant *parameters, gpointer user_data)
{
  if (g_str_equal (signal_name, "Seeked"))
    player_properties_changed_cb (proxy, NULL, NULL, user_data);
}

typedef struct
{
  TandemMprisWatch *watch;
  char *bus_name;
  GDBusProxy *root;
} Pending;

static void
player_proxy_cb (GObject *source, GAsyncResult *result, gpointer user_data)
{
  Pending *pending = user_data;
  TandemMprisWatch *self = pending->watch;
  g_autoptr(GError) error = NULL;
  GDBusProxy *player_proxy = g_dbus_proxy_new_for_bus_finish (result, &error);
  g_autoptr(GVariant) identity = NULL;
  g_autofree char *key = NULL;
  TandemMprisPlayer *player;
  int n = 2;

  if (player_proxy == NULL || find_by_bus_name (self, pending->bus_name) != NULL)
    {
      g_clear_object (&player_proxy);
      goto out;
    }
  identity = g_dbus_proxy_get_cached_property (pending->root, "Identity");
  key = g_strdup (identity != NULL ? g_variant_get_string (identity, NULL) : pending->bus_name + strlen (MPRIS_PREFIX));
  while (g_hash_table_contains (self->players, key))
    {
      g_autofree char *base = g_steal_pointer (&key);

      key = g_strdup_printf ("%s [%d]", base, n++);
    }

  player = g_new0 (TandemMprisPlayer, 1);
  player->bus_name = g_strdup (pending->bus_name);
  player->key = g_strdup (key);
  player->root = g_object_ref (pending->root);
  player->player = player_proxy;
  g_signal_connect (player->player, "g-properties-changed", G_CALLBACK (player_properties_changed_cb), player);
  g_signal_connect (player->player, "g-signal", G_CALLBACK (player_signal_cb), player);
  g_hash_table_insert (self->players, g_strdup (key), player);
  g_debug ("mpris: player %s (%s)", key, pending->bus_name);
  g_signal_emit (self, signals[PLAYER_ADDED], 0, key);

out:
  g_clear_object (&pending->root);
  g_free (pending->bus_name);
  g_object_unref (pending->watch);
  g_free (pending);
}

static void
root_proxy_cb (GObject *source, GAsyncResult *result, gpointer user_data)
{
  Pending *pending = user_data;
  g_autoptr(GError) error = NULL;

  pending->root = g_dbus_proxy_new_for_bus_finish (result, &error);
  if (pending->root == NULL)
    {
      g_free (pending->bus_name);
      g_object_unref (pending->watch);
      g_free (pending);
      return;
    }
  g_dbus_proxy_new_for_bus (G_BUS_TYPE_SESSION, G_DBUS_PROXY_FLAGS_NONE, NULL, pending->bus_name, MPRIS_PATH,
                            MPRIS_PLAYER_IFACE, NULL, player_proxy_cb, pending);
}

static void
add_player (TandemMprisWatch *self, const char *bus_name)
{
  Pending *pending;

  if (!g_str_has_prefix (bus_name, MPRIS_PREFIX) || g_str_has_prefix (bus_name, OWN_PREFIX))
    return;
  if (find_by_bus_name (self, bus_name) != NULL)
    return;
  pending = g_new0 (Pending, 1);
  pending->watch = g_object_ref (self);
  pending->bus_name = g_strdup (bus_name);
  g_dbus_proxy_new_for_bus (G_BUS_TYPE_SESSION, G_DBUS_PROXY_FLAGS_NONE, NULL, bus_name, MPRIS_PATH,
                            MPRIS_ROOT_IFACE, NULL, root_proxy_cb, pending);
}

static void
remove_player (TandemMprisWatch *self, const char *bus_name)
{
  TandemMprisPlayer *player = find_by_bus_name (self, bus_name);
  g_autofree char *key = NULL;

  if (player == NULL)
    return;
  key = g_strdup (player->key);
  g_signal_handlers_disconnect_by_data (player->player, player);
  g_hash_table_remove (self->players, key);
  g_debug ("mpris: player %s gone", key);
  g_signal_emit (self, signals[PLAYER_REMOVED], 0, key);
}

static void
owner_changed_cb (GDBusConnection *connection,
                  const char      *sender,
                  const char      *object_path,
                  const char      *interface_name,
                  const char      *signal_name,
                  GVariant        *parameters,
                  gpointer         user_data)
{
  TandemMprisWatch *self = user_data;
  const char *name, *old_owner, *new_owner;

  g_variant_get (parameters, "(&s&s&s)", &name, &old_owner, &new_owner);
  if (*new_owner == '\0')
    remove_player (self, name);
  else if (*old_owner == '\0')
    add_player (self, name);
}

static void
list_names_cb (GObject *source, GAsyncResult *result, gpointer user_data)
{
  g_autoptr(TandemMprisWatch) self = user_data;
  g_autoptr(GVariant) reply = g_dbus_connection_call_finish (G_DBUS_CONNECTION (source), result, NULL);
  g_autoptr(GVariantIter) iter = NULL;
  const char *name;

  if (reply == NULL)
    return;
  g_variant_get (reply, "(as)", &iter);
  while (g_variant_iter_loop (iter, "&s", &name))
    add_player (self, name);
}

TandemMprisWatch *
tandem_mpris_watch_get (void)
{
  static TandemMprisWatch *singleton;

  if (singleton == NULL)
    {
      singleton = g_object_new (TANDEM_TYPE_MPRIS_WATCH, NULL);
      singleton->bus = g_bus_get_sync (G_BUS_TYPE_SESSION, NULL, NULL);
      if (singleton->bus != NULL)
        {
          singleton->owner_subscription =
            g_dbus_connection_signal_subscribe (singleton->bus, "org.freedesktop.DBus", "org.freedesktop.DBus",
                                                "NameOwnerChanged", "/org/freedesktop/DBus", MPRIS_PREFIX,
                                                G_DBUS_SIGNAL_FLAGS_MATCH_ARG0_NAMESPACE, owner_changed_cb,
                                                singleton, NULL);
          g_dbus_connection_call (singleton->bus, "org.freedesktop.DBus", "/org/freedesktop/DBus",
                                  "org.freedesktop.DBus", "ListNames", NULL, G_VARIANT_TYPE ("(as)"),
                                  G_DBUS_CALL_FLAGS_NONE, -1, NULL, list_names_cb, g_object_ref (singleton));
        }
    }
  return singleton;
}

GList *
tandem_mpris_watch_list (TandemMprisWatch *self)
{
  return g_hash_table_get_keys (self->players);
}

TandemMprisPlayer *
tandem_mpris_watch_lookup (TandemMprisWatch *self, const char *key)
{
  return key != NULL ? g_hash_table_lookup (self->players, key) : NULL;
}

const char *
tandem_mpris_player_get_string (TandemMprisPlayer *player, const char *property, const char *fallback)
{
  g_autoptr(GVariant) value = g_dbus_proxy_get_cached_property (player->player, property);

  if (value == NULL || !g_variant_is_of_type (value, G_VARIANT_TYPE_STRING))
    return fallback;
  return g_intern_string (g_variant_get_string (value, NULL));
}

gboolean
tandem_mpris_player_get_boolean (TandemMprisPlayer *player, const char *property, gboolean fallback)
{
  g_autoptr(GVariant) value = g_dbus_proxy_get_cached_property (player->player, property);

  if (value == NULL || !g_variant_is_of_type (value, G_VARIANT_TYPE_BOOLEAN))
    return fallback;
  return g_variant_get_boolean (value);
}

double
tandem_mpris_player_get_double (TandemMprisPlayer *player, const char *property, double fallback)
{
  g_autoptr(GVariant) value = g_dbus_proxy_get_cached_property (player->player, property);

  if (value == NULL || !g_variant_is_of_type (value, G_VARIANT_TYPE_DOUBLE))
    return fallback;
  return g_variant_get_double (value);
}

GVariant *
tandem_mpris_player_get_metadata (TandemMprisPlayer *player)
{
  return g_dbus_proxy_get_cached_property (player->player, "Metadata");
}

gint64
tandem_mpris_player_get_position (TandemMprisPlayer *player)
{
  g_autoptr(GVariant) reply = NULL;
  g_autoptr(GVariant) value = NULL;

  reply = g_dbus_connection_call_sync (g_dbus_proxy_get_connection (player->player), player->bus_name, MPRIS_PATH,
                                       "org.freedesktop.DBus.Properties", "Get",
                                       g_variant_new ("(ss)", MPRIS_PLAYER_IFACE, "Position"),
                                       G_VARIANT_TYPE ("(v)"), G_DBUS_CALL_FLAGS_NONE, 500, NULL, NULL);
  if (reply == NULL)
    return 0;
  g_variant_get (reply, "(v)", &value);
  return g_variant_is_of_type (value, G_VARIANT_TYPE_INT64) ? g_variant_get_int64 (value) : 0;
}

void
tandem_mpris_player_call (TandemMprisPlayer *player, const char *method, GVariant *parameters)
{
  g_dbus_proxy_call (player->player, method, parameters, G_DBUS_CALL_FLAGS_NONE, 2000, NULL, NULL, NULL);
}

void
tandem_mpris_player_set_property (TandemMprisPlayer *player, const char *property, GVariant *value)
{
  g_dbus_connection_call (g_dbus_proxy_get_connection (player->player), player->bus_name, MPRIS_PATH,
                          "org.freedesktop.DBus.Properties", "Set",
                          g_variant_new ("(ssv)", MPRIS_PLAYER_IFACE, property, value), NULL,
                          G_DBUS_CALL_FLAGS_NONE, 2000, NULL, NULL, NULL);
}
