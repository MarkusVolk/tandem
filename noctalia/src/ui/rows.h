#pragma once

#include "ui/controls/flex.h"

#include <functional>
#include <memory>
#include <string>

class Button;
class Label;
class Toggle;

namespace tandem::rows {
  // A titled card of rows, like a preferences group; rows go into the returned card's body().
  class Group final : public Flex {
  public:
    Group(std::string title, std::string description = {});
    Flex* body() const noexcept { return m_body; }
    // Adds a row; a separator goes between rows.
    Node* add(std::unique_ptr<Node> row);
    void clear();

  private:
    Flex* m_body = nullptr;
  };

  // A row with a title, an optional subtitle under it and a control at the end.
  std::unique_ptr<Flex> row(
      std::string title, std::string subtitle = {}, std::unique_ptr<Node> trailing = nullptr, Label** subtitleOut = nullptr
  );
  // A row whose control is a switch.
  std::unique_ptr<Flex> switchRow(
      std::string title, std::string subtitle, bool checked, std::function<void(bool)> changed, Toggle** out = nullptr
  );
  // A row with a button at its end.
  std::unique_ptr<Flex> buttonRow(
      std::string title, std::string subtitle, std::string label, std::function<void()> clicked,
      Button** out = nullptr
  );
  // A small button with only a glyph, for the end of a row.
  std::unique_ptr<Button> glyphButton(const char* glyph, std::string tooltip, std::function<void()> clicked);
  // Plain text that is not parsed as markup.
  std::unique_ptr<Label> text(std::string value, float size, ColorRole role, int maxLines = 1);
} // namespace tandem::rows
