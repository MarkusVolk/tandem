#include "app/window.h"

#include "core/deferred_call.h"
#include "core/input/key_modifiers.h"
#include "core/input/key_symbols.h"
#include "core/ui_phase.h"
#include "pages/device_page.h"
#include "pages/preferences_page.h"
#include "platform/file_portal.h"
#include "render/render_context.h"
#include "tandem_build_info.h"
#include "ui/builders.h"
#include "ui/form_dialog.h"
#include "ui/rows.h"
#include "ui/style.h"
#include "ui/tooltip_presenter.h"
#include "wayland/text_input_service.h"
#include "wayland/wayland_connection.h"

#include <filesystem>
#include <linux/input-event-codes.h>
#include <stdexcept>
#include <utility>
#include <xkbcommon/xkbcommon-keysyms.h>

namespace {
  constexpr float kSidebarWidth = 280.0F;
  constexpr float kHeaderHeight = Style::controlHeightSm + 2.0F * Style::spaceSm;
  constexpr auto kStatusTime = std::chrono::seconds(5);
  constexpr auto kMenuReopenGuard = std::chrono::milliseconds(250);

  const char* deviceGlyph(const std::string& type) {
    if (type == "phone") {
      return "device-mobile";
    }
    if (type == "tablet") {
      return "device-tablet";
    }
    if (type == "laptop") {
      return "device-laptop";
    }
    if (type == "tv") {
      return "device-tv";
    }
    return "device-desktop";
  }

  std::string deviceStatus(const tandem::Device& device) {
    const std::string state = device.pairState();
    if (state == "paired-reachable") {
      return "Connected";
    }
    if (state == "paired") {
      return device.paused() ? "Disconnected" : "Paired, not reachable";
    }
    if (state == "requested") {
      return "Pairing requested";
    }
    if (state == "requested-by-peer") {
      return "Wants to pair";
    }
    return "Available";
  }

  // What the content side shows while no device is selected.
  class EmptyPage final : public tandem::Page {
  public:
    using Page::Page;
    std::string title() const override { return "Tandem"; }
    std::unique_ptr<Node> build() override {
      auto column = ui::column(
          {.align = FlexAlign::Center, .justify = FlexJustify::Center, .gap = Style::spaceMd, .flexGrow = 1}
      );
      column->addChild(ui::glyph({.glyph = "device-mobile", .glyphSize = 64.0F, .color = colorSpecFromRole(ColorRole::OnSurfaceVariant)}));
      column->addChild(ui::label({.out = &m_title, .fontSize = Style::fontSizeHeader, .fontWeight = FontWeight::SemiBold}));
      column->addChild(
          ui::label(
              {.out = &m_text,
               .fontSize = Style::fontSizeBody,
               .color = colorSpecFromRole(ColorRole::OnSurfaceVariant),
               .maxWidth = 420.0F,
               .maxLines = 3,
               .textAlign = TextAlign::Center}
          )
      );
      m_built = false;
      return column;
    }
    void refresh(Renderer& /*renderer*/) override {
      const bool running = daemon().running();
      if (m_built && m_running == running) {
        return;
      }
      m_built = true;
      m_running = running;
      m_title->setText(running ? "No Devices" : "Service not running");
      m_text->setText(
          running ? "Open KDE Connect on your phone and make sure both are on the same network."
                  : "Tandem's background service is not running. Start it with: systemctl --user start tandem"
      );
    }
    void event(const tandem::DaemonEvent& /*event*/) override { m_built = false; }

  private:
    Label* m_title = nullptr;
    Label* m_text = nullptr;
    bool m_built = false;
    bool m_running = false;
  };
} // namespace

enum class tandem::Window::Command : std::int32_t {
  Refresh = 1,
  Preferences,
  About,
  Quit,
};

