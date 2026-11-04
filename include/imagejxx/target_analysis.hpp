#pragma once
#include <imagejxx/calibrated_analysis.hpp>
#include <imagejxx/execution.hpp>

namespace imagejxx {
struct TargetAnalysisOptions {
  bool statistics = true;
  // Explicit, frozen, single-pixel-width line, in source XY voxel coordinates.
  // Profile sampling is independent of area ROI membership. Both may be enabled.
  std::optional<std::array<double,4>> line;
  bool interpolate = true;
  // Alternative to line: 2..4096 vertices, shared endpoints emitted once.
  std::vector<std::array<double,2>> path;
  // Zero uses ImageJ round(length)+1 for line; path uses the editor's
  // max(2,ceil(length)+1) per segment. Otherwise 2..1,000,000 per segment.
  std::size_t samples_per_segment = 0;
};
struct TargetAnalysisLimits {
  std::size_t max_pixels = 128ULL * 1024 * 1024;
  std::size_t max_working_bytes = 128ULL * 1024 * 1024;
  std::size_t max_record_bytes = 16ULL * 1024 * 1024;
};
struct TargetAnalysisEstimate {
  std::size_t plane_count=0, plane_pixels=0, total_pixels=0,
      working_bytes=0, plane_bytes=0, mask_bytes=0, geometry_bytes=0,
      record_bytes=0, calibration_bytes=0, kernel_bytes=0,
      result_bytes=0, plot_bytes=0, profile_samples=0;
};
struct TargetAnalysisPlane {
  PlanePosition position;
  std::optional<ImageJScalarStatistics> statistics;
  std::vector<double> profile_values;
  // Tight geometric mask membership, independent of density NaN exclusions.
  // nullopt denotes an empty geometric mask or a profile-only row.
  std::optional<std::array<int,4>> bounds;
};
// Detached results retain no source/provider or application. Distances are
// Euclidean lengths in the captured spatial affine, not screen coordinates.
struct TargetAnalysisResult {
  std::vector<TargetAnalysisPlane> planes;
  std::vector<double> profile_distances;
  SpatialCalibration spatial;
  MetadataValue intensity_record;
  MetadataObject record;
  TargetAnalysisEstimate estimate;
};
class ResolvedTargetAnalysis {
public:
  const ResolvedTarget& target() const;
  const TargetAnalysisOptions& options() const;
  const TargetAnalysisLimits& limits() const;
  const TargetAnalysisEstimate& estimate() const;
  const SpatialCalibration& spatial() const;
  const ImageJIntensityCalibration& intensity() const;
  // Provenance only, not a replay instruction or a live publication capability.
  const MetadataObject& record() const;
private:
  struct State;
  std::shared_ptr<const State> state_;
  explicit ResolvedTargetAnalysis(std::shared_ptr<const State>);
  friend ResolvedTargetAnalysis resolve_target_analysis(const ResolvedTarget&,
      const TargetAnalysisOptions&, const TargetAnalysisLimits&);
  friend TargetAnalysisResult execute_target_analysis(const ResolvedTargetAnalysis&,
      const ExecutionControl&, std::size_t);
};
// Metadata-only, callback-free admission. Scalar finite-policy UInt8/UInt16/
// Float32, plane execution, in-place/read-only target. Statistics use area ROI
// membership; profile-only targets may carry non-area selection dependencies.
// Includes serial source/mask/raster, one decoded lookup, kernel scratch,
// records, all retained rows, and an additional simultaneous plot copy.
// Source/cache/history, allocator overhead, Python getters/copies and RSS are
// external. No source plane is acquired or cached by preparation.
TargetAnalysisEstimate estimate_target_analysis(const ResolvedTarget&,
    const TargetAnalysisOptions& = {}, const TargetAnalysisLimits& = {});
ResolvedTargetAnalysis resolve_target_analysis(const ResolvedTarget&,
    const TargetAnalysisOptions& = {}, const TargetAnalysisLimits& = {});
// The frozen target is never recomputed from current navigation/global state.
// Pre-cancel/cap rejection has zero reads/callbacks. Results are detached and
// never edit documents/history. Before UI adoption, the owner must validate
// command.target() through Application::validate_target (or validate_source for
// a detached target) and stage its existing atomic UI publication protocol.
TargetAnalysisResult execute_target_analysis(const ResolvedTargetAnalysis&,
    const ExecutionControl& = {},
    std::size_t runtime_max_working_bytes = std::numeric_limits<std::size_t>::max());
}
