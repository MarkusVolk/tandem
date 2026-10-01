/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "config.h"
#include "cert.h"
#include "util.h"

#include <string.h>

struct _TandemConfig
{
  GObject parent_instance;

  char *dir;
  char *device_id;
  GTlsCertificate *certificate;
  GSettings *settings;
};

G_DEFINE_FINAL_TYPE (TandemConfig, tandem_config, G_TYPE_OBJECT)

static TandemConfig *default_config;

static void
tandem_config_finalize (GObject *object)
{
  TandemConfig *self = TANDEM_CONFIG (object);

  g_free (self->dir);
  g_free (self->device_id);
  g_clear_object (&self->certificate);
  g_clear_object (&self->settings);

  G_OBJECT_CLASS (tandem_config_parent_class)->finalize (object);
}

static void
tandem_config_class_init (TandemConfigClass *klass)
{
  G_OBJECT_CLASS (klass)->finalize = tandem_config_finalize;
}

static void
tandem_config_init (TandemConfig *self)
{
}

static gboolean
load_or_create_device_id (TandemConfig *self, GError **error)
{
  g_autofree char *path = g_build_filename (self->dir, "device-id", NULL);
  g_autofree char *contents = NULL;

  if (g_file_test (path, G_FILE_TEST_EXISTS))
    {
      contents = tandem_read_file (path, error);
      if (contents == NULL)
        return FALSE;
      g_strstrip (contents);
      if (tandem_device_id_is_valid (contents))
        {
          self->device_id = g_steal_pointer (&contents);
          return TRUE;
        }
      g_warning ("Ignoring invalid device id in %s", path);
    }

  self->device_id = tandem_device_id_generate ();
  return tandem_write_file (path, self->device_id, -1, 0644, error);
}

static gboolean
load_or_create_certificate (TandemConfig *self, GError **error)
{
  g_autofree char *cert_path = g_build_filename (self->dir, "certificate.pem", NULL);
  g_autofree char *key_path = g_build_filename (self->dir, "privateKey.pem", NULL);
  g_autofree char *cn = NULL;

  if (g_file_test (cert_path, G_FILE_TEST_EXISTS) && g_file_test (key_path, G_FILE_TEST_EXISTS))
    {
      g_autoptr(GError) local_error = NULL;

      self->certificate = g_tls_certificate_new_from_files (cert_path, key_path, &local_error);
      if (self->certificate != NULL)
        {
          cn = tandem_cert_common_name (self->certificate);
          if (g_strcmp0 (cn, self->device_id) == 0)
            return TRUE;
          g_warning ("Certificate CN %s does not match the device id, creating a new one", cn);
          g_clear_object (&self->certificate);
        }
      else
        {
          g_warning ("Cannot load the certificate: %s", local_error->message);
        }
    }

  if (!tandem_cert_generate (self->device_id, cert_path, key_path, error))
    return FALSE;
  self->certificate = g_tls_certificate_new_from_files (cert_path, key_path, error);
  return self->certificate != NULL;
}

TandemConfig *
tandem_config_new (const char *config_dir, GError **error)
{
  g_autoptr(TandemConfig) self = g_object_new (TANDEM_TYPE_CONFIG, NULL);

  if (config_dir != NULL)
    self->dir = g_strdup (config_dir);
  else
    self->dir = g_build_filename (g_get_user_config_dir (), "tandem", NULL);

  if (!tandem_ensure_dir (self->dir, error))
    return NULL;
  if (!load_or_create_device_id (self, error))
    return NULL;
  if (!load_or_create_certificate (self, error))
    return NULL;

  return g_steal_pointer (&self);
}

TandemConfig *
tandem_config_get_default (GError **error)
{
  if (default_config == NULL)
    {
      default_config = tandem_config_new (NULL, error);
      if (default_config == NULL)
        return NULL;
      default_config->settings = g_settings_new ("de.flk.Tandem");
    }
  return g_object_ref (default_config);
}

const char *
tandem_config_get_dir (TandemConfig *self)
{
  return self->dir;
}

const char *
tandem_config_get_device_id (TandemConfig *self)
{
  return self->device_id;
}

char *
tandem_config_get_device_name (TandemConfig *self)
{
  if (self->settings != NULL)
    {
      g_autofree char *name = g_settings_get_string (self->settings, "device-name");

      if (name != NULL && *name != '\0')
        return g_steal_pointer (&name);
    }
  return g_strdup (g_get_host_name ());
}

const char *
tandem_config_get_device_type (TandemConfig *self)
{
  static char type[16];

  if (self->settings != NULL)
    {
      g_autofree char *configured = g_settings_get_string (self->settings, "device-type");

      if (configured != NULL && *configured != '\0')
        {
          g_strlcpy (type, configured, sizeof type);
          return type;
        }
    }
  return "desktop";
}