tandem::Window::Window(Services services, RenderContext& renderer, TextInputService& textInput)
    : m_services(services), m_renderer(renderer), m_textInput(textInput), m_surface(services.connection),
      m_menu(services.connection, renderer) {
  m_surface.setRenderContext(&m_renderer);
  m_surface.setAnimationManager(&m_services.animations);
  m_surface.setPrepareFrameCallback([this](bool, bool) { prepare(); });
  m_surface.setUpdateCallback([] {});
  m_surface.setConfigureCallback([this](std::uint32_t, std::uint32_t) {
    if (m_tooltip) {
      m_tooltip->dismiss();
    }
    m_headerDirty = true;
    m_surface.requestLayout();
  });
  m_surface.setClosedCallback([this] {
    if (m_onQuit) {
      m_onQuit();
    }
  });
  m_dispatcher.setCursorShapeCallback([this](std::uint32_t serial, std::uint32_t shape) {
    m_services.connection.extension<DesktopIntegration>().setCursorShape(serial, shape);
  });
  m_menu.setOnActivate([this](const ContextMenuControlEntry& entry) { run(static_cast<Command>(entry.id)); });
  m_menu.setOnDismissed([this] {
    m_menuClosedAt = std::chrono::steady_clock::now();
    requestUpdate();
  });
  if (!m_surface.initialize(
          nullptr, {.width = 900, .height = 640, .minWidth = 560, .minHeight = 400, .title = "Tandem", .appId = "de.flk.Tandem"}
      )) {
    throw std::runtime_error("cannot create the window");
  }
  m_dispatcher.setTextInputContext(m_surface.wlSurface(), &m_textInput);
  m_listener = m_services.daemon.addListener([this](const DaemonEvent& event) { this->event(event); });
  if (!m_services.daemon.running()) {
    m_services.daemon.refresh();
  }
  setRoot(std::make_unique<EmptyPage>(*this));
}

tandem::Window::~Window() {
  m_services.daemon.removeListener(m_listener);
  m_menu.close();
}

void tandem::Window::requestUpdate() { m_surface.requestLayout(); }

void tandem::Window::notify(std::string message) {
  if (m_status) {
    m_status->setText(message);
    m_status->setVisible(true);
  }
  m_statusTimer.start(kStatusTime, [this] {
    if (m_status) {
      m_status->setVisible(false);
      requestUpdate();
    }
  });
  requestUpdate();
}

// ── Pages ────────────────────────────────────────────────────────────────────

void tandem::Window::unmount() {
  if (m_mountedNode) {
    m_dispatcher.cancelPointerCapture();
    std::shared_ptr<Node> doomed(m_body->removeChild(m_mountedNode).release());
    DeferredCall::callLater([doomed] {});
    m_mountedNode = nullptr;
  }
  m_mountedPage = nullptr;
}

void tandem::Window::setRoot(std::unique_ptr<Page> page) {
  unmount();
  // A page may be running this from a control of its own: pages go after this turn of the loop.
  auto doomed = std::make_shared<std::vector<std::unique_ptr<Page>>>(std::move(m_pages));
  DeferredCall::callLater([doomed] {});
  m_pages.clear();
  m_pages.push_back(std::move(page));
  m_headerDirty = true;
  requestUpdate();
}

void tandem::Window::push(std::unique_ptr<Page> page) {
  unmount();
  m_pages.push_back(std::move(page));
  m_headerDirty = true;
  requestUpdate();
}

void tandem::Window::pop() {
  if (m_pages.size() <= 1) {
    return;
  }
  unmount();
  // The page may be running this from a button of its own.
  std::shared_ptr<Page> doomed(m_pages.back().release());
  m_pages.pop_back();
  DeferredCall::callLater([doomed] {});
  m_headerDirty = true;
  requestUpdate();
}

void tandem::Window::showPreferences() { run(Command::Preferences); }

void tandem::Window::showTab(const std::string& name) {
  while (m_pages.size() > 1) {
    pop();
  }
  m_pages.front()->showTab(name);
}

void tandem::Window::showDevice(const std::string& id) {
  if (m_selected == id && !m_pages.empty() && m_pages.front()->deviceId() == id) {
    while (m_pages.size() > 1) {
      pop();
    }
    return;
  }
  m_selected = id;
  m_devicesDirty = true;
  setRoot(std::make_unique<DevicePage>(*this, id));
}

