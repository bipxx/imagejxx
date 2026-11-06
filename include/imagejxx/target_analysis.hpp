#pragma once
#include <imagejxx/calibrated_analysis.hpp>
#include <imagejxx/execution.hpp>

namespace imagejxx {
enum class TargetMeasurementMode { Area, Selection, RoiManager };
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
  // Explicit Analyzer selection/manager modes require statistics and no
  // independent profile options. Area preserves the original API semantics.
  TargetMeasurementMode measurement = TargetMeasurementMode::Area;
  // Manager mode: empty means every stored row; otherwise ordered unique IDs.
  std::vector<std::size_t> roi_indices;
  bool reflex_angle = false;
  // Explicit ordinary-area Circ./AR/Round./Solidity request; old scalar defaults stay unchanged.
  bool shape_descriptors = false;
  // Explicit ordinary-area five-column Feret request; frozen during preparation.
  bool feret = false;
};
struct TargetAnalysisLimits {
  std::size_t max_pixels = 128ULL * 1024 * 1024;
  std::size_t max_working_bytes = 128ULL * 1024 * 1024;
  std::size_t max_record_bytes = 16ULL * 1024 * 1024;
  std::size_t max_measurement_rows = 100000;
  std::size_t max_measurement_work = 32ULL*1024*1024;
};
struct TargetAnalysisEstimate {
  std::size_t plane_count=0, plane_pixels=0, total_pixels=0,
      working_bytes=0, plane_bytes=0, mask_bytes=0, geometry_bytes=0,
      record_bytes=0, calibration_bytes=0, kernel_bytes=0,
      result_bytes=0, plot_bytes=0, profile_samples=0, row_count=0;
};
struct TargetAnalysisPlane {
  PlanePosition position;
  std::optional<ImageJScalarStatistics> statistics;
  std::vector<double> profile_values;
  // Area: tight mask membership independent of density NaN exclusions.
  // Lines/polylines/angles: ImageJ open-selection drawing bounds.
  // nullopt denotes an empty area mask or profile-only row.
  std::optional<std::array<int,4>> bounds;
  std::optional<std::size_t> roi_index;
  // Point/multipoint rows expand in vertex order. Zero-based within the ROI.
  std::optional<std::size_t> point_index;
  std::optional<std::array<double,3>> point_world;
  std::optional<unsigned> point_counter;
  // Present only when this parent uses more than one distinct counter.
  std::optional<std::size_t> point_counter_count;
  std::optional<double> length, angle_degrees;
  // Qualified polygon/fitted, line and rectangle/ellipse perimeter. Other
  // analytic/mask/path variants remain absent. Float TRACED may be NaN.
  std::optional<double> perimeter;
  std::optional<std::array<double,3>> midpoint_world;
  // With perimeter present: true means unscaled image pixels (Analyzer's
  // no-ROI fallback); false uses TargetAnalysisResult::spatial.unit.
  bool perimeter_in_pixels=false;
  // Area-only geometric centroid: mean pixel centers (x+.5,y+.5), captured
  // plane z, transformed by spatial. Empty membership has NaN components.
  std::optional<std::array<double,3>> centroid_world;
  // Area-only intensity-weighted summary, at captured plane z. NaN/Inf moment
  // components propagate through nonzero spatial axes, not geometry APIs.
  std::optional<std::array<double,3>> center_of_mass_world;
  // Present together for an explicit shape_descriptors request.
  std::optional<double> circularity,aspect_ratio,roundness,solidity;
  // Present together only for feret. Angle uses the local calibrated image plane.
  std::optional<double> feret,min_feret,feret_angle,feret_x,feret_y;
  // Analyzer whole-image fallback uses pixels; generic endpoints are raw image
  // coordinates, while OvalRoi endpoints use spacing-scaled image coordinates.
  bool feret_in_pixels=false,feret_coordinates_scaled=false;
};
// Detached results retain no source/provider or application. Distances are
// Profile/calibrated lengths use the captured spatial affine; flagged whole-image
// perimeters use unscaled pixels.
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
// Metadata-only manager target. Unset ROI axes use the captured source view;
// output row order follows indices (or stored order), not grouped read order.
// The returned whole-plane guard depends on pixels/metadata (including ROIs),
// not the unrelated active selection. Resolve through Application when bound.
TargetRequest roi_measurement_target_request(const DocumentSnapshot&,
    const std::vector<std::size_t>& indices = {}, std::size_t max_rows = 100000);
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
