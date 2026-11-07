#include "analysis_measurement.hpp"
#include "render_memory.hpp"
#include "settings_io.hpp"
#include <imagejxx/manual_threshold.hpp>
#include <imagejxx/channel_display.hpp>
#include <imagejxx/measurement_overlay.hpp>
#include "../native/content_sha256.hpp"
#include <limits>
#include <type_traits>
#include "../native/kernel_control.hpp"
#include <cmath>

namespace imagejxx::desktop {
bool captured_profile_available(const DocumentSnapshot& source) {
  const auto& data=source.data();const auto& dataset=data.dataset;
  if(dataset.shape().s!=1 || dataset.sample_policy()!=FloatingSamplePolicy::FiniteOnly ||
      (dataset.pixel_type()!=PixelType::UInt8 && dataset.pixel_type()!=PixelType::UInt16 && dataset.pixel_type()!=PixelType::Float32))return false;
  if(!data.selection)return false;const auto& s=*data.selection;
  if((s.kind!="line" && s.kind!="polyline") || s.width>1 || s.points.size()<2 || s.points.size()>4096)return false;
  const auto shape=dataset.shape();
  for(const auto& p:s.points)if(p[0]<0 || p[1]<0 || p[0]>shape.x-1 || p[1]>shape.y-1)return false;
  return true;
}
bool captured_measurement_available(const DocumentSnapshot& source) {
  const auto& data=source.data();const auto& dataset=data.dataset;
  if(dataset.shape().s!=1 || dataset.sample_policy()!=FloatingSamplePolicy::FiniteOnly ||
      (dataset.pixel_type()!=PixelType::UInt8 && dataset.pixel_type()!=PixelType::UInt16 &&
       dataset.pixel_type()!=PixelType::Float32))return false;
  if(!data.selection)return true;
  const auto& kind=data.selection->kind;
  if(kind=="polyline" && data.selection->points.size()==1 && data.selection->width<=1) {
    const auto& p=data.selection->points.front();const auto shape=dataset.shape();
    return p[0]>=0 && p[1]>=0 && p[0]<=shape.x-1 && p[1]<=shape.y-1;
  }
  if(kind=="line" || kind=="polyline")return captured_profile_available(source);
  if(kind=="angle")return data.selection->points.size()==3;
  if(kind=="point" || kind=="multipoint")return !data.selection->points.empty();
  return kind!="line" && kind!="polyline" && kind!="angle" && kind!="point" && kind!="multipoint";
}
bool captured_roi_measurement_available(const DocumentSnapshot& source) {
  const auto& dataset=source.data().dataset;
  return dataset.shape().s==1 && dataset.sample_policy()==FloatingSamplePolicy::FiniteOnly &&
      (dataset.pixel_type()==PixelType::UInt8 || dataset.pixel_type()==PixelType::UInt16 || dataset.pixel_type()==PixelType::Float32);
}
std::size_t captured_measurement_row_bytes(const ResolvedTargetAnalysis& command) {
  const auto& source=command.target().source();
  auto text=RenderMemory::add(source.data().metadata.title.size(),command.spatial().unit.size());
  text=RenderMemory::add(text,command.intensity().value_unit().size());
  if(command.options().redirect) {
    text=RenderMemory::add(text,command.options().redirect->source().data().metadata.title.size());
    text=RenderMemory::add(text,command.target().calibration().unit.size());
  }
  // Map/value/key holders plus owned strings and formatting slack. No sampled
  // array is retained by a measurement row. Getter/externally injected rows are
  // outside this managed frontend allocation.
  auto bytes=RenderMemory::mul(command.estimate().row_count,RenderMemory::add(8192,RenderMemory::mul(text,8)));
  if(command.options().display_label) {
    const auto& title=command.options().redirect?command.options().redirect->source().data().metadata.title:source.data().metadata.title;
    // The captured Label owns another title plus bounded UTF16 name/slice text.
    const auto label=RenderMemory::add(title.size(),272);
    bytes=RenderMemory::add(bytes,RenderMemory::mul(command.estimate().row_count,RenderMemory::mul(label,8)));
  }
  if(command.options().measurement==TargetMeasurementMode::RoiManager) {
    const auto& rois=source.data().rois;const auto& indices=command.options().roi_indices;
    const auto parent_count=indices.empty()?rois.size():indices.size();
    for(std::size_t n=0;n<parent_count;++n) {
      const auto& roi=rois[indices.empty()?n:indices[n]];
      const auto count=roi.selection.kind=="point" || roi.selection.kind=="multipoint"?roi.selection.points.size():1;
      bytes=RenderMemory::add(bytes,RenderMemory::mul(RenderMemory::mul(roi.name.size(),8),count));
    }
  }
  return bytes;
}
namespace {
Json scalar(double value) {
  if(std::isfinite(value))return value;
  // Preserve non-finite numerical meaning in JSON/CSV rather than losing it to
  // JSON null. Empty populations and non-finite density tables are deliberate.
  return std::isnan(value)?"nan":value>0?"inf":"-inf";
}
}
Json captured_measurement_rows(const ResolvedTargetAnalysis& command,const TargetAnalysisResult& result) {
  if(result.planes.size()!=command.estimate().row_count)
    throw std::invalid_argument("Measurement result differs from its captured extent");
  Json rows=Json::array();rows.get_ref<Json::array_t&>().reserve(result.planes.size());
  const auto& source=command.target().source();
  for(const auto& row:result.planes) {
    kernel_checkpoint();
    if(!row.statistics)throw std::invalid_argument("Measurement requires scalar statistics");
    const auto& s=*row.statistics;
    Json values{{"Image",source.data().metadata.title},{"Slice",row.position.z+1},
      {"T",row.position.t+1},{"C",row.position.c+1},{"Z",row.position.z+1},
      {"count",s.pixel_count},{"minimum",scalar(s.minimum)},{"maximum",scalar(s.maximum)},
      {"mean",scalar(s.mean)},{"stddev",scalar(s.stddev)},{"median",scalar(s.median)},
      {"sum",scalar(s.mean*double(s.pixel_count))},{"integrated_density",scalar(s.integrated_density)},
      {"Mode",scalar(s.mode_value)},{"IntDen",scalar(s.integrated_density)},
      {"RawIntDen",scalar(double(s.pixel_count)*s.raw_mean)},
      {"Area",scalar(s.area)},{"Unit",result.spatial.unit},{"Value unit",command.intensity().value_unit()}};
    if(row.label)values["Label"]=*row.label;
    if(row.area_fraction)values["%Area"]=scalar(*row.area_fraction);
    if(row.skewness)values["Skew"]=scalar(*row.skewness);
    if(row.kurtosis)values["Kurt"]=scalar(*row.kurtosis);
    if(command.options().redirect) {
      const auto& redirect=*command.options().redirect;
      if(!row.sampling_position)throw std::invalid_argument("Redirect measurement requires captured sampling position");
      const auto p=*row.sampling_position;
      values["Redirect image"]=redirect.source().data().metadata.title;
      values["Redirect document"]=redirect.document_id();
      values["Redirect T"]=p.t+1;values["Redirect C"]=p.c+1;values["Redirect Z"]=p.z+1;
      values["Coordinate unit"]=(row.point_world || row.midpoint_world || (row.angle_degrees && !row.ellipse_angle_degrees))?command.target().calibration().unit:result.spatial.unit;
    }
    if(row.roi_index) {
      const auto& roi=source.data().rois[*row.roi_index];values["ROI"]=roi.name;
      values["ROI index"]=*row.roi_index;values["ROI kind"]=roi.selection.kind;
      values["Bounds"]=row.bounds?Json(*row.bounds):Json();
    }else if(command.target().roi_policy()==RoiPolicy::ActiveSelection && source.data().selection) {
      values["ROI"]=source.data().selection->kind;
      values["Bounds"]=row.bounds?Json(*row.bounds):Json();
    }
    if(row.perimeter) {
      values["Perim."]=scalar(*row.perimeter);
      values["Perim. unit"]=row.perimeter_in_pixels?std::string("pixel"):row.length?result.spatial.unit:command.target().calibration().unit;
    }
    if(row.bounding_x) {
      values["BX"]=scalar(*row.bounding_x);values["BY"]=scalar(*row.bounding_y);
      values["Width"]=scalar(*row.bounding_width);values["Height"]=scalar(*row.bounding_height);
    }
    if(row.feret) {
      values["Feret"]=scalar(*row.feret);values["MinFeret"]=scalar(*row.min_feret);
      values["FeretAngle"]=scalar(*row.feret_angle);values["FeretX"]=scalar(*row.feret_x);values["FeretY"]=scalar(*row.feret_y);
      values["Feret unit"]=row.feret_in_pixels?std::string("pixel"):command.target().calibration().unit;
      values["Feret coordinate unit"]=row.feret_coordinates_scaled?command.target().calibration().unit:std::string("pixel");
    }
    if(row.circularity) {
      values["Circ."]=scalar(*row.circularity);values["AR"]=scalar(*row.aspect_ratio);
      values["Round"]=scalar(*row.roundness);values["Solidity"]=scalar(*row.solidity);
    }
    if(row.center_of_mass_world) {
      values["XM"]=scalar((*row.center_of_mass_world)[0]);values["YM"]=scalar((*row.center_of_mass_world)[1]);
    }
    if(row.length)values["Length"]=scalar(*row.length);
    if(row.angle_degrees)values["Angle"]=scalar(*row.angle_degrees);
    if(row.ellipse_major) {
      values["Major"]=scalar(*row.ellipse_major);values["Minor"]=scalar(*row.ellipse_minor);
      values["Angle"]=scalar(*row.ellipse_angle_degrees);
    }
    if(row.threshold_lower) {
      values["MinThr"]=scalar(*row.threshold_lower);values["MaxThr"]=scalar(*row.threshold_upper);
    }
    if(row.point_index) {
      if(!row.point_world)throw std::invalid_argument("Point measurement requires captured coordinates");
      values["Point index"]=*row.point_index;
      if(row.point_counter_count){values["Counter"]=*row.point_counter;values["Count"]=*row.point_counter_count;}
      values["X"]=(*row.point_world)[0];values["Y"]=(*row.point_world)[1];
      values["centroid_x"]=(*row.point_world)[0];values["centroid_y"]=(*row.point_world)[1];
      values["centroid_z"]=(*row.point_world)[2];
    }else if(row.centroid_world) {
      values["X"]=scalar((*row.centroid_world)[0]);values["Y"]=scalar((*row.centroid_world)[1]);
      values["centroid_x"]=scalar((*row.centroid_world)[0]);values["centroid_y"]=scalar((*row.centroid_world)[1]);
      values["centroid_z"]=scalar((*row.centroid_world)[2]);
    }else if(row.midpoint_world) {
      values["X"]=(*row.midpoint_world)[0];values["Y"]=(*row.midpoint_world)[1];
      values["centroid_x"]=(*row.midpoint_world)[0];values["centroid_y"]=(*row.midpoint_world)[1];
      values["centroid_z"]=(*row.midpoint_world)[2];
    }else if(row.length || row.angle_degrees) {
      // Analyzer's angle/irregular-line rows leave these summaries at zero.
      values["X"]=0;values["Y"]=0;values["centroid_x"]=0;values["centroid_y"]=0;
    }
    if(row.reported_y)values["Y"]=values["centroid_y"]=scalar(*row.reported_y);
    if(row.reported_center_of_mass_y)values["YM"]=scalar(*row.reported_center_of_mass_y);
    if(row.reported_bounding_y)values["BY"]=scalar(*row.reported_bounding_y);
    rows.push_back(std::move(values));
  }
  return rows;
}
namespace {
const char* measurement_keys[]={"measurements","measurement_precision","measurement_scientific","measurement_invert_y","measurement_nan_empty_cells","measurement_add_to_overlay","measurement_limit_to_threshold","reflex_angle"};
void recordRequire(bool ok,const char* message){if(!ok)throw std::invalid_argument(message);}
void recordFields(const Json& value,std::initializer_list<const char*> fields){recordRequire(value.is_object() && value.size()==fields.size(),"Invalid measurement record fields");for(const auto* field:fields)recordRequire(value.contains(field),"Missing measurement record field");}
std::uint64_t recordUnsigned(const Json& value){recordRequire(value.is_number_integer() && (value.is_number_unsigned() || value.get<std::int64_t>()>=0),"Measurement record needs a nonnegative integer");return value.get<std::uint64_t>();}
void recordScan(const Json& value,unsigned depth,std::size_t& nodes,std::size_t& strings) {
  recordRequire(depth<=12 && ++nodes<=4096,"Measurement record exceeds structural limit");
  auto text=[&](const std::string& s){recordRequire(s.size()<=131064 && s.find('\0')==std::string::npos && s.size()<=524288-strings,"Measurement record exceeds text limit");strings+=s.size();};
  if(value.is_string())text(value.get_ref<const std::string&>());
  else if(value.is_object())for(const auto& row:value.items()){text(row.key());recordScan(row.value(),depth+1,nodes,strings);}
  else if(value.is_array())for(const auto& row:value)recordScan(row,depth+1,nodes,strings);
  else recordRequire(value.is_null() || value.is_boolean() || (value.is_number() && std::isfinite(value.get<double>())),"Measurement record needs finite values");
}
// Hash borrowed controls directly: no serialization/copy of potentially large
// metadata trees, overlay arrays, provider buffers or previous history. Tags and
// little-endian lengths make this private draft fingerprint unambiguous.
class MeasurementInputHash {
  detail::ContentSha256 hash;
  std::size_t remaining=1048576,nodes=0;
public:
  void bytes(const void* data,std::size_t n){recordRequire(n<=remaining,"Measure input metadata exceeds fingerprint allowance");remaining-=n;hash.update(static_cast<const unsigned char*>(data),n);}
  void integer(std::uint64_t n){unsigned char encoded[8];for(unsigned i=0;i<8;++i)encoded[i]=static_cast<unsigned char>(n>>(i*8));bytes(encoded,8);}
  void text(const std::string& value){integer(value.size());bytes(value.data(),value.size());}
  void number(double value){std::uint64_t bits;static_assert(sizeof(bits)==sizeof(value));std::memcpy(&bits,&value,sizeof(bits));integer(bits);}
  void object(const MetadataObject& values,unsigned depth=0,bool properties=false){
    recordRequire(depth<=128,"Measure input metadata exceeds fingerprint depth");
    for(const auto& item:values){if(properties && item.first==measurement_overlay_property)continue;integer(1);text(item.first);value(item.second,depth+1);}integer(0);
  }
  void value(const MetadataValue& input,unsigned depth){
    recordRequire(depth<=128 && ++nodes<=65536,"Measure input metadata exceeds fingerprint structure");integer(input.index());
    std::visit([&](const auto& v){using T=std::decay_t<decltype(v)>;
      if constexpr(std::is_same_v<T,std::monostate>){}
      else if constexpr(std::is_same_v<T,std::string>)text(v);
      else if constexpr(std::is_same_v<T,std::vector<std::string>>){integer(v.size());for(const auto& s:v)text(s);}
      else if constexpr(std::is_same_v<T,std::shared_ptr<const MetadataTree>>){recordRequire(bool(v),"Invalid Measure input metadata tree");std::visit([&](const auto& tree){using U=std::decay_t<decltype(tree)>;if constexpr(std::is_same_v<U,MetadataObject>){integer(1);object(tree,depth+1);}else{integer(2);integer(tree.size());for(const auto& item:tree)value(item,depth+1);}},v->value);}
      else if constexpr(std::is_same_v<T,double>)number(v);
      else integer(static_cast<std::uint64_t>(v));
    },input);
  }
  void calibration(const SpatialCalibration& c){for(auto v:c.spacing)number(v);for(auto v:c.origin)number(v);for(auto v:c.direction)number(v);text(c.unit);text(c.time_unit);number(c.frame_interval);number(c.time_origin);}
  std::string finish(){return hash.finish();}
};
std::string measurementInputFingerprint(const ResolvedTarget& target){
  MeasurementInputHash hash;const auto& data=target.source().data();
  hash.text(data.metadata.title);hash.object(data.metadata.properties,0,true);hash.object(data.metadata.extensions);hash.object(data.metadata.record_layout);
  hash.calibration(data.dataset.calibration());hash.calibration(target.calibration());return hash.finish();
}
MeasurementRecordSource recordSource(const ResolvedTarget& target,bool consumeComposite=false) {
  const auto& source=target.source();MeasurementRecordSource out;out.document_id=source.id();out.revisions=source.revisions();
  out.input_fingerprint=measurementInputFingerprint(target);
  if(consumeComposite)out.composite=bool(recognized_channel_display(source.display(),source.data().dataset));
  out.shape=source.data().dataset.shape();out.pixel_type=source.data().dataset.pixel_type();out.position={source.display().t,source.display().c,source.display().crosshair[2]};
  const auto found=source.display().properties.find(manual_threshold_property);if(found!=source.display().properties.end())out.threshold=metadataJson(found->second);
  if(const auto* c=target.calibration_context()){out.global_generation=c->global_generation();out.bypass_generation=c->bypass_generation();out.uses_global=c->uses_global();out.ignores_global=c->ignores_global();}return out;
}
Json sourceRecord(const MeasurementRecordSource& s){return {{"document_id",s.document_id},{"revisions",{s.revisions.pixels,s.revisions.metadata,s.revisions.selection}},{"shape",s.shape.dimensions()},{"pixel_type",int(s.pixel_type)},{"position",{s.position.t,s.position.c,s.position.z}},{"threshold",s.threshold},{"input_fingerprint",s.input_fingerprint},{"composite",s.composite?Json(*s.composite):Json{}},{"calibration",{{"global_generation",s.global_generation},{"bypass_generation",s.bypass_generation},{"uses_global",s.uses_global},{"ignores_global",s.ignores_global}}}};}
MeasurementRecordSource decodeSource(const Json& value) {
  recordFields(value,{"document_id","revisions","shape","pixel_type","position","threshold","input_fingerprint","composite","calibration"});MeasurementRecordSource out;
  out.document_id=recordUnsigned(value.at("document_id"));recordRequire(out.document_id!=0,"Measurement record needs a positive source ID");
  const auto& r=value.at("revisions");recordRequire(r.is_array() && r.size()==3,"Measurement record needs three data revisions");out.revisions={recordUnsigned(r[0]),recordUnsigned(r[1]),recordUnsigned(r[2]),0};
  const auto& shape=value.at("shape");recordRequire(shape.is_array() && shape.size()==6,"Invalid measurement record shape");std::array<int,6> dims;
  for(std::size_t i=0;i<6;++i){const auto n=recordUnsigned(shape[i]);recordRequire(n>0 && n<=std::uint64_t(std::numeric_limits<int>::max()),"Invalid measurement record extent");dims[i]=int(n);}out.shape={dims[0],dims[1],dims[2],dims[3],dims[4],dims[5]};out.shape.validate();recordRequire(out.shape.s==1,"Measurement record requires scalar pixels");
  const auto type=recordUnsigned(value.at("pixel_type"));recordRequire(type<=std::uint64_t(PixelType::Float32),"Unsupported measurement record pixel type");out.pixel_type=static_cast<PixelType>(type);
  const auto& pos=value.at("position");recordRequire(pos.is_array() && pos.size()==3,"Invalid measurement record position");std::array<int,3> positions;
  for(int i=0;i<3;++i){const auto n=recordUnsigned(pos[i]);recordRequire(n<std::uint64_t(dims[i]),"Measurement record position outside source");positions[i]=int(n);}out.position={positions[0],positions[1],positions[2]};
  const auto& composite=value.at("composite");recordRequire(composite.is_null() || composite.is_boolean(),"Invalid measurement composite context");if(composite.is_boolean())out.composite=composite.get<bool>();
  const auto& fingerprint=value.at("input_fingerprint");recordRequire(fingerprint.is_string(),"Invalid Measure input fingerprint");out.input_fingerprint=fingerprint.get<std::string>();recordRequire(out.input_fingerprint.size()==64 && out.input_fingerprint.find_first_not_of("0123456789abcdef")==std::string::npos,"Invalid Measure input fingerprint");
  out.threshold=value.at("threshold");recordRequire(out.threshold.is_null() || out.threshold.is_object(),"Invalid measurement record threshold");
  if(!out.threshold.is_null()){DisplayState display;display.properties=metadataObjectFromJson(Json{{manual_threshold_property,out.threshold}});(void)manual_threshold_state(display,out.shape);}
  const auto& c=value.at("calibration");recordFields(c,{"global_generation","bypass_generation","uses_global","ignores_global"});out.global_generation=recordUnsigned(c.at("global_generation"));out.bypass_generation=recordUnsigned(c.at("bypass_generation"));recordRequire(c.at("uses_global").is_boolean() && c.at("ignores_global").is_boolean(),"Invalid measurement record calibration flags");out.uses_global=c.at("uses_global").get<bool>();out.ignores_global=c.at("ignores_global").get<bool>();return out;
}
}
bool isMeasurementRecord(const Json& value){return value.is_object() && value.value("schema",Json{})=="imagejxx.measurement-command";}
std::size_t measurementRecordWorkingBytes(const Json& value){std::size_t nodes=0,strings=0;recordScan(value,0,nodes,strings);return RenderMemory::add(8192,RenderMemory::add(RenderMemory::mul(nodes,128),RenderMemory::mul(strings,4)));}
Json measurementSettingsJson(const Settings& settings){Json out=Json::object();for(const auto* key:measurement_keys)out[key]=std::visit([](const auto& value)->Json{if constexpr(std::is_same_v<std::decay_t<decltype(value)>,std::monostate>)return nullptr;else return value;},settings.at(key));return out;}
MeasurementRecord decodeMeasurementRecord(const Json& value) {
  measurementRecordWorkingBytes(value);recordFields(value,{"schema","version","source","redirect","settings"});recordRequire(isMeasurementRecord(value) && recordUnsigned(value.at("version"))==1,"Unsupported measurement record schema/version");
  MeasurementRecord out;out.source=decodeSource(value.at("source"));if(!value.at("redirect").is_null())out.redirect=decodeSource(value.at("redirect"));const auto& settings=value.at("settings");recordRequire(settings.is_object() && settings.size()==8,"Invalid measurement record settings");for(const auto* key:measurement_keys)recordRequire(settings.contains(key),"Missing measurement record setting");out.settings.update(settingsPatch(settings));return out;
}
Json captureMeasurementRecord(const ResolvedTargetAnalysis& command,const Settings& settings) {
  recordRequire(command.options().measurement==TargetMeasurementMode::Selection && command.target().position_count()==1,"Recorder currently requires one current-selection Measure");
  const bool overlay=settings.boolean("measurement_add_to_overlay") && command.target().source().data().selection.has_value();
  Json value={{"schema","imagejxx.measurement-command"},{"version",1},{"source",sourceRecord(recordSource(command.target(),overlay && command.target().source().data().dataset.shape().plane_count()>1))},{"redirect",command.options().redirect?sourceRecord(recordSource(*command.options().redirect)):Json{}},{"settings",measurementSettingsJson(settings)}};(void)decodeMeasurementRecord(value);return value;
}
MeasurementRecordSource captureMeasurementReplaySource(const ResolvedTarget& target,bool consumeComposite){return recordSource(target,consumeComposite);}
void validateMeasurementReplaySource(const MeasurementRecordSource& expected,const ResolvedTarget& target,bool rebind) {
  auto now=recordSource(target,expected.composite.has_value());recordRequire(now.shape.dimensions()==expected.shape.dimensions() && now.pixel_type==expected.pixel_type && target.source().data().dataset.sample_policy()==FloatingSamplePolicy::FiniteOnly,"Recorded Measure source shape/type differs; choose a compatible source");
  // Metadata revision also advances for our annotation output. Compare its
  // actual input fingerprint instead; retain pixel/selection revisions and all
  // captured pose/threshold/calibration/owner guards.
  if(!rebind){auto binding=expected;now.revisions.metadata=0;binding.revisions.metadata=0;recordRequire(sourceRecord(now)==sourceRecord(binding),"Recorded Measure source changed or closed; explicitly rebind to the selected source");}
}

}
