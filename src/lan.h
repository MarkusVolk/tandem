/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once

#include <gio/gio.h>

#include "config.h"
#include "link.h"
#include "packet.h"

G_BEGIN_DECLS

#define TANDEM_UDP_PORT 1716
#define TANDEM_TCP_PORT_MIN 1716
#define TANDEM_TCP_PORT_MAX 1764

#define TANDEM_TYPE_LAN (tandem_lan_get_type ())
G_DECLARE_FINAL_TYPE (TandemLan, tandem_lan, TANDEM, LAN, GObject)

TandemLan    *tandem_lan_new               (TandemConfig *config);
gboolean      tandem_lan_start             (TandemLan *self, GError **error);
void          tandem_lan_stop              (TandemLan *self);
int           tandem_lan_get_tcp_port      (TandemLan *self);
void          tandem_lan_set_capabilities  (TandemLan *self,
                                            const char *const *incoming,
                                            const char *const *outgoing);
TandemPacket *tandem_lan_build_identity    (TandemLan *self);
void          tandem_lan_broadcast         (TandemLan *self);
void          tandem_lan_send_identity_to  (TandemLan *self, GInetAddress *address);
void          tandem_lan_set_periodic      (TandemLan *self, gboolean enabled);

G_END_DECLS
