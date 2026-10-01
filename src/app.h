/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once

#include <adwaita.h>

#include "config.h"
#include "manager.h"

G_BEGIN_DECLS

#define TANDEM_TYPE_APP (tandem_app_get_type ())
G_DECLARE_FINAL_TYPE (TandemApp, tandem_app, TANDEM, APP, AdwApplication)

TandemApp     *tandem_app_new              (void);
TandemConfig  *tandem_app_get_config       (TandemApp *self);
TandemManager *tandem_app_get_manager      (TandemApp *self);
void           tandem_app_show_window      (TandemApp *self);
void           tandem_app_quit             (TandemApp *self);
void           tandem_app_refresh          (TandemApp *self);
void           tandem_app_describe_devices (TandemApp *self, GVariantBuilder *builder);

G_END_DECLS
