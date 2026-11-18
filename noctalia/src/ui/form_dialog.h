#pragma once

#include "ui/controls/flex.h"

#include <functional>
#include <string>
#include <vector>

class InputArea;
class Input;

namespace tandem {
  // A question over the window: a title, a line of explanation, optional text fields and a row
  // of answers that receive what was typed.
  class FormDialog final : public Flex {
  public:
    struct Field {
      std::string placeholder;
      std::string value;
      bool multiline = false;
    };
    struct Answer {
      std::string label;
      // The answer Enter gives.
      bool primary = false;
      bool destructive = false;
      std::function<void(const std::vector<std::string>&)> chosen;
    };

    FormDialog(std::string title, std::string message, std::vector<Field> fields, std::vector<Answer> answers);
    // The control that takes the keyboard when the dialog opens.
    InputArea* defaultArea() const noexcept { return m_default; }
    std::vector<std::string> values() const;

  private:
    InputArea* m_default = nullptr;
    std::vector<Input*> m_inputs;
    std::function<void()> m_submit;
  };
} // namespace tandem
