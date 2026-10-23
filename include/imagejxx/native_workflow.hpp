#pragma once
#include <imagejxx/execution.hpp>
#include <imagejxx/gaussian_command.hpp>
#include <imagejxx/imagej_annotation_export.hpp>
#include <imagejxx/rank_command.hpp>
#include <imagejxx/histogram_command.hpp>
#include <imagejxx/selection_creation.hpp>

namespace imagejxx {
namespace detail {
struct NativeWorkflowAccess;
}
// Explicit processing intent. A captured Gaussian/rank command requires its
// complete original record, empty registry parameters and an explicit target.
// Optional typed filter_options author new raw intent without replacing
// history. An untyped registry capture may instead be retained as historical
// evidence; explicit parameters/target carry the new intent. Import/export
// The rgb_dataset operation requires empty parameters, a complete immutable
// RGB capture in original, and explicit complete-TCZ WholePlane InPlace ranges.
// rgb_contrast_stack requires its full saved capture, empty parameters and one
// explicit ActiveSelection position identifying the active processor. It maps
// the entire TCZ stack, rebinds destination selection and preserves navigation.
// Each subsequent step resolves against the preceding stage. Import/export
// originals accept their complete legacy geometric or heterogeneous captured
// records.
using NativeWorkflowFilterOptions =
    std::variant<GaussianBlurCommandOptions, RankFilterCommandOptions>;
struct NativeWorkflowOperation {
  std::string operation;
  OperationParameters parameters;
  TargetRequest target;
  std::optional<MetadataObject> original;
  bool use_destination_calibration = false;
  // Explicit raw filter intent, independent of the immutable original capture.
  // Requires empty registry parameters and a matching Gaussian/rank operation.
  std::optional<NativeWorkflowFilterOptions> filter_options;
  bool enabled = true;
  // Authored area intent for selection_creation, independent of immutable
  // original provenance. Without it, retain the original command's choices.
  std::optional<SelectionCreationRequest> selection_creation;
};
struct NativeWorkflowImport {
  std::vector<ImageJAnnotationImportItem> items;
  ImageJRoiImportMode mode = ImageJRoiImportMode::Append;
  ImageJAnnotationImportLimits limits;
  std::optional<MetadataObject> original;
  bool enabled = true;
};
struct NativeWorkflowExport {
  // Unique flat filename/token, not permission to write a filesystem path.
  std::string name;
  std::vector<ImageJAnnotationExportItem> items;
  ImageJAnnotationExportLimits limits;
  bool zip = true, compress = true;
  std::optional<MetadataObject> original;
  bool enabled = true;
};
struct NativeWorkflowHistogramOptions {
  TargetHistogramOptions histogram;
  bool log_counts = false;
};
// Read-only analysis. Absent authored_options retains the original choices;
// the explicit target is resolved against the preceding workflow stage.
// Name is a unique flat output token, shared with annotation exports.
struct NativeWorkflowHistogram {
  std::string name;
  TargetRequest target;
  std::optional<MetadataObject> original;
  HistogramCommandLimits limits;
  bool enabled = true;
  // New explicit choices, independent of optional historical evidence.
  // Required when there is no original capture. Never mutates the original.
  std::optional<NativeWorkflowHistogramOptions> authored_options;
};
using NativeWorkflowStep =
    std::variant<NativeWorkflowOperation, NativeWorkflowImport,
                 NativeWorkflowExport, NativeWorkflowHistogram>;
struct NativeWorkflowLimits {
  std::size_t max_steps = 64;
  // Conservative cumulative datasets, metadata, inputs and export bytes.
  // Includes complete output backing per processing step even when later freed.
  // Phase-local codec/kernel scratch and custom reader caches are separate.
  std::size_t max_retained_bytes = 512ULL * 1024 * 1024;
};
struct NativeWorkflowOutput {
  std::size_t step_index;
  std::string name;
  ResolvedImageJAnnotationExport value;
};
struct NativeWorkflowHistogramOutput {
  std::size_t step_index;
  std::string name;
  MetadataObject record;
  TargetHistogramResult value;
};
class PreparedNativeWorkflow {
public:
  const DocumentSnapshot &source() const { return source_; }
  const DocumentSnapshot &preview() const { return preview_; }
  const std::vector<NativeWorkflowOutput> &outputs() const { return outputs_; }
  // Detached analyses contain no source/provider references.
  const std::vector<NativeWorkflowHistogramOutput> &histograms() const { return histograms_; }
  // Only enabled, executed steps. Output indices refer to the retained order.
  std::size_t completed_steps() const { return completed_steps_; }
  std::size_t retained_bytes() const { return retained_bytes_; }
  Change dependencies() const { return (paired_display_ || guard_display_) ? Change::All : Change::Data; }
  // One guarded transaction. Conversion pairs display with pixel history and
  // requires every captured revision; other steps retain fresh display state.
  // Commit through the source owner;
  // no live mutation, external write, or workflow recording occurs here.
  DocumentTransaction begin_edit() const;

private:
  friend struct detail::NativeWorkflowAccess;
  friend PreparedNativeWorkflow prepare_native_workflow(
      const DocumentSnapshot &, const std::vector<NativeWorkflowStep> &,
      const NativeWorkflowLimits &, const ExecutionControl &);
  PreparedNativeWorkflow(DocumentSnapshot source, DocumentSnapshot preview,
                         std::vector<NativeWorkflowOutput> outputs,
                         std::vector<NativeWorkflowHistogramOutput> histograms,
                         std::size_t completed, std::size_t bytes, bool paired_display, bool guard_display)
      : source_(std::move(source)), preview_(std::move(preview)),
        outputs_(std::move(outputs)), histograms_(std::move(histograms)), completed_steps_(completed),
        retained_bytes_(bytes), paired_display_(paired_display), guard_display_(guard_display) {}
  DocumentSnapshot source_, preview_;
  std::vector<NativeWorkflowOutput> outputs_;
  std::vector<NativeWorkflowHistogramOutput> histograms_;
  std::size_t completed_steps_ = 0, retained_bytes_ = 0;
  bool paired_display_ = false, guard_display_ = false;
};
// Preflight the entire request, then execute in order on a detached scientific
// owner with at most one internal undo entry. Each later step sees earlier
// results. Export captures that exact intermediate state. Unsupported
// geometry/destination and malformed original records are refused before any
// processing pixel reads. Disabled steps retain and validate portable intent
// but do not resolve destination compatibility or produce workflow outputs.
// This API is path-free; hosts must separately
// plan/atomically publish outputs.
PreparedNativeWorkflow prepare_native_workflow(
    const DocumentSnapshot &, const std::vector<NativeWorkflowStep> &,
    const NativeWorkflowLimits & = {}, const ExecutionControl & = {});
} // namespace imagejxx
