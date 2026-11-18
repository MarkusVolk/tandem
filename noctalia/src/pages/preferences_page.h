#pragma once

#include "app/page.h"

class Flex;

namespace tandem {
  // The settings of this computer as the daemon keeps them.
  class PreferencesPage final : public Page {
  public:
    explicit PreferencesPage(PageHost& host) : Page(host) {}
    std::string title() const override { return "Preferences"; }
    std::unique_ptr<Node> build() override;
    void refresh(Renderer& renderer) override;
    void event(const DaemonEvent& event) override;

  private:
    void rebuild();

    Flex* m_content = nullptr;
    bool m_dirty = true;
    // The name field keeps what is being typed while settings arrive.
    bool m_editing = false;
  };
} // namespace tandem
