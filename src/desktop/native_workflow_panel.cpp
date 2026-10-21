#include "native_workflow_panel.hpp"
#include "../native/atomic_file.hpp"
#include "../native/imagej_roi_import_internal.hpp"
#include "../native/kernel_control.hpp"
#include "../native/native_workflow_batch_internal.hpp"
#include "../native/native_workflow_internal.hpp"
#include "../native/rgb_dataset_internal.hpp"
#include "../native/color_stack_internal.hpp"
#include "../native/indexed_dataset_internal.hpp"
#include "../native/indexed_rgb_internal.hpp"
#include "../native/native_workflow_program_internal.hpp"
#include "native_workflow_parameters.hpp"
#include "native_workflow_viewport.hpp"
#include "roi_mixed_export_panel.hpp"
#include <imagejxx/gaussian_command.hpp>
#include <imagejxx/imagej_roi_zip_export.hpp>
#include <imagejxx/native_workflow_batch_setup.hpp>
#include <imagejxx/rank_command.hpp>
#include <imgui_internal.h>
#include <imgui_stdlib.h>
#include <iterator>
#include <mutex>
#include <set>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif
namespace imagejxx::desktop {
OperationParameters nativeOperationParameters(const Json &);
namespace {
void require(bool value, const char *message) {
  if (!value)
    throw std::invalid_argument(message);
}
bool matches(const DocumentSnapshot &a, const DocumentSnapshot &b) {
  return a.same_document(b) &&
         a.revisions().matches(b.revisions(), Change::Data);
}
struct Entry {
  std::shared_ptr<const NativeWorkflowStep> step;
  std::string title, path;
  bool enabled = true;
  bool confirmed = false, calibration = false, authored = false,
       retained_target = false;
  MetadataObject review_record;
  std::size_t choice_bytes = 0;
  int scope = 0, roi = 0;
  std::array<int, 3> first{}, stop{}, stride{1, 1, 1};
  std::array<int, 3> position{};
  std::vector<PlanePosition> positions;
};
std::size_t admit_review_table(const Json &table, std::size_t cap) {
  std::size_t remaining = cap / 4, nodes = 0;
  auto add = [&](std::size_t n) {
    require(n <= remaining,
            "Workflow review exceeds the processing memory setting");
    remaining -= n;
  };
  std::function<void(const Json &, std::size_t)> visit =
      [&](const Json &v, std::size_t depth) {
        require(depth <= 128 && ++nodes <= 1048576,
                "Workflow review structure limit");
        add(64);
        if (v.is_string())
          add(v.get_ref<const std::string &>().size());
        else if (v.is_array())
          for (const auto &child : v)
            visit(child, depth + 1);
        else if (v.is_object())
          for (const auto &field : v.items()) {
            add(field.key().size());
            visit(field.value(), depth + 1);
          }
      };
  visit(table, 0);
  return cap / 4 - remaining;
}
std::vector<Entry> program_entries(const Json &record, std::size_t cap) {
  auto program = std::make_shared<const NativeWorkflowProgram>(
      native_workflow_program_from_record(
          metadataObjectFromJson(record),
          desktop_workflow_program_limits(cap)));
  std::vector<Entry> entries;
  for (const auto &step : program->steps()) {
    Entry entry;
    entry.step = std::shared_ptr<const NativeWorkflowStep>(program, &step);
    entry.authored = true;
    entry.enabled = std::visit([](const auto &v) { return v.enabled; }, step);
    if (const auto op = std::get_if<NativeWorkflowOperation>(&step)) {
      entry.title = op->operation=="rgb_dataset"?"RGB dataset conversion":op->operation=="color_stack"?"RGB / HSB component stack":op->operation=="indexed_dataset"?"Indexed color dataset":op->operation=="indexed_rgb"?"Indexed palette to RGB":operation_spec(op->operation).title;
      entry.scope = int(op->target.scope);
      entry.roi = int(op->target.roi);
      entry.calibration = op->use_destination_calibration;
      entry.positions = op->target.positions;
      entry.retained_target = true;
    } else
      entry.title = std::holds_alternative<NativeWorkflowImport>(step)
                        ? "Import annotations"
                        : "Export annotations";
    entries.push_back(std::move(entry));
  }
  return entries;
}
std::vector<Entry> compile(const Json &table, std::size_t cap) {
  require(table.is_array() && !table.empty() && table.size() <= 64,
          "Workflow requires 1..64 recorded steps");
  std::vector<Entry> entries;
  entries.reserve(table.size());
  for (const auto &raw : table) {
    require(raw.is_object(), "Workflow step must be an object");
    if (raw.value("schema", std::string{}) ==
        "imagejxx.native-workflow-program") {
      auto segment = program_entries(raw, cap);
      require(segment.size() <= 64 - entries.size(),
              "Flattened workflow exceeds64 steps");
      entries.insert(entries.end(), std::make_move_iterator(segment.begin()),
                     std::make_move_iterator(segment.end()));
      continue;
    }
    require(entries.size() < 64, "Flattened workflow exceeds64 steps");
    const auto record = metadataObjectFromJson(raw);
    const auto schema = string(get(raw, "schema"));
    NativeWorkflowStep step;
    std::string title;
    if (schema == "imagejxx.imagej-annotation-import-command") {
      auto request = decode_imagej_annotation_import_request(record);
      step = NativeWorkflowImport{std::move(request.items), request.mode,
                                  request.limits, record};
      title = "Import annotations";
    } else if (schema == "imagejxx.imagej-roi-import-command") {
      auto request = decode_imagej_roi_import_request(record);
      NativeWorkflowImport value;
      for (auto &item : request.items)
        value.items.emplace_back(std::move(item));
      value.mode = request.mode;
      value.original = record;
      value.limits.batch = request.limits;
      step = std::move(value);
      title = "Import geometric ROIs";
    } else if (schema == "imagejxx.imagej-annotation-export") {
      auto request = decode_imagej_annotation_export_request(record);
      NativeWorkflowExport value;
      value.items = std::move(request.items);
      value.limits = request.limits;
      value.zip = request.is_zip;
      value.compress = request.compress;
      value.original = record;
      step = std::move(value);
      title = "Export annotations";
    } else if (schema == "imagejxx.imagej-roi-zip-export") {
      auto request = decode_imagej_roi_zip_export_request(record);
      NativeWorkflowExport value;
      for (auto &item : request.items)
        value.items.push_back({std::move(item.name),
                               item.source_index,
                               ImageJAnnotationKind::Selection,
                               item.annotation_index,
                               std::move(item.standard_projection),
                               {}});
      value.limits.sources.batch = request.limits.sources;
      value.limits.output = request.limits.output;
      value.limits.zip = request.limits.zip;
      value.original = record;
      value.compress = request.compress;
      step = std::move(value);
      title = "Export geometric ZIP";
    } else if (schema == "imagejxx.imagej-roi-export") {
      auto request = decode_imagej_roi_export_request(record);
      const auto pairing = metadataJson(request.pairing_record);
      NativeWorkflowExport value;
      value.items.push_back({"annotation.roi",
                             pairing.at("source_index").get<std::size_t>(),
                             ImageJAnnotationKind::Selection,
                             pairing.at("annotation_index").get<std::size_t>(),
                             std::move(request.standard_projection),
                             {}});
      value.limits.sources.batch = detail::imagej_roi_import_limits_from_record(
          metadataObjectFromJson(pairing.at("limits")));
      value.zip = false;
      value.original = record;
      value.limits.output = request.limits;
      step = std::move(value);
      title = "Export geometric ROI";
    } else if(schema=="imagejxx.rgb-dataset-command" || schema=="imagejxx.color-stack-command" || schema=="imagejxx.indexed-dataset-command" || schema=="imagejxx.indexed-rgb-command") {
      const bool component=schema=="imagejxx.color-stack-command";
      const bool indexed=schema=="imagejxx.indexed-dataset-command",expand=schema=="imagejxx.indexed-rgb-command";
      if(component)validate_color_stack_command_record(record);else if(indexed)validate_indexed_dataset_command_record(record);else if(expand)validate_indexed_rgb_command_record(record);else validate_rgb_dataset_command_record(record);
      NativeWorkflowOperation value;value.operation=component?"color_stack":indexed?"indexed_dataset":expand?"indexed_rgb":"rgb_dataset";value.original=record;
      const auto shape=raw.at("source").at("shape").get<std::array<int,6>>();
      value.target.scope=TargetScope::Ranges;value.target.roi=RoiPolicy::WholePlane;
      value.target.t=AxisRange{0,shape[0],1};value.target.c=AxisRange{0,shape[1],1};value.target.z=AxisRange{0,shape[2],1};
      step=std::move(value);title=component?"RGB / HSB component stack":indexed?"Indexed color dataset":expand?"Indexed palette to RGB":"RGB dataset conversion";
    } else {
      require(schema.empty(), "Unsupported native workflow schema");
      NativeWorkflowOperation value;
      value.operation = string(get(raw, "operation"));
      if (raw.contains("schema_version")) {
        if (value.operation == "gaussian_blur_imagej")
          validate_gaussian_command_record(record);
        else if ((value.operation == "rank_filter_imagej" || (value.operation == "remove_outliers_imagej" || value.operation == "top_hat_imagej" || value.operation == "remove_nans_imagej")))
          validate_rank_command_record(record);
        else
          throw std::invalid_argument("Unsupported typed workflow operation");
        value.original = record;
      } else {
        for (const auto &field : raw.items())
          require(field.key() == "operation" || field.key() == "parameters" ||
                      field.key() == "selection" ||
                      field.key() == "all_slices" ||
                      field.key() == "slice_index" ||
                      field.key() == "dataset_policy",
                  "Unsupported workflow step field");
        if (raw.contains("dataset_policy"))
          require(value.operation == "threshold" &&
                      raw.at("dataset_policy") == "preserve_untargeted",
                  "Unsupported workflow dataset policy");
        value.parameters =
            nativeOperationParameters(raw.value("parameters", Json::object()));
        validate_operation_record(value.operation, value.parameters);
        if (raw.contains("all_slices"))
          require(raw.at("all_slices").is_boolean(),
                  "Workflow all_slices requires boolean");
        if (raw.contains("slice_index"))
          require(raw.at("slice_index").is_number_integer() &&
                      raw.at("slice_index") >= 0 &&
                      raw.at("slice_index") <= INT32_MAX,
                  "Workflow slice index requires nonnegative int32");
        if (raw.contains("selection") && !raw.at("selection").is_null() &&
            !raw.at("selection").empty())
          (void)selectionFromJson(raw.at("selection"));
      }
      require(operation_spec(value.operation).geometry ==
                  GeometryEffect::Preserve,
              "Workflow geometry requires explicit native remap support");
      if (!raw.contains("schema_version"))
        value.original = record;
      title = operation_spec(value.operation).title;
      step = std::move(value);
    }
    if (auto ex = std::get_if<NativeWorkflowExport>(&step))
      ex->name = "step-" + std::to_string(entries.size()) +
                 (ex->zip ? ".zip" : ".roi");
    Entry entry;
    entry.review_record = record;
    entry.title = std::move(title);
    entry.step = std::make_shared<const NativeWorkflowStep>(std::move(step));
    if(const auto op=std::get_if<NativeWorkflowOperation>(entry.step.get());op && (op->operation=="rgb_dataset" || op->operation=="color_stack" || op->operation=="indexed_dataset" || op->operation=="indexed_rgb")) {
      entry.scope=int(TargetScope::Ranges);entry.roi=int(RoiPolicy::WholePlane);entry.retained_target=true;
    }
    entries.push_back(std::move(entry));
  }
  return entries;
}
std::shared_ptr<const NativeWorkflowStep> effective(const Entry &e) {
  const auto op = std::get_if<NativeWorkflowOperation>(e.step.get());
  if (!op) {
    if (e.enabled ==
        std::visit([](const auto &v) { return v.enabled; }, *e.step))
      return e.step;
    auto value = *e.step;
    std::visit([&](auto &v) { v.enabled = e.enabled; }, value);
    return std::make_shared<const NativeWorkflowStep>(std::move(value));
  }
  auto v = *op;
  v.enabled = e.enabled;
  v.use_destination_calibration = e.calibration;
  if (e.retained_target)
    return std::make_shared<const NativeWorkflowStep>(std::move(v));
  v.target.t.reset();
  v.target.c.reset();
  v.target.z.reset();
  v.target.positions.clear();
  v.target.scope = TargetScope(e.scope);
  v.target.roi = RoiPolicy(e.roi);
  v.use_destination_calibration = e.calibration;
  if (e.scope == 2) {
    v.target.t = AxisRange{e.first[0], e.stop[0], e.stride[0]};
    v.target.c = AxisRange{e.first[1], e.stop[1], e.stride[1]};
    v.target.z = AxisRange{e.first[2], e.stop[2], e.stride[2]};
  }
  if (e.scope == 3)
    v.target.positions = e.positions;
  return std::make_shared<const NativeWorkflowStep>(std::move(v));
}
bool alias(const std::filesystem::path &a, const std::filesystem::path &b) {
#ifdef _WIN32
  const auto x = a.native(), y = b.native();
  return CompareStringOrdinal(x.c_str(), int(x.size()), y.c_str(),
                              int(y.size()), TRUE) == CSTR_EQUAL;
#else
  return a == b;
#endif
}
std::filesystem::path destination(const std::string &text, bool zip) {
  namespace fs = std::filesystem;
  require(!text.empty() && text.find('\0') == std::string::npos,
          "Choose every output destination");
  auto p = fs::absolute(fs::u8path(text)).lexically_normal();
  require(p.has_filename(), "Choose an output filename");
  require(extension(p.u8string()) == (zip ? ".zip" : ".roi"),
          "Output extension must match ROI/ZIP packaging");
  require(fs::is_directory(p.parent_path()),
          "Output parent must already be a directory");
  p = fs::canonical(p.parent_path()) / p.filename();
  std::error_code ec;
  auto status = fs::symlink_status(p, ec);
  if (ec && ec != std::errc::no_such_file_or_directory)
    throw fs::filesystem_error("Inspect workflow output", p, ec);
  require(status.type() == fs::file_type::not_found,
          "Workflow output already exists; choose a new file");
  return p;
}
struct File {
  std::string path;
  std::unique_ptr<detail::StagedAtomicFile> staged;
  bool published = false;
};
struct BatchProgress {
  std::mutex mutex;
  std::vector<NativeWorkflowBatchState> states;
};
const char *batch_state(NativeWorkflowBatchState s) {
  static const char *names[] = {
      "Queued",    "Validating", "Reading", "Planning", "Encoding", "Prepared",
      "Committed", "Partial",    "Failed",  "Canceled", "Skipped"};
  return names[int(s)];
}
struct Ready {
  std::optional<NativeWorkflowBatchSetup> setup;
  std::vector<Entry> setup_entries;
  std::size_t setup_table_bytes = 0;
  std::string setup_path;
  bool setup_save = false, setup_replace = false;
  std::optional<PreparedNativeWorkflow> plan;
  std::optional<PreparedNativeWorkflowBatch> batch;
  std::optional<Json> program, operation_record;
  std::optional<NativeWorkflowBatchReview> reviewed_input;
  Json input_table;
  std::shared_ptr<const NativeWorkflowProgram> verified_program;
  std::string verified_fingerprint;
  std::size_t input_index = SIZE_MAX, review_document = 0;
  std::optional<DocumentSnapshot> private_source;
  std::size_t prefix_bytes = 0, prefix_completed = 0;
  std::vector<File> files;
  bool attempted = false, preview_only = false;
  std::size_t edit_index = SIZE_MAX;
  std::uint64_t cancel_epoch = 0;
  std::shared_ptr<const void> resources;
  explicit Ready(PreparedNativeWorkflow value) : plan(std::move(value)) {}
  explicit Ready(PreparedNativeWorkflowBatch value) : batch(std::move(value)) {}
  explicit Ready(Json value) : program(std::move(value)) {}
  Ready(DocumentSnapshot source, std::size_t bytes)
      : private_source(std::move(source)), prefix_bytes(bytes) {}
};
} // namespace
NativeWorkflowProgramLimits desktop_workflow_program_limits(std::size_t cap) {
  return {64, std::min(cap / 4,
                       NativeWorkflowProgramLimits::hard_max_record_bytes)};
}
bool needs_native_workflow_review(const Json &table) {
  return table.is_array() &&
         std::any_of(table.begin(), table.end(), [](const auto &step) {
           return step.is_object() && step.contains("schema");
         });
}
void validate_legacy_batch_workflow(const Json &table) {
  require(table.is_array() && !table.empty(), "Record or load a workflow");
  for (const auto &step : table) {
    require(step.is_object(), "Workflow step must be an object");
    require(!step.contains("schema"),
            "This workflow requires native workflow review; generic batch "
            "integration is pending");
    const auto name = string(get(step, "operation"));
    if (step.contains("schema_version")) {
      auto record = metadataObjectFromJson(step);
      if (name == "gaussian_blur_imagej")
        validate_gaussian_command_record(record);
      else if ((name == "rank_filter_imagej" || (name == "remove_outliers_imagej" || name == "top_hat_imagej" || name == "remove_nans_imagej")))
        validate_rank_command_record(record);
      else
        throw std::invalid_argument("Unsupported typed workflow operation");
    } else
      validateRecord(name, step.value("parameters", Json::object()));
  }
}
struct NativeWorkflowPanel::State {
  std::shared_ptr<JobScheduler> scheduler;
  std::optional<DocumentSnapshot> source;
  Json table = Json::array(), controls = Json::object(), bounds = Json::array();
  std::vector<Entry> entries;
  bool batch = false, draft_mode = false, all_documents = false,
       batch_confirmed = false;
  int document_index = 0;
  std::string input_path, output_directory;
  std::vector<NativeWorkflowBatchInput> inputs;
  std::vector<int> review_indices;
  std::unique_ptr<NativeWorkflowPanel> input_editor;
  std::shared_ptr<Ready> setup_ready;
  std::string setup_path;
  std::string draft_label;
  std::shared_ptr<Ready> input_review, retiring_input;
  Json input_table;
  Json input_bounds = Json::array();
  std::shared_ptr<BatchProgress> progress;
  std::uint64_t browse_route = 0;

