/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once

#include <gio/gio.h>

G_BEGIN_DECLS

typedef struct
{
  char *uid;
  char *name;
  GPtrArray *numbers;
  GPtrArray *emails;
  GBytes *photo;
  gint64 timestamp;
} TandemContact;

TandemContact *tandem_vcard_parse        (const char *text);
void           tandem_contact_free       (TandemContact *contact);
char          *tandem_phone_number_key   (const char *number);

G_DEFINE_AUTOPTR_CLEANUP_FUNC (TandemContact, tandem_contact_free)

G_END_DECLS
