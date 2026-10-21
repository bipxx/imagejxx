#pragma once
#include "../native/target_mask_resources.hpp"
#include <limits>

namespace imagejxx::desktop {
// Only actual legacy operation/preview callers use this supplement. Generic
// jobs such as exports must not inherit a charge from an unused selection.
inline std::size_t legacy_operation_workspace(const std::string &operation,
    const DocumentSnapshot &source, bool all, int slice, std::size_t extra) {
  const auto target = resolve_legacy_target(source, all, slice, operation);
  const auto rounded = detail::target_rounded_mask_workspace(target);
  if (rounded > std::numeric_limits<std::size_t>::max()-extra)
    throw std::invalid_argument("Job reservation overflows addressable memory");
  return extra+rounded;
}
} // namespace imagejxx::desktop
