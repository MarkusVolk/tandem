#include "platform/clipboard.h"

#include "primary-selection-unstable-v1-client-protocol.h"

#include <array>
#include <cerrno>
#include <fcntl.h>
#include <glib-unix.h>
#include <glib.h>
#include <map>
#include <set>
#include <string_view>
#include <thread>
#include <unistd.h>
#include <utility>
#include <vector>

namespace {
  constexpr std::array<std::string_view, 5> kTextTypes{
      "text/plain;charset=utf-8", "UTF8_STRING", "text/plain", "STRING", "TEXT"
  };
  constexpr const char* kUriList = "text/uri-list";
  // What a paste may bring in; a client that keeps writing past this is cut off.
  constexpr std::size_t kReadLimit = 256U * 1024U * 1024U;

  // Writes `text` to a reader on a thread, so a full pipe never stalls the window.
  void sendText(std::string text, int fd) {
    std::thread([text = std::move(text), fd] {
      std::size_t done = 0;
      while (done < text.size()) {
        const ssize_t count = ::write(fd, text.data() + done, text.size() - done);
        if (count < 0 && errno == EINTR) {
          continue;
        }
        if (count <= 0) {
          break;
        }
        done += static_cast<std::size_t>(count);
      }
      close(fd);
    }).detach();
  }
} // namespace

// What both selections share: the offers announced so far, the one that is current, the text
// this process offers and the reads in flight. The protocol calls differ and come as functions.
struct tandem::WaylandClipboard::State : std::enable_shared_from_this<State> {
  struct Read {
    std::weak_ptr<State> owner;
    int fd = -1;
    guint source = 0;
    std::string data;
    std::function<void(std::optional<std::string>)> callback;
  };
  struct Protocol {
    void (*receive)(void* offer, const char* type, int fd);
    void (*destroyOffer)(void* offer);
    void (*destroySource)(void* source);
    void* (*createSource)(State& state);
    void (*setSelection)(void* device, void* source, std::uint32_t serial);
    void (*releaseDevice)(void* device);
  };

  Protocol protocol{};
  wl_display* display = nullptr;
  void* manager = nullptr;
  void* device = nullptr;
  SerialSource serial;
  std::map<void*, std::set<std::string>> offers;
  void* selected = nullptr;
  // Set while this process owns the selection.
  void* source = nullptr;
  std::optional<std::string> text;
  std::set<Read*> reads;
  // The offer of a drag over one of our surfaces that brings files, and where it is.
  void* dragOffer = nullptr;
  wl_surface* dragSurface = nullptr;
  DropHandler dropHandler;

  void discard(void* offer) {
    if (offer && offers.erase(offer) > 0) {
      protocol.destroyOffer(offer);
    }
  }
  void announced(void* offer) { offers.emplace(offer, std::set<std::string>{}); }
  void typed(void* offer, const char* type) {
    if (const auto found = offers.find(offer); found != offers.end()) {
      found->second.emplace(type);
    }
  }
  void current(void* offer) {
    if (selected != offer) {
      discard(std::exchange(selected, offer));
    }
  }
  void send(int fd) { sendText(text.value_or(std::string()), fd); }
  void cancelled(void* cancelledSource) {
    if (source == cancelledSource) {
      source = nullptr;
      text.reset();
    }
    protocol.destroySource(cancelledSource);
  }

  static void finish(Read* read, bool ok) {
    if (read->source) {
      g_source_remove(read->source);
    }
    close(read->fd);
    if (const auto owner = read->owner.lock()) {
      owner->reads.erase(read);
    }
    const auto callback = std::move(read->callback);
    auto data = ok ? std::optional(std::move(read->data)) : std::nullopt;
    delete read;
    if (callback) {
      callback(std::move(data));
    }
  }

