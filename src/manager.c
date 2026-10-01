/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "manager.h"
#include "mdns.h"
#include "plugin.h"

#include <string.h>

#define MAX_UNPAIRED_DEVICES 42

struct _TandemManager
{
  GObject parent_instance;

  TandemConfig *config;
  TandemLan *lan;
  TandemMdns *mdns;
  GListStore *devices;
};

enum {
  DEVICE_ADDED,
  DEVICE_REMOVED,
  DEVICE_CHANGED,
  N_SIGNALS
};

static guint signals[N_SIGNALS];

G_DEFINE_FINAL_TYPE (TandemManager, tandem_manager, G_TYPE_OBJECT)

static void
tandem_manager_dispose (GObject *object)
{
  TandemManager *self = TANDEM_MANAGER (object);

  tandem_manager_stop (self);
  g_clear_object (&self->lan);
  g_clear_object (&self->devices);
  g_clear_object (&self->config);

  G_OBJECT_CLASS (tandem_manager_parent_class)->dispose (object);
}

static void
tandem_manager_class_init (TandemManagerClass *klass)
{
  G_OBJECT_CLASS (klass)->dispose = tandem_manager_dispose;

  signals[DEVICE_ADDED] = g_signal_new ("device-added", TANDEM_TYPE_MANAGER, G_SIGNAL_RUN_LAST, 0,
                                        NULL, NULL, NULL, G_TYPE_NONE, 1, TANDEM_TYPE_DEVICE);
  signals[DEVICE_REMOVED] = g_signal_new ("device-removed", TANDEM_TYPE_MANAGER, G_SIGNAL_RUN_LAST, 0,
                                          NULL, NULL, NULL, G_TYPE_NONE, 1, TANDEM_TYPE_DEVICE);
  signals[DEVICE_CHANGED] = g_signal_new ("device-changed", TANDEM_TYPE_MANAGER, G_SIGNAL_RUN_LAST, 0,
                                          NULL, NULL, NULL, G_TYPE_NONE, 1, TANDEM_TYPE_DEVICE);
}

static void
tandem_manager_init (TandemManager *self)
{
  self->devices = g_list_store_new (TANDEM_TYPE_DEVICE);
}

TandemManager *
tandem_manager_new (TandemConfig *config)
{
  TandemManager *self = g_object_new (TANDEM_TYPE_MANAGER, NULL);

  self->config = g_object_ref (config);
  g_auto(GStrv) incoming = NULL;
  g_auto(GStrv) outgoing = NULL;

  self->lan = tandem_lan_new (config);
  tandem_plugin_registry_capabilities (&incoming, &outgoing);
  tandem_lan_set_capabilities (self->lan, (const char *const *) incoming, (const char *const *) outgoing);
  return self;
}

static TandemDevice *
find_by_id (TandemManager *self, const char *id, guint *position)
{
  guint n = g_list_model_get_n_items (G_LIST_MODEL (self->devices));

  for (guint i = 0; i < n; i++)
    {
      g_autoptr(TandemDevice) device = g_list_model_get_item (G_LIST_MODEL (self->devices), i);

      if (g_strcmp0 (tandem_device_get_id (device), id) == 0)
        {
          if (position != NULL)
            *position = i;
          return device;
        }
    }
  return NULL;
}

static void
update_periodic (TandemManager *self)
{
  guint n = g_list_model_get_n_items (G_LIST_MODEL (self->devices));
  gboolean wanted = FALSE;

  for (guint i = 0; i < n && !wanted; i++)
    {
      g_autoptr(TandemDevice) device = g_list_model_get_item (G_LIST_MODEL (self->devices), i);

      if (tandem_device_is_paired (device) && !tandem_device_is_reachable (device) && !tandem_device_is_paused (device))
        wanted = TRUE;
    }
  tandem_lan_set_periodic (self->lan, wanted);
}

static void
remove_device (TandemManager *self, TandemDevice *device)
{
  guint position;

  if (find_by_id (self, tandem_device_get_id (device), &position) == NULL)
    return;
  g_object_ref (device);
  g_signal_handlers_disconnect_by_data (device, self);
  g_list_store_remove (self->devices, position);
  g_signal_emit (self, signals[DEVICE_REMOVED], 0, device);
  g_object_unref (device);
}

static void
device_changed_cb (TandemDevice *device, gpointer user_data)
{
  TandemManager *self = user_data;

  if (!tandem_device_is_reachable (device) && !tandem_device_is_paired (device))
    {
      remove_device (self, device);
      update_periodic (self);
      return;
    }
  g_signal_emit (self, signals[DEVICE_CHANGED], 0, device);
  update_periodic (self);
}

