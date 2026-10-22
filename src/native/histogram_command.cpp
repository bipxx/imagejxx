#include <imagejxx/histogram_command.hpp>
#include "axis_command_record.hpp"
#include "histogram_command_internal.hpp"
#include "target_record_internal.hpp"
#include <cmath>
#include <limits>
#include <stdexcept>

namespace imagejxx {
namespace {
namespace wire = detail::axis_command_record;
[[noreturn]] void invalid(const char *message) { throw std::invalid_argument(message); }
std::size_t add(std::size_t a, std::size_t b) {
  if (b > std::numeric_limits<std::size_t>::max()-a)
    invalid("Histogram command size overflow");
  return a+b;
}
// Walk before conversion or copying. Every edge is charged, including shared
// nodes appearing repeatedly, so a compact DAG cannot expand without a cap.
struct Budget {
  std::size_t remaining;
  void charge(std::size_t n) {
    if (n>remaining) invalid("Histogram command exceeds its record budget");
    remaining-=n;
  }
  void text(const std::string &s) {
    charge(64);
    if (s.size()>remaining/6) invalid("Histogram command exceeds its record budget");
    charge(s.size()*6);
  }
  void object(const MetadataObject &o, unsigned depth) {
    if(depth>128) invalid("Histogram command metadata is too deep");
    charge(64);
    for(const auto &[k,v]:o){text(k);value(v,depth+1);}
  }
  void value(const MetadataValue &v, unsigned depth) {
    if(depth>128) invalid("Histogram command metadata is too deep");
    charge(64);
    if(const auto s=std::get_if<std::string>(&v)) text(*s);
    else if(const auto strings=std::get_if<std::vector<std::string>>(&v)) {
      for(const auto &item:*strings) text(item);
    } else if(const auto p=std::get_if<std::shared_ptr<const MetadataTree>>(&v)) {
      if(!*p) invalid("Histogram command has a null metadata node");
      if(const auto o=std::get_if<MetadataObject>(&(*p)->value)) object(*o,depth);
      else for(const auto &item:std::get<MetadataArray>((*p)->value)) value(item,depth+1);
    }
  }
};
void options_valid(const TargetHistogramOptions &o) {
  if(o.binning==HistogramBinning::ProcessorRaw) {
    if(o.bins!=256 || o.range) invalid("Raw histogram command requires 256 bins and no range");
  } else if(o.binning==HistogramBinning::UniformRange) {
    if(o.bins<1 || o.bins>65536 || !o.range || !std::isfinite((*o.range)[0]) ||
       !std::isfinite((*o.range)[1]) || (*o.range)[0]>=(*o.range)[1])
      invalid("Uniform histogram command requires 1..65536 bins and finite low < high");
  } else invalid("Unknown histogram command binning");
}
const char *type_name(PixelType type) {
  switch(type){case PixelType::UInt8:return "uint8";case PixelType::UInt16:return "uint16";
    case PixelType::Float32:return "float32";default:invalid("Histogram command requires scalar UInt8/UInt16/Float32");}
}
const char *policy_name(FloatingSamplePolicy policy) {
  switch(policy){case FloatingSamplePolicy::FiniteOnly:return "finite_only";
    case FloatingSamplePolicy::PreserveQuietNaNFloat32:return "preserve_quiet_nan_float32";
    default:invalid("Unknown histogram command sample policy");}
}
void target_valid(const TargetRequest &request) {
  if(request.mode!=ExecutionMode::Planes || request.destination!=OutputDestination::InPlace)
    invalid("Histogram command requires captured XY planes");
}
TargetHistogramEstimate estimate(const ResolvedTarget &target,
    const TargetHistogramOptions &options,const HistogramCommandLimits &limits) {
  auto out=estimate_target_histogram(target,options,limits.processing);
  // The target estimate includes per-position storage; fixed fields have no
  // caller-supplied strings. This allowance also bounds pre-serialization work.
  const auto record=add(out.record_bytes,65536);
  if(record>limits.max_record_bytes) invalid("Histogram command exceeds its record budget");
  out.working_bytes=add(out.working_bytes,record);
  out.record_bytes=record;
  if(out.working_bytes>limits.processing.max_working_bytes)
    invalid("Histogram command exceeds its working limit");
  return out;
}
struct Decoded {
  TargetHistogramOptions options;
  bool log;
  detail::TargetRecord target;
  std::string type,policy;
};
Decoded decode_record(const MetadataObject &record,std::size_t max_bytes) {
  Budget{max_bytes}.object(record,0);validate_metadata(record);
  wire::exact_fields(record,{"schema","schema_version","operation","profile","options","log_counts","target","pixel_type","sample_policy"});
  if(wire::text_value(record.at("schema"))!="imagejxx.histogram-command" ||
     wire::unsigned_value(record.at("schema_version"))!=1 ||
     wire::text_value(record.at("operation"))!="histogram" ||
     wire::text_value(record.at("profile"))!="raw_target_histogram_v1")
    invalid("Unsupported histogram command record");
  const auto &o=wire::object_value(record.at("options"));
  wire::exact_fields(o,{"binning","bins","range"});
  TargetHistogramOptions options;
  const auto &binning=wire::text_value(o.at("binning"));
  if(binning=="processor_raw")options.binning=HistogramBinning::ProcessorRaw;
  else if(binning=="uniform_range")options.binning=HistogramBinning::UniformRange;
  else invalid("Unknown histogram command binning");
  options.bins=wire::index_value(o.at("bins"),true);
  if(!std::holds_alternative<std::monostate>(o.at("range"))) {
    const auto &range=wire::array_value(o.at("range"));
    if(range.size()!=2) invalid("Histogram command range requires two bounds");
    options.range=std::array<double,2>{wire::number_value(range[0]),wire::number_value(range[1])};
  }
  options_valid(options);
  const auto log=std::get_if<bool>(&record.at("log_counts"));
  if(!log) invalid("Histogram command log_counts requires a Boolean");
  auto target=detail::decode_target_record(wire::object_value(record.at("target")));
  target_valid(target.request);
  const auto type=wire::text_value(record.at("pixel_type"));
  const auto policy=wire::text_value(record.at("sample_policy"));
  if(target.source_shape.s!=1 || (type!="uint8" && type!="uint16" && type!="float32") ||
     (policy!="finite_only" && policy!="preserve_quiet_nan_float32") ||
     (policy=="preserve_quiet_nan_float32" && type!="float32"))
    invalid("Histogram command has unsupported scalar storage claims");
  return {options,*log,std::move(target),type,policy};
}
Decoded decode(const MetadataObject &record,std::size_t max_bytes) {
  try {return decode_record(record,max_bytes);}
  catch(const std::invalid_argument &error) {
    throw std::invalid_argument(std::string("Histogram command: ")+error.what());
  }
}
}
struct ResolvedHistogramCommand::Data {
  ResolvedTarget target;
  TargetHistogramOptions options;
  bool log;
  MetadataObject record;
};
ResolvedHistogramCommand::ResolvedHistogramCommand(std::shared_ptr<const Data> data):data_(std::move(data)){}
const ResolvedTarget &ResolvedHistogramCommand::target() const{return data_->target;}
TargetHistogramOptions ResolvedHistogramCommand::options() const{return data_->options;}
bool ResolvedHistogramCommand::log_counts() const{return data_->log;}
MetadataObject ResolvedHistogramCommand::record() const{return data_->record;}
ResolvedHistogramCommand resolve_histogram_command(const DocumentSnapshot &source,
    const TargetHistogramOptions &options,const TargetRequest &request,bool log,
    const HistogramCommandLimits &limits) {
  // Freeze caller inputs before any future execution callbacks.
  options_valid(options);target_valid(request);
  auto target=resolve_target(source,request);
  estimate(target,options,limits);
  const auto &dataset=source.data().dataset;
  MetadataObject record{{"schema",std::string("imagejxx.histogram-command")},{"schema_version",1},
    {"operation",std::string("histogram")},{"profile",std::string("raw_target_histogram_v1")},
    {"log_counts",log},{"target",metadata_object(target.record())},
    {"pixel_type",std::string(type_name(dataset.pixel_type()))},
    {"sample_policy",std::string(policy_name(dataset.sample_policy()))},
    {"options",metadata_object({{"binning",std::string(options.binning==HistogramBinning::ProcessorRaw?"processor_raw":"uniform_range")},
      {"bins",options.bins},{"range",options.range?metadata_array({(*options.range)[0],(*options.range)[1]}):MetadataValue{std::monostate{}}}})}};
  Budget{limits.max_record_bytes}.object(record,0);
  return ResolvedHistogramCommand(std::make_shared<const ResolvedHistogramCommand::Data>(
      ResolvedHistogramCommand::Data{std::move(target),options,log,std::move(record)}));
}
void detail::validate_histogram_command_options(const TargetHistogramOptions &options){options_valid(options);}
void validate_histogram_command_record(const MetadataObject &record,std::size_t max_bytes){(void)decode(record,max_bytes);}
ResolvedHistogramCommand histogram_command_from_record(const DocumentSnapshot &source,
    const MetadataObject &record,const HistogramCommandLimits &limits) {
  const auto decoded=decode(record,limits.max_record_bytes);
  const auto &dataset=source.data().dataset;
  if(decoded.type!=type_name(dataset.pixel_type()) || decoded.policy!=policy_name(dataset.sample_policy()))
    invalid("Histogram command source storage differs");
  (void)detail::strict_target_from_record(source,decoded.target,{});
  return resolve_histogram_command(source,decoded.options,decoded.target.request,decoded.log,limits);
}
ResolvedHistogramCommand rebind_histogram_command(const DocumentSnapshot &source,
    const MetadataObject &record,const TargetRequest &request,const HistogramCommandLimits &limits) {
  const auto decoded=decode(record,limits.max_record_bytes);
  return resolve_histogram_command(source,decoded.options,request,decoded.log,limits);
}
TargetHistogramEstimate estimate_histogram_command(const ResolvedHistogramCommand &command,
    const HistogramCommandLimits &limits){return estimate(command.target(),command.options(),limits);}
TargetHistogramResult execute_histogram_command(const ResolvedHistogramCommand &command,
    const HistogramCommandLimits &limits,const ExecutionControl &control) {
  // Cheap immutable command capture protects callbacks reassigning the caller's
  // command variable. The lower-level accumulator freezes limits/control.
  const auto captured=command;
  estimate_histogram_command(captured,limits);
  return accumulate_target_histogram(captured.target(),captured.options(),limits.processing,control);
}
}
