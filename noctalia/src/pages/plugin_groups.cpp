#include "pages/plugin_groups.h"

#include "app/page.h"
#include "core/text_clipboard.h"
#include "pages/sms_page.h"
#include "platform/file_portal.h"
#include "ui/builders.h"
#include "ui/form_dialog.h"
#include "ui/rows.h"
#include "ui/style.h"

#include <algorithm>
#include <array>
#include <format>
#include <utility>

namespace {
  using tandem::Device;
  using tandem::FormDialog;
  using tandem::PageHost;
  using tandem::PluginGroup;
  using tandem::Variant;
  using tandem::VariantMap;
  namespace rows = tandem::rows;

  constexpr std::array<std::pair<const char*, const char*>, 19> kTitles{{
      {"ping", "Ping"},
      {"battery", "Battery"},
      {"connectivity", "Mobile network"},
      {"findmyphone", "Find my phone"},
      {"lock", "Lock"},
      {"runcommand", "Run commands"},
      {"notifications", "Notifications"},
      {"share", "Share"},
      {"mpriscontrol", "Media control"},
      {"systemvolume", "System volume"},
      {"mprisremote", "Remote media"},
      {"mousepad", "Remote input"},
      {"presenter", "Presenter"},
      {"contacts", "Contacts"},
      {"telephony", "Telephony"},
      {"sms", "Messages"},
      {"clipboard", "Clipboard"},
      {"sendnotifications", "Send notifications"},
      {"remoteinput", "Remote keyboard"},
  }};

  constexpr std::size_t kNotificationLength = 160;

  std::unique_ptr<rows::Group> group(const std::string& plugin, std::string description = {}) {
    return std::make_unique<rows::Group>(tandem::pluginTitle(plugin), std::move(description));
  }

  // A group whose rows are all rebuilt from the plugin state.
  class StateGroup : public PluginGroup {
  public:
    using PluginGroup::PluginGroup;
    std::unique_ptr<Node> build() override {
      auto built = group(plugin(), description());
      m_group = built.get();
      fill(device().pluginState(plugin()));
      return built;
    }
    void update(const Variant& state) override {
      if (m_group) {
        m_group->clear();
        fill(state);
      }
    }

  protected:
    virtual std::string description() const { return {}; }
    virtual void fill(const Variant& state) = 0;
    rows::Group* m_group = nullptr;
  };

  // ── Device tab ────────────────────────────────────────────────────────────

  class BatteryGroup final : public StateGroup {
  public:
    using StateGroup::StateGroup;

  private:
    void fill(const Variant& state) override {
      const auto charge = state.integer("charge", -1);
      const bool charging = state.boolean("charging");
      const char* glyph = charge < 0 ? "battery-off" : charging ? "battery-charging" : charge < 15 ? "battery"
          : charge < 40                                                                          ? "battery-1"
          : charge < 65                                                                          ? "battery-2"
          : charge < 90                                                                          ? "battery-3"
                                                                                                 : "battery-4";
      m_group->add(
          rows::row(
              "Charge", charge < 0 ? std::string("Unknown") : std::format("{} %{}", charge, charging ? ", charging" : ""),
              ui::glyph({.glyph = glyph, .glyphSize = 22.0F, .color = colorSpecFromRole(ColorRole::OnSurfaceVariant)})
          )
      );
    }
  };

  class ConnectivityGroup final : public StateGroup {
  public:
    using StateGroup::StateGroup;

  private:
    void fill(const Variant& state) override {
      const std::string summary = state.string("summary");
      m_group->add(rows::row("Signal", summary.empty() ? std::string("Unknown") : summary));
    }
  };

  class LockGroup final : public PluginGroup {
  public:
    using PluginGroup::PluginGroup;
    std::unique_ptr<Node> build() override {
      auto built = group(plugin());
      built->add(
          rows::switchRow(
              "Device locked", {}, device().pluginState(plugin()).boolean("locked"),
              [weak = weakDevice()](bool locked) {
                if (auto device = weak.lock()) {
                  device->callPlugin("lock", "lock", VariantMap().add("locked", locked).end());
                }
              },
              &m_toggle
          )
      );
      return built;
    }
    void update(const Variant& state) override {
      if (m_toggle) {
        m_toggle->setChecked(state.boolean("locked"));
      }
    }

  private:
    Toggle* m_toggle = nullptr;
  };

  // ── Messages tab ──────────────────────────────────────────────────────────

