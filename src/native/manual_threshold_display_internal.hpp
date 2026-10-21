#pragma once
#include <imagejxx/manual_threshold.hpp>
namespace imagejxx::detail {
// Read-only display admission, independent of finite-only threshold editing.
// Opted Float32 samples are validated bitwise; nonfinite pixels never tint.
void blend_manual_threshold_source_overlay(
    const Image &source, Image &rgb, ManualThresholdRange range,
    FloatingSamplePolicy policy, const ManualThresholdLimits &limits = {},
    const ExecutionControl &control = {});
}
