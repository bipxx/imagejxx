#include <imagejxx/application.hpp>
#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <cstring>
#include <iostream>
#include <limits>
#include <memory>
#include <set>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

using namespace imagejxx;
namespace {
void check(bool value, const char *message) {
  if (!value) throw std::runtime_error(message);
}
template<class Error, class Function> void rejects(Function function, const char *message) {
  bool caught=false; try {function();} catch (const Error &) {caught=true;}
  check(caught,message);
}
template<class Function> void callback_error(Function function,const char *expected) {
  try {function();} catch(const std::runtime_error &error) {
    check(std::string(error.what())==expected,"A callback exception was replaced");return;
  }
  throw std::runtime_error("A callback exception was swallowed");
}
bool exact(double a,double b) {return std::memcmp(&a,&b,sizeof(double))==0;}
void near(double actual,long double expected) {
  check(std::abs(static_cast<long double>(actual)-expected)<=2e-12L*
        std::max({1.L,std::abs(static_cast<long double>(actual)),std::abs(expected)}),
        "Owned nominal geometry differs from independent literal world mapping");
}
enum class Seed { Normal, ZPair, XOnly, ZOnly, TransposeOrder, SignedCycle, Fractional, NoncanonicalF32, ForeignNoncanonicalF32 };
struct Spec {
  DatasetShape shape{1,1,2,2,2,1};
  PixelType type=PixelType::Float32;
  SpatialCalibration cal;
  Seed seed=Seed::Normal;
};
double literal(const Spec &s,PlanePosition p,int x,int y,int sample) {
  switch(s.seed) {
  case Seed::ZPair:return (s.type==PixelType::UInt16?32768.:0.)+2*p.z;
  case Seed::XOnly:return x;
  case Seed::ZOnly:return p.z;
  case Seed::TransposeOrder:return x==0?1.:(p.z==0?0x1p80:-0x1p80);
  case Seed::SignedCycle: {
    const std::array<double,4> values{-0.,double(std::numeric_limits<float>::denorm_min()),
                                     0.,-double(std::numeric_limits<float>::denorm_min())};
    return values[(p.z+x+y)%values.size()];
  }
  case Seed::Fractional:return .5;
  case Seed::NoncanonicalF32:return .1;
  case Seed::ForeignNoncanonicalF32:return y==1?.1:10.*p.z+x;
  case Seed::Normal:return 1000.*p.t+100.*p.c+10.*p.z+2.*y+x+sample;
  }
  throw std::runtime_error("Unknown literal seed");
}
Image region(const Spec &s,PlanePosition p,int x,int y,int width,int height) {
  Image value(width,height,s.shape.s,s.type);
  for(int v=0;v<height;++v) for(int u=0;u<width;++u) for(int c=0;c<s.shape.s;++c)
    value.at(u,v,c)=literal(s,p,x+u,y+v,c);
  return value;
}
Dataset eager(const Spec &s) {
  Stack planes;
  for(int t=0;t<s.shape.t;++t) for(int c=0;c<s.shape.c;++c) for(int z=0;z<s.shape.z;++z)
    planes.push_back(region(s,{t,c,z},0,0,s.shape.x,s.shape.y));
  return Dataset::from_planes(s.shape,std::move(planes),s.cal);
}
class CountedSource final:public PlaneSource {
public:
  explicit CountedSource(Spec s):spec(std::move(s)) {}
  Spec spec;
  mutable std::size_t planes=0,regions=0;
  DatasetShape shape() const override {return spec.shape;}
  PixelType pixel_type() const override {return spec.type;}
  std::shared_ptr<const Image> read_plane(PlanePosition p) const override {
    ++planes; return std::make_shared<const Image>(region(spec,p,0,0,spec.shape.x,spec.shape.y));
  }
  Image read_region(PlanePosition p,int x,int y,int width,int height) const override {
    ++regions;return region(spec,p,x,y,width,height);
  }
  std::size_t reads() const {return planes+regions;}
};
class FallbackSource final:public PlaneSource {
public:
  explicit FallbackSource(Spec s):spec(std::move(s)){}
  Spec spec;mutable std::size_t planes=0;
  DatasetShape shape()const override{return spec.shape;}
  PixelType pixel_type()const override{return spec.type;}
  std::shared_ptr<const Image>read_plane(PlanePosition p)const override {
    ++planes;return std::make_shared<const Image>(region(spec,p,0,0,spec.shape.x,spec.shape.y));
  }
  // Deliberately inherit the public regional fallback: foreign XY pixels are
  // returned by the provider but are outside the extracted selected profile.
};
AxisRangeSlicerResizeRequest request(Plane plane=Plane::XZ) {
  AxisRangeSlicerResizeRequest value;value.orientation.range.plane=plane;return value;
}
std::size_t bytes(const Spec &s,int k,int m) {
  const auto a=std::size_t(s.shape.x),q=a*s.shape.z,p=a*m;
  return sizeof(double)*(std::size_t(k)*p+q+std::size_t(s.shape.x)*s.shape.y+a)+
    std::size_t(k)*(2*sizeof(Image)+sizeof(std::shared_ptr<const Image>)+256)+2*sizeof(Image)+256;
}
std::shared_ptr<Document> annotated(const Spec &s,bool lazy=false) {
  Dataset input=lazy?Dataset(std::make_shared<CountedSource>(s),s.cal):eager(s);
  DocumentData data{std::move(input),{"Slicer source",{{"retained",true}}},{},{}};
  data.selection=Selection("point",{{1,0}});data.previous_selection=Selection("point",{{0,0}});
  data.rois={{"saved",Selection("point",{{0,0}}),0,0,0}};
  data.overlays={{{"marker",true}}};data.metadata.extensions["opaque"]=true;
  data.metadata.record_layout={{"old",true}};
  data.spectrum=FourierState(Spectrum{s.shape.x,s.shape.y,
      std::vector<std::complex<double>>(std::size_t(s.shape.x)*s.shape.y)},
      std::array<int,2>{s.shape.y,s.shape.x});
  auto doc=std::make_shared<Document>(std::move(data));
  auto edit=doc->begin_edit();edit.set_property("history",true);doc->commit(edit);doc->undo();
  return doc;
}
void unchanged(const Document &doc,const DocumentSnapshot &before,const HistoryInfo &history) {
  const auto after=doc.snapshot();
  check(after.same_document(before)&&after.shares_data(before)&&after.data_identity()==before.data_identity()&&
        after.revisions().matches(before.revisions(),Change::All)&&after.dirty()==before.dirty()&&
        doc.history().undo_steps==history.undo_steps&&doc.history().redo_steps==history.redo_steps,
        "Operation changed retained owner data/state/history");
}
void pixel_mappings() {
  for(PixelType type:{PixelType::UInt8,PixelType::UInt16,PixelType::Float32}) {
    Spec spec;spec.type=type;spec.seed=Seed::ZPair;spec.cal.spacing={1,2.25,4};
    const auto input=eager(spec);
    for(Plane plane:{Plane::XZ,Plane::YZ}) for(bool transpose:{false,true}) {
      auto req=request(plane);req.orientation.transpose=transpose;
      const auto result=reslice_slicer_axis_range(input,req);
      const auto estimate=estimate_axis_range_slicer_resize(input,req);
      check(result.resize_branch==AxisRangeSlicerResizeBranch::Bilinear&&result.vertical_count==8,
            "Quarter-grid bilinear branch/count differs");
      const auto output=result.dataset.plane();
      const auto actual=transpose?output->at(2,0):output->at(0,2);
      const auto expected=type==PixelType::UInt16?32769.:type==PixelType::UInt8?1.:.5;
      check(exact(actual,expected),"Quarter-center or unsigned half-up rounding differs");
      check(result.dataset.pixel_type()==type&&estimate.width==output->width&&estimate.height==output->height,
            "Estimated shape or preserved type differs");
    }
  }
  Spec spec;spec.cal.spacing={1,1,2};spec.seed=Seed::XOnly;
  auto result=reslice_slicer_axis_range(eager(spec),request());
  check(exact(result.dataset.plane()->at(1,0),0.9990000128746033),
        "Real resize failed to clamp the unchanged in-plane axis");
  spec.seed=Seed::ZOnly;
  result=reslice_slicer_axis_range(eager(spec),request());
  check(exact(result.dataset.plane()->at(0,3),0.9990000128746033),
        "Real resize failed to clamp its changed Z axis");
  spec.seed=Seed::TransposeOrder;spec.cal.spacing={1,1,4};
  result=reslice_slicer_axis_range(eager(spec),request());
  check(exact(result.dataset.plane()->at(1,3),0.),"Untransposed binary64 arithmetic order differs");
  auto transposed=request();transposed.orientation.transpose=true;
  result=reslice_slicer_axis_range(eager(spec),transposed);
  check(exact(result.dataset.plane()->at(3,1),0.0010000000474974513),
        "Transpose must precede bilinear floating arithmetic");
  std::cout<<"slicer.mappings.quarter_both_axes_rounding_transpose_order\n";
}
void exact_copy_branches() {
  Spec spec;spec.shape={1,1,7,2,2,1};spec.seed=Seed::SignedCycle;
  for(double depth:{1.,1.125}) for(bool flip:{false,true}) for(bool transpose:{false,true}) {
    spec.cal.spacing={1,2.25,depth};auto req=request();req.orientation.flip_vertical=flip;req.orientation.transpose=transpose;
    const auto result=reslice_slicer_axis_range(eager(spec),req);
    check(result.resize_branch==(depth==1.?AxisRangeSlicerResizeBranch::NoResize:AxisRangeSlicerResizeBranch::EqualDimensionCrop)&&
          exact(result.processor_vertical_origin,0.)&&exact(result.processor_vertical_step,1.),"Copy branch geometry changed");
    const auto output=result.dataset.plane();
    for(int z=0;z<7;++z) for(int x=0;x<2;++x)
      check(exact(transpose?output->at(z,x):output->at(x,z),literal(spec,{0,0,flip?6-z:z},x,0,0)),
            "Canonical signed-zero/subnormal copy was quantized or reordered");
  }
  spec.shape={1,1,2,1,2,1};spec.seed=Seed::SignedCycle;spec.cal.spacing={1,1,1};
  check(reslice_slicer_axis_range(eager(spec),request()).dataset.shape().z==1,
        "A singleton fixed extent must not be confused with a singleton processor axis");
  std::cout<<"slicer.mappings.exact_canonical_copy_branches\n";
}
void geometry_and_fresh_publication() {
  Spec spec;spec.type=PixelType::UInt16;spec.shape={2,3,10,5,4,1};
  spec.cal.spacing={1,2.25,1.3};spec.cal.origin={11,-7,19};
  spec.cal.direction={-1,.25,1.5,.5,1,0,0,.75,2};
  spec.cal.unit="um";spec.cal.frame_interval=.75;spec.cal.time_origin=2.25;
  auto req=request();req.orientation.range={Plane::XZ,1,4,2,1,2};
  req.orientation.reverse_order=req.orientation.flip_vertical=req.orientation.transpose=true;
  const auto result=reslice_slicer_axis_range(eager(spec),req);const auto shape=result.dataset.shape();
  volatile double scale=13./10.;volatile double reciprocal=1./scale;
  check(exact(result.processor_vertical_step,reciprocal)&&!exact(reciprocal,10./13.),
        "Nominal affine scale must retain reciprocal processor-scale bits");
  volatile double quarter=scale/4.;volatile double center=6.5+quarter;
  volatile double quotient=(0.-center)/scale;volatile double origin=quotient+5.;
  check(exact(result.processor_vertical_origin,origin)&&shape.t==1&&shape.c==1&&shape.z==2&&shape.x==13&&shape.y==4,
        "Captured T/C, unaligned reverse, transpose or nominal grid differs");
  check(result.scalar_spacing==std::array<double,3>{1,1,2.25}&&result.dataset.calibration().unit=="um"&&
        exact(result.dataset.calibration().time_origin,3.),"Scalar metric or selected native time/unit differs");
  const auto &cal=result.dataset.calibration();
  const std::array<double,3> nominal_spacing{1.3*double(reciprocal),1,4.5};
  const std::array<double,9> signed_columns{-1.5,-1,-.25,-0.,.5,-1,-2,0,-.75};
  for(std::size_t i=0;i<3;++i)check(exact(cal.spacing[i],nominal_spacing[i]),"Nominal spacing was renormalized");
  for(std::size_t i=0;i<9;++i)check(exact(cal.direction[i],signed_columns[i]),"Signed/sheared columns were renormalized");
  for(int k=0;k<shape.z;++k) for(int v=0;v<shape.y;++v) for(int u=0;u<shape.x;++u) {
    const long double x=v,y=3-2*k,z=9-(static_cast<long double>(origin)+u*static_cast<long double>(reciprocal));
    const auto world=cal.voxel_to_world({double(u),double(v),double(k)});
    near(world[0],11-x+.5625L*y+1.95L*z);near(world[1],-7+.5L*x+2.25L*y);near(world[2],19+1.6875L*y+2.6L*z);
  }
  check(result.first_plane_geometry.origin==cal.origin&&result.first_plane_geometry.horizontal_axis=="Z"&&
        result.first_plane_geometry.vertical_axis=="X","First geometry must be first OUTPUT plane");
  Application app;auto source=annotated(spec);auto other=std::make_shared<Document>(eager(Spec{}),"Other");
  app.adopt(source);app.adopt(other);const auto before=source->snapshot();const auto history=source->history();
  const auto other_before=other->snapshot();const auto other_history=other->history();const auto ids=app.document_ids();
  auto prepared=prepare_axis_range_slicer_resize(before,req);auto candidate=prepared.prepared_new_document();
  check(candidate&&candidate->id!=source->id&&!app.contains(candidate->id)&&app.document_ids()==ids&&app.active_document_id()==other->id,
        "Preparation adopted or retargeted an owner");
  const auto fresh=candidate->snapshot();const auto &data=fresh.data();
  check(!fresh.dirty()&&fresh.data_identity()==0&&candidate->history().undo_steps==0&&candidate->history().redo_steps==0&&
        !data.selection&&!data.previous_selection&&data.rois.empty()&&data.overlays.empty()&&!data.spectrum&&
        data.metadata.properties.empty()&&data.metadata.record_layout.empty()&&data.metadata.extensions.size()==1,
        "Fresh output retained source annotations/history/state");
  const auto tree=std::get<std::shared_ptr<const MetadataTree>>(data.metadata.extensions.at("axis_range_slicer_resize"));
  const auto &provenance=std::get<MetadataObject>(tree->value);
  const std::set<std::string> provenance_keys{
    "schema_version","source_document_id","plane","first","last","step","count","last_selected","t","c",
    "reverse_order","flip_vertical","transpose","horizontal_axis","vertical_axis","fixed_axis","index_convention",
    "source_vertical_count","vertical_count","z_spacing_ratio","processor_vertical_origin","processor_vertical_step",
    "resize_branch","scalar_spacing","geometry_policy","scalar_spacing_policy","sampling","border_policy","quantization_policy"};
  const std::set<std::string> parameter_keys{
    "plane","first","last","step","t","c","reverse_order","flip_vertical","transpose","count","last_selected",
    "effective_first","effective_last","source_vertical_count","vertical_count","z_spacing_ratio","processor_vertical_origin",
    "processor_vertical_step","resize_branch","scalar_spacing","geometry_policy","scalar_spacing_policy","sampling",
    "border_policy","quantization_policy","max_working_bytes","working_bytes","pixel_bytes","object_bytes","sizeof_image",
    "sizeof_shared_image","allocation_allowance_bytes","image_object_bytes","shared_const_image_pointer_bytes","per_plane_control_allowance_bytes"};
  std::set<std::string> actual_provenance_keys,actual_parameter_keys;
  for(const auto &entry:provenance)actual_provenance_keys.insert(entry.first);
  for(const auto &entry:prepared.parameters())actual_parameter_keys.insert(entry.first);
  check(actual_provenance_keys==provenance_keys&&actual_parameter_keys==parameter_keys&&
        std::get<std::uint64_t>(provenance.at("source_document_id"))==source->id&&
        std::get<std::string>(provenance.at("geometry_policy"))=="nominal_unclamped_processor_sampling_affine"&&
        data.metadata.title=="Slicer source XZ 2-5 step 2 ImageJ bilinear [R1 F1 T1]"&&
        prepared.operation()=="axis_range_slicer_resize"&&
        prepared.target().dependencies()==(Change::Pixels|Change::Metadata)&&prepared.target().position_count()==10,
        "Typed provenance/parameter/target contract differs");
  const auto &numeric=std::get<NumericArray>(prepared.parameters().at("scalar_spacing"));
  check(numeric.shape==std::vector<std::size_t>{3}&&numeric.values==std::vector<double>{1,1,2.25},"Scalar metric lost typed dimensions");
  unchanged(*source,before,history);unchanged(*other,other_before,other_history);
  auto display=candidate->snapshot().display();display.zoom=2;candidate->set_display(display);candidate->set_history_limit(7);
  const auto published=app.publish(prepared);
  check(published.created&&published.document==candidate&&published.document_id==candidate->id&&app.active_document_id()==candidate->id,
        "Publication failed to adopt the exact staged owner");
  unchanged(*source,before,history);unchanged(*other,other_before,other_history);
  const auto published_ids=app.document_ids();rejects<std::invalid_argument>([&]{app.publish(prepared);},"Repeat publication admitted");
  check(app.document_ids()==published_ids&&app.active_document_id()==candidate->id,"Repeat rejection changed membership/active owner");
  std::cout<<"slicer.mappings.owned_geometry_tc_fresh_state\n";
}
void no_read_admission_and_caps() {
  for(int scenario=0;scenario<7;++scenario) {
    Spec spec;auto req=request();
    if(scenario==0)spec.type=PixelType::Float64;
    if(scenario==1)spec.shape.s=3;
    if(scenario==2)spec.shape.z=1;
    if(scenario==3)spec.shape.x=1;
    if(scenario==4)spec.cal.spacing={1,1,.5};
    if(scenario==5)req.orientation.range.max_working_bytes=0;
    if(scenario==6)req.orientation.range.plane=Plane::XY;
    auto counter=std::make_shared<CountedSource>(spec);Dataset input(counter,spec.cal);std::size_t callbacks=0;
    ExecutionControl control;control.checkpoint=[&]{++callbacks;};control.progress=[&](std::size_t,std::size_t){++callbacks;};
    control.stage_progress=[&](std::string_view,std::size_t,std::size_t){++callbacks;};
    rejects<std::invalid_argument>([&]{estimate_axis_range_slicer_resize(input,req);},"Invalid metadata estimate admitted");
    rejects<std::invalid_argument>([&]{reslice_slicer_axis_range(input,req,control);},"Invalid metadata processing admitted");
    check(counter->reads()==0&&callbacks==0,"Metadata refusal read pixels or invoked callbacks");
  }
  for(int scenario=0;scenario<4;++scenario) {
    Spec spec;auto counter=std::make_shared<CountedSource>(spec);Dataset input(counter,spec.cal);
    auto req=request();
    if(scenario==0)req.orientation.range.first=-1;
    if(scenario==1)req.orientation.range.last=2;
    if(scenario==2)req.orientation.range.t=1;
    if(scenario==3)req.orientation.range.c=1;
    std::size_t callbacks=0;ExecutionControl control;control.checkpoint=[&]{++callbacks;};
    rejects<std::out_of_range>([&]{reslice_slicer_axis_range(input,req,control);},"Out-of-bounds capture admitted");
    check(counter->reads()==0&&callbacks==0,"Bound refusal read source or invoked callbacks");
  }
  Spec spec;spec.shape={1,1,64,2,32,1};spec.cal.spacing={1,1,.0625};
  auto counter=std::make_shared<CountedSource>(spec);Dataset input(counter,spec.cal);auto req=request();
  const auto estimate=estimate_axis_range_slicer_resize(input,req);
  check(estimate.vertical_count==4&&estimate.working_bytes==bytes(spec,1,4)&&counter->reads()==0,"Independent memory/count estimate differs");
  req.orientation.range.max_working_bytes=estimate.working_bytes-1;
  rejects<std::invalid_argument>([&]{reslice_slicer_axis_range(input,req);},"One-less cap admitted");
  check(counter->reads()==0,"Cap refusal read provider pixels");
  req.orientation.range.max_working_bytes=estimate.working_bytes;
  check(reslice_slicer_axis_range(input,req).working_bytes==estimate.working_bytes,"Equal cap rejected");
  rejects<std::invalid_argument>([&]{reslice_oriented_axis_range(input,req.orientation);},
                                "Fixture must demonstrate downsample fitting below old full-height cap");
  Image quiet(2,2,1,PixelType::Float32);quiet.pixels[0]=float32_source_sample(0x7fc12345u,FloatingSamplePolicy::PreserveQuietNaNFloat32);
  const auto policy=Dataset::from_stack(Stack{quiet,quiet},{},FloatingSamplePolicy::PreserveQuietNaNFloat32);
  rejects<std::invalid_argument>([&]{reslice_slicer_axis_range(policy,request());},"Quiet-capable policy admitted");
  std::cout<<"slicer.validation.metadata_memory_admission\n";
}
void canonical_values_and_controls() {
  for(int scenario=0;scenario<3;++scenario) {
    Spec spec;spec.type=scenario==2?PixelType::Float32:scenario==1?PixelType::UInt16:PixelType::UInt8;
    spec.seed=scenario==2?Seed::NoncanonicalF32:Seed::Fractional;
    auto counter=std::make_shared<CountedSource>(spec);Dataset input(counter,spec.cal);
    check(estimate_axis_range_slicer_resize(input,request()).resize_branch==AxisRangeSlicerResizeBranch::NoResize&&counter->reads()==0,
          "Metadata estimate incorrectly asserted canonicality");
    rejects<std::invalid_argument>([&]{reslice_slicer_axis_range(input,request());},"Noncanonical selected copy value admitted");
    check(counter->reads()>0,"Unknown lazy canonicality was not checked on consumed reads");
  }
  Spec foreign_spec;foreign_spec.seed=Seed::ForeignNoncanonicalF32;
  auto fallback=std::make_shared<FallbackSource>(foreign_spec);Dataset foreign(fallback);
  const auto selected=reslice_slicer_axis_range(foreign,request());
  check(fallback->planes>0&&exact(selected.dataset.plane()->at(1,1),11.),
        "Unselected noncanonical fallback XY contents incorrectly blocked selected canonical values");
  Spec spec;spec.shape={1,1,2,1,4097,1};spec.cal.spacing={1,1,2};
  auto source=annotated(spec);Application app;app.adopt(source);const auto before=source->snapshot();const auto history=source->history();
  const auto ids=app.document_ids();const auto active=app.active_document_id();
  auto token=std::make_shared<CancellationToken>();token->cancel();ExecutionControl control;control.cancellation=token;
  rejects<OperationCancelled>([&]{prepare_axis_range_slicer_resize(before,request(),control);},"Pre-canceled operation returned");
  token=std::make_shared<CancellationToken>();control={};control.cancellation=token;bool partial=false,completed=false;
  control.stage_progress=[&](std::string_view stage,std::size_t done,std::size_t total){
    if(stage=="axis_range_slicer_resize_sample"&&done>0&&done<total){partial=true;token->cancel();}
  };
  control.progress=[&](std::size_t done,std::size_t total){if(done==total)completed=true;};
  rejects<OperationCancelled>([&]{prepare_axis_range_slicer_resize(before,request(),control);},"Blockwise cancellation did not abort");
  check(partial&&!completed,"Cancellation did not exercise a nonfinal block or reported completion");
  control={};control.progress=[](std::size_t,std::size_t){throw std::runtime_error("Literal progress failure");};
  callback_error([&]{prepare_axis_range_slicer_resize(before,request(),control);},"Literal progress failure");
  control={};control.stage_progress=[](std::string_view,std::size_t,std::size_t){throw std::runtime_error("Literal stage failure");};
  callback_error([&]{prepare_axis_range_slicer_resize(before,request(),control);},"Literal stage failure");
  check(app.document_ids()==ids&&app.active_document_id()==active,"Aborted preparation published/retargeted");unchanged(*source,before,history);
  Spec replacement_spec;replacement_spec.type=PixelType::Float64;replacement_spec.shape.x=3;
  const auto replacement=eager(replacement_spec);auto input=eager(Spec{});auto req=request();bool mutated=false;
  control={};control.checkpoint=[&]{mutated=true;input=replacement;req.orientation.range.plane=Plane::XY;req.orientation.range.first=-1;};
  const auto captured=reslice_slicer_axis_range(input,req,control);
  check(mutated&&captured.request.orientation.range.plane==Plane::XZ&&captured.request.orientation.range.first==0&&
        captured.dataset.shape().x==2&&captured.dataset.pixel_type()==PixelType::Float32&&input.shape().x==3,
        "Whole request/source capture did not precede callbacks");
  std::cout<<"slicer.validation.canonicality_cancellation_capture\n";
}
void publication_refusals_and_annotation_edits() {
  for(int scenario=0;scenario<5;++scenario) {
    Spec spec;spec.cal.spacing={1,1,2};Application app;auto source=annotated(spec);auto other=annotated(Spec{});
    app.adopt(source);app.adopt(other);auto token=std::make_shared<CancellationToken>();
    ExecutionControl control;control.cancellation=token;
    auto prepared=prepare_axis_range_slicer_resize(source->snapshot(),request(),control);
    auto candidate=prepared.prepared_new_document();
    if(scenario==0){auto edit=source->begin_edit();edit.replace_plane({},Image(2,2,1,PixelType::Float32));source->commit(edit);}
    if(scenario==1){auto edit=source->begin_edit();auto cal=spec.cal;cal.origin[0]=7;edit.set_calibration(cal);source->commit(edit);}
    if(scenario==2){auto edit=candidate->begin_edit();edit.replace_plane({},Image(2,4,1,PixelType::Float32));candidate->commit(edit);}
    if(scenario==3)app.close(source->id,true);
    if(scenario==4)token->cancel();
    const auto ids=app.document_ids();const auto active=app.active_document_id();const auto source_after_stimulus=source->snapshot();
    const auto history=source->history();
    if(scenario==3)rejects<DocumentUnavailable>([&]{app.publish(prepared);},"Closed source published");
    else if(scenario==4)rejects<OperationCancelled>([&]{app.publish(prepared);},"Canceled prepared output published");
    else rejects<RevisionConflict>([&]{app.publish(prepared);},"Stale/tampered source or candidate published");
    check(app.document_ids()==ids&&app.active_document_id()==active&&!app.contains(candidate->id)&&prepared.active(),
          "Failed publication changed membership/active/shared prepared state");
    unchanged(*source,source_after_stimulus,history);
  }
  Spec spec;Application app;auto source=annotated(spec);app.adopt(source);
  auto prepared=prepare_axis_range_slicer_resize(source->snapshot(),request());
  source->select(Selection("point",{{0,1}}));auto display=source->snapshot().display();display.zoom=2;source->set_display(display);
  const auto before=source->snapshot();const auto history=source->history();
  check(app.publish(prepared).created,"Unrelated selection/display edits incorrectly blocked publication");
  unchanged(*source,before,history);
  std::cout<<"slicer.validation.publication_source_and_candidate_guards\n";
}
}
int main(int argc,char **argv) {
  try {
    check(argc==2,"Choose mappings or validation");const std::string mode=argv[1];
    if(mode=="mappings"){pixel_mappings();exact_copy_branches();geometry_and_fresh_publication();}
    else if(mode=="validation"){no_read_admission_and_caps();canonical_values_and_controls();publication_refusals_and_annotation_edits();}
    else throw std::invalid_argument("Unknown test mode");
    return 0;
  } catch(const std::exception &error){std::cerr<<error.what()<<'\n';return 1;}
}
