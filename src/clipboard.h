/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once

#include <gio/gio.h>

G_BEGIN_DECLS

#define TANDEM_TYPE_CLIPBOARD (tandem_clipboard_get_type ())
G_DECLARE_FINAL_TYPE (TandemClipboard, tandem_clipboard, TANDEM, CLIPBOARD, GObject)

TandemClipboard *tandem_clipboard_get          (void);
const char      *tandem_clipboard_get_text     (TandemClipboard *self);
gint64           tandem_clipboard_get_timestamp (TandemClipboard *self);
void             tandem_clipboard_set_text     (TandemClipboard *self, const char *text);
const char      *tandem_clipboard_backend_name (TandemClipboard *self);

G_END_DECLS
