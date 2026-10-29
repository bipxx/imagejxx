#include <imagejxx/imagej_projection_command.hpp>
#include "calibration_preparation_internal.hpp"
#include "calibrated_descriptor_internal.hpp"
#include <imagejxx/imagej_intensity_calibration.hpp>
#include "projection_record_internal.hpp"
#include "contrast_command_record.hpp"
#include <algorithm>
namespace imagejxx {
namespace {
using namespace detail::axis_command_record;
using detail::contrast_command_record::add;
using detail::contrast_command_record::mul;
using detail::contrast_command_record::Budget;
constexpr const char* schema="imagejxx.imagej-projection-command";
const DatasetShape lut_shape{1,1,1,1,1,1};
bool flag(const MetadataValue& v) {
  if(const auto p=std::get_if<bool>(&v)) return *p;
  throw std::invalid_argument("Projection flag requires Boolean");
}
const char* axis_name(ImageJProjectionAxis a) {
  switch(a) {case ImageJProjectionAxis::ImageJDefault:return "automatic";
    case ImageJProjectionAxis::Z:return "z";case ImageJProjectionAxis::Time:return "time";
    case ImageJProjectionAxis::Channels:return "channels";}
  throw std::invalid_argument("Invalid projection axis");
}
ImageJProjectionAxis axis(const MetadataValue& v) {
  const auto& s=text_value(v);
  if(s=="automatic")return ImageJProjectionAxis::ImageJDefault;
  if(s=="z")return ImageJProjectionAxis::Z;
  if(s=="time")return ImageJProjectionAxis::Time;
  if(s=="channels")return ImageJProjectionAxis::Channels;
  throw std::invalid_argument("Unknown projection axis");
}
MetadataValue lut_record(const std::optional<ChannelDisplayLut>& lut) {
  if(!lut) return {};
  ChannelDisplayState state;state.mode=ChannelDisplayMode::Color;
  state.channels.resize(1);state.channels[0].lut=*lut;
  const auto record=encode_channel_display(state,lut_shape,PixelType::UInt8,FloatingSamplePolicy::FiniteOnly);
  return object_value(array_value(record.at("channels"))[0]).at("lut");
}
std::optional<ChannelDisplayLut> read_lut(const MetadataValue& v) {
  if(std::holds_alternative<std::monostate>(v)) return {};
  MetadataObject state{{"schema",std::string("imagejxx.channel-display")},{"version",1},
    {"mode",std::string("color")},{"channels",metadata_array({metadata_object({
      {"visible",true},{"range",metadata_array({0,255})},{"lut",v}})})}};
  return parse_channel_display(state,lut_shape,PixelType::UInt8,FloatingSamplePolicy::FiniteOnly).channels[0].lut;
}
void density_defaults(const ImageJProjectionDocumentRequest& r) {
  if(r.projection.method.size()>64 || (r.output_title && r.output_title->size()>1024*1024) ||
      (r.source_lut && r.source_lut->name.size()>32))
    throw std::invalid_argument("Projection request text exceeds its limit");
  if(r.projection.signed16_density || !r.projection.calibration_table.empty())
    throw std::invalid_argument("Prepared projection density comes from the source document");
  if(!r.max_working_bytes || !r.max_record_bytes || r.max_record_bytes>16ULL*1024*1024 || !r.projection.max_working_bytes ||
      !r.projection.max_output_pixel_bytes || !r.display_limits.max_pixels || !r.display_limits.max_working_bytes)
    throw std::invalid_argument("Projection requires positive memory limits");
}
ImageJDatasetProjectionLayout layout(const detail::axis_command_record::SourceRecord& s,
    const ImageJProjectionDocumentRequest& r,const ImageJIntensityCalibration& intensity,bool portable=false) {
  density_defaults(r);auto p=r.projection;
  apply_imagej_intensity_calibration(intensity,s.pixel_type,s.shape.s,p);
  if(portable)p.max_working_bytes=std::numeric_limits<std::size_t>::max();
  return s.shape.s==3?describe_imagej_rgb_projection(s.shape,s.pixel_type,s.calibration,p):
      describe_imagej_dataset_projection(s.shape,s.pixel_type,s.calibration,p);
}
MetadataObject derived(const ImageJDatasetProjectionLayout& l) {
  MetadataArray shape;for(auto n:l.shape.dimensions())shape.emplace_back(n);
  return {{"shape",metadata_array(std::move(shape))},{"pixel_type",std::string(pixel_type_name(l.pixel_type))},
    {"calibration",metadata_object(calibration_record(l.calibration))},{"axis",std::string(axis_name(l.axis))},
    {"source_first",l.source_first},{"source_first_t",l.source_first_t},{"source_first_c",l.source_first_c},
    {"contributors_per_group",l.contributors_per_group},{"output_pixel_bytes",std::uint64_t(l.output_pixel_bytes)},
    {"preserve_density_calibration",l.preserve_density_calibration}};
}
void presentation(const MetadataObject& properties,const SourceRecord& s) {
  if(auto p=properties.find("channel_display");p!=properties.end())
    (void)parse_channel_display(object_value(p->second),s.shape,s.pixel_type,s.sample_policy);
}
struct Decoded {
  SourceRecord source;ImageJProjectionDocumentRequest request;
  std::uint64_t selection,display;
};
Decoded decode(const MetadataObject& record) {
  // Bound wire/object traversal before cloning a request or density table.
  Budget initial{16ULL*1024*1024};initial.object(record);
  exact_fields(record,{"schema","version","profile","operation","source","selection_revision",
    "display_revision","intensity","presentation","request","derived","destination"});
  if(text_value(record.at("schema"))!=schema || unsigned_value(record.at("version"))!=1 ||
      text_value(record.at("profile"))!="imagej154r" || text_value(record.at("operation"))!="project_imagej" ||
      text_value(record.at("destination"))!="new_document")
    throw std::invalid_argument("Unsupported ImageJ projection command grammar");
  auto s=read_source(object_value(record.at("source")));
  auto r=imagej_projection_request_from_record(object_value(record.at("request")));
  Budget bounded{r.max_record_bytes};bounded.object(record);
  const auto intensity=ImageJIntensityCalibration::from_record(record.at("intensity"));
  intensity.validate_for(s.pixel_type,s.shape.s);
  presentation(object_value(record.at("presentation")),s);
  if(!metadata_equal(derived(layout(s,r,intensity,true)),object_value(record.at("derived"))))
    throw std::invalid_argument("Projection derived claims contradict source/request");
  return {std::move(s),std::move(r),unsigned_value(record.at("selection_revision")),
    unsigned_value(record.at("display_revision"))};
}
} // namespace
MetadataObject imagej_projection_request_record(const ImageJProjectionDocumentRequest& r) {
  density_defaults(r);const auto& p=r.projection;
  return {{"axis",std::string(axis_name(p.axis))},{"first",p.first},{"last",p.last},{"group_size",p.group_size},
    {"active_t",p.active_t},{"active_c",p.active_c},{"all_time_frames",p.all_time_frames},
    {"all_channels",p.all_channels},{"method",p.method},{"clip_when_summing",p.clip_when_summing},
    {"tile_width",p.tile_width},{"tile_height",p.tile_height},
    {"max_output_pixel_bytes",std::uint64_t(p.max_output_pixel_bytes)},
    {"max_reduction_working_bytes",std::uint64_t(p.max_working_bytes)},
    {"max_display_pixels",std::uint64_t(r.display_limits.max_pixels)},
    {"max_display_working_bytes",std::uint64_t(r.display_limits.max_working_bytes)},
    {"max_working_bytes",std::uint64_t(r.max_working_bytes)},
    {"max_record_bytes",std::uint64_t(r.max_record_bytes)},
    {"desktop_grays",r.desktop_grays},{"source_lut",lut_record(r.source_lut)},
    {"output_title",r.output_title?MetadataValue(*r.output_title):MetadataValue{}}};
}
ImageJProjectionDocumentRequest imagej_projection_request_from_record(const MetadataObject& o) {
  exact_fields(o,{"axis","first","last","group_size","active_t","active_c","all_time_frames","all_channels",
    "method","clip_when_summing","tile_width","tile_height","max_output_pixel_bytes","max_reduction_working_bytes",
    "max_display_pixels","max_display_working_bytes","max_working_bytes","max_record_bytes","desktop_grays",
    "source_lut","output_title"});
  const auto record_cap=size_value(o.at("max_record_bytes"));
  if(record_cap>16ULL*1024*1024)throw std::invalid_argument("Projection record limit exceeds16MiB");
  Budget raw{record_cap};raw.object(o);
  ImageJProjectionDocumentRequest r;auto& p=r.projection;
  p.axis=axis(o.at("axis"));p.first=index_value(o.at("first"));p.last=index_value(o.at("last"));
  p.group_size=index_value(o.at("group_size"));p.active_t=index_value(o.at("active_t"));p.active_c=index_value(o.at("active_c"));
  p.all_time_frames=flag(o.at("all_time_frames"));p.all_channels=flag(o.at("all_channels"));
  p.method=text_value(o.at("method"));p.clip_when_summing=flag(o.at("clip_when_summing"));
  p.tile_width=index_value(o.at("tile_width"),true);p.tile_height=index_value(o.at("tile_height"),true);
  p.max_output_pixel_bytes=size_value(o.at("max_output_pixel_bytes"));
  p.max_working_bytes=size_value(o.at("max_reduction_working_bytes"));
  r.display_limits.max_pixels=size_value(o.at("max_display_pixels"));
  r.display_limits.max_working_bytes=size_value(o.at("max_display_working_bytes"));
  r.max_working_bytes=size_value(o.at("max_working_bytes"));r.max_record_bytes=size_value(o.at("max_record_bytes"));
  r.desktop_grays=flag(o.at("desktop_grays"));r.source_lut=read_lut(o.at("source_lut"));
  if(!std::holds_alternative<std::monostate>(o.at("output_title")))r.output_title=text_value(o.at("output_title"));
  density_defaults(r);return r;
}
ImageJProjectionDocumentEstimate estimate_imagej_projection_document(
    const DocumentSnapshot& source,const ImageJProjectionDocumentRequest& r) {
  return detail::estimate_calibrated_projection(source,nullptr,r);
}
ImageJProjectionDocumentEstimate detail::estimate_calibrated_projection(
    const DocumentSnapshot& source,const EffectiveCalibration* context,
    const ImageJProjectionDocumentRequest& r) {
  density_defaults(r);
  ImageJProjectionDocumentEstimate e;
  e.metadata_bytes=detail::contrast_command_record::source_budget(source,std::min(r.max_record_bytes,r.max_working_bytes));
  Budget extras{std::min(r.max_record_bytes,r.max_working_bytes)};
  extras.text(r.projection.method);
  if(r.output_title)extras.text(*r.output_title);
  if(r.source_lut)extras.text(r.source_lut->name);
  extras.object(imagej_projection_request_record(r));
  const auto intensity=context?context->intensity():read_imagej_intensity_calibration(source);
  if(context) {
    extras.value(intensity.record(),0);
    extras.text(context->spatial().unit);extras.text(context->spatial().time_unit);
  }
  e.metadata_bytes=add(e.metadata_bytes,extras.used);
  auto s=source_metadata(source);
  if(context)s.calibration=context->spatial();
  presentation(source.display().properties,s);
  e.layout=layout(s,r,intensity);
  const auto pixels=mul(std::size_t(e.layout.shape.x),std::size_t(e.layout.shape.y));
  // Existing escaped-metadata/geometry bound covers transaction, preview,
  // fresh-owner, frontend wire/record staging and publication copies. Numeric
  // layout already charges output, one provider fallback, tile and executor table.
  // Output channel state can grow even without source presentation metadata;
  // charge its native/wire/publication copies separately per output C.
  const auto table=intensity.mode()==ImageJIntensityMode::Lookup?
      mul(intensity.bit_depth()==8?256:65536,sizeof(std::uint32_t)):0;
  e.working_bytes=add(e.layout.working_bytes,add(table,add(mul(e.metadata_bytes,32),
      add(1024*1024,add(mul(pixels,8*sizeof(double)),add(mul(e.layout.shape.plane_count(),1024),
          mul(e.layout.shape.c,16384)))))));
  if(e.working_bytes>r.max_working_bytes)throw std::invalid_argument("Projection exceeds processing memory budget");
  if(s.shape.s==1 && (pixels>r.display_limits.max_pixels ||
      add(mul(pixels,sizeof(double)),mul(e.layout.shape.c,sizeof(ChannelDisplayChannel)+8+
          sizeof(ChannelDisplayRange)+2*sizeof(int)))>r.display_limits.max_working_bytes))
    throw std::invalid_argument("Projection exceeds display working budget");
  return e;
}
ResolvedImageJProjectionCommand resolve_imagej_projection_command(
    const DocumentSnapshot& source,const ImageJProjectionDocumentRequest& r) {
  auto result=detail::projection_descriptor(source,nullptr,r);
  return {source,r,std::move(result.first),std::move(result.second)};
}
std::pair<ImageJProjectionDocumentEstimate,MetadataObject> detail::projection_descriptor(
    const DocumentSnapshot& source,const EffectiveCalibration* context,
    const ImageJProjectionDocumentRequest& r) {
  auto e=estimate_calibrated_projection(source,context,r);
  auto metadata=source_metadata(source);
  if(context)metadata.calibration=context->spatial();
  MetadataObject record{{"schema",std::string(schema)},{"version",1},{"profile",std::string("imagej154r")},
    {"operation",std::string("project_imagej")},{"source",metadata_object(source_record(metadata))},
    {"selection_revision",source.revisions().selection},{"display_revision",source.revisions().display},
    {"intensity",(context?context->intensity():read_imagej_intensity_calibration(source)).record()},
    {"presentation",metadata_object(source.display().properties)},
    {"request",metadata_object(imagej_projection_request_record(r))},{"derived",metadata_object(derived(e.layout))},
    {"destination",std::string("new_document")}};
  Budget budget{r.max_record_bytes};budget.object(record);
  return {std::move(e),std::move(record)};
}
ImageJProjectionDocumentRequest detail::projection_descriptor_request(const MetadataObject& record) {
  return decode(record).request;
}
void validate_imagej_projection_command_record(const MetadataObject& record) {(void)decode(record);}
ResolvedImageJProjectionCommand imagej_projection_command_from_record(
    const DocumentSnapshot& source,const MetadataObject& record) {
  const auto d=decode(record);const auto s=source_metadata(source);
  if(s.document_id!=d.source.document_id)throw std::invalid_argument("Projection source owner differs");
  auto revisions=d.source.revisions;revisions.selection=d.selection;revisions.display=d.display;
  if(!source.revisions().matches(revisions,Change::All))throw RevisionConflict();
  if(s.shape.dimensions()!=d.source.shape.dimensions() || s.pixel_type!=d.source.pixel_type ||
      s.sample_policy!=d.source.sample_policy || !same_calibration(s.calibration,d.source.calibration) ||
      !metadata_equal(read_imagej_intensity_calibration(source).record(),record.at("intensity")) ||
      !metadata_equal(source.display().properties,object_value(record.at("presentation"))))
    throw std::invalid_argument("Projection captured source metadata differs");
  return resolve_imagej_projection_command(source,d.request);
}
ResolvedImageJProjectionCommand rebind_imagej_projection_command(const DocumentSnapshot& source,
    const MetadataObject& original,const ImageJProjectionDocumentRequest& r) {
  (void)decode(original);return resolve_imagej_projection_command(source,r);
}
PreparedOperation prepare_imagej_projection(const ResolvedImageJProjectionCommand& command,
    const ExecutionControl& control,std::size_t cap) {
  if(!cap)throw std::invalid_argument("Projection runtime cap must be positive");
  auto request=command.request();request.max_working_bytes=std::min(request.max_working_bytes,cap);
  return prepare_imagej_projection(command.source(),request,control);
}
} // namespace imagejxx
