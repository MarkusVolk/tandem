#include "app/instance_service.h"

#include "core/log.h"

#include <exception>
#include <string_view>
#include <utility>

namespace {
  constexpr const char* kBusName = "de.flk.Tandem";
  constexpr const char* kObjectPath = "/de/flk/Tandem";
  constexpr const char* kInterface = "org.freedesktop.Application";
  constexpr guint32 kDoNotQueue = 4;
  constexpr guint32 kPrimaryOwner = 1;
  constexpr guint32 kExists = 3;
  constexpr const char* kIntrospection = R"(<node>
  <interface name="org.freedesktop.Application">
    <method name="Activate">
      <arg type="a{sv}" name="platform_data" direction="in"/>
    </method>
    <method name="Open">
      <arg type="as" name="uris" direction="in"/>
      <arg type="a{sv}" name="platform_data" direction="in"/>
    </method>
    <method name="ActivateAction">
      <arg type="s" name="action_name" direction="in"/>
      <arg type="av" name="parameter" direction="in"/>
      <arg type="a{sv}" name="platform_data" direction="in"/>
    </method>
  </interface>
</node>)";

  const Logger kLog("instance-dbus");

  std::string activationToken(GVariant* platformData) {
    const char* token = nullptr;
    if (g_variant_lookup(platformData, "activation-token", "&s", &token) && token != nullptr) {
      return token;
    }
    return {};
  }
} // namespace

tandem::InstanceService::InstanceService() : m_node(g_dbus_node_info_new_for_xml(kIntrospection, nullptr)) {
  GError* error = nullptr;
  m_connection = g_bus_get_sync(G_BUS_TYPE_SESSION, nullptr, &error);
  if (m_connection == nullptr) {
    kLog.debug("no session bus; running as a separate instance: {}", error->message);
    g_error_free(error);
    return;
  }
  // Export before owning the name: a launch that finds the name always finds the object.
  static const GDBusInterfaceVTable vtable = {
      .method_call = methodCall, .get_property = nullptr, .set_property = nullptr, .padding = {}
  };
  m_registration = g_dbus_connection_register_object(
      m_connection, kObjectPath, m_node->interfaces[0], &vtable, this, nullptr, &error
  );
  if (m_registration == 0) {
    kLog.warn("cannot export {}: {}", kObjectPath, error->message);
    g_error_free(error);
    return;
  }
  GVariant* reply = g_dbus_connection_call_sync(
      m_connection, "org.freedesktop.DBus", "/org/freedesktop/DBus", "org.freedesktop.DBus", "RequestName",
      g_variant_new("(su)", kBusName, kDoNotQueue), G_VARIANT_TYPE("(u)"), G_DBUS_CALL_FLAGS_NONE, -1, nullptr, &error
  );
  if (reply == nullptr) {
    kLog.warn("cannot claim {}: {}", kBusName, error->message);
    g_error_free(error);
    unregister();
    return;
  }
  guint32 result = 0;
  g_variant_get(reply, "(u)", &result);
  g_variant_unref(reply);
  if (result == kPrimaryOwner) {
    m_role = Role::Primary;
    return;
  }
  unregister();
  m_role = result == kExists ? Role::Secondary : Role::Standalone;
}

tandem::InstanceService::~InstanceService() {
  release();
  if (m_connection != nullptr) {
    g_object_unref(m_connection);
  }
  g_dbus_node_info_unref(m_node);
}

bool tandem::InstanceService::forward(const std::string& activationToken) {
  if (m_role != Role::Secondary) {
    return false;
  }
  GVariantBuilder platform;
  g_variant_builder_init(&platform, G_VARIANT_TYPE_VARDICT);
  if (!activationToken.empty()) {
    g_variant_builder_add(&platform, "{sv}", "activation-token", g_variant_new_string(activationToken.c_str()));
  }
  GError* error = nullptr;
  GVariant* reply = g_dbus_connection_call_sync(
      m_connection, kBusName, kObjectPath, kInterface, "Activate", g_variant_new("(a{sv})", &platform), nullptr,
      G_DBUS_CALL_FLAGS_NO_AUTO_START, -1, nullptr, &error
  );
  if (reply == nullptr) {
    kLog.warn("cannot reach the running instance: {}", error->message);
    g_error_free(error);
    return false;
  }
  g_variant_unref(reply);
  return true;
}

