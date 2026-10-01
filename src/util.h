/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once

#include <gio/gio.h>

G_BEGIN_DECLS

gboolean tandem_device_id_is_valid (const char *id);
char    *tandem_device_id_generate (void);
char    *tandem_object_path_for_id (const char *id);
gboolean tandem_ensure_dir         (const char *path, GError **error);
char    *tandem_read_file          (const char *path, GError **error);
gboolean tandem_write_file         (const char *path, const char *contents, gssize len, int mode, GError **error);
char    *tandem_unique_filename    (const char *dir, const char *name);
gint64   tandem_now_ms             (void);

G_END_DECLS
