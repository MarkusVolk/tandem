#pragma once

#include "app/page.h"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

class Button;
class Flex;
class Image;
class Input;
class ScrollView;

namespace tandem {
  // The SMS threads of a device, newest first.
  class SmsThreadsPage final : public Page {
  public:
    SmsThreadsPage(PageHost& host, std::string id);
    std::string title() const override { return "Messages"; }
    std::string deviceId() const override { return m_id; }
    std::unique_ptr<Node> build() override;
    void refresh(Renderer& renderer) override;
    void event(const DaemonEvent& event) override;

  private:
    void rebuild();

    std::string m_id;
    std::shared_ptr<Device> m_device;
    Flex* m_list = nullptr;
    bool m_dirty = true;
  };

  // One thread: its messages as bubbles and a field to answer.
  class SmsConversationPage final : public Page {
  public:
    SmsConversationPage(PageHost& host, std::string id, std::int64_t thread, std::string title, std::vector<std::string> addresses);
    std::string title() const override { return m_title; }
    std::string deviceId() const override { return m_id; }
    std::unique_ptr<Node> build() override;
    void refresh(Renderer& renderer) override;
    void event(const DaemonEvent& event) override;

  private:
    struct Thumbnail {
      Image* image;
      std::vector<std::uint8_t> bytes;
    };
    void load();
    void rebuild(Renderer& renderer);
    void send();
    void older();

    std::string m_id;
    std::shared_ptr<Device> m_device;
    std::int64_t m_thread;
    std::string m_title;
    std::vector<std::string> m_addresses;
    Variant m_messages;
    std::int64_t m_firstDate = 0;
    ScrollView* m_scroll = nullptr;
    Flex* m_list = nullptr;
    Input* m_input = nullptr;
    Button* m_older = nullptr;
    bool m_dirty = false;
    bool m_toEnd = true;
    std::shared_ptr<bool> m_alive = std::make_shared<bool>(true);
  };

  // "14:02" for today, "03.11.2026 14:02" before.
  std::string formatMessageDate(std::int64_t milliseconds);
} // namespace tandem
