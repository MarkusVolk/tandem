/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "../device.h"
#include "../frontend.h"
#include "../plugin.h"

#define HIDE_AFTER_SECONDS 2

#define TANDEM_TYPE_PRESENTER_PLUGIN (tandem_presenter_plugin_get_type ())
G_DECLARE_FINAL_TYPE (TandemPresenterPlugin, tandem_presenter_plugin, TANDEM, PRESENTER_PLUGIN, TandemPlugin)

struct _TandemPresenterPlugin
{
  TandemPlugin parent_instance;

  gboolean active;
  double x;
  double y;
  guint hide_id;
};

G_DEFINE_FINAL_TYPE (TandemPresenterPlugin, tandem_presenter_plugin, TANDEM_TYPE_PLUGIN)

static const char *const presenter_incoming[] = { "kdeconnect.presenter", NULL };

static void
stop (TandemPresenterPlugin *self)
{
  g_clear_handle_id (&self->hide_id, g_source_remove);
  if (!self->active)
    return;
  self->active = FALSE;
  tandem_plugin_changed (TANDEM_PLUGIN (self));
}

static gboolean
hide_cb (gpointer user_data)
{
  TandemPresenterPlugin *self = user_data;

  self->hide_id = 0;
  stop (self);
  return G_SOURCE_REMOVE;
}

static void
presenter_handle_packet (TandemPlugin *plugin, TandemPacket *packet)
{
  TandemPresenterPlugin *self = TANDEM_PRESENTER_PLUGIN (plugin);

  if (tandem_packet_get_boolean (packet, "stop", FALSE))
    {
      stop (self);
      return;
    }
  if (!self->active)
    {
      self->active = TRUE;
      self->x = 0.5;
      self->y = 0.5;
      tandem_frontend_action ("presenter", g_variant_new_string (tandem_device_get_id (tandem_plugin_get_device (plugin))));
    }
  self->x = CLAMP (self->x + tandem_packet_get_double (packet, "dx", 0) / 1000.0, 0.0, 1.0);
  self->y = CLAMP (self->y + tandem_packet_get_double (packet, "dy", 0) / 1000.0, 0.0, 1.0);
  g_clear_handle_id (&self->hide_id, g_source_remove);
  self->hide_id = g_timeout_add_seconds (HIDE_AFTER_SECONDS, hide_cb, self);
  tandem_plugin_changed (plugin);
}

static void
presenter_disconnected (TandemPlugin *plugin)
{
  stop (TANDEM_PRESENTER_PLUGIN (plugin));
}

static GVariant *
presenter_get_state (TandemPlugin *plugin)
{
  TandemPresenterPlugin *self = TANDEM_PRESENTER_PLUGIN (plugin);
  GVariantBuilder builder;

  g_variant_builder_init (&builder, G_VARIANT_TYPE ("a{sv}"));
  g_variant_builder_add (&builder, "{sv}", "active", g_variant_new_boolean (self->active));
  g_variant_builder_add (&builder, "{sv}", "x", g_variant_new_double (self->x));
  g_variant_builder_add (&builder, "{sv}", "y", g_variant_new_double (self->y));
  return g_variant_builder_end (&builder);
}

static void
tandem_presenter_plugin_dispose (GObject *object)
{
  g_clear_handle_id (&TANDEM_PRESENTER_PLUGIN (object)->hide_id, g_source_remove);
  G_OBJECT_CLASS (tandem_presenter_plugin_parent_class)->dispose (object);
}

static void
tandem_presenter_plugin_class_init (TandemPresenterPluginClass *klass)
{
  TandemPluginClass *plugin_class = TANDEM_PLUGIN_CLASS (klass);

  G_OBJECT_CLASS (klass)->dispose = tandem_presenter_plugin_dispose;
  plugin_class->name = "presenter";
  plugin_class->title = "Presenter";
  plugin_class->incoming = presenter_incoming;
  plugin_class->handle_packet = presenter_handle_packet;
  plugin_class->disconnected = presenter_disconnected;
  plugin_class->get_state = presenter_get_state;
}

static void
tandem_presenter_plugin_init (TandemPresenterPlugin *self)
{
}
