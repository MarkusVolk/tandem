/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once

#include <gio/gio.h>

#include "config.h"
#include "device.h"
#include "lan.h"

G_BEGIN_DECLS

#define TANDEM_TYPE_MANAGER (tandem_manager_get_type ())
G_DECLARE_FINAL_TYPE (TandemManager, tandem_manager, TANDEM, MANAGER, GObject)

TandemManager *tandem_manager_new         (TandemConfig *config);
gboolean       tandem_manager_start       (TandemManager *self, GError **error);
void           tandem_manager_stop        (TandemManager *self);
GListModel    *tandem_manager_get_devices (TandemManager *self);
TandemDevice  *tandem_manager_find        (TandemManager *self, const char *id_or_name);
TandemLan     *tandem_manager_get_lan     (TandemManager *self);
void           tandem_manager_refresh     (TandemManager *self);

G_END_DECLS
