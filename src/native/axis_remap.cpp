#include <imagejxx/axis_remap.hpp>
#include <imagejxx/planar_annotations.hpp>
#include <imagejxx/manual_threshold.hpp>
#include "session_metadata.hpp"
#include "kernel_control.hpp"
#include "axis_region_remap.hpp"
#include <algorithm>
#include <limits>

namespace imagejxx {
RegularAxisMapping::RegularAxisMapping(int extent,int first,int last,int step)
    : extent_(extent),first_(first),step_(step),count_(0) {
  if (extent<=0 || first<0 || last<first || last>=extent || step<=0)
    throw std::invalid_argument("Invalid inclusive regular axis subset");
  count_=(last-first)/step+1;
}
int RegularAxisMapping::source_index(int output) const {
  if(output<0 || output>=count_)throw std::out_of_range("Output axis index");
  return first_+output*step_;
}
std::optional<int> RegularAxisMapping::output_index(int source) const {
  if(source<0 || source>=extent_)throw std::out_of_range("Source axis index");
  if(source<first_ || (source-first_)%step_)return {};
  const int output=(source-first_)/step_;
  return output<count_?std::optional<int>(output):std::nullopt;
}
int RegularAxisMapping::nearest_output(int source) const {
  if(source<0 || source>=extent_)throw std::out_of_range("Active axis index");
  if(source<=first_)return 0;
  const int lower=std::min((source-first_)/step_,count_-1);
  if(lower==count_-1)return lower;
  return source-source_index(lower)>source_index(lower+1)-source?lower+1:lower;
}
namespace {
namespace codec=detail::session_metadata;
using Json=codec::Json;
std::optional<int> position(const Json &object,const char *key,int extent) {
  if(!object.contains(key) || object[key].is_null())return {};
  const auto &value=object[key];
  if(!value.is_number_integer() || value<0 || value>=extent)
    throw std::invalid_argument(std::string("Invalid overlay position: ")+key);
  return value.get<int>();
}
MetadataValue labels(const MetadataValue &value,const RegularAxisMapping &map,
                     DatasetShape shape,bool planes) {
  auto input=codec::to_json(value);
  const auto expected=planes?shape.plane_count():static_cast<std::size_t>(shape.z);
  if(!input.is_array() || input.size()!=expected)
    throw std::invalid_argument("Axis labels must match their source extent");
  for(const auto &label:input)
    if(!label.is_null() && !label.is_string())throw std::invalid_argument("Axis labels must be strings or null");
  Json output=Json::array();
  const int batches=planes?shape.t*shape.c:1;
  for(int batch=0;batch<batches;++batch)for(int z=0;z<map.count();++z)
    output.push_back(input[static_cast<std::size_t>(batch)*shape.z+map.source_index(z)]);
  return codec::from_json(output);
}
}
static PreparedAxisRemap prepare_z_axis_remap_frozen(const DocumentSnapshot &source,const ZSubsetRequest &request,
                                      const ExecutionControl &control) {
  ExecutionScope scope(control);kernel_checkpoint();
  const auto &data=source.data();const auto shape=data.dataset.shape();
  RegularAxisMapping map(shape.z,request.first,request.last,request.step);
  if(request.destination!=OutputDestination::NewDocument && request.destination!=OutputDestination::InPlace)
    throw std::invalid_argument("Invalid Z subset destination");
  auto output_shape=shape;output_shape.z=map.count();
  if(!request.max_planes || output_shape.plane_count()>request.max_planes || shape.plane_count()>request.max_planes)
    throw std::invalid_argument("Z subset exceeds shared-plane table limit");
  const auto table=data.dataset.shared_planes();
  if(!table)throw std::invalid_argument("Regular Z subset requires eager shared-plane storage");
  if(data.spectrum && !map.identity())throw std::invalid_argument("Z subset cannot retain a Fourier spectrum");
  auto calibration=data.dataset.calibration();
  calibration.origin=calibration.voxel_to_world({0,0,double(request.first)});
  calibration.spacing[2]*=request.step;calibration.validate();
  auto edit=source.begin_edit(Change::All);auto display=source.display();
  // Preserve exact identity and all metadata for a genuine no-op.
  if(map.identity())return {std::move(edit),std::move(display)};
  std::vector<std::size_t> indices;indices.reserve(output_shape.plane_count());
  for(int t=0;t<shape.t;++t)for(int c=0;c<shape.c;++c)for(int z=0;z<map.count();++z) {
    kernel_checkpoint_periodic();
    indices.push_back((static_cast<std::size_t>(t)*shape.c+c)*shape.z+map.source_index(z));
  }
  edit.replace_dataset(Dataset::from_shared_planes(output_shape,table->subset(indices),calibration));
  if(data.metadata.extensions.count(planar_annotations_extension)) {
    auto annotations=planar_annotations(data);
    std::vector<PlanarAnnotation> mapped;
    for(auto row:annotations) {
      if(row.plane==Plane::XY) {
        const auto target=map.output_index(row.coordinate);
        if(!target)continue;
        row.coordinate=*target;
      } else {
        // Cell boundaries map through voxel centers, including the half-cell
        // offset. Packed masks gather the retained source Z rows exactly.
        for(auto &point:row.selection.points)
          point[1]=(point[1]-.5-request.first)/request.step+.5;
        if(row.selection.is_region()) {
          row.selection=detail::region_z_subset(row.selection,request);
        } else if(row.selection.kind=="mask") {
          const auto width=row.selection.mask_width();
          const auto bits=std::size_t(width)*std::size_t(map.count());
          if(bits>16*1024*1024*8ULL)throw std::invalid_argument("Mapped planar mask exceeds its metadata cap");
          std::vector<unsigned char> packed((bits+7)/8);
          const auto &source_bits=row.selection.packed_mask();
          for(int y=0;y<map.count();++y)for(int x=0;x<width;++x) {
            kernel_checkpoint_periodic();
            const auto source_bit=std::size_t(map.source_index(y))*width+x;
            const auto target_bit=std::size_t(y)*width+x;
            if(source_bits[source_bit/8] & (1u<<(7-source_bit%8)))packed[target_bit/8]|=1u<<(7-target_bit%8);
          }
          auto geometry=Selection::from_packed_mask(width,map.count(),std::move(packed));
          geometry.points=row.selection.points;geometry.width=row.selection.width;geometry.attributes=row.selection.attributes;
          row.selection=std::move(geometry);
        }
      }
      // Unsupported affine box/shear combinations refuse before publication;
      // never preserve a rotated box with invalid perpendicular axes.
      row.validate(output_shape);mapped.push_back(std::move(row));
    }
    if(mapped.empty())edit.remove_extension(planar_annotations_extension);
    else edit.set_extension(planar_annotations_extension,encode_planar_annotations(output_shape,mapped));
  }
  std::vector<Roi> rois;rois.reserve(data.rois.size());
  for(auto roi:data.rois) {
    kernel_checkpoint_periodic();
    if(roi.z) {auto mapped=map.output_index(*roi.z);if(!mapped)continue;roi.z=*mapped;}
    rois.push_back(std::move(roi));
  }
  edit.set_rois(std::move(rois));
  std::vector<MetadataObject> overlays;overlays.reserve(data.overlays.size());
  for(const auto &overlay:data.overlays) {
    kernel_checkpoint_periodic();auto value=codec::to_json(overlay);
    (void)position(value,"t",shape.t);(void)position(value,"c",shape.c);
    const auto z=position(value,"z",shape.z),slice=position(value,"slice_index",shape.z);
    if(z && slice && z!=slice)throw std::invalid_argument("Conflicting overlay Z positions");
    const auto old=z?z:slice;
    if(old) {
      const auto mapped=map.output_index(*old);if(!mapped)continue;
      if(z)value["z"]=*mapped;if(slice)value["slice_index"]=*mapped;
    }
    overlays.push_back(codec::object_from_json(value));
  }
  edit.set_overlays(std::move(overlays));
  for(const auto *key:{"slice_labels","plane_labels"}) {
    auto p=data.metadata.properties.find(key);
    if(p!=data.metadata.properties.end())edit.set_property(key,labels(p->second,map,shape,std::string(key)=="plane_labels"));
    auto e=data.metadata.extensions.find(key);
    if(e!=data.metadata.extensions.end())edit.set_extension(key,labels(e->second,map,shape,std::string(key)=="plane_labels"));
  }
  // Legacy stack flag is a descriptor, while the 'labels' Boolean refers to
  // label-image intensities and must stay intact.
  if(data.metadata.extensions.count("is_stack"))edit.set_extension("is_stack",output_shape.plane_count()>1);
  if(data.metadata.properties.count("is_stack"))edit.set_property("is_stack",output_shape.plane_count()>1);
  const auto threshold=manual_threshold_state(display,shape);
  display.crosshair[2]=map.nearest_output(display.crosshair[2]);
  if(threshold) {
    if(threshold->ranges){auto state=*threshold;const auto old=(*state.ranges)[2];int first=-1,last=-1,stride=0;
      for(int z=0;z<map.count();++z){kernel_checkpoint_periodic();const int source_z=map.source_index(z);
        if(source_z>=old.start && source_z<*old.stop && (source_z-old.start)%old.step==0){if(first<0)first=z;else if(!stride)stride=z-last;last=z;}
      }
      if(first<0)reset_manual_threshold_state(display);
      else {(*state.ranges)[2]={first,last+1,stride?stride:1};state.position.z=map.nearest_output(state.position.z);set_manual_threshold_state(display,output_shape,state);}
    }else {
    const auto mapped=map.output_index(threshold->position.z);
    if(mapped || threshold->all_z) {auto state=*threshold;state.position.z=mapped?*mapped:map.nearest_output(threshold->position.z);set_manual_threshold_state(display,output_shape,state);}
    else reset_manual_threshold_state(display);
    }
  }
  edit.set_axis_remap_display(display);
  kernel_checkpoint();return {std::move(edit),std::move(display)};
}
PreparedAxisRemap prepare_z_axis_remap(const DocumentSnapshot &source,const ZSubsetRequest &request,
                                      const ExecutionControl &control) {
  const auto owned_source=source;
  const auto owned_request=request;
  const auto owned_control=control;
  const RegularAxisMapping mapping(owned_source.data().dataset.shape().z,
      owned_request.first,owned_request.last,owned_request.step);
  if(!mapping.identity())for(const auto &row:planar_annotations(owned_source.data()))
    if(row.plane!=Plane::XY && row.selection.is_region())
      detail::admit_region_z_subset(row.selection,owned_request);
  return prepare_z_axis_remap_frozen(owned_source,owned_request,owned_control);
}
} // namespace imagejxx
