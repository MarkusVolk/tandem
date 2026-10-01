/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "plugin.h"
#include "tandem-build.h"
#include "device.h"

#include <string.h>

typedef struct
{
  TandemDevice *device;
  GSettings *settings;
} TandemPluginPrivate;

enum {
  PROP_0,
  PROP_DEVICE,
  N_PROPS
};

static GParamSpec *properties[N_PROPS];

enum {
  CHANGED,
  N_SIGNALS
};

static guint signals[N_SIGNALS];

G_DEFINE_ABSTRACT_TYPE_WITH_PRIVATE (TandemPlugin, tandem_plugin, G_TYPE_OBJECT)

GType tandem_ping_plugin_get_type (void);
GType tandem_battery_plugin_get_type (void);
GType tandem_connectivity_plugin_get_type (void);
GType tandem_findmyphone_plugin_get_type (void);
GType tandem_lock_plugin_get_type (void);
GType tandem_runcommand_plugin_get_type (void);
GType tandem_notifications_plugin_get_type (void);
GType tandem_share_plugin_get_type (void);
GType tandem_mpriscontrol_plugin_get_type (void);
#ifdef TANDEM_HAVE_PULSE
GType tandem_systemvolume_plugin_get_type (void);
#endif
GType tandem_mprisremote_plugin_get_type (void);
GType tandem_mousepad_plugin_get_type (void);
GType tandem_presenter_plugin_get_type (void);
GType tandem_telephony_plugin_get_type (void);
GType tandem_contacts_plugin_get_type (void);
GType tandem_sms_plugin_get_type (void);
GType tandem_clipboard_plugin_get_type (void);
GType tandem_sendnotifications_plugin_get_type (void);
GType tandem_remoteinput_plugin_get_type (void);

static const TandemPluginInfo registry[] = {
  { "ping", tandem_ping_plugin_get_type },
  { "battery", tandem_battery_plugin_get_type },
  { "connectivity", tandem_connectivity_plugin_get_type },
  { "findmyphone", tandem_findmyphone_plugin_get_type },
  { "lock", tandem_lock_plugin_get_type },
  { "runcommand", tandem_runcommand_plugin_get_type },
  { "notifications", tandem_notifications_plugin_get_type },
  { "share", tandem_share_plugin_get_type },
  { "mpriscontrol", tandem_mpriscontrol_plugin_get_type },
#ifdef TANDEM_HAVE_PULSE
  { "systemvolume", tandem_systemvolume_plugin_get_type },
#endif
  { "mprisremote", tandem_mprisremote_plugin_get_type },
  { "mousepad", tandem_mousepad_plugin_get_type },
  { "presenter", tandem_presenter_plugin_get_type },
  { "contacts", tandem_contacts_plugin_get_type },
  { "telephony", tandem_telephony_plugin_get_type },
  { "sms", tandem_sms_plugin_get_type },
  { "clipboard", tandem_clipboard_plugin_get_type },
  { "sendnotifications", tandem_sendnotifications_plugin_get_type },
  { "remoteinput", tandem_remoteinput_plugin_get_type },
};

