#include "pages/device_page.h"

#include "ui/builders.h"
#include "ui/rows.h"
#include "ui/style.h"

#include <algorithm>

namespace {
  constexpr std::array<const char*, 5> kTabNames{"Device", "Messages", "Sharing", "Control", "Plugins"};
  constexpr float kContentWidth = 680.0F;
} // namespace

tandem::DevicePage::DevicePage(PageHost& host, std::string id)
    : Page(host), m_id(std::move(id)), m_device(daemon().device(m_id)) {}

std::string tandem::DevicePage::title() const { return m_device ? m_device->name() : std::string("Device"); }

std::unique_ptr<Node> tandem::DevicePage::build() {
  auto page = ui::column({.align = FlexAlign::Stretch, .flexGrow = 1});
  page->addChild(ui::column({.out = &m_banner, .align = FlexAlign::Stretch}));
  page->addChild(ui::row({.out = &m_tabBar, .align = FlexAlign::Center, .justify = FlexJustify::Center, .paddingV = Style::spaceSm}));
  page->addChild(ui::scrollView({.out = &m_scroll, .viewportPaddingH = Style::spaceLg, .flexGrow = 1}));
  m_scroll->content()->setDirection(FlexDirection::Vertical);
  m_scroll->content()->setAlign(FlexAlign::Center);
  m_scroll->content()->addChild(
      ui::column(
          {.out = &m_content,
           .align = FlexAlign::Stretch,
           .gap = Style::spaceLg,
           .paddingV = Style::spaceMd,
           .maxWidth = kContentWidth,
           .fillWidth = true}
      )
  );
  m_groupsBuilt = false;
  m_shownTabs.clear();
  m_statusDirty = true;
  m_contentDirty = true;
  return page;
}

void tandem::DevicePage::event(const DaemonEvent& event) {
  if (event.device != m_id) {
    if (event.kind == DaemonEvent::Kind::Settings && m_tab == DeviceTab::Plugins) {
      m_contentDirty = true;
    }
    return;
  }
  switch (event.kind) {
  case DaemonEvent::Kind::Device:
    m_statusDirty = true;
    if (m_tab == DeviceTab::Device) {
      m_contentDirty = true;
    }
    break;
  case DaemonEvent::Kind::Plugin:
    m_dirtyPlugins.insert(event.plugin);
    break;
  case DaemonEvent::Kind::DeviceSettings:
    m_settingsDirty = true;
    if (m_tab == DeviceTab::Plugins) {
      m_contentDirty = true;
    }
    break;
  default:
    break;
  }
}

void tandem::DevicePage::refresh(Renderer& /*renderer*/) {
  if (!m_device) {
    return;
  }
  const auto plugins = m_device->plugins();
  const bool paired = m_device->paired();
  if (!m_groupsBuilt || plugins != m_shownPlugins || paired != m_shownPaired) {
    m_groupsBuilt = true;
    m_shownPlugins = plugins;
    m_shownPaired = paired;
    m_groups.clear();
    m_tabUsed.fill(false);
    m_tabUsed[static_cast<std::size_t>(DeviceTab::Device)] = true;
    m_tabUsed[static_cast<std::size_t>(DeviceTab::Plugins)] = paired;
    for (const auto& plugin : plugins) {
      const auto tab = tabForPlugin(plugin);
      if (!tab) {
        continue;
      }
      if (auto group = makePluginGroup(host(), m_device, plugin)) {
        m_tabUsed[static_cast<std::size_t>(*tab)] = true;
        m_groups.push_back(std::move(group));
      }
    }
    if (!m_tabUsed[static_cast<std::size_t>(m_tab)]) {
      m_tab = DeviceTab::Device;
    }
    m_contentDirty = true;
    m_dirtyPlugins.clear();
    m_settingsDirty = false;
  }
  if (m_statusDirty) {
    rebuildStatus();
  }
  rebuildTabs();
  if (m_contentDirty) {
    rebuildContent();
    m_dirtyPlugins.clear();
    m_settingsDirty = false;
  }
  for (const auto& plugin : std::exchange(m_dirtyPlugins, {})) {
    for (const auto& group : m_groups) {
      if (group->plugin() == plugin && tabForPlugin(plugin) == m_tab) {
        group->update(m_device->pluginState(plugin));
      }
    }
  }
  if (std::exchange(m_settingsDirty, false)) {
    for (const auto& group : m_groups) {
      if (tabForPlugin(group->plugin()) == m_tab) {
        group->settingsChanged();
      }
    }
  }
}

void tandem::DevicePage::rebuildTabs() {
  std::vector<DeviceTab> tabs;
  for (std::size_t i = 0; i < m_tabUsed.size(); ++i) {
    if (m_tabUsed[i]) {
      tabs.push_back(static_cast<DeviceTab>(i));
    }
  }
  if (tabs == m_shownTabs) {
    return;
  }
  m_shownTabs = tabs;
  while (!m_tabBar->children().empty()) {
    m_tabBar->removeChild(m_tabBar->children().back().get());
  }
  m_tabBar->setVisible(tabs.size() > 1);
  if (tabs.size() <= 1) {
    return;
  }
  std::vector<ui::SegmentedOption> options;
  std::size_t selected = 0;
  for (std::size_t i = 0; i < tabs.size(); ++i) {
    options.push_back({.label = kTabNames[static_cast<std::size_t>(tabs[i])], .glyph = {}, .tooltip = {}});
    if (tabs[i] == m_tab) {
      selected = i;
    }
  }
  m_tabBar->addChild(
      ui::segmented(
          {.options = std::move(options),
           .selectedIndex = selected,
           .onChange = [this, tabs](std::size_t index) {
             if (index < tabs.size()) {
               select(tabs[index]);
             }
           }}
      )
  );
}

