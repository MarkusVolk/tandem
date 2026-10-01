/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "cert.h"
#include "util.h"

#include <gnutls/abstract.h>
#include <gnutls/crypto.h>
#include <gnutls/gnutls.h>
#include <gnutls/x509.h>
#include <string.h>

#define YEAR_SECONDS ((time_t) 365 * 24 * 3600)

static gboolean
set_gnutls_error (GError **error, const char *what, int rc)
{
  g_set_error (error, G_TLS_ERROR, G_TLS_ERROR_MISC, "%s: %s", what, gnutls_strerror (rc));
  return FALSE;
}

gboolean
tandem_cert_generate (const char  *device_id,
                      const char  *cert_path,
                      const char  *key_path,
                      GError     **error)
{
  gnutls_x509_privkey_t key = NULL;
  gnutls_x509_crt_t crt = NULL;
  gnutls_datum_t cert_pem = { 0 };
  gnutls_datum_t key_pem = { 0 };
  unsigned char serial[16];
  time_t now = time (NULL);
  gboolean ok = FALSE;
  int rc;

  g_return_val_if_fail (tandem_device_id_is_valid (device_id), FALSE);

  rc = gnutls_x509_privkey_init (&key);
  if (rc < 0)
    return set_gnutls_error (error, "privkey_init", rc);
  rc = gnutls_x509_privkey_generate (key, GNUTLS_PK_ECDSA,
                                     GNUTLS_CURVE_TO_BITS (GNUTLS_ECC_CURVE_SECP256R1), 0);
  if (rc < 0)
    {
      set_gnutls_error (error, "privkey_generate", rc);
      goto out;
    }

  rc = gnutls_x509_crt_init (&crt);
  if (rc < 0)
    {
      set_gnutls_error (error, "crt_init", rc);
      goto out;
    }

  gnutls_rnd (GNUTLS_RND_NONCE, serial, sizeof serial);
  serial[0] &= 0x7f;

  if ((rc = gnutls_x509_crt_set_version (crt, 3)) < 0 ||
      (rc = gnutls_x509_crt_set_key (crt, key)) < 0 ||
      (rc = gnutls_x509_crt_set_serial (crt, serial, sizeof serial)) < 0 ||
      (rc = gnutls_x509_crt_set_activation_time (crt, now - YEAR_SECONDS)) < 0 ||
      (rc = gnutls_x509_crt_set_expiration_time (crt, now + 10 * YEAR_SECONDS)) < 0 ||
      (rc = gnutls_x509_crt_set_dn_by_oid (crt, GNUTLS_OID_X520_COMMON_NAME, 0,
                                           device_id, strlen (device_id))) < 0 ||
      (rc = gnutls_x509_crt_set_dn_by_oid (crt, GNUTLS_OID_X520_ORGANIZATION_NAME, 0,
                                           "flk", 3)) < 0 ||
      (rc = gnutls_x509_crt_set_dn_by_oid (crt, GNUTLS_OID_X520_ORGANIZATIONAL_UNIT_NAME, 0,
                                           "tandem", 6)) < 0)
    {
      set_gnutls_error (error, "crt_set", rc);
      goto out;
    }

  rc = gnutls_x509_crt_sign2 (crt, crt, key, GNUTLS_DIG_SHA256, 0);
  if (rc < 0)
    {
      set_gnutls_error (error, "crt_sign", rc);
      goto out;
    }

  rc = gnutls_x509_crt_export2 (crt, GNUTLS_X509_FMT_PEM, &cert_pem);
  if (rc < 0)
    {
      set_gnutls_error (error, "crt_export", rc);
      goto out;
    }
  rc = gnutls_x509_privkey_export2 (key, GNUTLS_X509_FMT_PEM, &key_pem);
  if (rc < 0)
    {
      set_gnutls_error (error, "privkey_export", rc);
      goto out;
    }

  if (!tandem_write_file (key_path, (const char *) key_pem.data, key_pem.size, 0600, error))
    goto out;
  if (!tandem_write_file (cert_path, (const char *) cert_pem.data, cert_pem.size, 0644, error))
    goto out;
  ok = TRUE;

out:
  gnutls_free (cert_pem.data);
  if (key_pem.data != NULL)
    {
      memset (key_pem.data, 0, key_pem.size);
      gnutls_free (key_pem.data);
    }
  if (crt != NULL)
    gnutls_x509_crt_deinit (crt);
  if (key != NULL)
    gnutls_x509_privkey_deinit (key);
  return ok;
}

