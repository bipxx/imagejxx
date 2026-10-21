#include <imagejxx/application.hpp>
#include <imagejxx/axis_extraction_command.hpp>
#include <imagejxx/axis_orientation_command.hpp>
#include <imagejxx/session.hpp>

#include <array>
#include <chrono>
#include <cmath>
#include <complex>
#include <cstring>
#include <functional>
#include <iostream>
#include <limits>
#include <set>
#include <string>
#include <type_traits>
#include <vector>

namespace {
using namespace imagejxx;
std::size_t checks = 0;
const auto started = std::chrono::steady_clock::now();
void check(bool condition, const char *message) {
  ++checks;
  if (!condition) throw std::runtime_error(message);
  if (std::chrono::steady_clock::now() - started > std::chrono::seconds(60))
    throw std::runtime_error("Scientific orientation native assertions exceeded60seconds");
}
template<class Error = std::invalid_argument, class Function>
void rejects(Function work, const char *message) {
  bool caught = false;
  try { work(); } catch (const Error &) { caught = true; }
  check(caught, message);
}
SpatialCalibration calibration() {
  SpatialCalibration value;
  value.spacing = {.25, 1.5, 3}; value.origin = {-2, 4, 9};
  value.direction = {-1, .5, .25, 0, 1, .5, .5, 0, 1};
  value.unit = "um"; value.time_unit = "min";
  value.frame_interval = 2.5; value.time_origin = -3;
  return value;
}
std::array<int, 3> axes(Plane p) {
  return p == Plane::XY ? std::array<int,3>{0,1,2}
       : p == Plane::XZ ? std::array<int,3>{0,2,1} : std::array<int,3>{1,2,0};
}
const char *plane_name(Plane p) { return p == Plane::XY ? "XY" : p == Plane::XZ ? "XZ" : "YZ"; }
double literal(int t, int c, int z, int y, int x, int s) {
  return 64*t + 24*c + 8*z + 3*y + x + 2*s;
}
Dataset fixture(PixelType type = PixelType::UInt16, int samples = 1,
                DatasetShape shape = {2,3,4,5,7,1}, SpatialCalibration cal = calibration()) {
  shape.s = samples;
  Stack stack;
  for (int t=0;t<shape.t;++t) for (int c=0;c<shape.c;++c) for (int z=0;z<shape.z;++z) {
    Image image(shape.x,shape.y,samples,type);
    for (int y=0;y<shape.y;++y) for (int x=0;x<shape.x;++x) for (int s=0;s<samples;++s)
      image.at(x,y,s)=literal(t,c,z,y,x,s);
    stack.push_back(std::move(image));
  }
  return Dataset::from_planes(shape,std::move(stack),std::move(cal));
}
AxisRangeOrientationRequest request(Plane p=Plane::XZ, int flags=7) {
  const std::array<int,3> extent{7,5,4};
  return {{p,0,extent[axes(p)[2]]-1,2,1,2,1024*1024},
          bool(flags&1),bool(flags&2),bool(flags&4)};
}
std::array<int,3> source_voxel(DatasetShape shape, const AxisRangeOrientationRequest &r,
                               int u, int v, int k) {
  const auto a=axes(r.range.plane);
  const std::array<int,3> extent{shape.x,shape.y,shape.z};
  const int n=1+(r.range.last-r.range.first)/r.range.step;
  const int h=r.transpose?v:u;
  const int vertical=r.transpose?u:v;
  std::array<int,3> out{};
  out[a[0]]=h; out[a[1]]=r.flip_vertical?extent[a[1]]-1-vertical:vertical;
  out[a[2]]=r.reverse_order?r.range.first+(n-1-k)*r.range.step:r.range.first+k*r.range.step;
  return out;
}
// Independent affine arithmetic: no native descriptor, sampler or provenance
// value contributes to the expected source landmark or basis.
std::array<double,3> world(const SpatialCalibration &cal, std::array<int,3> voxel) {
  auto result=cal.origin;
  for(int row=0;row<3;++row) for(int column=0;column<3;++column)
    result[row]+=cal.direction[row*3+column]*cal.spacing[column]*voxel[column];
  return result;
}
void layout(const AxisExtractionLayout &actual, DatasetShape shape,
            const SpatialCalibration &cal, const AxisRangeOrientationRequest &r) {
  const auto a=axes(r.range.plane);
  const std::array<int,3> extent{shape.x,shape.y,shape.z};
  const int n=1+(r.range.last-r.range.first)/r.range.step;
  const int width=extent[a[r.transpose?1:0]], height=extent[a[r.transpose?0:1]];
  check(actual.shape.dimensions()==std::array<int,6>{1,1,n,height,width,shape.s},"Oriented TCZYXS layout differs");
  check(actual.source_axes==a && actual.plane_name==plane_name(r.range.plane),"Base source_axes were redefined by flags");
  const auto zero=source_voxel(shape,r,0,0,0);
  check(actual.calibration.origin==world(cal,zero) && actual.geometry.origin==world(cal,zero),"Oriented affine origin differs");
  const std::array<int,3> order{a[r.transpose?1:0],a[r.transpose?0:1],a[2]};
  const std::array<int,3> sign{r.transpose?(r.flip_vertical?-1:1):1,
      r.transpose?1:(r.flip_vertical?-1:1),r.reverse_order?-1:1};
  for(int column=0;column<3;++column) {
    const double spacing=cal.spacing[order[column]]*(column==2?r.range.step:1);
    check(actual.calibration.spacing[column]==spacing,"Oriented positive spacing/stride differs");
    for(int row=0;row<3;++row) {
      const double direction=cal.direction[row*3+order[column]]*sign[column];
      check(actual.calibration.direction[row*3+column]==direction,"Signed/sheared direction column differs");
      if(column<2) check((column?actual.geometry.vertical_step:actual.geometry.horizontal_step)[row]==direction*spacing,
                         "Direct geometry basis differs from independent source basis");
    }
  }
  check(actual.calibration.unit==cal.unit && actual.geometry.unit==cal.unit
      && actual.calibration.time_unit==cal.time_unit && actual.calibration.frame_interval==cal.frame_interval
      && actual.calibration.time_origin==cal.time_origin+r.range.t*cal.frame_interval,"Captured time/unit differs");
  check(actual.geometry.horizontal_axis==std::string(1,"XYZ"[order[0]])
      && actual.geometry.vertical_axis==std::string(1,"XYZ"[order[1]]),"Transposed axis labels differ");
  const auto plane=std::size_t(extent[a[0]])*extent[a[1]]*shape.s;
  const auto fallback=std::size_t(shape.x)*shape.y*shape.s;
  const auto strip=r.range.plane==Plane::XY?0:std::size_t(extent[a[0]])*shape.s;
  check(actual.working_bytes==(plane*n+plane+fallback+strip)*sizeof(double),"Oriented scoped output/scratch estimate differs");
  check(world(actual.calibration,{width-1,height-1,n-1})==world(cal,source_voxel(shape,r,width-1,height-1,n-1)),
        "Nonzero full affine/time landmark differs");
}
void samples(const Dataset &output, DatasetShape original, const AxisRangeOrientationRequest &r) {
  const auto size=output.shape();
  const auto a=axes(r.range.plane);const std::array<int,3> extent{original.x,original.y,original.z};
  const int count=1+(r.range.last-r.range.first)/r.range.step;
  check(size.dimensions()==std::array<int,6>{1,1,count,extent[a[r.transpose?0:1]],extent[a[r.transpose?1:0]],original.s},
        "Prepared dataset shape differs from independent TCZYXS expectation");
  const auto cal=calibration();const auto &out_cal=output.calibration();
  check(out_cal.origin==world(cal,source_voxel(original,r,0,0,0)) && out_cal.time_origin==cal.time_origin+r.range.t*cal.frame_interval
      && out_cal.frame_interval==cal.frame_interval && out_cal.unit==cal.unit && out_cal.time_unit==cal.time_unit,"Prepared dataset world/time metadata differs");
  const std::array<int,3> order{a[r.transpose?1:0],a[r.transpose?0:1],a[2]};
  const std::array<int,3> sign{r.transpose?(r.flip_vertical?-1:1):1,r.transpose?1:(r.flip_vertical?-1:1),r.reverse_order?-1:1};
  for(int column=0;column<3;++column){check(out_cal.spacing[column]==cal.spacing[order[column]]*(column==2?r.range.step:1),"Prepared dataset spacing differs");
    for(int row=0;row<3;++row)check(out_cal.direction[row*3+column]==sign[column]*cal.direction[row*3+order[column]],"Prepared dataset direction differs");}
  for(int k=0;k<size.z;++k) {
    const auto plane=output.plane({0,0,k});
    for(int y=0;y<size.y;++y) for(int x=0;x<size.x;++x) {
      const auto voxel=source_voxel(original,r,x,y,k);
      for(int s=0;s<size.s;++s)
        check(plane->at(x,y,s)==literal(r.range.t,r.range.c,voxel[2],voxel[1],voxel[0],s),"Exact integer sample permutation differs");
    }
  }
}
class Unread final : public PlaneSource {
public:
  DatasetShape sizes{2,3,4,5,7,1}; PixelType type=PixelType::UInt16;
  mutable std::size_t planes=0,regions=0;
  DatasetShape shape() const override {return sizes;}
  PixelType pixel_type() const override {return type;}
  std::shared_ptr<const Image> read_plane(PlanePosition) const override {
    ++planes;throw std::runtime_error("Metadata orientation path attempted a plane read");
  }
  Image read_region(PlanePosition,int,int,int,int) const override {
    ++regions;throw std::runtime_error("Metadata orientation path attempted a region read");
  }
};
MetadataObject object(const MetadataValue &v) {
  return std::get<MetadataObject>(std::get<std::shared_ptr<const MetadataTree>>(v)->value);
}
MetadataArray array(const MetadataValue &v) {
  return std::get<MetadataArray>(std::get<std::shared_ptr<const MetadataTree>>(v)->value);
}
using Path=std::vector<std::string>;
void edit(MetadataObject &tree,const Path &path,const std::function<void(MetadataValue &)> &change,std::size_t at=0) {
  if(at+1==path.size()) {change(tree.at(path[at]));return;}
  auto child=object(tree.at(path[at]));edit(child,path,change,at+1);tree[path[at]]=metadata_object(std::move(child));
}
MetadataObject replace(MetadataObject tree,const Path &path,MetadataValue value) {
  edit(tree,path,[&](MetadataValue &old){old=std::move(value);});return tree;
}
MetadataObject object_at(const MetadataObject &tree,const Path &path) {
  auto out=tree;for(const auto &key:path)out=object(out.at(key));return out;
}
void edit_object(MetadataObject &tree,const Path &path,const std::function<void(MetadataObject &)> &change) {
  if(path.empty()) {change(tree);return;}
  edit(tree,path,[&](MetadataValue &old){auto out=object(old);change(out);old=metadata_object(std::move(out));});
}
std::uint64_t integer(const MetadataValue &v) {
  if(const auto *n=std::get_if<std::uint64_t>(&v))return *n;
  return std::uint64_t(std::get<std::int64_t>(v));
}
MetadataValue alternate(const MetadataValue &v) {
  if(const auto *n=std::get_if<std::int64_t>(&v)) {if(*n>=0)return std::uint64_t(*n);}
  if(const auto *n=std::get_if<std::uint64_t>(&v)) {if(*n<=std::uint64_t((std::numeric_limits<std::int64_t>::max)()))return std::int64_t(*n);}
  if(const auto *node=std::get_if<std::shared_ptr<const MetadataTree>>(&v)) {
    if(const auto *o=std::get_if<MetadataObject>(&(*node)->value)) {
      MetadataObject out;for(const auto &[key,value]:*o)out[key]=alternate(value);return metadata_object(std::move(out));
    }
    MetadataArray out;for(const auto &value:std::get<MetadataArray>((*node)->value))out.push_back(alternate(value));return metadata_array(std::move(out));
  }
  return v;
}
void unchanged(const Document &owner,const DocumentSnapshot &before,const HistoryInfo &history) {
  const auto after=owner.snapshot();const auto &a=after.display(),&b=before.display();
  check(after.shares_data(before) && after.data_identity()==before.data_identity()
      && after.revisions().matches(before.revisions(),Change::All) && after.dirty()==before.dirty(),"Adapter changed source data/revisions/dirty");
  const auto h=owner.history();
  check(h.undo_steps==history.undo_steps && h.redo_steps==history.redo_steps && h.step_limit==history.step_limit && h.pixel_budget==history.pixel_budget,
        "Adapter changed source history/policy");
  check(a.t==b.t && a.c==b.c && a.crosshair==b.crosshair && a.zoom==b.zoom && a.planeZoom==b.planeZoom && a.pan==b.pan
      && a.orthogonal==b.orthogonal && a.linkedZoom==b.linkedZoom && metadata_equal(a.properties,b.properties),"Adapter changed source display");
}
std::shared_ptr<Document> annotated() {
  DocumentData data{fixture(PixelType::UInt16,3)};
  data.metadata.title="Orientation command source";
  data.metadata.properties["opaque"]=metadata_object({{"payload",metadata_array({1,true,std::string("retained")})}});
  data.metadata.extensions["future"]=metadata_object({{"version",37}});data.metadata.record_layout["presence"]=true;
  data.selection=Selection("rectangle",{{1,1},{4,3}});data.previous_selection=Selection("line",{{0,0},{5,3}});
  data.rois={{"positioned",Selection("point",{{2,3}}),1,2,1},{"wildcard",Selection("point",{{3,2}}),{},{},{}}};
  data.overlays={{{"opaque",true}}};
  data.spectrum=FourierState(Spectrum{7,5,std::vector<std::complex<double>>(35)},std::array<int,2>{5,7});
  auto owner=std::make_shared<Document>(std::move(data));
  auto tx=owner->begin_edit();tx.set_property("history",true);owner->commit(tx);owner->mark_saved();
  tx=owner->begin_edit();tx.set_property("redo",true);owner->commit(tx);owner->undo();
  auto display=owner->snapshot().display();display.t=1;display.c=2;display.crosshair={3,2,2};
  display.zoom=2;display.planeZoom={2,3,4};display.pan={{{1,-2},{3,-4},{5,-6}}};display.orthogonal=true;owner->set_display(display);
  return owner;
}

void N01() {
  std::size_t rows=0;
  for(auto type:{PixelType::UInt8,PixelType::UInt16,PixelType::Float32,PixelType::Float64})for(int s:{1,3,4}) {
    auto provider=std::make_shared<Unread>();provider->sizes.s=s;provider->type=type;
    const Dataset source(provider,calibration());Document owner(source);
    for(auto p:{Plane::XY,Plane::XZ,Plane::YZ})for(int f=0;f<8;++f) {
      const auto r=request(p,f);
      layout(describe_axis_range_orientation(provider->sizes,type,calibration(),r),provider->sizes,calibration(),r);
      layout(describe_axis_range_orientation(source,r),provider->sizes,calibration(),r);
      const auto command=resolve_axis_orientation_command(owner.snapshot(),r);
      layout(command.layout(),provider->sizes,calibration(),r);
      validate_axis_orientation_command_record(command.record());
      check(command.source().same_document(owner.snapshot()) && command.operation()=="axis_range_orientation","Resolution lost source/operation");
      ++rows;
    }
    check(provider->planes==0 && provider->regions==0,"Metadata matrix read source pixels");
  }
  check(rows==288,"Descriptor adapter matrix membership differs");
}
void N02() {
  std::size_t rows=0;
  for(int s:{1,3,4}) {
    Document owner(fixture(PixelType::UInt16,s));const auto before=owner.snapshot();const auto history=owner.history();
    for(auto p:{Plane::XY,Plane::XZ,Plane::YZ})for(int f=0;f<8;++f) {
      auto r=request(p,f);const auto command=resolve_axis_orientation_command(before,r);
      auto operation=prepare_axis_orientation(command);const auto out=operation.prepared_new_document();
      check(out && operation.active() && operation.operation()=="axis_range_orientation"
          && out->snapshot().data().dataset.pixel_type()==PixelType::UInt16,"Sibling did not stage correctly typed oriented output");
      layout(command.layout(),before.data().dataset.shape(),calibration(),r);
      samples(out->snapshot().data().dataset,before.data().dataset.shape(),r);unchanged(owner,before,history);++rows;
      r.range.first=r.range.last;r.range.step=3;
      const auto singleton=resolve_axis_orientation_command(before,r);auto one=prepare_axis_orientation(singleton);
      check(one.prepared_new_document()->snapshot().data().dataset.shape().z==1,"Singleton lost Z axis");
      layout(singleton.layout(),before.data().dataset.shape(),calibration(),r);
      samples(one.prepared_new_document()->snapshot().data().dataset,before.data().dataset.shape(),r);
    }
  }
  check(rows==72,"New prepared adapter owned72 denominator differs");
}
void N03() {
  Document owner(fixture());const auto snapshot=owner.snapshot();
  for(auto p:{Plane::XY,Plane::XZ,Plane::YZ})for(int flags=0;flags<8;++flags) {
    const auto r=request(p,flags);const auto command=resolve_axis_orientation_command(snapshot,r);const auto record=command.record();
    check(record.size()==7 && std::get<std::string>(record.at("schema"))=="imagejxx.axis-extraction-command"
        && integer(record.at("version"))==2 && std::get<std::string>(record.at("operation"))=="axis_range_orientation"
        && std::get<std::string>(record.at("destination"))=="new_document","Canonical V2 envelope differs");
    const auto wire=object(record.at("request"));
    std::set<std::string> keys;for(const auto &[key,value]:wire){(void)value;keys.insert(key);}
    check(keys==std::set<std::string>{"plane","first","last","step","t","c","max_working_bytes","reverse_order","flip_vertical","transpose"},"V2 request not exactly flat10 fields");
    check(std::get<bool>(wire.at("reverse_order"))==r.reverse_order && std::get<bool>(wire.at("flip_vertical"))==r.flip_vertical
        && std::get<bool>(wire.at("transpose"))==r.transpose && integer(wire.at("t"))==1 && integer(wire.at("c"))==2,"V2 captured flags/T/C differ");
    auto alternate_record=record;for(auto &[key,value]:alternate_record){(void)key;value=alternate(value);}
    validate_axis_orientation_command_record(alternate_record);
    check(metadata_equal(axis_orientation_command_from_record(snapshot,alternate_record).record(),record),"Exact alternate integers changed canonical V2 replay");
    check(metadata_equal(resolve_axis_orientation_command(snapshot,r).record(),record),"V2 record nondeterministic");
    rejects([&]{validate_axis_extraction_command_record(record);},"V1 accepted V2 orientation");
    const auto v1=resolve_axis_extraction_command(snapshot,AxisExtractionRequest{r.range});
    validate_axis_extraction_command_record(v1.record());rejects([&]{validate_axis_orientation_command_record(v1.record());},"V2 sibling accepted V1 range");
    if(flags==0) {
      const auto a=prepare_axis_orientation(command).prepared_new_document();const auto b=prepare_axis_extraction(v1).prepared_new_document();
      const auto size=a->snapshot().data().dataset.shape();
      for(int z=0;z<size.z;++z)check(a->snapshot().data().dataset.plane({0,0,z})->pixels==b->snapshot().data().dataset.plane({0,0,z})->pixels,"Flags-off V2 samples differ from V1 range");
      check(metadata_equal(command.record(),record) && integer(record.at("version"))==2,"Flags-off orientation collapsed into V1");
    }
  }
}
void N04() {
  auto provider=std::make_shared<Unread>();Document owner{Dataset(provider,calibration())};
  const auto command=resolve_axis_orientation_command(owner.snapshot(),request());const auto original=command.record();
  auto invalid_request=request();invalid_request.range.first=-1;
  const auto bad=[&](const MetadataObject &record) {
    rejects([&]{validate_axis_orientation_command_record(record);},"V2 standalone grammar accepted malformed claim");
    rejects([&]{axis_orientation_command_from_record(owner.snapshot(),record);},"V2 replay accepted malformed claim");
    rejects([&]{rebind_axis_orientation_command(owner.snapshot(),record,invalid_request);},"Rebind did not validate malformed original first");
    check(provider->planes==0 && provider->regions==0,"Invalid V2 record acquired source pixels");
  };
  for(const Path &path:std::vector<Path>{{},{"source"},{"source","revisions"},{"source","calibration"},{"request"},{"derived"},{"derived","calibration"},{"derived","geometry"}}) {
    const auto values=object_at(original,path);
    for(const auto &[key,value]:values) {
      (void)value;auto copy=original;edit_object(copy,path,[&](MetadataObject &v){v.erase(key);});bad(copy);
    }
    auto copy=original;edit_object(copy,path,[](MetadataObject &v){v["unexpected"]=true;});bad(copy);
  }
  for(const auto *flag:{"reverse_order","flip_vertical","transpose"})for(const MetadataValue &value:std::vector<MetadataValue>{0,1,1.0,std::string("true"),std::monostate{}})
    bad(replace(original,{"request",flag},value));
  for(const auto *index:{"first","last","step","t","c","max_working_bytes"}) {
    bad(replace(original,{"request",index},true));bad(replace(original,{"request",index},.5));
    if(std::string(index)!="max_working_bytes")
      bad(replace(original,{"request",index},std::numeric_limits<std::uint64_t>::max()));
    else bad(replace(original,{"request",index},0));
  }
  for(const auto &record:std::vector<MetadataObject>{replace(original,{"version"},1),replace(original,{"version"},true),
      replace(original,{"operation"},std::string("axis_range_reslice")),replace(original,{"destination"},std::string("in_place")),
      replace(original,{"request","plane"},std::string("xz")),replace(original,{"request","step"},0),
      replace(original,{"source","document_id"},std::uint64_t(0)),replace(original,{"derived","working_bytes"},std::uint64_t(1))})bad(record);
  for(const Path &path:std::vector<Path>{{"source","calibration","spacing"},{"derived","shape"},{"derived","calibration","origin"},{"derived","geometry","horizontal_step"}}) {
    // Native MetadataTree construction already refuses nested nonfinite values.
    // Keep that actual public boundary inside each expected refusal; a malformed
    // fixture must never escape before the command-validation assertion starts.
    const auto fixture=[&] {
      auto copy=original;
      edit(copy,path,[](MetadataValue &v){auto a=array(v);a[0]=std::numeric_limits<double>::infinity();v=metadata_array(std::move(a));});
      return copy;
    };
    rejects([&]{validate_axis_orientation_command_record(fixture());},"Nested nonfinite metadata/record boundary accepted claim");
    rejects([&]{axis_orientation_command_from_record(owner.snapshot(),fixture());},"Nested nonfinite replay boundary accepted claim");
    rejects([&]{rebind_axis_orientation_command(owner.snapshot(),fixture(),invalid_request);},"Nested nonfinite rebind boundary accepted claim");
    check(provider->planes==0 && provider->regions==0,"Nonfinite refusal acquired source pixels");
  }
  auto nonfinite=original;nonfinite["version"]=std::numeric_limits<double>::infinity();
  bad(nonfinite); // Top-level scalar is representable and reaches all validators.
  for(const Path &path:std::vector<Path>{{"source","calibration","origin"},{"source","calibration","spacing"},
      {"derived","shape"},{"derived","source_axes"},{"derived","calibration","origin"},{"derived","calibration","spacing"},
      {"derived","calibration","direction"},{"derived","geometry","origin"},{"derived","geometry","horizontal_step"},
      {"derived","geometry","vertical_step"}}) {
    auto copy=original;edit(copy,path,[](MetadataValue &v){auto a=array(v);
      if(const auto *number=std::get_if<double>(&a[0]))a[0]=*number+.125;
      else a[0]=integer(a[0])+std::uint64_t(1);
      v=metadata_array(std::move(a));});bad(copy);
  }
  bad(replace(original,{"source","calibration","time_origin"},13.0));
  bad(replace(original,{"derived","calibration","frame_interval"},7.0));
  check(metadata_equal(command.record(),original),"Detached malformed copies mutated command record");
}
DocumentSnapshot changed(const Document &owner,const DocumentSnapshot &base,Dataset dataset,Revisions revisions) {
  auto data=std::make_shared<DocumentData>(base.data());data->dataset=std::move(dataset);
  return {owner.identity,revisions,std::move(data),base.display(),base.dirty(),base.data_identity()};
}
void N05() {
  auto provider=std::make_shared<Unread>();Document owner{Dataset(provider,calibration())};const auto source=owner.snapshot();
  const auto record=resolve_axis_orientation_command(source,request()).record();
  Document other{Dataset(provider,calibration())};
  rejects([&]{axis_orientation_command_from_record(other.snapshot(),record);},"Strict orientation rebound another owner");
  for(auto dependency:{Change::Pixels,Change::Metadata}) {
    auto rev=source.revisions();rev.advance(dependency);
    const auto stale=changed(owner,source,source.data().dataset,rev);
    rejects<RevisionConflict>([&]{axis_orientation_command_from_record(stale,record);},"Strict orientation ignored revision dependency");
  }
  for(int which=0;which<9;++which) {
    auto cal=calibration();auto foreign=std::make_shared<Unread>();
    if(which==0)foreign->sizes.x=8;else if(which==1)foreign->type=PixelType::Float64;
    else if(which==2)cal.spacing[0]=.5;else if(which==3)cal.origin[1]=5;else if(which==4)cal.direction[1]=.75;
    else if(which==5)cal.unit="mm";else if(which==6)cal.time_unit="s";else if(which==7)cal.frame_interval=3;else cal.time_origin=-2;
    const auto altered=changed(owner,source,Dataset(foreign,cal),source.revisions());
    rejects([&]{axis_orientation_command_from_record(altered,record);},"Strict orientation ignored shape/type/full calibration");
    check(foreign->planes==0 && foreign->regions==0,"Stale geometry rejection acquired pixels");
  }
  Document finite(fixture(PixelType::Float32));const auto before=finite.snapshot();
  const auto finite_record=resolve_axis_orientation_command(before,request()).record();
  const auto quiet=Dataset::from_planes(before.data().dataset.shape(),before.data().dataset.shared_planes()->materialize(),calibration(),FloatingSamplePolicy::PreserveQuietNaNFloat32);
  const auto altered=changed(finite,before,quiet,before.revisions());
  rejects([&]{axis_orientation_command_from_record(altered,finite_record);},"Strict orientation ignored sample policy");
  auto tx=owner.begin_edit();tx.set_property("undo_stale",true);owner.commit(tx);owner.undo();
  rejects<RevisionConflict>([&]{axis_orientation_command_from_record(owner.snapshot(),record);},"Equal-values undo revived stale record");
  check(provider->planes==0 && provider->regions==0,"Strict replay/refusal performed IO");
}
void N06() {
  auto old=std::make_shared<Unread>();Document owner{Dataset(old,calibration())};
  const auto command=resolve_axis_orientation_command(owner.snapshot(),request());const auto original=command.record();
  auto cal=calibration();cal.spacing={2,4,.5};cal.origin={11,-5,21};cal.direction={0,-1,.25,1,.5,0,0,0,-1};
  cal.unit="mm";cal.time_origin=13;cal.frame_interval=.25;cal.time_unit="s";
  auto next=std::make_shared<Unread>();next->sizes={3,4,6,8,9,1};Document destination{Dataset(next,cal)};
  const AxisRangeOrientationRequest replacement{{Plane::YZ,2,8,3,2,3,65536},false,true,false};
  const auto rebound=rebind_axis_orientation_command(destination.snapshot(),original,replacement);
  layout(rebound.layout(),next->sizes,cal,replacement);
  check(rebound.source().id()==destination.id && rebound.request().range.t==2 && rebound.request().range.c==3
      && rebound.request().range.max_working_bytes==65536 && !rebound.request().reverse_order && rebound.request().flip_vertical
      && !rebound.request().transpose && metadata_equal(command.record(),original),"Complete explicit rebind lost field or mutated original");
  check(metadata_equal(axis_orientation_command_from_record(destination.snapshot(),rebound.record()).record(),rebound.record()),"Rebound V2 cannot replay its new owner");
  auto malformed=replace(original,{"derived","working_bytes"},std::uint64_t(1));
  rejects([&]{rebind_axis_orientation_command(destination.snapshot(),malformed,replacement);},"Rebind rescued malformed original derived claims");
  auto invalid=replacement;invalid.range.t=99;
  rejects([&]{rebind_axis_orientation_command(destination.snapshot(),malformed,invalid);},"New request preceded original record validation");
  check(old->planes==0 && old->regions==0 && next->planes==0 && next->regions==0,"Rebind read old/new pixels");
}
void N07() {
  auto provider=std::make_shared<Unread>();Document owner{Dataset(provider,calibration())};
  auto r=request();const auto command=resolve_axis_orientation_command(owner.snapshot(),r);const auto original=command.record();
  std::size_t calls=0;ExecutionControl control;control.checkpoint=[&]{++calls;};control.progress=[&](std::size_t,std::size_t){++calls;};
  control.stage_progress=[&](std::string_view,std::size_t,std::size_t){++calls;};
  rejects([&]{prepare_axis_orientation(command,control,command.layout().working_bytes-1);},"Tightened execution cap admitted output");
  rejects([&]{prepare_axis_orientation(command,control,0);},"Zero execution cap admitted output");
  r.range.max_working_bytes=command.layout().working_bytes-1;
  rejects([&]{resolve_axis_orientation_command(owner.snapshot(),r);},"Resolve ignored captured request cap");
  auto overflow=request();overflow.range.max_working_bytes=(std::numeric_limits<std::size_t>::max)();
  auto huge_calibration=calibration();huge_calibration.spacing={1e308,1e308,1e308};
  rejects([&]{describe_axis_range_orientation(provider->sizes,PixelType::UInt16,huge_calibration,overflow);},"Overflowing derived affine admitted");
  const int maximum=(std::numeric_limits<int>::max)();
  rejects([&]{describe_axis_range_orientation({2,3,maximum,maximum,maximum,1},PixelType::Float64,calibration(),overflow);},"Unaddressable output layout admitted");
  check(calls==0 && provider->planes==0 && provider->regions==0 && metadata_equal(command.record(),original),"Cap refusal occurred after callbacks/reads or mutated record");
  Document eager(fixture());const auto valid=resolve_axis_orientation_command(eager.snapshot(),request());
  auto prepared=prepare_axis_orientation(valid,{},valid.layout().working_bytes);
  check(prepared.prepared_new_document()!=nullptr && metadata_equal(valid.record(),resolve_axis_orientation_command(eager.snapshot(),request()).record()),"Exact cap boundary failed or rewrote captured record");
}
void N08() {
  for(int phase=0;phase<3;++phase) {
    Document owner(fixture(PixelType::UInt16,1,{1,1,3,2,4097,1}));const auto before=owner.snapshot();const auto history=owner.history();
    const AxisRangeOrientationRequest r{{Plane::XY,0,1,1,0,0,4*1024*1024},true,true,true};
    const auto command=resolve_axis_orientation_command(before,r);auto token=std::make_shared<CancellationToken>();
    std::size_t copies=0,positive=0;ExecutionControl control;control.cancellation=token;
    control.stage_progress=[&](std::string_view stage,std::size_t done,std::size_t total) {
      if(stage=="axis_range_orientation_copy" && done==total)++copies;
      if((phase==0 && stage=="plane-extract.copy" && done>=1024)
          ||(phase==1 && stage=="axis_range_orientation_copy" && done>=1024)
          ||(phase==2 && stage=="Immutable plane sample validation" && copies==2 && done>=1024)) {positive=done;token->cancel();}
    };
    rejects<OperationCancelled>([&]{prepare_axis_orientation(command,control);},"Positive copy/final-validation cancellation swallowed");
    check(positive>=1024 && (phase!=2 || copies==2),"Cancellation did not reach intended positive-work stage");unchanged(owner,before,history);
  }
  Document owner(fixture());const auto before=owner.snapshot();const auto history=owner.history();
  const auto command=resolve_axis_orientation_command(before,request());ExecutionControl fail;
  bool reached=false;fail.stage_progress=[&](std::string_view stage,std::size_t done,std::size_t) {
    if(stage=="axis_range_orientation_copy" && done>0){reached=true;throw std::runtime_error("Owned callback rejection");}
  };
  rejects<std::runtime_error>([&]{prepare_axis_orientation(command,fail);},"User callback exception swallowed");
  check(reached,"Callback exception preceded actual copy work");unchanged(owner,before,history);
  auto canceled=std::make_shared<CancellationToken>();canceled->cancel();
  rejects<OperationCancelled>([&]{prepare_axis_orientation(command,{canceled});},"Pre-canceled sibling admitted output");
}
void N09() {
  auto owner=annotated();const auto before=owner->snapshot();const auto history=owner->history();
  const auto r=request();auto prepared=prepare_axis_orientation(resolve_axis_orientation_command(before,r));const auto fresh=prepared.prepared_new_document();
  const auto out=fresh->snapshot();const auto &data=out.data();
  check(!out.same_document(before) && !out.shares_data(before) && !out.dirty()
      && fresh->history().undo_steps==0 && fresh->history().redo_steps==0 && out.display().t==0 && out.display().c==0,"Output not independent clean fresh owner");
  check(data.metadata.title=="Orientation command source XZ 1-5 step 2 [R1 F1 T1]" && data.metadata.properties.empty()
      && data.metadata.record_layout.empty() && data.metadata.extensions.size()==1 && data.metadata.extensions.count("axis_range_orientation")==1
      && !data.selection && !data.previous_selection && data.rois.empty() && data.overlays.empty() && !data.spectrum,"Output copied source-only annotations/title policy differs");
  samples(data.dataset,before.data().dataset.shape(),r);unchanged(*owner,before,history);
  constexpr std::array<std::uint32_t,8> bits{0,0x80000000u,1,0x007fffffu,0x7fc12345u,0xffc34567u,0x3f800000u,0xbf800000u};
  const auto policy=FloatingSamplePolicy::PreserveQuietNaNFloat32;
  Stack planes;for(int z=0;z<4;++z){Image image(7,5,1,PixelType::Float32);for(int y=0;y<5;++y)for(int x=0;x<7;++x)image.at(x,y)=float32_source_sample(bits[(z*35+y*7+x)%8],policy);planes.push_back(std::move(image));}
  Document quiet(Dataset::from_planes({1,1,4,5,7,1},std::move(planes),calibration(),policy));
  auto qr=request(Plane::XY);qr.range.t=0;qr.range.c=0;
  const auto qcommand=resolve_axis_orientation_command(quiet.snapshot(),qr);auto qprepared=prepare_axis_orientation(qcommand);
  const auto quiet_owner=qprepared.prepared_new_document();check(bool(quiet_owner),"Quiet preparation has no staged owner");
  const auto quiet_snapshot=quiet_owner->snapshot();const auto &q=quiet_snapshot.data().dataset;
  check(q.sample_policy()==policy && q.pixel_type()==PixelType::Float32,"Adapter changed quiet declared type/policy");
  check(q.shape().dimensions()==std::array<int,6>{1,1,2,7,5,1},"Quiet adapter output lost exact singleton TCZYXS shape");
  check(world(q.calibration(),{4,6,1})==world(calibration(),source_voxel({1,1,4,5,7,1},qr,4,6,1)),"Quiet prepared affine landmark differs");
  const auto size=q.shape();for(int k=0;k<size.z;++k){const auto plane=q.plane({0,0,k});for(int y=0;y<size.y;++y)for(int x=0;x<size.x;++x){const auto v=source_voxel({1,1,4,5,7,1},qr,x,y,k);check(float32_source_sample_bits(plane->at(x,y),policy)==bits[(v[2]*35+v[1]*7+v[0])%8],"Quiet payload/signed-zero bit permutation differs");}}
  SessionLimits limits;limits.max_documents=2;limits.max_planes=8;limits.max_samples=1024;limits.max_json_bytes=1024*1024;limits.max_encoded_bytes=1024*1024;limits.max_working_bytes=16*1024*1024;
  const auto reopened=decode_session(encode_session({{qprepared.prepared_new_document()->snapshot()},{},{metadata_object(qcommand.record())},{}},limits),limits);
  check(reopened.source_version==3 && reopened.documents.size()==1 && metadata_equal(object(reopened.workflow.at(0)),qcommand.record()),"NativeV3 lost oriented record");
  for(int k=0;k<size.z;++k){const auto a=q.plane({0,0,k}),b=reopened.documents[0]->snapshot().data().dataset.plane({0,0,k});for(std::size_t i=0;i<a->pixels.size();++i)check(float32_source_sample_bits(a->pixels[i],policy)==float32_source_sample_bits(b->pixels[i],policy),"NativeV3 lost quiet sample bits");}
}
void N10() {
  for(int scenario=0;scenario<8;++scenario) {
    Application app;auto owner=annotated();app.adopt(owner);
    auto prepared=prepare_axis_orientation(resolve_axis_orientation_command(owner->snapshot(),request()));auto fresh=prepared.prepared_new_document();
    if(scenario==0){auto tx=owner->begin_edit();tx.replace_plane({1,2,0},Image(7,5,3,PixelType::UInt16));owner->commit(tx);}
    if(scenario==1){auto tx=owner->begin_edit();tx.set_property("stale",true);owner->commit(tx);}
    if(scenario==2)app.close(owner->id,true);
    if(scenario==4){auto tx=fresh->begin_edit();tx.set_title("Changed");fresh->commit(tx);}
    if(scenario==5){auto tx=fresh->begin_edit();tx.select(Selection("point",{{1,1}}));fresh->commit(tx);}
    if(scenario==6){auto tx=fresh->begin_edit();tx.set_property("history",true);fresh->commit(tx);fresh->undo();}
    if(scenario==7)fresh->checkpoint();
    const auto ids=app.document_ids();const auto active=app.active_document_id();const auto before=owner->snapshot();
    if(scenario==3){Application foreign;rejects<DocumentUnavailable>([&]{foreign.publish(prepared);},"Foreign app published output");check(foreign.document_ids().empty(),"Foreign failure created orphan");}
    else if(scenario==2)rejects<DocumentUnavailable>([&]{app.publish(prepared);},"Closed source published output");
    else rejects<RevisionConflict>([&]{app.publish(prepared);},"Stale/modified output published");
    check(prepared.active() && app.document_ids()==ids && app.active_document_id()==active && !app.contains(fresh->id)
        && owner->snapshot().shares_data(before),"Publication refusal changed membership/source or consumed preparation");
  }
  Application app;auto owner=annotated();app.adopt(owner);auto prepared=prepare_axis_orientation(resolve_axis_orientation_command(owner->snapshot(),request()));
  auto fresh=prepared.prepared_new_document();fresh->set_history_policy(3,4096);auto display=fresh->snapshot().display();display.zoom=2;fresh->set_display(display);
  const auto result=app.publish(prepared);check(result.document==fresh && result.created && !prepared.active(),"Publication changed staged owner");
  rejects([&]{app.publish(prepared);},"Prepared orientation published twice");
}
void N11() {
  auto owner=annotated();const auto captured=owner->snapshot();const auto command=resolve_axis_orientation_command(captured,request());
  auto d=owner->snapshot().display();d.t=0;d.c=0;d.crosshair={6,4,3};d.zoom=4;owner->set_display(d);owner->select(Selection("point",{{5,4}}));
  const auto live=owner->snapshot();const auto history=owner->history();const auto replay=axis_orientation_command_from_record(live,command.record());
  check(replay.request().range.t==1 && replay.request().range.c==2 && metadata_equal(replay.record(),command.record()),"Navigation retargeted captured T/C");
  auto prepared=prepare_axis_orientation(replay);samples(prepared.prepared_new_document()->snapshot().data().dataset,live.data().dataset.shape(),request());unchanged(*owner,live,history);
  Application app;app.adopt(owner);const auto published=app.publish(prepared);
  check(published.document==prepared.prepared_new_document() && owner->snapshot().display().t==0 && owner->snapshot().display().c==0,"Whole-plane publication retargeted source navigation");
}
struct TwoTypeVisitor {
  int operator()(const AxisPlaneExportRequest &) const {return 1;}
  int operator()(const AxisRangeResliceRequest &) const {return 2;}
};
void N12() {
  static_assert(std::variant_size<AxisExtractionRequest>::value==2,"Public extraction variant widened");
  static_assert(std::is_same<decltype(std::declval<const ResolvedAxisExtractionCommand &>().request()),const AxisExtractionRequest &>::value,"V1 getter type changed");
  Document owner(fixture());const auto snap=owner.snapshot();
  const AxisExtractionRequest plane=AxisPlaneExportRequest{Plane::XY,2,1,2,1024*1024};const AxisExtractionRequest range=request().range;
  check(std::visit(TwoTypeVisitor{},plane)==1 && std::visit(TwoTypeVisitor{},range)==2,"Unchanged exhaustive visitor failed");
  for(const auto &r:{plane,range}) {
    const auto old=resolve_axis_extraction_command(snap,r);validate_axis_extraction_command_record(old.record());
    check(integer(old.record().at("version"))==1,"Existing extraction no longer emits V1");
    check(prepare_axis_extraction(old).prepared_new_document()!=nullptr,"Existing V1 preparation failed");
    rejects([&]{validate_axis_orientation_command_record(old.record());},"Oriented validator silently consumed V1");
    auto v2=replace(old.record(),{"version"},2);rejects([&]{validate_axis_extraction_command_record(v2);},"V1 validator admitted version-mutated old command");
    rejects([&]{validate_axis_orientation_command_record(v2);},"V2 validator admitted plane/range operation");
  }
}
} // namespace

