#pragma once
#include <imagejxx/execution.hpp>

namespace imagejxx {
enum class ImageJProjectionAxis { ImageJDefault, Z, Time };
struct ImageJDatasetProjectionRequest {
  // Zero-based inclusive bounds on the resolved axis. Zero group size reduces
  // the range to one plane/frame; positive groups must divide it exactly.
  int first = 0, last = 0, group_size = 0;
  ImageJProjectionAxis axis = ImageJProjectionAxis::ImageJDefault;
  int active_t = 0, active_c = 0;
  bool all_time_frames = true, all_channels = true;
  std::string method = "mean";
  bool signed16_density = false;
  // Maximum XY tile dimensions. No whole contributor group is retained.
  // Larger tiles reduce repeated reads for legacy full-plane region adapters;
  // genuine tiled providers avoid those decodes. Their private caches are excluded.
  int tile_width = 1024, tile_height = 32;
  std::size_t max_output_pixel_bytes = std::numeric_limits<std::size_t>::max();
  std::size_t max_working_bytes = std::numeric_limits<std::size_t>::max();
};
struct ImageJDatasetProjectionLayout {
  DatasetShape shape;
  SpatialCalibration calibration;
  PixelType pixel_type = PixelType::Float32;
  ImageJProjectionAxis axis = ImageJProjectionAxis::Z;
  int source_first_t = 0, source_first_c = 0, source_first = 0;
  int contributors_per_group = 0, tile_width = 0, tile_height = 0;
  bool signed16_density = false;
  std::size_t output_pixel_bytes = 0, reduction_scratch_bytes = 0;
  std::size_t working_bytes = 0;
  PlanePosition contributor(PlanePosition output, int within_group) const;
};
// Separate ImageJ 1.54r scalar numerical profile: U8/U16/F32, S1. Typed unsigned
// input codes must be exact integers, not native intermediate doubles. Finite
// Float32 input is rounded to its declared type. Reviewed IEEE sources retain
// their source words. Arithmetic NaNs are equivalent by class, not payload/sign.
// Metadata-only descriptors do not read pixels or invoke callbacks.
// Default resolves to Time exactly when Z==1. Z projection may select current
// or all T; Time projection preserves all Z and ignores all_time_frames (as the
// ImageJ one-Z dispatch does). C selection is independent in both cases.
// Calibration is copied unchanged for ordinary reduction. Positive grouping
// multiplies Z pitch even for Time and retains the voxel index of world zero,
// matching the reviewed plugin postprocessing convention. Grouping can therefore
// change world origin. No first-contributor/time rebasing. Does not implement LUTs,
// overlays, intensity metadata publication, RGB merge, history or a UI command.
ImageJDatasetProjectionLayout describe_imagej_dataset_projection(
    DatasetShape shape, PixelType type, const SpatialCalibration& calibration,
    const ImageJDatasetProjectionRequest& request);
ImageJDatasetProjectionLayout describe_imagej_dataset_projection(
    const Dataset& source, const ImageJDatasetProjectionRequest& request);
struct ImageJDatasetProjectionResult {
  Dataset dataset;
  // Dataset has no intensity calibration field: the publication owner must
  // retain this density intent when true; float results must clear it.
  bool signed16_density;
};
ImageJDatasetProjectionResult project_imagej_dataset(
    const Dataset& source, const ImageJDatasetProjectionRequest& request,
    const ExecutionControl& control = {});
} // namespace imagejxx
