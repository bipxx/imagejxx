#pragma once
#include <imagejxx/contrast_apply.hpp>
namespace imagejxx {
struct RgbContrastApplyOptions {
  double minimum=0,maximum=255;
  // ImageJ ColorProcessor component bits: red=4, green=2, blue=1.
  int channels=7;
};
struct RgbContrastApplyEstimate {
  std::size_t pixels=0,samples=0,working_bytes=0;
};
// UInt8 RGB only; RGBA is a separately qualified extension. Java ColorProcessor
// narrows the lower endpoint and its numerator, but retains the original double
// endpoint span. Always derives from this immutable baseline; no preview state.
Image apply_rgb_contrast_imagej(const Image &,const RgbContrastApplyOptions & = {},
                              const ExecutionControl & = {});
// Metadata-only admission. Limits count XY pixels; work accounts for 3 samples.
// Source metadata, provider caches, allocator/frontend and history are external.
RgbContrastApplyEstimate estimate_rgb_contrast_apply(const ResolvedTarget &,
    const RgbContrastApplyOptions & = {},const ContrastApplyLimits & = {});
// In-place independent XY planes and whole/active/positioned ROI policies.
// Preparation reads selected planes only and publishes nothing. Commit through
// the source owner/Application. Pixels and 0..255 display reset pair in history;
// threshold clears, LUTs persist, and all captured revisions guard publication.
// Empty masks retain the native no-change/no-IO extension.
DocumentTransaction prepare_rgb_contrast_apply(const ResolvedTarget &,
    const RgbContrastApplyOptions & = {},const ContrastApplyLimits & = {},
    const ExecutionControl & = {});
}
