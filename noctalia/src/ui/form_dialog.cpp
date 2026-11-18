#include "ui/form_dialog.h"

#include "ui/builders.h"
#include "ui/style.h"

namespace {
  constexpr float kWidth = 420.0F;
}

tandem::FormDialog::FormDialog(
    std::string title, std::string message, std::vector<Field> fields, std::vector<Answer> answers
) {
  setDirection(FlexDirection::Vertical);
  setAlign(FlexAlign::Stretch);
  setGap(Style::spaceMd);
  setPadding(Style::spaceLg);
  setFill(colorSpecFromRole(ColorRole::SurfaceContainerHigh));
  setStyleRadius(Style::radiusXl);
  setBorder(colorSpecFromRole(ColorRole::OutlineVariant), Style::borderWidth);
  setParticipatesInLayout(false);
  setZIndex(110);
  setMinWidth(kWidth);

  const auto literal = [](Label& label) { label.setUseMarkup(false); };
  addChild(
      ui::label(
          {.text = std::move(title),
           .fontSize = Style::fontSizeTitle,
           .fontWeight = FontWeight::SemiBold,
           .color = colorSpecFromRole(ColorRole::OnSurface),
           .maxWidth = kWidth,
           .maxLines = 2,
           .configure = literal}
      )
  );
  if (!message.empty()) {
    addChild(
        ui::label(
            {.text = std::move(message),
             .fontSize = Style::fontSizeBody,
             .color = colorSpecFromRole(ColorRole::OnSurfaceVariant),
             .maxWidth = kWidth,
             .maxLines = 6,
             .configure = literal}
        )
    );
  }
  for (auto& field : fields) {
    Input* input = nullptr;
    addChild(
        ui::input(
            {.out = &input,
             .value = field.value,
             .placeholder = field.placeholder,
             .onSubmit = [this](const std::string&) {
               if (m_submit) {
                 m_submit();
               }
             },
             .configure = [multiline = field.multiline](Input& input) {
               if (multiline) {
                 input.setMultiline(true);
               }
             }}
        )
    );
    m_inputs.push_back(input);
  }
  auto row = ui::row({.align = FlexAlign::Center, .justify = FlexJustify::End, .gap = Style::spaceSm});
  for (auto& answer : answers) {
    Button* button = nullptr;
    auto chosen = [this, callback = std::move(answer.chosen)] {
      if (callback) {
        callback(values());
      }
    };
    if (answer.primary) {
      m_submit = chosen;
    }
    row->addChild(
        ui::button(
            {.out = &button,
             .text = answer.label,
             .controlHeight = Style::controlHeightSm,
             .variant = answer.primary       ? ButtonVariant::Primary
                 : answer.destructive ? ButtonVariant::Destructive
                                      : ButtonVariant::Outline,
             .minWidth = 88.0F,
             .onClick = std::move(chosen)}
        )
    );
    if (m_inputs.empty() && (answer.primary || !m_default)) {
      m_default = button->inputArea();
    }
  }
  if (!m_inputs.empty()) {
    m_default = m_inputs.front()->inputArea();
  }
  addChild(std::move(row));
}

std::vector<std::string> tandem::FormDialog::values() const {
  std::vector<std::string> result;
  result.reserve(m_inputs.size());
  for (const Input* input : m_inputs) {
    result.push_back(input->value());
  }
  return result;
}
