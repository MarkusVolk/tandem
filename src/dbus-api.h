/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once

#include <gio/gio.h>

G_BEGIN_DECLS

#define TANDEM_DBUS_NAME "de.flk.Tandem"
#define TANDEM_DBUS_PATH "/de/flk/Tandem"
#define TANDEM_DBUS_MANAGER_IFACE "de.flk.Tandem.Manager"
#define TANDEM_DBUS_DEVICE_IFACE "de.flk.Tandem.Device"

typedef struct _TandemApp TandemApp;
typedef struct _TandemDevice TandemDevice;

gboolean tandem_dbus_api_register       (GDBusConnection *connection, TandemApp *app, GError **error);
void     tandem_dbus_api_unregister     (GDBusConnection *connection);
void     tandem_dbus_api_notify_device  (const char *signal_name, const char *device_id);
void     tandem_dbus_api_add_device     (TandemDevice *device);
void     tandem_dbus_api_remove_device  (TandemDevice *device);
void     tandem_dbus_api_device_changed (TandemDevice *device);

G_END_DECLS
