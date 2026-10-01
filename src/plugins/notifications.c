/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "../device.h"
#include "../notify.h"
#include "../plugin.h"
#include "../transfer.h"

#include <adwaita.h>
#include <glib/gstdio.h>
#include <stdlib.h>

#define TANDEM_TYPE_NOTIFICATIONS_PLUGIN (tandem_notifications_plugin_get_type ())
G_DECLARE_FINAL_TYPE (TandemNotificationsPlugin, tandem_notifications_plugin, TANDEM, NOTIFICATIONS_PLUGIN, TandemPlugin)

typedef struct
{
  TandemNotificationsPlugin *plugin;
  char *id;
  char *app_name;
  char *title;
  char *text;
  char *reply_id;
  GStrv actions;
  guint32 desktop_id;
  gboolean clearable;
  gboolean silent;
} RemoteNotification;

struct _TandemNotificationsPlugin
{
  TandemPlugin parent_instance;

  GHashTable *notifications;
  char *icon_dir;
};

G_DEFINE_FINAL_TYPE (TandemNotificationsPlugin, tandem_notifications_plugin, TANDEM_TYPE_PLUGIN)

static const char *const notifications_incoming[] = { "kdeconnect.notification", NULL };
static const char *const notifications_outgoing[] = {
  "kdeconnect.notification.request",
  "kdeconnect.notification.action",
  "kdeconnect.notification.reply",
  NULL
};

static void
remote_notification_free (RemoteNotification *n)
{
  g_free (n->id);
  g_free (n->app_name);
  g_free (n->title);
  g_free (n->text);
  g_free (n->reply_id);
  g_strfreev (n->actions);
  g_free (n);
}

static void
send_cancel (TandemPlugin *plugin, const char *id)
{
  g_autoptr(TandemPacket) packet = tandem_packet_new ("kdeconnect.notification.request");

  tandem_packet_set_string (packet, "cancel", id);
  tandem_plugin_send (plugin, packet);
}

static void
send_reply (TandemPlugin *plugin, const char *reply_id, const char *message)
{
  g_autoptr(TandemPacket) packet = tandem_packet_new ("kdeconnect.notification.reply");

  tandem_packet_set_string (packet, "requestReplyId", reply_id);
  tandem_packet_set_string (packet, "message", message);
  tandem_plugin_send (plugin, packet);
}

static void
send_action (TandemPlugin *plugin, const char *id, const char *action)
{
  g_autoptr(TandemPacket) packet = tandem_packet_new ("kdeconnect.notification.action");

  tandem_packet_set_string (packet, "key", id);
  tandem_packet_set_string (packet, "action", action);
  tandem_plugin_send (plugin, packet);
}

static void
reply_response_cb (AdwAlertDialog *dialog, const char *response, gpointer user_data)
{
  RemoteNotification *n = user_data;
  GtkWidget *entry = adw_alert_dialog_get_extra_child (dialog);
  const char *text = gtk_editable_get_text (GTK_EDITABLE (entry));

  if (g_str_equal (response, "send") && *text != '\0' && n->reply_id != NULL)
    send_reply (TANDEM_PLUGIN (n->plugin), n->reply_id, text);
}

static void
open_reply_dialog (RemoteNotification *n)
{
  g_autofree char *heading = g_strdup_printf ("Reply to %s", n->app_name);
  AdwDialog *dialog = adw_alert_dialog_new (heading, n->title);
  GtkWidget *entry = gtk_entry_new ();
  GtkWindow *window = gtk_application_get_active_window (GTK_APPLICATION (g_application_get_default ()));

  gtk_entry_set_placeholder_text (GTK_ENTRY (entry), "Message");
  adw_alert_dialog_set_extra_child (ADW_ALERT_DIALOG (dialog), entry);
  adw_alert_dialog_add_responses (ADW_ALERT_DIALOG (dialog), "cancel", "Cancel", "send", "Send", NULL);
  adw_alert_dialog_set_response_appearance (ADW_ALERT_DIALOG (dialog), "send", ADW_RESPONSE_SUGGESTED);
  adw_alert_dialog_set_default_response (ADW_ALERT_DIALOG (dialog), "send");
  g_signal_connect (dialog, "response", G_CALLBACK (reply_response_cb), n);
  adw_dialog_present (dialog, window != NULL ? GTK_WIDGET (window) : NULL);
}

static void
action_cb (guint32 id, const char *action, const char *reply, gpointer user_data)
{
  RemoteNotification *n = user_data;
  TandemPlugin *plugin = TANDEM_PLUGIN (n->plugin);

  if (g_str_equal (action, "inline-reply") && reply != NULL)
    {
      if (n->reply_id != NULL)
        send_reply (plugin, n->reply_id, reply);
    }
  else if (g_str_equal (action, "reply"))
    {
      open_reply_dialog (n);
    }
  else if (g_str_has_prefix (action, "act:"))
    {
      int index = atoi (action + 4);

      if (n->actions != NULL && index >= 0 && index < (int) g_strv_length (n->actions))
        send_action (plugin, n->id, n->actions[index]);
    }
}

