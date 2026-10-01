/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "contacts.h"

#include "../device.h"
#include "../util.h"

#include <adwaita.h>
#include <glib/gstdio.h>
#include <string.h>

#define BATCH_SIZE 50

struct _TandemContactsPlugin
{
  TandemPlugin parent_instance;

  char *dir;
  GHashTable *contacts;
  GHashTable *by_number;
  GPtrArray *pending_uids;
  guint pending_pos;
  gboolean syncing;
  GtkWidget *count_row;
};

G_DEFINE_FINAL_TYPE (TandemContactsPlugin, tandem_contacts_plugin, TANDEM_TYPE_PLUGIN)

static const char *const contacts_incoming[] = {
  "kdeconnect.contacts.response_uids_timestamps",
  "kdeconnect.contacts.response_vcards",
  NULL
};
static const char *const contacts_outgoing[] = {
  "kdeconnect.contacts.request_all_uids_timestamps",
  "kdeconnect.contacts.request_vcards_by_uid",
  NULL
};

static void
index_contact (TandemContactsPlugin *self, TandemContact *contact)
{
  for (guint i = 0; i < contact->numbers->len; i++)
    {
      g_autofree char *key = tandem_phone_number_key (g_ptr_array_index (contact->numbers, i));

      if (*key != '\0')
        g_hash_table_insert (self->by_number, g_steal_pointer (&key), contact);
    }
}

static void
rebuild_index (TandemContactsPlugin *self)
{
  GHashTableIter iter;
  gpointer value;

  g_hash_table_remove_all (self->by_number);
  g_hash_table_iter_init (&iter, self->contacts);
  while (g_hash_table_iter_next (&iter, NULL, &value))
    index_contact (self, value);
}

static void
load_from_disk (TandemContactsPlugin *self)
{
  g_autoptr(GDir) dir = g_dir_open (self->dir, 0, NULL);
  const char *name;

  if (dir == NULL)
    return;
  while ((name = g_dir_read_name (dir)) != NULL)
    {
      g_autofree char *path = NULL;
      g_autofree char *text = NULL;
      TandemContact *contact;

      if (!g_str_has_suffix (name, ".vcf"))
        continue;
      path = g_build_filename (self->dir, name, NULL);
      text = tandem_read_file (path, NULL);
      if (text == NULL)
        continue;
      contact = tandem_vcard_parse (text);
      if (contact->uid == NULL)
        contact->uid = g_strndup (name, strlen (name) - 4);
      g_hash_table_insert (self->contacts, g_strdup (contact->uid), contact);
    }
  rebuild_index (self);
}

static void
update_count_row (TandemContactsPlugin *self)
{
  g_autofree char *text = NULL;

  if (self->count_row == NULL)
    return;
  text = g_strdup_printf ("%u contacts%s", g_hash_table_size (self->contacts), self->syncing ? ", syncing" : "");
  adw_action_row_set_subtitle (ADW_ACTION_ROW (self->count_row), text);
}

static void
request_next_batch (TandemContactsPlugin *self)
{
  g_autoptr(TandemPacket) packet = NULL;
  g_autoptr(GPtrArray) batch = g_ptr_array_new ();

  if (self->pending_uids == NULL || self->pending_pos >= self->pending_uids->len)
    {
      g_clear_pointer (&self->pending_uids, g_ptr_array_unref);
      self->syncing = FALSE;
      update_count_row (self);
      tandem_plugin_changed (TANDEM_PLUGIN (self));
      return;
    }
  for (guint i = 0; i < BATCH_SIZE && self->pending_pos < self->pending_uids->len; i++)
    g_ptr_array_add (batch, g_ptr_array_index (self->pending_uids, self->pending_pos++));
  g_ptr_array_add (batch, NULL);
  packet = tandem_packet_new ("kdeconnect.contacts.request_vcards_by_uid");
  tandem_packet_set_strv (packet, "uids", (const char *const *) batch->pdata);
  tandem_plugin_send (TANDEM_PLUGIN (self), packet);
}