static gnutls_x509_crt_t
import_certificate (GTlsCertificate *certificate)
{
  g_autoptr(GByteArray) der = NULL;
  gnutls_x509_crt_t crt;
  gnutls_datum_t datum;

  g_object_get (certificate, "certificate", &der, NULL);
  if (der == NULL)
    return NULL;
  if (gnutls_x509_crt_init (&crt) < 0)
    return NULL;
  datum.data = der->data;
  datum.size = der->len;
  if (gnutls_x509_crt_import (crt, &datum, GNUTLS_X509_FMT_DER) < 0)
    {
      gnutls_x509_crt_deinit (crt);
      return NULL;
    }
  return crt;
}

char *
tandem_cert_common_name (GTlsCertificate *certificate)
{
  gnutls_x509_crt_t crt = import_certificate (certificate);
  char buf[128];
  size_t size = sizeof buf;
  char *cn = NULL;

  if (crt == NULL)
    return NULL;
  if (gnutls_x509_crt_get_dn_by_oid (crt, GNUTLS_OID_X520_COMMON_NAME, 0, 0, buf, &size) == 0)
    cn = g_strndup (buf, size);
  gnutls_x509_crt_deinit (crt);
  return cn;
}

GBytes *
tandem_cert_public_key_der (GTlsCertificate *certificate)
{
  gnutls_x509_crt_t crt = import_certificate (certificate);
  gnutls_pubkey_t pubkey = NULL;
  gnutls_datum_t der = { 0 };
  GBytes *bytes = NULL;

  if (crt == NULL)
    return NULL;
  if (gnutls_pubkey_init (&pubkey) == 0 &&
      gnutls_pubkey_import_x509 (pubkey, crt, 0) == 0 &&
      gnutls_pubkey_export2 (pubkey, GNUTLS_X509_FMT_DER, &der) == 0)
    bytes = g_bytes_new (der.data, der.size);
  gnutls_free (der.data);
  if (pubkey != NULL)
    gnutls_pubkey_deinit (pubkey);
  gnutls_x509_crt_deinit (crt);
  return bytes;
}

static int
compare_der (GBytes *a, GBytes *b)
{
  gsize la, lb;
  const guint8 *da = g_bytes_get_data (a, &la);
  const guint8 *db = g_bytes_get_data (b, &lb);
  int cmp = memcmp (da, db, MIN (la, lb));

  if (cmp != 0)
    return cmp;
  return (la > lb) - (la < lb);
}

char *
tandem_cert_verification_key_from_der (GBytes *a, GBytes *b, gint64 timestamp)
{
  g_autoptr(GChecksum) checksum = g_checksum_new (G_CHECKSUM_SHA256);
  GBytes *first = a;
  GBytes *second = b;
  g_autofree char *upper = NULL;

  if (compare_der (a, b) < 0)
    {
      first = b;
      second = a;
    }
  g_checksum_update (checksum, g_bytes_get_data (first, NULL), g_bytes_get_size (first));
  g_checksum_update (checksum, g_bytes_get_data (second, NULL), g_bytes_get_size (second));
  if (timestamp > 0)
    {
      g_autofree char *ts = g_strdup_printf ("%" G_GINT64_FORMAT, timestamp);
      g_checksum_update (checksum, (const guchar *) ts, strlen (ts));
    }
  upper = g_ascii_strup (g_checksum_get_string (checksum), 8);
  return g_strndup (upper, 8);
}

char *
tandem_cert_verification_key (GTlsCertificate *a, GTlsCertificate *b, gint64 timestamp)
{
  g_autoptr(GBytes) da = tandem_cert_public_key_der (a);
  g_autoptr(GBytes) db = tandem_cert_public_key_der (b);

  if (da == NULL || db == NULL)
    return NULL;
  return tandem_cert_verification_key_from_der (da, db, timestamp);
}

gboolean
tandem_cert_describe_key (GTlsCertificate *certificate, char **algorithm, unsigned int *bits)
{
  gnutls_x509_crt_t crt = import_certificate (certificate);
  unsigned int key_bits = 0;
  int algo;

  if (crt == NULL)
    return FALSE;
  algo = gnutls_x509_crt_get_pk_algorithm (crt, &key_bits);
  gnutls_x509_crt_deinit (crt);
  if (algo < 0)
    return FALSE;
  if (algorithm != NULL)
    *algorithm = g_strdup (gnutls_pk_algorithm_get_name (algo));
  if (bits != NULL)
    *bits = key_bits;
  return TRUE;
}

gboolean
tandem_cert_validity (GTlsCertificate *certificate, gint64 *not_before, gint64 *not_after)
{
  gnutls_x509_crt_t crt = import_certificate (certificate);

  if (crt == NULL)
    return FALSE;
  if (not_before != NULL)
    *not_before = gnutls_x509_crt_get_activation_time (crt);
  if (not_after != NULL)
    *not_after = gnutls_x509_crt_get_expiration_time (crt);
  gnutls_x509_crt_deinit (crt);
  return TRUE;
}
