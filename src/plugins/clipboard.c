/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "../clipboard.h"
#include "../device.h"
#include "../plugin.h"
#include "../util.h"


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

static GVariant *
clipboard_get_state (TandemPlugin *plugin)
{
  GVariantBuilder builder;

  g_variant_builder_init (&builder, G_VARIANT_TYPE ("a{sv}"));
  g_variant_builder_add (&builder, "{sv}", "backend",
                         g_variant_new_string (tandem_clipboard_backend_name (tandem_clipboard_get ())));
  return g_variant_builder_end (&builder);
}

static GVariant *
clipboard_call (TandemPlugin *plugin, const char *method, GVariant *args, GError **error)
{
  if (g_str_equal (method, "send"))
    {
      tandem_clipboard_plugin_send (plugin, tandem_clipboard_get_text (tandem_clipboard_get ()));
      return NULL;
    }
  g_set_error (error, G_DBUS_ERROR, G_DBUS_ERROR_UNKNOWN_METHOD, "Unknown method %s", method);
  return NULL;
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
  plugin_class->get_state = clipboard_get_state;
  plugin_class->call = clipboard_call;
}

static void
tandem_clipboard_plugin_init (TandemClipboardPlugin *self)
{
}
