#include <imagejxx/measurement_overlay.hpp>
#include <imagejxx/point_annotations.hpp>
#include "selection_creation_workflow.hpp"
#include "selection_codec.hpp"
#include "session_metadata.hpp"
#include "kernel_control.hpp"
#include <limits>
#include <stdexcept>
#include <algorithm>

namespace imagejxx {
namespace {
namespace codec=detail::session_metadata;
[[noreturn]] void bad(const char* message){throw std::invalid_argument(message);}
std::size_t add(std::size_t a,std::size_t b){if(b>std::numeric_limits<std::size_t>::max()-a)bad("Measurement overlay byte count overflow");return a+b;}
std::size_t mul(std::size_t a,std::size_t b){if(a && b>std::numeric_limits<std::size_t>::max()/a)bad("Measurement overlay byte count overflow");return a*b;}
const MetadataObject& object(const MetadataValue& value) {
  const auto node=std::get_if<std::shared_ptr<const MetadataTree>>(&value);
  if(!node || !*node)bad("Measurement overlay presentation must be an object");
  const auto out=std::get_if<MetadataObject>(&(*node)->value);
  if(!out)bad("Measurement overlay presentation must be an object");return *out;
}
const MetadataValue& field(const MetadataObject& values,const char* name) {
  const auto it=values.find(name);if(it==values.end())bad("Missing measurement overlay presentation field");return it->second;
}
bool boolean(const MetadataValue& value) {
  const auto out=std::get_if<bool>(&value);if(!out)bad("Measurement overlay flags must be Boolean");return *out;
}
std::size_t integer(const MetadataValue& value) {
  if(const auto out=std::get_if<std::uint64_t>(&value)) {if(*out>std::numeric_limits<std::size_t>::max())bad("Measurement overlay integer overflow");return std::size_t(*out);}
  const auto out=std::get_if<std::int64_t>(&value);if(!out || *out<0)bad("Measurement overlay integer must be nonnegative");return std::size_t(*out);
}
const std::string* selection_name(const Selection& value) {
  const auto it=value.attributes.find("imagej_roi_name");
  if(it==value.attributes.end() || std::holds_alternative<std::monostate>(it->second))return nullptr;
  const auto name=std::get_if<std::string>(&it->second);if(!name)bad("Measurement overlay ROI name must be text or null");return name;
}
MetadataValue presentation_value(MeasurementOverlayPresentation value) {
  MetadataArray rgba;for(auto v:value.label_color)rgba.emplace_back(std::int64_t(v));
  return metadata_object({{"version",1},{"draw_labels",value.draw_labels},{"draw_names",value.draw_names},
      {"draw_backgrounds",value.draw_backgrounds},{"label_color",metadata_array(std::move(rgba))}});
}
}
MeasurementOverlayPresentation measurement_overlay_presentation(const MetadataObject& properties) {
  MeasurementOverlayPresentation out;
  const auto it=properties.find(measurement_overlay_property);if(it==properties.end())return out;
  const auto& values=object(it->second);if(values.size()!=5)bad("Unexpected measurement overlay presentation fields");
  if(integer(field(values,"version"))!=1)bad("Unsupported measurement overlay presentation version");
  out.present=true;out.draw_labels=boolean(field(values,"draw_labels"));out.draw_names=boolean(field(values,"draw_names"));out.draw_backgrounds=boolean(field(values,"draw_backgrounds"));
  const auto node=std::get_if<std::shared_ptr<const MetadataTree>>(&field(values,"label_color"));
  if(!node || !*node)bad("Measurement overlay color requires RGBA");
  const auto color=std::get_if<MetadataArray>(&(*node)->value);if(!color || color->size()!=4)bad("Measurement overlay color requires RGBA");
  for(std::size_t i=0;i<4;++i){const auto v=integer((*color)[i]);if(v>255)bad("Measurement overlay color component exceeds255");out.label_color[i]=static_cast<unsigned char>(v);}
  return out;
}
void ResolvedMeasurementOverlay::validate_source(const DocumentSnapshot& source) const {
  analysis_.validate_source(source);
}
ResolvedMeasurementOverlay resolve_measurement_overlay(const ResolvedTargetAnalysis& analysis,
    std::size_t results_before,const MeasurementOverlayOptions& options,const MeasurementOverlayLimits& limits) {
  if(!limits.max_overlays || !limits.max_results_rows || !limits.max_working_bytes)bad("Invalid measurement overlay limits");
  kernel_checkpoint();
  if(!options.enabled || analysis.options().measurement==TargetMeasurementMode::RoiManager)
    return {analysis,options,limits,false,0,0};
  if(analysis.options().measurement!=TargetMeasurementMode::Selection)
    bad("Add to overlay requires captured selection Measure");
  if(analysis.target().position_count()!=1)bad("Add to overlay requires one captured XY plane");
  const auto& source=analysis.target().source();const auto& data=source.data();
  if(!data.selection)return {analysis,options,limits,false,0,0};
  if(data.overlays.size()>=limits.max_overlays)bad("Measurement overlay count limit exceeded");
  const auto count=analysis.estimate().row_count;
  if(results_before>limits.max_results_rows || count>limits.max_results_rows-results_before)bad("Measurement overlay result counter exceeds limit");
  const auto name=selection_name(*data.selection);auto name_bytes=name?name->size():std::size_t(1);
  if(!name)for(auto counter=results_before+count;counter>=10;counter/=10)++name_bytes;
  if(name_bytes>limits.max_name_bytes)bad("Measurement overlay ROI name exceeds byte limit");
  (void)measurement_overlay_presentation(data.metadata.properties);
  // Reuse the established control-copy walker; snapshots retain pixel owners
  // by shared identity. Bound staging, codec/UTF8 validation and transaction copies.
  const auto control=detail::document_control_copy_bytes(source,limits.max_working_bytes);
  auto bytes=add(32768,mul(control,8));
  const auto shape=data.dataset.shape();
  const auto& selected=*data.selection;
  if((selected.kind=="point" || selected.kind=="multipoint") && shape.plane_count()>1 &&
      !options.composite && int(shape.t>1)+int(shape.c>1)+int(shape.z>1)==1)
    bytes=add(bytes,mul(selected.points.size(),4096)); // new point-position trees, codec and transaction staging
  if(bytes>limits.max_working_bytes)bad("Measurement overlay exceeds working byte limit");
  kernel_checkpoint();return {analysis,options,limits,true,results_before+count,bytes};
}
std::optional<DocumentTransaction> prepare_measurement_overlay(const ResolvedMeasurementOverlay& plan,std::size_t cap) {
  if(!plan.will_add())return std::nullopt;
  if(!cap)cap=plan.limits_.max_working_bytes;
  if(plan.working_bytes()>cap)bad("Measurement overlay exceeds working byte limit");
  kernel_checkpoint();const auto& source=plan.analysis_.target().source();const auto& data=source.data();
  const auto& selection=*data.selection;const auto existing=selection_name(selection);
  const auto name=existing?*existing:std::to_string(plan.result_count_);
  auto cloned=selection;cloned.attributes["imagej_roi_name"]=name;
  const auto shape=data.dataset.shape();const auto position=plan.analysis_.target().position(0);const auto z=position.z;
  const bool stacked=shape.plane_count()>1;
  const int axes=int(shape.t>1)+int(shape.c>1)+int(shape.z>1);
  const bool hyper=stacked && (plan.options_.composite || axes>1);
  if(stacked && !hyper && (cloned.kind=="point" || cloned.kind=="multipoint")) {
    auto points=point_annotations(cloned);
    if(points.empty())points.resize(cloned.points.size());
    for(auto& point:points)point.position=position;
    cloned=with_point_annotations(cloned,points);
  }
  auto encoded=cloned.is_path()?detail::selection_codec::encode_selection(cloned,true,true):codec::selection_to_json(cloned);
  const auto flat=std::uint64_t(add(mul(add(mul(std::size_t(position.t),std::size_t(shape.z)),std::size_t(z)),std::size_t(shape.c)),std::size_t(position.c)+1));
  codec::Json row{{"kind","roi"},{"selection",std::move(encoded)},{"name",name},{"color","#ffff00"},
      {"c",stacked && !hyper && shape.c>1?codec::Json(position.c):codec::Json()},
      {"z",stacked && (hyper || shape.z>1)?codec::Json(z):codec::Json()},
      {"t",stacked && (hyper || shape.t>1)?codec::Json(position.t):codec::Json()},
      {measurement_overlay_row,{{"version",1},{"position",stacked && !hyper?flat:0},{"hyperstack",hyper}}}};
  // Validate retained strings before handing this annotation to any reader.
  try{(void)row.dump();}catch(const codec::Json::exception&){bad("Measurement overlay strings must be valid UTF8");}
  auto overlay=codec::object_from_json(row);
  auto presentation=measurement_overlay_presentation(data.metadata.properties);
  presentation.present=true;presentation.draw_labels=presentation.draw_labels || plan.options_.draw_labels;
  presentation.draw_names=true;presentation.draw_backgrounds=true;presentation.label_color={255,255,255,255};
  auto overlays=data.overlays;overlays.push_back(std::move(overlay));
  auto edit=source.begin_edit(Change::Pixels|Change::Metadata|Change::Selection);
  edit.set_overlays(std::move(overlays));edit.set_property(measurement_overlay_property,presentation_value(presentation));
  kernel_checkpoint();return std::optional<DocumentTransaction>(std::move(edit));
}
} // namespace imagejxx
