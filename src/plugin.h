/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once

#include <gtk/gtk.h>

#include "packet.h"

G_BEGIN_DECLS

typedef struct _TandemDevice TandemDevice;

#define TANDEM_TYPE_PLUGIN (tandem_plugin_get_type ())
G_DECLARE_DERIVABLE_TYPE (TandemPlugin, tandem_plugin, TANDEM, PLUGIN, GObject)

struct _TandemPluginClass
{
  GObjectClass parent_class;

  const char        *name;
  const char        *title;
  const char *const *incoming;
  const char *const *outgoing;

  void       (*handle_packet) (TandemPlugin *self, TandemPacket *packet);
  void       (*connected)     (TandemPlugin *self);
  void       (*disconnected)  (TandemPlugin *self);
  GtkWidget *(*create_group)  (TandemPlugin *self);
};

typedef struct
{
  const char *name;
  GType     (*get_type) (void);
} TandemPluginInfo;

TandemDevice *tandem_plugin_get_device   (TandemPlugin *self);
GSettings    *tandem_plugin_get_settings (TandemPlugin *self);
const char   *tandem_plugin_get_name     (TandemPlugin *self);
const char   *tandem_plugin_get_title    (TandemPlugin *self);
void          tandem_plugin_send         (TandemPlugin *self, TandemPacket *packet);
gboolean      tandem_plugin_handles      (TandemPlugin *self, const char *type);
void          tandem_plugin_changed      (TandemPlugin *self);

const TandemPluginInfo *tandem_plugin_registry              (guint *n_plugins);
void                    tandem_plugin_registry_capabilities (GStrv *incoming, GStrv *outgoing);
gboolean                tandem_plugin_type_supports         (GType type, TandemDevice *device);

G_END_DECLS