GTlsCertificate *
tandem_config_get_certificate (TandemConfig *self)
{
  return self->certificate;
}

GSettings *
tandem_config_get_settings (TandemConfig *self)
{
  return self->settings;
}

char *
tandem_config_trusted_dir (TandemConfig *self, const char *device_id)
{
  g_return_val_if_fail (tandem_device_id_is_valid (device_id), NULL);
  return g_build_filename (self->dir, "trusted", device_id, NULL);
}

GStrv
tandem_config_list_trusted (TandemConfig *self)
{
  g_autofree char *dir = g_build_filename (self->dir, "trusted", NULL);
  g_autoptr(GStrvBuilder) builder = g_strv_builder_new ();
  g_autoptr(GDir) handle = g_dir_open (dir, 0, NULL);
  const char *name;

  while (handle != NULL && (name = g_dir_read_name (handle)) != NULL)
    {
      g_autofree char *cert = g_build_filename (dir, name, "certificate.pem", NULL);

      if (tandem_device_id_is_valid (name) && g_file_test (cert, G_FILE_TEST_EXISTS))
        g_strv_builder_add (builder, name);
    }
  return g_strv_builder_end (builder);
}

gboolean
tandem_config_is_trusted (TandemConfig *self, const char *device_id)
{
  g_autofree char *dir = tandem_config_trusted_dir (self, device_id);
  g_autofree char *cert = dir != NULL ? g_build_filename (dir, "certificate.pem", NULL) : NULL;

  return cert != NULL && g_file_test (cert, G_FILE_TEST_EXISTS);
}

GTlsCertificate *
tandem_config_load_trusted (TandemConfig *self, const char *device_id, GKeyFile **info)
{
  g_autofree char *dir = tandem_config_trusted_dir (self, device_id);
  g_autofree char *cert_path = NULL;
  g_autofree char *info_path = NULL;
  GTlsCertificate *certificate;

  if (dir == NULL)
    return NULL;
  cert_path = g_build_filename (dir, "certificate.pem", NULL);
  certificate = g_tls_certificate_new_from_file (cert_path, NULL);
  if (certificate == NULL)
    return NULL;
  if (info != NULL)
    {
      *info = g_key_file_new ();
      info_path = g_build_filename (dir, "info.ini", NULL);
      g_key_file_load_from_file (*info, info_path, G_KEY_FILE_NONE, NULL);
    }
  return certificate;
}

gboolean
tandem_config_save_trusted (TandemConfig     *self,
                            const char       *device_id,
                            GTlsCertificate  *certificate,
                            GKeyFile         *info,
                            GError          **error)
{
  g_autofree char *dir = tandem_config_trusted_dir (self, device_id);
  g_autofree char *cert_path = NULL;
  g_autofree char *pem = NULL;

  if (dir == NULL)
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT, "Invalid device id");
      return FALSE;
    }
  g_object_get (certificate, "certificate-pem", &pem, NULL);
  cert_path = g_build_filename (dir, "certificate.pem", NULL);
  if (!tandem_write_file (cert_path, pem, -1, 0600, error))
    return FALSE;
  return tandem_config_update_trusted (self, device_id, info, error);
}

gboolean
tandem_config_update_trusted (TandemConfig *self, const char *device_id, GKeyFile *info, GError **error)
{
  g_autofree char *dir = tandem_config_trusted_dir (self, device_id);
  g_autofree char *info_path = NULL;
  g_autofree char *data = NULL;
  gsize len;

  if (dir == NULL || info == NULL)
    return TRUE;
  info_path = g_build_filename (dir, "info.ini", NULL);
  data = g_key_file_to_data (info, &len, error);
  if (data == NULL)
    return FALSE;
  return tandem_write_file (info_path, data, len, 0600, error);
}

void
tandem_config_remove_trusted (TandemConfig *self, const char *device_id)
{
  g_autofree char *dir = tandem_config_trusted_dir (self, device_id);
  g_autoptr(GFile) file = NULL;
  g_autoptr(GFileEnumerator) children = NULL;
  GFileInfo *child;

  if (dir == NULL)
    return;
  file = g_file_new_for_path (dir);
  children = g_file_enumerate_children (file, G_FILE_ATTRIBUTE_STANDARD_NAME,
                                        G_FILE_QUERY_INFO_NOFOLLOW_SYMLINKS, NULL, NULL);
  while (children != NULL && (child = g_file_enumerator_next_file (children, NULL, NULL)) != NULL)
    {
      g_autoptr(GFile) entry = g_file_get_child (file, g_file_info_get_name (child));

      g_file_delete (entry, NULL, NULL);
      g_object_unref (child);
    }
  g_file_delete (file, NULL, NULL);
}
