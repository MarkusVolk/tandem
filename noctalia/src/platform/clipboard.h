#pragma once

#include "core/text_clipboard.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>
#include <wayland-client.h>

struct zwp_primary_selection_device_manager_v1;

namespace tandem {
  // Plain text over one of the seat's selections: the clipboard (Ctrl+C, Ctrl+V) or the primary
  // selection (what is marked, pasted with a middle click). Offers what was set here and reads
  // what other clients offer without blocking the window.
  class WaylandClipboard final : public TextClipboard {
  public:
    // `serial` yields the input serial that authorises setting the selection, 0 when there is none.
    using SerialSource = std::function<std::uint32_t()>;
    static std::unique_ptr<WaylandClipboard>
    clipboard(wl_display* display, wl_data_device_manager* manager, wl_seat* seat, SerialSource serial);
    // Null when the compositor offers no primary selection.
    static std::unique_ptr<WaylandClipboard> primary(
        wl_display* display, zwp_primary_selection_device_manager_v1* manager, wl_seat* seat, SerialSource serial
    );
    ~WaylandClipboard() override;
    WaylandClipboard(const WaylandClipboard&) = delete;
    WaylandClipboard& operator=(const WaylandClipboard&) = delete;

    // Files dragged onto a surface: the surface and the URIs of text/uri-list. Clipboard only.
    using DropHandler = std::function<void(wl_surface*, std::vector<std::string>)>;
    void setDropHandler(DropHandler handler);

    std::optional<std::string> clipboardText() override;
    void setClipboardText(std::string text) override;
    void requestClipboardText(std::function<void(std::optional<std::string>)> callback) override;

  private:
    struct State;
    explicit WaylandClipboard(std::shared_ptr<State> state);
    std::shared_ptr<State> m_state;
  };
} // namespace tandem
