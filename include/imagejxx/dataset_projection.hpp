#pragma once
#include <imagejxx/dataset.hpp>

namespace imagejxx {
// Explicit zero-based inclusive Z bounds. Scientific C is independent of the
// interleaved sample axis S. Active indices are captured even for all-T/all-C.
struct DatasetZProjectionRequest {
  int first = 0, last = 0, group_size = 0;
  int active_t = 0, active_c = 0;
  bool all_time_frames = true, all_channels = true;
  std::string method = "mean";
  // Output pixel storage only, not total worker/provider/publication admission.
  std::size_t max_output_pixel_bytes = std::numeric_limits<std::size_t>::max();
};

struct DatasetZProjectionLayout {
  DatasetShape shape;
  SpatialCalibration calibration;
  PixelType pixel_type = PixelType::Float64;
  int source_first_t = 0, source_first_c = 0, source_first_z = 0;
  int contributors_per_group = 0;
  std::size_t output_pixel_bytes = 0, reduction_scratch_bytes = 0;
  // Inverse mapping to one contributor. Validates all output indices first.
  PlanePosition contributor(PlanePosition output, int within_group) const;
};

// Metadata-only: never reads/allocates source or output pixels. T/C are never
// flattened into Z, including for a one-Z time series. Uses the existing native
// reduction type policy; this is not pinned ImageJ numeric/dtype equivalence.
DatasetZProjectionLayout describe_dataset_z_projection(
    DatasetShape shape, PixelType type, const SpatialCalibration &calibration,
    const DatasetZProjectionRequest &request);
DatasetZProjectionLayout describe_dataset_z_projection(
    const Dataset &source, const DatasetZProjectionRequest &request);
} // namespace imagejxx