  static gboolean readable(gint fd, GIOCondition, gpointer data) {
    auto* read = static_cast<Read*>(data);
    std::array<char, 65536> chunk;
    while (true) {
      const ssize_t count = ::read(fd, chunk.data(), chunk.size());
      if (count > 0) {
        read->data.append(chunk.data(), static_cast<std::size_t>(count));
        if (read->data.size() > kReadLimit) {
          read->source = 0;
          finish(read, false);
          return G_SOURCE_REMOVE;
        }
        continue;
      }
      if (count < 0 && errno == EINTR) {
        continue;
      }
      if (count < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
        return G_SOURCE_CONTINUE;
      }
      read->source = 0;
      finish(read, count == 0);
      return G_SOURCE_REMOVE;
    }
  }

  void receive(std::function<void(std::optional<std::string>)> callback) {
    const auto found = offers.find(selected);
    if (found == offers.end()) {
      callback(std::nullopt);
      return;
    }
    const std::string* type = nullptr;
    for (const auto candidate : kTextTypes) {
      if (const auto at = found->second.find(std::string(candidate)); at != found->second.end()) {
        type = &*at;
        break;
      }
    }
    if (!type) {
      callback(std::nullopt);
      return;
    }
    receive(selected, *type, std::move(callback));
  }

  void receive(void* offer, const std::string& type, std::function<void(std::optional<std::string>)> callback) {
    std::array<int, 2> pipeFds{};
    if (pipe2(pipeFds.data(), O_CLOEXEC | O_NONBLOCK) != 0) {
      callback(std::nullopt);
      return;
    }
    protocol.receive(offer, type.c_str(), pipeFds[1]);
    close(pipeFds[1]);
    wl_display_flush(display);
    auto* read = new Read{.owner = weak_from_this(), .fd = pipeFds[0], .source = 0, .data = {}, .callback = std::move(callback)};
    reads.insert(read);
    read->source = g_unix_fd_add(pipeFds[0], static_cast<GIOCondition>(G_IO_IN | G_IO_HUP | G_IO_ERR), &readable, read);
  }
};

