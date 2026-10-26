#include <imagejxx/dataset_projection_command.hpp>
#include "projection_record_internal.hpp"
#include <algorithm>
#include <stdexcept>

namespace imagejxx {
namespace {
using namespace detail::axis_command_record;
constexpr auto schema = "imagejxx.dataset-z-projection-command";
bool boolean(const MetadataValue &value) {
  if (const auto flag = std::get_if<bool>(&value)) return *flag;
  throw std::invalid_argument("Projection record requires an exact Boolean");
}
MetadataValue shape_record(DatasetShape shape) {
  MetadataArray result;
  for (int value : shape.dimensions()) result.emplace_back(std::int64_t(value));
  return metadata_array(std::move(result));
}
MetadataObject request_record(const DatasetZProjectionRequest &r) {
  return {{"first", std::int64_t(r.first)}, {"last", std::int64_t(r.last)},
          {"group_size", std::int64_t(r.group_size)},
          {"active_t", std::int64_t(r.active_t)}, {"active_c", std::int64_t(r.active_c)},
          {"all_time_frames", r.all_time_frames}, {"all_channels", r.all_channels},
          {"method", r.method}, {"max_output_pixel_bytes", std::uint64_t(r.max_output_pixel_bytes)},
          {"max_working_bytes", std::uint64_t(r.max_working_bytes)}};
}
DatasetZProjectionRequest read_request(const MetadataObject &object) {
  exact_fields(object, {"first", "last", "group_size", "active_t", "active_c",
      "all_time_frames", "all_channels", "method", "max_output_pixel_bytes", "max_working_bytes"});
  DatasetZProjectionRequest r;
  r.first = index_value(object.at("first")); r.last = index_value(object.at("last"));
  r.group_size = index_value(object.at("group_size"));
  r.active_t = index_value(object.at("active_t")); r.active_c = index_value(object.at("active_c"));
  r.all_time_frames = boolean(object.at("all_time_frames"));
  r.all_channels = boolean(object.at("all_channels"));
  r.method = text_value(object.at("method"));
  r.max_output_pixel_bytes = size_value(object.at("max_output_pixel_bytes"));
  r.max_working_bytes = size_value(object.at("max_working_bytes"));
  return r;
}
DatasetZProjectionLayout describe(const SourceRecord &source, const DatasetZProjectionRequest &r) {
  if (source.sample_policy != FloatingSamplePolicy::FiniteOnly)
    throw std::invalid_argument("Projection command V1 requires the native finite profile");
  if (!r.max_output_pixel_bytes || !r.max_working_bytes)
    throw std::invalid_argument("Projection command requires positive memory caps");
  return describe_dataset_z_projection(source.shape, source.pixel_type, source.calibration, r);
}
MetadataObject derived_record(const DatasetZProjectionLayout &layout) {
  // Persist portable numerical claims. Native working_bytes includes ABI-sized
  // tables/owners and is deliberately recomputed under the request cap.
  return {{"shape", shape_record(layout.shape)},
          {"pixel_type", std::string(pixel_type_name(layout.pixel_type))},
          {"calibration", metadata_object(calibration_record(layout.calibration))},
          {"source_first_t", std::int64_t(layout.source_first_t)},
          {"source_first_c", std::int64_t(layout.source_first_c)},
          {"source_first_z", std::int64_t(layout.source_first_z)},
          {"contributors_per_group", std::int64_t(layout.contributors_per_group)},
          {"output_pixel_bytes", std::uint64_t(layout.output_pixel_bytes)},
          {"reduction_scratch_bytes", std::uint64_t(layout.reduction_scratch_bytes)}};
}
void validate_derived(const MetadataObject &object, const DatasetZProjectionLayout &layout) {
  exact_fields(object, {"shape", "pixel_type", "calibration", "source_first_t", "source_first_c",
      "source_first_z", "contributors_per_group", "output_pixel_bytes", "reduction_scratch_bytes"});
  if (read_shape(object.at("shape")).dimensions() != layout.shape.dimensions() ||
      read_pixel_type(object.at("pixel_type")) != layout.pixel_type ||
      !same_calibration(read_calibration(object_value(object.at("calibration"))), layout.calibration) ||
      index_value(object.at("source_first_t")) != layout.source_first_t ||
      index_value(object.at("source_first_c")) != layout.source_first_c ||
      index_value(object.at("source_first_z")) != layout.source_first_z ||
      index_value(object.at("contributors_per_group"), true) != layout.contributors_per_group ||
      size_value(object.at("output_pixel_bytes")) != layout.output_pixel_bytes ||
      size_value(object.at("reduction_scratch_bytes")) != layout.reduction_scratch_bytes)
    throw std::invalid_argument("Projection derived claims contradict original source/request");
}
struct Decoded { SourceRecord source; DatasetZProjectionRequest request; };
void require_source(const DocumentSnapshot &source, const SourceRecord &expected) {
  const auto current = source_metadata(source);
  if (current.document_id != expected.document_id)
    throw std::invalid_argument("Projection command source owner differs");
  if (!current.revisions.matches(expected.revisions, Change::Pixels | Change::Metadata))
    throw RevisionConflict();
  if (current.shape.dimensions() != expected.shape.dimensions() ||
      current.pixel_type != expected.pixel_type || current.sample_policy != expected.sample_policy ||
      !same_calibration(current.calibration, expected.calibration))
    throw std::invalid_argument("Projection command source metadata differs");
}
Decoded decode(const MetadataObject &record) {
  validate_metadata(record);
  exact_fields(record, {"schema", "version", "operation", "profile", "source", "request", "derived", "destination"});
  if (text_value(record.at("schema")) != schema || unsigned_value(record.at("version")) != 1 ||
      text_value(record.at("operation")) != "project_dataset_z" ||
      text_value(record.at("profile")) != "native_finite" ||
      text_value(record.at("destination")) != "new_document")
    throw std::invalid_argument("Unsupported scientific projection command grammar");
  auto source = read_source(object_value(record.at("source")));
  auto request = read_request(object_value(record.at("request")));
  // A positive working cap is a request, not a claim of prior admission on this
  // ABI. Validate portable derived claims here; binding/execution enforces the
  // original cap on the current ABI. Explicit rebind supplies its own new cap.
  auto portable = request;
  portable.max_working_bytes = std::numeric_limits<std::size_t>::max();
  try { validate_derived(object_value(record.at("derived")), describe(source, portable)); }
  catch (const std::out_of_range &error) { throw std::invalid_argument(error.what()); }
  return {std::move(source), std::move(request)};
}
}

ResolvedDatasetZProjectionCommand::ResolvedDatasetZProjectionCommand(
    DocumentSnapshot source, DatasetZProjectionRequest request,
    DatasetZProjectionLayout layout, MetadataObject record)
    : source_(std::move(source)), request_(std::move(request)),
      layout_(std::move(layout)), record_(std::move(record)) {}

ResolvedDatasetZProjectionCommand resolve_dataset_z_projection_command(
    const DocumentSnapshot &source, const DatasetZProjectionRequest &request) {
  const auto metadata = source_metadata(source);
  auto layout = describe(metadata, request);
  MetadataObject record{{"schema", std::string(schema)}, {"version", std::int64_t(1)},
      {"operation", std::string("project_dataset_z")}, {"profile", std::string("native_finite")},
      {"source", metadata_object(source_record(metadata))},
      {"request", metadata_object(request_record(request))},
      {"derived", metadata_object(derived_record(layout))},
      {"destination", std::string("new_document")}};
  return {source, request, std::move(layout), std::move(record)};
}
void validate_dataset_z_projection_command_record(const MetadataObject &record) { (void)decode(record); }

ResolvedDatasetZProjectionCommand dataset_z_projection_command_from_record(
    const DocumentSnapshot &source, const MetadataObject &record) {
  const auto decoded = decode(record);
  require_source(source, decoded.source);
  return resolve_dataset_z_projection_command(source, decoded.request);
}
ResolvedDatasetZProjectionCommand rebind_dataset_z_projection_command(
    const DocumentSnapshot &source, const MetadataObject &record,
    const DatasetZProjectionRequest &explicit_request) {
  (void)decode(record);
  return resolve_dataset_z_projection_command(source, explicit_request);
}
Dataset execute_dataset_z_projection(const ResolvedDatasetZProjectionCommand &command,
                                    const ExecutionControl &control, std::size_t cap) {
  if (!cap) throw std::invalid_argument("Projection execution cap must be positive");
  require_source(command.source(), read_source(object_value(command.record().at("source"))));
  auto request = command.request();
  request.max_working_bytes = std::min(request.max_working_bytes, cap);
  (void)describe_dataset_z_projection(command.source().data().dataset, request);
  return project_dataset_z(command.source().data().dataset, request, control);
}
}
