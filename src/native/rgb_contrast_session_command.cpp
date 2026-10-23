#include <imagejxx/rgb_contrast_session_command.hpp>
#include "rgb_contrast_session_internal.hpp"
#include "contrast_command_record.hpp"
#include "target_record_internal.hpp"
#include <cmath>

namespace imagejxx {
namespace {
namespace wire=detail::axis_command_record;
namespace scalar=detail::session_metadata;
namespace geometry=detail::selection_codec;
using detail::contrast_command_record::Budget;
using detail::contrast_command_record::add;
using detail::contrast_command_record::mul;
using detail::contrast_command_record::invalid;
bool regional_plan(const DocumentSnapshot &source,const RgbContrastSessionPlan &plan){
  if(source.data().selection && source.data().selection->is_region())return true;
  for(const auto &visit:plan.visits)for(const auto &step:visit.stages)
    if(step.selection && step.selection->is_region())return true;
  return false;
}
ContrastApplyCommandEstimate estimate(const DocumentSnapshot &source,const RgbContrastSessionPlan &plan,
    const ContrastApplyCommandLimits &limits){
  const auto raw=estimate_rgb_contrast_session(source,plan,limits);
  if(regional_plan(source,plan)) {
    detail::contrast_command_record::regional_capture_depth(source,1);
    for(const auto &visit:plan.visits)for(const auto &step:visit.stages)if(step.selection)
      for(const auto &[key,value]:step.selection->attributes)detail::regional_wire_depth(value,7);
  }
  if(source.display().properties.count("channel_display"))invalid("RGB session cannot use scalar channel display state");
  ContrastApplyCommandEstimate out;out.pixels=raw.pixels;out.processing_bytes=raw.working_bytes;out.metadata_bytes=raw.metadata_bytes;
  out.record_bytes=add(add(mul(raw.metadata_bytes,4),65536+8192),add(mul(raw.visits,1024),mul(raw.stages,1024)));
  if(out.record_bytes>limits.max_record_bytes)invalid("RGB session command record allowance exceeds limit");
  out.working_bytes=add(raw.working_bytes,add(mul(raw.metadata_bytes,32),add(mul(out.record_bytes,12),1024*1024)));
  if(out.working_bytes>limits.processing.max_working_bytes)invalid("RGB session command exceeds working limit");
  return out;
}
bool boolean(const MetadataValue &value){
  if(!std::holds_alternative<bool>(value))invalid("RGB session flags require bool");
  return std::get<bool>(value);
}
struct Decoded {MetadataObject capture;RgbContrastSessionPlan plan;};
Decoded decode(const MetadataObject &requested,std::size_t cap){
  Budget{cap}.object(requested);const auto record=requested;validate_metadata(record);
  wire::exact_fields(record,{"schema","schema_version","operation","profile","capture","entire_stack","visits"});
  const auto version=wire::unsigned_value(record.at("schema_version"));
  if(version==2)detail::regional_record_depth(record);
  if(wire::text_value(record.at("schema"))!="imagejxx.rgb-contrast-session-command" || (version!=1 && version!=2) ||
      wire::text_value(record.at("operation"))!="rgb_contrast_session" || wire::text_value(record.at("profile"))!="imagej_rgb_session_v1")invalid("Unsupported RGB session command record");
  const auto &capture=wire::object_value(record.at("capture"));validate_rgb_contrast_apply_command_record(capture,cap);
  if(version==1 && wire::unsigned_value(capture.at("schema_version"))!=1)invalid("Regional RGB session capture requires version 2");
  const auto target=detail::decode_target_record(wire::object_value(capture.at("target")));
  const auto &anchor=wire::array_value(capture.at("reset_anchor"));
  if(target.request.scope!=TargetScope::Ranges || target.request.roi!=RoiPolicy::ActiveSelection)invalid("RGB session capture requires singleton active selection target");
  const std::array<std::optional<AxisRange>,3> ranges{target.request.t,target.request.c,target.request.z};
  for(std::size_t i=0;i<3;++i){const int at=wire::index_value(anchor[i]);if(!ranges[i] || ranges[i]->start!=at || ranges[i]->stop!=at+1 || ranges[i]->step!=1)invalid("RGB session capture active target differs");}
  SessionLimits session;session.max_json_bytes=cap;session.max_encoded_bytes=cap;session.max_working_bytes=mul(cap,16);session.max_nodes=cap/64;session.max_depth=128;
  RgbContrastSessionPlan plan;plan.entire_stack=boolean(record.at("entire_stack"));
  const auto &visits=wire::array_value(record.at("visits"));
  if(visits.empty())invalid("RGB session requires a current visit");
  plan.visits.reserve(visits.size());
  const auto shape=target.source_shape;
  for(const auto &value:visits){
    const auto &row=wire::object_value(value);wire::exact_fields(row,{"position","stages"});
    const auto &p=wire::array_value(row.at("position"));if(p.size()!=3)invalid("RGB session position requires T/C/Z");
    RgbContrastSessionVisit visit;visit.position={wire::index_value(p[0]),wire::index_value(p[1]),wire::index_value(p[2])};
    if(visit.position.t>=shape.t || visit.position.c>=shape.c || visit.position.z>=shape.z)invalid("RGB session visit exceeds recorded source");
    const auto &stages=wire::array_value(row.at("stages"));if(stages.empty())invalid("RGB session visit requires a current draft");
    visit.stages.reserve(stages.size());
    for(const auto &value:stages){
      const auto &stage=wire::object_value(value);wire::exact_fields(stage,{"minimum","maximum","channels","whole_plane","selection"});
      RgbContrastSessionStep step;step.options={wire::number_value(stage.at("minimum")),wire::number_value(stage.at("maximum")),wire::index_value(stage.at("channels"))};
      if(!std::isfinite(step.options.minimum) || !std::isfinite(step.options.maximum) || step.options.minimum>step.options.maximum || step.options.channels<1 || step.options.channels>7)invalid("Invalid RGB session stage");
      step.whole_plane=boolean(stage.at("whole_plane"));
      if(!std::holds_alternative<std::monostate>(stage.at("selection"))){
        const auto json=scalar::to_json(stage.at("selection"));step.selection=geometry::decode_selection(json,session,version==2);
        if(geometry::encode_selection(*step.selection,version==2)!=json)invalid("Noncanonical RGB session selection");
      }
      detail::validate_rgb_contrast_session_selection(step.selection,shape);
      visit.stages.push_back(std::move(step));
    }
    plan.visits.push_back(std::move(visit));
  }
  const auto &last=plan.visits.back().stages.back().options;const auto &options=wire::object_value(capture.at("options"));
  if(wire::number_value(options.at("minimum"))!=last.minimum || wire::number_value(options.at("maximum"))!=last.maximum || wire::index_value(options.at("channels"))!=last.channels)invalid("RGB session capture current stage differs");
  return {capture,std::move(plan)};
}
}
struct ResolvedRgbContrastSessionCommand::Data {ResolvedRgbContrastApplyCommand capture;RgbContrastSessionPlan plan;MetadataObject record;};
ResolvedRgbContrastSessionCommand::ResolvedRgbContrastSessionCommand(std::shared_ptr<const Data> data):data_(std::move(data)){}
const ResolvedTarget &ResolvedRgbContrastSessionCommand::target()const{return data_->capture.target();}
const RgbContrastSessionPlan &ResolvedRgbContrastSessionCommand::plan()const{return data_->plan;}
MetadataObject ResolvedRgbContrastSessionCommand::record()const{return data_->record;}
ResolvedRgbContrastSessionCommand resolve_rgb_contrast_session_command(const DocumentSnapshot &requested,
    const RgbContrastSessionPlan &requested_plan,const ContrastApplyCommandLimits &requested_limits){
  (void)estimate(requested,requested_plan,requested_limits);
  const auto source=requested;const auto plan=requested_plan;const auto limits=requested_limits;
  TargetRequest target;target.roi=RoiPolicy::ActiveSelection;
  auto capture=resolve_rgb_contrast_apply_command(source,plan.visits.back().stages.back().options,target,limits);
  const auto regional=regional_plan(source,plan);
  MetadataArray visits;visits.reserve(plan.visits.size());
  for(const auto &visit:plan.visits){
    MetadataArray stages;stages.reserve(visit.stages.size());
    for(const auto &step:visit.stages)stages.push_back(metadata_object({{"minimum",step.options.minimum},{"maximum",step.options.maximum},{"channels",step.options.channels},{"whole_plane",step.whole_plane},
      {"selection",step.selection?scalar::from_json(geometry::encode_selection(*step.selection,regional)):MetadataValue{}}}));
    visits.push_back(metadata_object({{"position",metadata_array({visit.position.t,visit.position.c,visit.position.z})},{"stages",metadata_array(std::move(stages))}}));
  }
  MetadataObject record{{"schema",std::string("imagejxx.rgb-contrast-session-command")},{"schema_version",regional?2:1},{"operation",std::string("rgb_contrast_session")},{"profile",std::string("imagej_rgb_session_v1")},
    {"capture",metadata_object(capture.record())},{"entire_stack",plan.entire_stack},{"visits",metadata_array(std::move(visits))}};
  Budget{limits.max_record_bytes}.object(record);
  return ResolvedRgbContrastSessionCommand(std::make_shared<const ResolvedRgbContrastSessionCommand::Data>(ResolvedRgbContrastSessionCommand::Data{std::move(capture),plan,std::move(record)}));
}
void validate_rgb_contrast_session_command_record(const MetadataObject &record,std::size_t cap){(void)decode(record,cap);}
RgbContrastSessionPlan rgb_contrast_session_plan_from_record(const MetadataObject &record,std::size_t cap){return decode(record,cap).plan;}
ResolvedRgbContrastSessionCommand rgb_contrast_session_command_from_record(const DocumentSnapshot &source,const MetadataObject &record,const ContrastApplyCommandLimits &limits){
  const auto decoded=decode(record,limits.max_record_bytes);(void)rgb_contrast_apply_command_from_record(source,decoded.capture,limits);
  return resolve_rgb_contrast_session_command(source,decoded.plan,limits);
}
ResolvedRgbContrastSessionCommand rebind_rgb_contrast_session_command(const DocumentSnapshot &source,const MetadataObject &record,const ContrastApplyCommandLimits &limits){
  const auto decoded=decode(record,limits.max_record_bytes);return resolve_rgb_contrast_session_command(source,decoded.plan,limits);
}
ContrastApplyCommandEstimate estimate_rgb_contrast_session_command(const ResolvedRgbContrastSessionCommand &command,const ContrastApplyCommandLimits &limits){return estimate(command.target().source(),command.plan(),limits);}
DocumentTransaction execute_rgb_contrast_session_command(const ResolvedRgbContrastSessionCommand &command,const ContrastApplyCommandLimits &limits,const ExecutionControl &control){
  const auto frozen=command;const auto cap=limits;(void)estimate_rgb_contrast_session_command(frozen,cap);
  return prepare_rgb_contrast_session(frozen.target().source(),frozen.plan(),cap,control);
}
} // namespace imagejxx