void tandem::Window::share(const std::string& id) {
  auto device = m_services.daemon.device(id);
  if (!device) {
    return;
  }
  showDevice(id);
  m_services.portal.openFiles(
      portalHandle(), "Send files to " + device->name(), [weak = std::weak_ptr<Device>(device)](auto paths) {
        auto device = weak.lock();
        if (!device || paths.empty()) {
          return;
        }
        std::vector<std::string> files;
        for (const auto& path : paths) {
          files.push_back(path.string());
        }
        device->callPlugin("share", "files", VariantMap().add("paths", files).end());
      }
  );
}

void tandem::Window::dropped(const std::vector<std::string>& uris) {
  auto device = m_services.daemon.device(m_selected);
  if (!device || !device->hasPlugin("share")) {
    notify("Open a paired device to send files to it");
    return;
  }
  std::vector<std::string> files;
  for (const auto& uri : uris) {
    if (gchar* path = g_filename_from_uri(uri.c_str(), nullptr, nullptr)) {
      files.emplace_back(path);
      g_free(path);
    } else {
      device->callPlugin("share", "url", VariantMap().add("url", uri).end());
    }
  }
  if (!files.empty()) {
    device->callPlugin("share", "files", VariantMap().add("paths", files).end());
  }
  const std::string what = uris.size() > 1 ? std::to_string(uris.size()) + " items"
      : files.empty()                     ? std::string("the link")
                                          : std::filesystem::path(files.front()).filename().string();
  notify("Sending " + what + " to " + device->name());
}

void tandem::Window::reply(const std::string& id, const std::string& notification, const std::string& title) {
  auto device = m_services.daemon.device(id);
  if (!device) {
    return;
  }
  showDevice(id);
  showDialog(
      std::make_unique<FormDialog>(
          "Reply to " + device->name(), title, std::vector<FormDialog::Field>{{.placeholder = "Reply", .value = {}, .multiline = false}},
          std::vector<FormDialog::Answer>{
              {.label = "Cancel", .primary = false, .destructive = false, .chosen = [this](const auto&) { closeDialog(); }},
              {.label = "Send",
               .primary = true,
               .destructive = false,
               .chosen =
                   [this, weak = std::weak_ptr<Device>(device), notification](const std::vector<std::string>& values) {
                     if (auto device = weak.lock(); device && !values.front().empty()) {
                       device->callPlugin(
                           "notifications", "reply",
                           VariantMap().add("id", notification).add("text", values.front()).end()
                       );
                     }
                     closeDialog();
                   }},
          }
      )
  );
}

void tandem::Window::event(const DaemonEvent& event) {
  using Kind = DaemonEvent::Kind;
  if (event.kind == Kind::Running || event.kind == Kind::Devices || event.kind == Kind::Device) {
    m_devicesDirty = true;
    m_headerDirty = true;
  }
  if ((event.kind == Kind::Devices || event.kind == Kind::Running) && !m_selected.empty()
      && !m_services.daemon.device(m_selected)) {
    m_selected.clear();
    setRoot(std::make_unique<EmptyPage>(*this));
  }
  if (m_selected.empty() && event.kind == Kind::Devices) {
    // Open the first device without asking, like the list's autoselect.
    if (const auto devices = m_services.daemon.devices(); !devices.empty()) {
      DeferredCall::callLater([this, id = devices.front()->id()] {
        if (m_selected.empty()) {
          showDevice(id);
        }
      });
    }
  }
  for (const auto& page : m_pages) {
    page->event(event);
  }
  requestUpdate();
}

// ── Dialogs and menu ─────────────────────────────────────────────────────────

void tandem::Window::showDialog(std::unique_ptr<FormDialog> dialog) {
  if (!m_root) {
    build();
  }
  closeDialog();
  m_dialog = dialog.get();
  m_root->addChild(std::move(dialog));
  m_backdrop->setVisible(true);
  m_dispatcher.setFocus(m_dialog->defaultArea());
  requestUpdate();
}

