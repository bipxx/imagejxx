#include "color_stack_internal.hpp"
#include "color_components_internal.hpp"
#include "rgb_dataset_internal.hpp"
#include "axis_command_record.hpp"
#include "imagej_roi_import_internal.hpp"
#include "session_metadata.hpp"
#include "workflow_stage_metadata.hpp"
#include "kernel_control.hpp"
#include <imagejxx/manual_threshold.hpp>
#include <imagejxx/planar_display_annotations.hpp>
#include <algorithm>
#include <cmath>
#include <limits>
#include <type_traits>
namespace imagejxx {
namespace {
namespace wire=detail::axis_command_record;
namespace codec=detail::session_metadata;
struct Budget {
  std::size_t cap,used=0;
  void add(std::size_t n){if(n>cap-used)throw std::invalid_argument("Color stack working-byte limit");used+=n;}
  void product(std::size_t n,std::size_t k){if(n && k>(cap-used)/n)throw std::invalid_argument("Color stack working-byte limit");add(n*k);}
  void record(const MetadataObject &v){add(detail::imagej_roi_record_bytes(v,cap-used));}
};
const char *action_name(ColorStackAction a){switch(a){case ColorStackAction::Split:return "split";case ColorStackAction::Merge:return "merge";case ColorStackAction::FlattenRgb:return "flatten_rgb";}throw std::invalid_argument("Unknown color stack action");}
const char *space_name(ColorComponentSpace s){switch(s){case ColorComponentSpace::RGB:return "rgb";case ColorComponentSpace::HSB8:return "hsb8";case ColorComponentSpace::HSB32:return "hsb32";}throw std::invalid_argument("Unknown color stack space");}
const char *layout_name(ColorStackLayoutPolicy p){if(p==ColorStackLayoutPolicy::PreserveAxes)return "preserve_axes";if(p==ColorStackLayoutPolicy::ImageJLegacy)return "imagej_legacy";throw std::invalid_argument("Unknown color stack layout");}
bool boolean(const MetadataValue &v){const auto *p=std::get_if<bool>(&v);if(!p)throw std::invalid_argument("Color stack flag requires Boolean");return *p;}
MetadataValue dimensions(DatasetShape s){MetadataArray a;for(int n:s.dimensions())a.emplace_back(n);return metadata_array(std::move(a));}
bool flat_split(const ColorStackRequest &r){return r.action==ColorStackAction::Split && r.layout==ColorStackLayoutPolicy::ImageJLegacy && r.space!=ColorComponentSpace::RGB;}
bool flat_merge(const ColorStackRequest &r){return r.action==ColorStackAction::Merge && r.layout==ColorStackLayoutPolicy::ImageJLegacy;}
bool flat_rgb(DatasetShape s,const ColorStackRequest &r){return r.action==ColorStackAction::FlattenRgb && s.c>1;}
bool changes_nonxy(DatasetShape s,const ColorStackRequest &r){return (flat_split(r)&&(s.t>1||s.c>1)) || flat_rgb(s,r) || (flat_merge(r)&&s.z>1);}
int index(PlanePosition p,DatasetShape s){return int((std::size_t(p.t)*s.z+p.z)*s.c+p.c);}
PlanePosition unindex(int i,DatasetShape s){return {int(std::size_t(i)/(std::size_t(s.c)*s.z)),i%s.c,(i/s.c)%s.z};}
int group_size(DatasetShape s){return s.c==2?2:3;}
struct Position {std::optional<int> t,c,z;};
// A wildcard remains a wildcard whenever that set is still representable.
// Legacy reindexing expands partial wildcard sets into exact output slices.
template<class F>void positions(Position p,DatasetShape s,const ColorStackRequest &r,F emit){
  for(const auto &v:{std::pair{p.t,s.t},std::pair{p.c,s.c},std::pair{p.z,s.z}})if(v.first && (*v.first<0 || *v.first>=v.second))throw std::invalid_argument("Color stack annotation position outside source");
  if(flat_merge(r)){emit(Position{p.t?std::optional<int>(0):std::nullopt,p.c?std::optional<int>(0):std::nullopt,p.z?std::optional<int>(0):std::nullopt});return;}
  if(flat_split(r) && s.t==1 && s.c==1){p.c.reset();emit(p);return;}
  if(flat_split(r)||flat_rgb(s,r)){
    if(!p.t && !p.c && !p.z){emit(Position{});return;}
    for(int t=p.t.value_or(0);t<(p.t?*p.t+1:s.t);++t)for(int z=p.z.value_or(0);z<(p.z?*p.z+1:s.z);++z)for(int c=p.c.value_or(0);c<(p.c?*p.c+1:s.c);++c)
      emit(Position{0,std::nullopt,index({t,c,z},s)});
    return;
  }
  if(r.action==ColorStackAction::Split && p.c){for(int k=0;k<3;++k){auto q=p;q.c=3*(*p.c)+k;emit(q);}return;}
  if(r.action==ColorStackAction::Merge && p.c)p.c=*p.c/group_size(s);
  emit(p);
}
std::optional<int> field(const codec::Json &j,const char *key,int size){
  if(!j.contains(key)||j.at(key).is_null())return {};
  const auto &v=j.at(key);if(!v.is_number_integer() || v<0 || v>=size)throw std::invalid_argument("Color stack overlay position outside source");return v.get<int>();
}
Position overlay_position(const MetadataObject &o,DatasetShape s){const auto j=codec::to_json(o);return {field(j,"t",s.t),field(j,"c",s.c),field(j,"z",s.z)};}
void row_limit(std::size_t &n){if(++n>4096)throw std::invalid_argument("Color stack remapped annotation row limit");}
std::size_t metadata_charge(const DocumentSnapshot &source,const ColorStackRequest &r,std::size_t cap){
  const auto &d=source.data();const auto s=d.dataset.shape();Budget b{cap};b.add(65536);b.record(d.metadata.properties);b.record(d.metadata.extensions);b.record(d.metadata.record_layout);b.record(source.display().properties);b.add(d.metadata.title.size());
  auto selection=[&](const Selection &v){b.add(sizeof(Selection));b.product(v.points.size(),sizeof(v.points[0]));b.add(v.packed_mask().size());b.product(v.path_commands().size(),sizeof(double));b.record(v.attributes);};
  if(d.selection)selection(*d.selection);
  if(d.previous_selection)selection(*d.previous_selection);
  std::size_t fanout=1,n=0;
  auto count=[&](Position p){std::size_t generated=0;positions(p,s,r,[&](Position){row_limit(n);++generated;});fanout=std::max(fanout,generated);};
  for(const auto &roi:d.rois){b.add(sizeof(Roi)+roi.name.size());selection(roi.selection);b.record(roi.attributes);count({roi.t,roi.c,roi.z});}
  n=0;for(const auto &o:d.overlays){b.record(o);count(overlay_position(o,s));}
  auto annotation_count=[&](const auto &rows){n=0;for(const auto &a:rows){
    if(a.plane!=Plane::XY && changes_nonxy(s,r)){if(!r.allow_nonxy_annotation_drop)throw std::invalid_argument("Legacy color layout requires explicit non-XY annotation removal consent");continue;}
    count({a.t,a.c,a.plane==Plane::XY?std::optional<int>(a.coordinate):std::nullopt});
  }};
  annotation_count(planar_annotations(d));annotation_count(planar_display_annotations(d));
  // Slice label replication is independent of annotation wildcard expansion.
  if((flat_split(r)||flat_rgb(s,r)) && (d.metadata.properties.count("slice_labels")||d.metadata.extensions.count("slice_labels")))fanout=std::max(fanout,std::size_t(s.t)*s.c);
  if(r.action==ColorStackAction::Split)fanout=std::max<std::size_t>(fanout,3);
  Budget total{cap};total.product(b.used,16);total.product(total.used,fanout-1);return total.used;
}
MetadataObject request_record(const ColorStackRequest &r){return {{"action",std::string(action_name(r.action))},{"space",std::string(space_name(r.space))},{"layout",std::string(layout_name(r.layout))},{"destination",std::string(r.destination==OutputDestination::InPlace?"in_place":"new_document")},{"invert_rgb",r.invert_rgb},{"allow_calibration_reset",r.allow_calibration_reset},{"allow_nonxy_annotation_drop",r.allow_nonxy_annotation_drop},{"max_working_bytes",std::uint64_t(r.max_working_bytes)}};}
MetadataObject layout_record(const ColorStackLayout &l){
  return {{"shape",dimensions(l.shape)},{"pixel_type",std::string(l.pixel_type==PixelType::Float32?"float32":"uint8")},{"calibration_reset",l.calibration_reset},{"output_bytes",std::uint64_t(l.output_bytes)},{"working_bytes",std::uint64_t(l.working_bytes)}};
}
ColorStackRequest read_request(const MetadataObject &v){
  wire::exact_fields(v,{"action","space","layout","destination","invert_rgb","allow_calibration_reset","allow_nonxy_annotation_drop","max_working_bytes"});
  ColorStackRequest r;const auto &a=wire::text_value(v.at("action"));if(a=="split")r.action=ColorStackAction::Split;else if(a=="merge")r.action=ColorStackAction::Merge;else if(a=="flatten_rgb")r.action=ColorStackAction::FlattenRgb;else throw std::invalid_argument("Unknown color stack action");
  const auto &sp=wire::text_value(v.at("space"));if(sp=="rgb")r.space=ColorComponentSpace::RGB;else if(sp=="hsb8")r.space=ColorComponentSpace::HSB8;else if(sp=="hsb32")r.space=ColorComponentSpace::HSB32;else throw std::invalid_argument("Unknown color stack space");
  const auto &lp=wire::text_value(v.at("layout"));if(lp=="preserve_axes")r.layout=ColorStackLayoutPolicy::PreserveAxes;else if(lp=="imagej_legacy")r.layout=ColorStackLayoutPolicy::ImageJLegacy;else throw std::invalid_argument("Unknown color stack layout");
  const auto &dest=wire::text_value(v.at("destination"));if(dest=="in_place")r.destination=OutputDestination::InPlace;else if(dest=="new_document")r.destination=OutputDestination::NewDocument;else throw std::invalid_argument("Unknown color stack destination");
  r.invert_rgb=boolean(v.at("invert_rgb"));r.allow_calibration_reset=boolean(v.at("allow_calibration_reset"));r.allow_nonxy_annotation_drop=boolean(v.at("allow_nonxy_annotation_drop"));r.max_working_bytes=wire::size_value(v.at("max_working_bytes"));
  if(!r.max_working_bytes)throw std::invalid_argument("Color stack requires a positive byte limit");
  if(r.invert_rgb && (r.action!=ColorStackAction::Merge || r.space!=ColorComponentSpace::RGB))throw std::invalid_argument("RGB inversion requires RGB merge");
  if(r.action==ColorStackAction::FlattenRgb && (r.space!=ColorComponentSpace::RGB || r.layout!=ColorStackLayoutPolicy::ImageJLegacy))throw std::invalid_argument("Flatten requires explicit RGB legacy layout");
  return r;
}
struct Decoded {wire::SourceRecord source;ColorStackRequest request;ColorStackLayout layout;std::uint64_t selection,display;std::size_t metadata;std::optional<RgbDatasetRequest> rendering;};
Decoded decode(const MetadataObject &record){
  (void)detail::imagej_roi_record_bytes(record,16*1024*1024);
  if(!record.count("version"))throw std::invalid_argument("Color stack version is required");
  const auto version=wire::unsigned_value(record.at("version"));
  if(version==1)wire::exact_fields(record,{"schema","version","source","selection_revision","display_revision","view_position","request","layout","metadata_working_bytes","policy"});
  else if(version==2)wire::exact_fields(record,{"schema","version","source","selection_revision","display_revision","view_position","request","layout","metadata_working_bytes","policy","rendering"});
  else throw std::invalid_argument("Unsupported color stack command version");
  if(wire::text_value(record.at("schema"))!="imagejxx.color-stack-command" || wire::text_value(record.at("policy"))!=(version==1?"component-groups-explicit-legacy-loss-v1":"scalar-render-opening-range-legacy-loss-v2"))throw std::invalid_argument("Unsupported color stack command");
  const auto s=wire::read_source(wire::object_value(record.at("source")));const auto &position=wire::array_value(record.at("view_position"));
  if(position.size()!=3)throw std::invalid_argument("Color stack view position requires T/C/Z");
  const int limits[]{s.shape.t,s.shape.c,s.shape.z};for(int i=0;i<3;++i)if(wire::unsigned_value(position[i])>=std::uint64_t(limits[i]))throw std::invalid_argument("Color stack view outside source");
  const auto r=read_request(wire::object_value(record.at("request")));
  const bool scalar=r.action==ColorStackAction::FlattenRgb && s.shape.s==1;
  if(scalar!=(version==2))throw std::invalid_argument("Scalar RGB flatten requires V2 rendering intent");
  auto l=describe_color_stack(s.shape,s.pixel_type,s.sample_policy,s.calibration,r);const auto bytes=wire::size_value(record.at("metadata_working_bytes"));Budget b{r.max_working_bytes,l.working_bytes};b.add(bytes);
  std::optional<RgbDatasetRequest> rendering;
  if(scalar){const auto &nested=wire::object_value(record.at("rendering"));rendering=detail::rgb_dataset_request_from_record(nested);
    if(rendering->mode!=RgbDatasetMode::PerPlane || rendering->destination!=OutputDestination::InPlace || rendering->max_working_bytes>r.max_working_bytes)throw std::invalid_argument("Scalar flatten rendering requires bounded in-place per-plane intent");
    for(const char *name:{"source","selection_revision","display_revision","view_position"})if(!metadata_equal(record.at(name),nested.at(name)))throw std::invalid_argument("Scalar flatten rendering source differs from owner capture");
    b.add(wire::size_value(wire::object_value(nested.at("layout")).at("working_bytes")));
  }
  l.working_bytes=b.used;
  if(!metadata_equal(wire::object_value(record.at("layout")),layout_record(l)))throw std::invalid_argument("Color stack derived layout contradicts request");
  return {s,r,l,wire::unsigned_value(record.at("selection_revision")),wire::unsigned_value(record.at("display_revision")),bytes,std::move(rendering)};
}
std::string component_name(const ColorStackRequest &r,int k){const char *rgb[]{"Red","Green","Blue"},*hsb[]{"Hue","Saturation","Brightness"};return r.space==ColorComponentSpace::RGB?rgb[k]:hsb[k];}
std::vector<PlanePosition> sources(PlanePosition out,DatasetShape s,const ColorStackRequest &r){
  if(r.action==ColorStackAction::FlattenRgb)return {flat_rgb(s,r)?unindex(out.z,s):out};
  if(r.action==ColorStackAction::Split)return {flat_split(r)?unindex(out.z,s):PlanePosition{out.t,out.c/3,out.z}};
  std::vector<PlanePosition> result;
  if(flat_merge(r)){for(std::size_t i=0;i<s.plane_count();++i)result.push_back(unindex(int(i),s));}
  else for(int k=0;k<group_size(s);++k)result.push_back({out.t,out.c*group_size(s)+k,out.z});
  return result;
}
codec::Json joined(const codec::Json &input,const std::vector<std::size_t> &indices){std::string label;bool present=false;for(auto i:indices)if(input.at(i).is_string()){if(present)label+=" | ";label+=input.at(i).get<std::string>();present=true;}return present?codec::Json(label):codec::Json(nullptr);}
PreparedAxisRemap metadata(const ResolvedColorStackCommand &command,Dataset replacement){
  const auto &source=command.source();const auto &d=source.data();const auto s=d.dataset.shape(),out=command.layout().shape;const auto &r=command.request();
  auto edit=source.begin_edit(Change::All);edit.replace_dataset(std::move(replacement));edit.set_spectrum({});if(r.destination==OutputDestination::NewDocument)edit.set_title(d.metadata.title+" "+action_name(r.action)+" "+space_name(r.space));
  std::vector<Roi> rois;for(const auto &a:d.rois)positions({a.t,a.c,a.z},s,r,[&](Position p){auto row=a;row.t=p.t;row.c=p.c;row.z=p.z;rois.push_back(std::move(row));});edit.set_rois(std::move(rois));
  std::vector<MetadataObject> overlays;for(const auto &a:d.overlays)positions(overlay_position(a,s),s,r,[&](Position p){auto j=codec::to_json(a);for(const auto &v:{std::pair{"t",p.t},std::pair{"c",p.c},std::pair{"z",p.z}})if(v.second || j.contains(v.first))j[v.first]=v.second?codec::Json(*v.second):codec::Json(nullptr);overlays.push_back(codec::object_from_json(j));});edit.set_overlays(std::move(overlays));
  const bool changes_z=changes_nonxy(s,r);
  auto map_annotations=[&](auto rows){using Rows=decltype(rows);Rows result;for(const auto &a:rows){
    if(a.plane!=Plane::XY && changes_z){if(!r.allow_nonxy_annotation_drop)throw std::invalid_argument("Legacy color layout requires explicit non-XY annotation removal consent");continue;}
    positions({a.t,a.c,a.plane==Plane::XY?std::optional<int>(a.coordinate):std::nullopt},s,r,[&](Position p){auto row=a;row.t=p.t;row.c=p.c;if(row.plane==Plane::XY)row.coordinate=p.z.value_or(0);result.push_back(std::move(row));});
  }return result;};
  if(d.metadata.extensions.count(planar_annotations_extension))edit.set_extension(planar_annotations_extension,encode_planar_annotations(out,map_annotations(planar_annotations(d))));
  if(d.metadata.extensions.count(planar_display_annotations_extension))edit.set_extension(planar_display_annotations_extension,encode_planar_display_annotations(out,map_annotations(planar_display_annotations(d))));
  auto labels=[&](const MetadataObject &input,bool properties){for(const auto *key:{"plane_labels","slice_labels","channel_labels","channel_names"}){
    const auto found=input.find(key);if(found==input.end())continue;const auto old=codec::to_json(found->second);const std::string kind=key;
    const auto count=kind=="plane_labels"?s.plane_count():std::size_t(kind=="slice_labels"?s.z:s.c);
    if(!old.is_array() || old.size()!=count)throw std::invalid_argument("Color stack labels must match source extent");
    for(const auto &v:old)if(!v.is_null()&&!v.is_string())throw std::invalid_argument("Color stack labels require strings or null");
    codec::Json next=codec::Json::array();
    if(kind=="plane_labels")for(int t=0;t<out.t;++t)for(int c=0;c<out.c;++c)for(int z=0;z<out.z;++z){std::vector<std::size_t> indices;for(auto p:sources({t,c,z},s,r))indices.push_back((std::size_t(p.t)*s.c+p.c)*s.z+p.z);auto value=joined(old,indices);if(r.action==ColorStackAction::Split)value=(value.is_string()?value.get<std::string>()+" / ":"")+component_name(r,c%3);next.push_back(value);}
    else if(kind=="slice_labels")for(int z=0;z<out.z;++z){std::vector<std::size_t> indices;for(auto p:sources({0,0,z},s,r))if(std::find(indices.begin(),indices.end(),std::size_t(p.z))==indices.end())indices.push_back(p.z);next.push_back(joined(old,indices));}
    else for(int c=0;c<out.c;++c){
      if(r.action==ColorStackAction::Split){auto value=flat_split(r)?codec::Json(nullptr):old.at(c/3);next.push_back((value.is_string()?value.get<std::string>()+" / ":"")+component_name(r,c%3));}
      else if(r.action==ColorStackAction::FlattenRgb && !flat_rgb(s,r))next.push_back(old.at(c));
      else {std::vector<std::size_t> indices;if(flat_merge(r)||flat_rgb(s,r)){for(int k=0;k<s.c;++k)indices.push_back(k);}else for(int k=0;k<group_size(s);++k)indices.push_back(c*group_size(s)+k);next.push_back(joined(old,indices));}
    }
    if(properties)edit.set_property(key,codec::from_json(next));else edit.set_extension(key,codec::from_json(next));
  }};labels(d.metadata.properties,true);labels(d.metadata.extensions,false);
  if(r.action==ColorStackAction::Split){
    if(!d.metadata.properties.count("channel_names") && !d.metadata.extensions.count("channel_names")){
      MetadataArray names;for(int c=0;c<out.c;++c)names.emplace_back(component_name(r,c%3));edit.set_extension("channel_names",metadata_array(std::move(names)));
    }
    if(!d.metadata.properties.count("plane_labels") && !d.metadata.extensions.count("plane_labels")){
      MetadataArray names;for(int t=0;t<out.t;++t)for(int c=0;c<out.c;++c)for(int z=0;z<out.z;++z)names.emplace_back(component_name(r,c%3));edit.set_extension("plane_labels",metadata_array(std::move(names)));
    }
  }
  for(const auto *key:{"is_stack","labels"}){const auto value=std::string(key)=="is_stack" && out.plane_count()>1;
    const auto p=d.metadata.properties.find(key),e=d.metadata.extensions.find(key);
    if(p!=d.metadata.properties.end() && std::holds_alternative<bool>(p->second))edit.set_property(key,value);
    if(e!=d.metadata.extensions.end() && std::holds_alternative<bool>(e->second))edit.set_extension(key,value);}
  // The legacy scalar writer uses this descriptor only to default is_stack.
  // Exact six-axis extents are retained in the native source command record.
  codec::ScalarRecord scalar{d.metadata,d.dataset.calibration(),s.plane_count()>1?2:1,d.selection,d.previous_selection,d.rois,d.overlays};
  auto original_display=codec::view_json(source.display());original_display["properties"]=codec::to_json(source.display().properties);original_display["t"]=source.display().t;original_display["c"]=source.display().c;
  MetadataArray original_rois;auto optional=[](std::optional<int> p){return p?MetadataValue(*p):MetadataValue{};};
  for(const auto &row:d.rois)original_rois.push_back(metadata_object({{"name",row.name},{"selection",codec::from_json(codec::selection_to_json(row.selection))},{"t",optional(row.t)},{"c",optional(row.c)},{"z",optional(row.z)},{"attributes",metadata_object(row.attributes)}}));
  MetadataObject original_native{{"metadata",metadata_object({{"title",d.metadata.title},{"properties",metadata_object(d.metadata.properties)},{"extensions",metadata_object(d.metadata.extensions)},{"record_layout",metadata_object(d.metadata.record_layout)}})},{"rois",metadata_array(std::move(original_rois))}};
  edit.set_extension("imagejxx.color_stack_source",metadata_object({{"schema",std::string("imagejxx.color-stack-source")},{"version",1},{"command",metadata_object(command.record())},{"original_scalar_metadata",codec::from_json(codec::wire_metadata(scalar))},{"original_native_metadata",metadata_object(std::move(original_native))},{"original_display",codec::from_json(original_display)}}));
  auto display=source.display();const PlanePosition active{display.t,display.c,display.crosshair[2]};
  if(flat_merge(r)){display.t=display.c=display.crosshair[2]=0;}
  else if(flat_split(r)||flat_rgb(s,r)){display.t=display.c=0;display.crosshair[2]=index(active,s);}
  else if(r.action==ColorStackAction::Split)display.c*=3;
  else if(r.action==ColorStackAction::Merge)display.c/=group_size(s);
  display.properties.erase("display_range");display.properties.erase("channel_display");reset_manual_threshold_state(display);
  if(command.layout().calibration_reset){display.zoom=0;display.planeZoom={};display.pan={};}
  display.normalize(out);edit.set_axis_remap_display(display);return {std::move(edit),std::move(display)};
}
}
ColorStackLayout describe_color_stack(DatasetShape s,PixelType type,FloatingSamplePolicy policy,const SpatialCalibration &cal,const ColorStackRequest &r){
  s.validate();cal.validate();(void)action_name(r.action);(void)space_name(r.space);(void)layout_name(r.layout);
  if(r.destination!=OutputDestination::InPlace && r.destination!=OutputDestination::NewDocument)throw std::invalid_argument("Unknown color stack destination");
  if(policy!=FloatingSamplePolicy::FiniteOnly)throw std::invalid_argument("Color stack conversion requires finite samples");
  if(r.invert_rgb && (r.action!=ColorStackAction::Merge || r.space!=ColorComponentSpace::RGB))throw std::invalid_argument("RGB inversion requires RGB merge");
  auto out=s;PixelType output_type=PixelType::UInt8;bool reset=false;
  if(r.action==ColorStackAction::Split){
    if(type!=PixelType::UInt8 || s.s!=3)throw std::invalid_argument("Color stack split requires UInt8 RGB");
    output_type=r.space==ColorComponentSpace::HSB32?PixelType::Float32:PixelType::UInt8;out.s=1;
    if(flat_split(r)){if(s.plane_count()>std::size_t(std::numeric_limits<int>::max()))throw std::invalid_argument("Legacy stack Z overflow");out.t=1;out.c=3;out.z=int(s.plane_count());reset=s.t>1||s.c>1;}
    else {if(r.layout==ColorStackLayoutPolicy::ImageJLegacy && s.c!=1)throw std::invalid_argument("ImageJ RGB stack splitting requires C1");if(s.c>std::numeric_limits<int>::max()/3)throw std::invalid_argument("Component C overflow");out.c=s.c*3;}
  }else if(r.action==ColorStackAction::Merge){
    if(s.s!=1 || type!=(r.space==ColorComponentSpace::HSB32?PixelType::Float32:PixelType::UInt8))throw std::invalid_argument("Color stack merge requires scalar component dtype");
    out.s=3;
    if(flat_merge(r)){if(s.plane_count()!=3 && !(r.space==ColorComponentSpace::RGB && s.plane_count()==2))throw std::invalid_argument("Legacy merge consumes exactly three planes (RGB permits two)");out.t=out.c=out.z=1;reset=s.z>1||s.t>1;}
    else {if(s.c!=3 && !(s.c==2 && r.space==ColorComponentSpace::RGB) && (s.c<3 || s.c%3))throw std::invalid_argument("Merge requires component C groups of three (RGB permits two)");out.c=s.c/group_size(s);}
  }else {
    if(r.space!=ColorComponentSpace::RGB || r.layout!=ColorStackLayoutPolicy::ImageJLegacy || (s.s!=1 && (s.s!=3 || type!=PixelType::UInt8)))throw std::invalid_argument("Flatten requires scalar or UInt8 RGB and explicit ImageJ legacy layout");
    out.s=3;
    if(s.c>1){if(s.plane_count()>std::size_t(std::numeric_limits<int>::max()))throw std::invalid_argument("Flatten Z overflow");out.t=out.c=1;out.z=int(s.plane_count());reset=true;}
  }
  if(reset && !r.allow_calibration_reset)throw std::invalid_argument("Legacy color layout requires explicit spatial/time calibration reset consent");
  out.validate();Budget b{r.max_working_bytes};b.add(65536);b.product(s.pixel_count(),2*sizeof(double));Budget output{r.max_working_bytes};output.product(out.pixel_count(),sizeof(double));b.product(output.used,4);b.product(s.plane_count(),512);b.product(out.plane_count(),512);
  return {out,output_type,reset?SpatialCalibration{}:cal,reset,output.used,b.used};
}
ResolvedColorStackCommand::ResolvedColorStackCommand(DocumentSnapshot s,ColorStackRequest r,ColorStackLayout l,MetadataObject record):source_(std::move(s)),request_(r),layout_(std::move(l)),record_(std::move(record)){}
ResolvedColorStackCommand resolve_color_stack_command(const DocumentSnapshot &source,const ColorStackRequest &r){
  return resolve_color_stack_command(source,r,std::nullopt);
}
ResolvedColorStackCommand resolve_color_stack_command(const DocumentSnapshot &source,const ColorStackRequest &r,const std::optional<RgbDatasetRequest> &rendering){
  const auto &d=source.data().dataset;auto l=describe_color_stack(d.shape(),d.pixel_type(),d.sample_policy(),d.calibration(),r);const auto bytes=metadata_charge(source,r,r.max_working_bytes-l.working_bytes);Budget b{r.max_working_bytes,l.working_bytes};b.add(bytes);l.working_bytes=b.used;
  const auto &v=source.display();MetadataObject record{{"schema",std::string("imagejxx.color-stack-command")},{"version",1},{"source",metadata_object(wire::source_record(wire::source_metadata(source)))},{"selection_revision",source.revisions().selection},{"display_revision",source.revisions().display},{"view_position",metadata_array({v.t,v.c,v.crosshair[2]})},{"request",metadata_object(request_record(r))},{"layout",metadata_object(layout_record(l))},{"metadata_working_bytes",std::uint64_t(bytes)},{"policy",std::string("component-groups-explicit-legacy-loss-v1")}};
  if(r.action==ColorStackAction::FlattenRgb && d.shape().s==1){
    auto options=rendering.value_or(RgbDatasetRequest{});
    if(options.mode!=RgbDatasetMode::PerPlane)throw std::invalid_argument("Scalar flatten renders each plane; composite conversion is a separate command");
    options.destination=OutputDestination::InPlace;options.max_working_bytes=std::min(options.max_working_bytes,r.max_working_bytes);
    // A supplied rendering request is frozen intent. Its absent LUT means Grays,
    // and absent range means deferred reset, independent of destination display.
    if(rendering && !options.channels && !options.lut)options.lut=channel_display_lut("Grays");
    auto captured=resolve_rgb_dataset_command(source,options);options=captured.request();
    if(!options.channels && !options.lut){
      const auto custom=v.properties.find("custom_lut"),named=v.properties.find("lut");
      if(custom!=v.properties.end() && !std::holds_alternative<std::monostate>(custom->second)){
        const auto &rows=wire::array_value(custom->second);if(rows.size()!=256)throw std::invalid_argument("Scalar flatten LUT requires 256 rows");options.lut.emplace();
        for(std::size_t i=0;i<256;++i){const auto &row=wire::array_value(rows[i]);if(row.size()!=3)throw std::invalid_argument("Scalar flatten LUT requires RGB rows");for(int k=0;k<3;++k){const auto value=wire::unsigned_value(row[k]);if(value>255)throw std::invalid_argument("Scalar flatten LUT requires byte values");(*options.lut)[i][k]=static_cast<unsigned char>(value);}}
      }else if(named!=v.properties.end()){
        const auto name=wire::text_value(named->second);
        if(name=="Inverted"){options.lut.emplace();for(int i=0;i<256;++i)(*options.lut)[i]={static_cast<unsigned char>(255-i),static_cast<unsigned char>(255-i),static_cast<unsigned char>(255-i)};}
        else if(name=="Fire" || name=="Ice"){
          const std::array<std::array<double,3>,5> fire{{{0,0,0},{80,0,120},{220,20,20},{255,180,0},{255,255,255}}},ice{{{0,0,0},{0,40,120},{0,160,220},{160,240,255},{255,255,255}}};const auto &stops=name=="Fire"?fire:ice;options.lut.emplace();
          for(int i=0;i<256;++i){const double t=i/255.*4;const int k=std::min(3,int(t));for(int c=0;c<3;++c)(*options.lut)[i][c]=static_cast<unsigned char>(std::floor(stops[k][c]*(1-(t-k))+stops[k+1][c]*(t-k)+.5));}
        }else if(name=="Spectrum"){
          Image ramp(256,1,3,PixelType::Float64);for(int i=0;i<256;++i){ramp.at(i,0,0)=i/256.;ramp.at(i,0,1)=ramp.at(i,0,2)=1;}auto rgb=hsv_to_rgb(ramp,PixelType::UInt8);options.lut.emplace();for(int i=0;i<256;++i)for(int c=0;c<3;++c)(*options.lut)[i][c]=static_cast<unsigned char>(std::floor(rgb.at(i,0,c)+.5));
        }else options.lut=channel_display_lut(name);
      }else options.lut=channel_display_lut("Grays");
    }
    captured=resolve_rgb_dataset_command(source,options);b.add(captured.layout().working_bytes);l.working_bytes=b.used;
    record["version"]=2;record["policy"]=std::string("scalar-render-opening-range-legacy-loss-v2");record["rendering"]=metadata_object(captured.record());record["layout"]=metadata_object(layout_record(l));
  }else if(rendering)throw std::invalid_argument("Rendering intent is scalar RGB flatten only");
  (void)detail::imagej_roi_record_bytes(record,16*1024*1024);ResolvedColorStackCommand result(source,r,l,std::move(record));(void)detail::color_stack_metadata_preview(result);return result;
}
void validate_color_stack_command_record(const MetadataObject &r){(void)decode(r);}
std::optional<RgbDatasetRequest> detail::color_stack_rendering_options(const MetadataObject &r){return decode(r).rendering;}
ColorStackRequest detail::color_stack_workflow_request(const OperationParameters &parameters,const MetadataObject &original){
  const auto captured=decode(original).request;
  if(parameters.empty())return captured;
  auto values=request_record(captured);
  const char *fields[]{"action","space","layout","invert_rgb","allow_calibration_reset","allow_nonxy_annotation_drop","max_working_bytes"};
  if(parameters.size()!=7)throw std::invalid_argument("Authored color stack choices require all seven fields");
  for(const auto *name:fields){
    const auto it=parameters.find(name);if(it==parameters.end())throw std::invalid_argument("Missing authored color stack field");
    values[name]=std::visit([](const auto &v)->MetadataValue{using T=std::decay_t<decltype(v)>;
      if constexpr(std::is_same_v<T,bool> || std::is_same_v<T,std::int64_t> || std::is_same_v<T,std::uint64_t> || std::is_same_v<T,std::string>)return MetadataValue(v);
      else throw std::invalid_argument("Color stack choices require Boolean, integer or text literals");
    },it->second);
  }
  return read_request(values);
}
OperationParameters detail::color_stack_workflow_parameters(const ColorStackRequest &request){
  OperationParameters result;for(const auto &[name,value]:request_record(request))if(name!="destination")
    result[name]=std::visit([](const auto &v)->OperationValue{using T=std::decay_t<decltype(v)>;
      if constexpr(std::is_same_v<T,bool> || std::is_same_v<T,std::int64_t> || std::is_same_v<T,std::uint64_t> || std::is_same_v<T,std::string>)return OperationValue(v);
      else throw std::logic_error("Unexpected color stack request field");
    },value);
  return result;
}
ResolvedColorStackCommand color_stack_command_from_record(const DocumentSnapshot &source,const MetadataObject &r){const auto decoded=decode(r);
  if(!metadata_equal(wire::source_record(wire::source_metadata(source)),wire::source_record(decoded.source)) || source.revisions().selection!=decoded.selection || source.revisions().display!=decoded.display)throw RevisionConflict();
  auto result=resolve_color_stack_command(source,decoded.request,decoded.rendering);if(!metadata_equal(result.record(),r))throw std::invalid_argument("Color stack capture differs from source");return result;
}
ResolvedColorStackCommand rebind_color_stack_command(const DocumentSnapshot &source,const MetadataObject &record,const ColorStackRequest &request){(void)decode(record);return resolve_color_stack_command(source,request);}
DocumentSnapshot detail::color_stack_metadata_preview(const ResolvedColorStackCommand &c){return metadata(c,workflow_metadata_dataset(c.layout().shape,c.layout().pixel_type,c.layout().calibration)).transaction.preview();}
PreparedAxisRemap detail::prepare_color_stack_edit(const ResolvedColorStackCommand &command,const ExecutionControl &control,std::size_t cap){
  if(command.layout().working_bytes>cap)throw std::invalid_argument("Color stack execution working-byte limit");
  const auto captured=control;ExecutionScope scope(captured);kernel_checkpoint();const auto &d=command.source().data().dataset;const auto s=d.shape(),out=command.layout().shape;const auto &r=command.request();
  auto staged=metadata(command,workflow_metadata_dataset(out,command.layout().pixel_type,command.layout().calibration));
  if(r.action==ColorStackAction::FlattenRgb && s.s==1){
    const auto rendering=detail::color_stack_rendering_options(command.record()).value();
    auto range=rendering.display_range;std::shared_ptr<const Image> opening;
    const auto &view=command.source().display();const PlanePosition pose{view.t,view.c,view.crosshair[2]};
    if(!rendering.channels && d.pixel_type()!=PixelType::UInt8 && (!range || ((*range)[0]==0 && (*range)[1]==0))){
      opening=d.plane(pose);const bool f32=d.pixel_type()==PixelType::Float32;double lo=f32?double(static_cast<float>(opening->pixels.front())):opening->pixels.front(),hi=lo;for(const auto value:opening->pixels){kernel_checkpoint_periodic();const double sample=f32?double(static_cast<float>(value)):value;lo=std::min(lo,sample);hi=std::max(hi,sample);}range=std::array<double,2>{lo,hi};
    }
    Stack planes;planes.reserve(out.plane_count());
    for(int t=0;t<out.t;++t)for(int c=0;c<out.c;++c)for(int z=0;z<out.z;++z){kernel_checkpoint();const auto position=flat_rgb(s,r)?unindex(z,s):PlanePosition{t,c,z};auto image=opening && position.t==pose.t && position.c==pose.c && position.z==pose.z?opening:d.plane(position);
      auto selected_range=range;auto lut=rendering.lut;
      if(rendering.channels){const auto &channel=rendering.channels->channels[position.c];selected_range=channel.range;lut=rendering.channels->mode==ChannelDisplayMode::Grayscale?channel_display_lut("Grays"):channel.lut.table?*channel.lut.table:channel_display_lut(channel.lut.name);}
      {auto inner=control;inner.progress={};ExecutionScope suppress(inner);planes.push_back(convert_rgb(*image,selected_range,lut));}
      execution_progress(planes.size(),out.plane_count());kernel_checkpoint();
    }
    staged.transaction.replace_dataset(Dataset::from_planes(out,std::move(planes),command.layout().calibration));kernel_checkpoint();return staged;
  }
  if(r.action==ColorStackAction::FlattenRgb){
    class FlattenSource final:public PlaneSource {
      Dataset original_;DatasetShape shape_;
    public:
      FlattenSource(Dataset original,DatasetShape shape):original_(std::move(original)),shape_(shape){}
      DatasetShape shape() const override{return shape_;}
      PixelType pixel_type() const override{return PixelType::UInt8;}
      std::shared_ptr<const Image> read_plane(PlanePosition p) const override{return original_.plane(unindex(p.z,original_.shape()));}
    };
    if(flat_rgb(s,r)){
      if(const auto shared=d.shared_planes()){
        std::vector<std::size_t> order;order.reserve(out.plane_count());
        for(int i=0;i<out.z;++i){kernel_checkpoint();const auto p=unindex(i,s);order.push_back((std::size_t(p.t)*s.c+p.c)*s.z+p.z);}
        staged.transaction.replace_dataset(Dataset::from_shared_planes(out,shared->subset(order),command.layout().calibration));
      }else staged.transaction.replace_dataset(Dataset(std::make_shared<FlattenSource>(d,out),command.layout().calibration));
    }else staged.transaction.replace_dataset(d);
    execution_progress(1,1);kernel_checkpoint();return staged;
  }
  Stack planes;planes.reserve(out.plane_count());
  if(r.action==ColorStackAction::Split){
    std::vector<std::optional<Image>> slots(out.plane_count());std::size_t completed=0;
    for(int t=0;t<s.t;++t)for(int c=0;c<s.c;++c)for(int z=0;z<s.z;++z){kernel_checkpoint();auto parts=detail::split_color_components_controlled(*d.plane({t,c,z}),r.space,{},r.max_working_bytes,false);
      for(int k=0;k<3;++k){const PlanePosition p=flat_split(r)?PlanePosition{0,k,index({t,c,z},s)}:PlanePosition{t,c*3+k,z};slots[(std::size_t(p.t)*out.c+p.c)*out.z+p.z]=std::move(parts[k]);}
      execution_progress(++completed,s.plane_count());kernel_checkpoint();
    }
    for(auto &p:slots){kernel_checkpoint();planes.push_back(std::move(p.value()));}
  }else for(int t=0;t<out.t;++t)for(int c=0;c<out.c;++c)for(int z=0;z<out.z;++z){kernel_checkpoint();Stack parts;
    for(auto p:sources({t,c,z},s,r)){kernel_checkpoint();parts.push_back(*d.plane(p));}
    planes.push_back(detail::merge_color_components_controlled(parts,r.space,r.invert_rgb,{},r.max_working_bytes,false));execution_progress(planes.size(),out.plane_count());kernel_checkpoint();
  }
  staged.transaction.replace_dataset(Dataset::from_planes(out,std::move(planes),command.layout().calibration));kernel_checkpoint();return staged;
}
} // namespace imagejxx
