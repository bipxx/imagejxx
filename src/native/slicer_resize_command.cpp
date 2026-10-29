#include <imagejxx/slicer_resize_command.hpp>
#include "calibrated_descriptor_internal.hpp"
#include "axis_command_record.hpp"
#include <algorithm>
#include <utility>

namespace imagejxx {
namespace {
using namespace detail::axis_command_record;
bool boolean(const MetadataValue &value) {
  if (const auto flag = std::get_if<bool>(&value)) return *flag;
  throw std::invalid_argument("Slicer resize flags require Booleans");
}
MetadataObject request_record(const AxisRangeSlicerResizeRequest &request) {
  const auto &o=request.orientation;const auto &r=o.range;
  return {{"plane",std::string(plane_name(r.plane))},{"first",r.first},{"last",r.last},
      {"step",r.step},{"t",r.t},{"c",r.c},{"max_working_bytes",std::uint64_t(r.max_working_bytes)},
      {"reverse_order",o.reverse_order},{"flip_vertical",o.flip_vertical},{"transpose",o.transpose}};
}
MetadataObject grid_record(const AxisRangeSlicerResizeEstimate &g) {
  return {{"source_vertical_count",g.source_vertical_count},{"vertical_count",g.vertical_count},
      {"last_selected",g.last_selected},{"effective_first",g.effective_first},{"effective_last",g.effective_last},
      {"z_spacing_ratio",g.z_spacing_ratio},{"processor_vertical_origin",g.processor_vertical_origin},
      {"processor_vertical_step",g.processor_vertical_step},{"resize_branch",std::int64_t(g.resize_branch)},
      {"output_axis_0",g.output_axes[0]},{"output_axis_1",g.output_axes[1]},{"output_axis_2",g.output_axes[2]},
      {"scalar_spacing_0",g.scalar_spacing[0]},{"scalar_spacing_1",g.scalar_spacing[1]},{"scalar_spacing_2",g.scalar_spacing[2]}};
}
void validate_grid(const MetadataObject &record,const AxisRangeSlicerResizeEstimate &grid) {
  if(!metadata_equal(metadata_object(record),metadata_object(grid_record(grid))))
    throw std::invalid_argument("Slicer grid record differs from its request");
}
struct Decoded {SourceRecord source;AxisRangeSlicerResizeRequest request;};
Decoded decode(const MetadataObject &record) {
  validate_metadata(record);
  exact_fields(record,{"schema","version","operation","source","request","derived","grid","destination"});
  if(text_value(record.at("schema"))!=schema_name || unsigned_value(record.at("version"))!=4 ||
      text_value(record.at("operation"))!="axis_range_slicer_resize" || text_value(record.at("destination"))!="new_document")
    throw std::invalid_argument("Unsupported bilinear-resize command schema/version/operation");
  auto source=read_source(object_value(record.at("source")));
  const auto &r=object_value(record.at("request"));
  exact_fields(r,{"plane","first","last","step","t","c","max_working_bytes",
      "reverse_order","flip_vertical","transpose"});
  AxisRangeSlicerResizeRequest request;
  request.orientation.range={read_plane(r.at("plane")),index_value(r.at("first")),index_value(r.at("last")),
      index_value(r.at("step"),true),index_value(r.at("t")),index_value(r.at("c")),size_value(r.at("max_working_bytes"))};
  request.orientation.reverse_order=boolean(r.at("reverse_order"));
  request.orientation.flip_vertical=boolean(r.at("flip_vertical"));
  request.orientation.transpose=boolean(r.at("transpose"));
  const auto layout=describe_slicer_resize(source.shape,source.pixel_type,source.calibration,source.sample_policy,request);
  validate_layout(object_value(record.at("derived")),layout.output);
  validate_grid(object_value(record.at("grid")),layout.grid);
  return {std::move(source),request};
}
}
ResolvedSlicerResizeCommand::ResolvedSlicerResizeCommand(DocumentSnapshot source,AxisRangeSlicerResizeRequest request,
    SlicerResizeLayoutDescriptor layout,MetadataObject record)
    :source_(std::move(source)),request_(request),layout_(std::move(layout)),record_(std::move(record)) {}
ResolvedSlicerResizeCommand resolve_slicer_resize_command(
    const DocumentSnapshot& source, const AxisRangeSlicerResizeRequest& request) {
  auto result=detail::slicer_descriptor(detail::axis_command_record::source_metadata(source),request);
  return {source, request, std::move(result.first), std::move(result.second)};
}
std::pair<SlicerResizeLayoutDescriptor,MetadataObject> detail::slicer_descriptor(
    const axis_command_record::SourceRecord& metadata, const AxisRangeSlicerResizeRequest& request) {

  auto layout=describe_slicer_resize(metadata.shape,metadata.pixel_type,metadata.calibration,metadata.sample_policy,request);
  MetadataObject record{{"schema",std::string(detail::axis_command_record::schema_name)},{"version",4},
      {"operation",std::string("axis_range_slicer_resize")},{"source",metadata_object(detail::axis_command_record::source_record(metadata))},
      {"request",metadata_object(request_record(request))},{"derived",metadata_object(detail::axis_command_record::layout_record(layout.output))},
      {"grid",metadata_object(grid_record(layout.grid))},{"destination",std::string("new_document")}};
  return {std::move(layout),std::move(record)};
}
AxisRangeSlicerResizeRequest detail::slicer_descriptor_request(const MetadataObject& record) {
  return decode(record).request;
}
void validate_slicer_resize_command_record(const MetadataObject &record) {(void)decode(record);}
ResolvedSlicerResizeCommand slicer_resize_command_from_record(const DocumentSnapshot &source,const MetadataObject &record) {
  const auto decoded=decode(record);const auto current=detail::axis_command_record::source_metadata(source);
  if(current.document_id!=decoded.source.document_id)throw std::invalid_argument("Slicer resize belongs to another source");
  if(!current.revisions.matches(decoded.source.revisions,Change::Pixels|Change::Metadata))throw RevisionConflict();
  if(current.shape.dimensions()!=decoded.source.shape.dimensions() || current.pixel_type!=decoded.source.pixel_type ||
     current.sample_policy!=decoded.source.sample_policy || !detail::axis_command_record::same_calibration(current.calibration,decoded.source.calibration))
    throw std::invalid_argument("Slicer resize source metadata differs");
  return resolve_slicer_resize_command(source,decoded.request);
}
ResolvedSlicerResizeCommand rebind_slicer_resize_command(const DocumentSnapshot &source,const MetadataObject &record,const AxisRangeSlicerResizeRequest &request) {
  (void)decode(record);return resolve_slicer_resize_command(source,request);
}
PreparedOperation prepare_slicer_resize(const ResolvedSlicerResizeCommand &command,const ExecutionControl &control,std::size_t cap) {
  if(!cap)throw std::invalid_argument("Slicer resize execution cap must be positive");
  auto request=command.request();auto &range=request.orientation.range;
  range.max_working_bytes=std::min(range.max_working_bytes,cap);
  (void)estimate_axis_range_slicer_resize(command.source().data().dataset,request);
  return prepare_axis_range_slicer_resize(command.source(),request,control);
}
} // namespace imagejxx
