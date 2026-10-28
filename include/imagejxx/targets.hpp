#pragma once

#include <imagejxx/document.hpp>
#include <imagejxx/global_calibration.hpp>

namespace imagejxx {
class Application;
namespace detail { struct TargetCalibrationBinding; }

// Half-open voxel-index range [start, stop), with a positive sampling step.
// An omitted stop resolves to the axis extent. Omitted request ranges use the
// captured display position, rather than silently selecting another axis.
struct AxisRange {
  int start = 0;
  std::optional<int> stop;
  int step = 1;
};
enum class TargetScope { CurrentPlane, AllZ, Ranges, Positions };
enum class RoiPolicy { WholePlane, ActiveSelection, PositionedRois };
enum class ExecutionMode { Planes, Volume };
enum class OutputDestination { InPlace, NewDocument };
struct TargetRequest {
  TargetScope scope = TargetScope::CurrentPlane;
  std::optional<AxisRange> t, c, z;
  std::vector<PlanePosition> positions;
  RoiPolicy roi = RoiPolicy::ActiveSelection;
  ExecutionMode mode = ExecutionMode::Planes;
  OutputDestination destination = OutputDestination::InPlace;
};

// A retained source and immutable, fully resolved target. Ranges stay compact,
// including on virtual sources. Explicit positions preserve caller order and
// reject duplicates. Resolution and serialization never request pixel planes.
class ResolvedTarget {
public:
  const DocumentSnapshot &source() const { return source_; }
  std::uint64_t document_id() const { return source_.id(); }
  Revisions source_revisions() const { return source_.revisions(); }
  // Application captures share their generation guard across targets/jobs.
  // Detached snapshot targets retain the existing local-only contract.
  bool application_calibration_bound() const { return bool(calibration_binding_); }
  const EffectiveCalibration* calibration_context() const;
  const SpatialCalibration& calibration() const;
  ImageJIntensityCalibration intensity_calibration() const;
  TargetScope scope() const { return scope_; }
  RoiPolicy roi_policy() const { return roi_; }
  ExecutionMode execution_mode() const { return mode_; }
  OutputDestination destination() const { return destination_; }
  const std::array<AxisRange, 3> &ranges() const { return ranges_; }
  const std::vector<PlanePosition> &explicit_positions() const {
    return positions_;
  }
  std::size_t position_count() const { return count_; }
  PlanePosition position(std::size_t index) const;
  bool contains(PlanePosition position) const;
  Change dependencies() const;
  void validate_source(const DocumentSnapshot &current) const;
  void validate_operation(const std::string &operation) const;
  DocumentTransaction begin_edit() const;
  // nullopt means the whole plane. Positioned ROIs form a union for matching
  // optional T/C/Z positions; no match yields an empty mask, never a full one.
  // ActiveSelection without a selection uses the whole plane, as legacy edits.
  std::optional<Image> mask(std::size_t index) const;
  MetadataObject record() const;

private:
  friend class Application;
  std::shared_ptr<const detail::TargetCalibrationBinding> calibration_binding_;
  friend ResolvedTarget resolve_target(const DocumentSnapshot &,
                                       const TargetRequest &,
                                       const std::string &);
  ResolvedTarget(DocumentSnapshot source, TargetScope scope,
                 std::array<AxisRange, 3> ranges,
                 std::vector<PlanePosition> positions, RoiPolicy roi,
                 ExecutionMode mode, OutputDestination destination);
  DocumentSnapshot source_;
  TargetScope scope_;
  std::array<AxisRange, 3> ranges_;
  std::vector<PlanePosition> positions_;
  RoiPolicy roi_;
  ExecutionMode mode_;
  OutputDestination destination_;
  std::size_t count_;
};

ResolvedTarget resolve_target(const DocumentSnapshot &source,
                              const TargetRequest &request = {},
                              const std::string &operation = {});
// The legacy all_slices flag means all Z at the captured T/C, not all T/C.
ResolvedTarget
resolve_legacy_target(const DocumentSnapshot &source, bool all_slices = false,
                      std::optional<int> slice_index = std::nullopt,
                      const std::string &operation = {});
// Recorded targets are bound to their source ID/data revisions and shape.
// Display-only navigation may differ; frozen positions are never recomputed.
// Cross-document replay requires an explicit new request, not an implicit
// rebind.
ResolvedTarget target_from_record(const DocumentSnapshot &source,
                                  const MetadataObject &record,
                                  const std::string &operation = {});

} // namespace imagejxx
