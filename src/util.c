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

typedef struct
{
  gsize max;
  gsize length;
} LineRead;

static void line_scan (GTask *task);

static void
line_fill_cb (GObject *source, GAsyncResult *result, gpointer user_data)
{
  GTask *task = user_data;
  GError *error = NULL;
  gssize n = g_buffered_input_stream_fill_finish (G_BUFFERED_INPUT_STREAM (source), result, &error);

  if (n < 0)
    g_task_return_error (task, error);
  else if (n == 0)
    g_task_return_pointer (task, NULL, NULL);
  else
    {
      line_scan (task);
      return;
    }
  g_object_unref (task);
}

static void
line_scan (GTask *task)
{
  GBufferedInputStream *stream = g_task_get_source_object (task);
  LineRead *read = g_task_get_task_data (task);
  gsize available = 0;
  const char *buffer = g_buffered_input_stream_peek_buffer (stream, &available);
  const char *newline = memchr (buffer, '\n', available);

  if (newline != NULL)
    {
      gsize length = newline - buffer;
      char *line = g_malloc (length + 1);

      memcpy (line, buffer, length);
      line[length] = '\0';
      g_input_stream_skip (G_INPUT_STREAM (stream), length + 1, NULL, NULL);
      if (!g_utf8_validate_len (line, length, NULL))
        {
          g_free (line);
          g_task_return_new_error (task, G_IO_ERROR, G_IO_ERROR_INVALID_DATA, "Line is not valid UTF-8");
        }
      else
        {
          read->length = length;
          g_task_return_pointer (task, line, g_free);
        }
    }
  else if (available >= read->max)
    {
      g_task_return_new_error (task, G_IO_ERROR, G_IO_ERROR_MESSAGE_TOO_LARGE, "Line longer than %" G_GSIZE_FORMAT " bytes",
                               read->max);
    }
  else
    {
      g_buffered_input_stream_fill_async (stream, -1, G_PRIORITY_DEFAULT, g_task_get_cancellable (task),
                                          line_fill_cb, task);
      return;
    }
  g_object_unref (task);
}

void
tandem_read_line_async (GBufferedInputStream *stream,
                        gsize                 max,
                        GCancellable         *cancellable,
                        GAsyncReadyCallback   callback,
                        gpointer              user_data)
{
  GTask *task = g_task_new (stream, cancellable, callback, user_data);
  LineRead *read = g_new0 (LineRead, 1);

  read->max = max;
  g_task_set_task_data (task, read, g_free);
  if (g_buffered_input_stream_get_buffer_size (stream) < max)
    g_buffered_input_stream_set_buffer_size (stream, max);
  line_scan (task);
}

char *
tandem_read_line_finish (GBufferedInputStream *stream, GAsyncResult *result, gsize *length, GError **error)
{
  LineRead *read = g_task_get_task_data (G_TASK (result));
  char *line = g_task_propagate_pointer (G_TASK (result), error);

  if (length != NULL)
    *length = line != NULL ? read->length : 0;
  return line;
}
