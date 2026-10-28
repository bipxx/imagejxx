#pragma once
#include <imagejxx/global_calibration.hpp>
namespace imagejxx::detail {
// Shared across target/preparation/job copies. Local raw targets need only the
// generation guard, without parsing an irrelevant reserved intensity record.
struct TargetCalibrationBinding {
  std::weak_ptr<const int> application;
  std::uint64_t global_generation=0,bypass_generation=0;
  bool ignore=false;
  std::optional<EffectiveCalibration> effective;
};
} // namespace imagejxx::detail
