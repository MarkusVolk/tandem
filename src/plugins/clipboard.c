/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "../clipboard.h"
#include "../device.h"
#include "../plugin.h"
#include "../util.h"

#include <adwaita.h>

#define TANDEM_TYPE_CLIPBOARD_PLUGIN (tandem_clipboard_plugin_get_type ())
G_DECLARE_FINAL_TYPE (TandemClipboardPlugin, tandem_clipboard_plugin, TANDEM, CLIPBOARD_PLUGIN, TandemPlugin)

struct _TandemClipboardPlugin
{
  TandemPlugin parent_instance;

  gulong changed_id;
  char *remote_text;
};

G_DEFINE_FINAL_TYPE (TandemClipboardPlugin, tandem_clipboard_plugin, TANDEM_TYPE_PLUGIN)

static const char *const clipboard_types[] = { "kdeconnect.clipboard", "kdeconnect.clipboard.connect", NULL };

static gboolean
auto_share (TandemPlugin *plugin)
{
  return g_settings_get_boolean (tandem_plugin_get_settings (plugin), "clipboard-auto");
}

void
tandem_clipboard_plugin_send (TandemPlugin *plugin, const char *text)
{
  g_autoptr(TandemPacket) packet = tandem_packet_new ("kdeconnect.clipboard");

  if (text == NULL)
    return;
  tandem_packet_set_string (packet, "content", text);
  tandem_plugin_send (plugin, packet);
}

static void
local_changed_cb (TandemClipboard *clipboard, const char *text, gpointer user_data)
{
  TandemClipboardPlugin *self = user_data;

  if (g_strcmp0 (text, self->remote_text) == 0)
    return;
  if (auto_share (TANDEM_PLUGIN (self)))
    tandem_clipboard_plugin_send (TANDEM_PLUGIN (self), text);
}

static void
clipboard_handle_packet (TandemPlugin *plugin, TandemPacket *packet)
{
  TandemClipboardPlugin *self = TANDEM_CLIPBOARD_PLUGIN (plugin);
  TandemClipboard *clipboard = tandem_clipboard_get ();
  const char *content = tandem_packet_get_string (packet, "content", NULL);

  if (content == NULL)
    return;
  if (tandem_packet_is_type (packet, "kdeconnect.clipboard.connect"))
    {
      gint64 timestamp = tandem_packet_get_int (packet, "timestamp", 0);

      if (timestamp == 0 || timestamp <= tandem_clipboard_get_timestamp (clipboard))
        return;
    }
  if (g_strcmp0 (content, tandem_clipboard_get_text (clipboard)) == 0)
    return;
  g_free (self->remote_text);
  self->remote_text = g_strdup (content);
  tandem_clipboard_set_text (clipboard, content);
}

static void
clipboard_connected (TandemPlugin *plugin)
{
  TandemClipboard *clipboard = tandem_clipboard_get ();
  const char *text = tandem_clipboard_get_text (clipboard);

  if (!auto_share (plugin) || text == NULL)
    return;
  {
    g_autoptr(TandemPacket) packet = tandem_packet_new ("kdeconnect.clipboard.connect");

    tandem_packet_set_string (packet, "content", text);
    tandem_packet_set_int (packet, "timestamp", tandem_clipboard_get_timestamp (clipboard));
    tandem_plugin_send (plugin, packet);
  }
}

static void
send_now_cb (AdwButtonRow *row, gpointer user_data)
{
  tandem_clipboard_plugin_send (TANDEM_PLUGIN (user_data), tandem_clipboard_get_text (tandem_clipboard_get ()));
}

static GtkWidget *
clipboard_create_group (TandemPlugin *plugin)
{
  GtkWidget *group = adw_preferences_group_new ();
  GtkWidget *auto_row = adw_switch_row_new ();
  GtkWidget *send_row = adw_button_row_new ();
  g_autofree char *description = g_strdup_printf ("Clipboard access through %s",
                                                  tandem_clipboard_backend_name (tandem_clipboard_get ()));

  adw_preferences_group_set_title (ADW_PREFERENCES_GROUP (group), "Clipboard");
  adw_preferences_group_set_description (ADW_PREFERENCES_GROUP (group), description);
  adw_preferences_row_set_title (ADW_PREFERENCES_ROW (auto_row), "Share automatically");
  g_settings_bind (tandem_plugin_get_settings (plugin), "clipboard-auto", auto_row, "active", G_SETTINGS_BIND_DEFAULT);
  adw_preferences_group_add (ADW_PREFERENCES_GROUP (group), auto_row);
  adw_preferences_row_set_title (ADW_PREFERENCES_ROW (send_row), "Send clipboard now");
  adw_button_row_set_start_icon_name (ADW_BUTTON_ROW (send_row), "edit-paste-symbolic");
  g_signal_connect_object (send_row, "activated", G_CALLBACK (send_now_cb), plugin, 0);
  adw_preferences_group_add (ADW_PREFERENCES_GROUP (group), send_row);
  return group;
}

static void
tandem_clipboard_plugin_constructed (GObject *object)
{
  TandemClipboardPlugin *self = TANDEM_CLIPBOARD_PLUGIN (object);

  G_OBJECT_CLASS (tandem_clipboard_plugin_parent_class)->constructed (object);
  self->changed_id = g_signal_connect (tandem_clipboard_get (), "changed", G_CALLBACK (local_changed_cb), self);
}

static void
tandem_clipboard_plugin_dispose (GObject *object)
{
  TandemClipboardPlugin *self = TANDEM_CLIPBOARD_PLUGIN (object);

  g_clear_signal_handler (&self->changed_id, tandem_clipboard_get ());
  G_OBJECT_CLASS (tandem_clipboard_plugin_parent_class)->dispose (object);
}

static void
tandem_clipboard_plugin_finalize (GObject *object)
{
  g_free (TANDEM_CLIPBOARD_PLUGIN (object)->remote_text);
  G_OBJECT_CLASS (tandem_clipboard_plugin_parent_class)->finalize (object);
}

static void
tandem_clipboard_plugin_class_init (TandemClipboardPluginClass *klass)
{
  TandemPluginClass *plugin_class = TANDEM_PLUGIN_CLASS (klass);
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->constructed = tandem_clipboard_plugin_constructed;
  object_class->dispose = tandem_clipboard_plugin_dispose;
  object_class->finalize = tandem_clipboard_plugin_finalize;
  plugin_class->name = "clipboard";
  plugin_class->title = "Clipboard";
  plugin_class->incoming = clipboard_types;
  plugin_class->outgoing = clipboard_types;
  plugin_class->handle_packet = clipboard_handle_packet;
  plugin_class->connected = clipboard_connected;
  plugin_class->create_group = clipboard_create_group;
}

static void
tandem_clipboard_plugin_init (TandemClipboardPlugin *self)
{
}