void tandem::Window::closeDialog() {
  if (!m_dialog) {
    return;
  }
  m_dispatcher.cancelPointerCapture();
  std::shared_ptr<Node> doomed(m_root->removeChild(m_dialog).release());
  DeferredCall::callLater([doomed] {});
  m_dialog = nullptr;
  m_backdrop->setVisible(false);
  m_dispatcher.setFocus(nullptr);
  requestUpdate();
}

void tandem::Window::showMenu() {
  if (m_tooltip) {
    m_tooltip->dismiss();
  }
  const auto id = [](Command command) { return static_cast<std::int32_t>(command); };
  float x = 0.0F, y = 0.0F;
  Node::absolutePosition(m_menuButton, x, y);
  m_dispatcher.cancelPointerCapture();
  m_menu.open(
      {.entries =
           {
               {.id = id(Command::Refresh), .label = "Refresh", .shortcut = "F5"},
               {.id = id(Command::Preferences), .label = "Preferences", .shortcut = "Ctrl+,"},
               {.id = id(Command::About), .label = "About Tandem"},
               {.label = "", .separator = true},
               {.id = id(Command::Quit), .label = "Quit", .shortcut = "Ctrl+Q"},
           },
       .minMenuWidth = 200.0F,
       .anchor =
           {.x = static_cast<std::int32_t>(x),
            .y = static_cast<std::int32_t>(y),
            .width = static_cast<std::int32_t>(m_menuButton->width()),
            .height = static_cast<std::int32_t>(m_menuButton->height())},
       .parent = {.attach = {}, .xdgSurface = m_surface.xdgSurface(), .output = nullptr, .keyboardGrab = {}},
       .pointerParentSurface = m_surface.wlSurface(),
       .inputSerial = m_services.connection.lastInputSerial()}
  );
}

void tandem::Window::run(Command command) {
  switch (command) {
  case Command::Refresh:
    notify(m_services.daemon.running() ? "Looking for devices…" : "Starting the Tandem service…");
    m_services.daemon.refresh();
    break;
  case Command::Preferences:
    if (m_pages.back()->title() != "Preferences") {
      push(std::make_unique<PreferencesPage>(*this));
    }
    break;
  case Command::About:
    showAbout();
    break;
  case Command::Quit:
    m_surface.close();
    break;
  }
}

void tandem::Window::showAbout() {
  showDialog(
      std::make_unique<FormDialog>(
          "Tandem " TANDEM_VERSION,
          "Connects your phone with the KDE Connect protocol.\nGPL-2.0-or-later, https://github.com/MarkusVolk/tandem\n"
          "Built on the Noctalia UI library (MIT).",
          std::vector<FormDialog::Field>{},
          std::vector<FormDialog::Answer>{
              {.label = "Close", .primary = true, .destructive = false, .chosen = [this](const auto&) { closeDialog(); }}
          }
      )
  );
}

// ── Scene ────────────────────────────────────────────────────────────────────