static void
handle_timestamps (TandemContactsPlugin *self, TandemPacket *packet)
{
  JsonArray *uids = tandem_packet_get_array (packet, "uids");
  g_autoptr(GHashTable) seen = g_hash_table_new (g_str_hash, g_str_equal);
  GHashTableIter iter;
  gpointer key, value;

  g_clear_pointer (&self->pending_uids, g_ptr_array_unref);
  self->pending_uids = g_ptr_array_new_with_free_func (g_free);
  self->pending_pos = 0;
  for (guint i = 0; uids != NULL && i < json_array_get_length (uids); i++)
    {
      const char *uid = json_array_get_string_element (uids, i);
      gint64 stamp = json_object_has_member (packet->body, uid) ? json_object_get_int_member (packet->body, uid) : 0;
      TandemContact *existing;

      if (uid == NULL)
        continue;
      g_hash_table_add (seen, (gpointer) uid);
      existing = g_hash_table_lookup (self->contacts, uid);
      if (existing == NULL || existing->timestamp != stamp)
        g_ptr_array_add (self->pending_uids, g_strdup (uid));
    }

  g_hash_table_iter_init (&iter, self->contacts);
  while (g_hash_table_iter_next (&iter, &key, &value))
    {
      if (!g_hash_table_contains (seen, key))
        {
          g_autofree char *file = g_strconcat (key, ".vcf", NULL);
          g_autofree char *path = g_build_filename (self->dir, file, NULL);

          g_unlink (path);
          g_hash_table_iter_remove (&iter);
        }
    }
  rebuild_index (self);
  self->syncing = self->pending_uids->len > 0;
  update_count_row (self);
  request_next_batch (self);
}

static void
handle_vcards (TandemContactsPlugin *self, TandemPacket *packet)
{
  JsonArray *uids = tandem_packet_get_array (packet, "uids");

  tandem_ensure_dir (self->dir, NULL);
  for (guint i = 0; uids != NULL && i < json_array_get_length (uids); i++)
    {
      const char *uid = json_array_get_string_element (uids, i);
      const char *text;
      TandemContact *contact;
      g_autofree char *file = NULL;
      g_autofree char *path = NULL;

      if (uid == NULL || !json_object_has_member (packet->body, uid))
        continue;
      text = json_object_get_string_member (packet->body, uid);
      if (text == NULL)
        continue;
      contact = tandem_vcard_parse (text);
      g_free (contact->uid);
      contact->uid = g_strdup (uid);
      file = g_strconcat (uid, ".vcf", NULL);
      path = g_build_filename (self->dir, file, NULL);
      tandem_write_file (path, text, -1, 0600, NULL);
      g_hash_table_insert (self->contacts, g_strdup (uid), contact);
    }
  rebuild_index (self);
  update_count_row (self);
  request_next_batch (self);
}

static void
contacts_handle_packet (TandemPlugin *plugin, TandemPacket *packet)
{
  TandemContactsPlugin *self = TANDEM_CONTACTS_PLUGIN (plugin);

  if (tandem_packet_is_type (packet, "kdeconnect.contacts.response_uids_timestamps"))
    handle_timestamps (self, packet);
  else if (tandem_packet_is_type (packet, "kdeconnect.contacts.response_vcards"))
    handle_vcards (self, packet);
}

void
tandem_contacts_plugin_sync (TandemContactsPlugin *self)
{
  g_autoptr(TandemPacket) packet = tandem_packet_new ("kdeconnect.contacts.request_all_uids_timestamps");

  tandem_plugin_send (TANDEM_PLUGIN (self), packet);
}

static void
contacts_connected (TandemPlugin *plugin)
{
  tandem_contacts_plugin_sync (TANDEM_CONTACTS_PLUGIN (plugin));
}

TandemContact *
tandem_contacts_plugin_lookup_number (TandemContactsPlugin *self, const char *number)
{
  g_autofree char *key = tandem_phone_number_key (number);

  if (self == NULL || *key == '\0')
    return NULL;
  return g_hash_table_lookup (self->by_number, key);
}

TandemContact *
tandem_contacts_plugin_lookup_uid (TandemContactsPlugin *self, const char *uid)
{
  return self != NULL && uid != NULL ? g_hash_table_lookup (self->contacts, uid) : NULL;
}

static int
compare_contacts (gconstpointer a, gconstpointer b)
{
  const TandemContact *ca = a;
  const TandemContact *cb = b;

  return g_utf8_collate (ca->name, cb->name);
}

GList *
tandem_contacts_plugin_list (TandemContactsPlugin *self)
{
  return g_list_sort (g_hash_table_get_values (self->contacts), compare_contacts);
}

guint
tandem_contacts_plugin_count (TandemContactsPlugin *self)
{
  return g_hash_table_size (self->contacts);
}

