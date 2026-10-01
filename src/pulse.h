/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once

#include <gio/gio.h>

G_BEGIN_DECLS

#define TANDEM_TYPE_PULSE (tandem_pulse_get_type ())
G_DECLARE_FINAL_TYPE (TandemPulse, tandem_pulse, TANDEM, PULSE, GObject)

typedef struct
{
  char *name;
  char *description;
  gboolean muted;
  guint32 volume;
  guint32 max_volume;
  gboolean is_default;
  guint32 index;
} TandemSink;

TandemPulse *tandem_pulse_get         (void);
gboolean     tandem_pulse_is_ready    (TandemPulse *self);
GPtrArray   *tandem_pulse_get_sinks   (TandemPulse *self);
TandemSink  *tandem_pulse_find_sink   (TandemPulse *self, const char *name);
void         tandem_pulse_set_volume  (TandemPulse *self, const char *name, guint32 volume);
void         tandem_pulse_set_muted   (TandemPulse *self, const char *name, gboolean muted);
void         tandem_pulse_set_default (TandemPulse *self, const char *name);

G_END_DECLS