  void showReply(PageHost& host, const std::weak_ptr<Device>& weak, const std::string& id, const std::string& title) {
    auto device = weak.lock();
    if (!device) {
      return;
    }
    host.showDialog(
        std::make_unique<FormDialog>(
            "Reply to " + device->name(), title, std::vector<FormDialog::Field>{{.placeholder = "Reply", .value = {}, .multiline = false}},
            std::vector<FormDialog::Answer>{
                {.label = "Cancel", .primary = false, .destructive = false, .chosen = [&host](const auto&) { host.closeDialog(); }},
                {.label = "Send",
                 .primary = true,
                 .destructive = false,
                 .chosen =
                     [&host, weak, id](const std::vector<std::string>& values) {
                       if (auto device = weak.lock(); device && !values.front().empty()) {
                         device->callPlugin(
                             "notifications", "reply", VariantMap().add("id", id).add("text", values.front()).end()
                         );
                       }
                       host.closeDialog();
                     }},
            }
        )
    );
  }

  class NotificationsGroup final : public StateGroup {
  public:
    using StateGroup::StateGroup;
    void settingsChanged() override {
      if (m_toggle) {
        m_toggle->setChecked(device().setting("show-notifications", true));
      }
    }

  private:
    void fill(const Variant& state) override {
      m_group->add(
          rows::switchRow(
              "Show as desktop notifications", {}, device().setting("show-notifications", true),
              [weak = weakDevice()](bool on) {
                if (auto device = weak.lock()) {
                  device->setSetting("show-notifications", g_variant_new_boolean(on));
                }
              },
              &m_toggle
          )
      );
      const auto notifications = state.maps("notifications");
      if (notifications.empty()) {
        m_group->add(rows::row("No notifications on the device"));
        return;
      }
      for (const auto& notification : notifications) {
        const std::string id = notification.string("id");
        std::string title = notification.string("title");
        const std::string text = notification.string("text");
        const std::string app = notification.string("app");
        if (title.empty()) {
          title = app;
        }
        auto actions = ui::row({.align = FlexAlign::Center, .gap = Style::spaceXs});
        if (notification.boolean("replyable")) {
          actions->addChild(rows::glyphButton("message-reply", "Reply", [this, id, title] {
            showReply(host(), weakDevice(), id, title);
          }));
        }
        if (notification.boolean("clearable")) {
          actions->addChild(rows::glyphButton("x", "Dismiss", [weak = weakDevice(), id] {
            if (auto device = weak.lock()) {
              device->callPlugin("notifications", "dismiss", VariantMap().add("id", id).end());
            }
          }));
        }
        std::string subtitle = text.empty() ? app : app.empty() || app == title ? text : app + " — " + text;
        std::ranges::replace(subtitle, '\n', ' ');
        if (subtitle.size() > kNotificationLength) {
          // Cut on a character boundary, not inside a UTF-8 sequence.
          std::size_t cut = kNotificationLength;
          while (cut > 0 && (static_cast<unsigned char>(subtitle[cut]) & 0xC0U) == 0x80U) {
            --cut;
          }
          subtitle = subtitle.substr(0, cut) + "…";
        }
        m_group->add(rows::row(title, subtitle, std::move(actions)));
      }
    }

    Toggle* m_toggle = nullptr;
  };

  class ContactsGroup final : public StateGroup {
  public:
    using StateGroup::StateGroup;

  private:
    void fill(const Variant& state) override {
      const auto count = state.integer("count");
      const bool syncing = state.boolean("syncing");
      m_group->add(
          rows::buttonRow(
              "Address book", syncing ? std::string("Synchronising…") : std::format("{} contacts", count), "Synchronise",
              [weak = weakDevice()] {
                if (auto device = weak.lock()) {
                  device->callPlugin("contacts", "sync");
                }
              }
          )
      );
    }
  };

