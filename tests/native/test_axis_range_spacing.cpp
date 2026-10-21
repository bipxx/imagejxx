#include <imagejxx/application.hpp>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <iostream>
#include <limits>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>

using namespace imagejxx;
namespace {
void check(bool condition, const char *message) {
  if (!condition) throw std::runtime_error(message);
}
template<class Error, class Function> void rejects(Function function, const char *message) {
  bool rejected=false;
  try { function(); } catch (const Error &) { rejected=true; }
  check(rejected,message);
}
bool exact(double a,double b) { return std::memcmp(&a,&b,sizeof(double))==0; }
void near(double a,double b) {
  check(std::abs(a-b)<=1e-12*std::max({1.,std::abs(a),std::abs(b)}),
        "Physical grid world coordinate or basis differs");
}
enum class Seed { Normal, Quiet, FractionalU8, FractionalU16, IntegerCycle,
                  Float32Cycle, Impulse, Overflow };
struct Fixture {
  const char *id;
  DatasetShape shape;
  PixelType type;
  Seed seed;
  AxisRangeSpacingRequest request;
  bool shear;
};
// Literal source fixtures are generated from the separately reviewed workload,
// never from implementation estimates, processed pixels or status fields.
#include "axis_range_spacing_cases.hpp"
SpatialCalibration calibration(bool shear=false) {
  SpatialCalibration value;
  value.spacing={.5,1.25,2}; value.origin={11,-7,19};
  value.direction={-1,.25,shear?1.5:0,.5,1,0,0,.75,2};
  value.unit="um"; value.time_unit="s";
  value.frame_interval=.75; value.time_origin=2.25;
  return value;
}
int mod(int value,int modulus) { return (value%modulus+modulus)%modulus; }
double sample(const Fixture &f,int t,int c,int z,int y,int x,int s) {
  switch (f.seed) {
  case Seed::Quiet: {
    constexpr std::array<std::uint32_t,5> bits{0,0x80000000u,0x7fc12345u,0xffc23456u,0x3f800000u};
    return float32_source_sample(bits[(std::size_t(z)*f.shape.y*f.shape.x+
        std::size_t(y)*f.shape.x+x)%bits.size()],FloatingSamplePolicy::PreserveQuietNaNFloat32);
  }
  case Seed::FractionalU8: return -.25+(x%3)*.5;
  case Seed::FractionalU16: return 65535.75+(x%3)*.5;
  case Seed::IntegerCycle: {
    constexpr std::array<double,9> values{-.25,.75,1.25,65535.75,65536.25,-2,2,3,4};
    return values[z%values.size()];
  }
  case Seed::Float32Cycle: {
    constexpr std::array<double,5> values{0x1.0000000001000p+0,0x1.0000000001000p+0,
                                        0x1.0000100000000p+0,-0.,0.};
    return values[z%values.size()];
  }
  case Seed::Impulse: return z==1?255:0;
  case Seed::Overflow: return z%2?1e308:-1e308;
  case Seed::Normal:
    if (f.type==PixelType::UInt8) return mod(13*x+29*y+47*z+61*t+73*c+19*s,251);
    if (f.type==PixelType::UInt16) return mod(257*x+509*y+1021*z+4093*t+8191*c+137*s,65521);
    if (f.type==PixelType::Float32) return (mod(13*x-29*y+47*z-61*t+73*c+19*s,511)-255)/8.;
    return (mod(13*x-29*y+47*z-61*t+73*c+19*s,1023)-511)/16.;
  }
  throw std::runtime_error("Unknown fixture seed");
}
FloatingSamplePolicy policy(const Fixture &f) {
  return f.seed==Seed::Quiet?FloatingSamplePolicy::PreserveQuietNaNFloat32:FloatingSamplePolicy::FiniteOnly;
}
Image image(const Fixture &f,PlanePosition p,int x=0,int y=0,int width=0,int height=0) {
  Image result(width?width:f.shape.x,height?height:f.shape.y,f.shape.s,f.type);
  for (int v=0;v<result.height;++v) for (int u=0;u<result.width;++u) for (int s=0;s<f.shape.s;++s)
    result.at(u,v,s)=sample(f,p.t,p.c,p.z,y+v,x+u,s);
  return result;
}
Dataset dataset(const Fixture &f) {
  Stack planes;
  for (int t=0;t<f.shape.t;++t) for (int c=0;c<f.shape.c;++c) for (int z=0;z<f.shape.z;++z)
    planes.push_back(image(f,{t,c,z}));
  return Dataset::from_planes(f.shape,std::move(planes),calibration(f.shear),policy(f));
}
class CountedSource final : public PlaneSource {
public:
  explicit CountedSource(Fixture fixture):f(std::move(fixture)) {}
  Fixture f;
  mutable std::size_t plane_reads=0,region_reads=0;
  DatasetShape shape() const override {return f.shape;}
  PixelType pixel_type() const override {return f.type;}
  std::shared_ptr<const Image> read_plane(PlanePosition p) const override {
    ++plane_reads; return std::make_shared<const Image>(image(f,p));
  }
  Image read_region(PlanePosition p,int x,int y,int width,int height) const override {
    ++region_reads; return image(f,p,x,y,width,height);
  }
};
std::size_t expected_bytes(const Fixture &f,int k,int m) {
  const auto a=std::size_t(f.request.orientation.range.plane==Plane::XZ?f.shape.x:f.shape.y);
  const auto s=std::size_t(f.shape.s);
  return sizeof(double)*(std::size_t(k)*a*m*s+a*f.shape.z*s+
      std::size_t(f.shape.x)*f.shape.y*s+a*s)+
      std::size_t(k)*(2*sizeof(Image)+sizeof(std::shared_ptr<const Image>)+256)+2*sizeof(Image)+256;
}
// Independent one-dimensional stencil. All reviewed steps are dyadic, so
// extended precision supplies exact expectations without copying the kernel's
// volatile subtraction/multiplication/addition implementation.
double expected_sample(const Fixture &f,int a,double z,int fixed,int component) {
  const auto &r=f.request.orientation.range;
  const int x=r.plane==Plane::XZ?a:fixed, y=r.plane==Plane::XZ?fixed:a;
  if (f.request.sampling==AxisRangeSampling::Nearest)
    return sample(f,r.t,r.c,std::clamp(int(std::floor(z+.5)),0,f.shape.z-1),y,x,component);
  const int lower=int(std::floor(z)), upper=std::min(lower+1,f.shape.z-1);
  const double left=sample(f,r.t,r.c,lower,y,x,component);
  const double right=sample(f,r.t,r.c,upper,y,x,component);
  if (z==lower || lower==upper || left==right) return left;
  const long double weight=static_cast<long double>(z)-lower;
  const long double mixed=(1-weight)*static_cast<long double>(left)+weight*static_cast<long double>(right);
  if (f.type==PixelType::UInt8 || f.type==PixelType::UInt16)
    return double(std::floor(std::clamp(mixed,0.L,f.type==PixelType::UInt8?255.L:65535.L)+.5L));
  if (f.type==PixelType::Float32) return double(static_cast<float>(mixed));
  return double(mixed);
}
void compare(const Fixture &f,const AxisRangeSpacingResult &result) {
  const auto &o=f.request.orientation; const auto &r=o.range;
  const int a=r.plane==Plane::XZ?f.shape.x:f.shape.y;
  const double length=f.shear?5:4, delta=f.request.physical_vertical_spacing/length;
  const int k=1+(r.last-r.first)/r.step, m=1+int(std::floor((f.shape.z-1)/delta));
  const int last=r.first+(k-1)*r.step;
  const auto actual=result.dataset.shape();
  check(actual.dimensions()==std::array<int,6>{1,1,k,o.transpose?a:m,o.transpose?m:a,f.shape.s},
        "Physical grid output shape differs");
  check(result.dataset.pixel_type()==f.type && result.dataset.sample_policy()==policy(f) &&
        result.vertical_count==m && result.source_vertical_spacing==length && result.voxel_step==delta &&
        result.working_bytes==expected_bytes(f,k,m),"Physical grid result metadata differs");
  const auto &captured=result.request;
  check(captured.physical_vertical_spacing==f.request.physical_vertical_spacing && captured.sampling==f.request.sampling &&
        captured.orientation.range.t==r.t && captured.orientation.range.c==r.c &&
        captured.orientation.reverse_order==o.reverse_order && captured.orientation.flip_vertical==o.flip_vertical &&
        captured.orientation.transpose==o.transpose,"Captured physical request differs");
  const auto &cal=result.dataset.calibration();
  check(cal.unit=="um" && cal.time_unit=="s" && cal.frame_interval==.75 && cal.time_origin==2.25+.75*r.t,
        "Physical grid lost selected time or units");
  const std::array<std::array<double,3>,3> basis{{{-.5,.25,0},{.3125,1.25,.9375},{f.shear?3.:0.,0,4}}};
  const int horizontal=r.plane==Plane::XZ?0:1, fixed_axis=r.plane==Plane::XZ?1:0;
  for (int n=0;n<k;++n) {
    const auto output=result.dataset.plane({0,0,n});
    const int fixed=o.reverse_order?last-n*r.step:r.first+n*r.step;
    for (int j=0;j<m;++j) for (int u=0;u<a;++u) {
      const double distance=std::clamp(j*delta,0.,double(f.shape.z-1));
      const double z=o.flip_vertical?f.shape.z-1-distance:distance;
      const int ox=o.transpose?j:u, oy=o.transpose?u:j;
      for (int s=0;s<f.shape.s;++s)
        check(exact(output->at(ox,oy,s),expected_sample(f,u,z,fixed,s)),
              "Physical sampler changed copied bits or interpolation/quantization");
      const std::array<double,3> point{r.plane==Plane::XZ?double(u):double(fixed),
          r.plane==Plane::XZ?double(fixed):double(u),z};
      const auto world=cal.voxel_to_world({double(ox),double(oy),double(n)});
      for (int row=0;row<3;++row)
        near(world[row],std::array<double,3>{11,-7,19}[row]+basis[0][row]*point[0]+
            basis[1][row]*point[1]+basis[2][row]*point[2]);
    }
  }
  check(result.first_plane_geometry.origin==cal.origin,"Geometry is not the first OUTPUT plane");
  const std::array<std::string,3> labels{"X","Y","Z"};
  check(result.first_plane_geometry.horizontal_axis==labels[o.transpose?2:horizontal] &&
        result.first_plane_geometry.vertical_axis==labels[o.transpose?horizontal:2],"Output plane axis labels differ");
  for (int column=0;column<3;++column) {
    const int source_axis=column==2?fixed_axis:(o.transpose?(column==0?2:horizontal):(column==0?horizontal:2));
    const double scale=column==2?(o.reverse_order?-r.step:r.step):source_axis==2?(o.flip_vertical?-delta:delta):1;
    for (int row=0;row<3;++row) {
      near(cal.direction[row*3+column]*cal.spacing[column],basis[source_axis][row]*scale);
      if (column<2) near(column==0?result.first_plane_geometry.horizontal_step[row]:
          result.first_plane_geometry.vertical_step[row],basis[source_axis][row]*scale);
    }
  }
}
std::shared_ptr<Document> owner(const Fixture &f) {
  DocumentData data{dataset(f),{"Physical source",{{"retained",true}}},{},{}};
  data.selection=Selection("point",{{1,0}}); data.previous_selection=Selection("point",{{0,0}});
  data.rois={{"saved",Selection("point",{{0,0}}),0,0,0}};
  data.overlays={{{"marker",true}}}; data.metadata.extensions["opaque"]=true;
  data.metadata.record_layout={{"old",true}};
  data.spectrum=FourierState(Spectrum{f.shape.x,f.shape.y,
      std::vector<std::complex<double>>(std::size_t(f.shape.x)*f.shape.y)},
      std::array<int,2>{f.shape.y,f.shape.x});
  auto result=std::make_shared<Document>(std::move(data));
  auto edit=result->begin_edit(); edit.set_property("history",true); result->commit(edit); result->undo();
  return result;
}
void unchanged(const Document &source,const DocumentSnapshot &before,const HistoryInfo &history) {
  const auto after=source.snapshot();
  check(after.same_document(before) && after.shares_data(before) && after.data_identity()==before.data_identity() &&
        after.revisions().matches(before.revisions(),Change::All) && after.dirty()==before.dirty() &&
        source.history().undo_steps==history.undo_steps && source.history().redo_steps==history.redo_steps,
        "Physical operation changed existing owner/data/state/history");
}
void fresh_state(const Fixture &f,const PreparedOperation &prepared,std::uint64_t source_id) {
  const auto fresh=prepared.prepared_new_document(); const auto snapshot=fresh->snapshot(); const auto &d=snapshot.data();
  check(prepared.operation()=="axis_range_spacing" && prepared.target().dependencies()==(Change::Pixels|Change::Metadata) &&
        fresh->id!=source_id && !snapshot.dirty() && snapshot.data_identity()==0 && fresh->history().undo_steps==0 &&
        fresh->history().redo_steps==0 && !d.selection && !d.previous_selection && d.rois.empty() && d.overlays.empty() &&
        !d.spectrum && d.metadata.properties.empty() && d.metadata.record_layout.empty() && d.metadata.extensions.size()==1,
        "Fresh physical owner retained old document state");
  const auto tree=std::get<std::shared_ptr<const MetadataTree>>(d.metadata.extensions.at("axis_range_spacing"));
  const auto &p=std::get<MetadataObject>(tree->value);
  const std::set<std::string> expected{"schema_version","source_document_id","plane","first","last","step","count",
      "last_selected","t","c","reverse_order","flip_vertical","transpose","horizontal_axis","vertical_axis",
      "fixed_axis","index_convention","physical_vertical_spacing","source_vertical_spacing","voxel_step",
      "vertical_count","sampling","grid_policy","border_policy","quantization_policy"};
  std::set<std::string> actual; for (const auto &row:p) actual.insert(row.first);
  const auto &o=f.request.orientation; const auto &r=o.range;
  const int k=1+(r.last-r.first)/r.step, last=r.first+(k-1)*r.step;
  const double length=f.shear?5:4, delta=f.request.physical_vertical_spacing/length;
  const int m=1+int(std::floor((f.shape.z-1)/delta));
  const auto a=r.plane==Plane::XZ?"X":"Y", fixed=r.plane==Plane::XZ?"Y":"X";
  const auto plane=r.plane==Plane::XZ?"XZ":"YZ", sampling=f.request.sampling==AxisRangeSampling::Nearest?"nearest":"linear";
  const MetadataObject values{
    {"schema_version",std::int64_t(1)},{"source_document_id",source_id},{"plane",std::string(plane)},
    {"first",std::int64_t(r.first)},{"last",std::int64_t(r.last)},{"step",std::int64_t(r.step)},
    {"count",std::int64_t(k)},{"last_selected",std::int64_t(last)},{"t",std::int64_t(r.t)},{"c",std::int64_t(r.c)},
    {"reverse_order",o.reverse_order},{"flip_vertical",o.flip_vertical},{"transpose",o.transpose},
    {"horizontal_axis",std::string(o.transpose?"Z":a)},{"vertical_axis",std::string(o.transpose?a:"Z")},
    {"fixed_axis",std::string(fixed)},{"index_convention",std::string("zero_based_voxel_centers")},
    {"physical_vertical_spacing",f.request.physical_vertical_spacing},{"source_vertical_spacing",length},
    {"voxel_step",delta},{"vertical_count",std::int64_t(m)},{"sampling",std::string(sampling)},
    {"grid_policy",std::string("source_endpoint_centers_physical_z")},
    {"border_policy",std::string("clamp_endpoint_roundoff")},
    {"quantization_policy",std::string("stored_double_copy_else_declared_type_linear")}};
  check(actual==expected && metadata_equal(p,values),"Typed physical provenance differs");
  std::ostringstream spacing; spacing<<f.request.physical_vertical_spacing;
  const auto title=std::string("Physical source ")+plane+" "+std::to_string(r.first+1)+"-"+std::to_string(r.last+1)+
      " step "+std::to_string(r.step)+" Z spacing "+spacing.str()+" um "+sampling+
      " [R"+std::to_string(int(o.reverse_order))+" F"+std::to_string(int(o.flip_vertical))+" T"+std::to_string(int(o.transpose))+"]";
  check(d.metadata.title==title,"Generated physical title differs");
  check(std::get<std::uint64_t>(prepared.parameters().at("sizeof_image"))==sizeof(Image) &&
        std::get<std::uint64_t>(prepared.parameters().at("sizeof_shared_image"))==sizeof(std::shared_ptr<const Image>) &&
        std::get<std::uint64_t>(prepared.parameters().at("allocation_allowance_bytes"))==256,
        "Measured memory parameters differ");
  check(std::get<std::uint64_t>(prepared.parameters().at("working_bytes"))==expected_bytes(f,k,m) &&
        std::get<std::uint64_t>(prepared.parameters().at("pixel_bytes"))+
          std::get<std::uint64_t>(prepared.parameters().at("object_bytes"))==expected_bytes(f,k,m),
        "Measured pixel/object memory split differs");
}
void mappings() {
  std::size_t count=0;
  for (const auto &f:fixtures) {
    if (std::string(f.id).find("spacing.reject.")==0) continue;
    auto counter=std::make_shared<CountedSource>(f);
    // Quiet NaNs are admitted only by reviewed eager storage; a custom lazy
    // provider cannot widen that global policy. Counted reads cover finite rows.
    const Dataset lazy=f.seed==Seed::Quiet?dataset(f):Dataset(counter,calibration(f.shear));
    const auto estimate=estimate_axis_range_spacing(lazy,f.request);
    check(counter->plane_reads==0 && counter->region_reads==0,"Metadata estimate read provider pixels");
    const auto result=reslice_spaced_axis_range(lazy,f.request); compare(f,result);
    const auto shape=result.dataset.shape();
    check(estimate.width==shape.x && estimate.height==shape.y && estimate.plane_count==shape.z &&
          estimate.samples==shape.s && estimate.vertical_count==result.vertical_count &&
          estimate.voxel_step==result.voxel_step && estimate.working_bytes==result.working_bytes,
          "Metadata estimate disagrees with actual result");
    Application app; const auto source=owner(f); app.adopt(source);
    auto other=std::make_shared<Document>(Dataset::from_stack(Stack{Image(2,2)}),"Other"); app.adopt(other);
    const auto before=source->snapshot(), other_before=other->snapshot();
    const auto history=source->history(), other_history=other->history();
    const auto ids=app.document_ids(); const auto active=app.active_document_id();
    auto prepared=app.prepare_axis_range_spacing(f.request,source->id);
    fresh_state(f,prepared,source->id);
    auto candidate=prepared.prepared_new_document();
    auto staged=result; staged.dataset=candidate->snapshot().data().dataset; compare(f,staged);
    check(!app.contains(candidate->id) && app.document_ids()==ids && app.active_document_id()==active,
          "Preparation registered or activated the candidate");
    unchanged(*source,before,history); unchanged(*other,other_before,other_history);
    const auto published=app.publish(prepared);
    check(published.created && published.document==candidate && !prepared.active() && app.contains(candidate->id) &&
          app.active_document_id()==candidate->id,"Publication did not attach the exact candidate once");
    unchanged(*source,before,history); unchanged(*other,other_before,other_history);
    std::cout<<f.id<<'\n'; ++count;
  }
  check(count==122,"Physical successful workload denominator differs");
}
void refusals() {
  std::size_t count=0;
  for (const auto &f:fixtures) {
    const std::string id=f.id;
    if (id.find("spacing.reject.")!=0) continue;
    auto request=f.request;
    auto counter=std::make_shared<CountedSource>(f);
    const Dataset lazy=f.seed==Seed::Quiet?dataset(f):Dataset(counter,calibration(f.shear));
    const auto suffix=id.substr(std::string("spacing.reject.").size());
    const bool admission=suffix=="xy_plane" || suffix=="invalid_sampling_enum" || suffix=="zero_spacing" ||
      suffix=="negative_spacing" || suffix=="nan_spacing" || suffix=="infinite_spacing" ||
      suffix=="voxel_step_underflow" || suffix=="vertical_count_limit" || suffix=="memory_cap_minus_one" ||
      suffix=="linear_quiet_policy_admission";
    if (admission) {
      if (suffix=="xy_plane") request.orientation.range.plane=Plane::XY;
      else if (suffix=="invalid_sampling_enum") request.sampling=static_cast<AxisRangeSampling>(91);
      else if (suffix=="zero_spacing") request.physical_vertical_spacing=0;
      else if (suffix=="negative_spacing") request.physical_vertical_spacing=-1;
      else if (suffix=="nan_spacing") request.physical_vertical_spacing=std::numeric_limits<double>::quiet_NaN();
      else if (suffix=="infinite_spacing") request.physical_vertical_spacing=std::numeric_limits<double>::infinity();
      else if (suffix=="voxel_step_underflow") request.physical_vertical_spacing=std::numeric_limits<double>::denorm_min();
      else if (suffix=="vertical_count_limit") request.physical_vertical_spacing=0x1p-40;
      else if (suffix=="memory_cap_minus_one") {
        const int k=1+(request.orientation.range.last-request.orientation.range.first)/request.orientation.range.step;
        request.orientation.range.max_working_bytes=expected_bytes(f,k,17)-1;
      }
      std::size_t progress=0,checkpoints=0;
      ExecutionControl control; control.progress=[&](std::size_t,std::size_t){++progress;};
      control.checkpoint=[&]{++checkpoints;};
      rejects<std::invalid_argument>([&]{estimate_axis_range_spacing(lazy,request);},"Invalid estimate was admitted");
      rejects<std::invalid_argument>([&]{reslice_spaced_axis_range(lazy,request,control);},"Invalid spacing was processed");
      check(counter->plane_reads==0 && counter->region_reads==0 && progress==0 && checkpoints==0,
            "Admission refusal read pixels or invoked callbacks");
      if (suffix=="memory_cap_minus_one") {
        ++request.orientation.range.max_working_bytes;
        const auto result=reslice_spaced_axis_range(lazy,request);
        check(result.working_bytes==request.orientation.range.max_working_bytes,"Exact memory cap did not succeed");
      }
    } else {
      Application app; auto source=owner(f); app.adopt(source);
      const auto before=source->snapshot(); const auto history=source->history();
      const auto initial_ids=app.document_ids(); const auto initial_active=app.active_document_id();
      if (suffix.find("at_publish")!=std::string::npos || suffix=="repeat_publish_after_success") {
        auto prepared=app.prepare_axis_range_spacing(request,source->id);
        auto fresh=prepared.prepared_new_document();
        if (suffix=="source_stale_pixels_at_publish") {
          auto edit=source->begin_edit(); edit.replace_plane({1,4,0},Image(f.shape.x,f.shape.y,1,f.type)); source->commit(edit);
        } else if (suffix=="source_stale_calibration_at_publish") {
          auto edit=source->begin_edit(); auto cal=calibration(); cal.spacing[0]=.75; edit.set_calibration(cal); source->commit(edit);
        } else if (suffix=="source_closed_at_publish") app.close(source->id,true);
        else if (suffix=="candidate_pixels_tamper_at_publish") {
          auto edit=fresh->begin_edit(); const auto shape=fresh->snapshot().data().dataset.shape();
          edit.replace_plane({},Image(shape.x,shape.y,shape.s,f.type)); fresh->commit(edit);
        } else if (suffix=="repeat_publish_after_success") app.publish(prepared);
        else throw std::runtime_error("Unknown publication fixture");
        const auto ids=app.document_ids(); const auto active=app.active_document_id();
        const auto preserved=source->snapshot(); const auto preserved_history=source->history();
        if (suffix=="source_closed_at_publish") rejects<DocumentUnavailable>([&]{app.publish(prepared);},"Closed source was published");
        else if (suffix=="repeat_publish_after_success") rejects<std::invalid_argument>([&]{app.publish(prepared);},"Prepared result was published twice");
        else rejects<RevisionConflict>([&]{app.publish(prepared);},"Stale or tampered physical result was published");
        check(app.document_ids()==ids && app.active_document_id()==active,"Rejected publication changed app membership");
        unchanged(*source,preserved,preserved_history);
      } else {
        auto token=std::make_shared<CancellationToken>(); ExecutionControl control; control.cancellation=token;
        bool stimulated=false,completed=false; std::size_t previous=0;
        if (suffix=="pre_canceled") {token->cancel(); stimulated=true;}
        control.progress=[&](std::size_t done,std::size_t total) {
          check(done>=previous && done<=total,"Spacing progress is not monotonic"); previous=done;
          if (done==total) completed=true;
          if (suffix=="progress_callback_exception") {stimulated=true;throw std::runtime_error("Owned progress failure");}
        };
        control.stage_progress=[&](std::string_view stage,std::size_t done,std::size_t total) {
          if (stage=="axis_range_spacing_sample" && done>0 && done<total) {
            if (suffix=="interior_resampling_cancel") {stimulated=true;token->cancel();}
          }
          if (stage=="axis_range_spacing_sample" && done>0 && suffix=="stage_callback_exception") {
            stimulated=true;throw std::runtime_error("Owned stage failure");
          }
        };
        if (suffix=="pre_canceled" || suffix=="interior_resampling_cancel")
          rejects<OperationCancelled>([&]{app.prepare_axis_range_spacing(request,source->id,control);},"Spacing cancellation failed");
        else if (suffix=="interpolation_arithmetic_overflow")
          rejects<std::invalid_argument>([&]{app.prepare_axis_range_spacing(request,source->id,control);},"Nonfinite linear arithmetic was admitted");
        else rejects<std::runtime_error>([&]{app.prepare_axis_range_spacing(request,source->id,control);},"Spacing callback failure was swallowed");
        check(!completed && (stimulated || suffix=="interpolation_arithmetic_overflow"),
              "Processing refusal lacked actual stimulus or reported completion");
        unchanged(*source,before,history);
        check(app.document_ids()==initial_ids && app.active_document_id()==initial_active,
              "Processing refusal published a partial result");
      }
    }
    std::cout<<f.id<<'\n'; ++count;
  }
  check(count==20,"Physical refusal workload denominator differs");
}
void supplemental_capture_and_final_validation() {
  const auto &f=fixtures[0]; Dataset input=dataset(f); const auto retained=input;
  const auto replacement=Dataset::from_stack(Stack{Image(2,2)});
  auto request=f.request; const auto captured=request; bool mutated=false;
  ExecutionControl control; control.checkpoint=[&]{mutated=true; input=replacement;
    request.orientation.range={Plane::XY,-1,-1,0}; request.physical_vertical_spacing=-1;};
  compare(f,reslice_spaced_axis_range(input,request,control));
  check(mutated && retained.shape().t==2 && captured.orientation.range.t==1,"Captured inputs were not exercised");
  auto large=fixtures[132]; large.shape.x=4097;
  Application app; auto source=owner(large); app.adopt(source);
  const auto before=source->snapshot(); const auto history=source->history();
  const auto ids=app.document_ids(); const auto active=app.active_document_id();
  auto token=std::make_shared<CancellationToken>(); std::size_t copies=0; bool canceled=false,completed=false;
  control={}; control.cancellation=token;
  const int k=1+(large.request.orientation.range.last-large.request.orientation.range.first)/large.request.orientation.range.step;
  control.stage_progress=[&](std::string_view stage,std::size_t done,std::size_t total) {
    if (stage=="axis_range_spacing_sample" && done==total) ++copies;
    if (stage=="Immutable plane sample validation" && copies==std::size_t(k) && done>=1024) {
      canceled=true;token->cancel();
    }
  };
  control.progress=[&](std::size_t done,std::size_t total){if(done==total)completed=true;};
  rejects<OperationCancelled>([&]{app.prepare_axis_range_spacing(large.request,source->id,control);},
                              "Final immutable validation cancellation failed");
  check(canceled && !completed && app.document_ids()==ids && app.active_document_id()==active,
        "Final validation cancellation reported completion or published output");
  unchanged(*source,before,history);
  std::cout<<"spacing.supplemental.captured_inputs_and_final_validation_cancel\n";
}
}
int main(int argc,char **argv) {
  try {
    check(argc==2,"Choose mappings or validation");
    const std::string mode=argv[1];
    if (mode=="mappings") mappings();
    else if (mode=="validation") {refusals();supplemental_capture_and_final_validation();}
    else throw std::invalid_argument("Unknown test mode");
    return 0;
  } catch (const std::exception &error) {std::cerr<<error.what()<<'\n';return 1;}
}
