/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "pulse.h"

#include <pulse/glib-mainloop.h>
#include <pulse/pulseaudio.h>
#include <string.h>

#define RECONNECT_SECONDS 10

struct _TandemPulse
{
  GObject parent_instance;

  pa_glib_mainloop *mainloop;
  pa_context *context;
  GPtrArray *sinks;
  char *default_sink;
  gboolean ready;
  guint reconnect_id;
  guint emit_id;
};

enum {
  CHANGED,
  N_SIGNALS
};

static guint signals[N_SIGNALS];

G_DEFINE_FINAL_TYPE (TandemPulse, tandem_pulse, G_TYPE_OBJECT)

static void connect_context (TandemPulse *self);

static void
sink_free (TandemSink *sink)
{
  g_free (sink->name);
  g_free (sink->description);
  g_free (sink);
}

static void
tandem_pulse_dispose (GObject *object)
{
  TandemPulse *self = TANDEM_PULSE (object);

  g_clear_handle_id (&self->reconnect_id, g_source_remove);
  g_clear_handle_id (&self->emit_id, g_source_remove);
  if (self->context != NULL)
    {
      pa_context_disconnect (self->context);
      pa_context_unref (self->context);
      self->context = NULL;
    }
  if (self->mainloop != NULL)
    {
      pa_glib_mainloop_free (self->mainloop);
      self->mainloop = NULL;
    }
  g_clear_pointer (&self->sinks, g_ptr_array_unref);
  g_clear_pointer (&self->default_sink, g_free);
  G_OBJECT_CLASS (tandem_pulse_parent_class)->dispose (object);
}

static void
tandem_pulse_class_init (TandemPulseClass *klass)
{
  G_OBJECT_CLASS (klass)->dispose = tandem_pulse_dispose;

  signals[CHANGED] = g_signal_new ("changed", TANDEM_TYPE_PULSE, G_SIGNAL_RUN_LAST, 0, NULL, NULL, NULL,
                                   G_TYPE_NONE, 0);
}

static void
tandem_pulse_init (TandemPulse *self)
{
  self->sinks = g_ptr_array_new_with_free_func ((GDestroyNotify) sink_free);
}

static gboolean
emit_changed_cb (gpointer user_data)
{
  TandemPulse *self = user_data;

  self->emit_id = 0;
  g_signal_emit (self, signals[CHANGED], 0);
  return G_SOURCE_REMOVE;
}

static void
schedule_changed (TandemPulse *self)
{
  if (self->emit_id == 0)
    self->emit_id = g_timeout_add (150, emit_changed_cb, self);
}

static void
sink_info_cb (pa_context *context, const pa_sink_info *info, int eol, void *user_data)
{
  TandemPulse *self = user_data;
  TandemSink *sink = NULL;

  if (eol > 0 || info == NULL)
    {
      schedule_changed (self);
      return;
    }
  for (guint i = 0; i < self->sinks->len; i++)
    {
      TandemSink *candidate = g_ptr_array_index (self->sinks, i);

      if (candidate->index == info->index)
        {
          sink = candidate;
          break;
        }
    }
  if (sink == NULL)
    {
      sink = g_new0 (TandemSink, 1);
      sink->index = info->index;
      g_ptr_array_add (self->sinks, sink);
    }
  g_free (sink->name);
  g_free (sink->description);
  sink->name = g_strdup (info->name);
  sink->description = g_strdup (info->description != NULL ? info->description : info->name);
  sink->muted = info->mute != 0;
  sink->volume = pa_cvolume_max (&info->volume);
  sink->max_volume = PA_VOLUME_NORM;
  sink->is_default = g_strcmp0 (self->default_sink, info->name) == 0;
}

static void
server_info_cb (pa_context *context, const pa_server_info *info, void *user_data)
{
  TandemPulse *self = user_data;

  if (info == NULL)
    return;
  g_free (self->default_sink);
  self->default_sink = g_strdup (info->default_sink_name);
  for (guint i = 0; i < self->sinks->len; i++)
    {
      TandemSink *sink = g_ptr_array_index (self->sinks, i);

      sink->is_default = g_strcmp0 (self->default_sink, sink->name) == 0;
    }
  pa_operation_unref (pa_context_get_sink_info_list (context, sink_info_cb, self));
}

static void
remove_sink (TandemPulse *self, guint32 index)
{
  for (guint i = 0; i < self->sinks->len; i++)
    {
      TandemSink *sink = g_ptr_array_index (self->sinks, i);

      if (sink->index == index)
        {
          g_ptr_array_remove_index (self->sinks, i);
          break;
        }
    }
  schedule_changed (self);
}

