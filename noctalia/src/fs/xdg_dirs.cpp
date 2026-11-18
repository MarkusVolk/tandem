#include "fs/xdg_dirs.h"

#include <cstdlib>
#include <format>
#include <stdexcept>
#include <string_view>

namespace {
  std::filesystem::path baseDirectory(const char* variable, std::string_view fallback) {
    if (const char* value = std::getenv(variable); value != nullptr && *value != '\0') {
      std::filesystem::path directory(value);
      if (!directory.is_absolute()) {
        throw std::invalid_argument(std::format("{} must be an absolute path", variable));
      }
      return directory;
    }
    const char* home = std::getenv("HOME");
    if (home == nullptr || !std::filesystem::path(home).is_absolute()) {
      throw std::invalid_argument(std::format("HOME must be an absolute path when {} is unset", variable));
    }
    return std::filesystem::path(home) / fallback;
  }
} // namespace

std::filesystem::path tandem::configHome() { return baseDirectory("XDG_CONFIG_HOME", ".config"); }
std::filesystem::path tandem::stateHome() { return baseDirectory("XDG_STATE_HOME", ".local/state"); }
std::filesystem::path tandem::cacheHome() { return baseDirectory("XDG_CACHE_HOME", ".cache"); }
std::filesystem::path tandem::dataHome() { return baseDirectory("XDG_DATA_HOME", ".local/share"); }
