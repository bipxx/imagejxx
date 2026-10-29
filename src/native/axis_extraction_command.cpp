#include <imagejxx/axis_extraction_command.hpp>
#include "calibrated_descriptor_internal.hpp"
#include "axis_command_record.hpp"

#include <algorithm>
#include <cmath>
#include <initializer_list>
#include <limits>
#include <set>
#include <stdexcept>
#include <type_traits>
#include <utility>

namespace imagejxx {
namespace {

using namespace detail::axis_command_record;
constexpr const char *plane_operation = "axis_plane_export";
constexpr const char *range_operation = "axis_range_reslice";

const char *operation_name(const AxisExtractionRequest &request) {
  return std::holds_alternative<AxisPlaneExportRequest>(request)
             ? plane_operation : range_operation;
}

MetadataObject request_record(const AxisExtractionRequest &request) {
  return std::visit([](const auto &typed) {
    MetadataObject result{
        {"plane", std::string(plane_name(typed.plane))},
        {"t", static_cast<std::int64_t>(typed.t)},
        {"c", static_cast<std::int64_t>(typed.c)},
        {"max_working_bytes", static_cast<std::uint64_t>(typed.max_working_bytes)}};
    if constexpr (std::is_same_v<std::decay_t<decltype(typed)>, AxisPlaneExportRequest>)
      result.emplace("coordinate", static_cast<std::int64_t>(typed.coordinate));
    else {
      result.emplace("first", static_cast<std::int64_t>(typed.first));
      result.emplace("last", static_cast<std::int64_t>(typed.last));
      result.emplace("step", static_cast<std::int64_t>(typed.step));
    }
    return result;
  }, request);
}

AxisExtractionRequest read_request(const std::string &operation,
                                    const MetadataObject &record) {
  if (operation == plane_operation) {
    exact_fields(record, {"plane", "coordinate", "t", "c", "max_working_bytes"});
    return AxisPlaneExportRequest{
        read_plane(record.at("plane")), index_value(record.at("coordinate")),
        index_value(record.at("t")), index_value(record.at("c")),
        size_value(record.at("max_working_bytes"))};
  }
  if (operation == range_operation) {
    exact_fields(record, {"plane", "first", "last", "step", "t", "c",
                          "max_working_bytes"});
    return AxisRangeResliceRequest{
        read_plane(record.at("plane")), index_value(record.at("first")),
        index_value(record.at("last")), index_value(record.at("step"), true),
        index_value(record.at("t")), index_value(record.at("c")),
        size_value(record.at("max_working_bytes"))};
  }
  throw std::invalid_argument("Unsupported axis extraction command operation");
}

AxisExtractionLayout describe(const SourceRecord &source,
                                const AxisExtractionRequest &request) {
  return std::visit([&](const auto &typed) {
    if (!typed.max_working_bytes)
      throw std::invalid_argument("Axis extraction requires a positive working-memory cap");
    if constexpr (std::is_same_v<std::decay_t<decltype(typed)>, AxisPlaneExportRequest>)
      return describe_axis_plane_export(source.shape, source.pixel_type,
                                        source.calibration, typed);
    else
      return describe_axis_range_reslice(source.shape, source.pixel_type,
                                         source.calibration, typed);
  }, request);
}

struct DecodedCommand {
  SourceRecord source;
  AxisExtractionRequest request;
};

DecodedCommand decode_record(const MetadataObject &record) {
  validate_metadata(record);
  exact_fields(record, {"schema", "version", "operation", "source", "request",
                        "derived", "destination"});
  if (text_value(record.at("schema")) != schema_name ||
      unsigned_value(record.at("version")) != 1)
    throw std::invalid_argument("Unsupported axis extraction command schema/version");
  if (text_value(record.at("destination")) != "new_document")
    throw std::invalid_argument("Axis extraction destination must be new_document");
  auto source = read_source(object_value(record.at("source")));
  auto request = read_request(text_value(record.at("operation")),
                              object_value(record.at("request")));
  // Recompute from the original raw metadata. No manufactured Dataset/provider
  // and no new source may supply missing or contradictory original claims.
  try {
    const auto layout = describe(source, request);
    validate_layout(object_value(record.at("derived")), layout);
  } catch (const std::out_of_range &error) {
    throw std::invalid_argument(error.what());
  }
  return {std::move(source), std::move(request)};
}

} // namespace

ResolvedAxisExtractionCommand::ResolvedAxisExtractionCommand(
    DocumentSnapshot source, AxisExtractionRequest request,
    AxisExtractionLayout layout, std::string operation, MetadataObject record)
    : source_(std::move(source)), request_(std::move(request)),
      layout_(std::move(layout)), operation_(std::move(operation)),
      record_(std::move(record)) {}

ResolvedAxisExtractionCommand resolve_axis_extraction_command(
    const DocumentSnapshot& source, const AxisExtractionRequest& request) {
  auto result=detail::extraction_descriptor(detail::axis_command_record::source_metadata(source),request);
  const std::string operation=detail::axis_command_record::text_value(result.second.at("operation"));
  return {source, request, std::move(result.first), operation, std::move(result.second)};
}
std::pair<AxisExtractionLayout,MetadataObject> detail::extraction_descriptor(
    const axis_command_record::SourceRecord& metadata, const AxisExtractionRequest& request) {

  auto layout = describe(metadata, request);
  const std::string operation = operation_name(request);
  MetadataObject record{
      {"schema", std::string(schema_name)}, {"version", std::int64_t(1)},
      {"operation", operation},
      {"source", metadata_object(source_record(metadata))},
      {"request", metadata_object(request_record(request))},
      {"derived", metadata_object(layout_record(layout))},
      {"destination", std::string("new_document")}};
  return {std::move(layout),std::move(record)};
}
AxisExtractionRequest detail::extraction_descriptor_request(const MetadataObject& record) {
  return decode_record(record).request;
}

void validate_axis_extraction_command_record(const MetadataObject &record) {
  (void)decode_record(record);
}

ResolvedAxisExtractionCommand axis_extraction_command_from_record(
    const DocumentSnapshot &source, const MetadataObject &record) {
  const auto decoded = decode_record(record);
  const auto current = source_metadata(source);
  if (decoded.source.document_id != current.document_id)
    throw std::invalid_argument("Axis extraction command differs from its source owner");
  if (!current.revisions.matches(decoded.source.revisions,
                                 Change::Pixels | Change::Metadata))
    throw RevisionConflict();
  if (decoded.source.shape.dimensions() != current.shape.dimensions() ||
      decoded.source.pixel_type != current.pixel_type ||
      decoded.source.sample_policy != current.sample_policy ||
      !same_calibration(decoded.source.calibration, current.calibration))
    throw std::invalid_argument("Axis extraction command differs from its source metadata");
  return resolve_axis_extraction_command(source, decoded.request);
}

ResolvedAxisExtractionCommand rebind_axis_extraction_command(
    const DocumentSnapshot &new_source, const MetadataObject &record,
    const AxisExtractionRequest &explicit_request) {
  // Even an entirely explicit new request cannot excuse a malformed original.
  (void)decode_record(record);
  return resolve_axis_extraction_command(new_source, explicit_request);
}

PreparedOperation prepare_axis_extraction(
    const ResolvedAxisExtractionCommand &command,
    const ExecutionControl &control, std::size_t execution_max_working_bytes) {
  if (!execution_max_working_bytes)
    throw std::invalid_argument("Axis extraction execution cap must be positive");
  return std::visit([&](auto request) {
    request.max_working_bytes =
        std::min(request.max_working_bytes, execution_max_working_bytes);
    if constexpr (std::is_same_v<decltype(request), AxisPlaneExportRequest>) {
      // Reject the tightened cap before installing user callbacks in the
      // preparation's execution scope. The captured command stays immutable.
      (void)describe_axis_plane_export(command.source().data().dataset, request);
      return prepare_axis_plane_export(command.source(), request, control);
    } else {
      (void)describe_axis_range_reslice(command.source().data().dataset, request);
      return prepare_axis_range_reslice(command.source(), request, control);
    }
  }, command.request());
}

} // namespace imagejxx
