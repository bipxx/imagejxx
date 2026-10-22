#include "workflow_stage_metadata.hpp"
#include "contrast_apply_workflow.hpp"
#include <imagejxx/channel_display.hpp>
#include <imagejxx/manual_threshold.hpp>
#include <imagejxx/planar_display_annotations.hpp>
#include <imagejxx/targets.hpp>
#include "kernel_control.hpp"
#include "retained_resource_internal.hpp"
#include "indexed_type_internal.hpp"
#include <algorithm>
#include <set>
namespace imagejxx::detail {
namespace {
class DescriptorSource final : public PlaneSource {
  DatasetShape shape_;
  PixelType type_;
  bool pending_;
public:
  DescriptorSource(DatasetShape shape,PixelType type,bool pending):shape_(shape),type_(type),pending_(pending){}
  bool pending_palette()const{return pending_;}
  DatasetShape shape() const override{return shape_;}
  PixelType pixel_type() const override{return type_;}
  std::shared_ptr<const Image> read_plane(PlanePosition) const override {
    throw std::logic_error("Workflow preflight cannot acquire samples");
  }
};
}
Dataset workflow_metadata_dataset(DatasetShape shape,PixelType type,
                                   const SpatialCalibration &calibration,bool pending) {
  return Dataset(std::make_shared<DescriptorSource>(shape,type,pending),calibration);
}
bool workflow_pending_indexed_palette(const Dataset &dataset){const auto *s=dynamic_cast<const DescriptorSource *>(ResourceAccess::source(dataset));return s&&s->pending_palette();}
DisplayState pair_numeric_conversion_metadata(const DocumentSnapshot &source,
                                               DatasetShape shape,
                                               DocumentTransaction &edit,PixelType type) {
  if(source.data().dataset.shape().s!=shape.s &&
      source.data().metadata.extensions.count(planar_display_annotations_extension))
    edit.set_extension(planar_display_annotations_extension,
        encode_planar_display_annotations(shape,planar_display_annotations(source.data())));
  auto display=source.display();display.properties.erase("display_range");
  display.properties.erase("channel_display");reset_manual_threshold_state(display);
  if(const auto plan=resolve_indexed_type_plan(source,type))return pair_indexed_type_metadata(source,*plan,edit);
  if(source.data().dataset.shape().s!=1 && source.data().metadata.extensions.count(indexed_palette_extension)){
    edit.set_extension("imagejxx.retired_indexed_palette",source.data().metadata.extensions.at(indexed_palette_extension));edit.remove_extension(indexed_palette_extension);
    display.properties.erase("lut");display.properties.erase("custom_lut");
  }
  display.normalize(shape);edit.set_axis_remap_display(display);return display;
}
DocumentSnapshot scalar_conversion_metadata_preview(const DocumentSnapshot &source,
                                                      PixelType type) {
  const auto &before=source.data().dataset;
  if(before.shape().s==1 && before.pixel_type()==type && !source.data().metadata.extensions.count(indexed_palette_extension))return source;
  auto shape=before.shape();shape.s=1;auto edit=source.begin_edit(Change::All);
  edit.replace_dataset(workflow_metadata_dataset(shape,type,before.calibration()));
  edit.set_spectrum({});pair_numeric_conversion_metadata(source,shape,edit,type);
  return edit.preview();
}
DocumentSnapshot contrast_apply_metadata_preview(const ResolvedContrastApplyCommand &command) {
  const auto &target=command.target();const auto &source=target.source();
  const auto &input=source.data().dataset;const auto options=command.options();
  std::set<int> affected;
  for(std::size_t i=0;i<target.position_count();++i){
    kernel_checkpoint();const auto mask=target.mask(i);bool any=!mask;
    if(mask)for(std::size_t p=0;p<mask->pixels.size();++p){kernel_checkpoint_periodic();any=any||mask->pixels[p]!=0;}
    if(any)affected.insert(target.position(i).c);
  }
  if(affected.empty())return source;
  auto edit=source.begin_edit(Change::All);
  edit.replace_dataset(workflow_metadata_dataset(input.shape(),input.pixel_type(),input.calibration()));
  edit.set_spectrum({});auto display=source.display();
  // Numeric placeholders bound metadata only. Execution computes observed
  // automatic UInt16 bounds from output samples, including outside each ROI.
  const std::array<double,2> range{0,input.pixel_type()==PixelType::UInt8?255.:
      options.unsigned16_bits?double((1u<<options.unsigned16_bits)-1):65535.};
  if(auto channels=recognized_channel_display(display,input)){
    for(auto c:affected)channels->channels.at(c).range=range;
    display=with_channel_display(std::move(display),input,std::move(channels));
  } else display.properties["display_range"]=metadata_array({range[0],range[1]});
  reset_manual_threshold_state(display);edit.set_axis_remap_display(std::move(display));return edit.preview();
}
DocumentSnapshot workflow_filter_metadata_preview(const DocumentSnapshot &source,
    const std::string &operation,const OperationParameters &parameters,const ResolvedTarget &target) {
  const auto &input=source.data().dataset;auto output=input.pixel_type();
  static const std::set<std::string> byte_output={"threshold","auto_mask","in_range","local_threshold",
      "fill_holes","binary_outline","skeletonize","remove_small_objects","clear_border","local_maxima",
      "binary_erode","binary_dilate","binary_open","binary_close"};
  static const std::set<std::string> double_output={"variance_filter","find_edges","laplacian",
      "difference_of_gaussians","anisotropic_diffusion","distance_transform","label","binary_watershed"};
  if(byte_output.count(operation))output=PixelType::UInt8;
  else if(double_output.count(operation) || (operation=="rank_filter" && std::get<std::string>(parameters.at("method"))=="variance"))output=PixelType::Float64;
  else if(operation=="convert" || operation=="calculate"){
    const auto found=parameters.find("dtype");
    if(found!=parameters.end())if(const auto name=std::get_if<std::string>(&found->second);name && *name!="preserve"){
      if(*name=="uint8")output=PixelType::UInt8;else if(*name=="uint16")output=PixelType::UInt16;
      else if(*name=="float32")output=PixelType::Float32;else if(*name=="float64")output=PixelType::Float64;
      else throw std::invalid_argument("Unknown workflow output dtype");
    }
  }
  if(output==input.pixel_type())return source;
  bool any=false,full=true;
  for(std::size_t i=0;i<target.position_count();++i){
    kernel_checkpoint();const auto mask=target.mask(i);
    if(!mask)any=true;
    else {bool mask_any=false,mask_full=true;for(std::size_t p=0;p<mask->pixels.size();++p){kernel_checkpoint_periodic();mask_any=mask_any||mask->pixels[p]!=0;mask_full=mask_full&&mask->pixels[p]!=0;}any=any||mask_any;full=full&&mask_full;}
    if(any && !full)break;
  }
  if(!any)return source;
  if(!full || target.position_count()!=input.shape().plane_count())output=static_cast<PixelType>(std::max(int(output),int(input.pixel_type())));
  if(output==input.pixel_type())return source;
  auto edit=source.begin_edit();edit.replace_dataset(workflow_metadata_dataset(input.shape(),output,input.calibration()));edit.set_spectrum({});
  (void)retire_indexed_filter_palette(source,edit.preview().data().dataset,operation,edit);
  auto preview=edit.preview();return preview.with_display(preview.display());
}
}
