#include "app/application.h"

#include "app/instance_service.h"
#include "app/main_loop.h"
#include "app/palette_watcher.h"
#include "app/presenter_window.h"
#include "app/window.h"
#include "core/application_identity.h"
#include "core/files/resource_paths.h"
#include "core/input/key_modifiers.h"
#include "core/input/key_symbols.h"
#include "core/input/keybind_matcher.h"
#include "core/log.h"
#include "core/timer_manager.h"
#include "core/ui_strings.h"
#include "daemon/client.h"
#include "event_loop/deferred_call_poll_source.h"
#include "event_loop/glib_poll_source.h"
#include "event_loop/timer_poll_source.h"
#include "platform/clipboard.h"
#include "platform/desktop_integration.h"
#include "platform/file_portal.h"
#include "platform/stb_image_decoder.h"
#include "render/animation/animation_manager.h"
#include "render/gl_shared_context.h"
#include "render/render_context.h"
#include "tandem_build_info.h"
#include "ui/controls/input.h"
#include "ui/palette_file.h"
#include "wayland/key_repeat_poll_source.h"
#include "wayland/text_input_service.h"
#include "wayland/wayland_connection.h"

#include <array>
#include <chrono>
#include <csignal>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <memory>
#include <print>

namespace {
  volatile std::sig_atomic_t g_stopRequested = 0;
  void stopRequested(int) { g_stopRequested = 1; }

  // A D-Bus activated start that never receives its request gives up after this.
  constexpr auto kServiceIdle = std::chrono::seconds(30);

  void initializeKeys() {
    const std::array<std::pair<KeybindAction, std::uint32_t>, 7> keys{{
        {KeybindAction::Cancel, XKB_KEY_Escape},
        {KeybindAction::Left, XKB_KEY_Left},
        {KeybindAction::Right, XKB_KEY_Right},
        {KeybindAction::Up, XKB_KEY_Up},
        {KeybindAction::Down, XKB_KEY_Down},
        {KeybindAction::TabNext, XKB_KEY_Tab},
        {KeybindAction::Delete, XKB_KEY_Delete},
    }};
    for (const auto& [action, sym] : keys) {
      KeybindMatcher::setMatcher(action, [sym](std::uint32_t key, std::uint32_t modifiers) {
        return key == sym && modifiers == 0U;
      });
    }
    KeybindMatcher::setMatcher(KeybindAction::Validate, [](std::uint32_t sym, std::uint32_t modifiers) {
      return modifiers == 0 && KeySymbol::isEnterOrSpace(sym);
    });
    KeybindMatcher::setMatcher(KeybindAction::Copy, [](std::uint32_t sym, std::uint32_t modifiers) {
      return sym == XKB_KEY_c && modifiers == KeyMod::Ctrl;
    });
    KeybindMatcher::setMatcher(KeybindAction::TabPrevious, [](std::uint32_t sym, std::uint32_t modifiers) {
      return KeySymbol::isTab(sym) && modifiers == KeyMod::Shift;
    });
  }

  void initializeLibrary() {
    const char* overridePath = std::getenv("TANDEM_ASSETS_DIR");
    const std::filesystem::path assets = overridePath ? overridePath : TANDEM_INSTALL_ASSETSDIR;
    paths::setAssetRoots({.application = assets, .library = assets});
    setUiStrings({.keyName = {}, .selectPlaceholder = [] { return std::string("Select…"); }});
    setPalette(tandem::defaultPalette());
    tandem::installStbImageDecoder();
  }
} // namespace

