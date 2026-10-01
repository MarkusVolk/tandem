/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once

#include <gio/gio.h>

G_BEGIN_DECLS

gboolean tandem_cert_generate         (const char *device_id,
                                       const char *cert_path,
                                       const char *key_path,
                                       GError    **error);
char    *tandem_cert_common_name      (GTlsCertificate *certificate);
GBytes  *tandem_cert_public_key_der   (GTlsCertificate *certificate);
char    *tandem_cert_verification_key (GTlsCertificate *a,
                                       GTlsCertificate *b,
                                       gint64           timestamp);
char    *tandem_cert_verification_key_from_der (GBytes *a,
                                                GBytes *b,
                                                gint64  timestamp);
gboolean tandem_cert_describe_key     (GTlsCertificate *certificate,
                                       char           **algorithm,
                                       unsigned int    *bits);
gboolean tandem_cert_validity         (GTlsCertificate *certificate,
                                       gint64          *not_before,
                                       gint64          *not_after);

G_END_DECLS
