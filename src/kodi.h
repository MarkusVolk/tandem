/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once

#include <gio/gio.h>
#include <json-glib/json-glib.h>

G_BEGIN_DECLS

#define TANDEM_TYPE_KODI (tandem_kodi_get_type ())
G_DECLARE_FINAL_TYPE (TandemKodi, tandem_kodi, TANDEM, KODI, GObject)

typedef void (*TandemKodiReply) (JsonNode *result, gpointer user_data);

TandemKodi *tandem_kodi_get          (void);
gboolean    tandem_kodi_is_connected (TandemKodi *self);
void        tandem_kodi_call         (TandemKodi      *self,
                                      const char      *method,
                                      JsonNode        *params,
                                      TandemKodiReply  callback,
                                      gpointer         user_data);
void        tandem_kodi_open         (TandemKodi *self, const char *file_or_url);

G_END_DECLS
