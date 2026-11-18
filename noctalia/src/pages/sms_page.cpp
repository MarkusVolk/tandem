#include "pages/sms_page.h"

#include "ui/builders.h"
#include "ui/controls/image.h"
#include "ui/rows.h"
#include "ui/style.h"

#include <algorithm>
#include <ctime>
#include <format>

namespace {
  constexpr float kBubbleWidth = 420.0F;
  constexpr float kContentWidth = 680.0F;

  std::string oneLine(std::string text) {
    std::ranges::replace(text, '\n', ' ');
    return text;
  }
} // namespace

std::string tandem::formatMessageDate(std::int64_t milliseconds) {
  if (milliseconds <= 0) {
    return {};
  }
  const std::time_t when = static_cast<std::time_t>(milliseconds / 1000);
  const std::time_t now = std::time(nullptr);
  std::tm local{};
  std::tm today{};
  localtime_r(&when, &local);
  localtime_r(&now, &today);
  char text[32];
  const bool sameDay = local.tm_year == today.tm_year && local.tm_yday == today.tm_yday;
  std::strftime(text, sizeof(text), sameDay ? "%H:%M" : "%d.%m.%Y %H:%M", &local);
  return text;
}

// ── Threads ──────────────────────────────────────────────────────────────────

tandem::SmsThreadsPage::SmsThreadsPage(PageHost& host, std::string id)
    : Page(host), m_id(std::move(id)), m_device(daemon().device(m_id)) {}

std::unique_ptr<Node> tandem::SmsThreadsPage::build() {
  ScrollView* scroll = nullptr;
  auto view = ui::scrollView({.out = &scroll, .viewportPaddingH = Style::spaceLg, .flexGrow = 1});
  scroll->content()->setDirection(FlexDirection::Vertical);
  scroll->content()->setAlign(FlexAlign::Center);
  scroll->content()->addChild(
      ui::column(
          {.out = &m_list, .align = FlexAlign::Stretch, .gap = Style::spaceMd, .paddingV = Style::spaceMd,
           .maxWidth = kContentWidth, .fillWidth = true}
      )
  );
  if (m_device && m_device->reachable()) {
    m_device->callPlugin("sms", "threads");
  }
  m_dirty = true;
  return view;
}

void tandem::SmsThreadsPage::event(const DaemonEvent& event) {
  if (event.device == m_id && (event.plugin == "sms" || event.kind == DaemonEvent::Kind::Device)) {
    m_dirty = true;
  }
}

void tandem::SmsThreadsPage::refresh(Renderer& /*renderer*/) {
  if (m_dirty && m_device) {
    rebuild();
  }
}

void tandem::SmsThreadsPage::rebuild() {
  m_dirty = false;
  while (!m_list->children().empty()) {
    m_list->removeChild(m_list->children().back().get());
  }
  auto header = ui::row({.align = FlexAlign::Center, .gap = Style::spaceSm});
  auto hint = rows::text(
      m_device->reachable() ? "Conversations synchronised from the device" : "The device is not reachable",
      Style::fontSizeCaption, ColorRole::OnSurfaceVariant
  );
  hint->setFlexGrow(1);
  header->addChild(std::move(hint));
  header->addChild(rows::glyphButton("refresh", "Ask the device again", [weak = std::weak_ptr<Device>(m_device)] {
    if (auto device = weak.lock()) {
      device->callPlugin("sms", "threads");
    }
  }));
  m_list->addChild(std::move(header));

  auto threads = m_device->pluginState("sms").maps("threads");
  std::ranges::stable_sort(threads, [](const Variant& a, const Variant& b) {
    return a.lookup("last").integer("date") > b.lookup("last").integer("date");
  });
  if (threads.empty()) {
    m_list->addChild(rows::text("No conversations yet", Style::fontSizeBody, ColorRole::OnSurfaceVariant));
    return;
  }
  auto group = std::make_unique<rows::Group>("Conversations");
  for (const auto& thread : threads) {
    const Variant last = thread.lookup("last");
    const std::int64_t id = thread.integer("id");
    std::string title = thread.string("title");
    const auto addresses = thread.strings("addresses");
    if (title.empty() && !addresses.empty()) {
      title = addresses.front();
    }
    const bool unread = !last.boolean("read", true) && !last.boolean("outgoing", true);
    auto trailing = ui::row({.align = FlexAlign::Center, .gap = Style::spaceSm});
    if (unread) {
      trailing->addChild(ui::glyph({.glyph = "point-filled", .glyphSize = 12.0F, .color = colorSpecFromRole(ColorRole::Primary)}));
    }
    trailing->addChild(rows::text(formatMessageDate(last.integer("date")), Style::fontSizeCaption, ColorRole::OnSurfaceVariant));
    trailing->addChild(rows::glyphButton("chevron-right", "Open", [this, id, title, addresses] {
      host().push(std::make_unique<SmsConversationPage>(host(), m_id, id, title, addresses));
    }));
    group->add(rows::row(title, oneLine(last.string("body")), std::move(trailing)));
  }
  m_list->addChild(std::move(group));
}