  class SettingSwitchesGroup final : public PluginGroup {
  public:
    struct Switch {
      const char* key;
      const char* title;
      bool fallback;
    };
    SettingSwitchesGroup(PageHost& host, std::shared_ptr<Device> device, std::string plugin, std::vector<Switch> switches)
        : PluginGroup(host, std::move(device), std::move(plugin)), m_switches(std::move(switches)) {}
    std::unique_ptr<Node> build() override {
      auto built = group(plugin());
      m_toggles.assign(m_switches.size(), nullptr);
      for (std::size_t i = 0; i < m_switches.size(); ++i) {
        const Switch& item = m_switches[i];
        built->add(
            rows::switchRow(
                item.title, {}, device().setting(item.key, item.fallback),
                [weak = weakDevice(), key = item.key](bool on) {
                  if (auto device = weak.lock()) {
                    device->setSetting(key, g_variant_new_boolean(on));
                  }
                },
                &m_toggles[i]
            )
        );
      }
      return built;
    }
    void settingsChanged() override {
      for (std::size_t i = 0; i < m_switches.size() && i < m_toggles.size(); ++i) {
        if (m_toggles[i]) {
          m_toggles[i]->setChecked(device().setting(m_switches[i].key, m_switches[i].fallback));
        }
      }
    }

  private:
    std::vector<Switch> m_switches;
    std::vector<Toggle*> m_toggles;
  };

  class SmsGroup final : public PluginGroup {
  public:
    using PluginGroup::PluginGroup;
    std::unique_ptr<Node> build() override {
      auto built = group(plugin());
      built->add(
          rows::buttonRow("Conversations", "Read and send text messages", "Open", [this] {
            host().push(std::make_unique<tandem::SmsThreadsPage>(host(), device().id()));
          })
      );
      return built;
    }
  };

  // ── Sharing tab ───────────────────────────────────────────────────────────

  class ShareGroup final : public PluginGroup {
  public:
    using PluginGroup::PluginGroup;
    std::unique_ptr<Node> build() override {
      auto built = group(plugin(), "Files dropped onto this page are sent too.");
      built->add(rows::buttonRow("Send files", {}, "Choose…", [this] { chooseFiles(); }));
      auto send = ui::row({.align = FlexAlign::Center, .gap = Style::spaceSm, .paddingV = Style::spaceSm});
      send->addChild(
          ui::input(
              {.out = &m_input,
               .placeholder = "Text or link to send",
               .flexGrow = 1,
               .onSubmit = [this](const std::string&) { sendText(); }}
          )
      );
      send->addChild(rows::glyphButton("send", "Send", [this] { sendText(); }));
      built->add(std::move(send));
      built->add(rows::buttonRow("Send the clipboard as text", {}, "Send", [this] { sendClipboard(); }));
      return built;
    }

  private:
    void chooseFiles() {
      host().services().portal.openFiles(host().portalHandle(), "Send files to " + device().name(), [weak = weakDevice()](auto paths) {
        auto device = weak.lock();
        if (!device || paths.empty()) {
          return;
        }
        std::vector<std::string> files;
        for (const auto& path : paths) {
          files.push_back(path.string());
        }
        device->callPlugin("share", "files", VariantMap().add("paths", files).end());
      });
    }
    void sendText() {
      if (!m_input || m_input->value().empty()) {
        return;
      }
      const std::string text = m_input->value();
      const bool url = text.find("://") != std::string::npos && text.find_first_of(" \n\t") == std::string::npos;
      device().callPlugin("share", url ? "url" : "text", VariantMap().add(url ? "url" : "text", text).end());
      m_input->setValue("");
      host().notify(url ? "Link sent" : "Text sent");
    }
    void sendClipboard() {
      host().services().clipboard.requestClipboardText([this, weak = weakDevice()](std::optional<std::string> text) {
        auto device = weak.lock();
        if (!device) {
          return;
        }
        if (!text || text->empty()) {
          host().notify("The clipboard holds no text");
          return;
        }
        device->callPlugin("share", "text", VariantMap().add("text", *text).end());
        host().notify("Clipboard sent");
      });
    }

    Input* m_input = nullptr;
  };

  class ClipboardGroup final : public PluginGroup {
  public:
    using PluginGroup::PluginGroup;
    std::unique_ptr<Node> build() override {
      const std::string backend = device().pluginState(plugin()).string("backend");
      auto built = group(plugin(), backend.empty() ? std::string() : "Clipboard access through " + backend);
      built->add(
          rows::switchRow(
              "Share automatically", "Send every copied text to the device", device().setting("clipboard-auto"),
              [weak = weakDevice()](bool on) {
                if (auto device = weak.lock()) {
                  device->setSetting("clipboard-auto", g_variant_new_boolean(on));
                }
              },
              &m_toggle
          )
      );
      built->add(rows::buttonRow("Send the clipboard now", {}, "Send", [weak = weakDevice()] {
        if (auto device = weak.lock()) {
          device->callPlugin("clipboard", "send");
        }
      }));
      return built;
    }
    void settingsChanged() override {
      if (m_toggle) {
        m_toggle->setChecked(device().setting("clipboard-auto"));
      }
    }

