/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "../device.h"
#include "../plugin.h"

#include <adwaita.h>

#define TANDEM_TYPE_REMOTEINPUT_PLUGIN (tandem_remoteinput_plugin_get_type ())
G_DECLARE_FINAL_TYPE (TandemRemoteinputPlugin, tandem_remoteinput_plugin, TANDEM, REMOTEINPUT_PLUGIN, TandemPlugin)

struct _TandemRemoteinputPlugin
{
  TandemPlugin parent_instance;

  gboolean keyboard_ready;
};

G_DEFINE_FINAL_TYPE (TandemRemoteinputPlugin, tandem_remoteinput_plugin, TANDEM_TYPE_PLUGIN)

static const char *const remoteinput_incoming[] = { "kdeconnect.mousepad.keyboardstate", NULL };
static const char *const remoteinput_outgoing[] = { "kdeconnect.mousepad.request", NULL };

void
tandem_remoteinput_plugin_send_text (TandemPlugin *plugin, const char *text)
{
  g_autoptr(TandemPacket) packet = tandem_packet_new ("kdeconnect.mousepad.request");

  tandem_packet_set_string (packet, "key", text);
  tandem_plugin_send (plugin, packet);
}

void
tandem_remoteinput_plugin_send_special (TandemPlugin *plugin, int special)
{
  g_autoptr(TandemPacket) packet = tandem_packet_new ("kdeconnect.mousepad.request");

  tandem_packet_set_int (packet, "specialKey", special);
  tandem_plugin_send (plugin, packet);
}

static void
remoteinput_handle_packet (TandemPlugin *plugin, TandemPacket *packet)
{
  TandemRemoteinputPlugin *self = TANDEM_REMOTEINPUT_PLUGIN (plugin);

  self->keyboard_ready = tandem_packet_get_boolean (packet, "state", FALSE);
  tandem_plugin_changed (plugin);
}

static void
entry_activate_cb (GtkEntry *entry, gpointer user_data)
{
  const char *text = gtk_editable_get_text (GTK_EDITABLE (entry));

  if (*text != '\0')
    tandem_remoteinput_plugin_send_text (TANDEM_PLUGIN (user_data), text);
  gtk_editable_set_text (GTK_EDITABLE (entry), "");
}

static void
special_clicked_cb (GtkButton *button, gpointer user_data)
{
  tandem_remoteinput_plugin_send_special (TANDEM_PLUGIN (user_data),
                                          GPOINTER_TO_INT (g_object_get_data (G_OBJECT (button), "special")));
}

static GtkWidget *
special_button (TandemPlugin *plugin, const char *label, int special)
{
  GtkWidget *button = gtk_button_new_with_label (label);

  g_object_set_data (G_OBJECT (button), "special", GINT_TO_POINTER (special));
  g_signal_connect_object (button, "clicked", G_CALLBACK (special_clicked_cb), plugin, 0);
  gtk_widget_set_valign (button, GTK_ALIGN_CENTER);
  return button;
}

static GtkWidget *
remoteinput_create_group (TandemPlugin *plugin)
{
  TandemRemoteinputPlugin *self = TANDEM_REMOTEINPUT_PLUGIN (plugin);
  GtkWidget *group = adw_preferences_group_new ();
  GtkWidget *entry_row = adw_entry_row_new ();
  GtkWidget *keys_row = adw_action_row_new ();
  GtkWidget *keys_box = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 6);

  adw_preferences_group_set_title (ADW_PREFERENCES_GROUP (group), "Type on the device");
  adw_preferences_group_set_description (ADW_PREFERENCES_GROUP (group),
                                         self->keyboard_ready ? "The device accepts keyboard input"
                                                              : "Open a text field on the device first");
  adw_preferences_row_set_title (ADW_PREFERENCES_ROW (entry_row), "Text to send");
  adw_entry_row_set_show_apply_button (ADW_ENTRY_ROW (entry_row), TRUE);
  g_signal_connect_object (entry_row, "apply", G_CALLBACK (entry_activate_cb), plugin, 0);
  g_signal_connect_object (entry_row, "entry-activated", G_CALLBACK (entry_activate_cb), plugin, 0);
  adw_preferences_group_add (ADW_PREFERENCES_GROUP (group), entry_row);

  adw_preferences_row_set_title (ADW_PREFERENCES_ROW (keys_row), "Keys");
  gtk_box_append (GTK_BOX (keys_box), special_button (plugin, "Backspace", 1));
  gtk_box_append (GTK_BOX (keys_box), special_button (plugin, "Enter", 12));
  gtk_box_append (GTK_BOX (keys_box), special_button (plugin, "Tab", 2));
  gtk_box_append (GTK_BOX (keys_box), special_button (plugin, "Esc", 14));
  adw_action_row_add_suffix (ADW_ACTION_ROW (keys_row), keys_box);
  adw_preferences_group_add (ADW_PREFERENCES_GROUP (group), keys_row);
  return group;
}

static void
tandem_remoteinput_plugin_class_init (TandemRemoteinputPluginClass *klass)
{
  TandemPluginClass *plugin_class = TANDEM_PLUGIN_CLASS (klass);

  plugin_class->name = "remoteinput";
  plugin_class->title = "Remote keyboard";
  plugin_class->incoming = remoteinput_incoming;
  plugin_class->outgoing = remoteinput_outgoing;
  plugin_class->handle_packet = remoteinput_handle_packet;
  plugin_class->create_group = remoteinput_create_group;
}

static void
tandem_remoteinput_plugin_init (TandemRemoteinputPlugin *self)
{
}