  std::shared_ptr<Ready> ready, prefix, retiring_prefix;
  std::unique_ptr<NativeWorkflowViewport> viewport;
  std::unique_ptr<NativeWorkflowParameters> parameter_editor;
  std::unique_ptr<RoiImportPanel> import_editor;
  std::unique_ptr<RoiMixedExportPanel> export_editor;
  std::size_t table_bytes = 0;
  bool choices_accepted = false;
  bool child_visible() const {
    return (input_editor && input_editor->visible()) ||
           (parameter_editor && parameter_editor->visible()) ||
           (import_editor && import_editor->visible()) ||
           (export_editor && export_editor->visible());
  }
  Json child_diagnostics() const {
    if (parameter_editor && parameter_editor->visible())
      return parameter_editor->diagnostics();
    if (import_editor && import_editor->visible())
      return import_editor->diagnostics();
    if (export_editor && export_editor->visible())
      return export_editor->diagnostics();
    return Json::object();
  }
  bool child_working() const {
    return (input_editor && input_editor->working()) ||
           (import_editor && import_editor->working()) ||
           (export_editor && export_editor->working());
  }
  std::optional<JobInfo> child_job_info() const {
    if (input_editor)
      if (auto info = input_editor->job_info())
        return info;
    if (import_editor)
      if (auto info = import_editor->job_info())
        return info;
    return export_editor ? export_editor->job_info() : std::nullopt;
  }
  std::uint64_t child_job_id() const {
    const auto info = child_job_info();
    return info ? info->id : 0;
  }
  void close_input() {
    if (input_editor)
      input_editor->cancel("batch_input_review_closed");
    if (input_review && input_editor && input_editor->working())
      retiring_input = std::move(input_review);
    else {
      input_review.reset();
      if (input_editor && !input_editor->working())
        input_editor.reset();
    }
    input_table = Json();
  }
  void close_editor() {
    close_input();
    parameter_editor.reset();
    if (import_editor)
      import_editor->cancel("workflow_draft_closed");
    if (export_editor)
      export_editor->cancel("workflow_draft_closed");
    // Canceled running children still own snapshots until their checkpoint.
    // Keep the prefix reservation through that retirement, and block new work.
    if (prefix && child_working())
      retiring_prefix = std::move(prefix);
    else
      prefix.reset();
  }
  void setup_io(std::string path, bool load, bool replace) {
    require(batch && source && !working() && !child_working() &&
                !child_visible(),
            "Choose an idle native Batch draft");
    std::vector<std::shared_ptr<const NativeWorkflowStep>> common;
    if (!load)
      for (const auto &entry : entries)
        common.push_back(effective(entry));
    auto input_rows = load ? std::vector<NativeWorkflowBatchInput>{} : inputs;
    auto directory = load ? std::string{} : output_directory;
    require(document_index >= 0, "Batch document index must be nonnegative");
    NativeWorkflowBatchSelection selection{
        all_documents, all_documents ? 0 : std::size_t(document_index)};
    invalidate();
    const auto epoch = cancel_epoch.load();
    const auto epoch_source = &cancel_epoch;
    const auto limit = cap;
    const auto snapshot = *source;
    auto result = std::make_shared<std::shared_ptr<Ready>>();
    JobOptions options;
    options.label =
        load ? "Load native Batch setup" : "Save native Batch setup";
    options.document_id = source->id();
    options.resource_bytes = cap;
    options.retain_result_resources = true;
    job = scheduler->submit(
        [path = std::move(path), load, replace, common = std::move(common),
         input_rows = std::move(input_rows), directory = std::move(directory),
         selection, epoch, epoch_source, limit, snapshot,
         result](const JobContext &context) {
          auto control = context.execution_control();
          control.checkpoint = [&context, epoch, epoch_source] {
            context.checkpoint();
            if (epoch_source->load() != epoch)
              throw OperationCancelled();
          };
          ExecutionScope scope(control);
          NativeWorkflowBatchSetupLimits limits;
          limits.max_record_bytes = std::min(
              limit / 4, NativeWorkflowProgramLimits::hard_max_record_bytes);
          auto ready = std::make_shared<Ready>(Json());
          ready->program.reset();
          ready->setup_path = path;
          ready->cancel_epoch = epoch;
          if (load) {
            ready->setup =
                load_native_workflow_batch_setup(path, limits, control);
            require(ready->setup->selection().document_index <= INT32_MAX,
                    "Setup document index exceeds desktop controls");
            for (const auto &row : ready->setup->inputs())
              if (row.selection)
                require(row.selection->document_index <= INT32_MAX,
                        "Setup input document index exceeds desktop controls");
            ready->input_table = Json::array(
                {metadataJson(ready->setup->common_program().record())});
            ready->setup_table_bytes =
                admit_review_table(ready->input_table, limit);
            ready->setup_entries = compile(ready->input_table, limit);
            const auto shape = snapshot.data().dataset.shape();
            for (auto &entry : ready->setup_entries) {
              entry.stop = {shape.t, shape.c, shape.z};
              if (entry.retained_target) {
                const auto &target =
                    std::get<NativeWorkflowOperation>(*entry.step).target;
                const std::array<std::optional<AxisRange>, 3> ranges{
                    target.t, target.c, target.z};
                for (std::size_t i = 0; i < 3; ++i)
                  if (ranges[i]) {
                    entry.first[i] = ranges[i]->start;
                    entry.stop[i] = ranges[i]->stop.value_or(entry.stop[i]);
                    entry.stride[i] = ranges[i]->step;
                  }
              }
            }
          } else {
            std::vector<detail::NativeWorkflowStepRef> refs;
            for (const auto &step : common)
              std::visit([&](const auto &v) { refs.emplace_back(&v); }, *step);
            const auto program = detail::make_native_workflow_program_borrowed(
                refs, desktop_workflow_program_limits(limit));
            const auto setup = make_native_workflow_batch_setup(
                program, input_rows, directory, selection, limits, control);
            auto encoded = setup.json(control);
            ready->files.push_back(
                {path,
                 detail::stage_atomic_file(
                     std::filesystem::u8path(path),
                     reinterpret_cast<const unsigned char *>(encoded.data()),
                     encoded.size(), control),
                 false});
            ready->setup_save = true;
            ready->setup_replace = replace;
          }
          ready->resources = context.result_resource_guard();
          context.checkpoint();
          *result = std::move(ready);
        },
        options);
    cancel_id.store(job.info().id);
    cell = std::move(result);
    harvested = false;
    worker_generation = generation;
  }
  void finish_setup() {
    if (!setup_ready)
      return;
    auto result = std::move(setup_ready);
    try {
      require(result->cancel_epoch == cancel_epoch.load(),
              "Batch setup IO canceled");
      if (result->setup) {
        // Complete every fallible copy before replacing any processing choice.
        auto rows = result->setup->inputs();
        auto directory = result->setup->output_directory();
        std::vector<int> indices(rows.size(), 0);
        entries = std::move(result->setup_entries);
        inputs = std::move(rows);
        output_directory = std::move(directory);
        review_indices = std::move(indices);
        selected = 0;
        all_documents = result->setup->selection().all_documents;
        document_index = int(result->setup->selection().document_index);
        table_bytes = result->setup_table_bytes;
        batch_confirmed = file_consent = false;
        progress.reset();
        message = "Batch setup loaded; review input overrides and confirm "
                  "processing/publication choices";
      } else {
        require(result->setup_save && result->files.size() == 1,
                "Invalid setup save result");
        result->files[0].staged->publish(result->setup_replace);
        message = "Batch setup saved; review fingerprints and publication "
                  "consent were not stored";
      }
      setup_path = std::move(result->setup_path);
    } catch (const std::exception &e) {
      error = e.what();
    }
  }
  void review_input(std::size_t index) {
    require(batch && index < inputs.size(), "Choose a Batch input row");
    invalidate();
    const auto input = inputs[index];
    const auto selection =
        input.selection.value_or(NativeWorkflowBatchSelection{
            all_documents, std::size_t(document_index)});
    require(document_index >= 0 && review_indices.at(index) >= 0,
            "Review document index must be nonnegative");
    const auto doc = selection.all_documents
                         ? std::size_t(review_indices[index])
                         : selection.document_index;
    std::vector<std::shared_ptr<const NativeWorkflowStep>> steps;
    if (!input.program)
      for (const auto &entry : entries)
        steps.push_back(effective(entry));
    auto cell = std::make_shared<std::shared_ptr<Ready>>();
    const auto epoch = cancel_epoch.load();
    const auto epoch_source = &cancel_epoch;
    const auto limit = cap;
    JobOptions options;
    options.label = "Review Batch input session";
    options.document_id = source->id();
    options.resource_bytes = cap / 2;
    options.retain_result_resources = true;
    job = scheduler->submit(
        [input, index, doc, steps = std::move(steps), cell, epoch, epoch_source,
         limit](const JobContext &context) {
          auto control = context.execution_control();
          control.checkpoint = [&context, epoch, epoch_source] {
            context.checkpoint();
            if (epoch_source->load() != epoch)
              throw OperationCancelled();
          };
          ExecutionScope scope(control);
          std::shared_ptr<const NativeWorkflowProgram> program = input.program;
          if (!program) {
            std::vector<detail::NativeWorkflowStepRef> refs;
            for (const auto &step : steps)
              std::visit([&](const auto &v) { refs.emplace_back(&v); }, *step);
            program = std::make_shared<const NativeWorkflowProgram>(
                detail::make_native_workflow_program_borrowed(
                    refs, desktop_workflow_program_limits(limit / 2)));
          }
          SessionLimits limits;
          limits.max_encoded_bytes = limit / 8;
          limits.max_json_bytes = limit / 4;
          limits.max_working_bytes = limit / 4;
          auto reviewed =
              review_native_workflow_batch_input(input.path, limits, control);
          require(reviewed.working_bytes() <= limit / 4,
                  "Input review exceeds the processing memory setting");
          require(doc < reviewed.documents().size(),
                  "Review document index outside input session");
          auto value = std::make_shared<Ready>(Json());
          value->program.reset();
          value->input_table = Json::array({metadataJson(program->record())});
          value->reviewed_input = std::move(reviewed);
          value->input_index = index;
          value->review_document = doc;
          value->cancel_epoch = epoch;
          value->resources = context.result_resource_guard();
          context.checkpoint();
          *cell = std::move(value);
        },
        options);
    cancel_id.store(job.info().id);
    this->cell = std::move(cell);
    harvested = false;
    worker_generation = generation;
  }
  void keep_input(Json table) {
    require(input_review && input_review->reviewed_input &&
                input_review->cancel_epoch == cancel_epoch.load(),
            "Input review expired");
    require(table.is_array() && table.size() == 1,
            "Input program must be native intent");
    auto held = input_review;
    auto existing = inputs;
    const auto limit = cap;
    const auto index = held->input_index;
    auto cell = std::make_shared<std::shared_ptr<Ready>>();
    const auto epoch = cancel_epoch.load();
    const auto epoch_source = &cancel_epoch;
    JobOptions options;
    options.label = "Verify reviewed input program";
    options.document_id = source->id();
    options.resource_bytes = cap / 4;
    options.retain_result_resources = true;
    job = scheduler->submit(
        [held, existing = std::move(existing), table = std::move(table), limit,
         index, cell, epoch, epoch_source](const JobContext &context) {
          auto control = context.execution_control();
          control.checkpoint = [&context, epoch, epoch_source] {
            context.checkpoint();
            if (epoch_source->load() != epoch)
              throw OperationCancelled();
          };
          ExecutionScope scope(control);
          auto program = std::make_shared<const NativeWorkflowProgram>(
              native_workflow_program_from_record(
                  metadataObjectFromJson(table[0]),
                  desktop_workflow_program_limits(limit / 2)));
          std::size_t used = 0;
          std::set<const void *> seen;
          for (std::size_t i = 0; i < existing.size(); ++i) {
            const auto &p = i == index ? program : existing[i].program;
            if (!p || !seen.insert(&p->steps()).second)
              continue;
            std::vector<detail::NativeWorkflowStepRef> refs;
            for (const auto &step : p->steps())
              std::visit([&](const auto &v) { refs.emplace_back(&v); }, step);
            const auto bytes = detail::native_workflow_intent_bytes(
                refs, 64, limit / 4 - used);
            require(bytes <= limit / 4 - used,
                    "Input override programs exceed processing memory setting");
            used += bytes;
          }
          held->reviewed_input->verify(control);
          auto value = std::make_shared<Ready>(Json());
          value->program.reset();
          value->verified_program = std::move(program);
          value->verified_fingerprint = held->reviewed_input->fingerprint();
          value->input_index = index;
          value->cancel_epoch = epoch;
          value->resources = context.result_resource_guard();
          context.checkpoint();
          *cell = std::move(value);
        },
        options);
    cancel_id.store(job.info().id);
    this->cell = std::move(cell);
    harvested = false;
    worker_generation = generation;
  }
  void changed_structure() {
    invalidate();
    file_consent = false;
    batch_confirmed = false;
    browse_route = 0;
    for (auto &entry : entries) {
      entry.confirmed = false;
      entry.path.clear();
    }
    message = "Program structure changed; review all retained steps and "
              "publication choices again";
  }
  void accept_parameters(const NativeWorkflowOperation &proposal) {
    require(prefix && prefix->cancel_epoch == cancel_epoch.load(),
            "Workflow parameter review canceled");
    const auto index = prefix->edit_index;
    auto &entry = entries.at(index);
    const auto &old = std::get<NativeWorkflowOperation>(*entry.step);
    require(proposal.operation == old.operation,
            "Workflow parameter operation changed");
    auto next = old;
    next.parameters = proposal.parameters;
    next.filter_options = proposal.filter_options;
    const auto program = make_native_workflow_program(
        {next}, desktop_workflow_program_limits(cap));
    const auto charge = admit_review_table(metadataJson(program.record()), cap);
    auto used = table_bytes;
    for (std::size_t i = 0; i < entries.size(); ++i)
      if (i != index) {
        require(entries[i].choice_bytes <= cap / 4 - used,
                "Workflow choices exceed memory setting");
        used += entries[i].choice_bytes;
      }
    require(charge <= cap / 4 - used,
            "Workflow parameters exceed memory setting");
    entry.step = std::make_shared<const NativeWorkflowStep>(std::move(next));
    entry.choice_bytes = charge;
    entry.authored = true;
    for (std::size_t i = index; i < entries.size(); ++i)
      entries[i].confirmed = false;
    viewport.reset();
    ready.reset();
    ++generation;
    cancel_epoch.fetch_add(1);
    error.clear();
    message = "Parameters kept; confirm this step and all later steps again";
    choices_accepted = true;
  }
  void accept_choices(const MetadataObject &record) {
    require(prefix && prefix->cancel_epoch == cancel_epoch.load(),
            "Workflow choice review canceled");
    const auto index = prefix->edit_index;
    require(index < entries.size(), "Workflow choice step expired");
    const auto charge = admit_review_table(metadataJson(record), cap);
    auto used = table_bytes;
    for (std::size_t i = 0; i < entries.size(); ++i)
      if (i != index) {
        require(entries[i].choice_bytes <= cap / 4 - used,
                "Workflow choices exceed memory setting");
        used += entries[i].choice_bytes;
      }
    require(charge <= cap / 4 - used, "Workflow choices exceed memory setting");
    auto &entry = entries[index];
    NativeWorkflowStep next;
    if (const auto old = std::get_if<NativeWorkflowImport>(entry.step.get())) {
      NativeWorkflowImport value;
      value.original = old->original;
      const auto schema = std::get<std::string>(record.at("schema"));
      if (schema == "imagejxx.imagej-annotation-import-command") {
        auto request = decode_imagej_annotation_import_request(record);
        value.items = std::move(request.items);
        value.mode = request.mode;
        value.limits = request.limits;
      } else {
        auto request = decode_imagej_roi_import_request(record);
        for (auto &item : request.items)
          value.items.emplace_back(std::move(item));
        value.mode = request.mode;
        value.limits.batch = request.limits;
      }
      next = std::move(value);
    } else {
      const auto &previous = std::get<NativeWorkflowExport>(*entry.step);
      auto request = decode_imagej_annotation_export_request(record);
      NativeWorkflowExport value;
      value.name = previous.name;
      value.original = previous.original;
      value.items = std::move(request.items);
      value.limits = request.limits;
      value.zip = request.is_zip;
      value.compress = request.compress;
      next = std::move(value);
    }
    auto owned = std::make_shared<const NativeWorkflowStep>(std::move(next));
    auto reviewed = record;
    entry.step = std::move(owned);
    entry.review_record.swap(reviewed);
    entry.choice_bytes = charge;
    entry.authored = true;
    for (std::size_t i = index; i < entries.size(); ++i)
      entries[i].confirmed = false;
    viewport.reset();
    ready.reset();
    ++generation;
    cancel_epoch.fetch_add(1);
    error.clear();
    message = "Step choices kept; confirm this step and all later steps again";
    choices_accepted = true;
  }

