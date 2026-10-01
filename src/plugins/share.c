/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "../clipboard.h"
#include "../device.h"
#include "../kodi.h"
#include "../notify.h"
#include "../plugin.h"
#include "../transfer.h"
#include "../util.h"

#include <string.h>

#define TANDEM_TYPE_SHARE_PLUGIN (tandem_share_plugin_get_type ())
G_DECLARE_FINAL_TYPE (TandemSharePlugin, tandem_share_plugin, TANDEM, SHARE_PLUGIN, TandemPlugin)

struct _TandemSharePlugin
{
  TandemPlugin parent_instance;
};

G_DEFINE_FINAL_TYPE (TandemSharePlugin, tandem_share_plugin, TANDEM_TYPE_PLUGIN)

static const char *const share_types[] = { "kdeconnect.share.request", "kdeconnect.share.request.update", NULL };

static gboolean
play_in_kodi (const char *file_or_url)
{
  g_autoptr(GSettings) settings = g_settings_new ("de.flk.Tandem");

  if (!g_settings_get_boolean (settings, "play-received") || !tandem_kodi_is_connected (tandem_kodi_get ()))
    return FALSE;
  tandem_kodi_open (tandem_kodi_get (), file_or_url);
  return TRUE;
}

static char *
download_dir (void)
{
  g_autoptr(GSettings) settings = g_settings_new ("de.flk.Tandem");
  g_autofree char *configured = g_settings_get_string (settings, "download-dir");
  const char *dir;

  if (configured != NULL && *configured != '\0')
    return g_steal_pointer (&configured);
  dir = g_get_user_special_dir (G_USER_DIRECTORY_DOWNLOAD);
  if (dir == NULL)
    dir = g_get_home_dir ();
  return g_strdup (dir);
}

typedef struct
{
  TandemSharePlugin *plugin;
  GFile *file;
  char *name;
  gboolean open;
  gint64 last_modified;
  guint32 notification_id;
  gint64 last_percent;
} Receive;

static void
receive_free (Receive *rx)
{
  g_clear_object (&rx->file);
  g_free (rx->name);
  g_free (rx);
}

static void
received_action_cb (guint32 id, const char *action, const char *reply, gpointer user_data)
{
  GFile *file = user_data;
  g_autofree char *uri = g_file_get_uri (file);

  if (g_str_equal (action, "open"))
    {
      g_app_info_launch_default_for_uri (uri, NULL, NULL);
    }
  else if (g_str_equal (action, "folder"))
    {
      g_autoptr(GDBusConnection) bus = g_bus_get_sync (G_BUS_TYPE_SESSION, NULL, NULL);
      GVariantBuilder uris;

      g_variant_builder_init (&uris, G_VARIANT_TYPE ("as"));
      g_variant_builder_add (&uris, "s", uri);
      if (bus != NULL)
        g_dbus_connection_call (bus, "org.freedesktop.FileManager1", "/org/freedesktop/FileManager1",
                                "org.freedesktop.FileManager1", "ShowItems", g_variant_new ("(ass)", &uris, ""),
                                NULL, G_DBUS_CALL_FLAGS_NONE, 5000, NULL, NULL, NULL);
    }
  tandem_notify_close (id);
}

static void
progress_cb (TandemTransfer *transfer, gint64 done, gint64 total, gpointer user_data)
{
  Receive *rx = user_data;
  gint64 percent = total > 0 ? done * 100 / total : 0;
  g_autoptr(TandemNotification) notification = NULL;
  g_autofree char *body = NULL;

  if (percent == rx->last_percent || percent - rx->last_percent < 5)
    return;
  rx->last_percent = percent;
  body = g_strdup_printf ("%" G_GINT64_FORMAT " %%", percent);
  notification = tandem_notification_new (rx->name, body);
  tandem_notification_set_icon (notification, "folder-download-symbolic");
  tandem_notification_set_replaces (notification, rx->notification_id);
  tandem_notification_set_hint (notification, "value", g_variant_new_int32 ((gint32) percent));
  tandem_notification_set_timeout (notification, 0);
  tandem_notification_send (notification);
}

static void
progress_sent_cb (GObject *source, GAsyncResult *result, gpointer user_data)
{
  Receive *rx = user_data;

  rx->notification_id = tandem_notification_send_finish (result, NULL);
}

