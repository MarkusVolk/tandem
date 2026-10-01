/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "../device.h"
#include "../mpris-watch.h"
#include "../notify.h"
#include "../plugin.h"
#include "../pulse.h"
#include "../util.h"
#include "contacts.h"

#include <adwaita.h>
#include <glib/gstdio.h>

#define TANDEM_TYPE_TELEPHONY_PLUGIN (tandem_telephony_plugin_get_type ())
G_DECLARE_FINAL_TYPE (TandemTelephonyPlugin, tandem_telephony_plugin, TANDEM, TELEPHONY_PLUGIN, TandemPlugin)

struct _TandemTelephonyPlugin
{
  TandemPlugin parent_instance;

  guint32 notification_id;
  char *thumbnail_path;
  GPtrArray *paused_players;
  char *muted_sink;
  gboolean in_call;
};

G_DEFINE_FINAL_TYPE (TandemTelephonyPlugin, tandem_telephony_plugin, TANDEM_TYPE_PLUGIN)

static const char *const telephony_incoming[] = { "kdeconnect.telephony", NULL };
static const char *const telephony_outgoing[] = { "kdeconnect.telephony.request_mute", NULL };

static void
pause_media (TandemTelephonyPlugin *self)
{
  GSettings *settings = tandem_plugin_get_settings (TANDEM_PLUGIN (self));
  TandemMprisWatch *watch = tandem_mpris_watch_get ();
  TandemPulse *pulse = tandem_pulse_get ();
  g_autoptr(GList) players = NULL;

  if (self->in_call)
    return;
  self->in_call = TRUE;
  if (g_settings_get_boolean (settings, "pause-media-on-call"))
    {
      players = tandem_mpris_watch_list (watch);
      for (GList *l = players; l != NULL; l = l->next)
        {
          TandemMprisPlayer *player = tandem_mpris_watch_lookup (watch, l->data);

          if (player != NULL &&
              g_str_equal (tandem_mpris_player_get_string (player, "PlaybackStatus", ""), "Playing") &&
              tandem_mpris_player_get_boolean (player, "CanPause", TRUE))
            {
              tandem_mpris_player_call (player, "Pause", NULL);
              g_ptr_array_add (self->paused_players, g_strdup (l->data));
            }
        }
    }
  if (g_settings_get_boolean (settings, "mute-on-call") && tandem_pulse_is_ready (pulse))
    {
      GPtrArray *sinks = tandem_pulse_get_sinks (pulse);

      for (guint i = 0; i < sinks->len; i++)
        {
          TandemSink *sink = g_ptr_array_index (sinks, i);

          if (sink->is_default && !sink->muted)
            {
              tandem_pulse_set_muted (pulse, sink->name, TRUE);
              g_free (self->muted_sink);
              self->muted_sink = g_strdup (sink->name);
            }
        }
    }
}

static void
resume_media (TandemTelephonyPlugin *self)
{
  TandemMprisWatch *watch = tandem_mpris_watch_get ();
  TandemPulse *pulse = tandem_pulse_get ();

  if (!self->in_call)
    return;
  self->in_call = FALSE;
  for (guint i = 0; i < self->paused_players->len; i++)
    {
      TandemMprisPlayer *player = tandem_mpris_watch_lookup (watch, g_ptr_array_index (self->paused_players, i));

      if (player != NULL)
        tandem_mpris_player_call (player, "Play", NULL);
    }
  g_ptr_array_set_size (self->paused_players, 0);
  if (self->muted_sink != NULL)
    {
      tandem_pulse_set_muted (pulse, self->muted_sink, FALSE);
      g_clear_pointer (&self->muted_sink, g_free);
    }
}

static void
notification_action_cb (guint32 id, const char *action, const char *reply, gpointer user_data)
{
  TandemTelephonyPlugin *self = user_data;

  if (g_str_equal (action, "mute"))
    {
      g_autoptr(TandemPacket) packet = tandem_packet_new ("kdeconnect.telephony.request_mute");

      tandem_plugin_send (TANDEM_PLUGIN (self), packet);
    }
}

static void
notification_closed_cb (guint32 id, guint32 reason, gpointer user_data)
{
  TandemTelephonyPlugin *self = user_data;

  if (self->notification_id == id)
    self->notification_id = 0;
}

static void
notification_sent_cb (GObject *source, GAsyncResult *result, gpointer user_data)
{
  TandemTelephonyPlugin *self = user_data;

  self->notification_id = tandem_notification_send_finish (result, NULL);
  g_object_unref (self);
}

static char *
save_thumbnail (TandemTelephonyPlugin *self, const char *base64)
{
  gsize len = 0;
  g_autofree guchar *data = g_base64_decode (base64, &len);
  g_autofree char *dir = g_build_filename (g_get_user_cache_dir (), "tandem",
                                           tandem_device_get_id (tandem_plugin_get_device (TANDEM_PLUGIN (self))), NULL);
  g_autofree char *path = g_build_filename (dir, "caller.jpg", NULL);

  if (len == 0)
    return NULL;
  tandem_ensure_dir (dir, NULL);
  if (!tandem_write_file (path, (const char *) data, len, 0600, NULL))
    return NULL;
  return g_steal_pointer (&path);
}

