/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "input.h"

#include <string.h>
#include <xkbcommon/xkbcommon.h>

static const guint32 special_keysyms[] = {
  0,
  XKB_KEY_BackSpace,
  XKB_KEY_Tab,
  XKB_KEY_Linefeed,
  XKB_KEY_Left,
  XKB_KEY_Up,
  XKB_KEY_Right,
  XKB_KEY_Down,
  XKB_KEY_Page_Up,
  XKB_KEY_Page_Down,
  XKB_KEY_Home,
  XKB_KEY_End,
  XKB_KEY_Return,
  XKB_KEY_Delete,
  XKB_KEY_Escape,
  XKB_KEY_Print,
  XKB_KEY_Scroll_Lock,
  XKB_KEY_Control_L,
  XKB_KEY_Alt_L,
  XKB_KEY_Shift_L,
  XKB_KEY_Super_L,
  XKB_KEY_F1, XKB_KEY_F2, XKB_KEY_F3, XKB_KEY_F4, XKB_KEY_F5, XKB_KEY_F6,
  XKB_KEY_F7, XKB_KEY_F8, XKB_KEY_F9, XKB_KEY_F10, XKB_KEY_F11, XKB_KEY_F12,
};

static TandemInputBackend *current;
static gboolean selected;
static guint idle_close_id;

static gboolean
idle_close_cb (gpointer user_data)
{
  idle_close_id = 0;
  if (current != NULL && current->opened && current->close != NULL)
    {
      current->close (current);
      current->opened = FALSE;
    }
  return G_SOURCE_REMOVE;
}

static void
touch (void)
{
  g_clear_handle_id (&idle_close_id, g_source_remove);
  idle_close_id = g_timeout_add_seconds (300, idle_close_cb, NULL);
}

static TandemInputBackend *
select_backend (void)
{
  g_autoptr(GSettings) settings = g_settings_new ("de.flk.Tandem");
  g_autofree char *wanted = g_settings_get_string (settings, "input-backend");
  TandemInputBackend *candidates[] = {
    tandem_input_portal_backend (),
    tandem_input_wlr_backend (),
    tandem_input_uinput_backend (),
  };

  for (guint i = 0; i < G_N_ELEMENTS (candidates); i++)
    {
      TandemInputBackend *backend = candidates[i];

      if (g_strcmp0 (wanted, "auto") != 0 && g_strcmp0 (wanted, backend->name) != 0)
        continue;
      if (backend->available ())
        {
          g_message ("remote input through %s", backend->name);
          return backend;
        }
    }
  g_message ("no remote input backend available");
  return NULL;
}

static TandemInputBackend *
backend (void)
{
  if (!selected)
    {
      selected = TRUE;
      current = select_backend ();
    }
  return current;
}

static gboolean
ensure_open (void)
{
  TandemInputBackend *b = backend ();
  g_autoptr(GError) error = NULL;

  if (b == NULL)
    return FALSE;
  if (!b->opened)
    {
      if (!b->open (b, &error))
        {
          g_warning ("remote input: cannot open %s: %s", b->name, error->message);
          return FALSE;
        }
      b->opened = TRUE;
    }
  touch ();
  return TRUE;
}

gboolean
tandem_input_available (void)
{
  return backend () != NULL;
}

gboolean
tandem_input_has_keyboard (void)
{
  TandemInputBackend *b = backend ();

  return b != NULL && b->has_keyboard;
}

gboolean
tandem_input_has_pointer (void)
{
  TandemInputBackend *b = backend ();

  return b != NULL && b->has_pointer;
}

const char *
tandem_input_backend_name (void)
{
  TandemInputBackend *b = backend ();

  return b != NULL ? b->name : "none";
}

static void
flush (void)
{
  if (current != NULL && current->flush != NULL)
    current->flush (current);
}

void
tandem_input_pointer_motion (double dx, double dy)
{
  if (!ensure_open () || !current->has_pointer)
    return;
  current->pointer_motion (current, dx, dy);
  flush ();
}

void
tandem_input_pointer_scroll (double dx, double dy)
{
  if (!ensure_open () || !current->has_pointer)
    return;
  current->pointer_scroll (current, dx, dy);
  flush ();
}

void
tandem_input_button (guint32 button, gboolean pressed)
{
  if (!ensure_open () || !current->has_pointer)
    return;
  current->pointer_button (current, button, pressed);
  flush ();
}

void
tandem_input_click (guint32 button, int times)
{
  if (!ensure_open () || !current->has_pointer)
    return;
  for (int i = 0; i < times; i++)
    {
      current->pointer_button (current, button, TRUE);
      current->pointer_button (current, button, FALSE);
    }
  flush ();
}

static void
press_modifiers (TandemModifiers modifiers, gboolean pressed)
{
  if (modifiers & TANDEM_MOD_CTRL)
    current->keysym (current, XKB_KEY_Control_L, pressed);
  if (modifiers & TANDEM_MOD_ALT)
    current->keysym (current, XKB_KEY_Alt_L, pressed);
  if (modifiers & TANDEM_MOD_SHIFT)
    current->keysym (current, XKB_KEY_Shift_L, pressed);
  if (modifiers & TANDEM_MOD_SUPER)
    current->keysym (current, XKB_KEY_Super_L, pressed);
}

void
tandem_input_keysym (guint32 keysym, TandemModifiers modifiers)
{
  if (keysym == 0 || !ensure_open () || !current->has_keyboard)
    return;
  press_modifiers (modifiers, TRUE);
  current->keysym (current, keysym, TRUE);
  current->keysym (current, keysym, FALSE);
  press_modifiers (modifiers, FALSE);
  flush ();
}

void
tandem_input_type_text (const char *text, TandemModifiers modifiers)
{
  if (text == NULL || !ensure_open () || !current->has_keyboard)
    return;
  press_modifiers (modifiers, TRUE);
  for (const char *p = text; *p != '\0'; p = g_utf8_next_char (p))
    {
      gunichar ch = g_utf8_get_char (p);
      guint32 keysym = xkb_utf32_to_keysym (ch);

      if (ch == '\n')
        keysym = XKB_KEY_Return;
      if (keysym == 0)
        continue;
      current->keysym (current, keysym, TRUE);
      current->keysym (current, keysym, FALSE);
    }
  press_modifiers (modifiers, FALSE);
  flush ();
}

void
tandem_input_special_key (int special, TandemModifiers modifiers)
{
  if (special <= 0 || special >= (int) G_N_ELEMENTS (special_keysyms))
    return;
  tandem_input_keysym (special_keysyms[special], modifiers);
}

void
tandem_input_release (void)
{
  idle_close_cb (NULL);
  g_clear_handle_id (&idle_close_id, g_source_remove);
}
