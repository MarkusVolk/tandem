/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "app.h"
#include "dbus-api.h"
#include "ui/window.h"
#include "tandem-build.h"

struct _TandemApp
{
  AdwApplication parent_instance;

  TandemConfig *config;
  TandemManager *manager;
  GtkWindow *window;
  gboolean held;
};

G_DEFINE_FINAL_TYPE (TandemApp, tandem_app, ADW_TYPE_APPLICATION)

TandemConfig *
tandem_app_get_config (TandemApp *self)
{
  return self->config;
}

TandemManager *
tandem_app_get_manager (TandemApp *self)
{
  return self->manager;
}

void
tandem_app_show_window (TandemApp *self)
{
  if (self->window == NULL)
    {
      self->window = GTK_WINDOW (tandem_window_new (self));
      g_object_add_weak_pointer (G_OBJECT (self->window), (gpointer *) &self->window);
    }
  gtk_window_present (self->window);
}

void
tandem_app_quit (TandemApp *self)
{
  if (self->window != NULL)
    gtk_window_destroy (self->window);
  if (self->held)
    {
      self->held = FALSE;
      g_application_release (G_APPLICATION (self));
    }
  g_application_quit (G_APPLICATION (self));
}

void
tandem_app_refresh (TandemApp *self)
{
  if (self->manager != NULL)
    tandem_manager_refresh (self->manager);
}

void
tandem_app_describe_devices (TandemApp *self, GVariantBuilder *builder)
{
  GListModel *devices;
  guint n;

  if (self->manager == NULL)
    return;
  devices = tandem_manager_get_devices (self->manager);
  n = g_list_model_get_n_items (devices);
  for (guint i = 0; i < n; i++)
    {
      g_autoptr(TandemDevice) device = g_list_model_get_item (devices, i);
      GVariantBuilder props;

      g_variant_builder_init (&props, G_VARIANT_TYPE ("a{sv}"));
      g_variant_builder_add (&props, "{sv}", "name", g_variant_new_string (tandem_device_get_name (device)));
      g_variant_builder_add (&props, "{sv}", "type", g_variant_new_string (tandem_device_get_type_name (device)));
      g_variant_builder_add (&props, "{sv}", "state", g_variant_new_string (tandem_device_get_state_name (device)));
      g_variant_builder_add (&props, "{sv}", "paired", g_variant_new_boolean (tandem_device_is_paired (device)));
      g_variant_builder_add (&props, "{sv}", "reachable", g_variant_new_boolean (tandem_device_is_reachable (device)));
      g_variant_builder_add (&props, "{sv}", "protocolVersion",
                             g_variant_new_int32 ((gint32) tandem_device_get_protocol_version (device)));
      g_variant_builder_add (builder, "{sa{sv}}", tandem_device_get_id (device), &props);
    }
}

static void
device_added_cb (TandemManager *manager, TandemDevice *device, gpointer user_data)
{
  tandem_dbus_api_notify_device ("DeviceAdded", tandem_device_get_id (device));
}

static void
device_removed_cb (TandemManager *manager, TandemDevice *device, gpointer user_data)
{
  tandem_dbus_api_notify_device ("DeviceRemoved", tandem_device_get_id (device));
}

static void
device_changed_cb (TandemManager *manager, TandemDevice *device, gpointer user_data)
{
  tandem_dbus_api_notify_device ("DeviceChanged", tandem_device_get_id (device));
}

static void
quit_activated (GSimpleAction *action, GVariant *parameter, gpointer user_data)
{
  tandem_app_quit (TANDEM_APP (user_data));
}

static void
show_activated (GSimpleAction *action, GVariant *parameter, gpointer user_data)
{
  tandem_app_show_window (TANDEM_APP (user_data));
}

static void
refresh_activated (GSimpleAction *action, GVariant *parameter, gpointer user_data)
{
  tandem_app_refresh (TANDEM_APP (user_data));
}

static const GActionEntry app_actions[] = {
  { "quit", quit_activated },
  { "show", show_activated },
  { "refresh", refresh_activated },
};

static void
tandem_app_startup (GApplication *application)
{
  TandemApp *self = TANDEM_APP (application);
  g_autoptr(GError) error = NULL;

  G_APPLICATION_CLASS (tandem_app_parent_class)->startup (application);

  self->config = tandem_config_get_default (&error);
  if (self->config == NULL)
    g_error ("Cannot set up the configuration: %s", error->message);

  g_action_map_add_action_entries (G_ACTION_MAP (self), app_actions, G_N_ELEMENTS (app_actions), self);
  gtk_application_set_accels_for_action (GTK_APPLICATION (self), "app.quit",
                                         (const char *[]) { "<Control>q", NULL });

  if (g_application_get_flags (application) & G_APPLICATION_IS_SERVICE)
    {
      self->held = TRUE;
      g_application_hold (application);
    }

  g_message ("tandem %s, device id %s", TANDEM_VERSION, tandem_config_get_device_id (self->config));

  self->manager = tandem_manager_new (self->config);
  g_signal_connect (self->manager, "device-added", G_CALLBACK (device_added_cb), self);
  g_signal_connect (self->manager, "device-removed", G_CALLBACK (device_removed_cb), self);
  g_signal_connect (self->manager, "device-changed", G_CALLBACK (device_changed_cb), self);
  if (!tandem_manager_start (self->manager, &error))
    g_warning ("Cannot start the network: %s", error->message);
}

static void
tandem_app_activate (GApplication *application)
{
  tandem_app_show_window (TANDEM_APP (application));
}

static gboolean
tandem_app_dbus_register (GApplication     *application,
                          GDBusConnection  *connection,
                          const char       *object_path,
                          GError          **error)
{
  if (!G_APPLICATION_CLASS (tandem_app_parent_class)->dbus_register (application, connection,
                                                                      object_path, error))
    return FALSE;
  return tandem_dbus_api_register (connection, TANDEM_APP (application), error);
}

static void
tandem_app_dbus_unregister (GApplication    *application,
                            GDBusConnection *connection,
                            const char      *object_path)
{
  tandem_dbus_api_unregister (connection);
  G_APPLICATION_CLASS (tandem_app_parent_class)->dbus_unregister (application, connection, object_path);
}

static void
tandem_app_shutdown (GApplication *application)
{
  TandemApp *self = TANDEM_APP (application);

  if (self->manager != NULL)
    {
      g_signal_handlers_disconnect_by_data (self->manager, self);
      tandem_manager_stop (self->manager);
    }
  g_clear_object (&self->manager);
  g_clear_object (&self->config);
  G_APPLICATION_CLASS (tandem_app_parent_class)->shutdown (application);
}

static void
tandem_app_class_init (TandemAppClass *klass)
{
  GApplicationClass *app_class = G_APPLICATION_CLASS (klass);

  app_class->startup = tandem_app_startup;
  app_class->activate = tandem_app_activate;
  app_class->shutdown = tandem_app_shutdown;
  app_class->dbus_register = tandem_app_dbus_register;
  app_class->dbus_unregister = tandem_app_dbus_unregister;
}

static void
tandem_app_init (TandemApp *self)
{
}

TandemApp *
tandem_app_new (void)
{
  return g_object_new (TANDEM_TYPE_APP,
                       "application-id", TANDEM_APP_ID,
                       "flags", G_APPLICATION_DEFAULT_FLAGS,
                       NULL);
}