// ── Conversation ─────────────────────────────────────────────────────────────

tandem::SmsConversationPage::SmsConversationPage(
    PageHost& host, std::string id, std::int64_t thread, std::string title, std::vector<std::string> addresses
)
    : Page(host), m_id(std::move(id)), m_device(daemon().device(m_id)), m_thread(thread), m_title(std::move(title)),
      m_addresses(std::move(addresses)) {}

std::unique_ptr<Node> tandem::SmsConversationPage::build() {
  auto page = ui::column({.align = FlexAlign::Stretch, .flexGrow = 1});
  page->addChild(ui::scrollView({.out = &m_scroll, .viewportPaddingH = Style::spaceLg, .flexGrow = 1}));
  m_scroll->content()->setDirection(FlexDirection::Vertical);
  m_scroll->content()->setAlign(FlexAlign::Center);
  m_scroll->content()->addChild(
      ui::column(
          {.out = &m_list, .align = FlexAlign::Stretch, .gap = Style::spaceSm, .paddingV = Style::spaceMd,
           .maxWidth = kContentWidth, .fillWidth = true}
      )
  );
  auto entry = ui::row(
      {.align = FlexAlign::Center, .gap = Style::spaceSm, .paddingV = Style::spaceSm, .paddingH = Style::spaceLg,
       .fill = colorSpecFromRole(ColorRole::SurfaceContainerLow)}
  );
  entry->addChild(
      ui::input(
          {.out = &m_input, .placeholder = "Message", .flexGrow = 1, .onSubmit = [this](const std::string&) { send(); }}
      )
  );
  entry->addChild(rows::glyphButton("send", "Send", [this] { send(); }));
  page->addChild(std::move(entry));
  m_toEnd = true;
  load();
  bool loaded = false;
  if (m_device) {
    for (const auto& thread : m_device->pluginState("sms").maps("threads")) {
      if (thread.integer("id") == m_thread) {
        loaded = thread.boolean("loaded");
      }
    }
    if (!loaded) {
      m_device->callPlugin(
          "sms", "request-messages",
          VariantMap().add("thread", m_thread).add("before", std::int64_t{0}).add("count", std::int32_t{50}).end()
      );
    }
  }
  return page;
}

void tandem::SmsConversationPage::event(const DaemonEvent& event) {
  if (event.device == m_id && event.plugin == "sms") {
    load();
  }
}

void tandem::SmsConversationPage::load() {
  if (!m_device) {
    return;
  }
  m_device->callPlugin(
      "sms", "messages", VariantMap().add("thread", m_thread).end(),
      [this, alive = std::weak_ptr<bool>(m_alive)](CallResult result) {
        if (alive.expired() || !result.ok()) {
          return;
        }
        auto children = result.reply.children();
        m_messages = children.empty() ? Variant() : children.front();
        m_dirty = true;
        host().requestUpdate();
      }
  );
}

void tandem::SmsConversationPage::refresh(Renderer& renderer) {
  if (m_dirty && m_list) {
    rebuild(renderer);
  }
}

