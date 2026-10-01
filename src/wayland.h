/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once

#include <gio/gio.h>
#include <wayland-client.h>

G_BEGIN_DECLS

typedef struct
{
  struct wl_display *display;
  struct wl_registry *registry;
  struct wl_seat *seat;
  struct zwp_virtual_keyboard_manager_v1 *virtual_keyboard_manager;
  struct zwlr_virtual_pointer_manager_v1 *virtual_pointer_manager;
  struct ext_data_control_manager_v1 *data_control_manager;
} TandemWayland;

TandemWayland *tandem_wayland_get   (void);
void           tandem_wayland_flush (TandemWayland *self);

G_END_DECLS
