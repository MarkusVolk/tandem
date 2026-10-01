/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "clipboard.h"
#include "util.h"
#include "tandem-build.h"

#ifdef TANDEM_HAVE_WAYLAND
#include "wayland.h"
#endif

#include <fcntl.h>
#include <gio/gunixinputstream.h>
#include <gio/gunixoutputstream.h>
#include <string.h>
#include <unistd.h>
#include <wayland-client.h>

#ifdef TANDEM_HAVE_WAYLAND
#include "ext-data-control-v1-client-protocol.h"
#endif

#define TEXT_UTF8 "text/plain;charset=utf-8"
#define TEXT_PLAIN "text/plain"
#define MAX_CLIPBOARD (4 * 1024 * 1024)

struct _TandemClipboard
{
  GObject parent_instance;

  char *text;
  gint64 timestamp;
  const char *backend;

#ifdef TANDEM_HAVE_WAYLAND
  TandemWayland *wayland;
  struct ext_data_control_manager_v1 *manager;
  struct ext_data_control_device_v1 *device;
  struct ext_data_control_source_v1 *source;
  struct ext_data_control_offer_v1 *pending_offer;
  gboolean pending_has_text;
  gboolean pending_utf8;
#endif
  char *source_text;
};

G_DEFINE_FINAL_TYPE (TandemClipboard, tandem_clipboard, G_TYPE_OBJECT)

enum { CHANGED, N_SIGNALS };
static guint signals[N_SIGNALS];

static void
store_text (TandemClipboard *self, const char *text)
{
  if (text == NULL || g_strcmp0 (text, self->text) == 0)
    return;
  g_free (self->text);
  self->text = g_strdup (text);
  self->timestamp = tandem_now_ms ();
  g_signal_emit (self, signals[CHANGED], 0, self->text);
}

#ifdef TANDEM_HAVE_WAYLAND
static void
offer_offer_cb (void *data, struct ext_data_control_offer_v1 *offer, const char *mime_type)
{
  TandemClipboard *self = data;

  if (offer != self->pending_offer)
    return;
  if (g_str_equal (mime_type, TEXT_UTF8))
    self->pending_utf8 = TRUE;
  if (g_str_equal (mime_type, TEXT_PLAIN) || g_str_equal (mime_type, TEXT_UTF8) || g_str_equal (mime_type, "UTF8_STRING"))
    self->pending_has_text = TRUE;
}

static const struct ext_data_control_offer_v1_listener offer_listener = {
  .offer = offer_offer_cb,
};

typedef struct
{
  TandemClipboard *self;
  struct ext_data_control_offer_v1 *offer;
} ReadJob;

static void
read_done_cb (GObject *source, GAsyncResult *result, gpointer user_data)
{
  ReadJob *job = user_data;
  g_autoptr(GBytes) bytes = g_input_stream_read_bytes_finish (G_INPUT_STREAM (source), result, NULL);
  g_autoptr(GOutputStream) collected = g_object_get_data (G_OBJECT (source), "collected");

  if (collected != NULL)
    g_object_ref (collected);
  if (bytes != NULL && g_bytes_get_size (bytes) > 0 &&
      g_memory_output_stream_get_data_size (G_MEMORY_OUTPUT_STREAM (collected)) + g_bytes_get_size (bytes) < MAX_CLIPBOARD)
    {
      g_output_stream_write_bytes (collected, bytes, NULL, NULL);
      g_input_stream_read_bytes_async (G_INPUT_STREAM (source), 65536, G_PRIORITY_DEFAULT, NULL, read_done_cb, job);
      return;
    }
  g_output_stream_write (collected, "", 1, NULL, NULL);
  g_output_stream_close (collected, NULL, NULL);
  {
    const char *text = g_memory_output_stream_get_data (G_MEMORY_OUTPUT_STREAM (collected));

    if (text != NULL && g_utf8_validate (text, -1, NULL))
      store_text (job->self, text);
  }
  ext_data_control_offer_v1_destroy (job->offer);
  g_object_unref (source);
  g_object_unref (job->self);
  g_free (job);
}

static void
device_data_offer_cb (void *data, struct ext_data_control_device_v1 *device, struct ext_data_control_offer_v1 *offer)
{
  TandemClipboard *self = data;

  self->pending_offer = offer;
  self->pending_has_text = FALSE;
  self->pending_utf8 = FALSE;
  ext_data_control_offer_v1_add_listener (offer, &offer_listener, self);
}

static void
device_selection_cb (void *data, struct ext_data_control_device_v1 *device, struct ext_data_control_offer_v1 *offer)
{
  TandemClipboard *self = data;
  int fds[2];
  ReadJob *job;
  GInputStream *stream;
  GOutputStream *collected;

  if (offer == NULL)
    return;
  if (offer != self->pending_offer || !self->pending_has_text || self->source != NULL)
    {
      if (self->source != NULL && self->source_text != NULL)
        store_text (self, self->source_text);
      ext_data_control_offer_v1_destroy (offer);
      self->pending_offer = NULL;
      return;
    }
  if (pipe2 (fds, O_CLOEXEC) != 0)
    {
      ext_data_control_offer_v1_destroy (offer);
      return;
    }
  ext_data_control_offer_v1_receive (offer, self->pending_utf8 ? TEXT_UTF8 : TEXT_PLAIN, fds[1]);
  tandem_wayland_flush (self->wayland);
  close (fds[1]);
  self->pending_offer = NULL;

  job = g_new0 (ReadJob, 1);
  job->self = g_object_ref (self);
  job->offer = offer;
  stream = g_unix_input_stream_new (fds[0], TRUE);
  collected = g_memory_output_stream_new_resizable ();
  g_object_set_data_full (G_OBJECT (stream), "collected", collected, g_object_unref);
  g_input_stream_read_bytes_async (stream, 65536, G_PRIORITY_DEFAULT, NULL, read_done_cb, job);
}

