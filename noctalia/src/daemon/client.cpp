#include "daemon/client.h"

#include <algorithm>
#include <cctype>
#include <print>
#include <utility>

namespace {
  constexpr const char* kName = "de.flk.Tandem.Daemon";
  constexpr const char* kManagerPath = "/de/flk/Tandem";
  constexpr const char* kManager = "de.flk.Tandem.Manager";
  constexpr const char* kDevice = "de.flk.Tandem.Device";
  constexpr int kTimeout = 30000;

  struct PendingCall {
    std::weak_ptr<bool> alive;
    tandem::CallCallback done;
  };

  void finishCall(GObject* source, GAsyncResult* result, gpointer data) {
    std::unique_ptr<PendingCall> pending(static_cast<PendingCall*>(data));
    GError* error = nullptr;
    GVariant* reply = g_dbus_connection_call_finish(G_DBUS_CONNECTION(source), result, &error);
    tandem::CallResult outcome;
    if (reply != nullptr) {
      outcome.reply = tandem::Variant::adopt(reply);
    } else {
      if (error != nullptr) {
        g_dbus_error_strip_remote_error(error);
        outcome.error = error->message;
      } else {
        outcome.error = "unknown error";
      }
      g_clear_error(&error);
    }
    if (pending->alive.expired()) {
      return;
    }
    if (pending->done) {
      pending->done(std::move(outcome));
    } else if (!outcome.ok()) {
      std::println(stderr, "tandem: {}", outcome.error);
    }
  }

  // The first child of a reply tuple, which is where every daemon method puts its answer.
  tandem::Variant first(const tandem::Variant& reply) {
    auto children = reply.children();
    return children.empty() ? tandem::Variant() : children.front();
  }
} // namespace

std::string tandem::devicePath(const std::string& id) {
  std::string path = "/de/flk/Tandem/devices/d_";
  for (const char c : id) {
    path += std::isalnum(static_cast<unsigned char>(c)) ? c : '_';
  }
  return path;
}

// ── Device ───────────────────────────────────────────────────────────────────

tandem::Device::Device(DaemonClient& client, std::string id, GDBusProxy* proxy)
    : m_client(client), m_id(std::move(id)), m_proxy(proxy) {
  m_propertiesHandler = g_signal_connect(m_proxy, "g-properties-changed", G_CALLBACK(propertiesChanged), this);
  m_signalHandler = g_signal_connect(m_proxy, "g-signal", G_CALLBACK(signal), this);
}

tandem::Device::~Device() {
  g_signal_handler_disconnect(m_proxy, m_propertiesHandler);
  g_signal_handler_disconnect(m_proxy, m_signalHandler);
  g_object_unref(m_proxy);
}

tandem::Variant tandem::Device::property(const char* name) const {
  return Variant::adopt(g_dbus_proxy_get_cached_property(m_proxy, name));
}

std::string tandem::Device::name() const {
  std::string name = property("Name").string();
  return name.empty() ? m_id : name;
}

std::string tandem::Device::type() const { return property("Type").string(); }
std::string tandem::Device::pairState() const { return property("PairState").string(); }
bool tandem::Device::paired() const { return property("Paired").boolean(); }
bool tandem::Device::reachable() const { return property("Reachable").boolean(); }
bool tandem::Device::paused() const { return property("Paused").boolean(); }
std::string tandem::Device::verificationKey() const { return property("VerificationKey").string(); }
std::vector<std::string> tandem::Device::plugins() const { return property("Plugins").strings(); }

bool tandem::Device::hasPlugin(const std::string& plugin) const {
  const auto loaded = plugins();
  return std::ranges::find(loaded, plugin) != loaded.end();
}

tandem::Variant tandem::Device::pluginState(const std::string& plugin) const {
  const auto found = m_states.find(plugin);
  return found == m_states.end() ? Variant() : found->second;
}

void tandem::Device::call(const char* method, GVariant* parameters, CallCallback done) {
  m_client.call(devicePath(m_id).c_str(), kDevice, method, parameters, std::move(done));
}

