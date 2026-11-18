#pragma once

#include <filesystem>

namespace tandem {
  // XDG base directories: the variable when set and non-empty, otherwise the
  // spec default under $HOME. Throw std::invalid_argument when the variable is
  // relative, or when falling back and HOME is unset or relative.
  std::filesystem::path configHome();
  std::filesystem::path stateHome();
  std::filesystem::path cacheHome();
  std::filesystem::path dataHome();
} // namespace tandem