static void
closed_cb (guint32 id, guint32 reason, gpointer user_data)
{
  RemoteNotification *n = user_data;

  if (n->desktop_id == id)
    n->desktop_id = 0;
  if (reason == 2 && n->clearable)
    {
      send_cancel (TANDEM_PLUGIN (n->plugin), n->id);
      g_hash_table_remove (n->plugin->notifications, n->id);
    }
}

static void
sent_cb (GObject *source, GAsyncResult *result, gpointer user_data)
{
  RemoteNotification *n = user_data;
  g_autoptr(GError) error = NULL;
  guint32 id = tandem_notification_send_finish (result, &error);

  if (id == 0)
    g_debug ("notification for %s failed: %s", n->app_name, error != NULL ? error->message : "?");
  else if (g_hash_table_lookup (n->plugin->notifications, n->id) == n)
    n->desktop_id = id;
}

static void
show_notification (RemoteNotification *n, const char *icon_path)
{
  g_autoptr(TandemNotification) notification = tandem_notification_new (n->title, n->text);
  TandemDevice *device = tandem_plugin_get_device (TANDEM_PLUGIN (n->plugin));

  if (icon_path != NULL)
    tandem_notification_set_image (notification, icon_path);
  else
    tandem_notification_set_icon (notification, "phone-symbolic");
  tandem_notification_set_replaces (notification, n->desktop_id);
  tandem_notification_set_hint (notification, "x-kde-origin-name", g_variant_new_string (tandem_device_get_name (device)));
  tandem_notification_set_hint (notification, "x-kde-appname", g_variant_new_string (n->app_name));
  if (n->reply_id != NULL)
    {
      tandem_notification_add_action (notification, "reply", "Reply");
      if (tandem_notify_has_capability ("inline-reply"))
        tandem_notification_set_hint (notification, "x-kde-reply-placeholder-text", g_variant_new_string ("Reply"));
    }
  for (guint i = 0; n->actions != NULL && n->actions[i] != NULL; i++)
    {
      g_autofree char *key = g_strdup_printf ("act:%u", i);

      tandem_notification_add_action (notification, key, n->actions[i]);
    }
  tandem_notification_set_callbacks (notification, action_cb, closed_cb, n, NULL);
  tandem_notification_send_async (notification, sent_cb, n);
}

typedef struct
{
  RemoteNotification *notification;
  char *path;
} IconDownload;

static void
icon_downloaded_cb (GObject *source, GAsyncResult *result, gpointer user_data)
{
  IconDownload *dl = user_data;
  g_autoptr(GError) error = NULL;

  if (!tandem_transfer_download_to_file_finish (TANDEM_TRANSFER (source), result, &error))
    {
      g_debug ("notification icon failed: %s", error->message);
      g_unlink (dl->path);
      show_notification (dl->notification, NULL);
    }
  else
    {
      show_notification (dl->notification, dl->path);
    }
  g_free (dl->path);
  g_free (dl);
  g_object_unref (source);
}

static GStrv
strv_from_json_array (JsonArray *array)
{
  g_autoptr(GStrvBuilder) builder = g_strv_builder_new ();

  for (guint i = 0; array != NULL && i < json_array_get_length (array); i++)
    {
      JsonNode *node = json_array_get_element (array, i);

      if (JSON_NODE_HOLDS_VALUE (node) && json_node_get_value_type (node) == G_TYPE_STRING)
        g_strv_builder_add (builder, json_node_get_string (node));
    }
  return g_strv_builder_end (builder);
}

