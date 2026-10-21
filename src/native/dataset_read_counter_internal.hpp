#pragma once

#include <atomic>
#include <cstdint>
#include <limits>

namespace imagejxx::detail {
inline void increment_saturated_read_count(std::atomic<std::uint64_t> &counter,
                                           std::atomic<bool> &overflowed) noexcept {
  auto value = counter.load(std::memory_order_relaxed);
  for (;;) {
    if (value == std::numeric_limits<std::uint64_t>::max()) {
      overflowed.store(true, std::memory_order_relaxed);
      return;
    }
    if (counter.compare_exchange_weak(value, value + 1,
                                      std::memory_order_relaxed,
                                      std::memory_order_relaxed))
      return;
  }
}
} // namespace imagejxx::detail
