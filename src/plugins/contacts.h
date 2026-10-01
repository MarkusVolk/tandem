/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once

#include "../plugin.h"
#include "../vcard.h"

G_BEGIN_DECLS

#define TANDEM_TYPE_CONTACTS_PLUGIN (tandem_contacts_plugin_get_type ())
G_DECLARE_FINAL_TYPE (TandemContactsPlugin, tandem_contacts_plugin, TANDEM, CONTACTS_PLUGIN, TandemPlugin)

TandemContact *tandem_contacts_plugin_lookup_number (TandemContactsPlugin *self, const char *number);
TandemContact *tandem_contacts_plugin_lookup_uid    (TandemContactsPlugin *self, const char *uid);
GList         *tandem_contacts_plugin_list          (TandemContactsPlugin *self);
guint          tandem_contacts_plugin_count         (TandemContactsPlugin *self);
char          *tandem_contacts_plugin_photo_path    (TandemContactsPlugin *self, TandemContact *contact);
void           tandem_contacts_plugin_sync          (TandemContactsPlugin *self);

TandemContact *tandem_device_lookup_contact          (TandemDevice *device, const char *number);

G_END_DECLS
