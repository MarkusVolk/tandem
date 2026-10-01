/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once

#include <gio/gio.h>

G_BEGIN_DECLS

#define TANDEM_TYPE_MPRIS_WATCH (tandem_mpris_watch_get_type ())
G_DECLARE_FINAL_TYPE (TandemMprisWatch, tandem_mpris_watch, TANDEM, MPRIS_WATCH, GObject)

typedef struct
{
  char *bus_name;
  char *key;
  GDBusProxy *root;
  GDBusProxy *player;
} TandemMprisPlayer;

TandemMprisWatch  *tandem_mpris_watch_get        (void);
GList             *tandem_mpris_watch_list       (TandemMprisWatch *self);
TandemMprisPlayer *tandem_mpris_watch_lookup     (TandemMprisWatch *self, const char *key);

const char *tandem_mpris_player_get_string    (TandemMprisPlayer *player, const char *property, const char *fallback);
gboolean    tandem_mpris_player_get_boolean   (TandemMprisPlayer *player, const char *property, gboolean fallback);
double      tandem_mpris_player_get_double    (TandemMprisPlayer *player, const char *property, double fallback);
GVariant   *tandem_mpris_player_get_metadata  (TandemMprisPlayer *player);
gint64      tandem_mpris_player_get_position  (TandemMprisPlayer *player);
void        tandem_mpris_player_call          (TandemMprisPlayer *player, const char *method, GVariant *parameters);
void        tandem_mpris_player_set_property  (TandemMprisPlayer *player, const char *property, GVariant *value);

G_END_DECLS
