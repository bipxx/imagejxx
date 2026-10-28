#pragma once

#include <atomic>
#include <limits>
#include <imagejxx/operations.hpp>
#include <imagejxx/targets.hpp>
#include <string_view>

namespace imagejxx {
class Document;
struct ImageJProjectionDocumentRequest;
struct ImageJIntensityFitRequest;
class ResolvedZSubsetCommand;
class ResolvedRgbDatasetCommand;
class ResolvedColorStackCommand;
class ResolvedIndexedDatasetCommand;
class ResolvedIndexedRgbCommand;
enum class DatasetPolicy { PreserveUntargeted, LegacyDesktop };
enum class GeometryPolicy { RequireTransform, LegacyDesktop };
enum class ExecutionPhase { Apply, Preview };
struct ExecutionOptions {
  DatasetPolicy dataset = DatasetPolicy::PreserveUntargeted;
  GeometryPolicy geometry = GeometryPolicy::RequireTransform;
  // Clear threshold presentation in the same atomic commit as the pixels,
  // while retaining any concurrent camera/LUT changes.
  bool reset_manual_threshold = false;
  // Preview proposals are transient and cannot be committed or published.
  ExecutionPhase phase = ExecutionPhase::Apply;
  // Additional packed/flatten/scan scratch only. Dense images, kernel work,
  // source/history and provider caches retain their existing owning budgets.
  // SIZE_MAX preserves direct-core calls without an explicit byte allowance.
  std::size_t max_target_mask_bytes = std::numeric_limits<std::size_t>::max();
};
class OperationCancelled : public std::runtime_error {
public:
  OperationCancelled() : std::runtime_error("Operation was canceled") {}
};
class CancellationToken {
public:
  void cancel() { canceled_.store(true); }
  bool canceled() const { return canceled_.load(); }

private:
  std::atomic<bool> canceled_{false};
};
struct ExecutionControl {
  std::shared_ptr<CancellationToken> cancellation;
  // Called outside application/document locks; exceptions abort preparation.
  // Native work called from a callback retains cancellation checks but does
  // not recursively dispatch this execution scope's callbacks. Explicit child
  // execution scopes can supply and run their own callbacks.
  // Plane checkpoints do not claim cancellation inside a long plane kernel.
  std::function<void(std::size_t completed, std::size_t total)> progress;
  std::function<void()> checkpoint;
  // Stage units are separate from plane progress; the name is borrowed only
  // during this callback. Exceptions or cancellation abort preparation.
  std::function<void(std::string_view name, std::size_t completed,
                     std::size_t total)>
      stage_progress;
};
// Exact integer source plane. Coordinates and scientific T/C indices are
// zero-based; RGB/RGBA sample components remain together on S.
struct AxisPlaneExportRequest {
  Plane plane = Plane::XY;
  int coordinate = 0, t = 0, c = 0;
  // Conservative core output/strip/fallback-plane estimate. Custom reader
  // caches and other process allocations are outside this bound.
  std::size_t max_working_bytes = 512ULL * 1024 * 1024;
};
struct AxisPlaneExportResult {
  Dataset dataset;
  PlaneGeometry source_geometry;
  AxisPlaneExportRequest request;
  std::size_t working_bytes;
};
AxisPlaneExportResult export_axis_plane(
    const Dataset &source, const AxisPlaneExportRequest &request = {},
    const ExecutionControl &control = {});
// Inclusive positive integer range on the plane's fixed source axis. The
// emitted sequence is first + k*step <= last; T/C select one scientific volume.
struct AxisRangeResliceRequest {
  Plane plane = Plane::XZ;
  int first = 0, last = 0, step = 1, t = 0, c = 0;
  // Complete double-backed output plus one XY fallback and orthogonal strip;
  // custom reader caches and other process allocations remain outside the cap.
  std::size_t max_working_bytes = 512ULL * 1024 * 1024;
};
struct AxisRangeResliceResult {
  Dataset dataset;
  PlaneGeometry first_plane_geometry;
  AxisRangeResliceRequest request;
  std::size_t working_bytes;
};
AxisRangeResliceResult reslice_axis_range(
    const Dataset &source, const AxisRangeResliceRequest &request = {},
    const ExecutionControl &control = {});
// Shared metadata-only descriptor used by exact extraction and its captured
// command records. No provider, pixel allocation/read or user callback is used.
struct AxisExtractionLayout {
  DatasetShape shape;
  SpatialCalibration calibration;
  PlaneGeometry geometry;
  std::array<int, 3> source_axes;
  std::string plane_name;
  std::size_t working_bytes;
};
AxisExtractionLayout describe_axis_plane_export(
    DatasetShape shape, PixelType type, const SpatialCalibration &calibration,
    const AxisPlaneExportRequest &request = {});
AxisExtractionLayout describe_axis_plane_export(
    const Dataset &source, const AxisPlaneExportRequest &request = {});
AxisExtractionLayout describe_axis_range_reslice(
    DatasetShape shape, PixelType type, const SpatialCalibration &calibration,
    const AxisRangeResliceRequest &request = {});
AxisExtractionLayout describe_axis_range_reslice(
    const Dataset &source, const AxisRangeResliceRequest &request = {});
// Flip the original vertical axis before swapping output axes. Transpose
// matches ImageJ's Rotate90 output flag; it adds no further sign or rotation.
struct AxisRangeOrientationRequest {
  AxisRangeResliceRequest range{};
  bool reverse_order = false, flip_vertical = false, transpose = false;
};
// The exact integer orientation descriptor uses the same checked calculation
// as execution. It reads metadata only: no provider, pixels or user callback.
// source_axes remains the base horizontal/vertical/fixed permutation; the
// geometry and calibration carry the final transpose and signed directions.
AxisExtractionLayout describe_axis_range_orientation(
    DatasetShape shape, PixelType type, const SpatialCalibration &calibration,
    const AxisRangeOrientationRequest &request = {});
AxisExtractionLayout describe_axis_range_orientation(
    const Dataset &source, const AxisRangeOrientationRequest &request = {});
struct AxisRangeOrientationResult {
  Dataset dataset;
  PlaneGeometry first_plane_geometry;
  AxisRangeOrientationRequest request;
  std::size_t working_bytes;
};
AxisRangeOrientationResult reslice_oriented_axis_range(
    const Dataset &source, const AxisRangeOrientationRequest &request = {},
    const ExecutionControl &control = {});
// Owned endpoint-anchored physical Z grid in XZ/YZ before transpose. This
// policy is distinct from ImageJ resize and does not change integer exports.
enum class AxisRangeSampling { Nearest, Linear };
struct AxisRangeSpacingRequest {
  AxisRangeOrientationRequest orientation{};
  double physical_vertical_spacing = 1.0;
  AxisRangeSampling sampling = AxisRangeSampling::Nearest;
};
struct AxisRangeSpacingEstimate {
  int width, height, plane_count, samples, vertical_count;
  int last_selected, effective_first, effective_last;
  double source_vertical_spacing, voxel_step;
  std::array<std::string, 3> output_axes;
  std::size_t working_bytes;
};
struct AxisRangeSpacingResult {
  Dataset dataset;
  PlaneGeometry first_plane_geometry;
  AxisRangeSpacingRequest request;
  std::size_t working_bytes;
  double source_vertical_spacing, voxel_step;
  int vertical_count;
};
// Metadata only: no provider read, callback or pixel allocation. The same
// checked layout admits execution; frontend texture limits remain separate.
AxisRangeSpacingEstimate estimate_axis_range_spacing(
    const Dataset &source, const AxisRangeSpacingRequest &request = {});
AxisRangeSpacingResult reslice_spaced_axis_range(
    const Dataset &source, const AxisRangeSpacingRequest &request = {},
    const ExecutionControl &control = {});
// Finite canonical scalar ImageJ Slicer/processor bilinear sampling. The
// nominal native affine and separate positive scalar metric are distinct;
// this request does not change the existing physical-spacing policy.
struct AxisRangeSlicerResizeRequest {
  AxisRangeOrientationRequest orientation{};
};
enum class AxisRangeSlicerResizeBranch { NoResize, EqualDimensionCrop, Bilinear };
struct AxisRangeSlicerResizeEstimate {
  int width, height, plane_count, samples;
  int source_vertical_count, vertical_count;
  int last_selected, effective_first, effective_last;
  double z_spacing_ratio, processor_vertical_origin, processor_vertical_step;
  AxisRangeSlicerResizeBranch resize_branch;
  std::array<std::string, 3> output_axes;
  std::array<double, 3> scalar_spacing;
  std::size_t working_bytes;
};
struct AxisRangeSlicerResizeResult {
  Dataset dataset;
  PlaneGeometry first_plane_geometry;
  AxisRangeSlicerResizeRequest request;
  std::size_t working_bytes;
  int source_vertical_count, vertical_count;
  double z_spacing_ratio, processor_vertical_origin, processor_vertical_step;
  AxisRangeSlicerResizeBranch resize_branch;
  std::array<double, 3> scalar_spacing;
};
// Metadata-only admission: canonicality is checked only on selected samples
// actually read by execution. Frontend texture limits remain separate.
AxisRangeSlicerResizeEstimate estimate_axis_range_slicer_resize(
    const Dataset &source, const AxisRangeSlicerResizeRequest &request = {});
AxisRangeSlicerResizeResult reslice_slicer_axis_range(
    const Dataset &source, const AxisRangeSlicerResizeRequest &request = {},
    const ExecutionControl &control = {});
struct PlaneExecutionOutput {
  PlanePosition position;
  std::optional<int> label_count;
  bool label_image = false, skipped = false;
};
struct PublishedOperation {
  std::uint64_t document_id;
  std::shared_ptr<Document> document;
  bool created;
  std::vector<PlaneExecutionOutput> outputs;
};
class Application;
// Copies share one publication state. Preparation is isolated; preview and
// output records stay readable after publication/close. Application::publish
// consumes the result only after successful atomic publication.
class PreparedOperation {
public:
  const ResolvedTarget &target() const;
  const std::string &operation() const;
  const OperationParameters &parameters() const;
  const std::vector<PlaneExecutionOutput> &outputs() const;
  const DocumentSnapshot &preview() const;
  ExecutionOptions options() const;
  bool active() const;
  DocumentTransaction begin_edit() const;
  // Fresh axis exports and scientific projections expose their output owner so a
  // frontend can stage its facade/tab before atomic publication. Rank returns
  // its candidate only after stage_new_document(); other operations return null.
  // Data edits to this candidate invalidate publication;
  // Display and supported history-policy configuration remain permitted.
  std::shared_ptr<Document> prepared_new_document() const;
  // Opt-in rank NewDocument staging. Builds the ordinary clone-and-edit output
  // once, without application membership, so a frontend can attach its facade
  // before publication. Repeated calls return that exact owner; an omitted
  // title accepts the already frozen title, while a different explicit title
  // refuses. Data edits, undo/redo and save changes invalidate publication.
  // Display and history-policy configuration, including removal of oldest
  // frozen history entries by the configured budget, remain permitted.
  std::shared_ptr<Document>
  stage_new_document(std::optional<std::string> title = std::nullopt);

private:
  friend class Application;
  friend PreparedOperation prepare_z_subset(const ResolvedZSubsetCommand &,const ExecutionControl &);
  friend PreparedOperation prepare_operation(const std::string &,
                                             const ResolvedTarget &,
                                             const OperationParameters &,
                                             ExecutionOptions,
                                             const ExecutionControl &);
  friend PreparedOperation prepare_imagej_intensity_fit(
      const DocumentSnapshot &, const ImageJIntensityFitRequest &, const ExecutionControl &);
  friend PreparedOperation prepare_imagej_projection(
      const DocumentSnapshot &, const ImageJProjectionDocumentRequest &,
      const ExecutionControl &);
  friend PreparedOperation prepare_axis_plane_export(
      const DocumentSnapshot &, const AxisPlaneExportRequest &,
      const ExecutionControl &);
  friend PreparedOperation prepare_axis_range_reslice(
      const DocumentSnapshot &, const AxisRangeResliceRequest &,
      const ExecutionControl &);
  friend PreparedOperation prepare_axis_range_orientation(
      const DocumentSnapshot &, const AxisRangeOrientationRequest &,
      const ExecutionControl &);
  friend PreparedOperation prepare_axis_range_spacing(
      const DocumentSnapshot &, const AxisRangeSpacingRequest &,
      const ExecutionControl &);
  friend PreparedOperation prepare_axis_range_slicer_resize(
      const DocumentSnapshot &, const AxisRangeSlicerResizeRequest &,
      const ExecutionControl &);
  struct State;
  friend PreparedOperation prepare_rgb_dataset(const ResolvedRgbDatasetCommand &,
                                               const ExecutionControl &, std::size_t);
  friend PreparedOperation prepare_color_stack(const ResolvedColorStackCommand &,
                                               const ExecutionControl &, std::size_t);
  friend PreparedOperation prepare_indexed_dataset(const ResolvedIndexedDatasetCommand &,const ExecutionControl &,std::size_t);
  friend PreparedOperation prepare_indexed_rgb(const ResolvedIndexedRgbCommand &,const ExecutionControl &,std::size_t);
  explicit PreparedOperation(std::shared_ptr<State> state);
  void retain_resources(std::shared_ptr<const void> guard);
  PublishedOperation publish(Application &application,
                             std::optional<std::string> title);
  std::shared_ptr<State> state_;
};
PreparedOperation prepare_operation(const std::string &operation,
                                    const ResolvedTarget &target,
                                    const OperationParameters &parameters = {},
                                    ExecutionOptions options = {},
                                    const ExecutionControl &control = {});
PreparedOperation prepare_axis_plane_export(
    const DocumentSnapshot &source,
    const AxisPlaneExportRequest &request = {},
    const ExecutionControl &control = {});
PreparedOperation prepare_axis_range_reslice(
    const DocumentSnapshot &source,
    const AxisRangeResliceRequest &request = {},
    const ExecutionControl &control = {});
PreparedOperation prepare_axis_range_orientation(
    const DocumentSnapshot &source,
    const AxisRangeOrientationRequest &request = {},
    const ExecutionControl &control = {});
PreparedOperation prepare_axis_range_spacing(
    const DocumentSnapshot &source,
    const AxisRangeSpacingRequest &request = {},
    const ExecutionControl &control = {});
PreparedOperation prepare_axis_range_slicer_resize(
    const DocumentSnapshot &source,
    const AxisRangeSlicerResizeRequest &request = {},
    const ExecutionControl &control = {});
} // namespace imagejxx