static guint
count_unpaired (TandemManager *self)
{
  guint n = g_list_model_get_n_items (G_LIST_MODEL (self->devices));
  guint count = 0;

  for (guint i = 0; i < n; i++)
    {
      g_autoptr(TandemDevice) device = g_list_model_get_item (G_LIST_MODEL (self->devices), i);

      if (!tandem_device_is_paired (device))
        count++;
    }
  return count;
}

static void
link_established_cb (TandemLan *lan, TandemLink *link, gpointer user_data)
{
  TandemManager *self = user_data;
  const char *id = tandem_link_get_device_id (link);
  TandemDevice *existing = find_by_id (self, id, NULL);

  if (existing != NULL)
    {
      GTlsCertificate *known = tandem_device_get_certificate (existing);
      GTlsCertificate *peer = tandem_link_get_peer_certificate (link);

      if (known != NULL && peer != NULL && !g_tls_certificate_is_same (known, peer))
        {
          g_warning ("device %s connected with another certificate, ignoring", id);
          tandem_link_close (link);
          return;
        }
      tandem_device_set_link (existing, link);
      return;
    }

  if (count_unpaired (self) >= MAX_UNPAIRED_DEVICES)
    {
      g_warning ("too many unpaired devices, ignoring %s", id);
      tandem_link_close (link);
      return;
    }

  {
    g_autoptr(TandemDevice) device = tandem_device_new_from_link (self->config, link);

    g_signal_connect (device, "changed", G_CALLBACK (device_changed_cb), self);
    g_list_store_append (self->devices, device);
    g_signal_emit (self, signals[DEVICE_ADDED], 0, device);
  }
}

gboolean
tandem_manager_start (TandemManager *self, GError **error)
{
  g_auto(GStrv) trusted = tandem_config_list_trusted (self->config);

  for (char **id = trusted; id != NULL && *id != NULL; id++)
    {
      g_autoptr(TandemDevice) device = tandem_device_new_trusted (self->config, *id);

      if (device == NULL)
        continue;
      g_signal_connect (device, "changed", G_CALLBACK (device_changed_cb), self);
      g_list_store_append (self->devices, device);
      g_signal_emit (self, signals[DEVICE_ADDED], 0, device);
    }

  g_signal_connect (self->lan, "link-established", G_CALLBACK (link_established_cb), self);
  if (!tandem_lan_start (self->lan, error))
    return FALSE;
  if (g_settings_get_boolean (tandem_config_get_settings (self->config), "mdns"))
    {
      self->mdns = tandem_mdns_new (self->config, self->lan);
      tandem_mdns_start (self->mdns);
    }
  update_periodic (self);
  return TRUE;
}

void
tandem_manager_stop (TandemManager *self)
{
  guint n;

  if (self->mdns != NULL)
    {
      tandem_mdns_stop (self->mdns);
      g_clear_object (&self->mdns);
    }
  if (self->lan != NULL)
    {
      g_signal_handlers_disconnect_by_data (self->lan, self);
      tandem_lan_stop (self->lan);
    }
  n = g_list_model_get_n_items (G_LIST_MODEL (self->devices));
  for (guint i = 0; i < n; i++)
    {
      g_autoptr(TandemDevice) device = g_list_model_get_item (G_LIST_MODEL (self->devices), i);

      g_signal_handlers_disconnect_by_data (device, self);
      tandem_device_set_link (device, NULL);
    }
}

GListModel *
tandem_manager_get_devices (TandemManager *self)
{
  return G_LIST_MODEL (self->devices);
}

TandemDevice *
tandem_manager_find (TandemManager *self, const char *id_or_name)
{
  guint n = g_list_model_get_n_items (G_LIST_MODEL (self->devices));
  TandemDevice *match = NULL;

  for (guint i = 0; i < n; i++)
    {
      g_autoptr(TandemDevice) device = g_list_model_get_item (G_LIST_MODEL (self->devices), i);

      if (g_strcmp0 (tandem_device_get_id (device), id_or_name) == 0)
        return device;
      if (g_str_has_prefix (tandem_device_get_id (device), id_or_name) ||
          g_strcmp0 (tandem_device_get_name (device), id_or_name) == 0)
        match = device;
    }
  return match;
}

TandemLan *
tandem_manager_get_lan (TandemManager *self)
{
  return self->lan;
}

void
tandem_manager_refresh (TandemManager *self)
{
  guint n = g_list_model_get_n_items (G_LIST_MODEL (self->devices));

  tandem_lan_broadcast (self->lan);
  for (guint i = 0; i < n; i++)
    {
      g_autoptr(TandemDevice) device = g_list_model_get_item (G_LIST_MODEL (self->devices), i);

      if (!tandem_device_is_reachable (device))
        tandem_lan_send_identity_to (self->lan, tandem_device_get_last_address (device));
    }
}