  private:
    Toggle* m_toggle = nullptr;
  };

  // ── Control tab ───────────────────────────────────────────────────────────

  class RemoteInputGroup final : public PluginGroup {
  public:
    using PluginGroup::PluginGroup;
    std::unique_ptr<Node> build() override {
      const bool keyboard = device().pluginState(plugin()).boolean("keyboard");
      auto built = group(
          plugin(), keyboard ? "Type into the device's focused text field." : "The device shows no keyboard right now."
      );
      m_description = static_cast<Label*>(built->children().size() > 2 ? built->children()[1].get() : nullptr);
      auto send = ui::row({.align = FlexAlign::Center, .gap = Style::spaceSm, .paddingV = Style::spaceSm});
      send->addChild(
          ui::input(
              {.out = &m_input,
               .placeholder = "Text to send",
               .flexGrow = 1,
               .onSubmit = [this](const std::string&) { sendText(); }}
          )
      );
      send->addChild(rows::glyphButton("send", "Send", [this] { sendText(); }));
      built->add(std::move(send));
      auto keys = ui::row({.align = FlexAlign::Center, .gap = Style::spaceSm, .paddingV = Style::spaceSm});
      for (const auto& [label, key] : std::array<std::pair<const char*, std::int32_t>, 4>{{
               {"Backspace", 1}, {"Enter", 12}, {"Tab", 2}, {"Esc", 14}}}) {
        keys->addChild(
            ui::button(
                {.text = label,
                 .controlHeight = Style::controlHeightSm,
                 .variant = ButtonVariant::Outline,
                 .onClick = [weak = weakDevice(), key] {
                   if (auto device = weak.lock()) {
                     device->callPlugin("remoteinput", "special", VariantMap().add("key", key).end());
                   }
                 }}
            )
        );
      }
      built->add(std::move(keys));
      return built;
    }
    void update(const Variant& state) override {
      if (m_description) {
        m_description->setText(
            state.boolean("keyboard") ? "Type into the device's focused text field." : "The device shows no keyboard right now."
        );
      }
    }

  private:
    void sendText() {
      if (!m_input || m_input->value().empty()) {
        return;
      }
      device().callPlugin("remoteinput", "text", VariantMap().add("text", m_input->value()).end());
      m_input->setValue("");
    }

    Input* m_input = nullptr;
    Label* m_description = nullptr;
  };

  class RunCommandGroup final : public StateGroup {
  public:
    using StateGroup::StateGroup;

  private:
    std::string description() const override { return "Commands the device may run on this computer."; }
    void fill(const Variant& state) override {
      for (const auto& command : state.lookup("commands").children()) {
        const auto fields = command.children();
        if (fields.size() < 3) {
          continue;
        }
        const std::string key = fields[0].string();
        auto actions = ui::row({.align = FlexAlign::Center, .gap = Style::spaceXs});
        actions->addChild(rows::glyphButton("player-play", "Run now", [weak = weakDevice(), key] {
          if (auto device = weak.lock()) {
            device->callPlugin("runcommand", "run", VariantMap().add("key", key).end());
          }
        }));
        actions->addChild(rows::glyphButton("trash", "Remove", [weak = weakDevice(), key] {
          if (auto device = weak.lock()) {
            device->callPlugin("runcommand", "remove", VariantMap().add("key", key).end());
          }
        }));
        m_group->add(rows::row(fields[1].string(), fields[2].string(), std::move(actions)));
      }
      m_group->add(rows::buttonRow("Add a command", {}, "Add…", [this] { add(); }));
    }
    void add() {
      PageHost& host = this->host();
      host.showDialog(
          std::make_unique<FormDialog>(
              "Add a command", "The device lists it under Run command.",
              std::vector<FormDialog::Field>{
                  {.placeholder = "Name", .value = {}, .multiline = false},
                  {.placeholder = "Command", .value = {}, .multiline = false},
              },
              std::vector<FormDialog::Answer>{
                  {.label = "Cancel", .primary = false, .destructive = false, .chosen = [&host](const auto&) { host.closeDialog(); }},
                  {.label = "Add",
                   .primary = true,
                   .destructive = false,
                   .chosen =
                       [&host, weak = weakDevice()](const std::vector<std::string>& values) {
                         if (values[0].empty() || values[1].empty()) {
                           return;
                         }
                         if (auto device = weak.lock()) {
                           device->callPlugin(
                               "runcommand", "add", VariantMap().add("name", values[0]).add("command", values[1]).end()
                           );
                         }
                         host.closeDialog();
                       }},
              }
          )
      );
    }
  };

