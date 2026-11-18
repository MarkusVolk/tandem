#pragma once

#include "daemon/client.h"

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

class Node;

namespace tandem {
  class PageHost;

  // The rows of one plugin on the device page.
  class PluginGroup {
  public:
    PluginGroup(PageHost& host, std::shared_ptr<Device> device, std::string plugin);
    virtual ~PluginGroup() = default;
    PluginGroup(const PluginGroup&) = delete;
    PluginGroup& operator=(const PluginGroup&) = delete;

    const std::string& plugin() const noexcept { return m_plugin; }
    // Builds the group afresh; earlier nodes are no longer used.
    virtual std::unique_ptr<Node> build() = 0;
    // The plugin's state changed; called before a frame, never from a control of the group.
    virtual void update(const Variant& /*state*/) {}
    // The device settings changed.
    virtual void settingsChanged() {}

  protected:
    PageHost& host() const noexcept { return m_host; }
    Device& device() const noexcept { return *m_device; }
    std::weak_ptr<Device> weakDevice() const { return m_device; }

  private:
    PageHost& m_host;
    std::shared_ptr<Device> m_device;
    std::string m_plugin;
  };

  // The device page's tabs, in order.
  enum class DeviceTab : std::uint8_t { Device, Messages, Sharing, Control, Plugins };
  // The tab a plugin's group goes to; empty for plugins without a group.
  std::optional<DeviceTab> tabForPlugin(const std::string& plugin);
  // The group of a plugin, null for plugins without one.
  std::unique_ptr<PluginGroup> makePluginGroup(PageHost& host, std::shared_ptr<Device> device, const std::string& plugin);
  // A readable name of a plugin.
  std::string pluginTitle(const std::string& plugin);
} // namespace tandem
