#pragma once

#include "daemon/variant.h"

#include <functional>
#include <gio/gio.h>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace tandem {
  // The answer of a daemon call: the reply, or an error message when `reply` is empty.
  struct CallResult {
    Variant reply;
    std::string error;
    bool ok() const { return static_cast<bool>(reply); }
  };
  using CallCallback = std::function<void(CallResult)>;

  // What changed, for the window to redraw what shows it.
  struct DaemonEvent {
    enum class Kind : std::uint8_t {
      Running,
      Devices,
      Device,
      Plugin,
      Settings,
      DeviceSettings,
    };
    Kind kind;
    std::string device;
    std::string plugin;
  };

  class DaemonClient;

  // One device the daemon knows, as seen through its D-Bus object.
  class Device : public std::enable_shared_from_this<Device> {
  public:
    Device(DaemonClient& client, std::string id, GDBusProxy* proxy);
    ~Device();
    Device(const Device&) = delete;
    Device& operator=(const Device&) = delete;

    const std::string& id() const noexcept { return m_id; }
    std::string name() const;
    std::string type() const;
    // paired-reachable, paired, requested, requested-by-peer, reachable or unreachable.
    std::string pairState() const;
    bool paired() const;
    bool reachable() const;
    bool paused() const;
    std::string verificationKey() const;
    // The loaded plugins; only a paired device has any.
    std::vector<std::string> plugins() const;
    bool hasPlugin(const std::string& plugin) const;

    // The last GetPluginState answer of a plugin; empty until it arrived.
    Variant pluginState(const std::string& plugin) const;
    // The de.flk.Tandem.Device settings of this device.
    const Variant& settings() const noexcept { return m_settings; }
    bool setting(const char* key, bool fallback = false) const { return m_settings.boolean(key, fallback); }

    void call(const char* method, GVariant* parameters = nullptr, CallCallback done = {});
    void callPlugin(const std::string& plugin, const char* method, GVariant* args = nullptr, CallCallback done = {});
    void setSetting(const char* key, GVariant* value);
    void refreshPlugin(const std::string& plugin);

  private:
    friend class DaemonClient;
    void start();
    void refreshSettings();
    void refreshPlugins();
    Variant property(const char* name) const;
    static void propertiesChanged(GDBusProxy* proxy, GVariant* changed, GStrv invalidated, gpointer data);
    static void signal(GDBusProxy* proxy, const char* sender, const char* name, GVariant* parameters, gpointer data);

    DaemonClient& m_client;
    std::string m_id;
    GDBusProxy* m_proxy;
    std::map<std::string, Variant> m_states;
    std::vector<std::string> m_knownPlugins;
    Variant m_settings;
    gulong m_propertiesHandler = 0;
    gulong m_signalHandler = 0;
  };

  // The daemon on the session bus: its devices, settings and signals.
  class DaemonClient {
  public:
    using Listener = std::function<void(const DaemonEvent&)>;

    DaemonClient();
    ~DaemonClient();
    DaemonClient(const DaemonClient&) = delete;
    DaemonClient& operator=(const DaemonClient&) = delete;

    bool running() const noexcept { return m_running; }
    // Paired devices first, then by name.
    std::vector<std::shared_ptr<Device>> devices() const;
    std::shared_ptr<Device> device(const std::string& id) const;
    const std::string& selfId() const noexcept { return m_selfId; }
    const std::vector<std::string>& plugins() const noexcept { return m_plugins; }
    const Variant& settings() const noexcept { return m_settings; }

    // Starts the daemon by D-Bus activation when it is not running, else looks for devices.
    void refresh();
    void setSetting(const char* key, GVariant* value);

    // Returns a handle for removeListener.
    std::size_t addListener(Listener listener);
    void removeListener(std::size_t handle);

    GDBusConnection* connection() const noexcept { return m_connection; }
    void emit(const DaemonEvent& event);
    // An async call to the daemon; `done` is not run once the client is gone.
    void call(const char* path, const char* interface, const char* method, GVariant* parameters, CallCallback done);

  private:
    static void appeared(GDBusConnection* connection, const char* name, const char* owner, gpointer data);
    static void vanished(GDBusConnection* connection, const char* name, gpointer data);
    static void managerSignal(
        GDBusConnection* connection, const char* sender, const char* path, const char* interface, const char* signal,
        GVariant* parameters, gpointer data
    );
    void loadDevices();
    void addDevice(const std::string& id);
    void removeDevice(const std::string& id);
    void loadSettings();
    void clear();

    GDBusConnection* m_connection = nullptr;
    guint m_watch = 0;
    guint m_subscription = 0;
    bool m_running = false;
    std::map<std::string, std::shared_ptr<Device>> m_devices;
    std::vector<std::string> m_pendingDevices;
    std::string m_selfId;
    std::vector<std::string> m_plugins;
    Variant m_settings;
    std::map<std::size_t, Listener> m_listeners;
    std::size_t m_nextListener = 1;
    std::shared_ptr<bool> m_alive = std::make_shared<bool>(true);
  };

  // The object path of a device id, as the daemon builds it.
  std::string devicePath(const std::string& id);
} // namespace tandem
