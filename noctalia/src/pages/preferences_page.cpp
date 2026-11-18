#include "pages/preferences_page.h"

#include "platform/file_portal.h"
#include "ui/builders.h"
#include "ui/rows.h"
#include "ui/style.h"

#include <array>
#include <glib.h>

namespace {
  constexpr float kContentWidth = 680.0F;
  constexpr std::array<const char*, 2> kTypes{"desktop", "laptop"};
  constexpr std::array<const char*, 2> kTypeLabels{"Desktop", "Laptop"};
  constexpr std::array<const char*, 4> kBackends{"auto", "portal", "wlr", "uinput"};
  constexpr std::array<const char*, 4> kBackendLabels{"Automatic", "RemoteDesktop portal", "wlroots protocols", "uinput"};

  template <std::size_t N> std::size_t indexOf(const std::array<const char*, N>& values, const std::string& value) {
    for (std::size_t i = 0; i < N; ++i) {
      if (value == values[i]) {
        return i;
      }
    }
    return 0;
  }

  std::string downloadFolder(const std::string& setting) {
    if (!setting.empty()) {
      return setting;
    }
    const char* folder = g_get_user_special_dir(G_USER_DIRECTORY_DOWNLOAD);
    return folder ? folder : g_get_home_dir();
  }
} // namespace

std::unique_ptr<Node> tandem::PreferencesPage::build() {
  ScrollView* scroll = nullptr;
  auto view = ui::scrollView({.out = &scroll, .viewportPaddingH = Style::spaceLg, .flexGrow = 1});
  scroll->content()->setDirection(FlexDirection::Vertical);
  scroll->content()->setAlign(FlexAlign::Center);
  scroll->content()->addChild(
      ui::column(
          {.out = &m_content, .align = FlexAlign::Stretch, .gap = Style::spaceLg, .paddingV = Style::spaceMd,
           .maxWidth = kContentWidth, .fillWidth = true}
      )
  );
  m_dirty = true;
  return view;
}

void tandem::PreferencesPage::event(const DaemonEvent& event) {
  if (event.kind == DaemonEvent::Kind::Settings || event.kind == DaemonEvent::Kind::Running) {
    m_dirty = true;
  }
}

void tandem::PreferencesPage::refresh(Renderer& /*renderer*/) {
  if (m_dirty && !m_editing) {
    rebuild();
  }
}

void tandem::PreferencesPage::rebuild() {
  m_dirty = false;
  while (!m_content->children().empty()) {
    m_content->removeChild(m_content->children().back().get());
  }
  DaemonClient& client = daemon();
  const Variant& settings = client.settings();
  const auto set = [&client](const char* key, GVariant* value) { client.setSetting(key, value); };

  auto identity = std::make_unique<rows::Group>(
      "This device", client.selfId().empty() ? std::string("Device id unknown, tandemd is not running") : "Device id " + client.selfId()
  );
  identity->add(
      rows::row(
          "Name shown to other devices", {},
          ui::input(
              {.value = settings.string("device-name"),
               .placeholder = g_get_host_name(),
               .width = 240.0F,
               .onChange = [this](const std::string&) { m_editing = true; },
               .onSubmit =
                   [this, set](const std::string& value) {
                     m_editing = false;
                     set("device-name", g_variant_new_string(value.c_str()));
                   },
               .submitOnFocusLoss = true}
          )
      )
  );
  identity->add(
      rows::row(
          "Device type", {},
          ui::select(
              {.options = std::vector<std::string>(kTypeLabels.begin(), kTypeLabels.end()),
               .selectedIndex = indexOf(kTypes, settings.string("device-type", "desktop")),
               .width = 240.0F,
               .onSelectionChanged = [set](std::size_t index, std::string_view) {
                 set("device-type", g_variant_new_string(kTypes[std::min(index, kTypes.size() - 1)]));
               }}
          )
      )
  );
  m_content->addChild(std::move(identity));

  auto behaviour = std::make_unique<rows::Group>("Behaviour");
  const std::string folder = downloadFolder(settings.string("download-dir"));
  behaviour->add(rows::buttonRow("Folder for received files", folder, "Choose…", [this, set, folder] {
    host().services().portal.chooseFolder(host().portalHandle(), "Folder for received files", folder, [set](auto paths) {
      if (!paths.empty()) {
        set("download-dir", g_variant_new_string(paths.front().c_str()));
      }
    });
  }));
  behaviour->add(rows::switchRow("Discover devices with mDNS", "Takes effect after a restart", settings.boolean("mdns", true), [set](bool on) {
    set("mdns", g_variant_new_boolean(on));
  }));
  behaviour->add(rows::switchRow("Show a tray icon", "Takes effect after a restart", settings.boolean("show-tray", true), [set](bool on) {
    set("show-tray", g_variant_new_boolean(on));
  }));
  behaviour->add(
      rows::row(
          "Remote input backend", {},
          ui::select(
              {.options = std::vector<std::string>(kBackendLabels.begin(), kBackendLabels.end()),
               .selectedIndex = indexOf(kBackends, settings.string("input-backend", "auto")),
               .width = 240.0F,
               .onSelectionChanged = [set](std::size_t index, std::string_view) {
                 set("input-backend", g_variant_new_string(kBackends[std::min(index, kBackends.size() - 1)]));
               }}
          )
      )
  );
  behaviour->add(
      rows::switchRow("Share clipboard contents marked as passwords", {}, settings.boolean("send-passwords"), [set](bool on) {
        set("send-passwords", g_variant_new_boolean(on));
      })
  );
  m_content->addChild(std::move(behaviour));
}
