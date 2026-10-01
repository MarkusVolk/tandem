/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "wayland.h"

#include <errno.h>

#include "ext-data-control-v1-client-protocol.h"
#include "virtual-keyboard-unstable-v1-client-protocol.h"
#include "wlr-virtual-pointer-unstable-v1-client-protocol.h"

typedef struct
{
  GSource source;
  TandemWayland *wayland;
  gpointer fd_tag;
  gboolean reading;
} WaylandSource;

static gboolean
source_prepare (GSource *source, int *timeout)
{
  WaylandSource *ws = (WaylandSource *) source;
  struct wl_display *display = ws->wayland->display;

  *timeout = -1;
  if (wl_display_prepare_read (display) != 0)
    return TRUE;
  ws->reading = TRUE;
  if (wl_display_flush (display) < 0 && errno != EAGAIN)
    {
      wl_display_cancel_read (display);
      ws->reading = FALSE;
      return TRUE;
    }
  return FALSE;
}

static gboolean
source_check (GSource *source)
{
  WaylandSource *ws = (WaylandSource *) source;
  GIOCondition cond = g_source_query_unix_fd (source, ws->fd_tag);

  if (!ws->reading)
    return TRUE;
  if (cond & (G_IO_IN | G_IO_ERR | G_IO_HUP))
    {
      wl_display_read_events (ws->wayland->display);
      ws->reading = FALSE;
      return TRUE;
    }
  wl_display_cancel_read (ws->wayland->display);
  ws->reading = FALSE;
  return FALSE;
}

static gboolean
source_dispatch (GSource *source, GSourceFunc callback, gpointer user_data)
{
  WaylandSource *ws = (WaylandSource *) source;

  if (wl_display_dispatch_pending (ws->wayland->display) < 0)
    {
      g_warning ("wayland: connection lost");
      return G_SOURCE_REMOVE;
    }
  wl_display_flush (ws->wayland->display);
  return G_SOURCE_CONTINUE;
}

static GSourceFuncs source_funcs = {
  .prepare = source_prepare,
  .check = source_check,
  .dispatch = source_dispatch,
};

static void
registry_global_cb (void *data, struct wl_registry *registry, uint32_t name, const char *interface, uint32_t version)
{
  TandemWayland *self = data;

  if (g_str_equal (interface, wl_seat_interface.name) && self->seat == NULL)
    self->seat = wl_registry_bind (registry, name, &wl_seat_interface, MIN (version, 5));
  else if (g_str_equal (interface, zwp_virtual_keyboard_manager_v1_interface.name))
    self->virtual_keyboard_manager = wl_registry_bind (registry, name, &zwp_virtual_keyboard_manager_v1_interface, 1);
  else if (g_str_equal (interface, zwlr_virtual_pointer_manager_v1_interface.name))
    self->virtual_pointer_manager = wl_registry_bind (registry, name, &zwlr_virtual_pointer_manager_v1_interface, MIN (version, 2));
  else if (g_str_equal (interface, ext_data_control_manager_v1_interface.name))
    self->data_control_manager = wl_registry_bind (registry, name, &ext_data_control_manager_v1_interface, 1);
}

static void
registry_global_remove_cb (void *data, struct wl_registry *registry, uint32_t name)
{
}

static const struct wl_registry_listener registry_listener = {
  .global = registry_global_cb,
  .global_remove = registry_global_remove_cb,
};

TandemWayland *
tandem_wayland_get (void)
{
  static TandemWayland *instance;
  static gboolean tried;
  WaylandSource *ws;
  GSource *source;

  if (tried)
    return instance;
  tried = TRUE;
  instance = g_new0 (TandemWayland, 1);
  instance->display = wl_display_connect (NULL);
  if (instance->display == NULL)
    {
      g_message ("wayland: no display, remote input and clipboard through Wayland protocols unavailable");
      g_clear_pointer (&instance, g_free);
      return NULL;
    }
  instance->registry = wl_display_get_registry (instance->display);
  wl_registry_add_listener (instance->registry, &registry_listener, instance);
  wl_display_roundtrip (instance->display);

  source = g_source_new (&source_funcs, sizeof (WaylandSource));
  ws = (WaylandSource *) source;
  ws->wayland = instance;
  ws->fd_tag = g_source_add_unix_fd (source, wl_display_get_fd (instance->display), G_IO_IN | G_IO_ERR | G_IO_HUP);
  g_source_set_name (source, "tandem-wayland");
  g_source_attach (source, NULL);
  g_source_unref (source);
  return instance;
}

void
tandem_wayland_flush (TandemWayland *self)
{
  if (self != NULL)
    wl_display_flush (self->display);
}
