#include "indexed_type_internal.hpp"
#include "legacy_rgb_palette.hpp"
#include "axis_command_record.hpp"
#include "operation_internal.hpp"
#include "workflow_stage_metadata.hpp"
#include "session_metadata.hpp"
#include "kernel_control.hpp"
#include <imagejxx/channel_display.hpp>
#include <imagejxx/manual_threshold.hpp>
#include <imagejxx/conversion.hpp>
#include <cmath>
namespace imagejxx::detail {
namespace {
IndexedPalette gray(bool inverted){IndexedPalette p;p.color_count=256;for(int i=0;i<256;++i)p.palette[i].fill(static_cast<unsigned char>(inverted?255-i:i));return p;}
}
std::optional<IndexedTypePlan> resolve_indexed_type_plan(const DocumentSnapshot &source,PixelType type){
  const auto &d=source.data().dataset;
  if(d.shape().s!=1||d.pixel_type()!=PixelType::UInt8||!source.data().metadata.extensions.count(indexed_palette_extension))return {};
  auto stored=indexed_palette(source.data());IndexedTypePlan plan;plan.input=*stored;plan.type=type;
  if(recognized_channel_display(source.display(),d))throw std::invalid_argument("Indexed type conversion with channel display requires explicit RGB expansion first");
  if(manual_threshold_state(source.display(),d.shape()))throw std::invalid_argument("Clear the active threshold before indexed type conversion");
  if(const auto current=legacy_rgb_palette(source.display().properties)){
    if(*current!=plan.input.palette){plan.input.palette=*current;plan.input.color_count=256;}
  }
  plan.deferred=workflow_pending_indexed_palette(d);plan.classification=classify_indexed_palette(plan.input);
  if(type!=PixelType::UInt8 && plan.classification.indexed_color()&&!plan.deferred)throw std::invalid_argument("ImageJ indexed color requires 8-bit grayscale before 16/32-bit conversion");
  const auto range=source.display().properties.find("display_range");
  if(range!=source.display().properties.end()){
    const auto &v=axis_command_record::array_value(range->second);if(v.size()!=2)throw std::invalid_argument("Indexed display range requires two endpoints");
    plan.range=std::array<double,2>{axis_command_record::number_value(v[0]),axis_command_record::number_value(v[1])};if((*plan.range)[0]>(*plan.range)[1])throw std::invalid_argument("Indexed display range must be ordered");
  }
  plan.output=plan.input;
  if(type==PixelType::UInt8){
    plan.render=d.shape().plane_count()==1?!plan.classification.pseudo:plan.classification.color&&!plan.classification.pseudo;
    if(plan.deferred){plan.render=false;plan.output=gray(false);}
    else if(plan.render)plan.output=gray(false);
    else if(d.shape().plane_count()==1||plan.classification.color)plan.output=gray(plan.classification.inverted);
  }
  return plan;
}
OperationResult execute_indexed_type(const Image &input,const IndexedTypePlan &plan,const OperationParameters &parameters){
  if(plan.deferred)throw std::logic_error("Deferred indexed palette cannot execute");
  if(!plan.render)return execute_normalized_operation("convert_image",input,parameters);
  Image codes(256,1,1,PixelType::UInt8);for(int i=0;i<256;++i)codes.pixels[i]=i;
  const auto rgb=convert_rgb(codes,plan.range,plan.input.palette);
  const auto table=execute_normalized_operation("convert_image",rgb,parameters).image;
  Image output(input.width,input.height,1,plan.type);
  with_kernel_stage("Indexed grayscale lookup",input.pixels.size(),[&](const auto &stage){for(std::size_t i=0;i<input.pixels.size();++i){stage.periodic(i);const auto code=input.pixels[i];if(!std::isfinite(code)||code<0||code>255||code!=std::floor(code))throw std::invalid_argument("Indexed pixels require byte codes");output.pixels[i]=table.pixels[std::size_t(code)];}stage.finish();});
  return {std::move(output),std::nullopt,false};
}
DisplayState pair_indexed_type_metadata(const DocumentSnapshot &source,const IndexedTypePlan &plan,DocumentTransaction &edit){
  auto display=source.display();display.properties.erase("lut");display.properties.erase("channel_display");reset_manual_threshold_state(display);
  const auto output=encode_indexed_palette(plan.output);display.properties["custom_lut"]=output.at("palette");
  if(plan.render)display.properties["display_range"]=metadata_array({0.,255.});
  else if(plan.type==PixelType::UInt16)display.properties.erase("display_range");
  edit.set_extension(indexed_palette_extension,metadata_object(output));
  edit.set_extension("imagejxx.indexed_type_source",metadata_object({{"schema",std::string("imagejxx.indexed-type-source")},{"version",1},{"input_palette",metadata_object(encode_indexed_palette(plan.input))},{"stored_palette",source.data().metadata.extensions.at(indexed_palette_extension)},{"input_display",metadata_object(source.display().properties)},{"policy",std::string(plan.render?"palette-to-gray-byte-v1":"preserve-index-values-v1")}}));
  display.normalize(source.data().dataset.shape());edit.set_axis_remap_display(display);return display;
}
bool validate_indexed_filter_eligibility(const DocumentSnapshot &source,const std::string &operation){
  if(operation!="invert" && operation!="smooth" && operation!="sharpen" && operation!="find_edges")return false;
  const auto &data=source.data();const auto &dataset=data.dataset;
  if(dataset.shape().s!=1 || dataset.pixel_type()!=PixelType::UInt8 || !data.metadata.extensions.count(indexed_palette_extension))return false;
  auto palette=*indexed_palette(data);
  if(const auto current=legacy_rgb_palette(source.display().properties))
    if(*current!=palette.palette){palette.palette=*current;palette.color_count=256;}
  if(!workflow_pending_indexed_palette(dataset) && classify_indexed_palette(palette).indexed_color())
    throw std::invalid_argument("ImageJ filter does not support indexed color; convert to 8-bit grayscale or RGB first");
  return true;
}
std::optional<DisplayState> retire_indexed_filter_palette(const DocumentSnapshot &source,
    const Dataset &output,const std::string &operation,DocumentTransaction &edit){
  const auto &input=source.data().dataset;
  if(input.pixel_type()!=PixelType::UInt8 || input.shape().s!=1 ||
      (output.pixel_type()==PixelType::UInt8 && output.shape().s==1) ||
      !source.data().metadata.extensions.count(indexed_palette_extension))return {};
  // Validate the palette before interpreting or retiring a known/future record.
  (void)indexed_palette(source.data());
  edit.set_extension("imagejxx.retired_indexed_palette",source.data().metadata.extensions.at(indexed_palette_extension));
  edit.remove_extension(indexed_palette_extension);
  edit.set_extension("imagejxx.indexed_filter_source",metadata_object({
      {"schema",std::string("imagejxx.indexed-filter-source")},{"version",1},
      {"operation",operation},{"input_display",metadata_object(source.display().properties)},
      {"policy",std::string("retire-non-byte-index-domain-v1")}}));
  auto display=source.display();display.properties.erase("lut");display.properties.erase("custom_lut");
  display.properties.erase("display_range");display.properties.erase("channel_display");
  reset_manual_threshold_state(display);display.normalize(output.shape());
  edit.set_axis_remap_display(display);return display;
}
}
