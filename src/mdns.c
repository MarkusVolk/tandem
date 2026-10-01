/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "mdns.h"
#include "packet.h"

#define AVAHI_NAME "org.freedesktop.Avahi"
#define AVAHI_SERVER "org.freedesktop.Avahi.Server"
#define AVAHI_GROUP "org.freedesktop.Avahi.EntryGroup"
#define AVAHI_BROWSER "org.freedesktop.Avahi.ServiceBrowser"
#define SERVICE_TYPE "_kdeconnect._udp"

struct _TandemMdns
{
  GObject parent_instance;

  TandemConfig *config;
  TandemLan *lan;
  GDBusConnection *bus;
  guint avahi_watch;
  char *group_path;
  char *browser_path;
  guint item_new_subscription;
  GCancellable *cancellable;
};

G_DEFINE_FINAL_TYPE (TandemMdns, tandem_mdns, G_TYPE_OBJECT)

static void
add_txt (GVariantBuilder *builder, const char *key, const char *value)
{
  g_autofree char *entry = g_strdup_printf ("%s=%s", key, value);

  g_variant_builder_add (builder, "@ay", g_variant_new_bytestring (entry));
}

static void
group_committed_cb (GObject *source, GAsyncResult *result, gpointer user_data)
{
  g_autoptr(GError) error = NULL;
  g_autoptr(GVariant) reply = g_dbus_connection_call_finish (G_DBUS_CONNECTION (source), result, &error);

  if (reply == NULL)
    g_debug ("mdns: commit failed: %s", error->message);
}

static void
service_added_cb (GObject *source, GAsyncResult *result, gpointer user_data)
{
  TandemMdns *self = user_data;
  g_autoptr(GError) error = NULL;
  g_autoptr(GVariant) reply = g_dbus_connection_call_finish (G_DBUS_CONNECTION (source), result, &error);

  if (reply == NULL)
    {
      g_debug ("mdns: AddService failed: %s", error->message);
      return;
    }
  g_dbus_connection_call (self->bus, AVAHI_NAME, self->group_path, AVAHI_GROUP, "Commit", NULL, NULL,
                          G_DBUS_CALL_FLAGS_NONE, 5000, self->cancellable, group_committed_cb, NULL);
}

static void
group_created_cb (GObject *source, GAsyncResult *result, gpointer user_data)
{
  TandemMdns *self = user_data;
  g_autoptr(GError) error = NULL;
  g_autoptr(GVariant) reply = g_dbus_connection_call_finish (G_DBUS_CONNECTION (source), result, &error);
  GVariantBuilder txt;
  g_autofree char *name = NULL;
  g_autofree char *protocol = g_strdup_printf ("%d", TANDEM_PROTOCOL_VERSION);

  if (reply == NULL)
    {
      g_debug ("mdns: EntryGroupNew failed: %s", error->message);
      return;
    }
  g_variant_get (reply, "(o)", &self->group_path);
  name = tandem_config_get_device_name (self->config);
  g_variant_builder_init (&txt, G_VARIANT_TYPE ("aay"));
  add_txt (&txt, "id", tandem_config_get_device_id (self->config));
  add_txt (&txt, "name", name);
  add_txt (&txt, "type", tandem_config_get_device_type (self->config));
  add_txt (&txt, "protocol", protocol);
  g_dbus_connection_call (self->bus, AVAHI_NAME, self->group_path, AVAHI_GROUP, "AddService",
                          g_variant_new ("(iiussssqaay)", -1, -1, 0, tandem_config_get_device_id (self->config),
                                         SERVICE_TYPE, "", "", (guint16) TANDEM_UDP_PORT, &txt),
                          NULL, G_DBUS_CALL_FLAGS_NONE, 5000, self->cancellable, service_added_cb, self);
}

static void
resolved_cb (GObject *source, GAsyncResult *result, gpointer user_data)
{
  TandemMdns *self = user_data;
  g_autoptr(GError) error = NULL;
  g_autoptr(GVariant) reply = g_dbus_connection_call_finish (G_DBUS_CONNECTION (source), result, &error);
  const char *name, *address;
  g_autoptr(GInetAddress) inet = NULL;

  if (reply == NULL)
    {
      g_debug ("mdns: resolve failed: %s", error->message);
      return;
    }
  g_variant_get (reply, "(ii&sss&si&sq@aayu)", NULL, NULL, &name, NULL, NULL, NULL, NULL, &address, NULL, NULL, NULL);
  if (g_strcmp0 (name, tandem_config_get_device_id (self->config)) == 0)
    return;
  inet = g_inet_address_new_from_string (address);
  if (inet == NULL)
    return;
  g_debug ("mdns: %s at %s", name, address);
  tandem_lan_send_identity_to (self->lan, inet);
}

