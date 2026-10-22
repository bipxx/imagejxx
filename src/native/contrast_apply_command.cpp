#include <imagejxx/contrast_apply_command.hpp>
#include <imagejxx/channel_display.hpp>
#include "contrast_apply_internal.hpp"
#include "axis_command_record.hpp"
#include "target_record_internal.hpp"
#include "selection_codec.hpp"
#include "session_metadata.hpp"
#include <algorithm>
#include <cmath>

namespace imagejxx {
namespace {
namespace wire=detail::axis_command_record;
namespace scalar=detail::session_metadata;
namespace geometry=detail::selection_codec;
using Json=scalar::Json;
[[noreturn]]void invalid(const char *s){throw std::invalid_argument(s);}
std::size_t add(std::size_t a,std::size_t b){if(b>SIZE_MAX-a)invalid("Apply command size overflow");return a+b;}
std::size_t mul(std::size_t a,std::size_t b){if(b && a>SIZE_MAX/b)invalid("Apply command size overflow");return a*b;}
struct Budget {
  std::size_t remaining,used=0;
  void take(std::size_t n){if(n>remaining)invalid("Apply command metadata budget");remaining-=n;used=add(used,n);}
  void text(const std::string &s){take(add(64,mul(s.size(),6)));}
  void object(const MetadataObject &o,unsigned depth=0){
    if(depth>128)invalid("Apply command metadata depth");take(64);
    for(const auto &[k,v]:o){text(k);value(v,depth+1);}
  }
  void value(const MetadataValue &v,unsigned depth){
    if(depth>128)invalid("Apply command metadata depth");take(64);
    if(const auto p=std::get_if<double>(&v)){if(!std::isfinite(*p))invalid("Apply metadata must be finite");}
    else if(const auto p=std::get_if<std::string>(&v))text(*p);
    else if(const auto p=std::get_if<std::vector<std::string>>(&v)){for(const auto &s:*p)text(s);}
    else if(const auto p=std::get_if<std::shared_ptr<const MetadataTree>>(&v)){
      if(!*p)invalid("Apply command null metadata node");
      if(const auto o=std::get_if<MetadataObject>(&(*p)->value))object(*o,depth);
      else for(const auto &item:std::get<MetadataArray>((*p)->value))value(item,depth+1);
    }
  }
  void selection(const Selection &s){text(s.kind);take(mul(s.points.size(),256));take(mul(s.packed_mask().size(),2));object(s.attributes);}
  void roi(const Roi &r){take(512);text(r.name);selection(r.selection);object(r.attributes);}
};
std::size_t source_budget(const DocumentSnapshot &s,std::size_t limit){
  Budget b{limit};b.take(16384);const auto &d=s.data();
  b.text(d.metadata.title);b.object(d.metadata.properties);b.object(d.metadata.extensions);b.object(d.metadata.record_layout);
  b.object(s.display().properties);b.text(d.dataset.calibration().unit);b.text(d.dataset.calibration().time_unit);
  if(d.selection)b.selection(*d.selection);if(d.previous_selection)b.selection(*d.previous_selection);
  for(const auto &r:d.rois)b.roi(r);for(const auto &o:d.overlays)b.object(o);
  return b.used;
}
void storage(PixelType type,int samples,FloatingSamplePolicy policy){
  if(samples!=1 || (type!=PixelType::UInt8 && type!=PixelType::UInt16) || policy!=FloatingSamplePolicy::FiniteOnly)
    invalid("Apply command requires finite-only scalar UInt8/UInt16");
}
void display_valid(const MetadataObject &properties,DatasetShape shape,PixelType type,FloatingSamplePolicy policy){
  if(const auto p=properties.find("channel_display");p!=properties.end())
    (void)parse_channel_display(wire::object_value(p->second),shape,type,policy);
}
MetadataValue selected(const ResolvedTarget &t){
  const auto &s=t.source().data().selection;
  return t.roi_policy()==RoiPolicy::ActiveSelection && s?scalar::from_json(geometry::encode_selection(*s)):MetadataValue{};
}
MetadataValue rois(const ResolvedTarget &t){
  MetadataArray result;
  if(t.roi_policy()==RoiPolicy::PositionedRois)for(const auto &r:t.source().data().rois)result.push_back(scalar::from_json(geometry::encode_roi(r)));
  return metadata_array(std::move(result));
}
ContrastApplyCommandEstimate estimate(const ResolvedTarget &t,ContrastApplyOptions o,const ContrastApplyCommandLimits &limits){
  const auto &d=t.source().data().dataset;storage(d.pixel_type(),d.shape().s,d.sample_policy());
  ContrastApplyCommandEstimate result;
  result.processing_bytes=detail::contrast_apply_working_bytes(t,o,limits.processing);
  result.pixels=mul(mul(std::size_t(d.shape().x),std::size_t(d.shape().y)),t.position_count());
  result.metadata_bytes=source_budget(t.source(),limits.max_record_bytes);
  result.record_bytes=add(mul(result.metadata_bytes,4),add(65536,mul(t.scope()==TargetScope::Positions?t.position_count():0,1024)));
  if(result.record_bytes>limits.max_record_bytes)invalid("Apply command record allowance exceeds limit");
  // Transaction/preview/source metadata copies, wire intermediates, ROI raster
  // scratch (including rounded paths), and mask-union workspace.
  result.working_bytes=add(result.processing_bytes,add(mul(result.metadata_bytes,24),add(mul(result.record_bytes,4),1024*1024)));
  if(result.working_bytes>limits.processing.max_working_bytes)invalid("Apply command exceeds working limit");
  display_valid(t.source().display().properties,d.shape(),d.pixel_type(),d.sample_policy());
  return result;
}
struct Decoded {
  ContrastApplyOptions options;
  wire::SourceRecord source;
  detail::TargetRecord target;
  std::uint64_t display_revision;
  PlanePosition anchor;
  MetadataObject properties;
  std::optional<Selection> selection;
  std::vector<Roi> rois;
};
Decoded decode(const MetadataObject &record,std::size_t cap){
  Budget{cap}.object(record);validate_metadata(record);
  wire::exact_fields(record,{"schema","schema_version","operation","profile","source","target","display_revision","display_properties","reset_anchor","options","selection","rois","reset"});
  if(wire::text_value(record.at("schema"))!="imagejxx.contrast-apply-command" || wire::unsigned_value(record.at("schema_version"))!=1 ||
      wire::text_value(record.at("operation"))!="contrast_apply" || wire::text_value(record.at("profile"))!="scalar_integer_apply_v1")invalid("Unsupported Apply command record");
  auto source=wire::read_source(wire::object_value(record.at("source")));
  auto target=detail::decode_target_record(wire::object_value(record.at("target")));
  if(target.document_id!=source.document_id || target.source_shape.dimensions()!=source.shape.dimensions() ||
      !target.source_revisions.matches(source.revisions,Change::Pixels|Change::Metadata))invalid("Apply source and target descriptors differ");
  storage(source.pixel_type,source.shape.s,source.sample_policy);
  if(target.request.mode!=ExecutionMode::Planes || target.request.destination!=OutputDestination::InPlace)invalid("Apply record requires in-place XY targets");
  const auto &fields=wire::object_value(record.at("options"));wire::exact_fields(fields,{"minimum","maximum","unsigned16_bits"});
  ContrastApplyOptions options{wire::number_value(fields.at("minimum")),wire::number_value(fields.at("maximum")),wire::index_value(fields.at("unsigned16_bits"))};
  detail::validate_contrast_apply_options(source.pixel_type,source.shape.s,options);
  const auto &reset=wire::object_value(record.at("reset"));wire::exact_fields(reset,{"policy","range","threshold","indexed_palette","channel_plane"});
  const bool observed=source.pixel_type==PixelType::UInt16 && options.unsigned16_bits==0;
  const auto expected=source.pixel_type==PixelType::UInt8?255.:options.unsigned16_bits?double((1u<<options.unsigned16_bits)-1):0.;
  if(wire::text_value(reset.at("policy"))!="imagej_scalar_reset_v1" ||
      wire::text_value(reset.at("threshold"))!="clear" || wire::text_value(reset.at("indexed_palette"))!="preserve" ||
      wire::text_value(reset.at("channel_plane"))!="active_if_affected_else_first")invalid("Apply reset policy differs");
  if(observed){if(!std::holds_alternative<std::monostate>(reset.at("range")))invalid("Apply observed reset cannot claim fixed bounds");}
  else {const auto &range=wire::array_value(reset.at("range"));if(range.size()!=2 || wire::number_value(range[0])!=0 || wire::number_value(range[1])!=expected)invalid("Apply fixed reset bounds differ");}
  const auto &anchor_fields=wire::array_value(record.at("reset_anchor"));
  if(anchor_fields.size()!=3)invalid("Apply reset anchor requires T/C/Z");
  PlanePosition anchor{wire::index_value(anchor_fields[0]),wire::index_value(anchor_fields[1]),wire::index_value(anchor_fields[2])};
  if(anchor.t>=source.shape.t || anchor.c>=source.shape.c || anchor.z>=source.shape.z)invalid("Apply reset anchor out of bounds");
  const auto &properties=wire::object_value(record.at("display_properties"));display_valid(properties,source.shape,source.pixel_type,source.sample_policy);
  SessionLimits session;session.max_json_bytes=cap;session.max_encoded_bytes=cap;session.max_working_bytes=mul(cap,16);session.max_nodes=cap/64;session.max_depth=128;
  std::optional<Selection> selection;
  if(!std::holds_alternative<std::monostate>(record.at("selection"))){
    const auto json=scalar::to_json(record.at("selection"));selection=geometry::decode_selection(json,session);selection->validate(source.shape.x,source.shape.y);
    if(geometry::encode_selection(*selection)!=json)invalid("Noncanonical Apply selection");
  }
  if(target.request.roi!=RoiPolicy::ActiveSelection && selection)invalid("Apply selection policy differs");
  std::vector<Roi> rois;
  const auto roi_json=scalar::to_json(record.at("rois"));
  if(!roi_json.is_array())invalid("Apply positioned ROIs require an array");
  for(const auto &json:roi_json){
    auto roi=geometry::decode_roi(json,session);roi.validate(source.shape);
    if(geometry::encode_roi(roi)!=json)invalid("Noncanonical Apply positioned ROI");rois.push_back(std::move(roi));
  }
  if(target.request.roi!=RoiPolicy::PositionedRois && !rois.empty())invalid("Apply positioned ROI policy differs");
  return {options,std::move(source),std::move(target),wire::unsigned_value(record.at("display_revision")),anchor,properties,std::move(selection),std::move(rois)};
}
}
struct ResolvedContrastApplyCommand::Data {ResolvedTarget target;ContrastApplyOptions options;MetadataObject record;};
ResolvedContrastApplyCommand::ResolvedContrastApplyCommand(std::shared_ptr<const Data> data):data_(std::move(data)){}
const ResolvedTarget &ResolvedContrastApplyCommand::target()const{return data_->target;}
ContrastApplyOptions ResolvedContrastApplyCommand::options()const{return data_->options;}
MetadataObject ResolvedContrastApplyCommand::record()const{return data_->record;}
ResolvedContrastApplyCommand resolve_contrast_apply_command(const DocumentSnapshot &source,const ContrastApplyOptions &options,
    const TargetRequest &request,const ContrastApplyCommandLimits &limits){
  source_budget(source,limits.max_record_bytes);
  if(request.positions.size()>limits.max_record_bytes/1024)invalid("Apply explicit position record limit");
  auto target=resolve_target(source,request);estimate(target,options,limits);
  MetadataObject record{{"schema",std::string("imagejxx.contrast-apply-command")},{"schema_version",1},
    {"operation",std::string("contrast_apply")},{"profile",std::string("scalar_integer_apply_v1")},
    {"source",metadata_object(wire::source_record(wire::source_metadata(source)))},{"target",metadata_object(target.record())},
    {"display_revision",source.revisions().display},{"display_properties",metadata_object(source.display().properties)},
    {"reset_anchor",metadata_array({source.display().t,source.display().c,source.display().crosshair[2]})},
    {"options",metadata_object({{"minimum",options.minimum},{"maximum",options.maximum},{"unsigned16_bits",options.unsigned16_bits}})},
    {"selection",selected(target)},{"rois",rois(target)},
    {"reset",metadata_object({{"policy",std::string("imagej_scalar_reset_v1")},{"range",source.data().dataset.pixel_type()==PixelType::UInt16 && !options.unsigned16_bits?MetadataValue{}:metadata_array({0.,source.data().dataset.pixel_type()==PixelType::UInt8?255.:double((1u<<options.unsigned16_bits)-1)})},
      {"threshold",std::string("clear")},{"channel_plane",std::string("active_if_affected_else_first")},{"indexed_palette",std::string("preserve")}})}};
  Budget{limits.max_record_bytes}.object(record);
  return ResolvedContrastApplyCommand(std::make_shared<const ResolvedContrastApplyCommand::Data>(ResolvedContrastApplyCommand::Data{std::move(target),options,std::move(record)}));
}
void validate_contrast_apply_command_record(const MetadataObject &record,std::size_t cap){(void)decode(record,cap);}
ResolvedContrastApplyCommand contrast_apply_command_from_record(const DocumentSnapshot &source,const MetadataObject &record,const ContrastApplyCommandLimits &limits){
  const auto decoded=decode(record,limits.max_record_bytes);
  (void)detail::strict_target_from_record(source,decoded.target,{});
  const auto &dataset=source.data().dataset;
  if(dataset.pixel_type()!=decoded.source.pixel_type || dataset.sample_policy()!=decoded.source.sample_policy ||
      !wire::same_calibration(dataset.calibration(),decoded.source.calibration))invalid("Apply source storage/calibration differs");
  if(source.revisions().selection!=decoded.target.source_revisions.selection || source.revisions().display!=decoded.display_revision)throw RevisionConflict();
  if(source.display().t!=decoded.anchor.t || source.display().c!=decoded.anchor.c || source.display().crosshair[2]!=decoded.anchor.z)invalid("Apply reset anchor differs");
  if(!metadata_equal(source.display().properties,decoded.properties) ||
      (decoded.target.request.roi==RoiPolicy::ActiveSelection && source.data().selection!=decoded.selection) ||
      (decoded.target.request.roi==RoiPolicy::PositionedRois && source.data().rois!=decoded.rois))invalid("Apply source display/geometry differs");
  return resolve_contrast_apply_command(source,decoded.options,decoded.target.request,limits);
}
ResolvedContrastApplyCommand rebind_contrast_apply_command(const DocumentSnapshot &source,const MetadataObject &record,
    const TargetRequest &request,const ContrastApplyCommandLimits &limits){
  const auto decoded=decode(record,limits.max_record_bytes);
  return resolve_contrast_apply_command(source,decoded.options,request,limits);
}
ContrastApplyCommandEstimate estimate_contrast_apply_command(const ResolvedContrastApplyCommand &command,const ContrastApplyCommandLimits &limits){
  return estimate(command.target(),command.options(),limits);
}
DocumentTransaction execute_contrast_apply_command(const ResolvedContrastApplyCommand &command,const ContrastApplyCommandLimits &limits,const ExecutionControl &control){
  const auto captured=command;const auto frozen=limits;
  estimate_contrast_apply_command(captured,frozen);
  return prepare_contrast_apply(captured.target(),captured.options(),frozen.processing,control);
}
} // namespace imagejxx