std::unique_ptr<tandem::WaylandClipboard> tandem::WaylandClipboard::clipboard(
    wl_display* display, wl_data_device_manager* manager, wl_seat* seat, SerialSource serial
) {
  auto state = std::make_shared<State>();
  state->display = display;
  state->manager = manager;
  state->serial = std::move(serial);
  state->protocol = {
      .receive = [](void* offer, const char* type, int fd) {
        wl_data_offer_receive(static_cast<wl_data_offer*>(offer), type, fd);
      },
      .destroyOffer = [](void* offer) { wl_data_offer_destroy(static_cast<wl_data_offer*>(offer)); },
      .destroySource = [](void* source) { wl_data_source_destroy(static_cast<wl_data_source*>(source)); },
      .createSource = [](State& state) -> void* {
        auto* source = wl_data_device_manager_create_data_source(static_cast<wl_data_device_manager*>(state.manager));
        static const wl_data_source_listener listener{
            .target = [](void*, wl_data_source*, const char*) {},
            .send = [](void* data, wl_data_source*, const char*, std::int32_t fd) { static_cast<State*>(data)->send(fd); },
            .cancelled = [](void* data, wl_data_source* source) { static_cast<State*>(data)->cancelled(source); },
            .dnd_drop_performed = [](void*, wl_data_source*) {},
            .dnd_finished = [](void*, wl_data_source*) {},
            .action = [](void*, wl_data_source*, std::uint32_t) {},
        };
        wl_data_source_add_listener(source, &listener, &state);
        for (const auto type : kTextTypes) {
          wl_data_source_offer(source, std::string(type).c_str());
        }
        return source;
      },
      .setSelection = [](void* device, void* source, std::uint32_t serial) {
        wl_data_device_set_selection(static_cast<wl_data_device*>(device), static_cast<wl_data_source*>(source), serial);
      },
      .releaseDevice = [](void* device) { wl_data_device_release(static_cast<wl_data_device*>(device)); },
  };
  if (manager && seat) {
    auto* device = wl_data_device_manager_get_data_device(manager, seat);
    state->device = device;
    static const wl_data_offer_listener offerListener{
        .offer = [](void* data, wl_data_offer* offer, const char* type) { static_cast<State*>(data)->typed(offer, type); },
        .source_actions = [](void*, wl_data_offer*, std::uint32_t) {},
        .action = [](void*, wl_data_offer*, std::uint32_t) {},
    };
    static const wl_data_device_listener listener{
        .data_offer = [](void* data, wl_data_device*, wl_data_offer* offer) {
          static_cast<State*>(data)->announced(offer);
          wl_data_offer_add_listener(offer, &offerListener, data);
        },
        // A drag is taken when it brings files and someone wants them, else only released.
        .enter =
            [](void* data, wl_data_device*, std::uint32_t serial, wl_surface* surface, wl_fixed_t, wl_fixed_t,
               wl_data_offer* offer) {
              auto* state = static_cast<State*>(data);
              state->discard(std::exchange(state->dragOffer, nullptr));
              const auto found = state->offers.find(offer);
              if (!state->dropHandler || found == state->offers.end() || !found->second.contains(kUriList)) {
                if (offer) {
                  wl_data_offer_accept(offer, serial, nullptr);
                }
                state->discard(offer);
                return;
              }
              state->dragOffer = offer;
              state->dragSurface = surface;
              wl_data_offer_accept(offer, serial, kUriList);
              if (wl_data_offer_get_version(offer) >= 3) {
                wl_data_offer_set_actions(
                    offer, WL_DATA_DEVICE_MANAGER_DND_ACTION_COPY, WL_DATA_DEVICE_MANAGER_DND_ACTION_COPY
                );
              }
            },
        .leave =
            [](void* data, wl_data_device*) {
              auto* state = static_cast<State*>(data);
              state->discard(std::exchange(state->dragOffer, nullptr));
            },
        .motion = [](void*, wl_data_device*, std::uint32_t, wl_fixed_t, wl_fixed_t) {},
        .drop =
            [](void* data, wl_data_device*) {
              auto* state = static_cast<State*>(data);
              void* offer = std::exchange(state->dragOffer, nullptr);
              if (!offer) {
                return;
              }
              state->receive(offer, kUriList, [weak = state->weak_from_this(), offer, surface = state->dragSurface](auto text) {
                auto state = weak.lock();
                if (!state) {
                  return;
                }
                if (wl_data_offer_get_version(static_cast<wl_data_offer*>(offer)) >= 3) {
                  wl_data_offer_finish(static_cast<wl_data_offer*>(offer));
                }
                state->discard(offer);
                if (!text || !state->dropHandler) {
                  return;
                }
                std::vector<std::string> uris;
                std::size_t start = 0;
                while (start < text->size()) {
                  std::size_t end = text->find('\n', start);
                  if (end == std::string::npos) {
                    end = text->size();
                  }
                  std::string line = text->substr(start, end - start);
                  if (!line.empty() && line.back() == '\r') {
                    line.pop_back();
                  }
                  if (!line.empty() && line.front() != '#') {
                    uris.push_back(std::move(line));
                  }
                  start = end + 1;
                }
                if (!uris.empty()) {
                  state->dropHandler(surface, std::move(uris));
                }
              });
            },
        .selection = [](void* data, wl_data_device*, wl_data_offer* offer) { static_cast<State*>(data)->current(offer); },
    };
    wl_data_device_add_listener(device, &listener, state.get());
  }
  return std::unique_ptr<WaylandClipboard>(new WaylandClipboard(std::move(state)));
}