void tandem::SmsConversationPage::rebuild(Renderer& renderer) {
  m_dirty = false;
  const float offset = m_scroll->scrollOffset();
  while (!m_list->children().empty()) {
    m_list->removeChild(m_list->children().back().get());
  }
  auto messages = m_messages.maps("messages");
  std::ranges::stable_sort(messages, [](const Variant& a, const Variant& b) { return a.integer("date") < b.integer("date"); });
  m_list->addChild(
      ui::row(
          {.align = FlexAlign::Center, .justify = FlexJustify::Center},
          ui::button(
              {.out = &m_older,
               .text = "Load older messages",
               .controlHeight = Style::controlHeightSm,
               .enabled = !messages.empty(),
               .variant = ButtonVariant::Ghost,
               .onClick = [this] { older(); }}
          )
      )
  );
  m_firstDate = messages.empty() ? 0 : messages.front().integer("date");
  const std::weak_ptr<Device> weak = m_device;
  for (const auto& message : messages) {
    const bool outgoing = message.boolean("outgoing");
    auto bubble = ui::column(
        {.align = outgoing ? FlexAlign::End : FlexAlign::Start,
         .gap = Style::spaceXs,
         .padding = Style::spaceSm,
         .paddingH = Style::spaceMd,
         .fill = colorSpecFromRole(outgoing ? ColorRole::PrimaryContainer : ColorRole::SurfaceContainerHigh),
         .styleRadius = Style::radiusLg,
         .maxWidth = kBubbleWidth}
    );
    const std::string body = message.string("body");
    if (!body.empty()) {
      auto text = rows::text(body, Style::fontSizeBody, outgoing ? ColorRole::OnPrimaryContainer : ColorRole::OnSurface, 0);
      text->setMaxWidth(kBubbleWidth - 2.0F * Style::spaceMd);
      bubble->addChild(std::move(text));
    }
    for (const auto& attachment : message.maps("attachments")) {
      const std::int64_t part = attachment.integer("part_id");
      const std::string unique = attachment.string("unique_identifier");
      const auto thumbnail = attachment.bytes("thumbnail");
      Button* button = nullptr;
      bubble->addChild(
          ui::button(
              {.out = &button,
               .text = thumbnail.empty() ? attachment.string("mime_type", "Attachment") : std::string(),
               .glyph = thumbnail.empty() ? std::string("paperclip") : std::string(),
               .variant = ButtonVariant::Ghost,
               .tooltip = "Open the attachment",
               .padding = thumbnail.empty() ? Style::spaceXs : 0.0F,
               .onClick = [weak, part, unique] {
                 if (auto device = weak.lock()) {
                   device->callPlugin(
                       "sms", "attachment", VariantMap().add("part_id", part).add("unique_identifier", unique).end()
                   );
                 }
               }}
          )
      );
      if (!thumbnail.empty()) {
        auto image = std::make_unique<Image>();
        image->setFit(ImageFit::Contain);
        image->setSize(160.0F, 120.0F);
        image->setStyleRadius(Style::radiusMd);
        image->setSourceBytes(renderer, thumbnail.data(), thumbnail.size());
        button->addChild(std::move(image));
      }
    }
    bubble->addChild(
        rows::text(
            formatMessageDate(message.integer("date")), Style::fontSizeMini,
            outgoing ? ColorRole::OnPrimaryContainer : ColorRole::OnSurfaceVariant
        )
    );
    m_list->addChild(
        ui::row({.align = FlexAlign::Start, .justify = outgoing ? FlexJustify::End : FlexJustify::Start}, std::move(bubble))
    );
  }
  if (m_toEnd) {
    m_scroll->requestScrollToBottom();
  } else {
    m_scroll->requestScrollToOffset(offset);
  }
}

void tandem::SmsConversationPage::send() {
  if (!m_device || !m_input || m_input->value().empty()) {
    return;
  }
  m_device->callPlugin("sms", "send", VariantMap().add("addresses", m_addresses).add("body", m_input->value()).end());
  m_input->setValue("");
  m_toEnd = true;
}

void tandem::SmsConversationPage::older() {
  if (!m_device) {
    return;
  }
  m_toEnd = false;
  m_device->callPlugin(
      "sms", "request-messages",
      VariantMap().add("thread", m_thread).add("before", m_firstDate).add("count", std::int32_t{50}).end()
  );
}