void tandem::InstanceService::setHandler(Handler handler) {
  m_handler = std::move(handler);
  auto pending = std::exchange(m_pending, {});
  for (auto& request : pending) {
    deliver(std::move(request));
  }
}

void tandem::InstanceService::release() {
  if (m_role != Role::Primary) {
    return;
  }
  m_role = Role::Standalone;
  unregister();
  GError* error = nullptr;
  GVariant* reply = g_dbus_connection_call_sync(
      m_connection, "org.freedesktop.DBus", "/org/freedesktop/DBus", "org.freedesktop.DBus", "ReleaseName",
      g_variant_new("(s)", kBusName), G_VARIANT_TYPE("(u)"), G_DBUS_CALL_FLAGS_NONE, -1, nullptr, &error
  );
  if (reply == nullptr) {
    kLog.warn("cannot release {}: {}", kBusName, error->message);
    g_error_free(error);
    return;
  }
  g_variant_unref(reply);
}

void tandem::InstanceService::unregister() {
  if (m_registration != 0) {
    g_dbus_connection_unregister_object(m_connection, m_registration);
    m_registration = 0;
  }
}

void tandem::InstanceService::deliver(InstanceRequest request) {
  if (!m_handler) {
    m_pending.push_back(std::move(request));
    return;
  }
  // Exceptions must not unwind through GLib's C frames.
  try {
    m_handler(std::move(request));
  } catch (const std::exception& error) {
    kLog.warn("cannot handle a request of another launch: {}", error.what());
  }
}

void tandem::InstanceService::methodCall(
    GDBusConnection* /*connection*/, const char* /*sender*/, const char* /*path*/, const char* /*interface*/,
    const char* method, GVariant* parameters, GDBusMethodInvocation* invocation, gpointer data
) {
  auto* self = static_cast<InstanceService*>(data);
  const std::string_view name(method);
  InstanceRequest request;
  if (name == "Activate") {
    GVariant* platform = g_variant_get_child_value(parameters, 0);
    request.activationToken = activationToken(platform);
    g_variant_unref(platform);
  } else if (name == "Open") {
    GVariant* platform = g_variant_get_child_value(parameters, 1);
    request.activationToken = activationToken(platform);
    g_variant_unref(platform);
  } else if (name == "ActivateAction") {
    const char* action = nullptr;
    GVariant* values = nullptr;
    GVariant* platform = nullptr;
    g_variant_get(parameters, "(&s@av@a{sv})", &action, &values, &platform);
    request.action = action;
    if (g_variant_n_children(values) > 0) {
      GVariant* boxed = g_variant_get_child_value(values, 0);
      GVariant* value = g_variant_get_variant(boxed);
      if (g_variant_is_of_type(value, G_VARIANT_TYPE_STRING)) {
        request.parameters.emplace_back(g_variant_get_string(value, nullptr));
      } else if (g_variant_is_container(value)) {
        for (gsize i = 0; i < g_variant_n_children(value); ++i) {
          GVariant* child = g_variant_get_child_value(value, i);
          request.parameters.emplace_back(
              g_variant_is_of_type(child, G_VARIANT_TYPE_STRING) ? g_variant_get_string(child, nullptr) : ""
          );
          g_variant_unref(child);
        }
      }
      g_variant_unref(value);
      g_variant_unref(boxed);
    }
    request.activationToken = activationToken(platform);
    g_variant_unref(values);
    g_variant_unref(platform);
  } else {
    g_dbus_method_invocation_return_error(
        invocation, G_DBUS_ERROR, G_DBUS_ERROR_NOT_SUPPORTED, "Tandem has no method %s", method
    );
    return;
  }
  self->deliver(std::move(request));
  g_dbus_method_invocation_return_value(invocation, nullptr);
}
