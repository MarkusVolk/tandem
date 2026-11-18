#pragma once

#include "daemon/client.h"
#include "platform/desktop_integration.h"

#include <memory>
#include <string>

class AnimationManager;
class Box;
class Node;
class RenderContext;
class WaylandConnection;

namespace tandem {
  // A transparent full-screen layer with a red dot where the device's presenter points; it takes
  // no input and closes when the presenter stops.
  class PresenterWindow {
  public:
    PresenterWindow(
        WaylandConnection& connection, RenderContext& renderer, AnimationManager& animations, DaemonClient& daemon,
        std::string id
    );
    ~PresenterWindow();
    PresenterWindow(const PresenterWindow&) = delete;
    PresenterWindow& operator=(const PresenterWindow&) = delete;

    const std::string& deviceId() const noexcept { return m_id; }
    bool running() const { return m_surface.isRunning(); }
    wl_surface* surface() const { return m_surface.wlSurface(); }

  private:
    void prepare();

    RenderContext& m_renderer;
    DaemonClient& m_daemon;
    std::string m_id;
    WindowSurface m_surface;
    std::unique_ptr<Node> m_root;
    Box* m_dot = nullptr;
    std::size_t m_listener = 0;
    bool m_seenActive = false;
  };
} // namespace tandem