  class MousepadGroup final : public StateGroup {
  public:
    using StateGroup::StateGroup;

  private:
    void fill(const Variant& state) override {
      const std::string backend = state.string("backend");
      m_group->add(
          rows::row(
              "Backend", std::format(
                             "{}, keyboard {}, pointer {}", backend.empty() ? std::string("none") : backend,
                             state.boolean("keyboard") ? "yes" : "no", state.boolean("pointer") ? "yes" : "no"
                         )
          )
      );
    }
  };
} // namespace

tandem::PluginGroup::PluginGroup(PageHost& host, std::shared_ptr<Device> device, std::string plugin)
    : m_host(host), m_device(std::move(device)), m_plugin(std::move(plugin)) {}

std::string tandem::pluginTitle(const std::string& plugin) {
  for (const auto& [name, title] : kTitles) {
    if (plugin == name) {
      return title;
    }
  }
  if (plugin == "kodicontrol") {
    return "Kodi";
  }
  return plugin;
}

std::optional<tandem::DeviceTab> tandem::tabForPlugin(const std::string& plugin) {
  static const std::array<std::pair<const char*, DeviceTab>, 13> map{{
      {"battery", DeviceTab::Device},
      {"connectivity", DeviceTab::Device},
      {"lock", DeviceTab::Device},
      {"sms", DeviceTab::Messages},
      {"notifications", DeviceTab::Messages},
      {"contacts", DeviceTab::Messages},
      {"telephony", DeviceTab::Messages},
      {"share", DeviceTab::Sharing},
      {"clipboard", DeviceTab::Sharing},
      {"sendnotifications", DeviceTab::Sharing},
      {"remoteinput", DeviceTab::Control},
      {"runcommand", DeviceTab::Control},
      {"mousepad", DeviceTab::Control},
  }};
  for (const auto& [name, tab] : map) {
    if (plugin == name) {
      return tab;
    }
  }
  return std::nullopt;
}

std::unique_ptr<tandem::PluginGroup>
tandem::makePluginGroup(PageHost& host, std::shared_ptr<Device> device, const std::string& plugin) {
  if (plugin == "battery") {
    return std::make_unique<BatteryGroup>(host, std::move(device), plugin);
  }
  if (plugin == "connectivity") {
    return std::make_unique<ConnectivityGroup>(host, std::move(device), plugin);
  }
  if (plugin == "lock") {
    return std::make_unique<LockGroup>(host, std::move(device), plugin);
  }
  if (plugin == "notifications") {
    return std::make_unique<NotificationsGroup>(host, std::move(device), plugin);
  }
  if (plugin == "contacts") {
    return std::make_unique<ContactsGroup>(host, std::move(device), plugin);
  }
  if (plugin == "telephony") {
    return std::make_unique<SettingSwitchesGroup>(
        host, std::move(device), plugin,
        std::vector<SettingSwitchesGroup::Switch>{
            {.key = "pause-media-on-call", .title = "Pause media during calls", .fallback = false},
            {.key = "mute-on-call", .title = "Mute the speakers during calls", .fallback = false},
        }
    );
  }
  if (plugin == "sendnotifications") {
    return std::make_unique<SettingSwitchesGroup>(
        host, std::move(device), plugin,
        std::vector<SettingSwitchesGroup::Switch>{
            {.key = "send-notifications", .title = "Forward notifications to the device", .fallback = false},
        }
    );
  }
  if (plugin == "sms") {
    return std::make_unique<SmsGroup>(host, std::move(device), plugin);
  }
  if (plugin == "share") {
    return std::make_unique<ShareGroup>(host, std::move(device), plugin);
  }
  if (plugin == "clipboard") {
    return std::make_unique<ClipboardGroup>(host, std::move(device), plugin);
  }
  if (plugin == "remoteinput") {
    return std::make_unique<RemoteInputGroup>(host, std::move(device), plugin);
  }
  if (plugin == "runcommand") {
    return std::make_unique<RunCommandGroup>(host, std::move(device), plugin);
  }
  if (plugin == "mousepad") {
    return std::make_unique<MousepadGroup>(host, std::move(device), plugin);
  }
  return nullptr;
}
