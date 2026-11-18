#pragma once

#include <cstdint>
#include <functional>
#include <gio/gio.h>
#include <string>
#include <vector>

namespace tandem {
  // A request to the running window: come forward, or run a named action.
  struct InstanceRequest {
    // A named action of org.freedesktop.Application.ActivateAction, empty for Activate and Open.
    std::string action;
    // Its parameter: a string, or the strings of a tuple such as reply's (sss).
    std::vector<std::string> parameters;
    // xdg-activation token of the launch that sent the request, empty when there was none.
    std::string activationToken;
  };

  // Owns de.flk.Tandem on the session bus: the daemon and later launches reach the window through it.
  class InstanceService {
  public:
    enum class Role : std::uint8_t {
      // This process owns the name and receives later launches.
      Primary,
      // Another process owns the name; forward() hands this launch to it.
      Secondary,
      // No session bus or the name could not be claimed: this process runs on its own.
      Standalone,
    };
    using Handler = std::function<void(InstanceRequest)>;

    // Claims the name synchronously, before any window exists.
    InstanceService();
    ~InstanceService();
    InstanceService(const InstanceService&) = delete;
    InstanceService& operator=(const InstanceService&) = delete;

    Role role() const { return m_role; }
    // Secondary only: brings the window of the primary process forward.
    bool forward(const std::string& activationToken);
    // Primary only: requests received before the handler is set are delivered here first.
    void setHandler(Handler handler);
    // Gives the name back once the window is gone; later launches start their own primary.
    void release();

  private:
    static void methodCall(
        GDBusConnection* connection, const char* sender, const char* path, const char* interface, const char* method,
        GVariant* parameters, GDBusMethodInvocation* invocation, gpointer data
    );
    void deliver(InstanceRequest request);
    void unregister();

    Handler m_handler;
    std::vector<InstanceRequest> m_pending;
    GDBusNodeInfo* m_node = nullptr;
    GDBusConnection* m_connection = nullptr;
    guint m_registration = 0;
    Role m_role = Role::Standalone;
  };
} // namespace tandem
