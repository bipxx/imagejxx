#pragma once
#include "../native/target_mask_resources.hpp"
#include <limits>

namespace imagejxx::desktop {
// Shared by submission and preflight so the supplement receives exactly the
// remaining per-job allowance. Legacy image storage is double-backed.
inline std::size_t legacy_job_base_workspace(const DocumentSnapshot &source) {
  const auto shape = source.data().dataset.shape();
  const auto maximum = std::numeric_limits<std::size_t>::max();
  auto multiply = [maximum](std::size_t a, std::size_t b) {
    if (b && a > maximum / b)
      throw std::invalid_argument("Job reservation overflows addressable memory");
    return a * b;
  };
  const auto pixels = multiply(std::size_t(shape.x), std::size_t(shape.y));
  const auto plane = multiply(multiply(pixels, std::size_t(shape.s)), sizeof(double));
  if (std::size_t(shape.z) > maximum - 16)
    throw std::invalid_argument("Job reservation overflows addressable memory");
  return multiply(plane, std::size_t(shape.z) + 16);
}

// Only actual legacy operation/preview callers use this supplement. Generic
// jobs such as exports must not inherit a charge from an unused selection.
// A zero scheduler budget is unlimited; admission callers pass the live limit.
inline std::size_t legacy_operation_workspace(const std::string &operation,
    const DocumentSnapshot &source, bool all, int slice, std::size_t extra,
    std::size_t memory_budget = 0) {
  const auto maximum = std::numeric_limits<std::size_t>::max();
  const auto base = legacy_job_base_workspace(source);
  if (extra > maximum - base)
    throw std::invalid_argument("Job reservation overflows addressable memory");
  const auto cap = memory_budget ? memory_budget : maximum;
  if (base > cap || extra > cap - base)
    throw std::invalid_argument("Job reservation exceeds the processing memory budget");
  const auto target = resolve_legacy_target(source, all, slice, operation);
  // Current reservations affect worker scheduling, not whether this job can
  // ever fit. The quiet preflight scratch is not a scheduler-ledger reservation.
  const auto mask = detail::target_mask_workspace(target, operation, cap - base - extra);
  if (mask > maximum - base - extra)
    throw std::invalid_argument("Job reservation overflows addressable memory");
  return extra + mask;
}
} // namespace imagejxx::desktop