static void
file_received_cb (GObject *source, GAsyncResult *result, gpointer user_data)
{
  Receive *rx = user_data;
  g_autoptr(GError) error = NULL;
  TandemDevice *device = tandem_plugin_get_device (TANDEM_PLUGIN (rx->plugin));
  g_autoptr(TandemNotification) notification = NULL;

  if (!tandem_transfer_download_to_file_finish (TANDEM_TRANSFER (source), result, &error))
    {
      g_autofree char *summary = g_strdup_printf ("Receiving %s failed", rx->name);

      g_file_delete (rx->file, NULL, NULL);
      notification = tandem_notification_new (summary, error->message);
      tandem_notification_set_replaces (notification, rx->notification_id);
      tandem_notification_send (notification);
    }
  else
    {
      g_autofree char *summary = g_strdup_printf ("Received %s", rx->name);
      g_autofree char *body = g_strdup_printf ("from %s", tandem_device_get_name (device));

      if (rx->last_modified > 0)
        {
          g_autoptr(GFileInfo) info = g_file_info_new ();

          g_file_info_set_attribute_uint64 (info, G_FILE_ATTRIBUTE_TIME_MODIFIED, rx->last_modified / 1000);
          g_file_set_attributes_from_info (rx->file, info, G_FILE_QUERY_INFO_NONE, NULL, NULL);
        }
      notification = tandem_notification_new (summary, body);
      tandem_notification_set_icon (notification, "folder-download-symbolic");
      tandem_notification_set_replaces (notification, rx->notification_id);
      tandem_notification_add_action (notification, "open", "Open");
      tandem_notification_add_action (notification, "folder", "Show in folder");
      tandem_notification_set_callbacks (notification, received_action_cb, NULL, g_object_ref (rx->file), g_object_unref);
      tandem_notification_send (notification);
      if (play_in_kodi (g_file_peek_path (rx->file)))
        ;
      else if (rx->open)
        {
          g_autofree char *uri = g_file_get_uri (rx->file);

          g_app_info_launch_default_for_uri (uri, NULL, NULL);
        }
    }
  g_object_unref (source);
  receive_free (rx);
}

static void
receive_file (TandemSharePlugin *self, TandemPacket *packet)
{
  TandemDevice *device = tandem_plugin_get_device (TANDEM_PLUGIN (self));
  g_autofree char *dir = download_dir ();
  g_autofree char *basename = g_path_get_basename (tandem_packet_get_string (packet, "filename", "file"));
  g_autofree char *path = NULL;
  TandemTransfer *transfer;
  Receive *rx;
  g_autoptr(TandemNotification) notification = NULL;
  g_autofree char *body = NULL;

  g_mkdir_with_parents (dir, 0755);
  path = tandem_unique_filename (dir, basename);
  transfer = tandem_device_download (device, packet);
  if (transfer == NULL)
    return;

  rx = g_new0 (Receive, 1);
  rx->plugin = self;
  rx->file = g_file_new_for_path (path);
  rx->name = g_path_get_basename (path);
  rx->open = tandem_packet_get_boolean (packet, "open", FALSE);
  rx->last_modified = tandem_packet_get_int (packet, "lastModified", 0);

  body = g_strdup_printf ("from %s", tandem_device_get_name (device));
  notification = tandem_notification_new (rx->name, body);
  tandem_notification_set_icon (notification, "folder-download-symbolic");
  tandem_notification_set_hint (notification, "value", g_variant_new_int32 (0));
  tandem_notification_set_timeout (notification, 0);
  tandem_notification_send_async (notification, progress_sent_cb, rx);

  g_signal_connect (transfer, "progress", G_CALLBACK (progress_cb), rx);
  tandem_transfer_download_to_file_async (transfer, rx->file, NULL, file_received_cb, rx);
}

static void
receive_text (TandemSharePlugin *self, const char *text)
{
  if (g_uri_is_valid (text, G_URI_FLAGS_NONE, NULL) && play_in_kodi (text))
    return;
  TandemDevice *device = tandem_plugin_get_device (TANDEM_PLUGIN (self));
  g_autofree char *summary = g_strdup_printf ("Text from %s", tandem_device_get_name (device));
  g_autoptr(TandemNotification) notification = tandem_notification_new (summary, text);

  tandem_clipboard_set_text (tandem_clipboard_get (), text);
  tandem_notification_set_icon (notification, "edit-paste-symbolic");
  tandem_notification_send (notification);
}

static void
receive_url (TandemSharePlugin *self, const char *url)
{
  g_autoptr(GError) error = NULL;

  if (play_in_kodi (url))
    return;
  if (!g_app_info_launch_default_for_uri (url, NULL, &error))
    {
      TandemDevice *device = tandem_plugin_get_device (TANDEM_PLUGIN (self));
      g_autofree char *summary = g_strdup_printf ("Link from %s", tandem_device_get_name (device));
      g_autoptr(TandemNotification) notification = tandem_notification_new (summary, url);

      tandem_notification_send (notification);
    }
}

static void
share_handle_packet (TandemPlugin *plugin, TandemPacket *packet)
{
  TandemSharePlugin *self = TANDEM_SHARE_PLUGIN (plugin);

  if (tandem_packet_is_type (packet, "kdeconnect.share.request.update"))
    return;
  if (tandem_packet_has (packet, "filename") && tandem_packet_has_payload (packet))
    receive_file (self, packet);
  else if (tandem_packet_has (packet, "text"))
    receive_text (self, tandem_packet_get_string (packet, "text", ""));
  else if (tandem_packet_has (packet, "url"))
    receive_url (self, tandem_packet_get_string (packet, "url", ""));
}

