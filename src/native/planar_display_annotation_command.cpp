#include "axis_command_record.hpp"
#include "display_annotation_internal.hpp"
#include <imagejxx/planar_display_annotation_command.hpp>
#include <limits>
#include <algorithm>
namespace imagejxx {
namespace {
using namespace detail::axis_command_record;
const char *operation_name(PlanarDisplayAnnotationOperation operation) {
  switch (operation) {
  case PlanarDisplayAnnotationOperation::Store:
    return "store";
  case PlanarDisplayAnnotationOperation::Update:
    return "update";
  case PlanarDisplayAnnotationOperation::Delete:
    return "delete";
  }
  throw std::invalid_argument("Invalid planar annotation operation");
}
std::vector<PlanarDisplayAnnotation>
apply(DatasetShape shape, std::vector<PlanarDisplayAnnotation> rows,
      const PlanarDisplayAnnotationRequest &r) {
  (void)operation_name(r.operation);
  if (r.operation == PlanarDisplayAnnotationOperation::Store) {
    if (r.index || !r.annotation)
      throw std::invalid_argument("Store requires an annotation and no index");
    r.annotation->validate(shape);
    rows.push_back(*r.annotation);
  } else {
    if (!r.index || *r.index >= rows.size())
      throw std::out_of_range("Select a planar annotation");
    if (r.operation == PlanarDisplayAnnotationOperation::Update) {
      if (!r.annotation)
        throw std::invalid_argument("Update requires a complete annotation");
      r.annotation->validate(shape);
      rows[*r.index] = *r.annotation;
    } else {
      if (r.annotation)
        throw std::invalid_argument(
            "Delete must not contain replacement geometry");
      rows.erase(rows.begin() + *r.index);
    }
  }
  return rows;
}
struct Decoded {
  SourceRecord source;
  PlanarDisplayAnnotationRequest request;
  MetadataValue before;
};
Decoded decode(const MetadataObject &record) {
  detail::preflight_display_annotation_record(record);
  exact_fields(record, {"schema", "version", "source", "operation", "index",
                        "annotation", "before", "after", "destination"});
  const auto version=unsigned_value(record.at("version"));
  if (text_value(record.at("schema")) !=
          "imagejxx.planar-display-annotation-command" ||
      (version!=1 && version!=2) ||
      text_value(record.at("destination")) != "in_place")
    throw std::invalid_argument("Unsupported planar annotation command");
  bool affine=false;
  for(const auto *key:{"annotation","before","after"})if(!std::holds_alternative<std::monostate>(record.at(key))) {
    const auto &nested=object_value(record.at(key));const auto it=nested.find("version");
    if(it!=nested.end() && unsigned_value(it->second)==2)affine=true;
  }
  if((version==2)!=affine)throw std::invalid_argument("Unsupported planar annotation command");
  auto source = read_source(object_value(record.at("source")));
  const auto &operation = text_value(record.at("operation"));
  PlanarDisplayAnnotationRequest request;
  if (operation == "store")
    request.operation = PlanarDisplayAnnotationOperation::Store;
  else if (operation == "update")
    request.operation = PlanarDisplayAnnotationOperation::Update;
  else if (operation == "delete")
    request.operation = PlanarDisplayAnnotationOperation::Delete;
  else
    throw std::invalid_argument("Invalid planar annotation operation");
  if (!std::holds_alternative<std::monostate>(record.at("index"))) {
    const auto value = unsigned_value(record.at("index"));
    if (value > std::numeric_limits<std::size_t>::max())
      throw std::invalid_argument("Annotation index overflow");
    request.index = static_cast<std::size_t>(value);
  }
  if (!std::holds_alternative<std::monostate>(record.at("annotation"))) {
    auto row = decode_planar_display_annotations(source.shape,
                                                 record.at("annotation"));
    if (row.size() != 1)
      throw std::invalid_argument(
          "Command requires exactly one complete annotation");
    request.annotation = std::move(row[0]);
  }
  auto before =
      decode_planar_display_annotations(source.shape, record.at("before"));
  const auto expected = encode_planar_display_annotations(
      source.shape, apply(source.shape, std::move(before), request));
  // Decode every derived claim before comparing the original equation.
  (void)decode_planar_display_annotations(source.shape, record.at("after"));
  if (!metadata_equal(expected, record.at("after")))
    throw std::invalid_argument("Planar annotation command equation differs");
  return {std::move(source), std::move(request), record.at("before")};
}
} // namespace
PlanarDisplayAnnotationRequest
decode_planar_display_annotation_request(DatasetShape shape,
                                         const MetadataObject &record) {
  detail::preflight_display_annotation_record(record);
  exact_fields(record, {"operation", "index", "annotation"});
  const auto &name = text_value(record.at("operation"));
  PlanarDisplayAnnotationRequest request;
  if (name == "store")
    request.operation = PlanarDisplayAnnotationOperation::Store;
  else if (name == "update")
    request.operation = PlanarDisplayAnnotationOperation::Update;
  else if (name == "delete")
    request.operation = PlanarDisplayAnnotationOperation::Delete;
  else
    throw std::invalid_argument("Invalid planar annotation operation");
  if (!std::holds_alternative<std::monostate>(record.at("index"))) {
    const auto value = unsigned_value(record.at("index"));
    if (value > std::numeric_limits<std::size_t>::max())
      throw std::invalid_argument("Annotation index overflow");
    request.index = static_cast<std::size_t>(value);
  }
  if (!std::holds_alternative<std::monostate>(record.at("annotation"))) {
    auto rows =
        decode_planar_display_annotations(shape, record.at("annotation"));
    if (rows.size() != 1)
      throw std::invalid_argument("Request requires one annotation");
    request.annotation = std::move(rows[0]);
  }
  return request;
}
ResolvedPlanarDisplayAnnotationCommand
resolve_planar_display_annotation_command(
    const DocumentSnapshot &source,
    const PlanarDisplayAnnotationRequest &request) {
  const auto descriptor = source_metadata(source);
  const auto rows = planar_display_annotations(source.data());
  const auto after = encode_planar_display_annotations(
      descriptor.shape, apply(descriptor.shape, rows, request));
  const bool affine=std::any_of(rows.begin(),rows.end(),[](const auto &row){return row.has_transform();}) ||
      (request.annotation && request.annotation->has_transform());
  MetadataObject record{
      {"schema", std::string("imagejxx.planar-display-annotation-command")},
      {"version", affine?2:1},
      {"source", metadata_object(source_record(descriptor))},
      {"operation", std::string(operation_name(request.operation))},
      {"index", request.index ? MetadataValue(std::uint64_t(*request.index))
                              : MetadataValue{}},
      {"annotation", request.annotation
                         ? encode_planar_display_annotations(
                               descriptor.shape, {*request.annotation})
                         : MetadataValue{}},
      {"before", encode_planar_display_annotations(descriptor.shape, rows)},
      {"after", after},
      {"destination", std::string("in_place")}};
  detail::preflight_display_annotation_record(record);
  return {source, request, std::move(record)};
}
void validate_planar_display_annotation_command_record(
    const MetadataObject &record) {
  (void)decode(record);
}
ResolvedPlanarDisplayAnnotationCommand
planar_display_annotation_command_from_record(const DocumentSnapshot &source,
                                              const MetadataObject &record) {
  const auto decoded = decode(record);
  const auto current = source_metadata(source);
  if (current.document_id != decoded.source.document_id)
    throw std::invalid_argument("Annotation command belongs to another source");
  if (!current.revisions.matches(decoded.source.revisions,
                                 Change::Pixels | Change::Metadata))
    throw RevisionConflict();
  if (current.shape.dimensions() != decoded.source.shape.dimensions() ||
      current.pixel_type != decoded.source.pixel_type ||
      current.sample_policy != decoded.source.sample_policy ||
      !same_calibration(current.calibration, decoded.source.calibration) ||
      !metadata_equal(
          encode_planar_display_annotations(
              current.shape, planar_display_annotations(source.data())),
          decoded.before))
    throw std::invalid_argument(
        "Annotation source differs from captured metadata");
  return resolve_planar_display_annotation_command(source, decoded.request);
}
ResolvedPlanarDisplayAnnotationCommand rebind_planar_display_annotation_command(
    const DocumentSnapshot &source, const MetadataObject &record,
    const PlanarDisplayAnnotationRequest &request) {
  (void)decode(record);
  return resolve_planar_display_annotation_command(source, request);
}
DocumentTransaction prepare_planar_display_annotation(
    const ResolvedPlanarDisplayAnnotationCommand &command) {
  const auto &r = command.request();
  switch (r.operation) {
  case PlanarDisplayAnnotationOperation::Store:
    return prepare_store_planar_display_annotation(command.source(),
                                                   *r.annotation);
  case PlanarDisplayAnnotationOperation::Update:
    return prepare_update_planar_display_annotation(command.source(), *r.index,
                                                    *r.annotation);
  case PlanarDisplayAnnotationOperation::Delete:
    return prepare_delete_planar_display_annotation(command.source(), *r.index);
  }
  throw std::invalid_argument("Invalid planar annotation operation");
}
} // namespace imagejxx
