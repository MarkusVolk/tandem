/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "input.h"

#include "wayland.h"

#include <string.h>
#include <sys/mman.h>
#include <unistd.h>
#include <wayland-client.h>
#include <xkbcommon/xkbcommon.h>

#include "virtual-keyboard-unstable-v1-client-protocol.h"
#include "wlr-virtual-pointer-unstable-v1-client-protocol.h"

#define MAX_KEYS 240
#define FIRST_KEYCODE 9

#define KEY_SHIFT_INDEX 0
#define KEY_CTRL_INDEX 1
#define KEY_ALT_INDEX 2
#define KEY_SUPER_INDEX 3

typedef struct
{
  TandemWayland *wayland;
  struct zwp_virtual_keyboard_v1 *keyboard;
  struct zwlr_virtual_pointer_v1 *pointer;
  guint32 keysyms[MAX_KEYS];
  guint n_keys;
  guint32 mods_depressed;
} Wlr;

static gboolean
probe (void)
{
  TandemWayland *wayland = tandem_wayland_get ();

  return wayland != NULL && wayland->seat != NULL &&
         (wayland->virtual_keyboard_manager != NULL || wayland->virtual_pointer_manager != NULL);
}

static guint32
timestamp (void)
{
  return (guint32) (g_get_monotonic_time () / 1000);
}

static void
upload_keymap (Wlr *wlr)
{
  g_autoptr(GString) map = g_string_new ("xkb_keymap {\nxkb_keycodes \"tandem\" {\nminimum = 8;\nmaximum = 255;\n");
  int fd;
  gsize size;

  for (guint i = 0; i < wlr->n_keys; i++)
    g_string_append_printf (map, "<K%u> = %u;\n", i, FIRST_KEYCODE + i);
  g_string_append (map, "};\nxkb_types \"tandem\" { include \"complete\" };\n"
                        "xkb_compatibility \"tandem\" { include \"complete\" };\n"
                        "xkb_symbols \"tandem\" {\n");
  for (guint i = 0; i < wlr->n_keys; i++)
    {
      char name[64];

      xkb_keysym_get_name (wlr->keysyms[i], name, sizeof name);
      g_string_append_printf (map, "key <K%u> { [ %s ] };\n", i, name);
    }
  g_string_append_printf (map, "modifier_map Shift { <K%d> };\nmodifier_map Control { <K%d> };\n"
                               "modifier_map Mod1 { <K%d> };\nmodifier_map Mod4 { <K%d> };\n};\n};\n",
                          KEY_SHIFT_INDEX, KEY_CTRL_INDEX, KEY_ALT_INDEX, KEY_SUPER_INDEX);

  size = map->len + 1;
  fd = memfd_create ("tandem-keymap", MFD_CLOEXEC);
  if (fd < 0)
    return;
  if (ftruncate (fd, size) == 0 && write (fd, map->str, size) == (gssize) size)
    zwp_virtual_keyboard_v1_keymap (wlr->keyboard, WL_KEYBOARD_KEYMAP_FORMAT_XKB_V1, fd, size);
  close (fd);
}

static int
keycode_for (Wlr *wlr, guint32 keysym)
{
  for (guint i = 0; i < wlr->n_keys; i++)
    if (wlr->keysyms[i] == keysym)
      return FIRST_KEYCODE + i;
  if (wlr->n_keys >= MAX_KEYS)
    return -1;
  wlr->keysyms[wlr->n_keys++] = keysym;
  upload_keymap (wlr);
  return FIRST_KEYCODE + wlr->n_keys - 1;
}

static gboolean
wlr_available (void)
{
  return probe ();
}

static gboolean
wlr_open (TandemInputBackend *backend, GError **error)
{
  Wlr *wlr;

  if (!probe ())
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED, "No virtual input protocols on this compositor");
      return FALSE;
    }
  wlr = g_new0 (Wlr, 1);
  wlr->wayland = tandem_wayland_get ();
  if (wlr->wayland->virtual_keyboard_manager != NULL)
    {
      wlr->keyboard = zwp_virtual_keyboard_manager_v1_create_virtual_keyboard (wlr->wayland->virtual_keyboard_manager,
                                                                               wlr->wayland->seat);
      wlr->keysyms[KEY_SHIFT_INDEX] = XKB_KEY_Shift_L;
      wlr->keysyms[KEY_CTRL_INDEX] = XKB_KEY_Control_L;
      wlr->keysyms[KEY_ALT_INDEX] = XKB_KEY_Alt_L;
      wlr->keysyms[KEY_SUPER_INDEX] = XKB_KEY_Super_L;
      wlr->n_keys = 4;
      upload_keymap (wlr);
    }
  if (wlr->wayland->virtual_pointer_manager != NULL)
    wlr->pointer = zwlr_virtual_pointer_manager_v1_create_virtual_pointer (wlr->wayland->virtual_pointer_manager,
                                                                           wlr->wayland->seat);
  backend->has_keyboard = wlr->keyboard != NULL;
  backend->has_pointer = wlr->pointer != NULL;
  backend->data = wlr;
  tandem_wayland_flush (wlr->wayland);
  return TRUE;
}

