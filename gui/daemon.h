/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once

#include <gio/gio.h>

G_BEGIN_DECLS

#define TANDEM_GUI_DAEMON_NAME "de.flk.Tandem.Daemon"
#define TANDEM_GUI_DAEMON_PATH "/de/flk/Tandem"
#define TANDEM_GUI_MANAGER_IFACE "de.flk.Tandem.Manager"
#define TANDEM_GUI_DEVICE_IFACE "de.flk.Tandem.Device"

#define TANDEM_TYPE_GUI_DEVICE (tandem_gui_device_get_type ())
G_DECLARE_FINAL_TYPE (TandemGuiDevice, tandem_gui_device, TANDEM, GUI_DEVICE, GObject)

#define TANDEM_TYPE_GUI_DAEMON (tandem_gui_daemon_get_type ())
G_DECLARE_FINAL_TYPE (TandemGuiDaemon, tandem_gui_daemon, TANDEM, GUI_DAEMON, GObject)

TandemGuiDaemon  *tandem_gui_daemon_get         (void);
GListModel       *tandem_gui_daemon_get_devices (TandemGuiDaemon *self);
TandemGuiDevice  *tandem_gui_daemon_find        (TandemGuiDaemon *self, const char *id);
void              tandem_gui_daemon_refresh     (TandemGuiDaemon *self);
char             *tandem_gui_daemon_self_id     (TandemGuiDaemon *self);
gboolean          tandem_gui_daemon_is_running  (TandemGuiDaemon *self);

const char       *tandem_gui_device_get_id          (TandemGuiDevice *self);
const char       *tandem_gui_device_get_name        (TandemGuiDevice *self);
const char       *tandem_gui_device_get_type_name   (TandemGuiDevice *self);
const char       *tandem_gui_device_get_state       (TandemGuiDevice *self);
char             *tandem_gui_device_get_key         (TandemGuiDevice *self);
gboolean          tandem_gui_device_is_paired       (TandemGuiDevice *self);
gboolean          tandem_gui_device_is_reachable    (TandemGuiDevice *self);
GStrv             tandem_gui_device_get_plugins     (TandemGuiDevice *self);
gboolean          tandem_gui_device_has_plugin      (TandemGuiDevice *self, const char *plugin);
GSettings        *tandem_gui_device_get_settings    (TandemGuiDevice *self);
void              tandem_gui_device_call            (TandemGuiDevice *self, const char *method, GVariant *parameters);
GVariant         *tandem_gui_device_call_sync       (TandemGuiDevice *self, const char *method, GVariant *parameters, GError **error);
GVariant         *tandem_gui_device_get_plugin_state (TandemGuiDevice *self, const char *plugin);
void              tandem_gui_device_call_plugin     (TandemGuiDevice *self, const char *plugin, const char *method, GVariant *args);
GVariant         *tandem_gui_device_call_plugin_sync (TandemGuiDevice *self, const char *plugin, const char *method, GVariant *args, GError **error);
GVariantBuilder  *tandem_gui_args                   (void);

G_END_DECLS
