/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "util.h"

#include <glib/gstdio.h>
#include <string.h>

gboolean
tandem_device_id_is_valid (const char *id)
{
  gsize len;

  if (id == NULL)
    return FALSE;
  len = strlen (id);
  if (len < 32 || len > 38)
    return FALSE;
  for (gsize i = 0; i < len; i++)
    {
      char c = id[i];
      if (!g_ascii_isalnum (c) && c != '_' && c != '-')
        return FALSE;
    }
  return TRUE;
}

char *
tandem_device_id_generate (void)
{
  char *id = g_uuid_string_random ();

  for (char *p = id; *p != '\0'; p++)
    if (*p == '-')
      *p = '_';
  return id;
}

char *
tandem_object_path_for_id (const char *id)
{
  GString *path = g_string_new ("/de/flk/Tandem/devices/d_");

  for (const char *p = id; *p != '\0'; p++)
    g_string_append_c (path, g_ascii_isalnum (*p) ? *p : '_');
  return g_string_free (path, FALSE);
}

gboolean
tandem_ensure_dir (const char *path, GError **error)
{
  if (g_mkdir_with_parents (path, 0700) != 0)
    {
      int saved = errno;
      g_set_error (error, G_FILE_ERROR, g_file_error_from_errno (saved),
                   "Cannot create %s: %s", path, g_strerror (saved));
      return FALSE;
    }
  return TRUE;
}

char *
tandem_read_file (const char *path, GError **error)
{
  char *contents = NULL;

  if (!g_file_get_contents (path, &contents, NULL, error))
    return NULL;
  return contents;
}

gboolean
tandem_write_file (const char *path, const char *contents, gssize len, int mode, GError **error)
{
  g_autofree char *dir = g_path_get_dirname (path);

  if (!tandem_ensure_dir (dir, error))
    return FALSE;
  if (!g_file_set_contents_full (path, contents, len,
                                 G_FILE_SET_CONTENTS_CONSISTENT | G_FILE_SET_CONTENTS_ONLY_EXISTING,
                                 mode, error))
    return FALSE;
  return TRUE;
}

char *
tandem_unique_filename (const char *dir, const char *name)
{
  g_autofree char *base = NULL;
  const char *ext;
  char *candidate;

  candidate = g_build_filename (dir, name, NULL);
  if (!g_file_test (candidate, G_FILE_TEST_EXISTS))
    return candidate;
  g_free (candidate);

  ext = strrchr (name, '.');
  if (ext == name || ext == NULL)
    ext = "";
  base = g_strndup (name, strlen (name) - strlen (ext));

  for (int n = 1; n < 10000; n++)
    {
      g_autofree char *numbered = g_strdup_printf ("%s (%d)%s", base, n, ext);

      candidate = g_build_filename (dir, numbered, NULL);
      if (!g_file_test (candidate, G_FILE_TEST_EXISTS))
        return candidate;
      g_free (candidate);
    }
  return g_build_filename (dir, name, NULL);
}

gint64
tandem_now_ms (void)
{
  return g_get_real_time () / 1000;
}