static void
device_finished_cb (void *data, struct ext_data_control_device_v1 *device)
{
  TandemClipboard *self = data;

  g_clear_pointer (&self->device, ext_data_control_device_v1_destroy);
}

static void
device_primary_selection_cb (void *data, struct ext_data_control_device_v1 *device, struct ext_data_control_offer_v1 *offer)
{
  TandemClipboard *self = data;

  if (offer != NULL)
    {
      if (offer == self->pending_offer)
        self->pending_offer = NULL;
      ext_data_control_offer_v1_destroy (offer);
    }
}

static const struct ext_data_control_device_v1_listener device_listener = {
  .data_offer = device_data_offer_cb,
  .selection = device_selection_cb,
  .finished = device_finished_cb,
  .primary_selection = device_primary_selection_cb,
};

static void
write_done_cb (GObject *source, GAsyncResult *result, gpointer user_data)
{
  g_output_stream_write_all_finish (G_OUTPUT_STREAM (source), result, NULL, NULL);
  g_object_unref (source);
}

static void
source_send_cb (void *data, struct ext_data_control_source_v1 *source, const char *mime_type, int32_t fd)
{
  TandemClipboard *self = data;
  GOutputStream *stream = g_unix_output_stream_new (fd, TRUE);
  const char *text = self->source_text != NULL ? self->source_text : "";

  g_output_stream_write_all_async (stream, text, strlen (text), G_PRIORITY_DEFAULT, NULL, write_done_cb, NULL);
}

static void
source_cancelled_cb (void *data, struct ext_data_control_source_v1 *source)
{
  TandemClipboard *self = data;

  if (self->source == source)
    self->source = NULL;
  ext_data_control_source_v1_destroy (source);
}

static const struct ext_data_control_source_v1_listener source_listener = {
  .send = source_send_cb,
  .cancelled = source_cancelled_cb,
};

static gboolean
setup_wlr (TandemClipboard *self)
{
  TandemWayland *wayland = tandem_wayland_get ();

  if (wayland == NULL || wayland->seat == NULL || wayland->data_control_manager == NULL)
    return FALSE;
  self->wayland = wayland;
  self->manager = wayland->data_control_manager;
  self->device = ext_data_control_manager_v1_get_data_device (self->manager, wayland->seat);
  ext_data_control_device_v1_add_listener (self->device, &device_listener, self);
  tandem_wayland_flush (wayland);
  self->backend = "data-control";
  return TRUE;
}

#else
static gboolean
setup_wlr (TandemClipboard *self)
{
  return FALSE;
}
#endif

TandemClipboard *
tandem_clipboard_get (void)
{
  static TandemClipboard *instance;

  if (instance == NULL)
    {
      instance = g_object_new (TANDEM_TYPE_CLIPBOARD, NULL);
      if (!setup_wlr (instance))
        {
          instance->backend = "none";
          g_message ("clipboard: the compositor offers no ext-data-control, clipboard sync is off");
        }
      else
        g_message ("clipboard through %s", instance->backend);
    }
  return instance;
}

const char *
tandem_clipboard_get_text (TandemClipboard *self)
{
  return self->text;
}

gint64
tandem_clipboard_get_timestamp (TandemClipboard *self)
{
  return self->timestamp;
}

const char *
tandem_clipboard_backend_name (TandemClipboard *self)
{
  return self->backend;
}

void
tandem_clipboard_set_text (TandemClipboard *self, const char *text)
{
  if (text == NULL)
    return;
  g_free (self->text);
  self->text = g_strdup (text);
  self->timestamp = tandem_now_ms ();

#ifdef TANDEM_HAVE_WAYLAND
  if (self->device != NULL && self->manager != NULL)
    {
      g_free (self->source_text);
      self->source_text = g_strdup (text);
      if (self->source != NULL)
        {
          ext_data_control_source_v1_destroy (self->source);
          self->source = NULL;
        }
      self->source = ext_data_control_manager_v1_create_data_source (self->manager);
      ext_data_control_source_v1_add_listener (self->source, &source_listener, self);
      ext_data_control_source_v1_offer (self->source, TEXT_UTF8);
      ext_data_control_source_v1_offer (self->source, TEXT_PLAIN);
      ext_data_control_source_v1_offer (self->source, "UTF8_STRING");
      ext_data_control_source_v1_offer (self->source, "STRING");
      ext_data_control_source_v1_offer (self->source, "TEXT");
      ext_data_control_device_v1_set_selection (self->device, self->source);
      tandem_wayland_flush (self->wayland);
    }
#endif
}

static void
tandem_clipboard_finalize (GObject *object)
{
  TandemClipboard *self = TANDEM_CLIPBOARD (object);

  g_free (self->text);
  g_free (self->source_text);
  G_OBJECT_CLASS (tandem_clipboard_parent_class)->finalize (object);
}

static void
tandem_clipboard_class_init (TandemClipboardClass *klass)
{
  G_OBJECT_CLASS (klass)->finalize = tandem_clipboard_finalize;
  signals[CHANGED] = g_signal_new ("changed", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST, 0, NULL, NULL, NULL,
                                   G_TYPE_NONE, 1, G_TYPE_STRING);
}

static void
tandem_clipboard_init (TandemClipboard *self)
{
}