void tandem::Device::callPlugin(const std::string& plugin, const char* method, GVariant* args, CallCallback done) {
  if (args == nullptr) {
    args = VariantMap().end();
  }
  call("CallPlugin", g_variant_new("(ss@a{sv})", plugin.c_str(), method, args), std::move(done));
}

void tandem::Device::setSetting(const char* key, GVariant* value) {
  call("SetSetting", g_variant_new("(sv)", key, value), [weak = weak_from_this()](CallResult result) {
    if (!result.ok()) {
      std::println(stderr, "tandem: {}", result.error);
      if (auto self = weak.lock()) {
        self->refreshSettings();
      }
    }
  });
}

void tandem::Device::refreshPlugin(const std::string& plugin) {
  call("GetPluginState", g_variant_new("(s)", plugin.c_str()), [weak = weak_from_this(), plugin](CallResult result) {
    auto self = weak.lock();
    if (!self || !result.ok()) {
      return;
    }
    self->m_states[plugin] = first(result.reply);
    self->m_client.emit({.kind = DaemonEvent::Kind::Plugin, .device = self->m_id, .plugin = plugin});
  });
}

void tandem::Device::refreshSettings() {
  call("GetSettings", nullptr, [weak = weak_from_this()](CallResult result) {
    auto self = weak.lock();
    if (!self || !result.ok()) {
      return;
    }
    self->m_settings = first(result.reply);
    self->m_client.emit({.kind = DaemonEvent::Kind::DeviceSettings, .device = self->m_id, .plugin = {}});
  });
}

void tandem::Device::refreshPlugins() {
  auto loaded = plugins();
  std::erase_if(m_states, [&loaded](const auto& entry) { return std::ranges::find(loaded, entry.first) == loaded.end(); });
  for (const auto& plugin : loaded) {
    if (std::ranges::find(m_knownPlugins, plugin) == m_knownPlugins.end()) {
      refreshPlugin(plugin);
    }
  }
  m_knownPlugins = std::move(loaded);
}

void tandem::Device::start() {
  refreshSettings();
  refreshPlugins();
}

void tandem::Device::propertiesChanged(GDBusProxy* /*proxy*/, GVariant* changed, GStrv /*invalidated*/, gpointer data) {
  auto* self = static_cast<Device*>(data);
  if (Variant::adopt(g_variant_lookup_value(changed, "Plugins", nullptr))) {
    self->refreshPlugins();
  }
  self->m_client.emit({.kind = DaemonEvent::Kind::Device, .device = self->m_id, .plugin = {}});
}

void tandem::Device::signal(
    GDBusProxy* /*proxy*/, const char* /*sender*/, const char* name, GVariant* parameters, gpointer data
) {
  auto* self = static_cast<Device*>(data);
  const std::string_view signal(name);
  if (signal == "PluginChanged") {
    const char* plugin = nullptr;
    g_variant_get(parameters, "(&s)", &plugin);
    self->refreshPlugin(plugin);
  } else if (signal == "SettingsChanged") {
    self->refreshSettings();
  }
}

// ── DaemonClient ─────────────────────────────────────────────────────────────

tandem::DaemonClient::DaemonClient() {
  GError* error = nullptr;
  m_connection = g_bus_get_sync(G_BUS_TYPE_SESSION, nullptr, &error);
  if (m_connection == nullptr) {
    std::println(stderr, "tandem: no session bus: {}", error->message);
    g_error_free(error);
    return;
  }
  m_subscription = g_dbus_connection_signal_subscribe(
      m_connection, kName, kManager, nullptr, kManagerPath, nullptr, G_DBUS_SIGNAL_FLAGS_NONE, managerSignal, this,
      nullptr
  );
  m_watch = g_bus_watch_name_on_connection(
      m_connection, kName, G_BUS_NAME_WATCHER_FLAGS_AUTO_START, appeared, vanished, this, nullptr
  );
}

tandem::DaemonClient::~DaemonClient() {
  *m_alive = false;
  m_alive.reset();
  m_devices.clear();
  if (m_watch != 0) {
    g_bus_unwatch_name(m_watch);
  }
  if (m_subscription != 0) {
    g_dbus_connection_signal_unsubscribe(m_connection, m_subscription);
  }
  if (m_connection != nullptr) {
    g_object_unref(m_connection);
  }
}

