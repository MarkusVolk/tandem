/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once

#include <gio/gio.h>

G_BEGIN_DECLS

#define TANDEM_BTN_LEFT 0x110
#define TANDEM_BTN_RIGHT 0x111
#define TANDEM_BTN_MIDDLE 0x112

typedef enum
{
  TANDEM_MOD_SHIFT = 1 << 0,
  TANDEM_MOD_CTRL = 1 << 1,
  TANDEM_MOD_ALT = 1 << 2,
  TANDEM_MOD_SUPER = 1 << 3,
} TandemModifiers;

typedef struct _TandemInputBackend TandemInputBackend;

struct _TandemInputBackend
{
  const char *name;
  gboolean  (*available)      (void);
  gboolean  (*open)           (TandemInputBackend *backend, GError **error);
  void      (*close)          (TandemInputBackend *backend);
  void      (*pointer_motion) (TandemInputBackend *backend, double dx, double dy);
  void      (*pointer_button) (TandemInputBackend *backend, guint32 button, gboolean pressed);
  void      (*pointer_scroll) (TandemInputBackend *backend, double dx, double dy);
  void      (*keysym)         (TandemInputBackend *backend, guint32 keysym, gboolean pressed);
  void      (*flush)          (TandemInputBackend *backend);
  gboolean    has_keyboard;
  gboolean    has_pointer;
  gboolean    opened;
  gpointer    data;
};

TandemInputBackend *tandem_input_portal_backend (void);
TandemInputBackend *tandem_input_wlr_backend    (void);
TandemInputBackend *tandem_input_uinput_backend (void);

gboolean    tandem_input_available     (void);
gboolean    tandem_input_has_keyboard  (void);
gboolean    tandem_input_has_pointer   (void);
const char *tandem_input_backend_name  (void);
void        tandem_input_pointer_motion (double dx, double dy);
void        tandem_input_pointer_scroll (double dx, double dy);
void        tandem_input_click          (guint32 button, int times);
void        tandem_input_button         (guint32 button, gboolean pressed);
void        tandem_input_type_text      (const char *text, TandemModifiers modifiers);
void        tandem_input_special_key    (int special, TandemModifiers modifiers);
void        tandem_input_keysym         (guint32 keysym, TandemModifiers modifiers);
void        tandem_input_release        (void);

G_END_DECLS
