/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once

#include <gio/gio.h>

#include "manager.h"

G_BEGIN_DECLS

#define TANDEM_MENU_PATH "/MenuBar"

#define TANDEM_TYPE_DBUS_MENU (tandem_dbus_menu_get_type ())
G_DECLARE_FINAL_TYPE (TandemDbusMenu, tandem_dbus_menu, TANDEM, DBUS_MENU, GObject)

TandemDbusMenu *tandem_dbus_menu_new (GDBusConnection *connection, TandemManager *manager);

G_END_DECLS