int main(int argc,char **argv) {
  const std::array<std::pair<const char *,void(*)()>,12> cases{{
      {"N01.metadata_descriptor_zero_reads_288",N01},{"N02.oriented_command_prepare_owned72_singletons",N02},
      {"N03.v1_preserved_v2_closed_canonical_records",N03},{"N04.grammar_flags_and_derived_tamper_no_reads",N04},
      {"N05.strict_replay_complete_source_dependencies",N05},{"N06.explicit_full_rebind_original_first",N06},
      {"N07.cap_before_callback_request_immutability",N07},{"N08.cancellation_callbacks_final_validation",N08},
      {"N09.new_output_source_annotations_and_samples",N09},{"N10.publication_stale_closed_foreign_modified_repeated",N10},
      {"N11.display_selection_navigation_independence",N11},{"N12.unchanged_public_two_type_visitors_and_v1_grammar",N12}}};
  try {
    const std::string selected=argc==1?"all":argv[1];bool found=selected=="all";
    if(argc>2)throw std::invalid_argument("Expected all or one exact N01..N12 case ID");
    for(const auto &[id,run]:cases)if(selected=="all" || selected==id){found=true;const auto before=checks;run();std::cout<<"P06i.scientific-axis-orientation-command."<<id<<": "<<checks-before<<" checks\n";}
    if(!found)throw std::invalid_argument("Unknown scientific orientation native case ID");
    return 0;
  } catch(const std::exception &error){std::cerr<<"Scientific orientation failed after "<<checks<<" checks: "<<error.what()<<'\n';return 1;}
}
