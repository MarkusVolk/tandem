/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once

#include <gio/gio.h>

#include "config.h"
#include "link.h"
#include "packet.h"

G_BEGIN_DECLS

typedef TandemPacket *(*TandemIdentityFunc) (gpointer user_data);

void        tandem_handshake_connect_async (TandemConfig        *config,
                                            GSocketAddress      *address,
                                            TandemPacket        *udp_identity,
                                            TandemIdentityFunc   identity_func,
                                            gpointer             identity_data,
                                            GCancellable        *cancellable,
                                            GAsyncReadyCallback  callback,
                                            gpointer             user_data);

void        tandem_handshake_accept_async  (TandemConfig        *config,
                                            GSocketConnection   *connection,
                                            TandemIdentityFunc   identity_func,
                                            gpointer             identity_data,
                                            GCancellable        *cancellable,
                                            GAsyncReadyCallback  callback,
                                            gpointer             user_data);

TandemLink *tandem_handshake_finish        (GAsyncResult *result, GError **error);

gboolean    tandem_identity_is_valid       (TandemPacket *packet);
void        tandem_configure_socket        (GSocket *socket);

G_END_DECLS
