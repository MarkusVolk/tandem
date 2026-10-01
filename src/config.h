/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once

#include <gio/gio.h>

G_BEGIN_DECLS

#define TANDEM_TYPE_CONFIG (tandem_config_get_type ())
G_DECLARE_FINAL_TYPE (TandemConfig, tandem_config, TANDEM, CONFIG, GObject)

TandemConfig    *tandem_config_new              (const char *config_dir, GError **error);
TandemConfig    *tandem_config_get_default      (GError **error);
const char      *tandem_config_get_dir          (TandemConfig *self);
const char      *tandem_config_get_device_id    (TandemConfig *self);
char            *tandem_config_get_device_name  (TandemConfig *self);
const char      *tandem_config_get_device_type  (TandemConfig *self);
GTlsCertificate *tandem_config_get_certificate  (TandemConfig *self);
GSettings       *tandem_config_get_settings     (TandemConfig *self);

char            *tandem_config_trusted_dir      (TandemConfig *self, const char *device_id);
GStrv            tandem_config_list_trusted     (TandemConfig *self);
gboolean         tandem_config_is_trusted       (TandemConfig *self, const char *device_id);
GTlsCertificate *tandem_config_load_trusted     (TandemConfig *self, const char *device_id, GKeyFile **info);
gboolean         tandem_config_save_trusted     (TandemConfig *self, const char *device_id,
                                                 GTlsCertificate *certificate, GKeyFile *info, GError **error);
gboolean         tandem_config_update_trusted   (TandemConfig *self, const char *device_id, GKeyFile *info, GError **error);
void             tandem_config_remove_trusted   (TandemConfig *self, const char *device_id);

G_END_DECLS
