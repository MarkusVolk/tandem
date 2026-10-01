/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "input.h"

#include <fcntl.h>
#include <linux/input.h>
#include <linux/uinput.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <xkbcommon/xkbcommon.h>

#define UINPUT_PATH "/dev/uinput"

typedef struct
{
  int fd;
  struct xkb_keymap *keymap;
  gboolean shift_held;
} Uinput;

static gboolean
uinput_available (void)
{
  return access (UINPUT_PATH, W_OK) == 0;
}

static void
emit (Uinput *ui, guint16 type, guint16 code, gint32 value)
{
  struct input_event event = { 0 };

  event.type = type;
  event.code = code;
  event.value = value;
  if (write (ui->fd, &event, sizeof event) != sizeof event)
    g_debug ("uinput: write failed");
}

static gboolean
uinput_open (TandemInputBackend *backend, GError **error)
{
  Uinput *ui = g_new0 (Uinput, 1);
  struct uinput_setup setup = { 0 };
  struct xkb_context *context;
  struct xkb_rule_names names = { 0 };

  ui->fd = open (UINPUT_PATH, O_WRONLY | O_NONBLOCK);
  if (ui->fd < 0)
    {
      int saved = errno;

      g_set_error (error, G_IO_ERROR, g_io_error_from_errno (saved), "Cannot open %s: %s", UINPUT_PATH, g_strerror (saved));
      g_free (ui);
      return FALSE;
    }
  ioctl (ui->fd, UI_SET_EVBIT, EV_KEY);
  ioctl (ui->fd, UI_SET_EVBIT, EV_REL);
  ioctl (ui->fd, UI_SET_EVBIT, EV_SYN);
  for (int code = 1; code < 256; code++)
    ioctl (ui->fd, UI_SET_KEYBIT, code);
  ioctl (ui->fd, UI_SET_KEYBIT, BTN_LEFT);
  ioctl (ui->fd, UI_SET_KEYBIT, BTN_RIGHT);
  ioctl (ui->fd, UI_SET_KEYBIT, BTN_MIDDLE);
  ioctl (ui->fd, UI_SET_RELBIT, REL_X);
  ioctl (ui->fd, UI_SET_RELBIT, REL_Y);
  ioctl (ui->fd, UI_SET_RELBIT, REL_WHEEL);
  ioctl (ui->fd, UI_SET_RELBIT, REL_HWHEEL);
  setup.id.bustype = BUS_VIRTUAL;
  setup.id.vendor = 0x1d6b;
  setup.id.product = 0x0104;
  g_strlcpy (setup.name, "Tandem remote input", sizeof setup.name);
  if (ioctl (ui->fd, UI_DEV_SETUP, &setup) < 0 || ioctl (ui->fd, UI_DEV_CREATE) < 0)
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED, "Cannot create the uinput device");
      close (ui->fd);
      g_free (ui);
      return FALSE;
    }

  context = xkb_context_new (XKB_CONTEXT_NO_FLAGS);
  names.layout = g_getenv ("XKB_DEFAULT_LAYOUT");
  names.variant = g_getenv ("XKB_DEFAULT_VARIANT");
  ui->keymap = xkb_keymap_new_from_names (context, &names, XKB_KEYMAP_COMPILE_NO_FLAGS);
  xkb_context_unref (context);

  backend->has_pointer = TRUE;
  backend->has_keyboard = ui->keymap != NULL;
  backend->data = ui;
  return TRUE;
}

static void
uinput_close (TandemInputBackend *backend)
{
  Uinput *ui = backend->data;

  if (ui == NULL)
    return;
  ioctl (ui->fd, UI_DEV_DESTROY);
  close (ui->fd);
  g_clear_pointer (&ui->keymap, xkb_keymap_unref);
  g_free (ui);
  backend->data = NULL;
}

static void
uinput_pointer_motion (TandemInputBackend *backend, double dx, double dy)
{
  Uinput *ui = backend->data;

  emit (ui, EV_REL, REL_X, (gint32) dx);
  emit (ui, EV_REL, REL_Y, (gint32) dy);
  emit (ui, EV_SYN, SYN_REPORT, 0);
}

static void
uinput_pointer_button (TandemInputBackend *backend, guint32 button, gboolean pressed)
{
  Uinput *ui = backend->data;

  emit (ui, EV_KEY, button, pressed ? 1 : 0);
  emit (ui, EV_SYN, SYN_REPORT, 0);
}

static void
uinput_pointer_scroll (TandemInputBackend *backend, double dx, double dy)
{
  Uinput *ui = backend->data;

  if (dy != 0)
    emit (ui, EV_REL, REL_WHEEL, dy > 0 ? -1 : 1);
  if (dx != 0)
    emit (ui, EV_REL, REL_HWHEEL, dx > 0 ? 1 : -1);
  emit (ui, EV_SYN, SYN_REPORT, 0);
}

typedef struct
{
  guint32 keysym;
  xkb_keycode_t keycode;
  xkb_level_index_t level;
} Lookup;

static void
find_keycode_cb (struct xkb_keymap *keymap, xkb_keycode_t keycode, void *data)
{
  Lookup *lookup = data;

  if (lookup->keycode != XKB_KEYCODE_INVALID)
    return;
  for (xkb_level_index_t level = 0; level < 2; level++)
    {
      const xkb_keysym_t *syms;
      int n = xkb_keymap_key_get_syms_by_level (keymap, keycode, 0, level, &syms);

      for (int i = 0; i < n; i++)
        {
          if (syms[i] == lookup->keysym)
            {
              lookup->keycode = keycode;
              lookup->level = level;
              return;
            }
        }
    }
}

static void
uinput_keysym (TandemInputBackend *backend, guint32 keysym, gboolean pressed)
{
  Uinput *ui = backend->data;
  Lookup lookup = { keysym, XKB_KEYCODE_INVALID, 0 };

  if (ui->keymap == NULL)
    return;
  xkb_keymap_key_for_each (ui->keymap, find_keycode_cb, &lookup);
  if (lookup.keycode == XKB_KEYCODE_INVALID)
    return;
  if (lookup.level == 1 && pressed && !ui->shift_held)
    emit (ui, EV_KEY, KEY_LEFTSHIFT, 1);
  emit (ui, EV_KEY, lookup.keycode - 8, pressed ? 1 : 0);
  if (lookup.level == 1 && !pressed && !ui->shift_held)
    emit (ui, EV_KEY, KEY_LEFTSHIFT, 0);
  if (keysym == XKB_KEY_Shift_L || keysym == XKB_KEY_Shift_R)
    ui->shift_held = pressed;
  emit (ui, EV_SYN, SYN_REPORT, 0);
}

static TandemInputBackend uinput_backend = {
  .name = "uinput",
  .available = uinput_available,
  .open = uinput_open,
  .close = uinput_close,
  .pointer_motion = uinput_pointer_motion,
  .pointer_button = uinput_pointer_button,
  .pointer_scroll = uinput_pointer_scroll,
  .keysym = uinput_keysym,
  .has_keyboard = TRUE,
  .has_pointer = TRUE,
};

TandemInputBackend *
tandem_input_uinput_backend (void)
{
  return &uinput_backend;
}
