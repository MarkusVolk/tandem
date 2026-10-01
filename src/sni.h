/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once

#include <gio/gio.h>

#include "manager.h"

G_BEGIN_DECLS

#define TANDEM_TYPE_SNI (tandem_sni_get_type ())
G_DECLARE_FINAL_TYPE (TandemSni, tandem_sni, TANDEM, SNI, GObject)

TandemSni *tandem_sni_new    (GDBusConnection *connection, TandemManager *manager);
void       tandem_sni_update (TandemSni *self);

G_END_DECLS
