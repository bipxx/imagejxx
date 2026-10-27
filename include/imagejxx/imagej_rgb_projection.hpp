#pragma once
#include <imagejxx/imagej_dataset_projection.hpp>

namespace imagejxx {
// The scalar request/API retains its S1-only admission. This explicit RGB
// profile shares axis, grouping, calibration, tile/cap and cancellation rules.
// It accepts UInt8 S3 with C1, plus C-only legacy data reduced across Channels.
// RGB hyperstacks with C>1 and RGBA retain their separate native extensions.
// Intensity calibration tables are not admitted for RGB (ImageJ clears them).
struct ImageJRGBProjectionRequest : ImageJDatasetProjectionRequest {
  // Only affects sum. False: Float32 shared-max scaling followed by independent
  // channel range conversion. True: round/clamp each channel sum to UInt8.
  // ImageJ ignores this property for the other five projection methods.
  bool clip_when_summing = false;
};
ImageJDatasetProjectionLayout describe_imagej_rgb_projection(
    DatasetShape shape, PixelType type, const SpatialCalibration& calibration,
    const ImageJRGBProjectionRequest& request);
ImageJDatasetProjectionLayout describe_imagej_rgb_projection(
    const Dataset& source, const ImageJRGBProjectionRequest& request);
// Output is UInt8 S3, finite-only, with both density intents false. Grouped
// C-only projection can retain multiple output C groups, as ImageJ does.
// Sum/SD merge in the existing output buffer, with cancellable scan passes.
// Does not publish documents or implement overlay/LUT/UI/strict-record policy.
ImageJDatasetProjectionResult project_imagej_rgb_dataset(
    const Dataset& source, const ImageJRGBProjectionRequest& request,
    const ExecutionControl& control = {});
} // namespace imagejxx