std::vector<std::shared_ptr<tandem::Device>> tandem::DaemonClient::devices() const {
  std::vector<std::shared_ptr<Device>> result;
  for (const auto& [id, device] : m_devices) {
    result.push_back(device);
  }
  std::ranges::stable_sort(result, [](const auto& a, const auto& b) {
    if (a->paired() != b->paired()) {
      return a->paired();
    }
    return a->name() < b->name();
  });
  return result;
}

std::shared_ptr<tandem::Device> tandem::DaemonClient::device(const std::string& id) const {
  const auto found = m_devices.find(id);
  return found == m_devices.end() ? nullptr : found->second;
}

void tandem::DaemonClient::call(
    const char* path, const char* interface, const char* method, GVariant* parameters, CallCallback done
) {
  if (m_connection == nullptr || !m_running) {
    if (parameters != nullptr) {
      g_variant_unref(g_variant_ref_sink(parameters));
    }
    if (done) {
      done({.reply = {}, .error = "the Tandem service is not running"});
    }
    return;
  }
  g_dbus_connection_call(
      m_connection, kName, path, interface, method, parameters, nullptr, G_DBUS_CALL_FLAGS_NONE, kTimeout, nullptr,
      finishCall, new PendingCall{.alive = m_alive, .done = std::move(done)}
  );
}

void tandem::DaemonClient::refresh() {
  if (m_running || m_connection == nullptr) {
    call(kManagerPath, kManager, "Refresh", nullptr, {});
    return;
  }
  g_dbus_connection_call(
      m_connection, "org.freedesktop.DBus", "/org/freedesktop/DBus", "org.freedesktop.DBus", "StartServiceByName",
      g_variant_new("(su)", kName, 0U), nullptr, G_DBUS_CALL_FLAGS_NONE, kTimeout, nullptr,
      finishCall, new PendingCall{.alive = m_alive, .done = {}}
  );
}

void tandem::DaemonClient::setSetting(const char* key, GVariant* value) {
  call(kManagerPath, kManager, "SetSetting", g_variant_new("(sv)", key, value), [this](CallResult result) {
    if (!result.ok()) {
      std::println(stderr, "tandem: {}", result.error);
      loadSettings();
    }
  });
}

std::size_t tandem::DaemonClient::addListener(Listener listener) {
  m_listeners.emplace(m_nextListener, std::move(listener));
  return m_nextListener++;
}

void tandem::DaemonClient::removeListener(std::size_t handle) { m_listeners.erase(handle); }

void tandem::DaemonClient::emit(const DaemonEvent& event) {
  // A listener may add or remove listeners while it runs.
  std::vector<std::size_t> handles;
  for (const auto& [handle, listener] : m_listeners) {
    handles.push_back(handle);
  }
  for (const auto handle : handles) {
    if (const auto found = m_listeners.find(handle); found != m_listeners.end()) {
      auto listener = found->second;
      listener(event);
    }
  }
}

void tandem::DaemonClient::appeared(GDBusConnection* /*connection*/, const char* /*name*/, const char* /*owner*/, gpointer data) {
  auto* self = static_cast<DaemonClient*>(data);
  self->m_running = true;
  self->emit({.kind = DaemonEvent::Kind::Running, .device = {}, .plugin = {}});
  self->call(kManagerPath, kManager, "SelfId", nullptr, [self](CallResult result) {
    if (result.ok()) {
      self->m_selfId = first(result.reply).string();
      self->emit({.kind = DaemonEvent::Kind::Settings, .device = {}, .plugin = {}});
    }
  });
  self->call(kManagerPath, kManager, "ListPlugins", nullptr, [self](CallResult result) {
    if (result.ok()) {
      self->m_plugins = first(result.reply).strings();
      self->emit({.kind = DaemonEvent::Kind::Settings, .device = {}, .plugin = {}});
    }
  });
  self->loadSettings();
  self->loadDevices();
}

void tandem::DaemonClient::vanished(GDBusConnection* /*connection*/, const char* /*name*/, gpointer data) {
  auto* self = static_cast<DaemonClient*>(data);
  const bool was = self->m_running;
  self->m_running = false;
  self->clear();
  if (was) {
    self->emit({.kind = DaemonEvent::Kind::Running, .device = {}, .plugin = {}});
  }
}

