#pragma once

namespace tandem {
  // Runs the frontend. As a D-Bus activated service it waits for the request that started it
  // instead of opening the window right away.
  int runApplication(bool service);
} // namespace tandem
