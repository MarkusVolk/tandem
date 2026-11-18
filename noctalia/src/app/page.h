#pragma once

#include "daemon/client.h"

#include <memory>
#include <string>

class AnimationManager;
class Node;
class Renderer;
class TextClipboard;
class WaylandConnection;

namespace tandem {
  class FilePortal;
  class FormDialog;

  struct Services {
    WaylandConnection& connection;
    AnimationManager& animations;
    FilePortal& portal;
    DaemonClient& daemon;
    TextClipboard& clipboard;
  };

  class Page;

  // What a page may ask of the window that shows it.
  class PageHost {
  public:
    virtual ~PageHost() = default;
    virtual Services& services() = 0;
    virtual void push(std::unique_ptr<Page> page) = 0;
    // Closes the topmost page; the device page at the bottom stays.
    virtual void pop() = 0;
    virtual void requestUpdate() = 0;
    virtual void showDialog(std::unique_ptr<FormDialog> dialog) = 0;
    virtual void closeDialog() = 0;
    // A short message at the bottom of the window.
    virtual void notify(std::string message) = 0;
    // The parent handle portal dialogs take.
    virtual std::string portalHandle() const = 0;
  };

  // One screen of the content side. The window calls build() once and refresh() before every
  // frame; a page rebuilds what changed in refresh(), never from a callback of its own controls.
  class Page {
  public:
    explicit Page(PageHost& host) : m_host(host) {}
    virtual ~Page() = default;
    Page(const Page&) = delete;
    Page& operator=(const Page&) = delete;

    virtual std::string title() const = 0;
    // The device the page belongs to, empty for none.
    virtual std::string deviceId() const { return {}; }
    virtual std::unique_ptr<Node> build() = 0;
    virtual void refresh(Renderer& /*renderer*/) {}
    virtual void event(const DaemonEvent& /*event*/) {}
    // Shows one of the page's tabs by its name; pages without tabs ignore it.
    virtual void showTab(const std::string& /*name*/) {}

  protected:
    PageHost& host() const noexcept { return m_host; }
    DaemonClient& daemon() const { return m_host.services().daemon; }

  private:
    PageHost& m_host;
  };
} // namespace tandem
