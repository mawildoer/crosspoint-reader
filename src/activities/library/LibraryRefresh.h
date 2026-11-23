#pragma once

#include <cstdint>

// Wait for a quiet interval after article changes, including across millis() wrap.
class LibraryRefresh {
 public:
  void reset(uint32_t generation) {
    observed = generation;
    pending = false;
  }
  void observe(uint32_t generation, uint32_t now) {
    if (generation == observed) return;
    observed = generation;
    changedAt = now;
    pending = true;
  }
  bool due(uint32_t now) const { return pending && uint32_t(now - changedAt) >= 1500; }

 private:
  uint32_t observed = 0;
  uint32_t changedAt = 0;
  bool pending = false;
};