void tandem::Window::build() {
  m_root = ui::column({.align = FlexAlign::Stretch, .fill = colorSpecFromRole(ColorRole::Surface)});
  m_root->setAnimationManager(&m_services.animations);
  m_surface.setSceneRoot(m_root.get());
  m_dispatcher.setSceneRoot(m_root.get());

  auto tooltip = std::make_unique<TooltipPresenter>([this] { m_surface.requestUpdate(); });
  m_tooltip = tooltip.get();
  m_root->addChild(std::move(tooltip));

  auto main = ui::row({.align = FlexAlign::Stretch, .flexGrow = 1});

  auto sidebar = ui::column(
      {.align = FlexAlign::Stretch, .fill = colorSpecFromRole(ColorRole::SurfaceContainerLow), .width = kSidebarWidth}
  );
  auto sidebarHeader = ui::row(
      {.align = FlexAlign::Center, .gap = Style::spaceXs, .paddingH = Style::spaceMd, .height = kHeaderHeight}
  );
  sidebarHeader->addChild(rows::text("Devices", Style::fontSizeTitle, ColorRole::OnSurface));
  static_cast<Label*>(sidebarHeader->children().back().get())->setFontWeight(FontWeight::SemiBold);
  static_cast<Label*>(sidebarHeader->children().back().get())->setFlexGrow(1);
  sidebarHeader->addChild(rows::glyphButton("refresh", "Refresh (F5)", [this] { run(Command::Refresh); }));
  auto menu = rows::glyphButton("menu-2", "Menu (F10)", [this] {
    if (std::chrono::steady_clock::now() - m_menuClosedAt > kMenuReopenGuard) {
      showMenu();
    }
  });
  m_menuButton = menu.get();
  sidebarHeader->addChild(std::move(menu));
  sidebar->addChild(std::move(sidebarHeader));
  ScrollView* list = nullptr;
  sidebar->addChild(ui::scrollView({.out = &list, .viewportPaddingH = Style::spaceSm, .flexGrow = 1}));
  list->content()->setDirection(FlexDirection::Vertical);
  list->content()->setAlign(FlexAlign::Stretch);
  list->content()->setGap(Style::spaceXs);
  m_deviceList = list->content();
  main->addChild(std::move(sidebar));

  auto content = ui::column({.align = FlexAlign::Stretch, .flexGrow = 1});
  content->addChild(
      ui::row({.out = &m_header, .align = FlexAlign::Center, .gap = Style::spaceXs, .paddingH = Style::spaceSm, .height = kHeaderHeight})
  );
  content->addChild(ui::column({.out = &m_body, .align = FlexAlign::Stretch, .clipChildren = true, .flexGrow = 1}));
  auto status = ui::label(
      {.out = &m_status,
       .fontSize = Style::fontSizeCaption,
       .color = colorSpecFromRole(ColorRole::OnSurfaceVariant),
       .maxLines = 1,
       .visible = false,
       .configure = [](Label& label) { label.setUseMarkup(false); }}
  );
  content->addChild(ui::row({.align = FlexAlign::Center, .paddingV = Style::spaceXs, .paddingH = Style::spaceMd}, std::move(status)));
  main->addChild(std::move(content));
  m_root->addChild(std::move(main));

  // Swallows the pointer behind a dialog.
  auto backdrop = std::make_unique<InputArea>();
  backdrop->setParticipatesInLayout(false);
  backdrop->setZIndex(100);
  backdrop->setVisible(false);
  backdrop->addChild(ui::box({.fill = colorSpecFromRole(ColorRole::Scrim, 0.45F)}));
  m_backdrop = backdrop.get();
  m_root->addChild(std::move(backdrop));
}

void tandem::Window::rebuildDevices() {
  m_devicesDirty = false;
  while (!m_deviceList->children().empty()) {
    m_deviceList->removeChild(m_deviceList->children().back().get());
  }
  for (const auto& device : m_services.daemon.devices()) {
    const bool selected = device->id() == m_selected;
    Button* button = nullptr;
    m_deviceList->addChild(
        ui::button(
            {.out = &button,
             .glyph = deviceGlyph(device->type()),
             .glyphSize = 20.0F,
             .controlHeight = Style::controlHeightLg + Style::spaceSm,
             .selected = selected,
             .contentAlign = ButtonContentAlign::Start,
             .variant = selected ? ButtonVariant::Secondary : ButtonVariant::Ghost,
             .paddingH = Style::spaceMd,
             .gap = Style::spaceMd,
             .onClick = [this, id = device->id()] { showDevice(id); }}
        )
    );
    auto labels = ui::column({.align = FlexAlign::Start, .gap = 1.0F, .flexGrow = 1});
    labels->addChild(rows::text(device->name(), Style::fontSizeBody, selected ? ColorRole::OnSecondary : ColorRole::OnSurface));
    labels->addChild(
        rows::text(deviceStatus(*device), Style::fontSizeCaption, selected ? ColorRole::OnSecondary : ColorRole::OnSurfaceVariant)
    );
    button->addChild(std::move(labels));
  }
}

