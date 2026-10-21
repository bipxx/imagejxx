#include <imagejxx/axis_spacing_command.hpp>
#include "axis_command_record.hpp"
#include <algorithm>
#include <utility>

namespace imagejxx {
namespace {
using namespace detail::axis_command_record;
bool boolean(const MetadataValue &value) {
  if (const auto flag = std::get_if<bool>(&value)) return *flag;
  throw std::invalid_argument("Physical spacing flags require Booleans");
}
MetadataObject request_record(const AxisRangeSpacingRequest &request) {
  const auto &o=request.orientation;const auto &r=o.range;
  return {{"plane",std::string(plane_name(r.plane))},{"first",r.first},{"last",r.last},
      {"step",r.step},{"t",r.t},{"c",r.c},{"max_working_bytes",std::uint64_t(r.max_working_bytes)},
      {"reverse_order",o.reverse_order},{"flip_vertical",o.flip_vertical},{"transpose",o.transpose},
      {"physical_vertical_spacing",request.physical_vertical_spacing},
      {"sampling",std::string(request.sampling==AxisRangeSampling::Nearest?"nearest":"linear")}};
}
MetadataObject grid_record(const AxisRangeSpacingEstimate &grid) {
  return {{"vertical_count",grid.vertical_count},{"last_selected",grid.last_selected},
      {"effective_first",grid.effective_first},{"effective_last",grid.effective_last},
      {"source_vertical_spacing",grid.source_vertical_spacing},{"voxel_step",grid.voxel_step},
      {"output_axes",metadata_array({grid.output_axes[0],grid.output_axes[1],grid.output_axes[2]})}};
}
void validate_grid(const MetadataObject &record,const AxisRangeSpacingEstimate &grid) {
  exact_fields(record,{"vertical_count","last_selected","effective_first","effective_last",
      "source_vertical_spacing","voxel_step","output_axes"});
  if (index_value(record.at("vertical_count"),true)!=grid.vertical_count ||
      index_value(record.at("last_selected"))!=grid.last_selected ||
      index_value(record.at("effective_first"))!=grid.effective_first ||
      index_value(record.at("effective_last"))!=grid.effective_last ||
      number_value(record.at("source_vertical_spacing"))!=grid.source_vertical_spacing ||
      number_value(record.at("voxel_step"))!=grid.voxel_step)
    throw std::invalid_argument("Physical grid record differs from its request");
  // JSON/Python adapters preserve the public legacy string-list alternative.
  // It represents the same array of text as an immutable MetadataTree node.
  if (const auto axes=std::get_if<std::vector<std::string>>(&record.at("output_axes"))) {
    if (axes->size()!=3 || !std::equal(axes->begin(),axes->end(),grid.output_axes.begin()))
      throw std::invalid_argument("Physical grid output axes differ");
    return;
  }
  const auto &axes=array_value(record.at("output_axes"));
  if (axes.size()!=3)throw std::invalid_argument("Physical grid requires three output axes");
  for (std::size_t i=0;i<3;++i)
    if(text_value(axes[i])!=grid.output_axes[i])throw std::invalid_argument("Physical grid output axes differ");
}
struct Decoded {SourceRecord source;AxisRangeSpacingRequest request;};
Decoded decode(const MetadataObject &record) {
  validate_metadata(record);
  exact_fields(record,{"schema","version","operation","source","request","derived","grid","destination"});
  if(text_value(record.at("schema"))!=schema_name || unsigned_value(record.at("version"))!=3 ||
      text_value(record.at("operation"))!="axis_range_spacing" || text_value(record.at("destination"))!="new_document")
    throw std::invalid_argument("Unsupported physical-spacing command schema/version/operation");
  auto source=read_source(object_value(record.at("source")));
  const auto &r=object_value(record.at("request"));
  exact_fields(r,{"plane","first","last","step","t","c","max_working_bytes",
      "reverse_order","flip_vertical","transpose","physical_vertical_spacing","sampling"});
  AxisRangeSpacingRequest request;
  request.orientation.range={read_plane(r.at("plane")),index_value(r.at("first")),index_value(r.at("last")),
      index_value(r.at("step"),true),index_value(r.at("t")),index_value(r.at("c")),size_value(r.at("max_working_bytes"))};
  request.orientation.reverse_order=boolean(r.at("reverse_order"));
  request.orientation.flip_vertical=boolean(r.at("flip_vertical"));
  request.orientation.transpose=boolean(r.at("transpose"));
  request.physical_vertical_spacing=number_value(r.at("physical_vertical_spacing"));
  const auto &sampling=text_value(r.at("sampling"));
  if(sampling!="nearest" && sampling!="linear")throw std::invalid_argument("Invalid physical spacing sampling");
  request.sampling=sampling=="nearest"?AxisRangeSampling::Nearest:AxisRangeSampling::Linear;
  const auto layout=describe_axis_spacing(source.shape,source.pixel_type,source.calibration,source.sample_policy,request);
  validate_layout(object_value(record.at("derived")),layout.output);
  validate_grid(object_value(record.at("grid")),layout.grid);
  return {std::move(source),request};
}
}
ResolvedAxisSpacingCommand::ResolvedAxisSpacingCommand(DocumentSnapshot source,AxisRangeSpacingRequest request,
    AxisSpacingLayout layout,MetadataObject record)
    :source_(std::move(source)),request_(request),layout_(std::move(layout)),record_(std::move(record)) {}
ResolvedAxisSpacingCommand resolve_axis_spacing_command(const DocumentSnapshot &source,const AxisRangeSpacingRequest &request) {
  const auto metadata=detail::axis_command_record::source_metadata(source);
  auto layout=describe_axis_spacing(metadata.shape,metadata.pixel_type,metadata.calibration,metadata.sample_policy,request);
  MetadataObject record{{"schema",std::string(detail::axis_command_record::schema_name)},{"version",3},
      {"operation",std::string("axis_range_spacing")},{"source",metadata_object(detail::axis_command_record::source_record(metadata))},
      {"request",metadata_object(request_record(request))},{"derived",metadata_object(detail::axis_command_record::layout_record(layout.output))},
      {"grid",metadata_object(grid_record(layout.grid))},{"destination",std::string("new_document")}};
  return {source,request,std::move(layout),std::move(record)};
}
void validate_axis_spacing_command_record(const MetadataObject &record) {(void)decode(record);}
ResolvedAxisSpacingCommand axis_spacing_command_from_record(const DocumentSnapshot &source,const MetadataObject &record) {
  const auto decoded=decode(record);const auto current=detail::axis_command_record::source_metadata(source);
  if(current.document_id!=decoded.source.document_id)throw std::invalid_argument("Physical spacing belongs to another source");
  if(!current.revisions.matches(decoded.source.revisions,Change::Pixels|Change::Metadata))throw RevisionConflict();
  if(current.shape.dimensions()!=decoded.source.shape.dimensions() || current.pixel_type!=decoded.source.pixel_type ||
     current.sample_policy!=decoded.source.sample_policy || !detail::axis_command_record::same_calibration(current.calibration,decoded.source.calibration))
    throw std::invalid_argument("Physical spacing source metadata differs");
  return resolve_axis_spacing_command(source,decoded.request);
}
ResolvedAxisSpacingCommand rebind_axis_spacing_command(const DocumentSnapshot &source,const MetadataObject &record,const AxisRangeSpacingRequest &request) {
  (void)decode(record);return resolve_axis_spacing_command(source,request);
}
PreparedOperation prepare_axis_spacing(const ResolvedAxisSpacingCommand &command,const ExecutionControl &control,std::size_t cap) {
  if(!cap)throw std::invalid_argument("Physical spacing execution cap must be positive");
  auto request=command.request();auto &range=request.orientation.range;
  range.max_working_bytes=std::min(range.max_working_bytes,cap);
  (void)estimate_axis_range_spacing(command.source().data().dataset,request);
  return prepare_axis_range_spacing(command.source(),request,control);
}
} // namespace imagejxx
