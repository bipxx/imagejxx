#pragma once

#include <imagejxx/channel_display_command.hpp>
#include <imagejxx/dataset_histogram.hpp>

namespace imagejxx {
namespace detail { struct AutoContrastStorage; }

enum class AutoContrastScope { ActiveChannel, CompositeAll };
enum class AutoContrastRoiPolicy { ActiveSelection, WholePlane };
enum class AutoContrastOutcome { HistogramBounds, AreaExtrema, ResetFallback };

struct AutoContrastRequest {
  int previous_divisor = 0;
  AutoContrastScope scope = AutoContrastScope::ActiveChannel;
  AutoContrastRoiPolicy roi = AutoContrastRoiPolicy::ActiveSelection;
  // Capture Settings.conversion_unsigned16_range at the consumer boundary.
  // 0 is observed whole-plane extrema; fixed choices are 8/10/12/14/15/16.
  int unsigned16_bits = 0;
};
struct AutoContrastLimits {
  std::size_t max_pixels = 16ULL * 1024 * 1024;
  std::size_t max_working_bytes = 128ULL * 1024 * 1024;
  std::size_t max_record_bytes = 16ULL * 1024 * 1024;
  std::size_t max_selection_vertices = 65536;
};
struct AutoContrastCycle { int previous = 0, used = 5000, next = 5000; };
struct AutoContrastHistogramSummary {
  double origin = 0, bin_size = 1, minimum = 0, maximum = 0;
  std::uint64_t visited_count = 0, finite_count = 0, nan_count = 0;
  double plane_minimum = 0, plane_maximum = 0;
  std::uint64_t plane_finite_count = 0, plane_nan_count = 0;
};
struct AutoContrastEstimate {
  DatasetHistogramEstimate histogram;
  std::size_t selection_bytes = 0, display_bytes = 0, record_bytes = 0,
              retained_bytes = 0, working_bytes = 0;
};

class ResolvedAutoContrastCommand {
  struct Data {
    ResolvedChannelDisplayCommand effect_;
    AutoContrastRequest request_;
    PlanePosition position_;
    std::optional<Selection> selection_;
    std::uint64_t selectionRevision_, displayRevision_;
    AutoContrastCycle cycle_;
    AutoContrastHistogramSummary histogram_;
    AutoContrastOutcome outcome_;
    std::optional<std::array<int, 2>> indices_;
    std::optional<std::array<double, 2>> candidate_;
    std::vector<int> channels_;
    std::array<double, 2> resolved_, applied_;
    MetadataObject record_;
    AutoContrastEstimate estimate_;
    bool fresh_;
  };
public:
  ResolvedAutoContrastCommand(const ResolvedAutoContrastCommand &) = default;
  ResolvedAutoContrastCommand(ResolvedAutoContrastCommand &&) noexcept = default;
  ResolvedAutoContrastCommand &operator=(ResolvedAutoContrastCommand other) noexcept {
    data_.swap(other.data_);
    resource_guard_.swap(other.resource_guard_);
    // The old payload retires with other, before its old reservation lease.
    return *this;
  }
  const DocumentSnapshot &source() const { return data_->effect_.source(); }
  const AutoContrastRequest &request() const { return data_->request_; }
  PlanePosition position() const { return data_->position_; }
  const std::optional<Selection> &selection_provenance() const { return data_->selection_; }
  std::uint64_t selection_dependency_revision() const { return data_->selectionRevision_; }
  std::uint64_t display_dependency_revision() const { return data_->displayRevision_; }
  const ResolvedChannelDisplayCommand &effect() const { return data_->effect_; }
  const MetadataObject &record() const { return data_->record_; }
  const AutoContrastCycle &cycle() const { return data_->cycle_; }
  const AutoContrastHistogramSummary &histogram() const { return data_->histogram_; }
  AutoContrastOutcome outcome() const { return data_->outcome_; }
  const std::optional<std::array<int, 2>> &indices() const { return data_->indices_; }
  const std::optional<std::array<double, 2>> &candidate_range() const { return data_->candidate_; }
  const std::vector<int> &channels() const { return data_->channels_; }
  const std::array<double, 2> &resolved_range() const { return data_->resolved_; }
  const std::array<double, 2> &applied_range() const { return data_->applied_; }
  const AutoContrastEstimate &estimate() const { return data_->estimate_; }
  bool requires_captured_state() const { return data_->fresh_; }

private:
  friend class Application;
  friend struct detail::AutoContrastStorage;
  friend ResolvedAutoContrastCommand resolve_auto_contrast_command(
      const DocumentSnapshot &, const AutoContrastRequest &,
      const AutoContrastLimits &, const ExecutionControl &);
  friend ResolvedAutoContrastCommand auto_contrast_command_from_record(
      const DocumentSnapshot &, const MetadataObject &, const AutoContrastLimits &);
  ResolvedAutoContrastCommand(ResolvedChannelDisplayCommand, AutoContrastRequest,
      PlanePosition, std::optional<Selection>, std::uint64_t, std::uint64_t,
      AutoContrastCycle, AutoContrastHistogramSummary, AutoContrastOutcome,
      std::optional<std::array<int, 2>>, std::optional<std::array<double, 2>>,
      std::vector<int>, std::array<double, 2>, std::array<double, 2>,
      MetadataObject, AutoContrastEstimate, bool);
  // Retained with command copies/escaped result aliases; Application alone
  // attaches the scheduler reservation. This is not record/session state.
  // Declaration order keeps the lease until after the charged payload dies.
  std::shared_ptr<const void> resource_guard_;
  // Immutable payload keeps copies cheap and moves non-throwing even on
  // standard libraries whose map move allocates a replacement sentinel.
  std::shared_ptr<const Data> data_;
};

// Metadata/geometry admission only: no pixels, rasterization or callbacks.
// Bounds cover introspectable native captures/records/scratch, not allocator
// overhead, opaque callable targets, provider caches/history/GPU/process RSS or
// later Python/getter copies. Immutable metadata occurrences are charged even
// when shared. The bounded ROI request copy is made only after borrowed preflight.
AutoContrastEstimate estimate_auto_contrast_command(
    const DocumentSnapshot &, const AutoContrastRequest & = {},
    const AutoContrastLimits & = {});
// Raw scalar U8/U16/F32 only. Capture actual source.display TCZ and active ROI;
// no threshold/density weighting or inference of signed calibration metadata.
// One plane acquisition, including whole-plane fallback extrema. Empty/nonfinite
// area refuses. Configured unsigned16 fallback updates every stored C in all
// modes; CompositeAll otherwise copies getter-rounded range in Composite only.
ResolvedAutoContrastCommand resolve_auto_contrast_command(
    const DocumentSnapshot &, const AutoContrastRequest & = {},
    const AutoContrastLimits & = {}, const ExecutionControl & = {});
void validate_auto_contrast_command_record(
    const MetadataObject &, const AutoContrastLimits & = {});
// Validate the entire original record before binding; replay uses its nested
// Set effect without reads, callbacks, histogram/reset/Enhance or cycle changes.
// Historical Selection/Display provenance is retained, not a replay dependency.
ResolvedAutoContrastCommand auto_contrast_command_from_record(
    const DocumentSnapshot &, const MetadataObject &,
    const AutoContrastLimits & = {});
// Every replacement request value is explicit. Validate the entire original
// before inspecting the destination or resolving/recomputing any replacement.
ResolvedAutoContrastCommand rebind_auto_contrast_command(
    const DocumentSnapshot &, const MetadataObject &original_record,
    const AutoContrastRequest &complete_request,
    const AutoContrastLimits & = {}, const ExecutionControl & = {});
// Proposed display only. Fresh commands require exact owner/all four revisions;
// replay retains the existing Set Pixels/Metadata binding and fresh cameras/TCZ.
// Root's controller must additionally arbitrate active owner/cycle epoch and
// display/record/cycle publication atomically; this function publishes nothing.
DisplayState command_display_state(const ResolvedAutoContrastCommand &,
                                  const DocumentSnapshot &current);

} // namespace imagejxx
