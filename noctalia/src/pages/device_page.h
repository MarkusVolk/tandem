#pragma once

#include "app/page.h"
#include "pages/plugin_groups.h"

#include <array>
#include <memory>
#include <set>
#include <string>
#include <vector>

class Flex;
class Label;
class ScrollView;
class Segmented;

namespace tandem {
  namespace rows {
    class Group;
  }

  // A device: its pairing state and the groups of its plugins in tabs.
  class DevicePage final : public Page {
  public:
    DevicePage(PageHost& host, std::string id);
    std::string title() const override;
    std::string deviceId() const override { return m_id; }
    std::unique_ptr<Node> build() override;
    void refresh(Renderer& renderer) override;
    void event(const DaemonEvent& event) override;
    void showTab(const std::string& name) override;

  private:
    void rebuildTabs();
    void rebuildStatus();
    void rebuildContent();
    void select(DeviceTab tab);

    std::string m_id;
    std::shared_ptr<Device> m_device;
    std::vector<std::unique_ptr<PluginGroup>> m_groups;
    std::array<bool, 5> m_tabUsed{};
    std::vector<DeviceTab> m_shownTabs;
    DeviceTab m_tab = DeviceTab::Device;
    std::vector<std::string> m_shownPlugins;
    bool m_shownPaired = false;
    bool m_groupsBuilt = false;
    std::set<std::string> m_dirtyPlugins;
    Flex* m_banner = nullptr;
    Flex* m_tabBar = nullptr;
    ScrollView* m_scroll = nullptr;
    Flex* m_content = nullptr;
    bool m_statusDirty = true;
    bool m_contentDirty = true;
    bool m_settingsDirty = false;
  };
} // namespace tandem
