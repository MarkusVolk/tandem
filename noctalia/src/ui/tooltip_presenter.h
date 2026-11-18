#pragma once

#include "core/timer_manager.h"
#include "render/scene/tooltip_content.h"
#include "ui/controls/flex.h"

#include <functional>

class InputArea;
class Label;

// Window-local tooltip overlay, after Luciole's adaptation of Noctalia's tooltip manager: shows
// the tooltip text of the control under the pointer after a short delay.
class TooltipPresenter final : public Flex {
public:
  explicit TooltipPresenter(std::function<void()> requestUpdate);

  void resume() noexcept { m_armed = true; }
  void dismiss();
  void sync(Renderer& renderer, Node& root, float pointerX, float pointerY, LayoutSize bounds);

private:
  void place(Renderer& renderer, LayoutSize bounds);

  std::function<void()> m_requestUpdate;
  Timer m_delay;
  Label* m_label = nullptr;
  const InputArea* m_identity = nullptr; // Compared only; resolved afresh from the live tree each frame.
  std::string m_text;
  LayoutRect m_anchor{};
  TooltipPlacement m_placement = TooltipPlacement::Default;
  bool m_ready = false;
  bool m_armed = false;
};
