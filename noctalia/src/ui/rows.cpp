#include "ui/rows.h"

#include "ui/builders.h"
#include "ui/style.h"

namespace {
  constexpr float kRowHeight = 48.0F;
} // namespace

tandem::rows::Group::Group(std::string title, std::string description) {
  setDirection(FlexDirection::Vertical);
  setAlign(FlexAlign::Stretch);
  setGap(Style::spaceSm);
  addChild(text(std::move(title), Style::fontSizeBody, ColorRole::OnSurface));
  static_cast<Label*>(children().back().get())->setFontWeight(FontWeight::SemiBold);
  if (!description.empty()) {
    addChild(text(std::move(description), Style::fontSizeCaption, ColorRole::OnSurfaceVariant, 3));
  }
  auto body = ui::column({.out = &m_body, .align = FlexAlign::Stretch, .paddingH = Style::spaceMd});
  m_body->setCardStyle();
  addChild(std::move(body));
}

Node* tandem::rows::Group::add(std::unique_ptr<Node> row) {
  if (!m_body->children().empty()) {
    m_body->addChild(ui::separator({.color = colorSpecFromRole(ColorRole::OutlineVariant, 0.5F)}));
  }
  return m_body->addChild(std::move(row));
}

void tandem::rows::Group::clear() {
  while (!m_body->children().empty()) {
    m_body->removeChild(m_body->children().back().get());
  }
}

std::unique_ptr<Label> tandem::rows::text(std::string value, float size, ColorRole role, int maxLines) {
  auto label = ui::label(
      {.text = std::move(value), .fontSize = size, .color = colorSpecFromRole(role), .maxLines = maxLines,
       .configure = [](Label& label) { label.setUseMarkup(false); }}
  );
  if (maxLines == 1) {
    label->setEllipsize(TextEllipsize::End);
  }
  return label;
}

std::unique_ptr<Flex> tandem::rows::row(
    std::string title, std::string subtitle, std::unique_ptr<Node> trailing, Label** subtitleOut
) {
  auto labels = ui::column({.align = FlexAlign::Stretch, .gap = 2.0F, .flexGrow = 1});
  labels->addChild(text(std::move(title), Style::fontSizeBody, ColorRole::OnSurface, 2));
  if (!subtitle.empty() || subtitleOut != nullptr) {
    auto label = text(std::move(subtitle), Style::fontSizeCaption, ColorRole::OnSurfaceVariant, 3);
    if (subtitleOut != nullptr) {
      *subtitleOut = label.get();
    }
    labels->addChild(std::move(label));
  }
  auto line = ui::row({.align = FlexAlign::Center, .gap = Style::spaceMd, .paddingV = Style::spaceSm, .minHeight = kRowHeight});
  line->addChild(std::move(labels));
  if (trailing) {
    line->addChild(std::move(trailing));
  }
  return line;
}

std::unique_ptr<Flex> tandem::rows::switchRow(
    std::string title, std::string subtitle, bool checked, std::function<void(bool)> changed, Toggle** out
) {
  return row(
      std::move(title), std::move(subtitle), ui::toggle({.out = out, .checked = checked, .onChange = std::move(changed)})
  );
}

std::unique_ptr<Flex> tandem::rows::buttonRow(
    std::string title, std::string subtitle, std::string label, std::function<void()> clicked, Button** out
) {
  return row(
      std::move(title), std::move(subtitle),
      ui::button(
          {.out = out,
           .text = std::move(label),
           .controlHeight = Style::controlHeightSm,
           .variant = ButtonVariant::Outline,
           .minWidth = 96.0F,
           .onClick = std::move(clicked)}
      )
  );
}

std::unique_ptr<Button> tandem::rows::glyphButton(const char* glyph, std::string tooltip, std::function<void()> clicked) {
  return ui::button(
      {.glyph = glyph,
       .controlHeight = Style::controlHeightSm,
       .variant = ButtonVariant::Ghost,
       .tooltip = std::move(tooltip),
       .onClick = std::move(clicked)}
  );
}