static void
wlr_close (TandemInputBackend *backend)
{
  Wlr *wlr = backend->data;

  if (wlr == NULL)
    return;
  g_clear_pointer (&wlr->keyboard, zwp_virtual_keyboard_v1_destroy);
  g_clear_pointer (&wlr->pointer, zwlr_virtual_pointer_v1_destroy);
  tandem_wayland_flush (wlr->wayland);
  g_free (wlr);
  backend->data = NULL;
}

static void
wlr_pointer_motion (TandemInputBackend *backend, double dx, double dy)
{
  Wlr *wlr = backend->data;

  zwlr_virtual_pointer_v1_motion (wlr->pointer, timestamp (), wl_fixed_from_double (dx), wl_fixed_from_double (dy));
  zwlr_virtual_pointer_v1_frame (wlr->pointer);
}

static void
wlr_pointer_button (TandemInputBackend *backend, guint32 button, gboolean pressed)
{
  Wlr *wlr = backend->data;

  zwlr_virtual_pointer_v1_button (wlr->pointer, timestamp (), button,
                                  pressed ? WL_POINTER_BUTTON_STATE_PRESSED : WL_POINTER_BUTTON_STATE_RELEASED);
  zwlr_virtual_pointer_v1_frame (wlr->pointer);
}

static void
wlr_pointer_scroll (TandemInputBackend *backend, double dx, double dy)
{
  Wlr *wlr = backend->data;
  guint32 now = timestamp ();

  zwlr_virtual_pointer_v1_axis_source (wlr->pointer, WL_POINTER_AXIS_SOURCE_WHEEL);
  if (dy != 0)
    zwlr_virtual_pointer_v1_axis (wlr->pointer, now, WL_POINTER_AXIS_VERTICAL_SCROLL, wl_fixed_from_double (dy));
  if (dx != 0)
    zwlr_virtual_pointer_v1_axis (wlr->pointer, now, WL_POINTER_AXIS_HORIZONTAL_SCROLL, wl_fixed_from_double (dx));
  zwlr_virtual_pointer_v1_frame (wlr->pointer);
}

static guint32
modifier_bit (guint32 keysym)
{
  switch (keysym)
    {
    case XKB_KEY_Shift_L: case XKB_KEY_Shift_R: return 1 << 0;
    case XKB_KEY_Control_L: case XKB_KEY_Control_R: return 1 << 2;
    case XKB_KEY_Alt_L: case XKB_KEY_Alt_R: return 1 << 3;
    case XKB_KEY_Super_L: case XKB_KEY_Super_R: return 1 << 6;
    default: return 0;
    }
}

static void
wlr_keysym (TandemInputBackend *backend, guint32 keysym, gboolean pressed)
{
  Wlr *wlr = backend->data;
  int keycode = keycode_for (wlr, keysym);
  guint32 bit = modifier_bit (keysym);

  if (keycode < 0)
    return;
  if (bit != 0)
    {
      if (pressed)
        wlr->mods_depressed |= bit;
      else
        wlr->mods_depressed &= ~bit;
    }
  zwp_virtual_keyboard_v1_key (wlr->keyboard, timestamp (), keycode - 8,
                               pressed ? WL_KEYBOARD_KEY_STATE_PRESSED : WL_KEYBOARD_KEY_STATE_RELEASED);
  if (bit != 0)
    zwp_virtual_keyboard_v1_modifiers (wlr->keyboard, wlr->mods_depressed, 0, 0, 0);
}

static void
wlr_flush (TandemInputBackend *backend)
{
  Wlr *wlr = backend->data;

  if (wlr != NULL)
    tandem_wayland_flush (wlr->wayland);
}

static TandemInputBackend wlr_backend = {
  .name = "wlr",
  .available = wlr_available,
  .open = wlr_open,
  .close = wlr_close,
  .pointer_motion = wlr_pointer_motion,
  .pointer_button = wlr_pointer_button,
  .pointer_scroll = wlr_pointer_scroll,
  .keysym = wlr_keysym,
  .flush = wlr_flush,
  .has_keyboard = TRUE,
  .has_pointer = TRUE,
};

TandemInputBackend *
tandem_input_wlr_backend (void)
{
  return &wlr_backend;
}
