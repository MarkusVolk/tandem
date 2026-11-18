#include "platform/file_portal.h"

#include "core/log.h"

#include <algorithm>
#include <format>
#include <unistd.h>
#include <utility>

namespace {
  constexpr const char* kPortalName = "org.freedesktop.portal.Desktop";
  constexpr const char* kPortalPath = "/org/freedesktop/portal/desktop";
  constexpr const char* kChooser = "org.freedesktop.portal.FileChooser";
  constexpr const char* kRequest = "org.freedesktop.portal.Request";
  const Logger kLog("portal");
} // namespace

struct tandem::FilePortal::Request {
  FilePortal* owner = nullptr;
  guint subscription = 0;
  Callback callback;
};

tandem::FilePortal::FilePortal() {
  GError* error = nullptr;
  m_connection = g_bus_get_sync(G_BUS_TYPE_SESSION, nullptr, &error);
  if (!m_connection) {
    kLog.warn("no session bus, file dialogs are unavailable: {}", error ? error->message : "unknown");
    g_clear_error(&error);
  }
}

tandem::FilePortal::~FilePortal() {
  for (const auto& request : m_requests) {
    g_dbus_connection_signal_unsubscribe(m_connection, request->subscription);
  }
  if (m_connection) {
    g_object_unref(m_connection);
  }
}

void tandem::FilePortal::finish(Request* request, std::vector<std::filesystem::path> paths) {
  g_dbus_connection_signal_unsubscribe(m_connection, request->subscription);
  const auto callback = std::move(request->callback);
  std::erase_if(m_requests, [request](const auto& candidate) { return candidate.get() == request; });
  if (callback) {
    callback(std::move(paths));
  }
}

void tandem::FilePortal::response(
    GDBusConnection*, const char*, const char*, const char*, const char*, GVariant* parameters, gpointer data
) {
  auto* request = static_cast<Request*>(data);
  guint32 code = 2;
  GVariant* results = nullptr;
  g_variant_get(parameters, "(u@a{sv})", &code, &results);
  std::vector<std::filesystem::path> paths;
  if (code == 0) {
    const char** uris = nullptr;
    if (g_variant_lookup(results, "uris", "^a&s", &uris) && uris) {
      for (const char** uri = uris; *uri; ++uri) {
        if (gchar* path = g_filename_from_uri(*uri, nullptr, nullptr)) {
          paths.emplace_back(path);
          g_free(path);
        }
      }
      g_free(static_cast<gpointer>(uris));
    }
  }
  g_variant_unref(results);
  request->owner->finish(request, std::move(paths));
}

void tandem::FilePortal::call(
    const char* method, const std::string& parent, const char* title, GVariantBuilder& options, Callback callback
) {
  if (!m_connection) {
    callback({});
    return;
  }
  // The portal answers on a request object whose path follows from our bus name and this token;
  // subscribing before the call cannot miss the answer.
  const std::string token = std::format("tandem_{}_{}", getpid(), ++m_counter);
  std::string sender = g_dbus_connection_get_unique_name(m_connection) + 1;
  std::ranges::replace(sender, '.', '_');
  const std::string path = std::format("{}/request/{}/{}", kPortalPath, sender, token);
  g_variant_builder_add(&options, "{sv}", "handle_token", g_variant_new_string(token.c_str()));

  auto request = std::make_unique<Request>();
  request->owner = this;
  request->callback = std::move(callback);
  request->subscription = g_dbus_connection_signal_subscribe(
      m_connection, kPortalName, kRequest, "Response", path.c_str(), nullptr, G_DBUS_SIGNAL_FLAGS_NONE,
      &response, request.get(), nullptr
  );
  Request* raw = request.get();
  m_requests.push_back(std::move(request));
  g_dbus_connection_call(
      m_connection, kPortalName, kPortalPath, kChooser, method,
      g_variant_new("(ssa{sv})", parent.c_str(), title, &options), G_VARIANT_TYPE("(o)"), G_DBUS_CALL_FLAGS_NONE, -1,
      nullptr,
      [](GObject* source, GAsyncResult* result, gpointer data) {
        GError* error = nullptr;
        GVariant* reply = g_dbus_connection_call_finish(G_DBUS_CONNECTION(source), result, &error);
        if (reply) {
          g_variant_unref(reply);
          return;
        }
        kLog.warn("the file dialog could not be opened: {}", error ? error->message : "unknown");
        g_clear_error(&error);
        // The request may be gone already when the portal vanished with it.
        auto* request = static_cast<Request*>(data);
        auto* owner = request->owner;
        const bool pending = std::ranges::any_of(owner->m_requests, [request](const auto& candidate) {
          return candidate.get() == request;
        });
        if (pending) {
          owner->finish(request, {});
        }
      },
      raw
  );
}

void tandem::FilePortal::openFiles(const std::string& parent, const std::string& title, Callback callback) {
  GVariantBuilder options;
  g_variant_builder_init(&options, G_VARIANT_TYPE_VARDICT);
  g_variant_builder_add(&options, "{sv}", "multiple", g_variant_new_boolean(TRUE));
  call("OpenFile", parent, title.c_str(), options, std::move(callback));
}

void tandem::FilePortal::chooseFolder(
    const std::string& parent, const std::string& title, const std::filesystem::path& folder, Callback callback
) {
  GVariantBuilder options;
  g_variant_builder_init(&options, G_VARIANT_TYPE_VARDICT);
  g_variant_builder_add(&options, "{sv}", "directory", g_variant_new_boolean(TRUE));
  if (!folder.empty()) {
    g_variant_builder_add(&options, "{sv}", "current_folder", g_variant_new_bytestring(folder.c_str()));
  }
  call("OpenFile", parent, title.c_str(), options, std::move(callback));
}