void tandem::DaemonClient::clear() {
  m_devices.clear();
  m_pendingDevices.clear();
  emit({.kind = DaemonEvent::Kind::Devices, .device = {}, .plugin = {}});
}

void tandem::DaemonClient::managerSignal(
    GDBusConnection* /*connection*/, const char* /*sender*/, const char* /*path*/, const char* /*interface*/,
    const char* signal, GVariant* parameters, gpointer data
) {
  auto* self = static_cast<DaemonClient*>(data);
  const std::string_view name(signal);
  if (name == "SettingsChanged") {
    self->loadSettings();
    return;
  }
  const char* id = nullptr;
  if (!g_variant_is_of_type(parameters, G_VARIANT_TYPE("(s)"))) {
    return;
  }
  g_variant_get(parameters, "(&s)", &id);
  if (name == "DeviceAdded") {
    self->addDevice(id);
  } else if (name == "DeviceRemoved") {
    self->removeDevice(id);
  } else if (name == "DeviceChanged") {
    self->emit({.kind = DaemonEvent::Kind::Device, .device = id, .plugin = {}});
  }
}

void tandem::DaemonClient::loadSettings() {
  call(kManagerPath, kManager, "GetSettings", nullptr, [this](CallResult result) {
    if (result.ok()) {
      m_settings = first(result.reply);
      emit({.kind = DaemonEvent::Kind::Settings, .device = {}, .plugin = {}});
    }
  });
}

void tandem::DaemonClient::loadDevices() {
  call(kManagerPath, kManager, "ListDevices", nullptr, [this](CallResult result) {
    if (!result.ok()) {
      return;
    }
    const Variant devices = first(result.reply);
    for (const auto& entry : devices.children()) {
      const auto fields = entry.children();
      if (!fields.empty()) {
        addDevice(fields.front().string());
      }
    }
  });
}

void tandem::DaemonClient::addDevice(const std::string& id) {
  if (m_devices.contains(id) || std::ranges::find(m_pendingDevices, id) != m_pendingDevices.end()) {
    return;
  }
  m_pendingDevices.push_back(id);
  struct Pending {
    std::weak_ptr<bool> alive;
    DaemonClient* client;
    std::string id;
  };
  g_dbus_proxy_new(
      m_connection, G_DBUS_PROXY_FLAGS_DO_NOT_AUTO_START, nullptr, kName, devicePath(id).c_str(), kDevice, nullptr,
      [](GObject* /*source*/, GAsyncResult* result, gpointer data) {
        std::unique_ptr<Pending> pending(static_cast<Pending*>(data));
        GError* error = nullptr;
        GDBusProxy* proxy = g_dbus_proxy_new_finish(result, &error);
        if (pending->alive.expired()) {
          if (proxy != nullptr) {
            g_object_unref(proxy);
          }
          g_clear_error(&error);
          return;
        }
        DaemonClient* self = pending->client;
        const auto waiting = std::ranges::find(self->m_pendingDevices, pending->id);
        if (waiting == self->m_pendingDevices.end()) {
          // Removed, or the daemon went away, before the proxy was ready.
          if (proxy != nullptr) {
            g_object_unref(proxy);
          }
          g_clear_error(&error);
          return;
        }
        self->m_pendingDevices.erase(waiting);
        if (proxy == nullptr) {
          std::println(stderr, "tandem: device {}: {}", pending->id, error ? error->message : "no proxy");
          g_clear_error(&error);
          return;
        }
        auto device = std::make_shared<Device>(*self, pending->id, proxy);
        self->m_devices[pending->id] = device;
        device->start();
        self->emit({.kind = DaemonEvent::Kind::Devices, .device = pending->id, .plugin = {}});
      },
      new Pending{.alive = m_alive, .client = this, .id = id}
  );
}

void tandem::DaemonClient::removeDevice(const std::string& id) {
  std::erase(m_pendingDevices, id);
  if (m_devices.erase(id) > 0) {
    emit({.kind = DaemonEvent::Kind::Devices, .device = id, .plugin = {}});
  }
}
