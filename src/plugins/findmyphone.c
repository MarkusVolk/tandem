/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "../device.h"
#include "../notify.h"
#include "../plugin.h"

#include <adwaita.h>
#include <gst/gst.h>

#define RING_SECONDS 60
#define RING_SOUND "/usr/share/sounds/freedesktop/stereo/phone-incoming-call.oga"

#define TANDEM_TYPE_FINDMYPHONE_PLUGIN (tandem_findmyphone_plugin_get_type ())
G_DECLARE_FINAL_TYPE (TandemFindmyphonePlugin, tandem_findmyphone_plugin, TANDEM, FINDMYPHONE_PLUGIN, TandemPlugin)

struct _TandemFindmyphonePlugin
{
  TandemPlugin parent_instance;

  GstElement *player;
  guint bus_watch;
  guint stop_id;
  guint32 notification_id;
};

G_DEFINE_FINAL_TYPE (TandemFindmyphonePlugin, tandem_findmyphone_plugin, TANDEM_TYPE_PLUGIN)

static const char *const findmyphone_types[] = { "kdeconnect.findmyphone.request", NULL };

static void
stop_ringing (TandemFindmyphonePlugin *self)
{
  g_clear_handle_id (&self->stop_id, g_source_remove);
  g_clear_handle_id (&self->bus_watch, g_source_remove);
  if (self->player != NULL)
    {
      gst_element_set_state (self->player, GST_STATE_NULL);
      g_clear_pointer (&self->player, gst_object_unref);
    }
  if (self->notification_id != 0)
    {
      tandem_notify_close (self->notification_id);
      self->notification_id = 0;
    }
}

static gboolean
bus_cb (GstBus *bus, GstMessage *message, gpointer user_data)
{
  TandemFindmyphonePlugin *self = user_data;

  switch (GST_MESSAGE_TYPE (message))
    {
    case GST_MESSAGE_EOS:
      gst_element_seek_simple (self->player, GST_FORMAT_TIME, GST_SEEK_FLAG_FLUSH | GST_SEEK_FLAG_KEY_UNIT, 0);
      break;
    case GST_MESSAGE_ERROR:
      {
        g_autoptr(GError) error = NULL;

        gst_message_parse_error (message, &error, NULL);
        g_warning ("findmyphone: playback failed: %s", error->message);
        gst_element_set_state (self->player, GST_STATE_NULL);
      }
      break;
    default:
      break;
    }
  return G_SOURCE_CONTINUE;
}

static gboolean
stop_timeout_cb (gpointer user_data)
{
  TandemFindmyphonePlugin *self = user_data;

  self->stop_id = 0;
  stop_ringing (self);
  return G_SOURCE_REMOVE;
}

static void
stop_action_cb (guint32 id, const char *action, const char *reply, gpointer user_data)
{
  stop_ringing (TANDEM_FINDMYPHONE_PLUGIN (user_data));
}

static void
notification_sent_cb (GObject *source, GAsyncResult *result, gpointer user_data)
{
  TandemFindmyphonePlugin *self = user_data;

  self->notification_id = tandem_notification_send_finish (result, NULL);
}

static void
start_ringing (TandemFindmyphonePlugin *self)
{
  TandemDevice *device = tandem_plugin_get_device (TANDEM_PLUGIN (self));
  g_autofree char *summary = g_strdup_printf ("%s is looking for this device", tandem_device_get_name (device));
  g_autoptr(TandemNotification) notification = tandem_notification_new (summary, "Ringing");

  stop_ringing (self);
  if (g_file_test (RING_SOUND, G_FILE_TEST_EXISTS))
    {
      g_autofree char *uri = g_filename_to_uri (RING_SOUND, NULL, NULL);
      GstBus *bus;

      gst_init (NULL, NULL);
      self->player = gst_element_factory_make ("playbin", "tandem-ring");
      if (self->player != NULL)
        {
          g_object_set (self->player, "uri", uri, "volume", 1.0, NULL);
          bus = gst_element_get_bus (self->player);
          self->bus_watch = gst_bus_add_watch (bus, bus_cb, self);
          gst_object_unref (bus);
          gst_element_set_state (self->player, GST_STATE_PLAYING);
        }
    }
  tandem_notification_set_icon (notification, "audio-volume-high-symbolic");
  tandem_notification_set_urgency (notification, 2);
  tandem_notification_set_resident (notification, TRUE);
  tandem_notification_add_action (notification, "stop", "Stop");
  tandem_notification_set_callbacks (notification, stop_action_cb, NULL, self, NULL);
  tandem_notification_send_async (notification, notification_sent_cb, self);
  self->stop_id = g_timeout_add_seconds (RING_SECONDS, stop_timeout_cb, self);
}

static void
findmyphone_handle_packet (TandemPlugin *plugin, TandemPacket *packet)
{
  start_ringing (TANDEM_FINDMYPHONE_PLUGIN (plugin));
}

void
tandem_findmyphone_plugin_ring (TandemPlugin *plugin)
{
  g_autoptr(TandemPacket) packet = tandem_packet_new ("kdeconnect.findmyphone.request");

  tandem_plugin_send (plugin, packet);
}

static void
ring_activated_cb (AdwButtonRow *row, gpointer user_data)
{
  tandem_findmyphone_plugin_ring (TANDEM_PLUGIN (user_data));
}

static GtkWidget *
findmyphone_create_group (TandemPlugin *plugin)
{
  GtkWidget *group = adw_preferences_group_new ();
  GtkWidget *row = adw_button_row_new ();

  adw_preferences_group_set_title (ADW_PREFERENCES_GROUP (group), "Find my phone");
  adw_preferences_row_set_title (ADW_PREFERENCES_ROW (row), "Ring the device");
  adw_button_row_set_start_icon_name (ADW_BUTTON_ROW (row), "audio-volume-high-symbolic");
  g_signal_connect_object (row, "activated", G_CALLBACK (ring_activated_cb), plugin, 0);
  adw_preferences_group_add (ADW_PREFERENCES_GROUP (group), row);
  return group;
}

static void
tandem_findmyphone_plugin_dispose (GObject *object)
{
  stop_ringing (TANDEM_FINDMYPHONE_PLUGIN (object));
  G_OBJECT_CLASS (tandem_findmyphone_plugin_parent_class)->dispose (object);
}

static void
tandem_findmyphone_plugin_class_init (TandemFindmyphonePluginClass *klass)
{
  TandemPluginClass *plugin_class = TANDEM_PLUGIN_CLASS (klass);

  G_OBJECT_CLASS (klass)->dispose = tandem_findmyphone_plugin_dispose;
  plugin_class->name = "findmyphone";
  plugin_class->title = "Find my phone";
  plugin_class->incoming = findmyphone_types;
  plugin_class->outgoing = findmyphone_types;
  plugin_class->handle_packet = findmyphone_handle_packet;
  plugin_class->create_group = findmyphone_create_group;
}

static void
tandem_findmyphone_plugin_init (TandemFindmyphonePlugin *self)
{
}
