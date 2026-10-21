#pragma once
#include <imagejxx/execution.hpp>

namespace imagejxx {
enum class ColorComponentSpace { RGB, HSB8, HSB32 };
// Processor-plane interchange, not a scientific C/Z/T remap. Split requires
// UInt8 RGB. Components are three scalar byte planes, or Float32 for HSB32.
// Merge accepts three matching planes (RGB also accepts two, with blue zero).
// HSB32 accepts finite Float32 values, including values outside [0,1], using
// ImageJ/Java packed-RGB semantics. RGB inversion complements all three bytes.
// Inputs and callbacks are captured before callbacks run; outputs are owned.
// The conservative working cap covers double-backed input, snapshot and output
// plus fixed overhead, not all allocator/process memory or user callbacks.
Stack split_color_components(const Image &, ColorComponentSpace,
    const ExecutionControl & = {}, std::size_t max_working_bytes = 512ULL*1024*1024);
Image merge_color_components(const Stack &, ColorComponentSpace, bool invert_rgb = false,
    const ExecutionControl & = {}, std::size_t max_working_bytes = 512ULL*1024*1024);
} // namespace imagejxx
