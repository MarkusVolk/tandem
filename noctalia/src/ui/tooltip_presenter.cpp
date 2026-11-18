#include "ui/tooltip_presenter.h"

#include "render/scene/input_area.h"
#include "ui/builders.h"

#include <algorithm>
#include <chrono>
#include <utility>

namespace {
  constexpr auto kShowDelay = std::chrono::milliseconds(500);
  constexpr float kMaxContentWidth = 280.0F;
  constexpr float kPadH = Style::spaceMd + Style::borderWidth;
  constexpr float kPadV = Style::spaceSm + Style::borderWidth;
} // namespace

TooltipPresenter::TooltipPresenter(std::function<void()> requestUpdate) : m_requestUpdate(std::move(requestUpdate)) {
  setDirection(FlexDirection::Vertical);
  setAlign(FlexAlign::Stretch);
  setPadding(kPadV, kPadH);
  setFill(colorSpecFromRole(ColorRole::Surface));
  setStyleRadius(Style::radiusMd);
  setBorder(colorSpecFromRole(ColorRole::OutlineVariant), Style::borderWidth);
  setParticipatesInLayout(false);
  setHitTestVisible(false);
  setExcludeSubtreeFromTabOrder(true);
  setZIndex(100);
  setVisible(false);
  addChild(
      ui::label(
          {.out = &m_label,
           .fontSize = Style::fontSizeCaption,
           .color = colorSpecFromRole(ColorRole::OnSurface),
           .maxWidth = kMaxContentWidth,
           .maxLines = 3,
           .configure = [](Label& label) { label.setUseMarkup(false); }}
      )
  );
}

void TooltipPresenter::dismiss() {
  m_delay.stop();
  m_text.clear();
  m_identity = nullptr;
  m_ready = false;
  m_armed = false;
  setVisible(false);
}

void TooltipPresenter::place(Renderer& renderer, LayoutSize bounds) {
  const auto size = measure(renderer, LayoutConstraints::available(bounds.width, bounds.height));
  float x = m_anchor.x + (m_anchor.width - size.width) * 0.5F;
  float y = m_anchor.y + m_anchor.height + Style::spaceXs;
  if (m_placement == TooltipPlacement::Above
      || ((m_placement == TooltipPlacement::Default || m_placement == TooltipPlacement::Below)
          && y + size.height > bounds.height - Style::spaceSm)) {
    y = m_anchor.y - size.height - Style::spaceSm;
  } else if (m_placement == TooltipPlacement::Left || m_placement == TooltipPlacement::Right) {
    x = m_placement == TooltipPlacement::Left ? m_anchor.x - size.width - Style::spaceSm
                                              : m_anchor.x + m_anchor.width + Style::spaceSm;
    y = m_anchor.y + (m_anchor.height - size.height) * 0.5F;
  }
  x = std::clamp(x, Style::spaceSm, std::max(Style::spaceSm, bounds.width - size.width - Style::spaceSm));
  y = std::clamp(y, Style::spaceSm, std::max(Style::spaceSm, bounds.height - size.height - Style::spaceSm));
  setVisible(true);
  arrange(renderer, {x, y, size.width, size.height});
}

void TooltipPresenter::sync(Renderer& renderer, Node& root, float pointerX, float pointerY, LayoutSize bounds) {
  if (!m_armed) {
    return;
  }
  InputArea* area = nullptr;
  for (auto* node = Node::hitTest(&root, pointerX, pointerY); node; node = node->parent()) {
    if (auto* input = dynamic_cast<InputArea*>(node); input && input->hasTooltip()) {
      area = input;
      break;
    }
  }
  if (!area) {
    dismiss();
    return;
  }
  const TooltipContent content = area->tooltipContent();
  const auto* text = std::get_if<std::string>(&content);
  if (!text || text->empty()) {
    dismiss();
    return;
  }
  const auto* node = area->tooltipAnchorNode() ? area->tooltipAnchorNode() : area;
  LayoutRect anchor{0, 0, node->width(), node->height()};
  Node::absolutePosition(node, anchor.x, anchor.y);
  const auto placement = area->tooltipPlacement();
  if (area != m_identity
      || *text != m_text
      || placement != m_placement
      || anchor.x != m_anchor.x
      || anchor.y != m_anchor.y
      || anchor.width != m_anchor.width
      || anchor.height != m_anchor.height) {
    m_text = *text;
    m_identity = area;
    m_anchor = anchor;
    m_placement = placement;
    m_ready = false;
    setVisible(false);
    m_delay.start(kShowDelay, [this] {
      m_ready = true;
      m_requestUpdate();
    });
  }
  if (!m_ready) {
    return;
  }
  m_label->setText(m_text);
  m_label->setMaxWidth(std::min(kMaxContentWidth, std::max(1.0F, bounds.width - 2 * (Style::spaceSm + kPadH))));
  place(renderer, bounds);
}
