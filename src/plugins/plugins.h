/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once

#include "../plugin.h"

G_BEGIN_DECLS

void     tandem_ping_plugin_send               (TandemPlugin *plugin, const char *message);
void     tandem_findmyphone_plugin_ring        (TandemPlugin *plugin);
void     tandem_lock_plugin_set_remote         (TandemPlugin *plugin, gboolean locked);
void     tandem_share_plugin_send_text         (TandemPlugin *plugin, const char *text);
void     tandem_share_plugin_send_url          (TandemPlugin *plugin, const char *url);
gboolean tandem_share_plugin_send_file         (TandemPlugin *plugin, GFile *file, guint number_of_files, gint64 total_size, GError **error);
void     tandem_share_plugin_send_files        (TandemPlugin *plugin, GListModel *files);
void     tandem_clipboard_plugin_send          (TandemPlugin *plugin, const char *text);
void     tandem_remoteinput_plugin_send_text   (TandemPlugin *plugin, const char *text);
void     tandem_remoteinput_plugin_send_special (TandemPlugin *plugin, int special);
void     tandem_runcommand_plugin_add          (TandemPlugin *plugin, const char *name, const char *command);
void     tandem_runcommand_plugin_remove       (TandemPlugin *plugin, const char *key);
GVariant *tandem_runcommand_plugin_list        (TandemPlugin *plugin);
void     tandem_runcommand_plugin_run          (TandemPlugin *plugin, const char *key);
GVariant *tandem_notifications_plugin_describe (TandemPlugin *plugin);
void     tandem_notifications_plugin_dismiss   (TandemPlugin *plugin, const char *id);

G_END_DECLS