char *
tandem_contacts_plugin_photo_path (TandemContactsPlugin *self, TandemContact *contact)
{
  g_autofree char *file = NULL;
  g_autofree char *path = NULL;
  g_autofree char *cache_dir = NULL;

  if (contact == NULL || contact->photo == NULL || contact->uid == NULL)
    return NULL;
  cache_dir = g_build_filename (g_get_user_cache_dir (), "tandem",
                                tandem_device_get_id (tandem_plugin_get_device (TANDEM_PLUGIN (self))), "photos", NULL);
  file = g_strconcat (contact->uid, ".jpg", NULL);
  path = g_build_filename (cache_dir, file, NULL);
  if (!g_file_test (path, G_FILE_TEST_EXISTS))
    {
      gsize len;
      const char *data = g_bytes_get_data (contact->photo, &len);

      tandem_ensure_dir (cache_dir, NULL);
      if (!tandem_write_file (path, data, len, 0600, NULL))
        return NULL;
    }
  return g_steal_pointer (&path);
}

TandemContact *
tandem_device_lookup_contact (TandemDevice *device, const char *number)
{
  TandemPlugin *plugin = tandem_device_get_plugin (device, "contacts");

  if (plugin == NULL || number == NULL)
    return NULL;
  return tandem_contacts_plugin_lookup_number (TANDEM_CONTACTS_PLUGIN (plugin), number);
}

static void
sync_row_activated_cb (AdwActionRow *row, gpointer user_data)
{
  tandem_contacts_plugin_sync (TANDEM_CONTACTS_PLUGIN (user_data));
}

static void
count_row_destroyed_cb (gpointer data, GObject *where_the_object_was)
{
  TANDEM_CONTACTS_PLUGIN (data)->count_row = NULL;
}

static GtkWidget *
contacts_create_group (TandemPlugin *plugin)
{
  TandemContactsPlugin *self = TANDEM_CONTACTS_PLUGIN (plugin);
  GtkWidget *group = adw_preferences_group_new ();

  adw_preferences_group_set_title (ADW_PREFERENCES_GROUP (group), "Contacts");
  self->count_row = adw_action_row_new ();
  adw_preferences_row_set_title (ADW_PREFERENCES_ROW (self->count_row), "Synchronise now");
  adw_action_row_set_activatable_widget (ADW_ACTION_ROW (self->count_row), NULL);
  gtk_list_box_row_set_activatable (GTK_LIST_BOX_ROW (self->count_row), TRUE);
  gtk_widget_add_css_class (self->count_row, "property");
  g_signal_connect (self->count_row, "activated", G_CALLBACK (sync_row_activated_cb), self);
  g_object_weak_ref (G_OBJECT (self->count_row), count_row_destroyed_cb, self);
  adw_preferences_group_add (ADW_PREFERENCES_GROUP (group), self->count_row);
  update_count_row (self);
  return group;
}

static void
tandem_contacts_plugin_constructed (GObject *object)
{
  TandemContactsPlugin *self = TANDEM_CONTACTS_PLUGIN (object);
  TandemDevice *device = tandem_plugin_get_device (TANDEM_PLUGIN (self));

  G_OBJECT_CLASS (tandem_contacts_plugin_parent_class)->constructed (object);
  self->dir = g_build_filename (g_get_user_data_dir (), "tandem", tandem_device_get_id (device), "contacts", NULL);
  load_from_disk (self);
}

static void
tandem_contacts_plugin_finalize (GObject *object)
{
  TandemContactsPlugin *self = TANDEM_CONTACTS_PLUGIN (object);

  if (self->count_row != NULL)
    g_object_weak_unref (G_OBJECT (self->count_row), count_row_destroyed_cb, self);
  g_clear_pointer (&self->pending_uids, g_ptr_array_unref);
  g_hash_table_unref (self->by_number);
  g_hash_table_unref (self->contacts);
  g_free (self->dir);
  G_OBJECT_CLASS (tandem_contacts_plugin_parent_class)->finalize (object);
}

static void
tandem_contacts_plugin_class_init (TandemContactsPluginClass *klass)
{
  TandemPluginClass *plugin_class = TANDEM_PLUGIN_CLASS (klass);
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->constructed = tandem_contacts_plugin_constructed;
  object_class->finalize = tandem_contacts_plugin_finalize;
  plugin_class->name = "contacts";
  plugin_class->title = "Contacts";
  plugin_class->incoming = contacts_incoming;
  plugin_class->outgoing = contacts_outgoing;
  plugin_class->handle_packet = contacts_handle_packet;
  plugin_class->connected = contacts_connected;
  plugin_class->create_group = contacts_create_group;
}

static void
tandem_contacts_plugin_init (TandemContactsPlugin *self)
{
  self->contacts = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, (GDestroyNotify) tandem_contact_free);
  self->by_number = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);
}
