/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "../device.h"
#include "../input.h"
#include "../plugin.h"

#include <adwaita.h>

#define SCROLL_FACTOR 0.1

#define TANDEM_TYPE_MOUSEPAD_PLUGIN (tandem_mousepad_plugin_get_type ())
G_DECLARE_FINAL_TYPE (TandemMousepadPlugin, tandem_mousepad_plugin, TANDEM, MOUSEPAD_PLUGIN, TandemPlugin)

struct _TandemMousepadPlugin
{
  TandemPlugin parent_instance;
};

G_DEFINE_FINAL_TYPE (TandemMousepadPlugin, tandem_mousepad_plugin, TANDEM_TYPE_PLUGIN)

static const char *const mousepad_incoming[] = { "kdeconnect.mousepad.request", NULL };
static const char *const mousepad_outgoing[] = { "kdeconnect.mousepad.echo", "kdeconnect.mousepad.keyboardstate", NULL };

static TandemModifiers
modifiers_of (TandemPacket *packet)
{
  TandemModifiers mods = 0;

  if (tandem_packet_get_boolean (packet, "ctrl", FALSE))
    mods |= TANDEM_MOD_CTRL;
  if (tandem_packet_get_boolean (packet, "alt", FALSE))
    mods |= TANDEM_MOD_ALT;
  if (tandem_packet_get_boolean (packet, "shift", FALSE))
    mods |= TANDEM_MOD_SHIFT;
  if (tandem_packet_get_boolean (packet, "super", FALSE))
    mods |= TANDEM_MOD_SUPER;
  return mods;
}

static void
send_keyboard_state (TandemPlugin *plugin)
{
  g_autoptr(TandemPacket) packet = tandem_packet_new ("kdeconnect.mousepad.keyboardstate");

  tandem_packet_set_boolean (packet, "state", tandem_input_has_keyboard ());
  tandem_plugin_send (plugin, packet);
}

static void
mousepad_handle_packet (TandemPlugin *plugin, TandemPacket *packet)
{
  const char *key = tandem_packet_get_string (packet, "key", NULL);
  int special = (int) tandem_packet_get_int (packet, "specialKey", 0);
  double dx = tandem_packet_get_double (packet, "dx", 0);
  double dy = tandem_packet_get_double (packet, "dy", 0);

  if (tandem_packet_get_boolean (packet, "singleclick", FALSE))
    tandem_input_click (TANDEM_BTN_LEFT, 1);
  if (tandem_packet_get_boolean (packet, "doubleclick", FALSE))
    tandem_input_click (TANDEM_BTN_LEFT, 2);
  if (tandem_packet_get_boolean (packet, "middleclick", FALSE))
    tandem_input_click (TANDEM_BTN_MIDDLE, 1);
  if (tandem_packet_get_boolean (packet, "rightclick", FALSE))
    tandem_input_click (TANDEM_BTN_RIGHT, 1);
  if (tandem_packet_get_boolean (packet, "singlehold", FALSE))
    tandem_input_button (TANDEM_BTN_LEFT, TRUE);
  if (tandem_packet_get_boolean (packet, "singlerelease", FALSE))
    tandem_input_button (TANDEM_BTN_LEFT, FALSE);

  if (dx != 0 || dy != 0)
    {
      if (tandem_packet_get_boolean (packet, "scroll", FALSE))
        tandem_input_pointer_scroll (dx * SCROLL_FACTOR, -dy * SCROLL_FACTOR);
      else
        tandem_input_pointer_motion (dx, dy);
    }

  if (key != NULL && *key != '\0')
    tandem_input_type_text (key, modifiers_of (packet));
  else if (special > 0)
    tandem_input_special_key (special, modifiers_of (packet));

  if (tandem_packet_get_boolean (packet, "sendAck", FALSE))
    {
      g_autoptr(TandemPacket) echo = tandem_packet_new ("kdeconnect.mousepad.echo");
      JsonObjectIter iter;
      const char *member;
      JsonNode *node;

      json_object_iter_init (&iter, packet->body);
      while (json_object_iter_next (&iter, &member, &node))
        json_object_set_member (echo->body, member, json_node_copy (node));
      json_object_remove_member (echo->body, "sendAck");
      tandem_packet_set_boolean (echo, "isAck", TRUE);
      tandem_plugin_send (plugin, echo);
    }
}

static void
mousepad_connected (TandemPlugin *plugin)
{
  send_keyboard_state (plugin);
}

static void
mousepad_disconnected (TandemPlugin *plugin)
{
  tandem_input_release ();
}

static GtkWidget *
mousepad_create_group (TandemPlugin *plugin)
{
  GtkWidget *group = adw_preferences_group_new ();
  GtkWidget *row = adw_action_row_new ();
  g_autofree char *subtitle = NULL;

  adw_preferences_group_set_title (ADW_PREFERENCES_GROUP (group), "Remote input");
  adw_preferences_row_set_title (ADW_PREFERENCES_ROW (row), "Backend");
  if (tandem_input_available ())
    subtitle = g_strdup_printf ("%s, keyboard %s, pointer %s", tandem_input_backend_name (),
                                tandem_input_has_keyboard () ? "yes" : "no",
                                tandem_input_has_pointer () ? "yes" : "no");
  else
    subtitle = g_strdup ("No input backend available");
  adw_action_row_set_subtitle (ADW_ACTION_ROW (row), subtitle);
  adw_preferences_group_add (ADW_PREFERENCES_GROUP (group), row);
  return group;
}

static void
tandem_mousepad_plugin_class_init (TandemMousepadPluginClass *klass)
{
  TandemPluginClass *plugin_class = TANDEM_PLUGIN_CLASS (klass);

  plugin_class->name = "mousepad";
  plugin_class->title = "Remote input";
  plugin_class->incoming = mousepad_incoming;
  plugin_class->outgoing = mousepad_outgoing;
  plugin_class->handle_packet = mousepad_handle_packet;
  plugin_class->connected = mousepad_connected;
  plugin_class->disconnected = mousepad_disconnected;
  plugin_class->create_group = mousepad_create_group;
}

static void
tandem_mousepad_plugin_init (TandemMousepadPlugin *self)
{
}
