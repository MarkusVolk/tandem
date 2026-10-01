/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once

#include <gio/gio.h>

#include "packet.h"

G_BEGIN_DECLS

#define TANDEM_MAX_PACKET_SIZE (32 * 1024 * 1024)

#define TANDEM_TYPE_LINK (tandem_link_get_type ())
G_DECLARE_FINAL_TYPE (TandemLink, tandem_link, TANDEM, LINK, GObject)

TandemLink      *tandem_link_new                  (GIOStream        *tls_stream,
                                                   GSocketConnection *connection,
                                                   GDataInputStream *reader,
                                                   TandemPacket     *identity);
const char      *tandem_link_get_device_id        (TandemLink *self);
TandemPacket    *tandem_link_get_identity         (TandemLink *self);
GTlsCertificate *tandem_link_get_peer_certificate (TandemLink *self);
GInetAddress    *tandem_link_get_remote_address   (TandemLink *self);
gboolean         tandem_link_is_open              (TandemLink *self);
void             tandem_link_send                 (TandemLink *self, TandemPacket *packet);
void             tandem_link_close                (TandemLink *self);

G_END_DECLS