void tandem::Window::rebuildHeader() {
  m_headerDirty = false;
  while (!m_header->children().empty()) {
    m_header->removeChild(m_header->children().back().get());
  }
  if (m_pages.size() > 1) {
    m_header->addChild(rows::glyphButton("arrow-left", "Back (Alt+Left)", [this] { pop(); }));
  } else {
    m_header->addChild(ui::spacer());
    m_header->children().back()->setSize(Style::spaceSm, 1.0F);
  }
  auto title = rows::text(m_pages.back()->title(), Style::fontSizeTitle, ColorRole::OnSurface);
  title->setFontWeight(FontWeight::SemiBold);
  title->setFlexGrow(1);
  m_header->addChild(std::move(title));
  m_decorated = !m_surface.serverDecorated() && !m_surface.fullscreen();
  if (m_decorated) {
    m_header->addChild(rows::glyphButton("minus", "Minimize window", [this] { m_surface.minimize(); }));
    m_header->addChild(rows::glyphButton("square", "Toggle maximized", [this] { m_surface.toggleMaximized(); }));
    m_header->addChild(rows::glyphButton("x", "Close window", [this] { m_surface.close(); }));
  }
  const std::string name = m_pages.back()->title();
  m_surface.setTitle(name == "Tandem" ? name : name + " — Tandem");
}

void tandem::Window::mountPage() {
  Page* top = m_pages.back().get();
  if (top == m_mountedPage) {
    return;
  }
  unmount();
  m_mountedNode = m_body->addChild(top->build());
  m_mountedNode->setFlexGrow(1);
  m_mountedPage = top;
  if (!m_dialog) {
    m_dispatcher.setFocus(nullptr);
  }
}

void tandem::Window::prepare() {
  if (!m_surface.isRunning() || !m_renderer.makeCurrent(m_surface.renderTarget())) {
    return;
  }
  UiPhaseScope phase(UiPhase::Layout);
  if (!m_root) {
    build();
  }
  auto& renderer = m_surface.renderTarget().renderer();
  if (m_decorated != (!m_surface.serverDecorated() && !m_surface.fullscreen())) {
    m_headerDirty = true;
  }
  if (m_devicesDirty) {
    rebuildDevices();
  }
  if (m_headerDirty) {
    rebuildHeader();
  }
  mountPage();
  m_pages.back()->refresh(renderer);

  const auto width = static_cast<float>(m_surface.width()), height = static_cast<float>(m_surface.height());
  m_root->arrange(renderer, {0, 0, width, height});
  m_backdrop->arrange(renderer, {0, 0, width, height});
  m_backdrop->children().front()->arrange(renderer, {0, 0, width, height});
  if (m_dialog) {
    const auto size = m_dialog->measure(renderer, LayoutConstraints::available(width - 2.0F * Style::spaceLg, height));
    m_dialog->arrange(
        renderer, {std::round((width - size.width) * 0.5F), std::round((height - size.height) * 0.4F), size.width, size.height}
    );
  }
  if (m_dispatcher.pointerCaptured() || m_menu.isOpen()) {
    m_tooltip->dismiss();
  } else {
    m_tooltip->sync(renderer, *m_root, m_pointerX, m_pointerY, {width, height});
  }
}

// ── Input ────────────────────────────────────────────────────────────────────

void tandem::Window::focus(wl_surface* surface, bool entered) {
  if (surface != m_surface.wlSurface()) {
    return;
  }
  if (entered) {
    m_dispatcher.setTextInputContext(m_surface.wlSurface(), &m_textInput);
  } else if (m_tooltip) {
    m_tooltip->dismiss();
  }
  requestUpdate();
}