  std::shared_ptr<std::shared_ptr<Ready>> cell;
  Job job;
  std::atomic_uint64_t cancel_epoch{0}, cancel_id{0};
  bool harvested = true, open = false, focus = false, recording = false,
       file_consent = false, terminal = false, record_operation = false;
  int selected = 0;
  std::size_t cap = 0;
  std::uint64_t generation = 0, worker_generation = 0;
  std::string error, message, reason;
  ~State() {
    if (input_editor) {
      input_editor->cancel("parent_destroyed");
      input_editor->wait();
    }
    if (job.valid()) {
      job.cancel();
      job.wait();
    }
  }
  bool working() const { return job.valid() && !harvested; }
  void invalidate() {
    viewport.reset();
    cancel_epoch.fetch_add(1);
    if (job.valid())
      job.cancel();
    ready.reset();
    setup_ready.reset();
    close_editor();
    ++generation;
    error.clear();
    message.clear();
  }
  void observe(const std::string &key, bool enabled = true) {
    auto a = ImGui::GetItemRectMin(), b = ImGui::GetItemRectMax();
    const auto clip = ImGui::GetCurrentWindow()->ClipRect;
    const auto center_y = (a.y + b.y) / 2;
    const auto point_y =
        ImGui::IsItemVisible()
            ? std::clamp(center_y, clip.Min.y + 1,
                         std::max(clip.Min.y + 1, clip.Max.y - 1))
            : center_y;
    controls[key] = {
        {"id", ImGui::GetItemID()},
        {"enabled",
         enabled && !(ImGui::GetCurrentContext()->LastItemData.ItemFlags &
                      ImGuiItemFlags_Disabled)},
        {"visible", ImGui::IsItemVisible()},
        {"point", {a.x + std::min(70.f, (b.x - a.x) / 2), point_y}},
        {"rect", {a.x, a.y, b.x, b.y}}};
  }
  bool combo(const char *label, int &value, const char *const *names, int count,
             const char *key) {
    bool changed = false;
    if (ImGui::BeginCombo(label, names[value])) {
      for (int i = 0; i < count; ++i) {
        if (ImGui::Selectable(names[i], i == value)) {
          value = i;
          changed = true;
        }
        observe(std::string(key) + "." + std::to_string(i));
      }
      ImGui::EndCombo();
    }
    observe(key);
    return changed;
  }
};
NativeWorkflowPanel::NativeWorkflowPanel(std::shared_ptr<JobScheduler> s)
    : state_(std::make_unique<State>()) {
  state_->scheduler = std::move(s);
}
NativeWorkflowPanel::~NativeWorkflowPanel() = default;
void NativeWorkflowPanel::release_textures() {
  if (state_->viewport)
    state_->viewport->release_textures();
  if (state_->input_editor)
    state_->input_editor->release_textures();
}
void NativeWorkflowPanel::cancel(const char *reason) {
  auto &s = *state_;
  s.invalidate();
  s.open = false;
  s.reason = reason;
  s.source.reset();
  s.entries.clear();
}
void NativeWorkflowPanel::show(const DocumentSnapshot &source,
                               const Json &table, std::size_t cap,
                               bool recording, bool batch) {
  const auto table_bytes = admit_review_table(table, cap);
  auto entries = compile(table, cap);
  cancel("reopened");
  auto &s = *state_;
  s.entries = std::move(entries);
  s.table_bytes = table_bytes;
  s.table = table;
  s.source = source;
  s.cap = cap;
  s.recording = recording;
  s.record_operation = false;
  s.selected = 0;
  s.batch = batch;
  s.setup_path.clear();
  s.draft_mode = false;
  s.review_indices.clear();
  s.inputs.clear();
  s.input_path.clear();
  s.output_directory.clear();
  s.all_documents = false;
  s.document_index = 0;
  s.batch_confirmed = false;
  s.progress.reset();
  s.file_consent = false;
  s.terminal = false;
  s.reason.clear();
  s.open = s.focus = true;
  const auto shape = source.data().dataset.shape();
  for (auto &entry : s.entries) {
    entry.stop = {shape.t, shape.c, shape.z};
    if (entry.retained_target) {
      const auto &target =
          std::get<NativeWorkflowOperation>(*entry.step).target;
      const std::array<std::optional<AxisRange>, 3> ranges{target.t, target.c,
                                                           target.z};
      for (std::size_t i = 0; i < 3; ++i)
        if (ranges[i]) {
          entry.first[i] = ranges[i]->start;
          entry.stop[i] = ranges[i]->stop.value_or(entry.stop[i]);
          entry.stride[i] = ranges[i]->step;
        }
    }
  }
}
void NativeWorkflowPanel::show_input_draft(const DocumentSnapshot &source,
                                           const Json &table, std::size_t cap,
                                           const std::string &label) {
  show(source, table, cap, false, false);
  state_->draft_mode = true;
  state_->draft_label = label;
}
bool scientific_binary_operation(const std::string &id) {
  return id == "binary_erode" || id == "binary_dilate" || id == "binary_open" ||
         id == "binary_close";
}
bool scientific_operation_review_available(const std::string &id) {
  if (id == "convert_image")
    return true; // Explicit whole-dataset contract; other Samples remain guarded.
  static const std::set<std::string> available = [] {
    std::set<std::string> result;
    for (const auto &spec : operation_registry())
      if (spec.geometry == GeometryEffect::Preserve &&
          (spec.id == "invert_uint16" ||
           (spec.menu.size() == 2 && spec.menu[0] == "Process" &&
            (spec.menu[1] == "Math" || spec.menu[1] == "Filters" ||
             scientific_binary_operation(spec.id)))) &&
          std::none_of(spec.parameters.begin(), spec.parameters.end(),
                       [](const OperationParameter &p) {
                         return p.kind == ParameterKind::Image;
                       }))
        result.insert(spec.id);
    return result;
  }();
  return available.count(id) != 0;
}
bool scientific_operation_review_available(const std::string &id,
                                           const DocumentSnapshot &source) {
  if (!scientific_operation_review_available(id))
    return false;
  if (id == "convert_image") {
    const auto samples = source.data().dataset.shape().s;
    return samples == 1 ||
           (source.data().dataset.pixel_type() == PixelType::UInt8 &&
            (samples == 3 || samples == 4));
  }
  if (scientific_binary_operation(id) || id == "invert_uint16")
    return source.data().dataset.shape().s == 1 &&
           source.data().dataset.pixel_type() ==
               (id == "invert_uint16" ? PixelType::UInt16 : PixelType::UInt8);
  return true;
}
void NativeWorkflowPanel::show_operation(const DocumentSnapshot &source,
                                         const std::string &id,
                                         const Json &parameters,
                                         const Json &recorder, std::size_t cap,
                                         bool recording) {
  require(scientific_operation_review_available(id, source),
          "Operation requires a dedicated scientific adapter");
  require(parameters.is_object() && recorder.is_array(),
          "Operation parameters must be an object and recorder an array");
  const auto parameters_bytes = admit_review_table(parameters, cap);
  const auto recorder_bytes = admit_review_table(recorder, cap);
  require(parameters_bytes <= cap / 4 - recorder_bytes,
          "Scientific operation review context exceeds memory limit");
  const auto shape = source.data().dataset.shape();
  const OperationDefaultContext context{
      shape.x, shape.y, shape.s, source.data().dataset.pixel_type(), {}};
  auto values = operation_defaults(id, context, DefaultProfile::DesktopUI);
  for (const auto &[name, value] : nativeOperationParameters(parameters)) {
    require(values.count(name) != 0, "Unknown scientific operation parameter");
    values[name] = value;
  }
  NativeWorkflowOperation operation;
  operation.operation = id;
  operation.parameters = validate_operation(id, context, values);
  if (id == "convert_image") {
    operation.target.scope = TargetScope::Ranges;
    operation.target.roi = RoiPolicy::WholePlane;
    operation.target.t = AxisRange{0, shape.t, 1};
    operation.target.c = AxisRange{0, shape.c, 1};
    operation.target.z = AxisRange{0, shape.z, 1};
    (void)detail::admit_native_workflow(source, {&operation}, {64, cap / 2});
  }
  const auto program = make_native_workflow_program(
      {operation}, desktop_workflow_program_limits(cap));
  const auto seed = Json::array({metadataJson(program.record())});
  const auto seed_bytes = admit_review_table(seed, cap);
  require(seed_bytes <= cap / 4 - recorder_bytes - parameters_bytes,
          "Scientific operation review context exceeds memory limit");
  auto baseline = recorder; // Allocate before changing the current review.
  show(source, seed, cap, recording, false);
  state_->table.swap(baseline);
  state_->table_bytes += recorder_bytes + parameters_bytes;
  state_->record_operation = true;
}
void admit_scientific_conversion_review(const DocumentSnapshot &source,
                                        std::size_t cap) {
  NativeWorkflowOperation operation;
  operation.operation = "convert_image";
  const auto shape = source.data().dataset.shape();
  operation.target.scope = TargetScope::Ranges;
  operation.target.roi = RoiPolicy::WholePlane;
  operation.target.t = AxisRange{0, shape.t, 1};
  operation.target.c = AxisRange{0, shape.c, 1};
  operation.target.z = AxisRange{0, shape.z, 1};
  (void)detail::admit_native_workflow(source, {&operation}, {64, cap / 2});
}
void NativeWorkflowPanel::seed_batch(const Json &paths,
                                     const std::string &directory) {
  auto &s = *state_;
  require(s.open && s.batch && paths.is_array() && paths.size() <= 64,
          "Batch seeds require up to64 input paths");
  std::vector<NativeWorkflowBatchInput> inputs;
  for (const auto &path : paths) {
    require(path.is_string(), "Batch input paths require text");
    auto p = path.get<std::string>();
    inputs.push_back(
        {p, std::filesystem::u8path(p).stem().u8string() + "-processed"});
  }
  s.inputs = std::move(inputs);
  s.output_directory = directory;
  s.batch_confirmed = false;
}
Json NativeWorkflowPanel::recorded_operation() const {
  const auto &s = *state_;
  if (!s.record_operation || !s.recording || !s.ready || !s.ready->plan ||
      s.ready->preview_only || !s.ready->plan->completed_steps())
    return Json(nullptr);
  return s.ready->operation_record.value_or(Json(nullptr));
}
bool NativeWorkflowPanel::visible() const { return state_->open; }
bool NativeWorkflowPanel::working() const {
  return state_->working() || state_->child_working();
}
std::optional<JobInfo> NativeWorkflowPanel::job_info() const {
  if (state_->working())
    return state_->job.info();
  if (state_->child_working())
    return state_->child_job_info();
  return std::nullopt;
}
void NativeWorkflowPanel::request_cancel() const {
  state_->cancel_epoch.fetch_add(1);
  if (auto id = state_->cancel_id.load())
    state_->scheduler->cancel(id);
}

void NativeWorkflowPanel::poll() {
  auto &s = *state_;
  if (s.input_editor)
    s.input_editor->poll();
  if (!s.input_editor || !s.input_editor->working())
    s.retiring_input.reset();
  if (s.import_editor)
    s.import_editor->poll();
  if (s.export_editor)
    s.export_editor->poll();
  if (s.child_working())
    s.cancel_id.store(s.child_job_id());
  else
    s.retiring_prefix.reset();
  if (!s.working() || !s.job.ready())
    return;
  std::shared_ptr<Ready> value;
  try {
    s.job.rethrow();
    s.job.complete([&] { value = std::move(*s.cell); });
    if (s.worker_generation == s.generation && s.open) {
      if (value->setup || value->setup_save) {
        s.setup_ready = std::move(value);
      } else if (value->reviewed_input) {
        require(value->cancel_epoch == s.cancel_epoch.load(),
                "Input review canceled");
        s.input_review = std::move(value);
        s.input_table = s.input_review->input_table;
        s.input_editor = std::make_unique<NativeWorkflowPanel>(s.scheduler);
        s.input_editor->show_input_draft(
            s.input_review->reviewed_input->documents().at(
                s.input_review->review_document),
            s.input_table, s.cap / 2,
            s.input_review->reviewed_input->path() + " | document index " +
                std::to_string(s.input_review->review_document));
        s.message = "Reviewing the selected input document; every "
                    "execution-selected document must support the kept program";
      } else if (value->verified_program) {
        require(value->cancel_epoch == s.cancel_epoch.load() &&
                    value->input_index < s.inputs.size(),
                "Input program verification expired");
        auto &input = s.inputs[value->input_index];
        input.program = std::move(value->verified_program);
        input.expected_fingerprint = std::move(value->verified_fingerprint);
        s.batch_confirmed = false;
        s.file_consent = false;
        s.input_review.reset();
        s.input_table = Json();
        s.message = "Input program kept; confirm input selection and "
                    "publication choices again";
      } else if (value->edit_index != SIZE_MAX) {
        require(value->cancel_epoch == s.cancel_epoch.load(),
                "Workflow choice preparation canceled");
        s.prefix = std::move(value);
        auto &entry = s.entries.at(s.prefix->edit_index);
        if (std::holds_alternative<NativeWorkflowOperation>(*entry.step)) {
          auto operation_step = effective(entry);
          s.parameter_editor = std::make_unique<NativeWorkflowParameters>(
              *s.prefix->private_source,
              std::shared_ptr<const NativeWorkflowOperation>(
                  operation_step,
                  std::get_if<NativeWorkflowOperation>(operation_step.get())),
              s.cap / 2);
        } else if (std::holds_alternative<NativeWorkflowImport>(*entry.step)) {
          if (!s.import_editor)
            s.import_editor = std::make_unique<RoiImportPanel>(s.scheduler);
          if (entry.review_record.empty())
            s.import_editor->show_draft(
                *s.prefix->private_source,
                std::shared_ptr<const NativeWorkflowImport>(
                    entry.step,
                    std::get_if<NativeWorkflowImport>(entry.step.get())));
          else
            s.import_editor->show_draft(*s.prefix->private_source,
                                        entry.review_record);
        } else {
          if (!s.export_editor)
            s.export_editor =
                std::make_unique<RoiMixedExportPanel>(s.scheduler);
          if (entry.review_record.empty())
            s.export_editor->show_draft(
                *s.prefix->private_source,
                std::shared_ptr<const NativeWorkflowExport>(
                    entry.step,
                    std::get_if<NativeWorkflowExport>(entry.step.get())));
          else
            s.export_editor->show_draft(
                *s.prefix->private_source, entry.review_record,
                std::get<NativeWorkflowExport>(*entry.step).zip);
        }
        s.message = "Reviewing this step after earlier steps; image unchanged";
      } else {
        s.ready = std::move(value);
        s.message = s.ready->program
                        ? "Edited program prepared; image and files unchanged"
                        : "Prepared complete workflow; document unchanged";
      }
    }
  } catch (const std::exception &e) {
    if (s.prefix && !s.child_visible())
      s.close_editor();
    if (s.worker_generation == s.generation && s.open)
      s.error = e.what();
  }
  s.harvested = true;
  s.cell.reset();
}
void NativeWorkflowPanel::wait() const {
  if (state_->job.valid())
    state_->job.wait();
  if (state_->import_editor)
    state_->import_editor->wait();
  if (state_->export_editor)
    state_->export_editor->wait();
  if (state_->input_editor)
    state_->input_editor->wait();
}
bool NativeWorkflowPanel::wait_for(std::chrono::milliseconds n) const {
  const auto end = std::chrono::steady_clock::now() + n;
  auto remaining = [&] {
    return std::max(std::chrono::milliseconds(0),
                    std::chrono::duration_cast<std::chrono::milliseconds>(
                        end - std::chrono::steady_clock::now()));
  };
  return (!state_->job.valid() || state_->job.wait_for(remaining())) &&
         (!state_->import_editor ||
          state_->import_editor->wait_for(remaining())) &&
         (!state_->export_editor ||
          state_->export_editor->wait_for(remaining())) &&
         (!state_->input_editor || state_->input_editor->wait_for(remaining()));
}
RoiImportBrowseRequest NativeWorkflowPanel::browse_request() const {
  const auto &s = *state_;
  require(s.open && s.source && !s.working() && !s.draft_mode,
          "Choose an idle workflow draft");
  if (s.batch) {
    require(s.browse_route >= UINT64_MAX - 3, "Unknown batch browse route");
    return {*s.source, s.generation,
            s.browse_route == UINT64_MAX       ? s.output_directory
            : s.browse_route == UINT64_MAX - 1 ? s.input_path
                                               : s.setup_path,
            s.browse_route};
  }
  require(s.entries.at(s.selected).enabled &&
              std::holds_alternative<NativeWorkflowExport>(
                  *s.entries.at(s.selected).step),
          "Choose an export step");
  return {*s.source, s.generation, s.entries.at(s.selected).path,
          std::uint64_t(s.selected)};
}
bool NativeWorkflowPanel::browse_current(
    const RoiImportBrowseRequest &r,
    const std::optional<DocumentSnapshot> &current) const {
  const auto &s = *state_;
  return s.open && s.source && current && !s.working() &&
         s.generation == r.generation &&
         (r.route < s.entries.size() ||
          (s.batch && r.route >= UINT64_MAX - 3)) &&
         (s.batch || s.entries[r.route].enabled) &&
         matches(r.source, *s.source) && matches(r.source, *current);
}
void NativeWorkflowPanel::path(const RoiImportBrowseRequest &r,
                               const std::optional<DocumentSnapshot> &current,
                               std::string path, bool replace_existing) {
  require(browse_current(r, current), "Workflow file choice expired");
  auto &s = *state_;
  if (s.batch && (r.route == UINT64_MAX - 2 || r.route == UINT64_MAX - 3)) {
    s.setup_io(std::move(path), r.route == UINT64_MAX - 3, replace_existing);
    return;
  }
  s.invalidate();
  if (s.batch) {
    if (r.route == UINT64_MAX)
      s.output_directory = std::move(path);
    else
      s.input_path = std::move(path);
    s.batch_confirmed = false;
    s.file_consent = false;
    return;
  }
  s.entries.at(r.route).path = std::move(path);
  s.entries.at(r.route).confirmed = false;
}
void NativeWorkflowPanel::draw(
    const std::optional<DocumentSnapshot> &current, bool busy, std::size_t cap,
    bool recording, const Json &table, const std::function<void()> &browse,
    const std::function<void(const PreparedNativeWorkflow &,
                             const std::function<void()> &)> &publish,
    const std::function<void(const DocumentSnapshot &,
                             const std::function<void()> &)> &publish_batch,
    const std::function<void(const DocumentSnapshot &, Json)> &publish_program,
    bool gl, bool diagnostics) {
  auto &s = *state_;
  poll();
  if (!s.open)
    return;
  if (!s.terminal && (!s.source || !current || !matches(*s.source, *current))) {
    cancel("source_changed");
    return;
  }
  if (!s.terminal &&
      (cap != s.cap || recording != s.recording || table != s.table)) {
    cancel("workflow_context_changed");
    return;
  }
  s.finish_setup();
  if (s.input_review && s.input_review->cancel_epoch != s.cancel_epoch.load()) {
    s.close_input();
    s.error = "Input review canceled";
  }
  if (s.prefix && s.prefix->cancel_epoch != s.cancel_epoch.load()) {
    s.close_editor();
    s.error = "Workflow choice review canceled";
  }
  if (s.ready && s.ready->cancel_epoch != s.cancel_epoch.load()) {
    s.viewport.reset();
    if (s.ready->preview_only) {
      s.ready.reset();
      s.error = "Workflow preview canceled";
    }
  }
  s.controls = Json::object();
  ImGui::SetNextWindowSize(
      {740, std::min(740.f, ImGui::GetMainViewport()->WorkSize.y - 40)},
      ImGuiCond_FirstUseEver);
  if (s.focus) {
    ImGui::SetNextWindowFocus();
    s.focus = false;
  }
  ImGui::Begin(s.batch        ? "Review native session batch"
               : s.draft_mode ? "Edit input workflow"
                              : "Review complete native workflow",
               &s.open);
  {
    const auto a = ImGui::GetCurrentWindow()->InnerRect.Min,
               b = ImGui::GetCurrentWindow()->InnerRect.Max;
    s.bounds = {a.x, a.y, b.x, b.y};
  }
  ImGui::BeginChild("Workflow review fields",
                    {0, std::max(100.f, ImGui::GetContentRegionAvail().y - 48)},
                    false);
  if (s.draft_mode)
    ImGui::TextWrapped("Input: %s", s.draft_label.c_str());
  ImGui::TextWrapped(
      "Review every step on this image. Imports retain the recorded original "
      "mappings and complete native rows; exports use the listed "
      "source/current-row choices. Preparation leaves the image unchanged.");
  ImGui::BeginDisabled(busy || s.working() || s.child_working() || s.terminal ||
                       s.child_visible());
  if (s.batch) {
    if (ImGui::Button("Save setup...")) {
      s.browse_route = UINT64_MAX - 2;
      browse();
    }
    s.observe("batch.setup_save");
    ImGui::SameLine();
    if (ImGui::Button("Load setup...")) {
      s.browse_route = UINT64_MAX - 3;
      browse();
    }
    s.observe("batch.setup_load");
    if (!s.setup_path.empty())
      ImGui::TextWrapped("Setup: %s", s.setup_path.c_str());
    ImGui::TextWrapped(
        "Input sessions are read one at a time. Process all documents or the "
        "explicit index; preserve other documents and the input session "
        "tables. All items must prepare before staging. Outputs are new files; "
        "per-file failures retain earlier committed paths. The active image is "
        "unchanged.");
    ImGui::InputText("Input session", &s.input_path);
    s.observe("batch.input");
    ImGui::SameLine();
    if (ImGui::Button("Browse input...")) {
      s.browse_route = UINT64_MAX - 1;
      browse();
    }
    s.observe("batch.browse_input");
    if (ImGui::Button("Add input"))
      try {
        require(s.inputs.size() < 64, "Batch input limit");
        require(!s.input_path.empty(), "Choose an input session");
        s.inputs.push_back(
            {s.input_path,
             std::filesystem::u8path(s.input_path).stem().u8string() +
                 "-processed"});
        s.invalidate();
        s.batch_confirmed = false;
        s.file_consent = false;
      } catch (const std::exception &e) {
        s.error = e.what();
      }
    s.observe("batch.add");
    s.review_indices.resize(s.inputs.size());
    ImGui::BeginChild("Batch inputs", {0, 140}, true);
    const auto input_a = ImGui::GetCurrentWindow()->InnerRect.Min,
               input_b = ImGui::GetCurrentWindow()->InnerRect.Max;
    s.input_bounds = {input_a.x, input_a.y, input_b.x, input_b.y};
    int remove = -1;
    for (std::size_t i = 0; i < s.inputs.size(); ++i) {
      ImGui::PushID(int(i));
      ImGui::TextUnformatted(s.inputs[i].path.c_str());
      ImGui::SetNextItemWidth(190);
      bool edit = ImGui::InputText("Output stem", &s.inputs[i].output_stem);
      s.observe("batch.stem." + std::to_string(i));
      ImGui::SameLine();
      if (ImGui::SmallButton("Remove"))
        remove = int(i);
      s.observe("batch.remove." + std::to_string(i));
      ImGui::SameLine();
      if (ImGui::SmallButton("Review input..."))
        try {
          s.review_input(i);
        } catch (const std::exception &e) {
          s.error = e.what();
        }
      s.observe("batch.review." + std::to_string(i));
      ImGui::SameLine();
      const bool custom = bool(s.inputs[i].program) ||
                          bool(s.inputs[i].selection) ||
                          !s.inputs[i].expected_fingerprint.empty();
      ImGui::BeginDisabled(!custom);
      if (ImGui::SmallButton("Restore inheritance")) {
        s.invalidate();
        s.inputs[i].program.reset();
        s.inputs[i].selection.reset();
        s.inputs[i].expected_fingerprint.clear();
        s.batch_confirmed = false;
        s.file_consent = false;
      }
      s.observe("batch.inherit." + std::to_string(i), custom);
      ImGui::EndDisabled();
      auto &input = s.inputs[i];
      ImGui::TextUnformatted(input.program ? "Program: input override"
                                           : "Program: common inherited");
      if (input.program && input.expected_fingerprint.empty())
        ImGui::TextDisabled("Review this override on its selected input again");
      bool selection_override = bool(input.selection);
      ImGui::SameLine();
      if (ImGui::Checkbox("Override document selection", &selection_override)) {
        s.invalidate();
        if (selection_override)
          input.selection = NativeWorkflowBatchSelection{
              s.all_documents,
              s.all_documents ? 0 : std::size_t(std::max(0, s.document_index))};
        else
          input.selection.reset();
        input.expected_fingerprint.clear();
        s.batch_confirmed = false;
        s.file_consent = false;
      }
      s.observe("batch.selection_override." + std::to_string(i));
      auto selected = input.selection.value_or(NativeWorkflowBatchSelection{
          s.all_documents, std::size_t(std::max(0, s.document_index))});
      if (input.selection) {
        bool all = selected.all_documents;
        int index =
            int(std::min(selected.document_index, std::size_t(INT32_MAX)));
        bool change = ImGui::Checkbox("All documents for this input", &all);
        s.observe("batch.input_all." + std::to_string(i));
        if (!all) {
          ImGui::SameLine();
          ImGui::SetNextItemWidth(80);
          change |= ImGui::InputInt("Input document index", &index, 0, 0);
          s.observe("batch.input_document." + std::to_string(i));
        }
        if (change) {
          s.invalidate();
          input.selection = NativeWorkflowBatchSelection{
              all, all ? 0 : std::size_t(std::max(0, index))};
          input.expected_fingerprint.clear();
          s.batch_confirmed = false;
          s.file_consent = false;
        }
        selected = *input.selection;
      }
      if (selected.all_documents) {
        ImGui::SetNextItemWidth(80);
        ImGui::InputInt("Review document only (all execute)",
                        &s.review_indices[i], 0, 0);
        s.observe("batch.review_document." + std::to_string(i));
      }

      if (edit) {
        s.invalidate();
        s.batch_confirmed = false;
        s.file_consent = false;
      }
      ImGui::PopID();
    }
    ImGui::EndChild();
    if (remove >= 0) {
      s.inputs.erase(s.inputs.begin() + remove);
      s.review_indices.erase(s.review_indices.begin() + remove);
      s.invalidate();
      s.batch_confirmed = false;
      s.file_consent = false;
    }
    bool edit = ImGui::InputText("Existing output folder", &s.output_directory);
    s.observe("batch.directory");
    ImGui::SameLine();
    if (ImGui::Button("Browse folder...")) {
      s.browse_route = UINT64_MAX;
      browse();
    }
    s.observe("batch.browse_directory");
    bool selection_edit = ImGui::Checkbox(
        "Process all documents in each session", &s.all_documents);
    s.observe("batch.all_documents");
    ImGui::BeginDisabled(s.all_documents);
    selection_edit |=
        ImGui::InputInt("Document index (zero-based)", &s.document_index, 0, 0);
    s.observe("batch.document_index", !s.all_documents);
    ImGui::EndDisabled();
    if (edit || selection_edit) {
      s.invalidate();
      if (selection_edit)
        for (auto &input : s.inputs)
          if (!input.selection)
            input.expected_fingerprint.clear();
      s.batch_confirmed = false;
      s.file_consent = false;
    }
    if (ImGui::Checkbox("Confirm input/document/output choices",
                        &s.batch_confirmed))
      s.invalidate();
    s.observe("batch.confirm");
    if (ImGui::Checkbox("Accept per-file publication", &s.file_consent))
      s.invalidate();
    s.observe("file_consent");
  }
  for (std::size_t i = 0; i < s.entries.size(); ++i) {
    ImGui::PushID(int(i));
    const auto label = std::to_string(i + 1) + ". " + s.entries[i].title +
                       (s.entries[i].enabled ? "" : " [disabled]") +
                       (s.entries[i].confirmed ? " [reviewed]" : "");
    if (ImGui::Selectable(label.c_str(), s.selected == int(i)))
      s.selected = int(i);
    s.observe("step." + std::to_string(i));
    ImGui::PopID();
  }
  const bool can_up = s.selected > 0;
  ImGui::BeginDisabled(!can_up);
  if (ImGui::Button("Move step up")) {
    s.changed_structure();
    std::swap(s.entries[s.selected], s.entries[s.selected - 1]);
    --s.selected;
  }
  s.observe("move_up", can_up);
  ImGui::EndDisabled();
  ImGui::SameLine();
  const bool can_down = std::size_t(s.selected) + 1 < s.entries.size();
  ImGui::BeginDisabled(!can_down);
  if (ImGui::Button("Move step down")) {
    s.changed_structure();
    std::swap(s.entries[s.selected], s.entries[s.selected + 1]);
    ++s.selected;
  }
  s.observe("move_down", can_down);
  ImGui::EndDisabled();
  ImGui::SameLine();
  if (ImGui::Checkbox("Step enabled", &s.entries[s.selected].enabled))
    s.changed_structure();
  s.observe("enabled");
  auto &entry = s.entries.at(s.selected);
  {
    const auto operation=std::get_if<NativeWorkflowOperation>(entry.step.get());
    const bool rgb_step=operation && operation->operation=="rgb_dataset";
    const bool parameter_step = bool(operation);
    const bool prefix_confirmed =
        std::all_of(s.entries.begin(), s.entries.begin() + s.selected,
                    [](const auto &e) { return e.confirmed; });
    ImGui::BeginDisabled(!prefix_confirmed || s.batch || rgb_step);
    if (ImGui::Button(parameter_step ? "Edit this step's parameters..."
                                     : "Edit this step's choices..."))
      try {
        s.invalidate();
        const auto index = std::size_t(s.selected);
        std::vector<std::shared_ptr<const NativeWorkflowStep>> steps;
        for (std::size_t i = 0; i < index; ++i)
          steps.push_back(effective(s.entries[i]));
        const auto source = *s.source;
        const auto prefix_cap = s.cap / 2;
        require(prefix_cap > 0, "Private prefix requires a processing budget");
        const auto epoch = s.cancel_epoch.load();
        const auto epoch_source = &s.cancel_epoch;
        auto cell = std::make_shared<std::shared_ptr<Ready>>();
        JobOptions options;
        options.label = "Prepare private workflow prefix";
        options.document_id = source.id();
        options.resource_bytes = prefix_cap;
        options.retain_result_resources = true;
        s.job = s.scheduler->submit(
            [source, steps = std::move(steps), prefix_cap, index, cell, epoch,
             epoch_source](const JobContext &context) {
              auto control = context.execution_control();
              control.checkpoint = [&context, epoch, epoch_source] {
                context.checkpoint();
                if (epoch_source->load() != epoch)
                  throw OperationCancelled();
              };
              std::vector<detail::NativeWorkflowStepRef> refs;
              for (const auto &step : steps)
                std::visit([&](const auto &v) { refs.emplace_back(&v); },
                           *step);
              auto prepared = detail::prepare_native_workflow_borrowed(
                  source, refs, {64, prefix_cap}, control);
              auto ready = std::make_shared<Ready>(prepared.preview(),
                                                   prepared.retained_bytes());
              ready->prefix_completed = prepared.completed_steps();
              ready->edit_index = index;
              ready->cancel_epoch = epoch;
              ready->resources = context.result_resource_guard();
              *cell = std::move(ready);
            },
            options);
        s.cancel_id.store(s.job.info().id);
        s.cell = std::move(cell);
        s.harvested = false;
        s.worker_generation = s.generation;
      } catch (const std::exception &e) {
        s.error = e.what();
      }
    s.observe(parameter_step ? "edit_parameters" : "edit_choices",
              prefix_confirmed && !s.batch && !rgb_step);
    ImGui::EndDisabled();
    if (s.batch)
      ImGui::TextWrapped("Edit common choices in Run, or use Review input... "
                         "to author choices for one Batch input.");
    if (!prefix_confirmed)
      ImGui::TextWrapped(
          "Confirm earlier steps before preparing this step's private input.");
  }
  bool changed = false;
  if (const auto op = std::get_if<NativeWorkflowOperation>(entry.step.get())) {
    ImGui::TextWrapped(
        "Processing intent: %s. Edit parameters above or retain them; choose "
        "the "
        "destination target below. Selection targeting uses this image's "
        "native selection/positioned ROIs.",
        op->operation.c_str());
    if(op->operation=="rgb_dataset" || op->operation=="color_stack" || op->operation=="indexed_dataset" || op->operation=="indexed_rgb") {
      if(op->operation=="rgb_dataset")ImGui::TextWrapped("Captured RGB rendering/ranges/palettes are retained. This workflow replaces its owner in one transaction; the recorded destination remains historical. Use the RGB editor to author different rendering intent.");
      else if(op->operation=="indexed_dataset") {
        const auto request=detail::indexed_dataset_workflow_request(op->parameters,*op->original);
        ImGui::Text("Global stack palette: at most %d colors",request.max_colors);
        ImGui::TextWrapped("Quantize every RGB plane using one palette. Z, time and calibration are retained. Edit parameters to choose the color and memory limits.");
      } else if(op->operation=="indexed_rgb") {
        const auto request=detail::indexed_rgb_workflow_request(op->parameters,*op->original);
        ImGui::Text("Palette: %s",request.palette_source==IndexedRgbPaletteSource::Stored?"Stored indexed palette":"Current input display");
        ImGui::TextWrapped("Expand using each input's current-stage palette. Edit parameters to choose display rendering or stored palette bytes.");
      } else {
        const auto request=detail::color_stack_workflow_request(op->parameters,*op->original);
        ImGui::TextWrapped("Split or merge RGB / HSB components, or flatten an RGB stack. Edit this step's parameters to author different choices; the original capture stays unchanged. Publication replaces this owner in one paired transaction.");
        ImGui::TextWrapped(op->parameters.empty()?"The captured T/C/Z view below is retained.":"Authored choices use this input's current stage view; the captured view below is historical.");
        const char *actions[]{"Split component channels","Merge component channels","Flatten RGB channels to Z"};
        const char *spaces[]{"RGB 8-bit","HSB 8-bit","HSB 32-bit"};
        ImGui::Text("Choices: %s / %s / %s",actions[int(request.action)],spaces[int(request.space)],request.layout==ColorStackLayoutPolicy::PreserveAxes?"Preserve Z and time":"ImageJ stack layout");
        ImGui::Text("Spatial/time reset allowed: %s; non-XY removal allowed: %s",request.allow_calibration_reset?"yes":"no",request.allow_nonxy_annotation_drop?"yes":"no");
      }
      ImGui::TextWrapped("Choose complete T/C/Z sizes at this stage. Selection is retained; pixels and numeric display share Undo/Redo.");
      const auto position=metadataJson(op->original->at("view_position"));
      ImGui::Text("Captured view T%d C%d Z%d (zero-based)",position[0].get<int>(),position[1].get<int>(),position[2].get<int>());
      const char *axes[]{"T","C","Z"};
      for(int i=0;i<3;++i){ImGui::PushID(i);changed|=ImGui::InputInt(axes[i],&entry.stop[i],0,0);s.observe("rgb_stop."+std::to_string(i));ImGui::PopID();}
      if(changed){entry.scope=int(TargetScope::Ranges);entry.roi=int(RoiPolicy::WholePlane);entry.first={0,0,0};entry.stride={1,1,1};entry.retained_target=false;}
    } else if (op->operation == "convert_image") {
      ImGui::TextWrapped("Convert every T/C/Z plane; the active selection is retained "
                         "but is not used as a processing mask. Changed pixels "
                         "reset numeric display range, channel display and manual "
                         "threshold together. Undo/Redo restores both states.");
      ImGui::Text("Complete ranges T[0,%d), C[0,%d), Z[0,%d)",
                  entry.stop[0], entry.stop[1], entry.stop[2]);
    } else {
    const char *scopes[]{"Current T/C/Z plane", "All Z at current T/C",
                         "T/C/Z ranges", "Explicit T/C/Z positions"};
    const char *rois[]{"Whole plane", "Active selection", "Positioned ROIs"};
    changed |= s.combo("Process", entry.scope, scopes, 4, "scope");
    changed |= s.combo("Region", entry.roi, rois, 3, "region");
    if (entry.scope == 2) {
      const char *axes[]{"T", "C", "Z"};
      for (int i = 0; i < 3; ++i) {
        ImGui::PushID(i);
        ImGui::Text("%s range [start, stop), zero-based", axes[i]);
        changed |= ImGui::InputInt("Start", &entry.first[i], 0, 0);
        s.observe("start." + std::to_string(i));
        ImGui::SameLine();
        changed |= ImGui::InputInt("Stop", &entry.stop[i], 0, 0);
        s.observe("stop." + std::to_string(i));
        changed |= ImGui::InputInt("Step", &entry.stride[i], 0, 0);
        s.observe("stride." + std::to_string(i));
        ImGui::PopID();
      }
    }
    if (entry.scope == 3) {
      ImGui::InputInt3("Position T/C/Z (zero-based)", entry.position.data());
      s.observe("position");
      if (ImGui::Button("Add position")) {
        entry.positions.push_back(
            {entry.position[0], entry.position[1], entry.position[2]});
        changed = true;
      }
      s.observe("add_position");
      ImGui::SameLine();
      if (ImGui::Button("Clear positions")) {
        entry.positions.clear();
        changed = true;
      }
      s.observe("clear_positions");
      ImGui::Text("%zu ordered positions", entry.positions.size());
    }
    if (changed)
      entry.retained_target = false;
    }
    ImGui::Text("Execution: %s",
                op->target.mode == ExecutionMode::Volume ? "Volume" : "Planes");
    if (op->original &&
        detail::native_workflow_bound_operation_original(*op->original) &&
        op->operation == "gaussian_blur_imagej") {
      changed |=
          ImGui::Checkbox(s.batch ? "Use each input document's calibration"
                                  : "Use this image's calibration",
                          &entry.calibration);
      s.observe("calibration");
    }
  } else if (const auto imp =
                 std::get_if<NativeWorkflowImport>(entry.step.get())) {
    ImGui::Text("%zu input members; %s", imp->items.size(),
                imp->mode == ImageJRoiImportMode::Append ? "Append"
                                                         : "Replace");
    for (const auto &item : imp->items)
      std::visit(
          [](const auto &v) {
            ImGui::TextUnformatted(v.name.c_str());
            if (v.mapping) {
              const auto &m = [&]() -> const ImageJRoiPlaneMapping & {
                if constexpr (std::is_same_v<std::decay_t<decltype(v)>,
                                             ImageJRoiImportItem>)
                  return *v.mapping;
                else
                  return v.mapping->plane;
              }();
              ImGui::Text("Original plane %s; source extent %d/%d/%d/%d/%d/%d",
                          m.plane == Plane::XY   ? "XY"
                          : m.plane == Plane::XZ ? "XZ"
                                                 : "YZ",
                          m.extent.t, m.extent.c, m.extent.z, m.extent.y,
                          m.extent.x, m.extent.s);
            }
            if (v.native_replacement)
              ImGui::Text(
                  "Native row: %s; destination extent %d/%d/%d/%d/%d/%d",
                  v.native_replacement->annotation.name.c_str(),
                  v.native_replacement->extent.t,
                  v.native_replacement->extent.c,
                  v.native_replacement->extent.z,
                  v.native_replacement->extent.y,
                  v.native_replacement->extent.x,
                  v.native_replacement->extent.s);
          },
          item);
    ImGui::TextWrapped(
        "These choices must validate on this destination. Edit this step to "
        "review mappings and native replacements on its private input.");
  } else {
    const auto &ex = std::get<NativeWorkflowExport>(*entry.step);
    for (const auto &item : ex.items) {
      if (item.fresh_source)
        ImGui::Text("%s: generated source, current %s row %zu",
                    item.name.c_str(),
                    item.annotation_kind == ImageJAnnotationKind::Selection
                        ? "selection"
                        : "display",
                    item.annotation_index);
      else
        ImGui::Text("%s: archive source %zu, current %s row %zu",
                    item.name.c_str(), item.source_index,
                    item.annotation_kind == ImageJAnnotationKind::Selection
                        ? "selection"
                        : "display",
                    item.annotation_index);
    }
    if (s.batch || s.draft_mode) {
      ImGui::Text("Per-document output token: %s", ex.name.c_str());
    } else if (entry.enabled) {
      changed |= ImGui::InputText("Output file", &entry.path);
      s.observe("path");
      ImGui::SameLine();
      if (ImGui::Button("Browse..."))
        browse();
      s.observe("browse");
      ImGui::TextWrapped(
          "Outputs publish individually before the final image edit. If a "
          "later "
          "publication fails, earlier files remain and are listed below. "
          "Existing files are never overwritten.");
      if (ImGui::Checkbox("Accept per-file publication", &s.file_consent)) {
        s.invalidate();
      }
      s.observe("file_consent");
    }
  }
  if (changed) {
    s.invalidate();
    for (std::size_t i = std::size_t(s.selected); i < s.entries.size(); ++i)
      s.entries[i].confirmed = false;
  }
  if (!entry.enabled)
    ImGui::TextWrapped("Disabled: retain and validate this step's intent; no "
                       "destination binding, processing or export output.");
  if (ImGui::Checkbox(entry.enabled
                          ? "Use these complete step choices on this image"
                          : "Confirm this retained disabled step",
                      &entry.confirmed))
    s.invalidate();
  s.observe("confirm");
  if (!s.batch) {
    const bool confirmed =
        std::all_of(s.entries.begin(), s.entries.end(),
                    [](const auto &e) { return e.confirmed; });
    ImGui::BeginDisabled(!confirmed);
    if (ImGui::Button("Prepare image preview"))
      try {
        s.invalidate();
        std::vector<std::shared_ptr<const NativeWorkflowStep>> steps;
        for (const auto &item : s.entries)
          steps.push_back(effective(item));
        const auto source = *s.source;
        auto cell = std::make_shared<std::shared_ptr<Ready>>();
        const auto epoch = s.cancel_epoch.load();
        const auto epoch_source = &s.cancel_epoch;
        JobOptions options;
        options.label = "Prepare workflow image preview";
        options.document_id = source.id();
        options.resource_bytes = cap;
        options.retain_result_resources = true;
        s.job = s.scheduler->submit(
            [source, steps = std::move(steps), cell, epoch, epoch_source,
             cap](const JobContext &context) {
              auto control = context.execution_control();
              control.checkpoint = [&context, epoch, epoch_source] {
                context.checkpoint();
                if (epoch_source->load() != epoch)
                  throw OperationCancelled();
              };
              std::vector<detail::NativeWorkflowStepRef> refs;
              for (const auto &step : steps)
                std::visit([&](const auto &v) { refs.emplace_back(&v); },
                           *step);
              ExecutionScope scope(control);
              auto prepared = detail::prepare_native_workflow_borrowed(
                  source, refs, {64, cap / 2}, control);
              kernel_checkpoint();
              admit_native_workflow_viewport(prepared.preview(), cap / 2);
              auto ready = std::make_shared<Ready>(std::move(prepared));
              ready->preview_only = true;
              ready->cancel_epoch = epoch;
              ready->resources = context.result_resource_guard();
              context.checkpoint();
              *cell = std::move(ready);
            },
            options);
        s.cancel_id.store(s.job.info().id);
        s.cell = std::move(cell);
        s.harvested = false;
        s.worker_generation = s.generation;
      } catch (const std::exception &e) {
        s.error = e.what();
      }
    s.observe("prepare_preview", confirmed);
    if (ImGui::Button("Prepare edited program"))
      try {
        s.invalidate();
        std::vector<std::shared_ptr<const NativeWorkflowStep>> steps;
        for (const auto &item : s.entries)
          steps.push_back(effective(item));
        const auto source = *s.source;
        const auto program_cap = s.cap;
        auto cell = std::make_shared<std::shared_ptr<Ready>>();
        const auto epoch = s.cancel_epoch.load();
        const auto epoch_source = &s.cancel_epoch;
        JobOptions options;
        options.label = "Prepare portable workflow program";
        options.document_id = source.id();
        options.resource_bytes = program_cap;
        options.retain_result_resources = true;
        s.job = s.scheduler->submit(
            [steps = std::move(steps), program_cap, cell, epoch,
             epoch_source](const JobContext &context) {
              auto control = context.execution_control();
              control.checkpoint = [&context, epoch, epoch_source] {
                context.checkpoint();
                if (epoch_source->load() != epoch)
                  throw OperationCancelled();
              };
              ExecutionScope scope(control);
              kernel_checkpoint();
              std::vector<detail::NativeWorkflowStepRef> refs;
              for (const auto &step : steps)
                std::visit([&](const auto &v) { refs.emplace_back(&v); },
                           *step);
              auto program = detail::make_native_workflow_program_borrowed(
                  refs, desktop_workflow_program_limits(program_cap));
              auto value = metadataJson(program.record());
              kernel_checkpoint();
              auto ready =
                  std::make_shared<Ready>(Json::array({std::move(value)}));
              ready->cancel_epoch = epoch;
              ready->resources = context.result_resource_guard();
              *cell = std::move(ready);
            },
            options);
        s.cancel_id.store(s.job.info().id);
        s.cell = std::move(cell);
        s.harvested = false;
        s.worker_generation = s.generation;
      } catch (const std::exception &e) {
        s.error = e.what();
      }
    s.observe("prepare_program", confirmed);
    ImGui::EndDisabled();
  }
  if (!s.draft_mode) {
    bool all = std::all_of(s.entries.begin(), s.entries.end(),
                           [](const auto &e) { return e.confirmed; });
    all = all &&
          (!s.batch ||
           (!s.inputs.empty() && s.batch_confirmed && s.file_consent &&
            std::none_of(s.inputs.begin(), s.inputs.end(), [](const auto &v) {
              return v.program && v.expected_fingerprint.empty();
            })));
    ImGui::BeginDisabled(!all);
    if (ImGui::Button("Prepare complete workflow"))
      try {
        s.invalidate();
        std::vector<std::shared_ptr<const NativeWorkflowStep>> steps;
        std::vector<std::filesystem::path> paths;
        for (std::size_t i = 0; i < s.entries.size(); ++i) {
          const auto &e = s.entries[i];
          if (const auto op =
                  std::get_if<NativeWorkflowOperation>(e.step.get())) {
            steps.push_back(effective(e));
          } else if (const auto ex =
                         std::get_if<NativeWorkflowExport>(e.step.get())) {
            if (!e.enabled) {
              steps.push_back(effective(e));
              continue;
            }
            require(s.file_consent, "Confirm per-file publication");
            if (s.batch) {
              steps.push_back(effective(e));
              continue;
            }
            auto path = destination(e.path, ex->zip);
            for (const auto &prior : paths)
              require(!alias(prior, path),
                      "Workflow outputs address the same file");
            paths.push_back(path);
            steps.push_back(effective(e));
          } else
            steps.push_back(effective(e));
        }
        std::optional<Json> operation_record;
        std::size_t record_bytes = 0;
        if (s.record_operation && s.recording) {
          std::vector<NativeWorkflowStep> captured;
          captured.reserve(steps.size());
          for (const auto &step : steps) {
            captured.push_back(*step);
            if (auto op =
                    std::get_if<NativeWorkflowOperation>(&captured.back());
                op && op->enabled) {
              const auto frozen =
                  resolve_target(*s.source, op->target, op->operation);
              if (op->target.scope != TargetScope::Positions) {
                op->target.scope = TargetScope::Ranges;
                op->target.t = frozen.ranges()[0];
                op->target.c = frozen.ranges()[1];
                op->target.z = frozen.ranges()[2];
              }
            }
          }
          operation_record =
              metadataJson(make_native_workflow_program(
                               captured, desktop_workflow_program_limits(cap))
                               .record());
          require(s.table.size() < 64, "Recorder has reached64 rows");
          auto appended = s.table;
          appended.push_back(*operation_record);
          (void)admit_review_table(appended, cap);
          (void)compile(appended, cap);
          record_bytes = admit_review_table(*operation_record, cap);
        }
        const auto batch = s.batch;
        const auto inputs = s.inputs;
        const auto directory = s.output_directory;
        require(!batch || s.document_index >= 0,
                "Batch document index must be nonnegative");
        NativeWorkflowBatchOptions batch_options;
        batch_options.all_documents = s.all_documents;
        batch_options.document_index = std::size_t(s.document_index);
        batch_options.max_working_bytes = cap;
        batch_options.workflow.max_retained_bytes = cap;
        auto progress = std::make_shared<BatchProgress>();
        progress->states.resize(inputs.size(),
                                NativeWorkflowBatchState::Queued);
        s.progress = progress;
        const auto source = *s.source;
        const auto generation = s.generation;
        auto cell = std::make_shared<std::shared_ptr<Ready>>();
        JobOptions options;
        options.label = "Prepare native workflow";
        options.document_id = source.id();
        options.resource_bytes = s.cap;
        options.retain_result_resources = true;
        auto epoch = s.cancel_epoch.load();
        auto epoch_source = &s.cancel_epoch;
        auto job = s.scheduler->submit(
            [source, steps = std::move(steps), paths = std::move(paths), cell,
             cap, epoch, epoch_source, batch, inputs, directory, batch_options,
             progress, operation_record = std::move(operation_record),
             record_bytes](const JobContext &context) mutable {
              auto control = context.execution_control();
              control.checkpoint = [&context, epoch, epoch_source] {
                context.checkpoint();
                if (epoch_source->load() != epoch)
                  throw OperationCancelled();
              };
              std::vector<detail::NativeWorkflowStepRef> refs;
              for (const auto &step : steps)
                std::visit([&](const auto &v) { refs.emplace_back(&v); },
                           *step);
              if (batch) {
                auto prepared = detail::prepare_native_workflow_batch_borrowed(
                    inputs, refs, directory, batch_options, control,
                    [progress](std::size_t i, NativeWorkflowBatchState state) {
                      std::lock_guard<std::mutex> lock(progress->mutex);
                      progress->states.at(i) = state;
                    });
                auto ready = std::make_shared<Ready>(std::move(prepared));
                ready->resources = context.result_resource_guard();
                ready->cancel_epoch = epoch;
                *cell = std::move(ready);
                return;
              }
              auto prepared = detail::prepare_native_workflow_borrowed(
                  source, refs, {64, cap - record_bytes}, control);
              auto ready = std::make_shared<Ready>(std::move(prepared));
              ready->operation_record = std::move(operation_record);
              ready->resources = context.result_resource_guard();
              ready->cancel_epoch = epoch;
              require(ready->plan->outputs().size() == paths.size(),
                      "Workflow output count mismatch");
              ready->files.reserve(paths.size());
              for (std::size_t i = 0; i < paths.size(); ++i) {
                context.checkpoint();
                const auto &bytes = ready->plan->outputs()[i].value.data();
                auto staged = detail::stage_atomic_file(paths[i], bytes.data(),
                                                        bytes.size(), control);
                ready->files.push_back(
                    {paths[i].u8string(), std::move(staged), false});
              }
              context.checkpoint();
              *cell = std::move(ready);
            },
            options);
        s.cell = std::move(cell);
        s.job = std::move(job);
        s.cancel_id.store(s.job.info().id);
        s.harvested = false;
        s.worker_generation = generation;
      } catch (const std::exception &e) {
        s.error = e.what();
      }
    s.observe("prepare", all);
    ImGui::EndDisabled();
  } // No filesystem workflow preparation in an input draft.
  if (s.ready && s.ready->program) {
    ImGui::TextWrapped(s.draft_mode
                           ? "Keep these choices for this Batch input. Input "
                             "files and the active image stay unchanged."
                           : "Keep reusable choices and targets in Workflow, "
                             "then use Save. Output paths and write consent "
                             "are not stored. The image stays unchanged.");
    ImGui::BeginDisabled(s.ready->attempted);
    if (ImGui::Button(s.draft_mode ? "Keep input program"
                                   : "Keep edited program in Workflow"))
      try {
        s.ready->attempted = true;
        if (s.ready->cancel_epoch != s.cancel_epoch.load())
          throw OperationCancelled();
        publish_program(*s.source, *s.ready->program);
        s.open = false;
      } catch (const std::exception &e) {
        s.error = e.what();
      }
    s.observe("keep_program", !s.ready->attempted);
    ImGui::EndDisabled();
  } else if (s.ready) {
    if (s.ready->plan && !s.ready->attempted) {
      if (ImGui::Button("View prepared image..."))
        try {
          require(s.ready->cancel_epoch == s.cancel_epoch.load(),
                  "Workflow preview canceled");
          const auto used = s.ready->plan->retained_bytes();
          require(used < cap,
                  "Workflow viewport exceeds remaining memory setting");
          s.viewport = std::make_unique<NativeWorkflowViewport>(
              s.ready->plan->preview(), std::min(cap / 2, cap - used));
        } catch (const std::exception &e) {
          s.error = e.what();
        }
      s.observe("view_preview");
    }
    if (s.ready->batch)
      ImGui::Text("Prepared %zu inputs, %zu outputs; encoded %zu bytes",
                  s.ready->batch->items().size(),
                  s.ready->batch->outputs().size(),
                  s.ready->batch->encoded_bytes());
    else {
      ImGui::Text("Prepared %zu steps, %zu outputs; admission %zu bytes",
                  s.ready->plan->completed_steps(),
                  s.ready->plan->outputs().size(),
                  s.ready->plan->retained_bytes());
    }
    if (s.ready->preview_only)
      ImGui::TextWrapped("Preview only. Use complete preparation to review and "
                         "apply outputs.");
    ImGui::BeginDisabled(s.ready->attempted || s.ready->preview_only);
    if (ImGui::Button("Apply complete workflow"))
      try {
        s.viewport.reset();
        auto ready = s.ready;
        ready->attempted = true;
        s.terminal = true;
        if (ready->cancel_epoch != s.cancel_epoch.load())
          throw OperationCancelled();
        std::string done = s.batch ? "Batch published"
                           : ready->plan->completed_steps() == 0
                               ? "No enabled steps; image and files unchanged"
                               : "Workflow published";
        if (ready->batch)
          publish_batch(*s.source, [ready] { ready->batch->publish(); });
        else
          publish(*ready->plan, [ready] {
            for (auto &file : ready->files) {
              file.staged->publish(false);
              file.published = true;
            }
          });
        s.message.swap(done);
      } catch (const std::exception &e) {
        if (s.ready->batch && !s.ready->batch->attempted())
          s.ready->batch->abandon();
        s.error = e.what();
      }
    s.observe("apply", !s.ready->attempted && !s.ready->preview_only);
    ImGui::EndDisabled();
    if (s.ready->batch) {
      for (const auto &item : s.ready->batch->items())
        ImGui::TextWrapped("%s: %s", batch_state(item.state),
                           item.input.c_str());
      for (const auto &file : s.ready->batch->outputs())
        ImGui::TextWrapped("%s: %s",
                           file.published ? "Published"
                           : file.failed  ? "Failed"
                                          : "Not published",
                           file.path.c_str());
    }
    for (const auto &file : s.ready->files)
      ImGui::TextWrapped("%s: %s",
                         file.published ? "Published" : "Not published",
                         file.path.c_str());
  }
  if (s.batch && s.progress && !s.ready) {
    std::lock_guard<std::mutex> lock(s.progress->mutex);
    for (std::size_t i = 0; i < s.progress->states.size(); ++i)
      ImGui::Text("Input %zu: %s", i + 1, batch_state(s.progress->states[i]));
  }
  ImGui::EndDisabled();
  if (!s.error.empty())
    ImGui::TextWrapped("%s", s.error.c_str());
  if (!s.message.empty())
    ImGui::TextWrapped("%s", s.message.c_str());
  ImGui::EndChild();
  if (ImGui::Button("Cancel / close"))
    s.open = false;
  s.observe("cancel");
  ImGui::End();
  if (!s.open) {
    cancel("closed");
    return;
  }
  if (s.prefix && s.child_visible()) {
    const std::optional<DocumentSnapshot> private_source =
        *s.prefix->private_source;
    s.choices_accepted = false;
    auto refuse_browse = [] {
      throw std::invalid_argument(
          "Workflow choices do not select external files");
    };
    if (s.parameter_editor && s.parameter_editor->visible())
      s.parameter_editor->draw(!busy,
                               [&](const NativeWorkflowOperation &value) {
                                 s.accept_parameters(value);
                               });
    else if (s.import_editor && s.import_editor->visible())
      s.import_editor->draw(
          private_source, busy, cap / 2, refuse_browse,
          [](Plane, int, std::optional<int>, std::optional<int>) {},
          [&](const ResolvedImageJRoiImport &value) {
            s.accept_choices(value.record());
          },
          [&](const ResolvedImageJAnnotationImport &value) {
            s.accept_choices(value.record());
          });
    else if (s.export_editor && s.export_editor->visible())
      s.export_editor->draw(
          private_source, busy, cap / 2, refuse_browse,
          [](const PlanarAnnotation &) {},
          [](const PlanarDisplayAnnotation &) {},
          [](const ResolvedImageJAnnotationExport &,
             const std::function<void()> &) {
            throw std::invalid_argument("Workflow draft cannot publish files");
          },
          [&](const ResolvedImageJAnnotationExport &value) {
            s.accept_choices(value.record());
          });
    if (!s.child_visible() || s.choices_accepted)
      s.close_editor();
  }
  if (s.input_review && s.input_editor && s.input_editor->visible()) {
    const std::optional<DocumentSnapshot> private_source =
        s.input_review->reviewed_input->documents().at(
            s.input_review->review_document);
    auto refuse = [] {
      throw std::invalid_argument("Input program drafts do not publish files");
    };
    s.input_editor->draw(
        private_source, busy, cap / 2, false, s.input_table, refuse,
        [](const PreparedNativeWorkflow &, const std::function<void()> &) {
          throw std::invalid_argument("Input draft cannot Apply");
        },
        [](const DocumentSnapshot &, const std::function<void()> &) {
          throw std::invalid_argument("Input draft cannot Batch publish");
        },
        [&](const DocumentSnapshot &, Json table) {
          s.keep_input(std::move(table));
        },
        gl, diagnostics);
    if (!s.input_editor->visible())
      s.close_input();
  }
  if (s.viewport &&
      !s.viewport->draw(gl, !busy && !s.working() && !s.child_visible(),
                        diagnostics))
    s.viewport.reset();
}
Json NativeWorkflowPanel::diagnostics() const {
  const auto &s = *state_;
  Json entries = Json::array(), files = Json::array();
  for (const auto &e : s.entries)
    entries.push_back({{"title", e.title},
                       {"enabled", e.enabled},
                       {"confirmed", e.confirmed},
                       {"path", e.path},
                       {"authored", e.authored},
                       {"choice_bytes", e.choice_bytes}});
  if (s.ready)
    for (const auto &f : s.ready->files)
      files.push_back({{"path", f.path}, {"published", f.published}});
  Json inputs = Json::array();
  for (const auto &input : s.inputs)
    inputs.push_back(
        {{"path", input.path},
         {"output_stem", input.output_stem},
         {"program_override", bool(input.program)},
         {"program_steps", input.program ? input.program->steps().size() : 0},
         {"selection_override", bool(input.selection)},
         {"all_documents",
          input.selection ? Json(input.selection->all_documents) : Json()},
         {"document_index",
          input.selection ? Json(input.selection->document_index) : Json()},
         {"fingerprint", input.expected_fingerprint}});
  Json items = Json::array();
  if (s.ready && s.ready->batch) {
    for (const auto &i : s.ready->batch->items())
      items.push_back({{"input", i.input}, {"state", batch_state(i.state)}});
    for (const auto &f : s.ready->batch->outputs())
      files.push_back({{"path", f.path},
                       {"published", f.published},
                       {"failed", f.failed},
                       {"session", f.session},
                       {"input_index", f.input_index}});
  } else if (s.progress) {
    std::lock_guard<std::mutex> lock(s.progress->mutex);
    for (std::size_t i = 0; i < s.progress->states.size(); ++i)
      items.push_back({{"state", batch_state(s.progress->states[i])}});
  }
  return {{"batch", s.batch},
          {"draft_mode", s.draft_mode},
          {"input_bounds", s.input_bounds},
          {"inputs", inputs},
          {"items", items},
          {"output_directory", s.output_directory},
          {"setup_path", s.setup_path},
          {"setup_pending", bool(s.setup_ready)},
          {"editor", s.child_diagnostics()},
          {"input_editor",
           s.input_editor ? s.input_editor->diagnostics() : Json::object()},
          {"input_review", bool(s.input_review)},
          {"input_review_index",
           s.input_review ? s.input_review->input_index : SIZE_MAX},
          {"prefix_ready", bool(s.prefix)},
          {"prefix_retiring", bool(s.retiring_prefix)},
          {"prefix_steps", s.prefix ? s.prefix->edit_index : 0},
          {"prefix_completed_steps", s.prefix ? s.prefix->prefix_completed : 0},
          {"file_consent", s.file_consent},
          {"generation", s.generation},
          {"prefix_bytes", s.prefix ? s.prefix->prefix_bytes : 0},
          {"visible", s.open},
          {"working", s.working() || s.child_working()},
          {"ready", bool(s.ready)},
          {"program_ready", s.ready && bool(s.ready->program)},
          {"preview_only", s.ready && s.ready->preview_only},
          {"completed_steps",
           s.ready && s.ready->plan ? s.ready->plan->completed_steps() : 0},
          {"viewport", s.viewport ? s.viewport->diagnostics() : Json::object()},
          {"attempted", s.ready && s.ready->attempted},
          {"terminal", s.terminal},
          {"selected", s.selected},
          {"entries", entries},
          {"files", files},
          {"controls", s.controls},
          {"window", s.bounds},
          {"error", s.error},
          {"message", s.message},
          {"cancel_reason", s.reason}};
}
} // namespace imagejxx::desktop