static void
subscribe_cb (pa_context *context, pa_subscription_event_type_t type, uint32_t index, void *user_data)
{
  TandemPulse *self = user_data;
  pa_subscription_event_type_t facility = type & PA_SUBSCRIPTION_EVENT_FACILITY_MASK;
  pa_subscription_event_type_t event = type & PA_SUBSCRIPTION_EVENT_TYPE_MASK;

  if (facility == PA_SUBSCRIPTION_EVENT_SERVER)
    {
      pa_operation_unref (pa_context_get_server_info (context, server_info_cb, self));
      return;
    }
  if (facility != PA_SUBSCRIPTION_EVENT_SINK)
    return;
  if (event == PA_SUBSCRIPTION_EVENT_REMOVE)
    remove_sink (self, index);
  else
    pa_operation_unref (pa_context_get_sink_info_by_index (context, index, sink_info_cb, self));
}

static gboolean
reconnect_cb (gpointer user_data)
{
  TandemPulse *self = user_data;

  self->reconnect_id = 0;
  connect_context (self);
  return G_SOURCE_REMOVE;
}

static void
state_cb (pa_context *context, void *user_data)
{
  TandemPulse *self = user_data;

  switch (pa_context_get_state (context))
    {
    case PA_CONTEXT_READY:
      self->ready = TRUE;
      pa_context_set_subscribe_callback (context, subscribe_cb, self);
      pa_operation_unref (pa_context_subscribe (context, PA_SUBSCRIPTION_MASK_SINK | PA_SUBSCRIPTION_MASK_SERVER,
                                                NULL, NULL));
      pa_operation_unref (pa_context_get_server_info (context, server_info_cb, self));
      break;
    case PA_CONTEXT_FAILED:
    case PA_CONTEXT_TERMINATED:
      self->ready = FALSE;
      g_ptr_array_set_size (self->sinks, 0);
      schedule_changed (self);
      if (self->reconnect_id == 0)
        self->reconnect_id = g_timeout_add_seconds (RECONNECT_SECONDS, reconnect_cb, self);
      break;
    default:
      break;
    }
}

static void
connect_context (TandemPulse *self)
{
  if (self->context != NULL)
    {
      pa_context_disconnect (self->context);
      pa_context_unref (self->context);
    }
  self->context = pa_context_new (pa_glib_mainloop_get_api (self->mainloop), "Tandem");
  pa_context_set_state_callback (self->context, state_cb, self);
  if (pa_context_connect (self->context, NULL, PA_CONTEXT_NOFAIL, NULL) < 0)
    g_debug ("pulse: connect failed: %s", pa_strerror (pa_context_errno (self->context)));
}

TandemPulse *
tandem_pulse_get (void)
{
  static TandemPulse *singleton;

  if (singleton == NULL)
    {
      singleton = g_object_new (TANDEM_TYPE_PULSE, NULL);
      singleton->mainloop = pa_glib_mainloop_new (NULL);
      connect_context (singleton);
    }
  return singleton;
}

gboolean
tandem_pulse_is_ready (TandemPulse *self)
{
  return self->ready;
}

GPtrArray *
tandem_pulse_get_sinks (TandemPulse *self)
{
  return self->sinks;
}

TandemSink *
tandem_pulse_find_sink (TandemPulse *self, const char *name)
{
  for (guint i = 0; i < self->sinks->len; i++)
    {
      TandemSink *sink = g_ptr_array_index (self->sinks, i);

      if (g_strcmp0 (sink->name, name) == 0)
        return sink;
    }
  return NULL;
}

static void
volume_info_cb (pa_context *context, const pa_sink_info *info, int eol, void *user_data)
{
  guint32 volume = GPOINTER_TO_UINT (user_data);
  pa_cvolume cvolume;

  if (eol > 0 || info == NULL)
    return;
  cvolume = info->volume;
  pa_cvolume_scale (&cvolume, volume);
  pa_operation_unref (pa_context_set_sink_volume_by_index (context, info->index, &cvolume, NULL, NULL));
}

void
tandem_pulse_set_volume (TandemPulse *self, const char *name, guint32 volume)
{
  if (!self->ready)
    return;
  pa_operation_unref (pa_context_get_sink_info_by_name (self->context, name, volume_info_cb,
                                                        GUINT_TO_POINTER (MIN (volume, PA_VOLUME_UI_MAX))));
}

void
tandem_pulse_set_muted (TandemPulse *self, const char *name, gboolean muted)
{
  if (!self->ready)
    return;
  pa_operation_unref (pa_context_set_sink_mute_by_name (self->context, name, muted, NULL, NULL));
}

void
tandem_pulse_set_default (TandemPulse *self, const char *name)
{
  if (!self->ready)
    return;
  pa_operation_unref (pa_context_set_default_sink (self->context, name, NULL, NULL));
}