void tandem::Window::keyboard(const KeyboardEvent& event) {
  if (ContextMenuPopup::dispatchKeyboardEvent(event)) {
    return;
  }
  if (event.pressed && m_tooltip) {
    m_tooltip->dismiss();
  }
  const auto forward = [this, &event] {
    if (InputArea* focused = m_dispatcher.focusedArea(); focused && focused->capturesAllKeys()) {
      focused->dispatchKey(event.sym, event.utf32, event.modifiers, event.pressed, event.preedit);
    } else {
      m_dispatcher.keyEvent(event.sym, event.utf32, event.modifiers, event.pressed, event.preedit);
    }
    requestUpdate();
  };
  if (!event.pressed || event.preedit) {
    forward();
    return;
  }
  const std::uint32_t sym = event.sym;
  if (m_dialog) {
    if (KeySymbol::isEscape(sym)) {
      closeDialog();
    } else if (KeySymbol::isTab(sym) || sym == XKB_KEY_ISO_Left_Tab) {
      m_dispatcher.cycleTabFocusInSubtree(m_dialog, event.modifiers == KeyMod::Shift);
      requestUpdate();
    } else {
      forward();
    }
    return;
  }
  const bool ctrl = event.modifiers == KeyMod::Ctrl;
  if (ctrl && (sym == XKB_KEY_q || sym == XKB_KEY_Q)) {
    run(Command::Quit);
    return;
  }
  if (ctrl && sym == XKB_KEY_comma) {
    run(Command::Preferences);
    return;
  }
  if (event.modifiers == 0 && sym == XKB_KEY_F5) {
    run(Command::Refresh);
    return;
  }
  if (event.modifiers == 0 && sym == XKB_KEY_F10) {
    showMenu();
    return;
  }
  if (event.modifiers == KeyMod::Alt && sym == XKB_KEY_Left) {
    pop();
    return;
  }
  if (event.modifiers == 0 && KeySymbol::isEscape(sym) && m_pages.size() > 1 && !m_dispatcher.focusedArea()) {
    pop();
    return;
  }
  forward();
}

void tandem::Window::pointer(const PointerEvent& event) {
  if (event.type == PointerEvent::Type::Button && !event.pressed && std::exchange(m_swallowRelease, false)) {
    return;
  }
  if (m_menu.isOpen()) {
    if (event.type == PointerEvent::Type::Button
        && event.pressed
        && (event.button == BTN_RIGHT || event.surface != m_menu.wlSurface())) {
      m_menu.close();
      m_swallowRelease = true;
      return;
    }
    if (m_menu.onPointerEvent(event)) {
      return;
    }
  }
  if (event.surface != m_surface.wlSurface()) {
    return;
  }
  const auto x = static_cast<float>(event.sx), y = static_cast<float>(event.sy);
  if (m_tooltip) {
    if (!event.touch && (event.type == PointerEvent::Type::Enter || event.type == PointerEvent::Type::Motion)) {
      m_tooltip->resume();
    } else {
      m_tooltip->dismiss();
    }
  }
  m_pointerX = x;
  m_pointerY = y;
  switch (event.type) {
  case PointerEvent::Type::Enter:
    m_dispatcher.pointerEnter(x, y, event.serial);
    break;
  case PointerEvent::Type::Leave:
    m_dispatcher.pointerLeave();
    break;
  case PointerEvent::Type::Motion:
    m_dispatcher.pointerMotion(x, y, event.serial);
    break;
  case PointerEvent::Type::Button: {
    const bool taken = m_dispatcher.pointerButton(x, y, event.button, event.pressed, event.serial, event.time, event.touch);
    // Without server decorations the empty part of the headers moves the window.
    if (!taken && event.pressed && event.button == BTN_LEFT && m_decorated && y < kHeaderHeight && !m_dialog) {
      m_surface.beginMove(event.serial);
    }
    break;
  }
  case PointerEvent::Type::Axis:
    m_dispatcher.pointerAxis(
        x, y, event.axis, event.axisSource, event.axisValue, event.axisDiscrete, event.axisValue120, event.axisLines,
        event.axisGestureSerial
    );
    break;
  }
  requestUpdate();
}
