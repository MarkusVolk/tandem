/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "vcard.h"

#include <string.h>

void
tandem_contact_free (TandemContact *contact)
{
  if (contact == NULL)
    return;
  g_free (contact->uid);
  g_free (contact->name);
  g_ptr_array_unref (contact->numbers);
  g_ptr_array_unref (contact->emails);
  g_clear_pointer (&contact->photo, g_bytes_unref);
  g_free (contact);
}

char *
tandem_phone_number_key (const char *number)
{
  GString *digits = g_string_new (NULL);
  gsize len;

  for (const char *p = number != NULL ? number : ""; *p != '\0'; p++)
    if (g_ascii_isdigit (*p))
      g_string_append_c (digits, *p);
  len = digits->len;
  if (len > 9)
    g_string_erase (digits, 0, len - 9);
  return g_string_free (digits, FALSE);
}

static char *
unescape (const char *value)
{
  GString *out = g_string_new (NULL);

  for (const char *p = value; *p != '\0'; p++)
    {
      if (*p == '\\' && p[1] != '\0')
        {
          p++;
          if (*p == 'n' || *p == 'N')
            g_string_append_c (out, '\n');
          else
            g_string_append_c (out, *p);
        }
      else
        g_string_append_c (out, *p);
    }
  return g_string_free (out, FALSE);
}

static char *
name_from_n (const char *value)
{
  g_auto(GStrv) parts = g_strsplit (value, ";", -1);
  g_autofree char *family = parts[0] != NULL ? unescape (parts[0]) : g_strdup ("");
  g_autofree char *given = parts[0] != NULL && parts[1] != NULL ? unescape (parts[1]) : g_strdup ("");

  if (*given != '\0' && *family != '\0')
    return g_strdup_printf ("%s %s", given, family);
  return g_strdup (*given != '\0' ? given : family);
}

TandemContact *
tandem_vcard_parse (const char *text)
{
  TandemContact *contact = g_new0 (TandemContact, 1);
  g_auto(GStrv) raw_lines = NULL;
  g_autoptr(GPtrArray) lines = g_ptr_array_new_with_free_func (g_free);
  g_autofree char *fn = NULL;
  g_autofree char *n = NULL;
  GString *photo = NULL;
  gboolean photo_is_base64 = FALSE;

  contact->numbers = g_ptr_array_new_with_free_func (g_free);
  contact->emails = g_ptr_array_new_with_free_func (g_free);

  raw_lines = g_strsplit (text, "\n", -1);
  for (char **l = raw_lines; *l != NULL; l++)
    {
      char *line = *l;
      gsize len = strlen (line);

      if (len > 0 && line[len - 1] == '\r')
        line[len - 1] = '\0';
      if ((line[0] == ' ' || line[0] == '\t') && lines->len > 0)
        {
          char *previous = g_ptr_array_index (lines, lines->len - 1);
          char *joined = g_strconcat (previous, line + 1, NULL);

          g_ptr_array_remove_index (lines, lines->len - 1);
          g_ptr_array_add (lines, joined);
        }
      else if (photo != NULL && photo_is_base64 && line[0] != '\0' && strchr (line, ':') == NULL)
        {
          g_string_append (photo, line);
        }
      else
        {
          g_ptr_array_add (lines, g_strdup (line));
        }
    }

  for (guint i = 0; i < lines->len; i++)
    {
      const char *line = g_ptr_array_index (lines, i);
      const char *colon = strchr (line, ':');
      g_autofree char *field = NULL;
      g_autofree char *upper = NULL;
      g_auto(GStrv) params = NULL;
      const char *value;

      if (colon == NULL)
        continue;
      field = g_strndup (line, colon - line);
      value = colon + 1;
      params = g_strsplit (field, ";", -1);
      upper = g_ascii_strup (params[0], -1);
      if (g_str_has_prefix (upper, "ITEM") && strchr (upper, '.') != NULL)
        {
          g_autofree char *tmp = g_strdup (strchr (upper, '.') + 1);

          g_free (upper);
          upper = g_steal_pointer (&tmp);
        }

      if (g_str_equal (upper, "UID"))
        contact->uid = unescape (value);
      else if (g_str_equal (upper, "FN"))
        fn = unescape (value);
      else if (g_str_equal (upper, "N"))
        n = name_from_n (value);
      else if (g_str_equal (upper, "TEL"))
        g_ptr_array_add (contact->numbers, unescape (value));
      else if (g_str_equal (upper, "EMAIL"))
        g_ptr_array_add (contact->emails, unescape (value));
      else if (g_str_equal (upper, "X-KDECONNECT-TIMESTAMP"))
        contact->timestamp = g_ascii_strtoll (value, NULL, 10);
      else if (g_str_equal (upper, "PHOTO"))
        {
          gboolean base64 = FALSE;

          for (char **p = params + 1; *p != NULL; p++)
            {
              g_autofree char *param = g_ascii_strup (*p, -1);

              if (g_str_equal (param, "ENCODING=B") || g_str_equal (param, "ENCODING=BASE64") ||
                  g_str_equal (param, "ENCODING=BASE64") || g_str_has_prefix (param, "BASE64"))
                base64 = TRUE;
            }
          if (base64 || g_str_has_prefix (value, "data:"))
            {
              const char *data = g_str_has_prefix (value, "data:") ? strstr (value, ",") : NULL;

              if (photo != NULL)
                g_string_free (photo, TRUE);
              photo = g_string_new (data != NULL ? data + 1 : value);
              photo_is_base64 = TRUE;
            }
        }
    }

  if (photo != NULL)
    {
      g_autofree char *compact = g_strdup (photo->str);
      gsize out_len = 0;
      guchar *decoded;

      g_strdelimit (compact, " \t", '\0');
      decoded = g_base64_decode (photo->str, &out_len);
      if (out_len > 0)
        contact->photo = g_bytes_new_take (decoded, out_len);
      else
        g_free (decoded);
      g_string_free (photo, TRUE);
    }

  contact->name = fn != NULL && *fn != '\0' ? g_steal_pointer (&fn) : g_steal_pointer (&n);
  if (contact->name == NULL || *contact->name == '\0')
    {
      g_free (contact->name);
      contact->name = contact->numbers->len > 0 ? g_strdup (g_ptr_array_index (contact->numbers, 0)) : g_strdup ("");
    }
  return contact;
}