int tandem::runApplication(bool service) {
  initLogLevelFromEnvironment();
  InstanceService instance;
  if (instance.role() == InstanceService::Role::Secondary) {
    const char* token = std::getenv("XDG_ACTIVATION_TOKEN");
    if (instance.forward(token ? token : "")) {
      return EXIT_SUCCESS;
    }
  }
  initializeKeys();
  Input::setValidateKeyMatcher([](std::uint32_t sym, std::uint32_t modifiers) {
    return modifiers == 0 && KeySymbol::isEnter(sym);
  });
  g_stopRequested = 0;
  const auto previousTerm = std::signal(SIGTERM, stopRequested);
  const auto previousInt = std::signal(SIGINT, stopRequested);
  int result = EXIT_SUCCESS;
  try {
    initializeLibrary();
    GlibPollSource glib;
    TextInputService textInput;
    WaylandConnection connection;
    if (!connection.connect()) {
      throw std::runtime_error("could not connect to Wayland: " + connection.describeDisplayError());
    }
    connection.setTextInputService(&textInput);
    auto& desktop = connection.extension<DesktopIntegration>();
    const auto serial = [&connection] { return connection.lastKeyboardSurface() ? connection.lastInputSerial() : 0; };
    const auto clipboard = WaylandClipboard::clipboard(connection.display(), desktop.dataDeviceManager(), connection.seat(), serial);
    Input::setTextClipboard(clipboard.get());
    struct ClipboardBinding {
      ~ClipboardBinding() { Input::setTextClipboard(nullptr); }
    } clipboardBinding;
    GlSharedContext graphics;
    graphics.initialize(connection.display());
    RenderContext renderer;
    renderer.initialize(graphics);
    PaletteWatcher palette;
    AnimationManager animations;
    FilePortal portal;
    DaemonClient daemon;
    const Services services{
        .connection = connection, .animations = animations, .portal = portal, .daemon = daemon, .clipboard = *clipboard
    };

    std::unique_ptr<Window> window;
    std::unique_ptr<PresenterWindow> presenter;
    bool requested = !service;
    const auto started = std::chrono::steady_clock::now();
    const auto openWindow = [&]() -> Window& {
      if (!window) {
        window = std::make_unique<Window>(services, renderer, textInput);
      }
      return *window;
    };
    if (!service) {
      openWindow();
    }
    // Wakes the loop so an unused service start ends.
    Timer idle;
    if (service) {
      idle.start(std::chrono::duration_cast<std::chrono::milliseconds>(kServiceIdle) + std::chrono::seconds(1), [] {});
    }
    instance.setHandler([&](const InstanceRequest& request) {
      requested = true;
      const auto& p = request.parameters;
      if (request.action.empty()) {
        openWindow();
      } else if (request.action == "share" && p.size() >= 1) {
        openWindow().share(p[0]);
      } else if (request.action == "reply" && p.size() >= 3) {
        openWindow().reply(p[0], p[1], p[2]);
      } else if (request.action == "tab" && p.size() >= 1) {
        openWindow().showTab(p[0]);
      } else if (request.action == "preferences") {
        openWindow().showPreferences();
      } else if (request.action == "show-device" && p.size() >= 1) {
        openWindow().showDevice(p[0]);
      } else if (request.action == "presenter" && p.size() >= 1) {
        if (!presenter || !presenter->running() || presenter->deviceId() != p[0]) {
          presenter.reset();
          presenter = std::make_unique<PresenterWindow>(connection, renderer, animations, daemon, p[0]);
        }
        return;
      } else {
        std::println(stderr, "tandem: no action named {}", request.action);
        return;
      }
      if (!request.activationToken.empty() && window) {
        desktop.activate(request.activationToken, window->surface());
      }
    });

    clipboard->setDropHandler([&window](wl_surface* surface, std::vector<std::string> uris) {
      if (window && surface == window->surface()) {
        window->dropped(uris);
      }
    });
    connection.setPointerEventCallback([&](const PointerEvent& event) {
      desktop.pointer(event);
      if (window) {
        window->pointer(event);
      }
    });
    connection.setKeyboardEventCallback([&](const KeyboardEvent& event) {
      if (window) {
        window->keyboard(event);
      }
    });
    connection.setKeyboardFocusCallback([&](wl_surface* surface, bool entered) {
      if (window) {
        window->focus(surface, entered);
      }
    });

    DeferredCallPollSource deferred;
    TimerPollSource timers;
    KeyRepeatPollSource repeat(connection);
    MainLoop loop(connection, {&deferred, &timers, &repeat, &glib}, [&] {
      if (g_stopRequested) {
        if (window && window->running()) {
          window->close();
        }
        if (presenter && presenter->running()) {
          presenter.reset();
        }
      }
      if (window && !window->running()) {
        window.reset();
      }
      if (presenter && !presenter->running()) {
        presenter.reset();
      }
      if (window || presenter) {
        return true;
      }
      return !requested && !g_stopRequested && std::chrono::steady_clock::now() - started < kServiceIdle;
    });
    loop.run();
    clipboard->setDropHandler({});
    instance.release();
    connection.setPointerEventCallback({});
    connection.setKeyboardEventCallback({});
    connection.setKeyboardFocusCallback({});
    textInput.cleanup();
    connection.setTextInputService(nullptr);
  } catch (const std::exception& error) {
    std::println(stderr, "tandem: {}", error.what());
    result = EXIT_FAILURE;
  }
  std::signal(SIGTERM, previousTerm);
  std::signal(SIGINT, previousInt);
  return result;
}