static void
telephony_handle_packet (TandemPlugin *plugin, TandemPacket *packet)
{
  TandemTelephonyPlugin *self = TANDEM_TELEPHONY_PLUGIN (plugin);
  TandemDevice *device = tandem_plugin_get_device (plugin);
  const char *event = tandem_packet_get_string (packet, "event", "");
  const char *number = tandem_packet_get_string (packet, "phoneNumber", NULL);
  const char *contact_name = tandem_packet_get_string (packet, "contactName", NULL);
  const char *thumbnail = tandem_packet_get_string (packet, "phoneThumbnail", NULL);
  gboolean cancel = tandem_packet_get_boolean (packet, "isCancel", FALSE);
  TandemContact *contact = tandem_device_lookup_contact (device, number);
  g_autofree char *who = NULL;
  g_autofree char *summary = NULL;
  g_autofree char *photo = NULL;
  TandemNotification *notification;

  if (cancel)
    {
      if (g_str_equal (event, "talking") || g_str_equal (event, "ringing"))
        resume_media (self);
      if (self->notification_id != 0)
        {
          tandem_notify_close (self->notification_id);
          self->notification_id = 0;
        }
      return;
    }

  if (contact_name != NULL && *contact_name != '\0')
    who = g_strdup (contact_name);
  else if (contact != NULL)
    who = g_strdup (contact->name);
  else
    who = g_strdup (number != NULL && *number != '\0' ? number : "Unknown caller");

  if (g_str_equal (event, "ringing"))
    summary = g_strdup_printf ("Incoming call from %s", who);
  else if (g_str_equal (event, "talking"))
    summary = g_strdup_printf ("Call with %s", who);
  else if (g_str_equal (event, "missedCall"))
    summary = g_strdup_printf ("Missed call from %s", who);
  else if (g_str_equal (event, "sms"))
    return;
  else
    summary = g_strdup_printf ("%s: %s", event, who);

  if (g_str_equal (event, "ringing") || g_str_equal (event, "talking"))
    pause_media (self);

  notification = tandem_notification_new (summary, tandem_device_get_name (device));
  tandem_notification_set_icon (notification, "call-start-symbolic");
  tandem_notification_set_category (notification, "im");
  if (thumbnail != NULL && *thumbnail != '\0')
    photo = save_thumbnail (self, thumbnail);
  if (photo == NULL && contact != NULL)
    photo = tandem_contacts_plugin_photo_path (TANDEM_CONTACTS_PLUGIN (tandem_device_get_plugin (device, "contacts")), contact);
  if (photo != NULL)
    tandem_notification_set_image (notification, photo);
  if (self->notification_id != 0)
    tandem_notification_set_replaces (notification, self->notification_id);
  if (g_str_equal (event, "ringing"))
    {
      tandem_notification_set_urgency (notification, 2);
      tandem_notification_set_timeout (notification, 0);
      tandem_notification_add_action (notification, "mute", "Mute");
    }
  tandem_notification_set_callbacks (notification, notification_action_cb, notification_closed_cb, self, NULL);
  tandem_notification_send_async (notification, notification_sent_cb, g_object_ref (self));
}

static void
telephony_disconnected (TandemPlugin *plugin)
{
  resume_media (TANDEM_TELEPHONY_PLUGIN (plugin));
}

static GtkWidget *
telephony_create_group (TandemPlugin *plugin)
{
  GSettings *settings = tandem_plugin_get_settings (plugin);
  GtkWidget *group = adw_preferences_group_new ();
  GtkWidget *pause_row = adw_switch_row_new ();
  GtkWidget *mute_row = adw_switch_row_new ();

  adw_preferences_group_set_title (ADW_PREFERENCES_GROUP (group), "Telephony");
  adw_preferences_row_set_title (ADW_PREFERENCES_ROW (pause_row), "Pause media during calls");
  g_settings_bind (settings, "pause-media-on-call", pause_row, "active", G_SETTINGS_BIND_DEFAULT);
  adw_preferences_group_add (ADW_PREFERENCES_GROUP (group), pause_row);
  adw_preferences_row_set_title (ADW_PREFERENCES_ROW (mute_row), "Mute the speakers during calls");
  g_settings_bind (settings, "mute-on-call", mute_row, "active", G_SETTINGS_BIND_DEFAULT);
  adw_preferences_group_add (ADW_PREFERENCES_GROUP (group), mute_row);
  return group;
}

static void
tandem_telephony_plugin_finalize (GObject *object)
{
  TandemTelephonyPlugin *self = TANDEM_TELEPHONY_PLUGIN (object);

  g_ptr_array_unref (self->paused_players);
  g_free (self->muted_sink);
  g_free (self->thumbnail_path);
  G_OBJECT_CLASS (tandem_telephony_plugin_parent_class)->finalize (object);
}

static void
tandem_telephony_plugin_class_init (TandemTelephonyPluginClass *klass)
{
  TandemPluginClass *plugin_class = TANDEM_PLUGIN_CLASS (klass);

  G_OBJECT_CLASS (klass)->finalize = tandem_telephony_plugin_finalize;
  plugin_class->name = "telephony";
  plugin_class->title = "Telephony";
  plugin_class->incoming = telephony_incoming;
  plugin_class->outgoing = telephony_outgoing;
  plugin_class->handle_packet = telephony_handle_packet;
  plugin_class->disconnected = telephony_disconnected;
  plugin_class->create_group = telephony_create_group;
}

static void
tandem_telephony_plugin_init (TandemTelephonyPlugin *self)
{
  self->paused_players = g_ptr_array_new_with_free_func (g_free);
}