static void
item_new_cb (GDBusConnection *connection,
             const char      *sender,
             const char      *object_path,
             const char      *interface_name,
             const char      *signal_name,
             GVariant        *parameters,
             gpointer         user_data)
{
  TandemMdns *self = user_data;
  gint32 interface, protocol;
  const char *name, *type, *domain;

  g_variant_get (parameters, "(ii&s&s&su)", &interface, &protocol, &name, &type, &domain, NULL);
  g_dbus_connection_call (self->bus, AVAHI_NAME, "/", AVAHI_SERVER, "ResolveService",
                          g_variant_new ("(iisssiu)", interface, protocol, name, type, domain, -1, 0u),
                          NULL, G_DBUS_CALL_FLAGS_NONE, 10000, self->cancellable, resolved_cb, self);
}

static void
browser_created_cb (GObject *source, GAsyncResult *result, gpointer user_data)
{
  TandemMdns *self = user_data;
  g_autoptr(GError) error = NULL;
  g_autoptr(GVariant) reply = g_dbus_connection_call_finish (G_DBUS_CONNECTION (source), result, &error);

  if (reply == NULL)
    {
      g_debug ("mdns: ServiceBrowserNew failed: %s", error->message);
      return;
    }
  g_variant_get (reply, "(o)", &self->browser_path);
}

static void
avahi_appeared_cb (GDBusConnection *connection, const char *name, const char *owner, gpointer user_data)
{
  TandemMdns *self = user_data;

  g_debug ("mdns: avahi available");
  g_clear_pointer (&self->group_path, g_free);
  g_clear_pointer (&self->browser_path, g_free);
  if (self->item_new_subscription == 0)
    self->item_new_subscription = g_dbus_connection_signal_subscribe (self->bus, AVAHI_NAME, AVAHI_BROWSER, "ItemNew",
                                                                      NULL, NULL, G_DBUS_SIGNAL_FLAGS_NONE,
                                                                      item_new_cb, self, NULL);
  g_dbus_connection_call (self->bus, AVAHI_NAME, "/", AVAHI_SERVER, "EntryGroupNew", NULL, NULL,
                          G_DBUS_CALL_FLAGS_NONE, 5000, self->cancellable, group_created_cb, self);
  g_dbus_connection_call (self->bus, AVAHI_NAME, "/", AVAHI_SERVER, "ServiceBrowserNew",
                          g_variant_new ("(iissu)", -1, -1, SERVICE_TYPE, "", 0u), NULL,
                          G_DBUS_CALL_FLAGS_NONE, 5000, self->cancellable, browser_created_cb, self);
}

static void
avahi_vanished_cb (GDBusConnection *connection, const char *name, gpointer user_data)
{
  TandemMdns *self = user_data;

  g_clear_pointer (&self->group_path, g_free);
  g_clear_pointer (&self->browser_path, g_free);
}

void
tandem_mdns_start (TandemMdns *self)
{
  if (self->bus == NULL)
    self->bus = g_bus_get_sync (G_BUS_TYPE_SYSTEM, NULL, NULL);
  if (self->bus == NULL || self->avahi_watch != 0)
    return;
  self->cancellable = g_cancellable_new ();
  self->avahi_watch = g_bus_watch_name_on_connection (self->bus, AVAHI_NAME, G_BUS_NAME_WATCHER_FLAGS_NONE,
                                                      avahi_appeared_cb, avahi_vanished_cb, self, NULL);
}

void
tandem_mdns_stop (TandemMdns *self)
{
  g_cancellable_cancel (self->cancellable);
  g_clear_object (&self->cancellable);
  g_clear_handle_id (&self->avahi_watch, g_bus_unwatch_name);
  if (self->bus != NULL && self->item_new_subscription != 0)
    {
      g_dbus_connection_signal_unsubscribe (self->bus, self->item_new_subscription);
      self->item_new_subscription = 0;
    }
  if (self->bus != NULL && self->group_path != NULL)
    g_dbus_connection_call (self->bus, AVAHI_NAME, self->group_path, AVAHI_GROUP, "Free", NULL, NULL,
                            G_DBUS_CALL_FLAGS_NONE, 2000, NULL, NULL, NULL);
  if (self->bus != NULL && self->browser_path != NULL)
    g_dbus_connection_call (self->bus, AVAHI_NAME, self->browser_path, AVAHI_BROWSER, "Free", NULL, NULL,
                            G_DBUS_CALL_FLAGS_NONE, 2000, NULL, NULL, NULL);
  g_clear_pointer (&self->group_path, g_free);
  g_clear_pointer (&self->browser_path, g_free);
}

TandemMdns *
tandem_mdns_new (TandemConfig *config, TandemLan *lan)
{
  TandemMdns *self = g_object_new (TANDEM_TYPE_MDNS, NULL);

  self->config = g_object_ref (config);
  self->lan = g_object_ref (lan);
  return self;
}

static void
tandem_mdns_dispose (GObject *object)
{
  TandemMdns *self = TANDEM_MDNS (object);

  tandem_mdns_stop (self);
  g_clear_object (&self->bus);
  g_clear_object (&self->lan);
  g_clear_object (&self->config);
  G_OBJECT_CLASS (tandem_mdns_parent_class)->dispose (object);
}

static void
tandem_mdns_class_init (TandemMdnsClass *klass)
{
  G_OBJECT_CLASS (klass)->dispose = tandem_mdns_dispose;
}

static void
tandem_mdns_init (TandemMdns *self)
{
}
