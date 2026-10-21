#include "atomic_file.hpp"
#include "axis_command_record.hpp"
#include "imagej_roi_import_internal.hpp"
#include "kernel_control.hpp"
#include "native_workflow_program_internal.hpp"
#include "session_io_internal.hpp"
#include <algorithm>
#include <filesystem>
#include <imagejxx/native_workflow_batch_setup.hpp>
namespace imagejxx {
namespace {
using namespace detail::axis_command_record;
namespace codec = detail::session_metadata;
void require(bool value, const char *message) {
  if (!value)
    throw std::invalid_argument(message);
}
void valid(const NativeWorkflowBatchSetupLimits &l) {
  require(l.max_inputs && l.max_inputs <= 4096 && l.max_steps_per_program &&
              l.max_steps_per_program <= 4096 && l.max_total_steps &&
              l.max_total_steps <= 262144 && l.max_record_bytes &&
              l.max_record_bytes <=
                  NativeWorkflowProgramLimits::hard_max_record_bytes,
          "Batch setup limits out of range");
}
struct Budget {
  std::size_t left;
  void add(std::size_t n) {
    require(n <= left, "Batch setup aggregate byte limit");
    left -= n;
  }
};
void text(const std::string &s, bool nonempty = false,
          std::size_t cap = 131072) {
  require((!nonempty || !s.empty()) && s.size() <= cap &&
              s.find('\0') == std::string::npos,
          "Batch setup path/stem text limit");
  // Enforce UTF-8 on both Windows and byte-oriented platforms.
  (void)codec::Json(s).dump();
}
void stem(const std::string &s) {
  text(s, true, 180);
  require(s != "." && s != ".." && s.find_first_of("/\\:") == std::string::npos,
          "Batch setup output stem must be flat");
}
void selector(const NativeWorkflowBatchSelection &s) {
  require(!s.all_documents || s.document_index == 0,
          "All-document setup selector cannot carry an index");
}
MetadataObject selection_record(const NativeWorkflowBatchSelection &s) {
  selector(s);
  return {{"all_documents", s.all_documents},
          {"document_index", std::uint64_t(s.document_index)}};
}
NativeWorkflowBatchSelection selection_from(const MetadataValue &v) {
  const auto &r = object_value(v);
  exact_fields(r, {"all_documents", "document_index"});
  const auto p = std::get_if<bool>(&r.at("all_documents"));
  require(p, "Batch setup selector requires bool");
  const auto index = unsigned_value(r.at("document_index"));
  require(index <= SIZE_MAX, "Batch setup document index exceeds native range");
  NativeWorkflowBatchSelection s{*p, std::size_t(index)};
  selector(s);
  return s;
}
bool null(const MetadataValue &v) {
  return std::holds_alternative<std::monostate>(v);
}
const MetadataArray &array(const MetadataValue &v) {
  if (const auto p = std::get_if<std::vector<std::string>>(&v);
      p && p->empty()) {
    static const MetadataArray empty;
    return empty;
  }
  return array_value(v);
}
MetadataObject encode(const NativeWorkflowProgram &common,
                      const std::vector<NativeWorkflowBatchInput> &inputs,
                      const std::string &directory,
                      const NativeWorkflowBatchSelection &selection,
                      const NativeWorkflowBatchSetupLimits &limits) {
  valid(limits);
  require(inputs.size() <= limits.max_inputs, "Batch setup input count limit");
  text(directory);
  selector(selection);
  Budget budget{limits.max_record_bytes};
  budget.add(1024);
  budget.add(directory.size());
  // Admit external strings before copying any program or input vector.
  for (const auto &input : inputs) {
    kernel_checkpoint();
    text(input.path, true);
    stem(input.output_stem);
    if (input.selection)
      selector(*input.selection);
    budget.add(512);
    budget.add(input.path.size());
    budget.add(input.output_stem.size());
  }
  std::size_t steps = 0, retained = 0;
  auto program_record = [&](const NativeWorkflowProgram &p) {
    require(p.steps().size() <= limits.max_steps_per_program &&
                p.steps().size() <= limits.max_total_steps - steps,
            "Batch setup total step limit");
    steps += p.steps().size();
    const auto used =
        detail::native_workflow_program_retained_bytes(p, budget.left);
    budget.add(used);
    retained += used;
    auto record = detail::native_workflow_program_record_bounded(
        p, {limits.max_steps_per_program, budget.left});
    budget.add(detail::imagej_roi_record_bytes(record, budget.left));
    return metadata_object(std::move(record));
  };
  auto common_record = program_record(common);
  MetadataArray rows;
  for (const auto &input : inputs) {
    kernel_checkpoint();
    rows.push_back(metadata_object(
        {{"path", input.path},
         {"output_stem", input.output_stem},
         {"program",
          input.program ? program_record(*input.program) : MetadataValue{}},
         {"selection", input.selection
                           ? metadata_object(selection_record(*input.selection))
                           : MetadataValue{}}}));
  }
  MetadataObject record{
      {"schema", "imagejxx.native-workflow-batch-setup"},
      {"version", std::uint64_t(1)},
      {"common_program", std::move(common_record)},
      {"selection", metadata_object(selection_record(selection))},
      {"output_directory", directory},
      {"inputs", metadata_array(std::move(rows))}};
  detail::native_workflow_record_budget(record, limits.max_record_bytes);
  require(detail::imagej_roi_record_bytes(record, limits.max_record_bytes) <=
              limits.max_record_bytes - retained,
          "Batch setup aggregate byte limit");
  detail::native_workflow_json_budget(record, limits.max_record_bytes);
  return record;
}
} // namespace
struct detail::NativeWorkflowBatchSetupAccess {
  static NativeWorkflowBatchSetup
  make(NativeWorkflowProgram common,
       std::vector<NativeWorkflowBatchInput> inputs, std::string directory,
       NativeWorkflowBatchSelection selection,
       NativeWorkflowBatchSetupLimits limits) {
    NativeWorkflowBatchSetup s;
    for (auto &input : inputs)
      input.expected_fingerprint.clear();
    s.common_ =
        std::make_shared<const NativeWorkflowProgram>(std::move(common));
    s.inputs_ = std::make_shared<const std::vector<NativeWorkflowBatchInput>>(
        std::move(inputs));
    s.directory_ = std::move(directory);
    s.selection_ = selection;
    s.limits_ = limits;
    return s;
  }
};
MetadataObject
NativeWorkflowBatchSetup::record(const ExecutionControl &control) const {
  ExecutionScope scope(control);
  detail::file_checkpoint(control);
  return encode(*common_, *inputs_, directory_, selection_, limits_);
}
std::string
NativeWorkflowBatchSetup::json(const ExecutionControl &control) const {
  ExecutionScope scope(control);
  auto r = record(control);
  auto result = codec::to_json(r).dump();
  require(result.size() <= limits_.max_record_bytes,
          "Batch setup JSON transport limit");
  detail::file_checkpoint(control);
  return result;
}
NativeWorkflowBatchSetup make_native_workflow_batch_setup(
    const NativeWorkflowProgram &common,
    const std::vector<NativeWorkflowBatchInput> &inputs,
    const std::string &directory, const NativeWorkflowBatchSelection &selection,
    const NativeWorkflowBatchSetupLimits &limits,
    const ExecutionControl &control) {
  ExecutionScope scope(control);
  detail::file_checkpoint(control);
  (void)encode(common, inputs, directory, selection, limits);
  detail::file_checkpoint(control);
  return detail::NativeWorkflowBatchSetupAccess::make(common, inputs, directory,
                                                      selection, limits);
}
NativeWorkflowBatchSetup native_workflow_batch_setup_from_record(
    const MetadataObject &r, const NativeWorkflowBatchSetupLimits &limits,
    const ExecutionControl &control) {
  valid(limits);
  ExecutionScope scope(control);
  detail::file_checkpoint(control);
  detail::native_workflow_record_budget(r, limits.max_record_bytes);
  validate_metadata(r);
  Budget budget{limits.max_record_bytes};
  budget.add(detail::imagej_roi_record_bytes(r, budget.left));
  exact_fields(r, {"schema", "version", "common_program", "selection",
                   "output_directory", "inputs"});
  require(text_value(r.at("schema")) ==
                  "imagejxx.native-workflow-batch-setup" &&
              unsigned_value(r.at("version")) == 1,
          "Unsupported native Batch setup");
  const auto selection = selection_from(r.at("selection"));
  const auto &directory = text_value(r.at("output_directory"));
  text(directory);
  const auto &rows = array(r.at("inputs"));
  require(rows.size() <= limits.max_inputs, "Batch setup input count limit");
  // Admit every row's closed structure/counts before decoding any program.
  std::size_t total = 0;
  auto count = [&](const MetadataObject &p) {
    const auto n = array(p.at("steps")).size();
    require(n && n <= limits.max_steps_per_program &&
                n <= limits.max_total_steps - total,
            "Batch setup total step limit");
    total += n;
  };
  count(object_value(r.at("common_program")));
  for (const auto &row : rows) {
    kernel_checkpoint();
    const auto &i = object_value(row);
    exact_fields(i, {"path", "output_stem", "program", "selection"});
    text(text_value(i.at("path")), true);
    stem(text_value(i.at("output_stem")));
    if (!null(i.at("selection")))
      (void)selection_from(i.at("selection"));
    if (!null(i.at("program")))
      count(object_value(i.at("program")));
  }
  auto decode = [&](const MetadataValue &v) {
    kernel_checkpoint();
    auto p = native_workflow_program_from_record(
        object_value(v), {limits.max_steps_per_program, budget.left});
    budget.add(detail::native_workflow_program_retained_bytes(p, budget.left));
    return p;
  };
  auto common = decode(r.at("common_program"));
  std::vector<NativeWorkflowBatchInput> inputs;
  for (const auto &row : rows) {
    const auto &i = object_value(row);
    NativeWorkflowBatchInput input{text_value(i.at("path")),
                                   text_value(i.at("output_stem"))};
    if (!null(i.at("selection")))
      input.selection = selection_from(i.at("selection"));
    if (!null(i.at("program")))
      input.program = std::make_shared<const NativeWorkflowProgram>(
          decode(i.at("program")));
    inputs.push_back(std::move(input));
  }
  detail::file_checkpoint(control);
  // Canonical output also has to fit before the candidate becomes observable.
  (void)encode(common, inputs, directory, selection, limits);
  return detail::NativeWorkflowBatchSetupAccess::make(
      std::move(common), std::move(inputs), directory, selection, limits);
}
NativeWorkflowBatchSetup native_workflow_batch_setup_from_json(
    const std::string &text, const NativeWorkflowBatchSetupLimits &limits,
    const ExecutionControl &control) {
  valid(limits);
  ExecutionScope scope(control);
  detail::file_checkpoint(control);
  return native_workflow_batch_setup_from_record(
      codec::object_from_json(detail::bounded_native_workflow_json(
          text, {std::min(limits.max_total_steps, std::size_t(4096)),
                 limits.max_record_bytes})),
      limits, control);
}
void save_native_workflow_batch_setup(const std::string &path,
                                      const NativeWorkflowBatchSetup &setup,
                                      bool replace_existing,
                                      const ExecutionControl &control) {
  text(path, true);
  const auto encoded = setup.json(control);
  detail::file_checkpoint(control);
  auto staged = detail::stage_atomic_file(
      std::filesystem::u8path(path),
      reinterpret_cast<const unsigned char *>(encoded.data()), encoded.size(),
      control);
  detail::file_checkpoint(control);
  staged->publish(replace_existing);
}
NativeWorkflowBatchSetup
load_native_workflow_batch_setup(const std::string &path,
                                 const NativeWorkflowBatchSetupLimits &limits,
                                 const ExecutionControl &control) {
  valid(limits);
  text(path, true);
  SessionLimits io;
  io.max_encoded_bytes = io.max_working_bytes = limits.max_record_bytes;
  auto bytes = detail::read_session_bytes(path, io, control);
  return native_workflow_batch_setup_from_json(
      std::string(bytes.begin(), bytes.end()), limits, control);
}
} // namespace imagejxx
