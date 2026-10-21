// Installed public core-only scientific-orientation adapter assertions.
// Expected indices, sample bits and world coordinates are authored here;
// descriptors/preparations/provenance never generate expected values.
#include <imagejxx/application.hpp>
#include <imagejxx/axis_extraction_command.hpp>
#include <imagejxx/axis_orientation_command.hpp>
#include <imagejxx/session.hpp>

#include <array>
#include <chrono>
#include <cmath>
#include <cstring>
#include <iomanip>
#include <sstream>
#include <typeinfo>
#include <functional>
#include <iostream>
#include <limits>
#include <set>
#include <type_traits>
#include <vector>

namespace {
using namespace imagejxx;
std::size_t checks=0,finite_rows=0,bit_rows=0;
MetadataObject observed;
const auto started=std::chrono::steady_clock::now();
void require(bool condition,const char *message) {
  ++checks;if(!condition)throw std::runtime_error(message);
  if(std::chrono::steady_clock::now()-started>std::chrono::seconds(30))
    throw std::runtime_error("Scientific orientation SDK exceeded30seconds");
}
template<class Error=std::invalid_argument,class Function>
void refuses(Function function,const char *message) {
  bool caught=false;try{function();}catch(const Error &){caught=true;}require(caught,message);
}
SpatialCalibration calibration() {
  SpatialCalibration cal;cal.spacing={.5,1.25,2.5};cal.origin={11,-7,19};
  cal.direction={-1,.25,.5,0,1,.5,.5,0,1};cal.unit="um";
  cal.time_origin=2.25;cal.frame_interval=.75;cal.time_unit="s";return cal;
}
double literal(int t,int c,int z,int y,int x,int s) {return 64*t+24*c+8*z+3*y+x+2*s;}
Dataset fixture(PixelType type=PixelType::UInt16,int samples=1) {
  Stack planes;
  for(int t=0;t<2;++t)for(int c=0;c<3;++c)for(int z=0;z<4;++z) {
    Image image(7,5,samples,type);
    for(int y=0;y<5;++y)for(int x=0;x<7;++x)for(int s=0;s<samples;++s)image.at(x,y,s)=literal(t,c,z,y,x,s);
    planes.push_back(std::move(image));
  }
  return Dataset::from_planes({2,3,4,5,7,samples},std::move(planes),calibration());
}
std::array<int,3> axes(Plane p) {
  return p==Plane::XY?std::array<int,3>{0,1,2}:p==Plane::XZ?std::array<int,3>{0,2,1}:std::array<int,3>{1,2,0};
}
AxisRangeOrientationRequest request(Plane p=Plane::XZ,int flags=7) {
  const std::array<int,3> extent{7,5,4};
  return {{p,0,extent[axes(p)[2]]-1,2,1,2,1024*1024},bool(flags&1),bool(flags&2),bool(flags&4)};
}
std::array<int,3> voxel(DatasetShape shape,const AxisRangeOrientationRequest &r,int u,int v,int z) {
  const auto a=axes(r.range.plane);const std::array<int,3> extent{shape.x,shape.y,shape.z};
  const int count=1+(r.range.last-r.range.first)/r.range.step;
  const int h=r.transpose?v:u,vertical=r.transpose?u:v;
  std::array<int,3> out{};out[a[0]]=h;out[a[1]]=r.flip_vertical?extent[a[1]]-1-vertical:vertical;
  out[a[2]]=r.range.first+(r.reverse_order?count-1-z:z)*r.range.step;return out;
}
std::array<double,3> world(const SpatialCalibration &cal,std::array<int,3> position) {
  auto out=cal.origin;
  for(int row=0;row<3;++row)for(int column=0;column<3;++column)
    out[row]+=cal.direction[row*3+column]*cal.spacing[column]*position[column];
  return out;
}
void geometry(const AxisExtractionLayout &actual,DatasetShape source,const SpatialCalibration &cal,
              const AxisRangeOrientationRequest &r) {
  const auto a=axes(r.range.plane);const std::array<int,3> extents{source.x,source.y,source.z};
  const int count=1+(r.range.last-r.range.first)/r.range.step;
  const int width=extents[a[r.transpose?1:0]],height=extents[a[r.transpose?0:1]];
  require(actual.shape.dimensions()==std::array<int,6>{1,1,count,height,width,source.s},"SDK oriented TCZYXS dimensions");
  require(actual.source_axes==a,"SDK source_axes base permutation changed under transpose");
  require(actual.calibration.origin==world(cal,voxel(source,r,0,0,0)) && actual.geometry.origin==actual.calibration.origin,"SDK oriented world origin");
  const std::array<int,3> order{a[r.transpose?1:0],a[r.transpose?0:1],a[2]};
  const std::array<int,3> signs{r.transpose?(r.flip_vertical?-1:1):1,r.transpose?1:(r.flip_vertical?-1:1),r.reverse_order?-1:1};
  for(int column=0;column<3;++column) {
    const double spacing=cal.spacing[order[column]]*(column==2?r.range.step:1);
    require(actual.calibration.spacing[column]==spacing,"SDK positive/permuted fixed stride");
    for(int row=0;row<3;++row) {
      const double direction=signs[column]*cal.direction[row*3+order[column]];
      require(actual.calibration.direction[row*3+column]==direction,"SDK shear/reflection signed direction");
      if(column<2)require((column?actual.geometry.vertical_step:actual.geometry.horizontal_step)[row]==direction*spacing,"SDK geometry direct signed basis");
    }
  }
  require(actual.calibration.unit==cal.unit && actual.calibration.frame_interval==cal.frame_interval
      && actual.calibration.time_unit==cal.time_unit && actual.calibration.time_origin==cal.time_origin+cal.frame_interval*r.range.t,
      "SDK captured time and units");
  require(world(actual.calibration,{width-1,height-1,count-1})==world(cal,voxel(source,r,width-1,height-1,count-1)),"SDK nonzero world landmark");
}
void output(const Dataset &actual,DatasetShape source,const AxisRangeOrientationRequest &r) {
  const auto size=actual.shape();
  const auto a=axes(r.range.plane);const std::array<int,3> extent{source.x,source.y,source.z};
  const int count=1+(r.range.last-r.range.first)/r.range.step;
  require(size.dimensions()==std::array<int,6>{1,1,count,extent[a[r.transpose?0:1]],extent[a[r.transpose?1:0]],source.s},"SDK prepared dataset dimensions differ");
  const auto cal=calibration();const auto &out=actual.calibration();
  require(out.origin==world(cal,voxel(source,r,0,0,0)) && out.unit==cal.unit && out.time_unit==cal.time_unit
      && out.time_origin==cal.time_origin+r.range.t*cal.frame_interval && out.frame_interval==cal.frame_interval,"SDK prepared world/time metadata differs");
  const std::array<int,3> order{a[r.transpose?1:0],a[r.transpose?0:1],a[2]};
  const std::array<int,3> sign{r.transpose?(r.flip_vertical?-1:1):1,r.transpose?1:(r.flip_vertical?-1:1),r.reverse_order?-1:1};
  for(int column=0;column<3;++column){require(out.spacing[column]==cal.spacing[order[column]]*(column==2?r.range.step:1),"SDK prepared spacing differs");
    for(int row=0;row<3;++row)require(out.direction[row*3+column]==sign[column]*cal.direction[row*3+order[column]],"SDK prepared signed direction differs");}
  for(int k=0;k<size.z;++k) {
    const auto plane=actual.plane({0,0,k});
    for(int y=0;y<size.y;++y)for(int x=0;x<size.x;++x){const auto position=voxel(source,r,x,y,k);for(int s=0;s<size.s;++s)
      require(plane->at(x,y,s)==literal(r.range.t,r.range.c,position[2],position[1],position[0],s),"SDK exact independently indexed sample");}
  }
}
MetadataObject object(const MetadataValue &value) {return std::get<MetadataObject>(std::get<std::shared_ptr<const MetadataTree>>(value)->value);}
void path_edit(MetadataObject &tree,const std::vector<std::string> &path,MetadataValue replacement,std::size_t at=0) {
  if(at+1==path.size()){tree.at(path[at])=std::move(replacement);return;}
  auto child=object(tree.at(path[at]));path_edit(child,path,std::move(replacement),at+1);tree[path[at]]=metadata_object(std::move(child));
}
MetadataObject replace(MetadataObject tree,const std::vector<std::string> &path,MetadataValue replacement) {
  path_edit(tree,path,std::move(replacement));return tree;
}
std::uint64_t integer(const MetadataValue &value) {
  if(const auto *v=std::get_if<std::uint64_t>(&value))return *v;
  return std::uint64_t(std::get<std::int64_t>(value));
}
class NoRead final : public PlaneSource {
public:
  mutable std::size_t planes=0,regions=0;
  DatasetShape shape()const override{return {2,3,4,5,7,1};}
  PixelType pixel_type()const override{return PixelType::UInt16;}
  std::shared_ptr<const Image> read_plane(PlanePosition)const override{++planes;throw std::runtime_error("SDK metadata path read plane");}
  Image read_region(PlanePosition,int,int,int,int)const override{++regions;throw std::runtime_error("SDK metadata path read region");}
};
struct ExhaustiveV1Visitor {
  int operator()(const AxisPlaneExportRequest &)const{return 1;}
  int operator()(const AxisRangeResliceRequest &)const{return 2;}
};

// Independently authored raw public-DTO transport. No expected values,
// comparisons, check counts or PASS fields enter this object.
template<class Values> MetadataValue actual_array(const Values &values) {
  MetadataArray out; for(const auto &value:values) out.emplace_back(value);
  return metadata_array(std::move(out));
}
MetadataObject actual_calibration(const SpatialCalibration &cal) {
  return {{"spacing",actual_array(cal.spacing)},{"origin",actual_array(cal.origin)},
      {"direction",actual_array(cal.direction)},{"unit",cal.unit},
      {"frame_interval",cal.frame_interval},{"time_origin",cal.time_origin},{"time_unit",cal.time_unit}};
}
MetadataObject actual_layout(const AxisExtractionLayout &layout) {
  const auto &g=layout.geometry;
  return {{"shape",actual_array(layout.shape.dimensions())},{"calibration",metadata_object(actual_calibration(layout.calibration))},
      {"source_axes",actual_array(layout.source_axes)},{"plane_name",layout.plane_name},
      {"working_bytes",std::uint64_t(layout.working_bytes)},
      {"geometry",metadata_object({{"origin",actual_array(g.origin)},
          {"horizontal_step",actual_array(g.horizontal_step)},{"vertical_step",actual_array(g.vertical_step)},
          {"horizontal_axis",g.horizontal_axis},{"vertical_axis",g.vertical_axis},{"unit",g.unit}})}};
}
MetadataValue actual_selection(const std::optional<Selection> &selection) {
  if(!selection)return {};
  MetadataArray points;for(const auto &point:selection->points)points.push_back(actual_array(point));
  return metadata_object({{"kind",selection->kind},{"points",metadata_array(std::move(points))},
      {"width",selection->width},{"attributes",metadata_object(selection->attributes)}});
}
void actual_hex(std::string &out,std::uint64_t bits,unsigned bytes) {
  static constexpr char digits[]="0123456789abcdef";
  for(unsigned i=0;i<bytes;++i){const auto value=unsigned((bits>>(8*i))&255);out.push_back(digits[value>>4]);out.push_back(digits[value&15]);}
}
const char *actual_type(PixelType type) {
  switch(type){case PixelType::UInt8:return "uint8";case PixelType::UInt16:return "uint16";
    case PixelType::Float32:return "float32";case PixelType::Float64:return "float64";}
  throw std::logic_error("Unknown observed native pixel type");
}
MetadataObject actual_history(const HistoryInfo &h) {
  return {{"undo_steps",std::uint64_t(h.undo_steps)},{"redo_steps",std::uint64_t(h.redo_steps)},
      {"step_limit",std::uint64_t(h.step_limit)},{"pixel_budget",std::uint64_t(h.pixel_budget)},
      {"pixels",metadata_object({{"logical_bytes",std::uint64_t(h.pixels.logical_bytes)},
          {"current_bytes",std::uint64_t(h.pixels.current_bytes)},{"retained_bytes",std::uint64_t(h.pixels.retained_bytes)},
          {"obsolete_bytes",std::uint64_t(h.pixels.obsolete_bytes)},{"retained_planes",std::uint64_t(h.pixels.retained_planes)},
          {"exact",h.pixels.exact}})}};
}
MetadataObject actual_snapshot(const DocumentSnapshot &snapshot,const HistoryInfo &history,bool pixels=false) {
  const auto &data=snapshot.data();const auto &ds=data.dataset;const auto shape=ds.shape();
  const auto revisions=snapshot.revisions();const auto &display=snapshot.display();
  MetadataArray pan;for(const auto &point:display.pan)pan.push_back(actual_array(point));
  MetadataArray rois;for(const auto &roi:data.rois)rois.push_back(metadata_object({
      {"name",roi.name},{"selection",actual_selection(roi.selection)},
      {"t",roi.t?MetadataValue(*roi.t):MetadataValue{}},{"c",roi.c?MetadataValue(*roi.c):MetadataValue{}},
      {"z",roi.z?MetadataValue(*roi.z):MetadataValue{}},{"attributes",metadata_object(roi.attributes)}}));
  MetadataArray overlays;for(const auto &overlay:data.overlays)overlays.push_back(metadata_object(overlay));
  MetadataObject result{{"document_id",snapshot.id()},
      {"data_identity",snapshot.data_identity()?MetadataValue(*snapshot.data_identity()):MetadataValue{}},
      {"shape",actual_array(shape.dimensions())},{"dtype",std::string(actual_type(ds.pixel_type()))},
      {"sample_policy",std::string(ds.sample_policy()==FloatingSamplePolicy::FiniteOnly?"finite_only":"preserve_quiet_nan_float32")},
      {"calibration",metadata_object(actual_calibration(ds.calibration()))},{"dirty",snapshot.dirty()},
      {"revisions",metadata_object({{"pixels",revisions.pixels},{"metadata",revisions.metadata},
          {"selection",revisions.selection},{"display",revisions.display}})},
      {"display",metadata_object({{"t",display.t},{"c",display.c},{"orthogonal",display.orthogonal},
          {"linked_zoom",display.linkedZoom},{"crosshair",actual_array(display.crosshair)},
          {"zoom",display.zoom},{"plane_zoom",actual_array(display.planeZoom)},
          {"pan",metadata_array(std::move(pan))},{"revision",display.revision},{"properties",metadata_object(display.properties)}})},
      {"history",metadata_object(actual_history(history))},
      {"metadata",metadata_object({{"title",data.metadata.title},{"properties",metadata_object(data.metadata.properties)},
          {"extensions",metadata_object(data.metadata.extensions)},{"record_layout",metadata_object(data.metadata.record_layout)}})},
      {"selection",actual_selection(data.selection)},{"previous_selection",actual_selection(data.previous_selection)},
      {"rois",metadata_array(std::move(rois))},{"overlays",metadata_array(std::move(overlays))},
      {"spectrum_present",bool(data.spectrum)},{"pixels_observed",pixels}};
  if(pixels){
    MetadataArray planes;
    for(int t=0;t<shape.t;++t)for(int c=0;c<shape.c;++c)for(int z=0;z<shape.z;++z){
      const auto plane=ds.plane({t,c,z});std::string declared,storage;
      for(const double value:plane->pixels){
        std::uint64_t bits=0;std::memcpy(&bits,&value,sizeof(bits));actual_hex(storage,bits,8);
        switch(ds.pixel_type()){
          case PixelType::UInt8:actual_hex(declared,std::uint8_t(value),1);break;
          case PixelType::UInt16:actual_hex(declared,std::uint16_t(value),2);break;
          case PixelType::Float32:actual_hex(declared,float32_source_sample_bits(value,ds.sample_policy()),4);break;
          case PixelType::Float64:actual_hex(declared,bits,8);break;
        }
      }
      planes.push_back(metadata_object({{"t",t},{"c",c},{"z",z},{"width",plane->width},
          {"height",plane->height},{"samples",plane->channels},{"declared_hex",declared},{"storage_f64le_hex",storage}}));
    }
    result["planes"]=metadata_array(std::move(planes));
  }
  return result;
}
MetadataObject actual_exception(const std::exception &error) {
  std::string category="std_exception";
  if(dynamic_cast<const RevisionConflict*>(&error))category="RevisionConflict";
  else if(dynamic_cast<const OperationCancelled*>(&error))category="OperationCancelled";
  else if(dynamic_cast<const std::length_error*>(&error))category="length_error";
  else if(dynamic_cast<const std::invalid_argument*>(&error))category="invalid_argument";
  else if(dynamic_cast<const std::out_of_range*>(&error))category="out_of_range";
  else if(dynamic_cast<const std::runtime_error*>(&error))category="runtime_error";
  return {{"category",category},{"native_typeid",std::string(typeid(error).name())},{"message",std::string(error.what())}};
}
template<class Error=std::invalid_argument,class Function>
MetadataObject observed_refusal(Function function,const char *message) {
  MetadataObject value;
  refuses<Error>([&]{try{function();}catch(const Error &error){value=actual_exception(error);throw;}},message);
  return value;
}
void actual_json_string(std::ostream &out,const std::string &value) {
  static constexpr char digits[]="0123456789abcdef";out<<'"';
  for(const unsigned char c:value){if(c=='"'||c=='\\')out<<'\\'<<char(c);
    else if(c<32)out<<"\\u00"<<digits[c>>4]<<digits[c&15];else out<<char(c);}
  out<<'"';
}
void actual_json_value(std::ostream &out,const MetadataValue &value);
void actual_json_object(std::ostream &out,const MetadataObject &values) {
  out<<'{';bool first=true;for(const auto &[key,value]:values){if(!first)out<<',';first=false;
    actual_json_string(out,key);out<<':';actual_json_value(out,value);}out<<'}';
}
void actual_json_array(std::ostream &out,const MetadataArray &values) {
  out<<'[';bool first=true;for(const auto &value:values){if(!first)out<<',';first=false;actual_json_value(out,value);}out<<']';
}
void actual_json_value(std::ostream &out,const MetadataValue &value) {
  std::visit([&](const auto &item){using T=std::decay_t<decltype(item)>;
    if constexpr(std::is_same_v<T,std::monostate>)out<<"null";
    else if constexpr(std::is_same_v<T,bool>)out<<(item?"true":"false");
    else if constexpr(std::is_same_v<T,std::int64_t>||std::is_same_v<T,std::uint64_t>)out<<item;
    else if constexpr(std::is_same_v<T,double>){
      if(!std::isfinite(item))throw std::invalid_argument("Nonfinite observed metadata number");
      std::ostringstream number;number<<std::setprecision(std::numeric_limits<double>::max_digits10)<<item;
      auto encoded=number.str();if(encoded.find_first_of(".eE")==std::string::npos)encoded+=".0";out<<encoded;
    }else if constexpr(std::is_same_v<T,std::string>)actual_json_string(out,item);
    else if constexpr(std::is_same_v<T,std::vector<std::string>>){out<<'[';bool first=true;for(const auto &s:item){if(!first)out<<',';first=false;actual_json_string(out,s);}out<<']';}
    else if constexpr(std::is_same_v<T,std::shared_ptr<const MetadataTree>>){
      if(!item)throw std::invalid_argument("Null observed metadata tree");
      if(const auto *array=std::get_if<MetadataArray>(&item->value))actual_json_array(out,*array);
      else actual_json_object(out,std::get<MetadataObject>(item->value));
    }
  },static_cast<const MetadataVariant&>(value));
}
void emit_actual_observations() {
  std::ostringstream out;actual_json_object(out,{{"schema_version",1},
      {"schema",std::string("imagejxx.scientific-orientation-sdk-observations")},
      {"observation_state",std::string("observed")},{"acceptance_claim",false},
      {"cases",metadata_object(observed)}});
  const auto raw=out.str();require(raw.size()<=16*1024*1024,"SDK actual observation JSON exceeds16MiB");
  std::cout<<"scientific_axis_orientation_observations "<<raw<<'\n';
}

void S01() {
  static_assert(std::variant_size<AxisExtractionRequest>::value==2,"Existing public variant widened");
  static_assert(std::is_same<decltype(std::declval<const ResolvedAxisExtractionCommand &>().request()),const AxisExtractionRequest &>::value,"V1 getter type changed");
  auto provider=std::make_shared<NoRead>();Document owner{Dataset(provider,calibration())};
  const AxisExtractionRequest plane=AxisPlaneExportRequest{Plane::XY,2,1,2};
  const AxisExtractionRequest range=request().range;
  require(std::visit(ExhaustiveV1Visitor{},plane)==1 && std::visit(ExhaustiveV1Visitor{},range)==2,"Unchanged external exhaustive two-type visitor");
  for(const auto &v1:{plane,range}) {
    const auto command=resolve_axis_extraction_command(owner.snapshot(),v1);validate_axis_extraction_command_record(command.record());
    require(integer(command.record().at("version"))==1,"V1 source/wire compatibility");
    refuses([&]{validate_axis_orientation_command_record(command.record());},"V2 accepted V1 plane/range");
    auto changed=replace(command.record(),{"version"},2);
    refuses([&]{validate_axis_extraction_command_record(changed);},"V1 accepted version-mutated record");
    refuses([&]{validate_axis_orientation_command_record(changed);},"V2 accepted unsupported plane/range operation");
  }
  for(auto p:{Plane::XY,Plane::XZ,Plane::YZ})for(int f=0;f<8;++f) {
    const auto r=request(p,f);const auto command=resolve_axis_orientation_command(owner.snapshot(),r);
    geometry(describe_axis_range_orientation(provider->shape(),PixelType::UInt16,calibration(),r),provider->shape(),calibration(),r);
    geometry(describe_axis_range_orientation(owner.snapshot().data().dataset,r),provider->shape(),calibration(),r);
    geometry(command.layout(),provider->shape(),calibration(),r);validate_axis_orientation_command_record(command.record());
    require(integer(command.record().at("version"))==2 && command.operation()=="axis_range_orientation","V2 explicit oriented kind");
    const auto wire=object(command.record().at("request"));
    require(wire.size()==10 && std::get<bool>(wire.at("reverse_order"))==r.reverse_order
        && std::get<bool>(wire.at("flip_vertical"))==r.flip_vertical && std::get<bool>(wire.at("transpose"))==r.transpose,
        "V2 flat request/actual Boolean flags");
    refuses([&]{validate_axis_extraction_command_record(command.record());},"Existing V1 widened to orientation");
    for(const char *flag:{"reverse_order","flip_vertical","transpose"})
      refuses([&]{validate_axis_orientation_command_record(replace(command.record(),{"request",flag},1));},"V2 flag accepted integer");
    refuses([&]{validate_axis_orientation_command_record(replace(command.record(),{"request","t"},true));},"V2 index accepted Boolean");
    auto mixed=command.record();auto req=wire;req["coordinate"]=0;mixed["request"]=metadata_object(std::move(req));
    refuses([&]{validate_axis_orientation_command_record(mixed);},"V2 accepted mixed request grammar");
  }
  require(provider->planes==0 && provider->regions==0,"SDK metadata grammar/descriptor read pixels");
  const auto captured=resolve_axis_orientation_command(owner.snapshot(),request());
  observed["S01"]=metadata_object({{"v1_variant_arity",std::uint64_t(std::variant_size<AxisExtractionRequest>::value)},
      {"metadata_source",metadata_object(actual_snapshot(owner.snapshot(),owner.history()))},
      {"record",metadata_object(captured.record())},{"layout",metadata_object(actual_layout(captured.layout()))},
      {"provider_counts",metadata_object({{"plane_reads",std::uint64_t(provider->planes)},{"region_reads",std::uint64_t(provider->regions)}})}});
  auto positive_provider=std::make_shared<NoRead>();Document positive_owner{Dataset(positive_provider,calibration())};
  const auto positive_error=observed_refusal<std::runtime_error>([&]{positive_owner.snapshot().data().dataset.plane({0,0,0});},"SDK provider positive read did not reach the source");
  observed["S01_provider_positive_control"]=metadata_object({{"source_document_id",positive_owner.id},
      {"plane_reads",std::uint64_t(positive_provider->planes)},{"region_reads",std::uint64_t(positive_provider->regions)},
      {"error",metadata_object(positive_error)}});
  std::cout<<"S01.metadata_flags_records_v1_v2\n";
}
void S02() {
  const std::array<std::pair<PixelType,int>,4> profiles{{{PixelType::UInt8,1},{PixelType::UInt16,3},{PixelType::Float32,4},{PixelType::Float64,1}}};
  for(const auto &[type,s]:profiles) {
    Document owner(fixture(type,s));const auto before=owner.snapshot();
    for(auto p:{Plane::XY,Plane::XZ,Plane::YZ})for(int f=0;f<8;++f) {
      const auto r=request(p,f);const auto command=resolve_axis_orientation_command(before,r);
      auto operation=prepare_axis_orientation(command);const auto candidate=operation.prepared_new_document();
      require(candidate!=nullptr && candidate->snapshot().data().dataset.pixel_type()==type,"SDK oriented declared dtype");
      geometry(command.layout(),before.data().dataset.shape(),calibration(),r);
      output(candidate->snapshot().data().dataset,before.data().dataset.shape(),r);++finite_rows;
      if(type==PixelType::UInt16 && s==3 && p==Plane::XZ && f==7){
        const auto source_before=actual_snapshot(before,owner.history());
        const auto output_after=actual_snapshot(candidate->snapshot(),candidate->history(),true);
        observed["S02"]=metadata_object({{"source_before",metadata_object(source_before)},
            {"source_after",metadata_object(actual_snapshot(owner.snapshot(),owner.history()))},
            {"record",metadata_object(command.record())},{"layout",metadata_object(actual_layout(command.layout()))},
            {"output",metadata_object(output_after)}});
      }
      require(owner.snapshot().shares_data(before) && owner.snapshot().revisions().matches(before.revisions(),Change::All),"SDK numerical adapter changed source");
    }
  }
  require(finite_rows==96,"SDK fresh adapter matrix96 differs; not reused ImageJ reference96");
  // A singleton retains signed fixed stride even though there is one output Z.
  Document owner(fixture());auto r=request(Plane::YZ);r.range.first=r.range.last;r.range.step=3;
  const auto command=resolve_axis_orientation_command(owner.snapshot(),r);auto single=prepare_axis_orientation(command);
  geometry(command.layout(),owner.snapshot().data().dataset.shape(),calibration(),r);
  output(single.prepared_new_document()->snapshot().data().dataset,owner.snapshot().data().dataset.shape(),r);
  std::cout<<"S02.exact_owned_samples_world_time\n";
}
void S03() {
  auto provider=std::make_shared<NoRead>();Document unread{Dataset(provider,calibration())};
  const auto command=resolve_axis_orientation_command(unread.snapshot(),request());const auto original=command.record();
  std::size_t callbacks=0;ExecutionControl control;control.checkpoint=[&]{++callbacks;};
  const auto cap_error=observed_refusal([&]{prepare_axis_orientation(command,control,command.layout().working_bytes-1);},"SDK tightened execution cap admitted");
  observed["S03_cap"]=metadata_object({{"error",metadata_object(cap_error)},
      {"execution_cap",std::uint64_t(command.layout().working_bytes-1)},
      {"record_before",metadata_object(original)},{"record_after",metadata_object(command.record())},
      {"callback_count",std::uint64_t(callbacks)},{"plane_reads",std::uint64_t(provider->planes)},
      {"region_reads",std::uint64_t(provider->regions)}});
  require(callbacks==0 && provider->planes==0 && provider->regions==0 && metadata_equal(command.record(),original),"SDK cap followed read/callback or changed record");
  Document replacement(fixture());auto complete=request(Plane::YZ,2);complete.range.t=0;complete.range.c=1;
  const auto rebound=rebind_axis_orientation_command(replacement.snapshot(),original,complete);
  require(rebound.request().range.plane==Plane::YZ && rebound.request().range.t==0 && rebound.request().range.c==1
      && !rebound.request().reverse_order && rebound.request().flip_vertical && !rebound.request().transpose
      && metadata_equal(command.record(),original),"SDK complete explicit rebind/original preservation");
  geometry(rebound.layout(),replacement.snapshot().data().dataset.shape(),calibration(),complete);
  observed["S03_rebind"]=metadata_object({{"original_before",metadata_object(original)},
      {"original_after",metadata_object(command.record())},{"new_record",metadata_object(rebound.record())},
      {"layout",metadata_object(actual_layout(rebound.layout()))}});
  const auto damaged=replace(original,{"derived","working_bytes"},std::uint64_t(1));auto invalid=complete;invalid.range.t=99;
  observed["S03_original_first"]=metadata_object({{"damaged_original",metadata_object(damaged)},
      {"error",metadata_object(observed_refusal([&]{rebind_axis_orientation_command(replacement.snapshot(),damaged,invalid);},"SDK malformed original not checked first"))}});
  observed["S03_foreign_owner"]=metadata_object({{"record",metadata_object(original)},
      {"actual_owner",metadata_object(actual_snapshot(replacement.snapshot(),replacement.history()))},
      {"error",metadata_object(observed_refusal([&]{axis_orientation_command_from_record(replacement.snapshot(),original);},"SDK strict replay rebound fresh owner"))}});
  const auto capture=resolve_axis_orientation_command(replacement.snapshot(),complete);
  auto display=replacement.snapshot().display();display.t=1;display.c=2;display.crosshair={6,4,3};replacement.set_display(display);
  replacement.select(Selection("point",{{2,3}}));
  const auto replay=axis_orientation_command_from_record(replacement.snapshot(),capture.record());
  require(replay.request().range.t==0 && replay.request().range.c==1,"SDK display navigation retargeted replay");
  observed["S03_navigation"]=metadata_object({{"captured_record",metadata_object(capture.record())},
      {"replayed_record",metadata_object(replay.record())},{"actual_source",metadata_object(actual_snapshot(replacement.snapshot(),replacement.history()))}});
  auto prepared=prepare_axis_orientation(replay);output(prepared.prepared_new_document()->snapshot().data().dataset,replacement.snapshot().data().dataset.shape(),complete);
  auto tx=replacement.begin_edit();tx.set_property("stale",true);replacement.commit(tx);
  observed["S03_stale_metadata"]=metadata_object({{"record",metadata_object(capture.record())},
      {"actual_source",metadata_object(actual_snapshot(replacement.snapshot(),replacement.history()))},
      {"error",metadata_object(observed_refusal<RevisionConflict>([&]{axis_orientation_command_from_record(replacement.snapshot(),capture.record());},"SDK stale metadata replay accepted"))}});
  auto token=std::make_shared<CancellationToken>();token->cancel();
  refuses<OperationCancelled>([&]{prepare_axis_orientation(replay,{token});},"SDK pre-canceled preparation accepted");
  bool real_copy=false;ExecutionControl callback;callback.stage_progress=[&](std::string_view stage,std::size_t done,std::size_t){
    if(stage=="axis_range_orientation_copy" && done>0){real_copy=true;throw std::runtime_error("SDK owned callback failure");}
  };
  refuses<std::runtime_error>([&]{prepare_axis_orientation(replay,callback);},"SDK actual callback failure swallowed");
  require(real_copy && provider->planes==0 && provider->regions==0,"SDK callback/no-read fixture not exercised");
  std::cout<<"S03.replay_rebind_refusals_caps_callbacks\n";
}
void S04() {
  const auto policy=FloatingSamplePolicy::PreserveQuietNaNFloat32;
  constexpr std::array<std::uint32_t,8> bits{0,0x80000000u,1,0x007fffffu,0x7fc12345u,0xffc34567u,0x3f800000u,0xbf800000u};
  Stack planes;for(int z=0;z<4;++z){Image image(7,5,1,PixelType::Float32);for(int y=0;y<5;++y)for(int x=0;x<7;++x)
    image.at(x,y)=float32_source_sample(bits[(z*35+y*7+x)%8],policy);planes.push_back(std::move(image));}
  DocumentData source{Dataset::from_planes({1,1,4,5,7,1},std::move(planes),calibration(),policy)};
  source.metadata.title="SDK orientation quiet";source.metadata.properties["opaque"]=true;
  source.metadata.extensions["future"]=metadata_object({{"version",37}});source.metadata.record_layout["present"]=true;
  source.selection=Selection("rectangle",{{0,0},{4,3}});source.previous_selection=Selection("point",{{1,1}});
  source.rois={{"positioned",Selection("point",{{2,2}}),0,0,2}};source.overlays={{{"opaque",true}}};
  auto owner=std::make_shared<Document>(std::move(source));auto tx=owner->begin_edit();tx.set_property("history",true);owner->commit(tx);owner->mark_saved();
  tx=owner->begin_edit();tx.set_property("redo",true);owner->commit(tx);owner->undo();
  Application app;app.adopt(owner);const auto before=owner->snapshot();const auto history=owner->history();
  auto r=request(Plane::XY);r.range.t=0;r.range.c=0;
  const auto command=resolve_axis_orientation_command(before,r);auto operation=prepare_axis_orientation(command);const auto candidate=operation.prepared_new_document();
  const auto output_snapshot=candidate->snapshot();const auto output_history=candidate->history();const auto &data=output_snapshot.data();
  require(!output_snapshot.dirty() && !output_snapshot.same_document(before) && !output_snapshot.shares_data(before)
      && candidate->history().undo_steps==0 && candidate->history().redo_steps==0
      && output_snapshot.display().t==0 && output_snapshot.display().c==0,"SDK new owner clean history/identity");
  require(data.metadata.properties.empty() && data.metadata.record_layout.empty() && data.metadata.extensions.size()==1
      && data.metadata.extensions.count("axis_range_orientation")==1 && !data.selection && !data.previous_selection
      && data.rois.empty() && data.overlays.empty() && !data.spectrum,"SDK fresh output annotation policy");
  require(data.dataset.sample_policy()==policy && data.dataset.pixel_type()==PixelType::Float32,"SDK quiet sample policy");
  const auto size=data.dataset.shape();
  require(size.dimensions()==std::array<int,6>{1,1,2,7,5,1},"SDK quiet prepared dimensions differ");
  require(world(data.dataset.calibration(),{4,6,1})==world(calibration(),voxel({1,1,4,5,7,1},r,4,6,1)),"SDK quiet prepared world landmark");
  for(int k=0;k<size.z;++k){const auto plane=data.dataset.plane({0,0,k});for(int y=0;y<size.y;++y)for(int x=0;x<size.x;++x){const auto v=voxel({1,1,4,5,7,1},r,x,y,k);
    require(float32_source_sample_bits(plane->at(x,y),policy)==bits[(v[2]*35+v[1]*7+v[0])%8],"SDK quiet payload/signed-zero permutation");++bit_rows;}}
  candidate->set_history_policy(3,4096);auto display=candidate->snapshot().display();display.zoom=2;candidate->set_display(display);
  const auto published=app.publish(operation);
  require(published.created && published.document==candidate && app.active_document_id()==candidate->id && !operation.active(),"SDK exact staged publication");
  refuses([&]{app.publish(operation);},"SDK publication repeated");
  require(owner->snapshot().shares_data(before) && owner->snapshot().data_identity()==before.data_identity()
      && owner->snapshot().revisions().matches(before.revisions(),Change::All) && owner->snapshot().dirty()==before.dirty()
      && owner->history().undo_steps==history.undo_steps && owner->history().redo_steps==history.redo_steps,"SDK output publication changed source/history");
  SessionLimits limits;limits.max_documents=2;limits.max_planes=8;limits.max_samples=1024;limits.max_json_bytes=1024*1024;
  limits.max_encoded_bytes=1024*1024;limits.max_working_bytes=16*1024*1024;
  auto decoded=decode_session(encode_session({{owner->snapshot(),candidate->snapshot()},{},{metadata_object(command.record())},{}},limits),limits);
  require(decoded.source_version==3 && decoded.documents.size()==2 && decoded.workflow.size()==1
      && metadata_equal(object(decoded.workflow.at(0)),command.record()),"SDK NativeV3 oriented record roundtrip");
  for(const auto &fresh:decoded.documents)require(!fresh->snapshot().dirty() && fresh->history().undo_steps==0 && fresh->history().redo_steps==0,"SDK reopened owner not fresh clean");
  refuses([&]{axis_orientation_command_from_record(decoded.documents[0]->snapshot(),command.record());},"SDK Save/Open silently rebound strict record");
  const auto rebound=rebind_axis_orientation_command(decoded.documents[0]->snapshot(),command.record(),r);
  require(rebound.source().id()==decoded.documents[0]->id && metadata_equal(command.record(),object(decoded.workflow.at(0))),"SDK reopened explicit rebind changed original");
  const auto &reopened=decoded.documents[1]->snapshot().data().dataset;
  for(int k=0;k<size.z;++k){const auto a=data.dataset.plane({0,0,k}),b=reopened.plane({0,0,k});for(std::size_t i=0;i<a->pixels.size();++i)
    require(float32_source_sample_bits(a->pixels[i],policy)==float32_source_sample_bits(b->pixels[i],policy),"SDK NativeV3 quiet bits changed");}
  observed["S04"]=metadata_object({{"source_before",metadata_object(actual_snapshot(before,history,true))},
      {"source_after",metadata_object(actual_snapshot(owner->snapshot(),owner->history(),true))},
      {"record",metadata_object(command.record())},{"layout",metadata_object(actual_layout(command.layout()))},
      {"prepared_output",metadata_object(actual_snapshot(output_snapshot,output_history,true))},
      {"published_output",metadata_object(actual_snapshot(candidate->snapshot(),candidate->history(),true))},
      {"reopened_source",metadata_object(actual_snapshot(decoded.documents[0]->snapshot(),decoded.documents[0]->history(),true))},
      {"reopened_output",metadata_object(actual_snapshot(decoded.documents[1]->snapshot(),decoded.documents[1]->history(),true))},
      {"reopened_record",decoded.workflow.at(0)},{"rebound_record",metadata_object(rebound.record())},
      {"source_version",decoded.source_version}});
  std::cout<<"S04.fresh_owner_source_history_session\n";
}
} // namespace
int main(int argc,char **) {
  try {
    require(argc==1,"Scientific orientation SDK accepts no expected/output arguments");
    S01();S02();S03();S04();
    std::cout<<"scientific_axis_orientation: finite_adapter_rows="<<finite_rows<<" quiet_samples="<<bit_rows<<" checks="<<checks<<'\n';
    emit_actual_observations();
    return 0;
  } catch(const std::exception &error){std::cerr<<"scientific_axis_orientation after "<<checks<<" checks: "<<error.what()<<'\n';return 1;}
}