void
tandem_share_plugin_send_text (TandemPlugin *plugin, const char *text)
{
  g_autoptr(TandemPacket) packet = tandem_packet_new ("kdeconnect.share.request");

  tandem_packet_set_string (packet, "text", text);
  tandem_plugin_send (plugin, packet);
}

void
tandem_share_plugin_send_url (TandemPlugin *plugin, const char *url)
{
  g_autoptr(TandemPacket) packet = tandem_packet_new ("kdeconnect.share.request");

  tandem_packet_set_string (packet, "url", url);
  tandem_plugin_send (plugin, packet);
}

gboolean
tandem_share_plugin_send_file (TandemPlugin *plugin, GFile *file, guint number_of_files, gint64 total_size, GError **error)
{
  g_autoptr(GFileInfo) info = g_file_query_info (file, "standard::name,standard::size,time::modified,time::created",
                                                 G_FILE_QUERY_INFO_NONE, NULL, error);
  g_autoptr(GFileInputStream) stream = NULL;
  g_autoptr(TandemPacket) packet = NULL;
  gint64 size;

  if (info == NULL)
    return FALSE;
  stream = g_file_read (file, NULL, error);
  if (stream == NULL)
    return FALSE;
  size = g_file_info_get_size (info);
  packet = tandem_packet_new ("kdeconnect.share.request");
  tandem_packet_set_string (packet, "filename", g_file_info_get_name (info));
  tandem_packet_set_int (packet, "lastModified",
                         (gint64) g_file_info_get_attribute_uint64 (info, G_FILE_ATTRIBUTE_TIME_MODIFIED) * 1000);
  if (g_file_info_has_attribute (info, G_FILE_ATTRIBUTE_TIME_CREATED))
    tandem_packet_set_int (packet, "creationTime",
                           (gint64) g_file_info_get_attribute_uint64 (info, G_FILE_ATTRIBUTE_TIME_CREATED) * 1000);
  tandem_packet_set_int (packet, "numberOfFiles", number_of_files > 0 ? number_of_files : 1);
  tandem_packet_set_int (packet, "totalPayloadSize", total_size > 0 ? total_size : size);
  tandem_packet_set_payload (packet, G_INPUT_STREAM (stream), size);
  tandem_plugin_send (plugin, packet);
  return TRUE;
}

void
tandem_share_plugin_send_files (TandemPlugin *plugin, GListModel *files)
{
  guint n = g_list_model_get_n_items (files);
  gint64 total = 0;

  for (guint i = 0; i < n; i++)
    {
      g_autoptr(GFile) file = g_list_model_get_item (files, i);
      g_autoptr(GFileInfo) info = g_file_query_info (file, "standard::size", G_FILE_QUERY_INFO_NONE, NULL, NULL);

      if (info != NULL)
        total += g_file_info_get_size (info);
    }
  for (guint i = 0; i < n; i++)
    {
      g_autoptr(GFile) file = g_list_model_get_item (files, i);
      g_autoptr(GError) error = NULL;

      if (!tandem_share_plugin_send_file (plugin, file, n, total, &error))
        g_warning ("share: %s", error->message);
    }
}

static GVariant *
share_call (TandemPlugin *plugin, const char *method, GVariant *args, GError **error)
{
  const char *text = NULL;
  g_autofree const char **paths = NULL;

  if (g_str_equal (method, "files") && tandem_plugin_args_lookup (args, "paths", "^a&s", &paths))
    {
      g_autoptr(GListStore) files = g_list_store_new (G_TYPE_FILE);

      for (const char **p = paths; *p != NULL; p++)
        {
          g_autoptr(GFile) file = g_file_new_for_commandline_arg (*p);

          g_list_store_append (files, file);
        }
      tandem_share_plugin_send_files (plugin, G_LIST_MODEL (files));
    }
  else if (g_str_equal (method, "text") && tandem_plugin_args_lookup (args, "text", "&s", &text))
    tandem_share_plugin_send_text (plugin, text);
  else if (g_str_equal (method, "url") && tandem_plugin_args_lookup (args, "url", "&s", &text))
    tandem_share_plugin_send_url (plugin, text);
  else
    g_set_error (error, G_DBUS_ERROR, G_DBUS_ERROR_INVALID_ARGS, "Bad call %s", method);
  return NULL;
}

static void
tandem_share_plugin_class_init (TandemSharePluginClass *klass)
{
  TandemPluginClass *plugin_class = TANDEM_PLUGIN_CLASS (klass);

  plugin_class->name = "share";
  plugin_class->title = "Share";
  plugin_class->incoming = share_types;
  plugin_class->outgoing = share_types;
  plugin_class->handle_packet = share_handle_packet;
  plugin_class->call = share_call;
}

static void
tandem_share_plugin_init (TandemSharePlugin *self)
{
}
