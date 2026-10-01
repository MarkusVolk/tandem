/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "../device.h"
#include "../plugin.h"

#include <adwaita.h>

#define HIDE_AFTER_SECONDS 2
#define DOT_RADIUS 12

#define TANDEM_TYPE_PRESENTER_PLUGIN (tandem_presenter_plugin_get_type ())
G_DECLARE_FINAL_TYPE (TandemPresenterPlugin, tandem_presenter_plugin, TANDEM, PRESENTER_PLUGIN, TandemPlugin)

struct _TandemPresenterPlugin
{
  TandemPlugin parent_instance;

  GtkWindow *window;
  GtkWidget *area;
  double x;
  double y;
  guint hide_id;
};

G_DEFINE_FINAL_TYPE (TandemPresenterPlugin, tandem_presenter_plugin, TANDEM_TYPE_PLUGIN)

static const char *const presenter_incoming[] = { "kdeconnect.presenter", NULL };

static void
hide_overlay (TandemPresenterPlugin *self)
{
  g_clear_handle_id (&self->hide_id, g_source_remove);
  if (self->window != NULL)
    gtk_window_destroy (self->window);
  self->window = NULL;
  self->area = NULL;
}

static gboolean
hide_cb (gpointer user_data)
{
  TandemPresenterPlugin *self = user_data;

  self->hide_id = 0;
  hide_overlay (self);
  return G_SOURCE_REMOVE;
}

static void
draw_cb (GtkDrawingArea *area, cairo_t *cr, int width, int height, gpointer user_data)
{
  TandemPresenterPlugin *self = user_data;
  double x = CLAMP (self->x, 0.0, 1.0) * width;
  double y = CLAMP (self->y, 0.0, 1.0) * height;

  cairo_set_source_rgba (cr, 0.9, 0.1, 0.1, 0.85);
  cairo_arc (cr, x, y, DOT_RADIUS, 0, 2 * G_PI);
  cairo_fill (cr);
}

static void
window_destroyed_cb (GtkWindow *window, gpointer user_data)
{
  TandemPresenterPlugin *self = user_data;

  if (self->window == window)
    {
      self->window = NULL;
      self->area = NULL;
    }
}

static void
show_overlay (TandemPresenterPlugin *self)
{
  GtkWidget *window;
  GdkSurface *surface;

  if (self->window != NULL)
    return;
  window = gtk_window_new ();
  gtk_window_set_title (GTK_WINDOW (window), "Presenter");
  gtk_window_set_decorated (GTK_WINDOW (window), FALSE);
  gtk_widget_add_css_class (window, "tandem-presenter");
  self->area = gtk_drawing_area_new ();
  gtk_drawing_area_set_draw_func (GTK_DRAWING_AREA (self->area), draw_cb, self, NULL);
  gtk_widget_set_can_target (self->area, FALSE);
  gtk_widget_set_can_target (window, FALSE);
  gtk_window_set_child (GTK_WINDOW (window), self->area);
  g_signal_connect (window, "destroy", G_CALLBACK (window_destroyed_cb), self);
  self->window = GTK_WINDOW (window);
  gtk_window_fullscreen (GTK_WINDOW (window));
  gtk_window_present (GTK_WINDOW (window));
  surface = gtk_native_get_surface (GTK_NATIVE (window));
  if (surface != NULL)
    {
      cairo_region_t *empty = cairo_region_create ();

      gdk_surface_set_input_region (surface, empty);
      cairo_region_destroy (empty);
    }
}

static void
presenter_handle_packet (TandemPlugin *plugin, TandemPacket *packet)
{
  TandemPresenterPlugin *self = TANDEM_PRESENTER_PLUGIN (plugin);

  if (tandem_packet_get_boolean (packet, "stop", FALSE))
    {
      hide_overlay (self);
      return;
    }
  if (self->window == NULL)
    {
      self->x = 0.5;
      self->y = 0.5;
      show_overlay (self);
    }
  self->x = CLAMP (self->x + tandem_packet_get_double (packet, "dx", 0) / 1000.0, 0.0, 1.0);
  self->y = CLAMP (self->y + tandem_packet_get_double (packet, "dy", 0) / 1000.0, 0.0, 1.0);
  if (self->area != NULL)
    gtk_widget_queue_draw (self->area);
  g_clear_handle_id (&self->hide_id, g_source_remove);
  self->hide_id = g_timeout_add_seconds (HIDE_AFTER_SECONDS, hide_cb, self);
}

static void
tandem_presenter_plugin_dispose (GObject *object)
{
  hide_overlay (TANDEM_PRESENTER_PLUGIN (object));
  G_OBJECT_CLASS (tandem_presenter_plugin_parent_class)->dispose (object);
}

static void
tandem_presenter_plugin_class_init (TandemPresenterPluginClass *klass)
{
  TandemPluginClass *plugin_class = TANDEM_PLUGIN_CLASS (klass);
  static gboolean css_added;

  G_OBJECT_CLASS (klass)->dispose = tandem_presenter_plugin_dispose;
  plugin_class->name = "presenter";
  plugin_class->title = "Presenter";
  plugin_class->incoming = presenter_incoming;
  plugin_class->handle_packet = presenter_handle_packet;
  plugin_class->disconnected = (void (*) (TandemPlugin *)) hide_overlay;

  if (!css_added && gdk_display_get_default () != NULL)
    {
      GtkCssProvider *provider = gtk_css_provider_new ();

      gtk_css_provider_load_from_string (provider, "window.tandem-presenter { background: transparent; }");
      gtk_style_context_add_provider_for_display (gdk_display_get_default (), GTK_STYLE_PROVIDER (provider),
                                                  GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
      css_added = TRUE;
    }
}

static void
tandem_presenter_plugin_init (TandemPresenterPlugin *self)
{
}