static void
tandem_plugin_set_property (GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec)
{
  TandemPluginPrivate *priv = tandem_plugin_get_instance_private (TANDEM_PLUGIN (object));

  switch (prop_id)
    {
    case PROP_DEVICE:
      priv->device = g_value_get_object (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
tandem_plugin_get_property (GObject *object, guint prop_id, GValue *value, GParamSpec *pspec)
{
  TandemPluginPrivate *priv = tandem_plugin_get_instance_private (TANDEM_PLUGIN (object));

  switch (prop_id)
    {
    case PROP_DEVICE:
      g_value_set_object (value, priv->device);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
tandem_plugin_dispose (GObject *object)
{
  TandemPluginPrivate *priv = tandem_plugin_get_instance_private (TANDEM_PLUGIN (object));

  g_clear_object (&priv->settings);
  priv->device = NULL;

  G_OBJECT_CLASS (tandem_plugin_parent_class)->dispose (object);
}

static void
tandem_plugin_class_init (TandemPluginClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->set_property = tandem_plugin_set_property;
  object_class->get_property = tandem_plugin_get_property;
  object_class->dispose = tandem_plugin_dispose;

  properties[PROP_DEVICE] = g_param_spec_object ("device", NULL, NULL, G_TYPE_OBJECT,
                                                 G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS);
  g_object_class_install_properties (object_class, N_PROPS, properties);

  signals[CHANGED] = g_signal_new ("changed", TANDEM_TYPE_PLUGIN, G_SIGNAL_RUN_LAST, 0, NULL, NULL, NULL,
                                   G_TYPE_NONE, 0);
}

static void
tandem_plugin_init (TandemPlugin *self)
{
}

TandemDevice *
tandem_plugin_get_device (TandemPlugin *self)
{
  TandemPluginPrivate *priv = tandem_plugin_get_instance_private (self);

  return priv->device;
}

GSettings *
tandem_plugin_get_settings (TandemPlugin *self)
{
  TandemPluginPrivate *priv = tandem_plugin_get_instance_private (self);

  if (priv->settings == NULL && priv->device != NULL)
    priv->settings = g_object_ref (tandem_device_get_settings (priv->device));
  return priv->settings;
}

const char *
tandem_plugin_get_name (TandemPlugin *self)
{
  return TANDEM_PLUGIN_GET_CLASS (self)->name;
}

const char *
tandem_plugin_get_title (TandemPlugin *self)
{
  TandemPluginClass *klass = TANDEM_PLUGIN_GET_CLASS (self);

  return klass->title != NULL ? klass->title : klass->name;
}

void
tandem_plugin_send (TandemPlugin *self, TandemPacket *packet)
{
  TandemPluginPrivate *priv = tandem_plugin_get_instance_private (self);

  if (priv->device != NULL)
    tandem_device_send_packet (priv->device, packet);
}

gboolean
tandem_plugin_handles (TandemPlugin *self, const char *type)
{
  TandemPluginClass *klass = TANDEM_PLUGIN_GET_CLASS (self);

  return klass->incoming != NULL && g_strv_contains (klass->incoming, type);
}

const TandemPluginInfo *
tandem_plugin_registry (guint *n_plugins)
{
  if (n_plugins != NULL)
    *n_plugins = G_N_ELEMENTS (registry);
  return registry;
}

static void
add_unique (GStrvBuilder *builder, GHashTable *seen, const char *const *types)
{
  for (; types != NULL && *types != NULL; types++)
    {
      if (g_hash_table_add (seen, g_strdup (*types)))
        g_strv_builder_add (builder, *types);
    }
}

void
tandem_plugin_registry_capabilities (GStrv *incoming, GStrv *outgoing)
{
  g_autoptr(GStrvBuilder) in_builder = g_strv_builder_new ();
  g_autoptr(GStrvBuilder) out_builder = g_strv_builder_new ();
  g_autoptr(GHashTable) in_seen = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);
  g_autoptr(GHashTable) out_seen = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);

  for (guint i = 0; i < G_N_ELEMENTS (registry); i++)
    {
      TandemPluginClass *klass = g_type_class_ref (registry[i].get_type ());

      add_unique (in_builder, in_seen, klass->incoming);
      add_unique (out_builder, out_seen, klass->outgoing);
      g_type_class_unref (klass);
    }
  if (incoming != NULL)
    *incoming = g_strv_builder_end (in_builder);
  if (outgoing != NULL)
    *outgoing = g_strv_builder_end (out_builder);
}

gboolean
tandem_plugin_type_supports (GType type, TandemDevice *device)
{
  TandemPluginClass *klass = g_type_class_ref (type);
  gboolean supported = FALSE;

  for (const char *const *t = klass->incoming; !supported && t != NULL && *t != NULL; t++)
    supported = tandem_device_has_outgoing (device, *t);
  for (const char *const *t = klass->outgoing; !supported && t != NULL && *t != NULL; t++)
    supported = tandem_device_has_incoming (device, *t);
  g_type_class_unref (klass);
  return supported;
}

void
tandem_plugin_changed (TandemPlugin *self)
{
  g_signal_emit (self, signals[CHANGED], 0);
}

GVariant *
tandem_plugin_get_state (TandemPlugin *self)
{
  TandemPluginClass *klass = TANDEM_PLUGIN_GET_CLASS (self);
  GVariant *state = klass->get_state != NULL ? klass->get_state (self) : NULL;

  if (state == NULL)
    state = tandem_plugin_empty_result ();
  return g_variant_ref_sink (state);
}

GVariant *
tandem_plugin_call (TandemPlugin *self, const char *method, GVariant *args, GError **error)
{
  TandemPluginClass *klass = TANDEM_PLUGIN_GET_CLASS (self);
  GVariant *result;

  if (klass->call == NULL)
    {
      g_set_error (error, G_DBUS_ERROR, G_DBUS_ERROR_UNKNOWN_METHOD, "%s has no methods", klass->name);
      return NULL;
    }
  result = klass->call (self, method, args, error);
  if (result == NULL && (error == NULL || *error == NULL))
    result = tandem_plugin_empty_result ();
  return result != NULL ? g_variant_ref_sink (result) : NULL;
}

gboolean
tandem_plugin_args_lookup (GVariant *args, const char *key, const char *format, ...)
{
  va_list ap;
  g_autoptr(GVariant) value = NULL;

  if (args == NULL)
    return FALSE;
  value = g_variant_lookup_value (args, key, NULL);
  if (value == NULL || !g_variant_check_format_string (value, format, FALSE))
    return FALSE;
  va_start (ap, format);
  g_variant_get_va (value, format, NULL, &ap);
  va_end (ap);
  return TRUE;
}

GVariant *
tandem_plugin_empty_result (void)
{
  GVariantBuilder builder;

  g_variant_builder_init (&builder, G_VARIANT_TYPE ("a{sv}"));
  return g_variant_builder_end (&builder);
}
