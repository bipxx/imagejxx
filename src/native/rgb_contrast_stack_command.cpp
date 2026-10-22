#include <imagejxx/rgb_contrast_stack_command.hpp>
#include "contrast_command_record.hpp"
#include "target_record_internal.hpp"
#include <cmath>
namespace imagejxx {
namespace {
namespace wire=detail::axis_command_record;
using detail::contrast_command_record::Budget;
using detail::contrast_command_record::add;
using detail::contrast_command_record::mul;
using detail::contrast_command_record::invalid;
ContrastApplyCommandEstimate estimate(const DocumentSnapshot &source,
    const std::vector<RgbContrastStackStep> &steps,const ContrastApplyCommandLimits &limits) {
  const auto raw=estimate_rgb_contrast_stack(source,steps,limits);
  if(source.display().properties.count("channel_display"))invalid("RGB stack cannot use scalar channel display state");
  ContrastApplyCommandEstimate out;out.pixels=raw.pixels;out.processing_bytes=raw.working_bytes;out.metadata_bytes=raw.metadata_bytes;
  out.record_bytes=add(add(mul(raw.metadata_bytes,4),65536+8192),mul(steps.size(),1024));
  if(out.record_bytes>limits.max_record_bytes)invalid("RGB stack command record allowance exceeds limit");
  out.working_bytes=add(raw.working_bytes,add(mul(raw.metadata_bytes,32),add(mul(out.record_bytes,12),1024*1024)));
  if(out.working_bytes>limits.processing.max_working_bytes)invalid("RGB stack command exceeds working limit");
  return out;
}
struct Decoded {MetadataObject capture;std::vector<RgbContrastStackStep> steps;};
Decoded decode(const MetadataObject &requested,std::size_t cap) {
  Budget{cap}.object(requested);const auto record=requested;validate_metadata(record);
  wire::exact_fields(record,{"schema","schema_version","operation","profile","capture","stages"});
  if(wire::text_value(record.at("schema"))!="imagejxx.rgb-contrast-stack-command" || wire::unsigned_value(record.at("schema_version"))!=1 ||
      wire::text_value(record.at("operation"))!="rgb_contrast_stack" || wire::text_value(record.at("profile"))!="imagej_rgb_stack_v1")invalid("Unsupported RGB stack command record");
  const auto &capture=wire::object_value(record.at("capture"));validate_rgb_contrast_apply_command_record(capture,cap);
  const auto target=detail::decode_target_record(wire::object_value(capture.at("target")));
  const auto &anchor=wire::array_value(capture.at("reset_anchor"));
  if(target.request.scope!=TargetScope::Ranges || target.request.roi!=RoiPolicy::ActiveSelection)invalid("RGB stack capture requires its singleton active selection target");
  const std::array<std::optional<AxisRange>,3> ranges{target.request.t,target.request.c,target.request.z};
  for(std::size_t i=0;i<3;++i){const int at=wire::index_value(anchor[i]);if(!ranges[i] || ranges[i]->start!=at || ranges[i]->stop!=at+1 || ranges[i]->step!=1)invalid("RGB stack capture active target differs");}
  const auto &rows=wire::array_value(record.at("stages"));if(rows.empty() || rows.size()>64)invalid("RGB stack requires 1..64 stages");
  std::vector<RgbContrastStackStep> steps;steps.reserve(rows.size());
  for(const auto &row:rows){const auto &o=wire::object_value(row);wire::exact_fields(o,{"minimum","maximum","channels","whole_plane"});
    if(!std::holds_alternative<bool>(o.at("whole_plane")))invalid("RGB stack whole_plane requires bool");
    RgbContrastApplyOptions options{wire::number_value(o.at("minimum")),wire::number_value(o.at("maximum")),wire::index_value(o.at("channels"))};
    if(!std::isfinite(options.minimum) || !std::isfinite(options.maximum) || options.minimum>options.maximum || options.channels<1 || options.channels>7)invalid("Invalid RGB stack stage");
    steps.push_back({options,std::get<bool>(o.at("whole_plane"))});
  }
  const auto &last=steps.back().options;const auto &o=wire::object_value(capture.at("options"));
  if(wire::number_value(o.at("minimum"))!=last.minimum || wire::number_value(o.at("maximum"))!=last.maximum || wire::index_value(o.at("channels"))!=last.channels)invalid("RGB stack capture current stage differs");
  return {capture,std::move(steps)};
}
}
struct ResolvedRgbContrastStackCommand::Data {ResolvedRgbContrastApplyCommand capture;std::vector<RgbContrastStackStep> steps;MetadataObject record;};
ResolvedRgbContrastStackCommand::ResolvedRgbContrastStackCommand(std::shared_ptr<const Data> data):data_(std::move(data)){}
const ResolvedTarget &ResolvedRgbContrastStackCommand::target()const{return data_->capture.target();}
const std::vector<RgbContrastStackStep> &ResolvedRgbContrastStackCommand::steps()const{return data_->steps;}
MetadataObject ResolvedRgbContrastStackCommand::record()const{return data_->record;}
ResolvedRgbContrastStackCommand resolve_rgb_contrast_stack_command(const DocumentSnapshot &requested,
    const std::vector<RgbContrastStackStep> &requested_steps,const ContrastApplyCommandLimits &requested_limits) {
  (void)estimate(requested,requested_steps,requested_limits);
  const auto source=requested;const auto steps=requested_steps;const auto limits=requested_limits;
  TargetRequest target;target.roi=RoiPolicy::ActiveSelection;
  auto capture=resolve_rgb_contrast_apply_command(source,steps.back().options,target,limits);
  MetadataArray stages;stages.reserve(steps.size());
  for(const auto &s:steps)stages.push_back(metadata_object({{"minimum",s.options.minimum},{"maximum",s.options.maximum},{"channels",s.options.channels},{"whole_plane",s.whole_plane}}));
  MetadataObject record{{"schema",std::string("imagejxx.rgb-contrast-stack-command")},{"schema_version",1},{"operation",std::string("rgb_contrast_stack")},
    {"profile",std::string("imagej_rgb_stack_v1")},{"capture",metadata_object(capture.record())},{"stages",metadata_array(std::move(stages))}};
  Budget{limits.max_record_bytes}.object(record);
  return ResolvedRgbContrastStackCommand(std::make_shared<const ResolvedRgbContrastStackCommand::Data>(ResolvedRgbContrastStackCommand::Data{std::move(capture),steps,std::move(record)}));
}
void validate_rgb_contrast_stack_command_record(const MetadataObject &record,std::size_t cap){(void)decode(record,cap);}
ResolvedRgbContrastStackCommand rgb_contrast_stack_command_from_record(const DocumentSnapshot &source,const MetadataObject &record,const ContrastApplyCommandLimits &limits){
  const auto decoded=decode(record,limits.max_record_bytes);(void)rgb_contrast_apply_command_from_record(source,decoded.capture,limits);
  return resolve_rgb_contrast_stack_command(source,decoded.steps,limits);
}
ResolvedRgbContrastStackCommand rebind_rgb_contrast_stack_command(const DocumentSnapshot &source,const MetadataObject &record,const ContrastApplyCommandLimits &limits){
  const auto decoded=decode(record,limits.max_record_bytes);return resolve_rgb_contrast_stack_command(source,decoded.steps,limits);
}
ContrastApplyCommandEstimate estimate_rgb_contrast_stack_command(const ResolvedRgbContrastStackCommand &command,const ContrastApplyCommandLimits &limits){return estimate(command.target().source(),command.steps(),limits);}
DocumentTransaction execute_rgb_contrast_stack_command(const ResolvedRgbContrastStackCommand &command,const ContrastApplyCommandLimits &limits,const ExecutionControl &control){
  const auto frozen=command;const auto cap=limits;(void)estimate_rgb_contrast_stack_command(frozen,cap);
  return prepare_rgb_contrast_stack(frozen.target().source(),frozen.steps(),cap,control);
}
} // namespace imagejxx
