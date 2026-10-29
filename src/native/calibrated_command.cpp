#include <imagejxx/calibrated_command.hpp>
#include "calibrated_descriptor_internal.hpp"
#include "calibration_preparation_internal.hpp"
#include "contrast_command_record.hpp"
#include "plane_operation_resources_internal.hpp"
#include "target_mask_resources.hpp"
#include <algorithm>
#include <type_traits>

namespace imagejxx {
namespace {
using namespace detail::axis_command_record;
using detail::contrast_command_record::Budget;
using detail::contrast_command_record::add;
using detail::contrast_command_record::mul;
constexpr std::size_t record_limit = 16ULL * 1024 * 1024;
constexpr const char* schema = "imagejxx.calibrated-command";

void check_limits(CalibratedCommandLimits limits) {
  if(!limits.max_record_bytes || limits.max_record_bytes>record_limit || !limits.max_working_bytes)
    throw std::invalid_argument("Invalid calibrated command limits");
}
bool boolean(const MetadataValue& value) {
  if(const auto flag=std::get_if<bool>(&value))return *flag;
  throw std::invalid_argument("Calibrated command flags require Booleans");
}
std::string operation_of(const CalibratedCommandRequest& request) {
  return std::visit([](const auto& r)->std::string {
    using T=std::decay_t<decltype(r)>;
    if constexpr(std::is_same_v<T,AxisPlaneExportRequest>)return "axis_plane_export";
    else if constexpr(std::is_same_v<T,AxisRangeResliceRequest>)return "axis_range_reslice";
    else if constexpr(std::is_same_v<T,AxisRangeOrientationRequest>)return "axis_range_orientation";
    else if constexpr(std::is_same_v<T,AxisRangeSpacingRequest>)return "axis_range_spacing";
    else if constexpr(std::is_same_v<T,AxisRangeSlicerResizeRequest>)return "axis_range_slicer_resize";
    else if constexpr(std::is_same_v<T,ImageJProjectionDocumentRequest>)return "project_imagej";
    else return "gaussian_blur_imagej";
  },request);
}
CalibratedCommandRequest descriptor_request(const std::string& operation,const MetadataObject& descriptor,
    const SourceRecord& source,std::optional<detail::TargetRecord>& target) {
  if(text_value(descriptor.at("operation"))!=operation)
    throw std::invalid_argument("Calibrated operation contradicts descriptor");
  if(operation=="axis_plane_export" || operation=="axis_range_reslice")
    return std::visit([](const auto& r)->CalibratedCommandRequest{return r;},
        detail::extraction_descriptor_request(descriptor));
  if(operation=="axis_range_orientation")return detail::orientation_descriptor_request(descriptor);
  if(operation=="axis_range_spacing")return detail::spacing_descriptor_request(descriptor);
  if(operation=="axis_range_slicer_resize")return detail::slicer_descriptor_request(descriptor);
  if(operation=="project_imagej")return detail::projection_descriptor_request(descriptor);
  if(operation=="gaussian_blur_imagej") {
    exact_fields(descriptor,{"source","operation","command"});
    if(!((source.pixel_type==PixelType::UInt8 && (source.shape.s==1 || source.shape.s==3)) ||
        ((source.pixel_type==PixelType::UInt16 || source.pixel_type==PixelType::Float32) && source.shape.s==1)))
      throw std::invalid_argument("Unsupported calibrated Gaussian source storage");
    auto d=detail::gaussian_descriptor_request(object_value(descriptor.at("command")));
    if(!same_calibration(d.calibration,source.calibration) ||
        d.target.document_id!=source.document_id || d.target.source_shape.dimensions()!=source.shape.dimensions() ||
        !d.target.source_revisions.matches(source.revisions,Change::Pixels|Change::Metadata))
      throw std::invalid_argument("Gaussian descriptor contradicts effective source");
    if(d.target.request.roi==RoiPolicy::WholePlane && d.target.source_revisions.selection!=0)
      throw std::invalid_argument("Whole-plane calibrated Gaussian selection revision must be canonical zero");
    target=std::move(d.target);return d.options;
  }
  throw std::invalid_argument("Unsupported calibrated command operation");
}
struct Decoded {
  SourceRecord source;
  CalibratedCommandRequest request;
  CalibratedCommandLimits limits;
  std::string operation;
  std::optional<detail::TargetRecord> target;
};
Decoded decode(const MetadataObject& record) {
  Budget bounded{record_limit};bounded.object(record);
  exact_fields(record,{"schema","version","operation","source","binding","bypass",
      "effective","descriptor","limits"});
  if(text_value(record.at("schema"))!=schema || unsigned_value(record.at("version"))!=1)
    throw std::invalid_argument("Unsupported calibrated command schema/version");
  const auto& l=object_value(record.at("limits"));
  exact_fields(l,{"max_record_bytes","max_working_bytes"});
  CalibratedCommandLimits limits{size_value(l.at("max_record_bytes")),size_value(l.at("max_working_bytes"))};
  check_limits(limits);
  if(bounded.used>limits.max_record_bytes)throw std::invalid_argument("Calibrated record exceeds metadata budget");
  const auto& binding=text_value(record.at("binding"));
  const auto bypass=boolean(record.at("bypass"));
  if((binding!="application" && binding!="detached") || (bypass && binding!="application"))
    throw std::invalid_argument("Invalid calibrated binding policy");
  auto source=read_source(object_value(record.at("source")));
  auto effective_source=source;
  const auto global=!std::holds_alternative<std::monostate>(record.at("effective"));
  const MetadataObject* effective=nullptr;
  if(global) {
    if(binding!="application" || bypass)throw std::invalid_argument("Effective calibration requires an active application binding");
    effective=&object_value(record.at("effective"));
    exact_fields(*effective,{"spatial","intensity"});
    auto adapted=source_record(source);adapted["calibration"]=effective->at("spatial");
    effective_source=read_source(adapted);
    ImageJIntensityCalibration::from_record(effective->at("intensity")).validate_for(source.pixel_type,source.shape.s);
  }
  const auto& descriptor=object_value(record.at("descriptor"));
  // Check required fields before at(): malformed records produce a domain error.
  if(!descriptor.count("source") || !descriptor.count("operation"))
    throw std::invalid_argument("Calibrated descriptor lacks source or operation");
  if(!metadata_equal(source_record(effective_source),object_value(descriptor.at("source"))))
    throw std::invalid_argument("Calibrated descriptor contradicts original/effective source");
  const auto operation=text_value(record.at("operation"));
  std::optional<detail::TargetRecord> target;
  auto request=descriptor_request(operation,descriptor,effective_source,target);
  if(global && operation=="project_imagej" &&
      !metadata_equal(effective->at("intensity"),descriptor.at("intensity")))
    throw std::invalid_argument("Projection density contradicts effective calibration");
  return {std::move(source),std::move(request),limits,operation,std::move(target)};
}
std::pair<MetadataObject,std::size_t> describe(const ResolvedTarget& capture,
    const CalibratedCommandRequest& request) {
  auto source=source_metadata(capture.source());source.calibration=capture.calibration();
  return std::visit([&](const auto& r)->std::pair<MetadataObject,std::size_t> {
    using T=std::decay_t<decltype(r)>;
    if constexpr(std::is_same_v<T,AxisPlaneExportRequest> || std::is_same_v<T,AxisRangeResliceRequest>) {
      auto d=detail::extraction_descriptor(source,AxisExtractionRequest{r});
      return {std::move(d.second),d.first.working_bytes};
    } else if constexpr(std::is_same_v<T,AxisRangeOrientationRequest>) {
      auto d=detail::orientation_descriptor(source,r);return {std::move(d.second),d.first.working_bytes};
    } else if constexpr(std::is_same_v<T,AxisRangeSpacingRequest>) {
      auto d=detail::spacing_descriptor(source,r);return {std::move(d.second),d.first.output.working_bytes};
    } else if constexpr(std::is_same_v<T,AxisRangeSlicerResizeRequest>) {
      auto d=detail::slicer_descriptor(source,r);return {std::move(d.second),d.first.output.working_bytes};
    } else if constexpr(std::is_same_v<T,GaussianBlurCommandOptions>) {
      const auto command=resolve_gaussian_command(capture,r);
      const auto base=detail::plane_operation_base_resources("gaussian_blur_imagej",capture,command.parameters());
      const auto masks=detail::target_mask_workspace(capture,"gaussian_blur_imagej",std::numeric_limits<std::size_t>::max());
      auto legacy=command.record();
      if(capture.roi_policy()==RoiPolicy::WholePlane) {
        auto target=object_value(legacy.at("target"));
        auto revisions=array_value(target.at("source_revisions"));revisions[2]=std::uint64_t(0);
        target["source_revisions"]=metadata_array(std::move(revisions));legacy["target"]=metadata_object(std::move(target));
      }
      MetadataObject descriptor{{"operation",std::string("gaussian_blur_imagej")},
          {"source",metadata_object(source_record(source))},{"command",metadata_object(std::move(legacy))}};
      return {std::move(descriptor),add(base,masks)};
    } else {
      auto d=detail::projection_descriptor(capture.source(),capture.calibration_context(),r);
      return {std::move(d.second),d.first.working_bytes};
    }
  },request);
}
} // namespace

struct ResolvedCalibratedCommand::State {
  ResolvedTarget capture;
  CalibratedCommandRequest request;
  CalibratedCommandLimits limits;
  std::string operation;
  MetadataObject record;
  std::size_t working_bytes;
};
ResolvedCalibratedCommand::ResolvedCalibratedCommand(std::shared_ptr<const State> state):state_(std::move(state)) {}
const ResolvedTarget& ResolvedCalibratedCommand::capture() const{return state_->capture;}
const CalibratedCommandRequest& ResolvedCalibratedCommand::request() const{return state_->request;}
const CalibratedCommandLimits& ResolvedCalibratedCommand::limits() const{return state_->limits;}
const std::string& ResolvedCalibratedCommand::operation() const{return state_->operation;}
const MetadataObject& ResolvedCalibratedCommand::record() const{return state_->record;}
std::size_t ResolvedCalibratedCommand::working_bytes() const{return state_->working_bytes;}

ResolvedCalibratedCommand resolve_calibrated_command(const ResolvedTarget& capture,
    const CalibratedCommandRequest& request,CalibratedCommandLimits limits) {
  check_limits(limits);
  auto descriptor=describe(capture,request);
  MetadataValue effective;
  if(const auto context=capture.calibration_context()) {
    auto s=source_metadata(capture.source());s.calibration=context->spatial();
    effective=metadata_object({{"spatial",source_record(s).at("calibration")},
        {"intensity",context->intensity().record()}});
  }
  const auto operation=operation_of(request);
  MetadataObject record{{"schema",std::string(schema)},{"version",1},{"operation",operation},
      {"source",metadata_object(source_record(source_metadata(capture.source())))},
      {"binding",std::string(capture.application_calibration_bound()?"application":"detached")},
      {"bypass",detail::CalibrationPreparation::ignores_global(capture)},
      {"effective",std::move(effective)},{"descriptor",metadata_object(std::move(descriptor.first))},
      {"limits",metadata_object({{"max_record_bytes",std::uint64_t(limits.max_record_bytes)},
          {"max_working_bytes",std::uint64_t(limits.max_working_bytes)}})}};
  Budget bounded{limits.max_record_bytes};bounded.object(record);
  const auto source_bytes=detail::contrast_command_record::source_budget(capture.source(),
      std::min(limits.max_record_bytes,limits.max_working_bytes));
  const auto working=add(descriptor.second,add(1024*1024,mul(add(source_bytes,bounded.used),32)));
  if(working>limits.max_working_bytes)throw std::invalid_argument("Calibrated command exceeds working budget");
  return ResolvedCalibratedCommand(std::make_shared<const ResolvedCalibratedCommand::State>(
      ResolvedCalibratedCommand::State{capture,request,limits,operation,std::move(record),working}));
}
void validate_calibrated_command_record(const MetadataObject& record){(void)decode(record);}
ResolvedCalibratedCommand calibrated_command_from_record(const ResolvedTarget& capture,const MetadataObject& record) {
  const auto d=decode(record);
  if(capture.source().id()!=d.source.document_id)throw std::invalid_argument("Calibrated command belongs to another source");
  if(!capture.source().revisions().matches(d.source.revisions,Change::Pixels|Change::Metadata))throw RevisionConflict();
  if(d.operation=="project_imagej") {
    auto expected=d.source.revisions;const auto& descriptor=object_value(record.at("descriptor"));
    expected.selection=unsigned_value(descriptor.at("selection_revision"));
    expected.display=unsigned_value(descriptor.at("display_revision"));
    if(!capture.source().revisions().matches(expected,Change::All))throw RevisionConflict();
  }
  auto replay_capture=capture;
  if(d.target) {
    // Strictly check original target dependencies/shape before restoring frozen
    // ranges or positions. The caller supplies only current application context.
    (void)detail::strict_target_from_record(capture.source(),*d.target,d.operation);
    replay_capture=detail::CalibrationPreparation::target(capture,d.target->request);
  }
  auto resolved=resolve_calibrated_command(replay_capture,d.request,d.limits);
  if(!metadata_equal(resolved.record(),record))
    throw std::invalid_argument("Calibrated command source/context differs; explicit rebind required");
  return resolved;
}
ResolvedCalibratedCommand rebind_calibrated_command(const ResolvedTarget& capture,
    const MetadataObject& record,const CalibratedCommandRequest& request,CalibratedCommandLimits limits) {
  const auto d=decode(record);
  if(operation_of(request)!=d.operation)throw std::invalid_argument("Calibrated rebind cannot change operation family");
  return resolve_calibrated_command(capture,request,limits);
}
PreparedOperation prepare_calibrated_command(const ResolvedCalibratedCommand& command,
    const ExecutionControl& control,std::size_t cap) {
  if(!cap || command.working_bytes()>cap)throw std::invalid_argument("Calibrated command exceeds runtime working budget");
  return std::visit([&](auto r)->PreparedOperation {
    using T=std::decay_t<decltype(r)>;
    if constexpr(std::is_same_v<T,GaussianBlurCommandOptions>) {
      const auto gaussian=resolve_gaussian_command(command.capture(),r);
      const auto base=detail::plane_operation_base_resources(command.operation(),command.capture(),gaussian.parameters());
      ExecutionOptions options;options.max_target_mask_bytes=cap-base;
      return prepare_operation(command.operation(),command.capture(),gaussian.parameters(),options,control);
    } else if constexpr(std::is_same_v<T,ImageJProjectionDocumentRequest>) {
      r.max_working_bytes=std::min(r.max_working_bytes,cap);
      return detail::CalibrationPreparation::prepare_imagej_projection(command.capture(),r,control);
    } else if constexpr(std::is_same_v<T,AxisPlaneExportRequest>) {
      r.max_working_bytes=std::min(r.max_working_bytes,cap);
      return detail::CalibrationPreparation::prepare_axis_plane_export(command.capture(),r,control);
    } else if constexpr(std::is_same_v<T,AxisRangeResliceRequest>) {
      r.max_working_bytes=std::min(r.max_working_bytes,cap);
      return detail::CalibrationPreparation::prepare_axis_range_reslice(command.capture(),r,control);
    } else if constexpr(std::is_same_v<T,AxisRangeOrientationRequest>) {
      r.range.max_working_bytes=std::min(r.range.max_working_bytes,cap);
      return detail::CalibrationPreparation::prepare_axis_range_orientation(command.capture(),r,control);
    } else if constexpr(std::is_same_v<T,AxisRangeSpacingRequest>) {
      r.orientation.range.max_working_bytes=std::min(r.orientation.range.max_working_bytes,cap);
      return detail::CalibrationPreparation::prepare_axis_range_spacing(command.capture(),r,control);
    } else {
      r.orientation.range.max_working_bytes=std::min(r.orientation.range.max_working_bytes,cap);
      return detail::CalibrationPreparation::prepare_axis_range_slicer_resize(command.capture(),r,control);
    }
  },command.request());
}
} // namespace imagejxx