std::unique_ptr<tandem::WaylandClipboard> tandem::WaylandClipboard::primary(
    wl_display* display, zwp_primary_selection_device_manager_v1* manager, wl_seat* seat, SerialSource serial
) {
  if (!manager || !seat) {
    return nullptr;
  }
  auto state = std::make_shared<State>();
  state->display = display;
  state->manager = manager;
  state->serial = std::move(serial);
  state->protocol = {
      .receive = [](void* offer, const char* type, int fd) {
        zwp_primary_selection_offer_v1_receive(static_cast<zwp_primary_selection_offer_v1*>(offer), type, fd);
      },
      .destroyOffer = [](void* offer) {
        zwp_primary_selection_offer_v1_destroy(static_cast<zwp_primary_selection_offer_v1*>(offer));
      },
      .destroySource = [](void* source) {
        zwp_primary_selection_source_v1_destroy(static_cast<zwp_primary_selection_source_v1*>(source));
      },
      .createSource = [](State& state) -> void* {
        auto* source = zwp_primary_selection_device_manager_v1_create_source(
            static_cast<zwp_primary_selection_device_manager_v1*>(state.manager)
        );
        static const zwp_primary_selection_source_v1_listener listener{
            .send = [](void* data, zwp_primary_selection_source_v1*, const char*, std::int32_t fd) {
              static_cast<State*>(data)->send(fd);
            },
            .cancelled = [](void* data, zwp_primary_selection_source_v1* source) {
              static_cast<State*>(data)->cancelled(source);
            },
        };
        zwp_primary_selection_source_v1_add_listener(source, &listener, &state);
        for (const auto type : kTextTypes) {
          zwp_primary_selection_source_v1_offer(source, std::string(type).c_str());
        }
        return source;
      },
      .setSelection = [](void* device, void* source, std::uint32_t serial) {
        zwp_primary_selection_device_v1_set_selection(
            static_cast<zwp_primary_selection_device_v1*>(device), static_cast<zwp_primary_selection_source_v1*>(source),
            serial
        );
      },
      .releaseDevice = [](void* device) {
        zwp_primary_selection_device_v1_destroy(static_cast<zwp_primary_selection_device_v1*>(device));
      },
  };
  auto* device = zwp_primary_selection_device_manager_v1_get_device(manager, seat);
  state->device = device;
  static const zwp_primary_selection_offer_v1_listener offerListener{
      .offer = [](void* data, zwp_primary_selection_offer_v1* offer, const char* type) {
        static_cast<State*>(data)->typed(offer, type);
      },
  };
  static const zwp_primary_selection_device_v1_listener listener{
      .data_offer = [](void* data, zwp_primary_selection_device_v1*, zwp_primary_selection_offer_v1* offer) {
        static_cast<State*>(data)->announced(offer);
        zwp_primary_selection_offer_v1_add_listener(offer, &offerListener, data);
      },
      .selection = [](void* data, zwp_primary_selection_device_v1*, zwp_primary_selection_offer_v1* offer) {
        static_cast<State*>(data)->current(offer);
      },
  };
  zwp_primary_selection_device_v1_add_listener(device, &listener, state.get());
  return std::unique_ptr<WaylandClipboard>(new WaylandClipboard(std::move(state)));
}

tandem::WaylandClipboard::WaylandClipboard(std::shared_ptr<State> state) : m_state(std::move(state)) {}

tandem::WaylandClipboard::~WaylandClipboard() {
  auto& state = *m_state;
  for (auto* read : std::set(state.reads)) {
    read->callback = nullptr;
    State::finish(read, false);
  }
  for (const auto& [offer, types] : state.offers) {
    (void)types;
    state.protocol.destroyOffer(offer);
  }
  state.offers.clear();
  if (state.source) {
    state.protocol.destroySource(state.source);
  }
  if (state.device) {
    state.protocol.releaseDevice(state.device);
  }
}

std::optional<std::string> tandem::WaylandClipboard::clipboardText() { return m_state->text; }

void tandem::WaylandClipboard::setClipboardText(std::string text) {
  auto& state = *m_state;
  const std::uint32_t serial = state.serial ? state.serial() : 0;
  if (!state.device || serial == 0) {
    return;
  }
  // Marking more of the same text keeps the source and only changes what it sends.
  if (state.source && state.text) {
    state.text = std::move(text);
    return;
  }
  if (state.source) {
    state.protocol.destroySource(state.source);
  }
  state.source = state.protocol.createSource(state);
  state.text = std::move(text);
  state.protocol.setSelection(state.device, state.source, serial);
}

void tandem::WaylandClipboard::setDropHandler(DropHandler handler) { m_state->dropHandler = std::move(handler); }

void tandem::WaylandClipboard::requestClipboardText(std::function<void(std::optional<std::string>)> callback) {
  auto& state = *m_state;
  if (state.source) {
    callback(state.text);
    return;
  }
  state.receive(std::move(callback));
}
