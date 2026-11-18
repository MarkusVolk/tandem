#pragma once

#include "app/page.h"
#include "core/timer_manager.h"
#include "platform/desktop_integration.h"
#include "render/scene/input_dispatcher.h"
#include "ui/controls/context_menu_popup.h"

#include <chrono>
#include <functional>
#include <memory>
#include <string>
#include <vector>

class Button;
class Flex;
class InputArea;
class Label;
class RenderContext;
class ScrollView;
class TextInputService;
class TooltipPresenter;
struct KeyboardEvent;
struct PointerEvent;

namespace tandem {
  // The window: the devices on the left, the pages of the selected one on the right.
  class Window final : public PageHost {
  public:
    Window(Services services, RenderContext& renderer, TextInputService& textInput);
    ~Window() override;
    Window(const Window&) = delete;
    Window& operator=(const Window&) = delete;

    bool running() const { return m_surface.isRunning(); }
    void close() { m_surface.close(); }
    wl_surface* surface() const { return m_surface.wlSurface(); }
    void setOnQuit(std::function<void()> callback) { m_onQuit = std::move(callback); }

    void showDevice(const std::string& id);
    void showPreferences();
    // Shows a tab of the device page by its name ("Messages", ...).
    void showTab(const std::string& name);
    // The actions the daemon sends through org.freedesktop.Application.
    void share(const std::string& id);
    void reply(const std::string& id, const std::string& notification, const std::string& title);
    // Files or links dropped onto the window go to the device shown.
    void dropped(const std::vector<std::string>& uris);

    void keyboard(const KeyboardEvent& event);
    void pointer(const PointerEvent& event);
    void focus(wl_surface* surface, bool entered);

    Services& services() override { return m_services; }
    void push(std::unique_ptr<Page> page) override;
    void pop() override;
    void requestUpdate() override;
    void showDialog(std::unique_ptr<FormDialog> dialog) override;
    void closeDialog() override;
    void notify(std::string message) override;
    std::string portalHandle() const override { return m_surface.portalHandle(); }

  private:
    enum class Command : std::int32_t;

    void prepare();
    void build();
    void rebuildDevices();
    void rebuildHeader();
    void mountPage();
    void unmount();
    void setRoot(std::unique_ptr<Page> page);
    void event(const DaemonEvent& event);
    void showMenu();
    void run(Command command);
    void showAbout();

    Services m_services;
    RenderContext& m_renderer;
    TextInputService& m_textInput;
    WindowSurface m_surface;
    InputDispatcher m_dispatcher;
    ContextMenuPopup m_menu;
    std::unique_ptr<Node> m_root;
    Flex* m_deviceList = nullptr;
    Flex* m_header = nullptr;
    Flex* m_body = nullptr;
    Button* m_menuButton = nullptr;
    Label* m_status = nullptr;
    TooltipPresenter* m_tooltip = nullptr;
    InputArea* m_backdrop = nullptr;
    FormDialog* m_dialog = nullptr;
    Node* m_mountedNode = nullptr;
    Page* m_mountedPage = nullptr;

    std::vector<std::unique_ptr<Page>> m_pages;
    std::string m_selected;
    bool m_devicesDirty = true;
    bool m_headerDirty = true;
    bool m_decorated = false;
    std::size_t m_listener = 0;
    std::chrono::steady_clock::time_point m_menuClosedAt{};
    Timer m_statusTimer;
    bool m_swallowRelease = false;
    float m_pointerX = 0.0F;
    float m_pointerY = 0.0F;
    std::function<void()> m_onQuit;
  };
} // namespace tandem