static void
notifications_handle_packet (TandemPlugin *plugin, TandemPacket *packet)
{
  TandemNotificationsPlugin *self = TANDEM_NOTIFICATIONS_PLUGIN (plugin);
  const char *id = tandem_packet_get_string (packet, "id", NULL);
  RemoteNotification *n;
  const char *hash;

  if (id == NULL)
    return;
  if (tandem_packet_get_boolean (packet, "isCancel", FALSE))
    {
      n = g_hash_table_lookup (self->notifications, id);
      if (n != NULL)
        {
          if (n->desktop_id != 0)
            tandem_notify_close (n->desktop_id);
          g_hash_table_remove (self->notifications, id);
        }
      return;
    }

  n = g_hash_table_lookup (self->notifications, id);
  if (n == NULL)
    {
      n = g_new0 (RemoteNotification, 1);
      n->plugin = self;
      n->id = g_strdup (id);
      g_hash_table_insert (self->notifications, g_strdup (id), n);
    }
  else
    {
      g_free (n->app_name);
      g_free (n->title);
      g_free (n->text);
      g_free (n->reply_id);
      g_strfreev (n->actions);
    }
  n->app_name = g_strdup (tandem_packet_get_string (packet, "appName", ""));
  n->title = g_strdup (tandem_packet_get_string (packet, "title", n->app_name));
  n->text = g_strdup (tandem_packet_get_string (packet, "text", ""));
  n->reply_id = g_strdup (tandem_packet_get_string (packet, "requestReplyId", NULL));
  n->actions = strv_from_json_array (tandem_packet_get_array (packet, "actions"));
  n->clearable = tandem_packet_get_boolean (packet, "isClearable", FALSE);
  n->silent = tandem_packet_get_boolean (packet, "silent", FALSE);
  if (*n->title == '\0')
    {
      g_free (n->title);
      n->title = g_strdup (n->app_name);
    }
  tandem_plugin_changed (plugin);

  if (n->silent && n->desktop_id == 0)
    return;

  hash = tandem_packet_get_string (packet, "payloadHash", NULL);
  if (hash != NULL && tandem_packet_has_payload (packet))
    {
      g_autofree char *path = g_build_filename (self->icon_dir, hash, NULL);

      if (g_file_test (path, G_FILE_TEST_EXISTS))
        {
          show_notification (n, path);
        }
      else
        {
          TandemTransfer *transfer = tandem_device_download (tandem_plugin_get_device (plugin), packet);
          g_autoptr(GFile) file = g_file_new_for_path (path);
          IconDownload *dl;

          if (transfer == NULL)
            {
              show_notification (n, NULL);
              return;
            }
          g_mkdir_with_parents (self->icon_dir, 0700);
          dl = g_new0 (IconDownload, 1);
          dl->notification = n;
          dl->path = g_steal_pointer (&path);
          tandem_transfer_download_to_file_async (transfer, file, NULL, icon_downloaded_cb, dl);
        }
      return;
    }
  show_notification (n, NULL);
}

static void
notifications_connected (TandemPlugin *plugin)
{
  g_autoptr(TandemPacket) request = tandem_packet_new ("kdeconnect.notification.request");

  tandem_packet_set_boolean (request, "request", TRUE);
  tandem_plugin_send (plugin, request);
}

static void
notifications_disconnected (TandemPlugin *plugin)
{
  TandemNotificationsPlugin *self = TANDEM_NOTIFICATIONS_PLUGIN (plugin);
  GHashTableIter iter;
  gpointer value;

  g_hash_table_iter_init (&iter, self->notifications);
  while (g_hash_table_iter_next (&iter, NULL, &value))
    {
      RemoteNotification *n = value;

      if (n->desktop_id != 0)
        tandem_notify_close (n->desktop_id);
    }
  g_hash_table_remove_all (self->notifications);
  tandem_plugin_changed (plugin);
}

GList *
tandem_notifications_plugin_list (TandemPlugin *plugin)
{
  return g_hash_table_get_values (TANDEM_NOTIFICATIONS_PLUGIN (plugin)->notifications);
}

GVariant *
tandem_notifications_plugin_describe (TandemPlugin *plugin)
{
  TandemNotificationsPlugin *self = TANDEM_NOTIFICATIONS_PLUGIN (plugin);
  GVariantBuilder builder;
  GHashTableIter iter;
  gpointer value;

  g_variant_builder_init (&builder, G_VARIANT_TYPE ("a(ssss)"));
  g_hash_table_iter_init (&iter, self->notifications);
  while (g_hash_table_iter_next (&iter, NULL, &value))
    {
      RemoteNotification *n = value;

      g_variant_builder_add (&builder, "(ssss)", n->id, n->app_name != NULL ? n->app_name : "",
                             n->title != NULL ? n->title : "", n->text != NULL ? n->text : "");
    }
  return g_variant_builder_end (&builder);
}

void
tandem_notifications_plugin_dismiss (TandemPlugin *plugin, const char *id)
{
  TandemNotificationsPlugin *self = TANDEM_NOTIFICATIONS_PLUGIN (plugin);
  RemoteNotification *n = g_hash_table_lookup (self->notifications, id);

  if (n == NULL)
    return;
  if (n->desktop_id != 0)
    tandem_notify_close (n->desktop_id);
  send_cancel (plugin, id);
  g_hash_table_remove (self->notifications, id);
  tandem_plugin_changed (plugin);
}

static void
dismiss_clicked_cb (GtkButton *button, gpointer user_data)
{
  tandem_notifications_plugin_dismiss (TANDEM_PLUGIN (user_data), g_object_get_data (G_OBJECT (button), "id"));
}

