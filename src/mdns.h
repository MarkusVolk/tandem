/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once

#include <gio/gio.h>

#include "config.h"
#include "lan.h"

G_BEGIN_DECLS

#define TANDEM_TYPE_MDNS (tandem_mdns_get_type ())
G_DECLARE_FINAL_TYPE (TandemMdns, tandem_mdns, TANDEM, MDNS, GObject)

TandemMdns *tandem_mdns_new   (TandemConfig *config, TandemLan *lan);
void        tandem_mdns_start (TandemMdns *self);
void        tandem_mdns_stop  (TandemMdns *self);

G_END_DECLS