void tandem::DevicePage::showTab(const std::string& name) {
  for (std::size_t i = 0; i < kTabNames.size(); ++i) {
    if (name == kTabNames[i] && m_tabUsed[i]) {
      m_shownTabs.clear();
      select(static_cast<DeviceTab>(i));
    }
  }
}

void tandem::DevicePage::select(DeviceTab tab) {
  if (tab == m_tab) {
    return;
  }
  m_tab = tab;
  m_contentDirty = true;
  host().requestUpdate();
}

void tandem::DevicePage::rebuildStatus() {
  m_statusDirty = false;
  while (!m_banner->children().empty()) {
    m_banner->removeChild(m_banner->children().back().get());
  }
  const std::string state = m_device->pairState();
  std::string text;
  std::vector<std::pair<std::string, const char*>> actions;
  if (state == "requested-by-peer") {
    text = "This device asks to pair";
    actions = {{"Reject", "RejectPair"}, {"Accept", "AcceptPair"}};
  } else if (state == "requested") {
    text = "Waiting for the device to accept";
    actions = {{"Cancel", "RejectPair"}};
  } else if (!m_device->paired()) {
    text = "Not paired";
    if (m_device->reachable()) {
      actions = {{"Pair", "Pair"}};
    }
  } else {
    return;
  }
  auto banner = ui::row(
      {.align = FlexAlign::Center,
       .gap = Style::spaceSm,
       .paddingV = Style::spaceSm,
       .paddingH = Style::spaceLg,
       .fill = colorSpecFromRole(ColorRole::PrimaryContainer)}
  );
  auto label = rows::text(text, Style::fontSizeBody, ColorRole::OnPrimaryContainer);
  label->setFlexGrow(1);
  banner->addChild(std::move(label));
  for (std::size_t i = 0; i < actions.size(); ++i) {
    banner->addChild(
        ui::button(
            {.text = actions[i].first,
             .controlHeight = Style::controlHeightSm,
             .variant = i + 1 == actions.size() ? ButtonVariant::Primary : ButtonVariant::Outline,
             .onClick = [weak = std::weak_ptr<Device>(m_device), method = actions[i].second] {
               if (auto device = weak.lock()) {
                 device->call(method);
               }
             }}
        )
    );
  }
  m_banner->addChild(std::move(banner));
}

void tandem::DevicePage::rebuildContent() {
  m_contentDirty = false;
  const float offset = m_scroll->scrollOffset();
  while (!m_content->children().empty()) {
    m_content->removeChild(m_content->children().back().get());
  }
  const std::weak_ptr<Device> weak = m_device;
  if (m_tab == DeviceTab::Device) {
    const bool paired = m_device->paired();
    const bool reachable = m_device->reachable();
    const std::string state = m_device->pairState();
    auto status = std::make_unique<rows::Group>("Status");
    status->add(
        rows::row(
            "Status", !paired ? (reachable ? "Available, not paired" : "Not reachable")
                : reachable   ? "Connected"
                : m_device->paused() ? "Disconnected"
                                     : "Paired, not reachable"
        )
    );
    if (state == "requested" || state == "requested-by-peer") {
      status->add(rows::row("Verification key", m_device->verificationKey()));
    }
    if (paired) {
      const bool paused = m_device->paused();
      status->add(rows::buttonRow(paused ? "Connect" : "Disconnect", {}, paused ? "Connect" : "Disconnect", [weak, paused] {
        if (auto device = weak.lock()) {
          device->call(paused ? "Connect" : "Disconnect");
        }
      }));
      status->add(rows::buttonRow("Unpair", "Forget this device until it is paired again", "Unpair", [weak] {
        if (auto device = weak.lock()) {
          device->call("Unpair");
        }
      }));
    }
    m_content->addChild(std::move(status));
    if (paired) {
      auto actions = std::make_unique<rows::Group>("Actions");
      Button* ring = nullptr;
      Button* ping = nullptr;
      if (m_device->hasPlugin("findmyphone")) {
        actions->add(rows::buttonRow("Ring the device", {}, "Ring", [weak] {
          if (auto device = weak.lock()) {
            device->callPlugin("findmyphone", "ring");
          }
        }, &ring));
      }
      actions->add(rows::buttonRow("Send a ping", {}, "Ping", [this, weak] {
        if (auto device = weak.lock()) {
          device->call("Ping", g_variant_new("(s)", ""));
          host().notify("Ping sent");
        }
      }, &ping));
      for (Button* button : {ring, ping}) {
        if (button) {
          button->setEnabled(reachable);
        }
      }
      m_content->addChild(std::move(actions));
    }
  }
  if (m_tab == DeviceTab::Plugins) {
    auto plugins = std::make_unique<rows::Group>("Plugins", "Plugins switched off here stay off for this device.");
    const auto disabled = m_device->settings().strings("disabled-plugins");
    for (const auto& plugin : daemon().plugins()) {
      const bool on = std::ranges::find(disabled, plugin) == disabled.end();
      plugins->add(rows::switchRow(pluginTitle(plugin), {}, on, [weak, plugin](bool enabled) {
        if (auto device = weak.lock()) {
          device->call("SetPluginEnabled", g_variant_new("(sb)", plugin.c_str(), enabled));
        }
      }));
    }
    m_content->addChild(std::move(plugins));
  }
  for (const auto& group : m_groups) {
    if (tabForPlugin(group->plugin()) == m_tab) {
      m_content->addChild(group->build());
    }
  }
  m_scroll->requestScrollToOffset(offset);
}