static void
fill_group (TandemPlugin *plugin, GtkWidget *group)
{
  TandemNotificationsPlugin *self = TANDEM_NOTIFICATIONS_PLUGIN (plugin);
  GtkWidget *listbox = g_object_get_data (G_OBJECT (group), "rows");
  GtkWidget *child;
  GHashTableIter iter;
  gpointer value;
  guint count = 0;

  if (listbox == NULL)
    {
      listbox = gtk_list_box_new ();
      gtk_list_box_set_selection_mode (GTK_LIST_BOX (listbox), GTK_SELECTION_NONE);
      gtk_widget_add_css_class (listbox, "boxed-list");
      g_object_set_data (G_OBJECT (group), "rows", listbox);
      adw_preferences_group_add (ADW_PREFERENCES_GROUP (group), listbox);
    }
  while ((child = gtk_widget_get_first_child (listbox)) != NULL)
    gtk_list_box_remove (GTK_LIST_BOX (listbox), child);

  g_hash_table_iter_init (&iter, self->notifications);
  while (g_hash_table_iter_next (&iter, NULL, &value))
    {
      RemoteNotification *n = value;
      GtkWidget *row = adw_action_row_new ();
      GtkWidget *dismiss = gtk_button_new_from_icon_name ("window-close-symbolic");
      g_autofree char *subtitle = g_strdup_printf ("%s · %s", n->app_name, n->text);

      adw_preferences_row_set_title (ADW_PREFERENCES_ROW (row), n->title);
      adw_action_row_set_subtitle (ADW_ACTION_ROW (row), subtitle);
      adw_action_row_set_subtitle_lines (ADW_ACTION_ROW (row), 2);
      gtk_widget_set_valign (dismiss, GTK_ALIGN_CENTER);
      gtk_widget_add_css_class (dismiss, "flat");
      g_object_set_data_full (G_OBJECT (dismiss), "id", g_strdup (n->id), g_free);
      g_signal_connect_object (dismiss, "clicked", G_CALLBACK (dismiss_clicked_cb), plugin, 0);
      adw_action_row_add_suffix (ADW_ACTION_ROW (row), dismiss);
      gtk_list_box_append (GTK_LIST_BOX (listbox), row);
      count++;
    }
  if (count == 0)
    {
      GtkWidget *row = adw_action_row_new ();

      adw_preferences_row_set_title (ADW_PREFERENCES_ROW (row), "No notifications");
      gtk_list_box_append (GTK_LIST_BOX (listbox), row);
    }
}

static void
notifications_changed_cb (TandemPlugin *plugin, gpointer user_data)
{
  fill_group (plugin, GTK_WIDGET (user_data));
}

static GtkWidget *
notifications_create_group (TandemPlugin *plugin)
{
  GtkWidget *group = adw_preferences_group_new ();

  adw_preferences_group_set_title (ADW_PREFERENCES_GROUP (group), "Notifications");
  fill_group (plugin, group);
  g_signal_connect_object (plugin, "changed", G_CALLBACK (notifications_changed_cb), group, 0);
  return group;
}

static void
tandem_notifications_plugin_constructed (GObject *object)
{
  TandemNotificationsPlugin *self = TANDEM_NOTIFICATIONS_PLUGIN (object);
  TandemDevice *device = tandem_plugin_get_device (TANDEM_PLUGIN (self));

  G_OBJECT_CLASS (tandem_notifications_plugin_parent_class)->constructed (object);
  self->icon_dir = g_build_filename (g_get_user_cache_dir (), "tandem", tandem_device_get_id (device), "icons", NULL);
}

static void
tandem_notifications_plugin_finalize (GObject *object)
{
  TandemNotificationsPlugin *self = TANDEM_NOTIFICATIONS_PLUGIN (object);

  g_hash_table_unref (self->notifications);
  g_free (self->icon_dir);
  G_OBJECT_CLASS (tandem_notifications_plugin_parent_class)->finalize (object);
}

static void
tandem_notifications_plugin_class_init (TandemNotificationsPluginClass *klass)
{
  TandemPluginClass *plugin_class = TANDEM_PLUGIN_CLASS (klass);
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->constructed = tandem_notifications_plugin_constructed;
  object_class->finalize = tandem_notifications_plugin_finalize;
  plugin_class->name = "notifications";
  plugin_class->title = "Notifications";
  plugin_class->incoming = notifications_incoming;
  plugin_class->outgoing = notifications_outgoing;
  plugin_class->handle_packet = notifications_handle_packet;
  plugin_class->connected = notifications_connected;
  plugin_class->disconnected = notifications_disconnected;
  plugin_class->create_group = notifications_create_group;
}

static void
tandem_notifications_plugin_init (TandemNotificationsPlugin *self)
{
  self->notifications = g_hash_table_new_full (g_str_hash, g_str_equal, g_free,
                                               (GDestroyNotify) remote_notification_free);
}
