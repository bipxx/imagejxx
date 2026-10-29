#include <imagejxx/axis_orientation_command.hpp>
#include "calibrated_descriptor_internal.hpp"
#include "axis_command_record.hpp"

#include <algorithm>
#include <stdexcept>
#include <utility>

namespace imagejxx {
namespace {

using namespace detail::axis_command_record;
constexpr const char *orientation_operation = "axis_range_orientation";

bool boolean_value(const MetadataValue &value) {
  if (const auto flag = std::get_if<bool>(&value))
    return *flag;
  throw std::invalid_argument("Axis orientation flags require actual Booleans");
}

MetadataObject request_record(const AxisRangeOrientationRequest &request) {
  const auto &range = request.range;
  return {{"plane", std::string(plane_name(range.plane))},
          {"first", static_cast<std::int64_t>(range.first)},
          {"last", static_cast<std::int64_t>(range.last)},
          {"step", static_cast<std::int64_t>(range.step)},
          {"t", static_cast<std::int64_t>(range.t)},
          {"c", static_cast<std::int64_t>(range.c)},
          {"max_working_bytes", static_cast<std::uint64_t>(range.max_working_bytes)},
          {"reverse_order", request.reverse_order},
          {"flip_vertical", request.flip_vertical},
          {"transpose", request.transpose}};
}

AxisRangeOrientationRequest read_request(const MetadataObject &record) {
  exact_fields(record, {"plane", "first", "last", "step", "t", "c",
                        "max_working_bytes", "reverse_order", "flip_vertical",
                        "transpose"});
  return {{read_plane(record.at("plane")), index_value(record.at("first")),
           index_value(record.at("last")), index_value(record.at("step"), true),
           index_value(record.at("t")), index_value(record.at("c")),
           size_value(record.at("max_working_bytes"))},
          boolean_value(record.at("reverse_order")),
          boolean_value(record.at("flip_vertical")),
          boolean_value(record.at("transpose"))};
}

struct DecodedCommand {
  SourceRecord source;
  AxisRangeOrientationRequest request;
};

DecodedCommand decode_record(const MetadataObject &record) {
  validate_metadata(record);
  exact_fields(record, {"schema", "version", "operation", "source", "request",
                        "derived", "destination"});
  if (text_value(record.at("schema")) != schema_name ||
      unsigned_value(record.at("version")) != 2 ||
      text_value(record.at("operation")) != orientation_operation)
    throw std::invalid_argument("Unsupported axis orientation command schema/version/operation");
  if (text_value(record.at("destination")) != "new_document")
    throw std::invalid_argument("Axis orientation destination must be new_document");
  auto source = read_source(object_value(record.at("source")));
  auto request = read_request(object_value(record.at("request")));
  // Original claims are recomputed from raw metadata before any replacement
  // owner/request is consulted. No Dataset/provider, output or callback exists.
  try {
    const auto layout = describe_axis_range_orientation(
        source.shape, source.pixel_type, source.calibration, request);
    validate_layout(object_value(record.at("derived")), layout);
  } catch (const std::out_of_range &error) {
    throw std::invalid_argument(error.what());
  }
  return {std::move(source), std::move(request)};
}

} // namespace

ResolvedAxisOrientationCommand::ResolvedAxisOrientationCommand(
    DocumentSnapshot source, AxisRangeOrientationRequest request,
    AxisExtractionLayout layout, std::string operation, MetadataObject record)
    : source_(std::move(source)), request_(std::move(request)),
      layout_(std::move(layout)), operation_(std::move(operation)),
      record_(std::move(record)) {}

ResolvedAxisOrientationCommand resolve_axis_orientation_command(
    const DocumentSnapshot& source, const AxisRangeOrientationRequest& request) {
  auto result=detail::orientation_descriptor(detail::axis_command_record::source_metadata(source),request);
  const std::string operation=detail::axis_command_record::text_value(result.second.at("operation"));
  return {source, request, std::move(result.first), operation, std::move(result.second)};
}
std::pair<AxisExtractionLayout,MetadataObject> detail::orientation_descriptor(
    const axis_command_record::SourceRecord& metadata, const AxisRangeOrientationRequest& request) {

  auto layout = describe_axis_range_orientation(
      metadata.shape, metadata.pixel_type, metadata.calibration, request);
  const std::string operation = orientation_operation;
  MetadataObject record{
      {"schema", std::string(schema_name)}, {"version", std::int64_t(2)},
      {"operation", operation},
      {"source", metadata_object(source_record(metadata))},
      {"request", metadata_object(request_record(request))},
      {"derived", metadata_object(layout_record(layout))},
      {"destination", std::string("new_document")}};
  return {std::move(layout),std::move(record)};
}
AxisRangeOrientationRequest detail::orientation_descriptor_request(const MetadataObject& record) {
  return decode_record(record).request;
}

void validate_axis_orientation_command_record(const MetadataObject &record) {
  (void)decode_record(record);
}

ResolvedAxisOrientationCommand axis_orientation_command_from_record(
    const DocumentSnapshot &source, const MetadataObject &record) {
  const auto decoded = decode_record(record);
  const auto current = source_metadata(source);
  if (decoded.source.document_id != current.document_id)
    throw std::invalid_argument("Axis orientation command differs from its source owner");
  if (!current.revisions.matches(decoded.source.revisions,
                                 Change::Pixels | Change::Metadata))
    throw RevisionConflict();
  if (decoded.source.shape.dimensions() != current.shape.dimensions() ||
      decoded.source.pixel_type != current.pixel_type ||
      decoded.source.sample_policy != current.sample_policy ||
      !same_calibration(decoded.source.calibration, current.calibration))
    throw std::invalid_argument("Axis orientation command differs from its source metadata");
  return resolve_axis_orientation_command(source, decoded.request);
}

ResolvedAxisOrientationCommand rebind_axis_orientation_command(
    const DocumentSnapshot &new_source, const MetadataObject &record,
    const AxisRangeOrientationRequest &explicit_request) {
  // Complete replacement geometry cannot excuse invalid original V2 claims.
  (void)decode_record(record);
  return resolve_axis_orientation_command(new_source, explicit_request);
}

PreparedOperation prepare_axis_orientation(
    const ResolvedAxisOrientationCommand &command,
    const ExecutionControl &control, std::size_t execution_max_working_bytes) {
  if (!execution_max_working_bytes)
    throw std::invalid_argument("Axis orientation execution cap must be positive");
  auto request = command.request();
  request.range.max_working_bytes =
      std::min(request.range.max_working_bytes, execution_max_working_bytes);
  // Tightened-cap admission precedes installation of user callbacks inside the
  // unchanged preparation path. The captured command/record is not rewritten.
  (void)describe_axis_range_orientation(command.source().data().dataset, request);
  return prepare_axis_range_orientation(command.source(), request, control);
}

} // namespace imagejxx
