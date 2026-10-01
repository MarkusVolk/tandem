/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once

#include <gio/gio.h>

#include "config.h"

G_BEGIN_DECLS

#define TANDEM_PAYLOAD_PORT_MIN 1739
#define TANDEM_PAYLOAD_PORT_MAX 1764

#define TANDEM_TYPE_TRANSFER (tandem_transfer_get_type ())
G_DECLARE_FINAL_TYPE (TandemTransfer, tandem_transfer, TANDEM, TRANSFER, GObject)

TandemTransfer *tandem_transfer_new_download        (TandemConfig    *config,
                                                     GTlsCertificate *peer_certificate,
                                                     GInetAddress    *address,
                                                     guint16          port,
                                                     gint64           size);
void            tandem_transfer_download_to_file_async  (TandemTransfer      *self,
                                                         GFile               *file,
                                                         GCancellable        *cancellable,
                                                         GAsyncReadyCallback  callback,
                                                         gpointer             user_data);
gboolean        tandem_transfer_download_to_file_finish (TandemTransfer *self, GAsyncResult *result, GError **error);
void            tandem_transfer_download_to_bytes_async (TandemTransfer      *self,
                                                         GCancellable        *cancellable,
                                                         GAsyncReadyCallback  callback,
                                                         gpointer             user_data);
GBytes         *tandem_transfer_download_to_bytes_finish (TandemTransfer *self, GAsyncResult *result, GError **error);

TandemTransfer *tandem_transfer_new_upload          (TandemConfig    *config,
                                                     GTlsCertificate *peer_certificate,
                                                     GInputStream    *stream,
                                                     gint64           size);
gboolean        tandem_transfer_upload_start        (TandemTransfer *self, guint16 *port, GError **error);

gint64          tandem_transfer_get_size            (TandemTransfer *self);
gint64          tandem_transfer_get_done            (TandemTransfer *self);
void            tandem_transfer_cancel              (TandemTransfer *self);

G_END_DECLS
