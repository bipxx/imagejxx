#include "imagej_projection_document.hpp"
#include "axis_command_record.hpp"
#include "kernel_control.hpp"
#include <imagejxx/imagej_intensity_calibration.hpp>
#include <algorithm>
#include <stdexcept>

namespace imagejxx::detail {
namespace {
std::optional<int> position(const MetadataObject& o, const char* key) {
  const auto it=o.find(key);
  if(it==o.end() || std::holds_alternative<std::monostate>(it->second)) return {};
  return axis_command_record::index_value(it->second);
}
std::optional<int> mapped(int value, int first, int count, int group) {
  if(value<first || (value-first)/group>=count) return {};
  return (value-first)/group;
}
std::vector<MetadataObject> native_overlays(const DocumentSnapshot& source,
    const ImageJDatasetProjectionLayout& layout) {
  std::vector<MetadataObject> output;
  const auto shape=source.data().dataset.shape();
  for(auto value:source.data().overlays) {
    kernel_checkpoint_periodic();
    auto t=position(value,"t"),c=position(value,"c"),z=position(value,"z");
    const auto slice=position(value,"slice_index");
    if(slice && z && slice!=z) throw std::invalid_argument("Conflicting projection overlay Z positions");
    if(!z) z=slice;
    // A prior result may retain a hidden position outside its output extent.
    // Keep it hidden by excluding it from all actual source contributors.
    if((t && *t>=shape.t)||(c && *c>=shape.c)||(z && *z>=shape.z)) continue;
    if(t) t=mapped(*t,layout.axis==ImageJProjectionAxis::Time?layout.source_first:layout.source_first_t,layout.shape.t,
        layout.axis==ImageJProjectionAxis::Time?layout.contributors_per_group:1);
    if(c) c=mapped(*c,layout.axis==ImageJProjectionAxis::Channels?layout.source_first:layout.source_first_c,layout.shape.c,
        layout.axis==ImageJProjectionAxis::Channels?layout.contributors_per_group:1);
    if(z) z=mapped(*z,layout.axis==ImageJProjectionAxis::Z?layout.source_first:0,
        layout.shape.z,layout.axis==ImageJProjectionAxis::Z?layout.contributors_per_group:1);
    if((position(value,"t") && !t)||(position(value,"c") && !c)||
       ((position(value,"z")||slice) && !z)) continue;
    if(t) value["t"]=*t;
    if(c) value["c"]=*c;
    if(z) {
      if(position(value,"z")) value["z"]=*z;
      if(slice) value["slice_index"]=*z;
    }
    output.push_back(std::move(value));
  }
  return output;
}
// ImageJ ordinary API overlay rules are deliberately different from native
// contributor mapping. Missing positions are ImageJ's one-based wildcard0;
// native zero-based optional coordinates encode the resulting visible meaning.
std::vector<MetadataObject> overlays(const DocumentSnapshot& source,
    const ImageJDatasetProjectionLayout& layout,
    const ImageJRGBProjectionRequest& request) {
  const auto shape=source.data().dataset.shape();
  const bool ordinary=request.group_size==0 && request.all_channels &&
      ((layout.axis==ImageJProjectionAxis::Z && shape.z>1) ||
       (layout.axis==ImageJProjectionAxis::Time && shape.z==1) ||
       (layout.axis==ImageJProjectionAxis::Channels && shape.t==1 && shape.z==1));
  if(!ordinary) return native_overlays(source,layout);
  const bool hyper=layout.axis!=ImageJProjectionAxis::Channels &&
      ((shape.c>1 && (shape.t>1 || shape.z>1)) || (shape.t>1 && shape.z>1));
  std::vector<MetadataObject> output;
  for(auto value:source.data().overlays) {
    kernel_checkpoint_periodic();
    const auto c=position(value,"c"),z=position(value,"z"),t=position(value,"t");
    const auto flat=position(value,"slice_index");
    if(z && flat && z!=flat) throw std::invalid_argument("Conflicting projection overlay Z positions");
    const bool positioned=value.count("t") || value.count("c") || value.count("z");
    // Compare zero-based indices without adding1 (hidden INT_MAX positions
    // remain legal); an absent optional corresponds to ImageJ wildcard0.
    const auto zs=positioned?z:flat;
    const bool inside=zs && *zs>=request.first && *zs<=request.last;
    auto clear=[&] { for(auto key:{"t","c","z","slice_index"}) value.erase(key); };
    if(shape.s==3) {
      // ImageJ's RGB projector intentionally admits wildcard C or T even
      // outside the selected Z range, then retains the original flat T.
      if(!(inside || !zs || !c || !t)) continue;
      if(layout.shape.t==1 && t && *t!=request.active_t) continue;
      clear();
      if(t) {
        if(*t<layout.shape.t) value["t"]=*t;
        else value["slice_index"]=*t; // retained hidden flat position
      }
    } else if(!hyper) {
      // Hyper-positioned ROIs have legacy getPosition()==0. Legacy stack
      // positions retain their original value, including positions hidden
      // beyond the one-plane output; never collapse them to the first plane.
      if(!positioned && flat && !inside) continue;
      clear();
      if(!positioned && flat) value["slice_index"]=*flat;
    } else if(layout.shape.t==1) {
      if(!((inside && t && *t==request.active_t) || !c)) continue;
      clear();if(c) value["c"]=*c;
    } else {
      if(!inside && zs) continue;
      clear();
      if(c) value["c"]=*c;
      if(inside) value["z"]=0;
      if(t) value["t"]=*t;
    }
    output.push_back(std::move(value));
  }
  return output;
}
const char* prefix(const std::string& method) {
  if(method=="maximum") return "MAX_";
  if(method=="minimum") return "MIN_";
  if(method=="sum") return "SUM_";
  if(method=="stddev") return "STD_";
  if(method=="median") return "MED_";
  return "AVG_";
}
ChannelDisplayLut legacy_lut(const DisplayState& display) {
  ChannelDisplayLut lut;
  const auto custom=display.properties.find("custom_lut");
  if(custom!=display.properties.end() && !std::holds_alternative<std::monostate>(custom->second)) {
    const auto& rows=axis_command_record::array_value(custom->second);
    if(rows.size()!=256) throw std::invalid_argument("Projection LUT requires256 rows");
    ChannelDisplayTable table;
    for(std::size_t i=0;i<256;++i) {
      const auto& row=axis_command_record::array_value(rows[i]);
      if(row.size()!=3) throw std::invalid_argument("Projection LUT requires RGB rows");
      for(std::size_t k=0;k<3;++k) {
        const auto v=axis_command_record::unsigned_value(row[k]);
        if(v>255) throw std::invalid_argument("Projection LUT component exceeds255");
        table[i][k]=static_cast<std::uint8_t>(v);
      }
    }
    lut.name.clear();lut.table=std::move(table);
  } else if(const auto it=display.properties.find("lut");it!=display.properties.end()) {
    lut.name=axis_command_record::text_value(it->second);
    (void)channel_display_lut(lut.name);
  }
  return lut;
}
} // namespace
ImageJProjectionDocument project_imagej_document(const DocumentSnapshot& source_input,
    const ImageJProjectionDocumentRequest& input,const ExecutionControl& control) {
  const auto source=source_input;
  if(input.projection.signed16_density || !input.projection.calibration_table.empty())
    throw std::invalid_argument("Prepared projection density comes from the source document");
  const auto request=input;
  auto projection=request.projection;
  const auto& dataset=source.data().dataset;
  const auto shape=dataset.shape();
  const bool rgb=shape.s==3;
  const auto intensity=read_imagej_intensity_calibration(source);
  apply_imagej_intensity_calibration(intensity,dataset.pixel_type(),shape.s,projection);
  const auto layout=rgb?describe_imagej_rgb_projection(dataset,projection):
      describe_imagej_dataset_projection(dataset,projection);
  if(!rgb) {
    // Match the existing reset path's conservative plane/state/range/index
    // bound before creating output pixels or state. This remains a separate
    // display bound; it is not a full job/provider-cache admission estimate.
    const auto pixels=static_cast<std::size_t>(layout.shape.x)*layout.shape.y;
    const auto per_channel=sizeof(ChannelDisplayChannel)+8+sizeof(ChannelDisplayRange)+2*sizeof(int);
    const auto count=static_cast<std::size_t>(layout.shape.c);
    const auto maximum=std::numeric_limits<std::size_t>::max();
    if(pixels>maximum/sizeof(double) || count>(maximum-pixels*sizeof(double))/per_channel ||
       !request.display_limits.max_pixels || !request.display_limits.max_working_bytes ||
       pixels>request.display_limits.max_pixels ||
       pixels*sizeof(double)+count*per_channel>request.display_limits.max_working_bytes)
      throw std::invalid_argument("Projection display limits exceeded");
  }
  const auto channels=recognized_channel_display(source.display(),dataset);
  const bool source_palette=!rgb && layout.shape.c==1 && layout.pixel_type!=PixelType::Float32 &&
      projection.method!="median";
  auto lut=(source_palette && !channels)?(request.source_lut?*request.source_lut:legacy_lut(source.display())):ChannelDisplayLut{};
  if(source_palette && channels) lut=channels->channels[projection.active_c].lut;
  if(!source_palette) lut=ChannelDisplayLut{};
  ExecutionScope scope(control);
  kernel_checkpoint();
  // Validate and capture all annotation metadata before processing pixels.
  auto output_overlays=overlays(source,layout,projection);
  const auto result=rgb?project_imagej_rgb_dataset(dataset,projection):
      project_imagej_dataset(dataset,projection);
  kernel_checkpoint();
  ImageJProjectionDocument output{
      DocumentData{result.dataset,{},{},{},{},{},{}},{},layout};
  output.data.metadata.title=request.output_title.value_or(
      std::string(prefix(projection.method))+source.data().metadata.title);
  output.data.overlays=std::move(output_overlays);
  const auto output_intensity=projected_imagej_intensity_calibration(intensity,
      result.dataset.pixel_type(),shape.s,result.preserve_density_calibration);
  if(output_intensity.has_density() || output_intensity.value_unit()!="Gray Value")
    output.data.metadata.properties[imagej_intensity_property]=output_intensity.record();
  // A fresh display intentionally resets source navigation/camera/threshold and
  // unrelated interpretation. C/T/Z start at the first actual output position.
  if(!rgb) {
    ChannelDisplayState state;
    state.mode=layout.shape.c>1?ChannelDisplayMode::Composite:ChannelDisplayMode::Color;
    state.version=result.dataset.sample_policy()==FloatingSamplePolicy::PreserveIEEEFloat32?
        ChannelDisplayVersion::V3:ChannelDisplayVersion::V1;
    state.channels.resize(static_cast<std::size_t>(layout.shape.c));
    const bool copy_channels=channels && layout.axis!=ImageJProjectionAxis::Channels;
    if(copy_channels && layout.shape.c>1) state.mode=channels->mode;
    if(!copy_channels && layout.shape.c>1)
      state=default_channel_display(result.dataset,0,0,ChannelDisplayMode::Composite,request.display_limits);
    for(int c=0;c<layout.shape.c;++c) {
      if(copy_channels && layout.shape.c>1) state.channels[c]=channels->channels[layout.source_first_c+c];
      else if(layout.shape.c==1) state.channels[c].lut=lut;
    }
    const bool preserve_ranges=layout.shape.c>1 &&
        projection.method!="sum" && projection.method!="stddev";
    if(!preserve_ranges)
      state=reset_channel_display_ranges(result.dataset,state,0,0,{},request.display_limits).first;
    if(layout.axis==ImageJProjectionAxis::Channels && !projection.group_size && request.desktop_grays)
      for(auto& channel:state.channels) channel.lut=ChannelDisplayLut{};
    output.display=with_channel_display(std::move(output.display),result.dataset,std::move(state));
  }
  output.data.validate();
  output.display.validate(layout.shape);
  kernel_checkpoint();
  return output;
}
} // namespace imagejxx::detail
