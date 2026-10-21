// Independent native host assertions for P06e. Expected samples and affine
// landmarks come from the literal fixture, never a product extractor/record.
#include "window.hpp"
#include "axis_plane_ui_probe.hpp"
#include "axis_range_ui_probe.hpp"
#include "gaussian_cancellation_internal.hpp"
#include "../../src/native/operation_job_probe_internal.hpp"
#include <imagejxx/axis_extraction_command.hpp>

#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <complex>
#include <cstring>
#include <exception>
#include <filesystem>
#include <iostream>
#include <limits>
#include <memory>
#include <mutex>
#include <new>
#include <stdexcept>
#include <string>
#include <thread>
#include <type_traits>
#include <vector>

namespace {
using namespace imagejxx;
namespace desktop = imagejxx::desktop;
namespace fs = std::filesystem;
using desktop::GaussianCancellationAccess;
using desktop::Json;
using desktop::Window;
using namespace std::chrono_literals;
std::size_t assertions = 0;
void check(bool value, const char *message) {
  ++assertions;
  if (!value) throw std::runtime_error(message);
}
template <class Function> void rejects(Function function, const char *message) {
  bool rejected = false;
  try { function(); }
  catch (const std::bad_alloc &) { throw; }
  catch (const std::exception &) { rejected = true; }
  check(rejected, message);
}
bool same_display(const DisplayState &a, const DisplayState &b) {
  return a.t == b.t && a.c == b.c && a.crosshair == b.crosshair &&
      a.orthogonal == b.orthogonal && a.linkedZoom == b.linkedZoom &&
      a.zoom == b.zoom && a.planeZoom == b.planeZoom && a.pan == b.pan &&
      a.revision == b.revision && metadata_equal(a.properties, b.properties);
}
SpatialCalibration calibration() {
  SpatialCalibration c;
  c.spacing = {.25, 1.5, 3}; c.origin = {-2, 4, 9};
  c.direction = {-1, .5, .25, 0, 1, .5, .5, 0, 1};
  c.unit = "um"; c.time_unit = "min";
  c.frame_interval = 2.5; c.time_origin = -3;
  return c;
}
double value(int t, int c, int z, int y, int x, int s) {
  return 64*t + 24*c + 8*z + 3*y + x + 2*s;
}
constexpr std::array<std::uint32_t, 6> bit_values{
    0, 0x80000000u, 1, 0x7fc12345u, 0xffc34567u, 0x3f800000u};
std::uint32_t expected_bits(int t, int c, int z, int y, int x) {
  return bit_values[std::size_t((((t*3+c)*4+z)*5+y)*7+x) % bit_values.size()];
}
std::shared_ptr<Document> make_owner(PixelType type = PixelType::Float32,
                                    int samples = 1, bool bit_pattern = false,
                                    bool singleton_positions = false) {
  const int times = singleton_positions ? 1 : 2;
  const int channels = singleton_positions ? 1 : 3;
  Stack planes;
  for (int t=0;t<times;++t) for (int c=0;c<channels;++c) for (int z=0;z<4;++z) {
    Image image(7,5,samples,type);
    for (int y=0;y<5;++y) for (int x=0;x<7;++x) for (int s=0;s<samples;++s)
      image.at(x,y,s) = bit_pattern
          ? float32_source_sample(expected_bits(t,c,z,y,x), FloatingSamplePolicy::PreserveQuietNaNFloat32)
          : value(t,c,z,y,x,s);
    planes.push_back(std::move(image));
  }
  const auto policy = bit_pattern ? FloatingSamplePolicy::PreserveQuietNaNFloat32
                                  : FloatingSamplePolicy::FiniteOnly;
  DocumentData data{Dataset::from_planes({times,channels,4,5,7,samples},std::move(planes),calibration(),policy)};
  data.metadata.title = "P06e scientific source";
  data.metadata.properties["vendor"] = metadata_object({{"unsigned",std::numeric_limits<std::uint64_t>::max()},
                                                        {"array",metadata_array({true,1,std::string("keep")})}});
  data.metadata.extensions["opaque"] = metadata_object({{"version",37},{"payload",std::string("kept")}});
  data.metadata.record_layout = {{"presence",true}};
  data.selection = Selection("rectangle",{{1,1},{4,4}});
  data.previous_selection = Selection("line",{{0,0},{5,3}});
  data.rois = {{"zero",Selection("point",{{2,3}}),0,0,0,{}},
               {"wildcard",Selection("rectangle",{{0,0},{3,2}}),{},{},{},{}}};
  data.overlays = {{{"vendor",std::string("source overlay")}}};
  DisplayState display;
  display.t = times-1; display.c = channels-1; display.crosshair = {4,3,2};
  display.orthogonal = true; display.linkedZoom = false;
  display.zoom = 2.5; display.planeZoom = {1.25,2,3};
  display.pan = {{{3,-4},{-.5,.25},{0,-2}}};
  display.properties = {{"lut",std::string("Ice")}};
  auto owner = std::make_shared<Document>(std::move(data),display);
  auto history = owner->begin_edit(); history.set_property("history probe",true);
  owner->commit(history); owner->undo(); owner->mark_saved();
  return owner;
}
std::shared_ptr<Document> make_eager() {
  const auto model=make_owner(PixelType::UInt16,1,false,true)->snapshot();
  auto data=model.data();
  for(auto &roi:data.rois){roi.t.reset();roi.c.reset();}
  auto owner=std::make_shared<Document>(std::move(data),model.display());
  owner->mark_saved();return owner;
}
std::array<int,3> axes(Plane plane) {
  if (plane == Plane::XY) return {0,1,2};
  if (plane == Plane::XZ) return {0,2,1};
  return {1,2,0};
}
const char *name(Plane plane) {
  return plane == Plane::XY ? "XY" : plane == Plane::XZ ? "XZ" : "YZ";
}
AxisExtractionRequest request(bool range, Plane plane, int t=1, int c=2,
                              std::size_t cap=1024*1024) {
  if (!range) return AxisPlaneExportRequest{plane,2,t,c,cap};
  return AxisRangeResliceRequest{plane,1,plane == Plane::XY ? 3 : plane == Plane::XZ ? 4 : 6,2,t,c,cap};
}
Plane requested_plane(const AxisExtractionRequest &r) { return std::visit([](auto v){return v.plane;},r); }
int requested_t(const AxisExtractionRequest &r) { return std::visit([](auto v){return v.t;},r); }
int requested_c(const AxisExtractionRequest &r) { return std::visit([](auto v){return v.c;},r); }
int first(const AxisExtractionRequest &r) {
  if (const auto *p = std::get_if<AxisPlaneExportRequest>(&r)) return p->coordinate;
  return std::get<AxisRangeResliceRequest>(r).first;
}
int step(const AxisExtractionRequest &r) {
  if (const auto *p = std::get_if<AxisRangeResliceRequest>(&r)) return p->step;
  return 1;
}
int count(const AxisExtractionRequest &r) {
  if (const auto *p = std::get_if<AxisRangeResliceRequest>(&r)) return 1+(p->last-p->first)/p->step;
  return 1;
}
std::uint64_t extract(Window &window,const DocumentSnapshot &source,const AxisExtractionRequest &r) {
  if (const auto *p = std::get_if<AxisPlaneExportRequest>(&r)) return window.exportAxisPlane(source,*p);
  return window.resliceAxisRange(source,std::get<AxisRangeResliceRequest>(r));
}
Json explicit_request_json(const AxisExtractionRequest &r) {
  return std::visit([](const auto &q) {
    Json value={{"plane",name(q.plane)},{"t",q.t},{"c",q.c},{"max_working_bytes",q.max_working_bytes}};
    if constexpr(std::is_same_v<std::decay_t<decltype(q)>,AxisPlaneExportRequest>) {
      value["operation"]="axis_plane_export";value["coordinate"]=q.coordinate;
    } else {
      value["operation"]="axis_range_reslice";value["first"]=q.first;value["last"]=q.last;value["step"]=q.step;
    }
    return value;
  },r);
}
std::array<double,3> world(const SpatialCalibration &c,std::array<double,3> xyz) {
  auto out=c.origin;
  for (int row=0;row<3;++row) for (int column=0;column<3;++column)
    out[row] += c.direction[row*3+column]*c.spacing[column]*xyz[column];
  return out;
}
MetadataObject object(const MetadataValue &v) {
  const auto *node=std::get_if<std::shared_ptr<const MetadataTree>>(&v);
  check(node && *node,"Expected provenance object node");
  const auto *out=std::get_if<MetadataObject>(&(*node)->value);
  check(out != nullptr,"Expected provenance object rather than array");
  return *out;
}
std::uint64_t integer(const MetadataValue &v) {
  if (const auto *u=std::get_if<std::uint64_t>(&v)) return *u;
  const auto *i=std::get_if<std::int64_t>(&v);
  check(i && *i>=0,"Expected exact nonnegative provenance integer");
  return std::uint64_t(*i);
}
void geometry(const Dataset &output,const SpatialCalibration &c,const AxisExtractionRequest &r,int samples) {
  const auto order=axes(requested_plane(r));
  const std::array<int,3> extent{7,5,4};
  check(output.shape().dimensions() == std::array<int,6>{1,1,count(r),extent[order[1]],extent[order[0]],samples},
        "Output TCZYXS shape did not preserve source axis/sample correspondence");
  const auto &actual=output.calibration();
  std::array<double,3> base{}; base[order[2]]=first(r);
  check(actual.origin==world(c,base),"Output origin differs from the independent selected-source landmark");
  for (int column=0;column<3;++column) {
    check(actual.spacing[column]==c.spacing[order[column]]*(column==2?step(r):1),"Output spacing/stride differs");
    for (int row=0;row<3;++row)
      check(actual.direction[row*3+column]==c.direction[row*3+order[column]],"Output full affine direction differs");
  }
  check(actual.unit==c.unit && actual.time_unit==c.time_unit && actual.frame_interval==c.frame_interval &&
        actual.time_origin==c.time_origin+requested_t(r)*c.frame_interval,"Output captured time calibration differs");
  for (int k : {0,count(r)-1}) {
    const std::array<double,3> out{double(extent[order[0]]-1),double(extent[order[1]]-1),double(k)};
    std::array<double,3> original{};
    original[order[0]]=out[0]; original[order[1]]=out[1]; original[order[2]]=first(r)+k*step(r);
    check(world(actual,out)==world(c,original),"Nonzero output world landmark differs from source affine");
  }
}
void pixels(const Dataset &output,const AxisExtractionRequest &r,int samples,PixelType type,bool bits=false) {
  check(output.pixel_type()==type,"Extraction changed native dtype");
  const auto policy=bits?FloatingSamplePolicy::PreserveQuietNaNFloat32:FloatingSamplePolicy::FiniteOnly;
  check(output.sample_policy()==policy,"Extraction changed explicit floating sample policy");
  const auto order=axes(requested_plane(r));
  for (int k=0;k<count(r);++k) {
    const auto image=output.plane({0,0,k});
    for (int y=0;y<image->height;++y) for (int x=0;x<image->width;++x) {
      std::array<int,3> v{};
      v[order[0]]=x;v[order[1]]=y;v[order[2]]=first(r)+k*step(r);
      for (int s=0;s<samples;++s) {
        if (bits) check(float32_source_sample_bits(image->at(x,y,s),policy)==
                       expected_bits(requested_t(r),requested_c(r),v[2],v[1],v[0]),
                       "Exact extraction changed Float32 payload/subnormal/signed-zero bits");
        else check(image->at(x,y,s)==value(requested_t(r),requested_c(r),v[2],v[1],v[0],s),
                   "Output samples differ from literal current-T/C axis indexing");
      }
    }
  }
}
void unchanged(const std::shared_ptr<Document> &owner,const DocumentSnapshot &before,const HistoryInfo &history) {
  const auto after=owner->snapshot();
  check(after.same_document(before) && after.shares_data(before) && after.data_identity()==before.data_identity() &&
        after.revisions().matches(before.revisions(),Change::All) && after.dirty()==before.dirty() &&
        same_display(after.display(),before.display()) && owner->undo_steps()==history.undo_steps && owner->redo_steps()==history.redo_steps,
        "Slicing changed source data/calibration/annotations/history/save identity/cameras");
  const auto old=before.data().dataset.shared_planes(), now=after.data().dataset.shared_planes();
  check(old && now && old->size()==now->size(),"Slicing changed source eager plane storage");
  for (std::size_t i=0;i<old->size();++i)
    check(old->shared(i)==now->shared(i),"Slicing replaced an original immutable source plane");
}
struct Collection {
  Json results,workflow,preferences;
  bool recording;
  std::uint64_t active=0;
  std::vector<DocumentSnapshot> snapshots;
  std::vector<HistoryInfo> histories;
  explicit Collection(const Window &w):results(w.results),workflow(w.workflow),preferences(w.preferencesJson()),recording(w.recording) {
    if (!w.documents().empty()) active=w.document()->id;
    for (const auto &d:w.documents()) { snapshots.push_back(d->nativeSnapshot());histories.push_back(d->nativeOwner()->history()); }
  }
  void same(const Window &w) const {
    check(w.documents().size()==snapshots.size() && w.results==results && w.workflow==workflow &&
          w.preferencesJson()==preferences && w.recording==recording && (w.documents().empty()?0:w.document()->id)==active,
          "Rejected slicing changed collection/results/workflow/preferences/activation");
    for (std::size_t i=0;i<snapshots.size();++i) {
      check(w.documents()[i]->id==snapshots[i].id(),"Rejected slicing changed document order/identity");
      unchanged(w.nativeDocument(snapshots[i].id()),snapshots[i],histories[i]);
    }
  }
};
void check_output(Window &w,std::uint64_t id,const DocumentSnapshot &source,const AxisExtractionRequest &r,
                  int samples,PixelType type,bool bits=false) {
  const auto out=w.nativeSnapshot(id);const auto &data=out.data();
  check(id!=source.id() && w.document()->id==id && !w.documentById(id)->scientific() && !out.dirty() &&
        w.nativeDocument(id)->undo_steps()==0 && w.nativeDocument(id)->redo_steps()==0,
        "Derived result is not a new active clean ordinary owner with empty history");
  geometry(data.dataset,source.data().dataset.calibration(),r,samples);pixels(data.dataset,r,samples,type,bits);
  check(!data.dataset.shares_pixels(source.data().dataset) && !data.selection && !data.previous_selection &&
        data.rois.empty() && data.overlays.empty() && !data.spectrum && data.metadata.properties.empty() &&
        data.metadata.record_layout.empty() && data.metadata.extensions.size()==1,
        "Derived result copied source pixels/annotations/opaque metadata");
  const bool range=std::holds_alternative<AxisRangeResliceRequest>(r);
  const auto key=range?"axis_range_reslice":"axis_plane_export";
  check(data.metadata.extensions.count(key)==1,"Derived output lost its dedicated provenance extension");
  const auto p=object(data.metadata.extensions.at(key));
  check(integer(p.at("source_document_id"))==source.id() && integer(p.at("t"))==std::uint64_t(requested_t(r)) &&
        integer(p.at("c"))==std::uint64_t(requested_c(r)) && std::get<std::string>(p.at("plane"))==name(requested_plane(r)),
        "Derived provenance lost exact original owner/T/C/plane");
  if (range) {
    const auto &q=std::get<AxisRangeResliceRequest>(r);
    check(integer(p.at("first"))==std::uint64_t(q.first) && integer(p.at("last"))==std::uint64_t(q.last) &&
          integer(p.at("step"))==std::uint64_t(q.step) && integer(p.at("count"))==std::uint64_t(count(r)),
          "Range provenance lost inclusive endpoints/positive stride/nonlanding Last");
  } else check(integer(p.at("coordinate"))==std::uint64_t(first(r)),"Plane provenance changed fixed-axis coordinate");
}
void mapping(const fs::path &dir) {
  std::size_t rows=0;
  for (bool range:{false,true}) for (auto p:{Plane::XY,Plane::XZ,Plane::YZ})
    for (auto type:{PixelType::UInt8,PixelType::UInt16,PixelType::Float32,PixelType::Float64}) for (int samples:{1,3,4}) {
      Window w((dir/("finite-"+std::to_string(rows)+".json")).u8string());
      w.updateSettings({{"default_zoom",2.0},{"history_limit",3}},false);
      auto source=make_owner(type,samples);w.addNativeDocument(source);
      const auto before=source->snapshot();const auto history=source->history();
      const auto r=request(range,p);const auto id=extract(w,before,r);
      check_output(w,id,before,r,samples,type);unchanged(source,before,history);
      check(w.nativeDocument(id)->history_limit()==3,"Derived owner ignored configured history policy");
      const auto &c=w.nativeSnapshot(id).data().dataset.calibration();
      double length=0;for(int row=0;row<3;++row)length+=c.direction[row*3]*c.direction[row*3];
      check(w.nativeSnapshot(id).display().zoom==2.0/(c.spacing[0]*std::sqrt(length)),
            "Derived owner ignored configured physical initial zoom");
      check(w.workflow.empty() && w.results.empty(),"Recording-off extraction changed workflow/results");
      w.closeDocument(id,true);check(w.documents().size()==1,"Disposable finite-row output remained attached");
      ++rows;
    }
  check(rows==72,"Finite native desktop interface matrix lost a declared row");
  std::size_t bit_rows=0;
  for (bool range:{false,true}) for (auto p:{Plane::XY,Plane::XZ,Plane::YZ}) {
    Window w((dir/("bits-"+std::to_string(bit_rows)+".json")).u8string());
    auto source=make_owner(PixelType::Float32,1,true);w.addNativeDocument(source);
    const auto before=source->snapshot();const auto history=source->history();const auto r=request(range,p);
    const auto id=extract(w,before,r);check_output(w,id,before,r,1,PixelType::Float32,true);unchanged(source,before,history);
    const auto file=dir/("bits-"+std::to_string(bit_rows)+".ijx");
    desktop::saveNativeSession(file.u8string(),{w.documentById(id)},Json::array(),Json::array());
    Json rows_json,steps_json;const auto opened=desktop::loadNativeSession(file.u8string(),rows_json,steps_json);
    check(opened.size()==1,"Exact bit output archive reopened the wrong owner count");
    pixels(opened[0]->nativeSnapshot().data().dataset,r,1,PixelType::Float32,true);
    geometry(opened[0]->nativeSnapshot().data().dataset,calibration(),r,1);
    w.closeDocument(id,true);++bit_rows;
  }
  check(bit_rows==6,"Quiet-NaN bit matrix lost a declared separate row");
  for (bool range:{false,true}) {
    Window w((dir/(range?"singleton-range.json":"singleton-plane.json")).u8string());
    auto source=make_owner(PixelType::UInt16,1,false,true);w.addNativeDocument(source);
    check(w.document()->scientific(),"Explicit singleton ROI zero positions did not retain scientific classification");
    const auto before=source->snapshot();const auto history=source->history();const auto r=request(range,Plane::XZ,0,0);
    const auto id=extract(w,before,r);check_output(w,id,before,r,1,PixelType::UInt16);unchanged(source,before,history);
  }
  for(bool range:{false,true}) {
    Window w((dir/(range?"inactive-eager-range.json":"inactive-eager-plane.json")).u8string());
    auto source=make_eager(),active=make_owner();w.addNativeDocument(source);w.addNativeDocument(active);
    check(!w.documentById(source->id)->scientific() && w.document()->id==active->id,
          "Eager explicit-inactive regression did not establish its required ownership");
    const auto before=source->snapshot();const auto history=source->history();
    const auto other=active->snapshot();const auto other_history=active->history();const auto r=request(range,Plane::XZ,0,0);
    const auto id=extract(w,before,r);check_output(w,id,before,r,1,PixelType::UInt16);
    unchanged(source,before,history);unchanged(active,other,other_history);
    w.closeDocument(id,true);w.setActiveIndex(1);
    if(range)w.action("reslice_axis_range",{{"document_id",source->id},{"plane","XZ"},{"first",1},{"last",4},{"step",2}});
    else w.action("export_axis_plane",{{"document_id",source->id},{"plane","XZ"},{"coordinate",2}});
    check_output(w,w.document()->id,before,r,1,PixelType::UInt16);
    unchanged(source,before,history);unchanged(active,other,other_history);
  }
}

bool same_usage(const JobUsage &a,const JobUsage &b) {
  return a.running==b.running && a.queued==b.queued && a.reserved_bytes==b.reserved_bytes &&
      a.running_bytes==b.running_bytes && a.retained_bytes==b.retained_bytes;
}
void released(Window &w) {
  const auto s=GaussianCancellationAccess::scheduler(w);w.poll();const auto u=s->usage();
  check(!u.running && !u.queued && !u.reserved_bytes && !u.running_bytes && !u.retained_bytes,
        "Slicing/Gaussian cleanup retained running work or reservations");
}
void drain(Window &w) {
  const auto deadline=std::chrono::steady_clock::now()+5s;
  while(w.busy() && std::chrono::steady_clock::now()<deadline){w.poll();std::this_thread::yield();}
  check(!w.busy(),"Native host work did not terminate within bounded 5000 ms polling");
  w.poll();
}
TargetRequest gaussian_target() {
  TargetRequest r;r.scope=TargetScope::Positions;r.positions={{1,2,2}};r.roi=RoiPolicy::WholePlane;return r;
}
OperationJob preview(Window &w,const DocumentSnapshot &source,std::uint64_t generation) {
  GaussianCancellationAccess::request(w,resolve_gaussian_command(source,gaussian_target(),{.85,false}),generation);
  const auto job=GaussianCancellationAccess::laneJob(w,true);
  check(job && job->wait_for(5s) && job->info().state==JobState::Ready,"Gaussian preview did not become READY within 5000 ms");
  w.poll();const auto d=GaussianCancellationAccess::diagnostics(w);
  check(d["gaussian_jobs"]["active_installed_generation"]==generation &&
        d["gaussian_jobs"]["preview"]["exit"]=="installed","Gaussian preview was not actually installed");
  return *job;
}
class UnreadSource final:public PlaneSource {
public:
  DatasetShape shape()const override{return {2,3,4,5,7,1};}
  PixelType pixel_type()const override{return PixelType::Float32;}
  std::shared_ptr<const Image> read_plane(PlanePosition)const override {++reads;throw std::runtime_error("Forbidden lazy desktop read");}
  mutable std::size_t reads=0;
};
void refusal(const fs::path &dir) {
  Window w((dir/"preferences.json").u8string());auto inactive=make_owner(),source=make_owner();
  w.addNativeDocument(inactive);w.addNativeDocument(source);w.recording=true;
  w.results=Json::array({Json{{"kept",7}}});w.workflow=Json::array({Json{{"kept","workflow"}}});
  const auto before=source->snapshot();preview(w,before,101);
  const auto scheduler=GaussianCancellationAccess::scheduler(w);const Collection collection(w);
  const auto diagnostic=GaussianCancellationAccess::diagnostics(w);const auto usage=scheduler->usage();
  const auto same=[&]{collection.same(w);check(GaussianCancellationAccess::diagnostics(w)==diagnostic && same_usage(scheduler->usage(),usage),
                                            "Deterministic slicing refusal disturbed Gaussian preview/lane/resources");};
  for(bool range:{false,true}) {
    rejects([&]{extract(w,inactive->snapshot(),request(range,Plane::XY));},"Inactive scientific owner was exported");same();
    auto foreign=make_owner();rejects([&]{extract(w,foreign->snapshot(),request(range,Plane::XY));},"Foreign scientific owner was exported");same();
    auto wrong_tc=request(range,Plane::XY,0,0);
    rejects([&]{extract(w,before,wrong_tc);},"Scientific new command silently accepted noncaptured T/C");same();
    rejects([&]{extract(w,before,request(range,static_cast<Plane>(99)));},"Invalid extraction plane enum was admitted");same();
    rejects([&]{extract(w,before,request(range,Plane::XY,1,2,0));},"Zero extraction working cap was admitted");same();
    rejects([&]{extract(w,before,request(range,Plane::XY,1,2,1));},"Insufficient extraction working cap was admitted");same();
  }
  for (auto q:{AxisPlaneExportRequest{Plane::XY,-1,1,2},AxisPlaneExportRequest{Plane::XY,4,1,2},
               AxisPlaneExportRequest{Plane::XZ,5,1,2},AxisPlaneExportRequest{Plane::YZ,7,1,2}}) {
    rejects([&]{w.exportAxisPlane(before,q);},"Invalid fixed-axis coordinate was admitted");same();
  }
  for (auto q:{AxisRangeResliceRequest{Plane::XY,0,3,0,1,2},AxisRangeResliceRequest{Plane::XY,0,3,-1,1,2},
               AxisRangeResliceRequest{Plane::XZ,4,3,1,1,2},AxisRangeResliceRequest{Plane::YZ,0,7,1,1,2}}) {
    rejects([&]{w.resliceAxisRange(before,q);},"Malformed inclusive integer range was admitted");same();
  }
  for (const auto &p:std::vector<Json>{{{"plane","XY"},{"coordinate",true}},{{"plane","XY"},{"coordinate",1.5}},
                                    {{"plane","XY"},{"coordinate",std::uint64_t(std::numeric_limits<int>::max())+1}},
                                    {{"plane","XY"},{"coordinate",0},{"unknown",true}}}) {
    rejects([&]{w.action("export_axis_plane",p);},"Malformed direct plane action was admitted");same();
  }
  for (const auto &p:std::vector<Json>{{{"plane","XY"},{"first",0},{"last",3},{"step",1}},
      {{"document_id",source->id},{"plane","XY"},{"first",0},{"last",3},{"step",true}},
      {{"document_id",source->id},{"plane","XY"},{"first",0},{"last",3},{"step",0}}}) {
    rejects([&]{w.action("reslice_axis_range",p);},"Malformed/missing-ID direct range action was admitted");same();
  }
  for (const auto &action:{"reslice","reslice_oriented_axis_range","reslice_spaced_axis_range","reslice_slicer_axis_range"}) {
    rejects([&]{w.action(action);},"Unqualified scientific slicing sibling was enabled");same();
  }
  w.cancel();released(w);
  w.closeDocument(source->id,true);const Collection closed(w);
  rejects([&]{w.exportAxisPlane(before,std::get<AxisPlaneExportRequest>(request(false,Plane::XY)));},"Closed source capture exported");closed.same(w);

  // Reject unsupported source state at its real admission boundary. If the
  // existing frontend can hold it, the extraction command must still refuse.
  Window unsupported((dir/"unsupported.json").u8string());
  const auto negative=[&](const std::shared_ptr<Document> &owner){
    const Collection absent(unsupported);bool refused=false;
    try{unsupported.addNativeDocument(owner);}catch(const std::invalid_argument &){refused=true;}
    if(refused){absent.same(unsupported);return;}
    const Collection admitted(unsupported);
    rejects([&]{extract(unsupported,owner->snapshot(),request(false,Plane::XY));},"Unsupported desktop source was exported");
    admitted.same(unsupported);unsupported.closeDocument(owner->id,true);
  };
  // DatasetShape currently rejects S2 before an owner/facade can exist. Keep
  // that representation refusal inside its own boundary, and do not report
  // this fixture as an admitted desktop source or extraction observation.
  const Collection before_s2(unsupported);
  rejects([&]{(void)make_owner(PixelType::Float32,2);},"Native dataset admitted unsupported S2 storage");
  before_s2.same(unsupported);
  auto lazy=std::make_shared<UnreadSource>();negative(std::make_shared<Document>(Dataset(lazy,calibration())));
  check(lazy->reads==0,"Unsupported lazy source admission/extraction read pixels");
  auto fourier=make_owner();auto edit=fourier->begin_edit();
  edit.set_spectrum(FourierState(Spectrum{7,5,std::vector<std::complex<double>>(35)},std::array<int,2>{5,7}));fourier->commit(edit);negative(fourier);
  auto future=make_owner();auto display=future->snapshot().display();display.properties["future_display"]=true;future->set_display(display);negative(future);
  for(bool range:{false,true})for(int edit_kind=0;edit_kind<4;++edit_kind) {
    Window stale((dir/("stale-"+std::to_string(range)+"-"+std::to_string(edit_kind)+".json")).u8string());
    auto owner=make_owner();stale.addNativeDocument(owner);stale.recording=true;const auto capture=owner->snapshot();
    auto edit=owner->begin_edit();
    if(edit_kind==0)edit.set_property("changed",true);
    if(edit_kind==1)edit.replace_plane({1,2,0},Image(7,5,1,PixelType::Float32));
    if(edit_kind==2){auto c=calibration();c.origin[0]=11;edit.set_calibration(c);}
    if(edit_kind==3){auto rois=capture.data().rois;rois[0].name="changed stored ROI";edit.set_rois(std::move(rois));}
    owner->commit(edit);const Collection state(stale);
    rejects([&]{extract(stale,capture,request(range,Plane::XY));},"Stale Pixels/calibration/stored-ROI/metadata capture was exported");state.same(stale);
  }
  {
    Window limited((dir/"configured-cap.json").u8string());limited.updateSettings({{"processing_memory_mb",1}},false);
    Stack planes;for(int i=0;i<24;++i)planes.emplace_back(257,257,1,PixelType::Float32);
    auto owner=std::make_shared<Document>(Dataset::from_planes({2,3,4,257,257,1},std::move(planes),calibration()));
    limited.addNativeDocument(owner);limited.recording=true;const auto capture=owner->snapshot();const Collection state(limited);
    for(bool range:{false,true}) {
      rejects([&]{extract(limited,capture,request(range,Plane::XY,0,0,8*1024*1024));},
              "Request cap raised the smaller configured processing-memory limit");state.same(limited);
    }
  }
}

struct Gate {
  std::mutex mutex;std::condition_variable changed;bool entered=false,open=false;
  void block(){std::unique_lock<std::mutex>lock(mutex);entered=true;changed.notify_all();
    if(!changed.wait_for(lock,5s,[&]{return open;}))throw std::runtime_error("Slicing busy gate exceeded 5000 ms");}
  void await(){std::unique_lock<std::mutex>lock(mutex);check(changed.wait_for(lock,5s,[&]{return entered;}),"Busy job never reached its bounded gate");}
  void release(){std::lock_guard<std::mutex>lock(mutex);open=true;changed.notify_all();}
};
struct Release {std::shared_ptr<Gate>gate;~Release(){gate->release();}};
void lifecycle(const fs::path &dir) {
  for(bool range:{false,true}) {
    Window w((dir/(range?"preview-range.json":"preview-plane.json")).u8string());
    auto source=make_owner();w.addNativeDocument(source);w.recording=true;
    const auto captured=source->snapshot();preview(w,captured,range?202:201);
    const auto diagnostic=GaussianCancellationAccess::diagnostics(w);const auto scheduler=GaussianCancellationAccess::scheduler(w);const auto usage=scheduler->usage();
    const Collection collection(w);
    rejects([&]{extract(w,captured,request(range,Plane::XZ,1,2,1));},"Preparation cap failure was admitted beside retained preview");
    collection.same(w);check(GaussianCancellationAccess::diagnostics(w)==diagnostic && same_usage(scheduler->usage(),usage),
                            "Preparation failure released or retargeted READY preview");
    // Navigation and transient selection are genuine allowed concurrent state.
    auto display=source->snapshot().display();display.t=0;display.c=1;display.crosshair={6,4,0};w.setNativeDisplay(display,source->id);
    source->select(Selection("point",{{3,2}}));const auto navigated=source->snapshot();const auto history=source->history();
    const auto r=request(range,Plane::XZ);const auto id=extract(w,captured,r);
    check_output(w,id,captured,r,1,PixelType::Float32);unchanged(source,navigated,history);
    check(w.workflow.size()==1,"Successful extraction beside preview did not record exactly once");
    released(w);const auto final=GaussianCancellationAccess::diagnostics(w);
    check(final["gaussian_jobs"]["active_installed_generation"].is_null(),"Successful output activation retained source preview generation");
  }
  for(const std::string mode:{"queued","running","ready"}) {
    Window w((dir/(mode+".json")).u8string());auto source=make_owner();w.addNativeDocument(source);w.recording=true;
    const auto captured=source->snapshot();const auto scheduler=GaussianCancellationAccess::scheduler(w);scheduler->set_limits({1,0,64});
    auto gate=std::make_shared<Gate>();Release release{gate};std::optional<Job>blocker;
    if(mode=="queued"){blocker=scheduler->submit([gate](const JobContext&c){gate->block();c.checkpoint();});gate->await();}
    auto stopped=std::make_shared<std::atomic<bool>>(false);
    {detail::ScopedOperationJobProbe probe([mode,gate,stopped](const detail::OperationJobProbeEvent&e){
      if(mode=="running" && e.phase=="stage_progress" && e.stage=="ImageJ Gaussian source copy" && e.completed>0 && !stopped->exchange(true))gate->block();});
      w.applyGaussian(gaussian_target(),{.85,false},source->id);}
    const auto job=GaussianCancellationAccess::laneJob(w,false);check(bool(job),"Busy Apply did not create a native job");
    if(mode=="running")gate->await();else if(mode=="ready")check(job->wait_for(5s) && job->info().state==JobState::Ready,"Apply did not reach READY without publication");
    check(w.busy() && job->info().state==(mode=="queued"?JobState::Queued:mode=="running"?JobState::Running:JobState::Ready),"Busy fixture did not establish its declared actual job state");
    const Collection state(w);const auto usage=scheduler->usage();const auto diagnostic=GaussianCancellationAccess::diagnostics(w);
    for(bool range:{false,true}){rejects([&]{extract(w,captured,request(range,Plane::XY));},"Slicing was admitted during Apply busy");state.same(w);}
    check(same_usage(scheduler->usage(),usage) && GaussianCancellationAccess::diagnostics(w)==diagnostic,
          "Busy slicing refusal changed held Apply job/lane/reservations");
    w.cancel();gate->release();if(blocker)check(blocker->wait_for(5s),"Queue blocker did not release within 5000 ms");
    check(job->wait_for(5s),"Canceled Apply failed to terminate within 5000 ms");released(w);
  }
  for(const std::string mode:{"legacy","threshold"}) {
    Window w((dir/(mode+"-busy.json")).u8string());auto source=make_eager();w.addNativeDocument(source);w.recording=true;
    const auto captured=source->snapshot();
    if(mode=="legacy") {
      // Submit through the public legacy lane. Its completed preparation stays
      // busy until creator-thread polling, so this case needs neither an ImGui
      // session nor the UI-only startBusyPreparation diagnostic gate.
      w.applyOperation("add",{{"value",0}},false,true,source->id);
    } else {
      w.action("show_manual_threshold",{{"document_id",source->id}});
      w.action("apply_manual_threshold",{{"document_id",source->id},{"lower",10},{"upper",100}});
    }
    const auto busy_job=w.jobInfo();
    check(w.busy() && busy_job && busy_job->id>0 && busy_job->document_id==source->id,
          "Legacy/threshold Apply did not establish an actual owned busy job");const Collection before(w);
    for(bool range:{false,true}) {
      rejects([&]{extract(w,captured,request(range,Plane::XY,0,0));},"Slicing was admitted during threshold/legacy Apply");before.same(w);
    }
    check(w.jobInfo() && w.jobInfo()->id==busy_job->id,"Busy slicing refusal replaced the actual legacy/threshold job");
    w.cancel();
    if(mode=="threshold") {
      const auto info=w.jobInfo();
      if(info) {
        for(const auto &job:w.jobs())if(job.id==info->id)check(job.cancel_requested || job.state==JobState::Canceled,
                                                             "Threshold busy fixture did not request actual cancellation");
      }
    }
    drain(w);
    released(w);
  }
  // Use the established observer to interleave actual writes immediately before
  // native publication. The observer supplies no expected pixels or transforms.
  for(bool range:{false,true})for(int scenario=0;scenario<5;++scenario) {
    Window w((dir/("conflict-"+std::to_string(range)+"-"+std::to_string(scenario)+".json")).u8string());
    auto source=make_owner(),other=make_owner();w.addNativeDocument(source);w.addNativeDocument(other);w.setActiveIndex(0);w.recording=true;
    const auto captured=source->snapshot();std::size_t callbacks=0;std::exception_ptr callback_error;
    std::shared_ptr<Document>candidate;std::unique_ptr<Collection>intervening;
    const auto mutate=[&](const char*phase,const std::shared_ptr<Document>&fresh){
      if(std::strcmp(phase,"before_native_publish")!=0)return;
      ++callbacks;candidate=fresh;
      try {
        if(scenario==0){auto edit=source->begin_edit();edit.set_property("intervening",true);source->commit(edit);}
        if(scenario==1){auto edit=source->begin_edit();edit.replace_plane({1,2,0},Image(7,5,1,PixelType::Float32));source->commit(edit);}
        if(scenario==2)w.closeDocument(source->id,true);
        if(scenario==3)w.setActiveIndex(1);
        if(scenario==4){auto edit=fresh->begin_edit();edit.set_title("Intervening candidate edit");fresh->commit(edit);}
        intervening=std::make_unique<Collection>(w);
      }catch(...){callback_error=std::current_exception();}
    };
    desktop::AxisPlaneUiProbeAccess::setPublicationProbe(w,[&](const char*p,const DocumentSnapshot&,const AxisPlaneExportRequest&,const std::shared_ptr<Document>&d){mutate(p,d);});
    desktop::AxisRangeUiProbeAccess::setPublicationProbe(w,[&](const char*p,const DocumentSnapshot&,const AxisRangeResliceRequest&,const std::shared_ptr<Document>&d){mutate(p,d);});
    rejects([&]{extract(w,captured,request(range,Plane::XZ));},"Stale/closed/switched scientific publication was adopted");
    desktop::AxisPlaneUiProbeAccess::setPublicationProbe(w,{});desktop::AxisRangeUiProbeAccess::setPublicationProbe(w,{});
    if(callback_error)std::rethrow_exception(callback_error);
    check(callbacks==1 && candidate && intervening,"Publication conflict did not reach the real staged candidate boundary");
    intervening->same(w);
    rejects([&]{w.nativeDocument(candidate->id);},"Rejected output left an orphan facade/controller owner");
    check(candidate.use_count()==1,"Rejected output retained an orphan Pane/facade/preparation owner beyond the assertion capture");
    check(w.workflow.empty() && w.results.empty(),"Rejected publication appended workflow/results");
  }
}

void record_replay(const fs::path &dir) {
  const Json legacy={{"operation","add"},{"parameters",{{"value",1}}},{"all_slices",false},{"slice_index",0}};
  for(bool range:{false,true}) {
    Window w((dir/(range?"range.json":"plane.json")).u8string());auto source=make_owner();w.addNativeDocument(source);
    w.updateSettings({{"processing_memory_mb",1}},false);
    w.recording=true;w.workflow=Json::array({legacy});w.results=Json::array({Json{{"sentinel",9}}});
    const auto captured=source->snapshot();const auto r=request(range,Plane::YZ,1,2,8*1024*1024);const auto expected=resolve_axis_extraction_command(captured,r).record();
    const auto output=extract(w,captured,r);
    check(w.workflow.size()==2 && w.workflow[0]==legacy && w.workflow[1]==desktop::metadataJson(expected) && w.results==Json::array({Json{{"sentinel",9}}}),
          "Successful extraction did not atomically append exactly one normalized command beside legacy records");
    w.closeDocument(output,true);w.setActiveIndex(0);
    auto display=source->snapshot().display();display.t=0;display.c=0;display.crosshair={6,4,0};w.setNativeDisplay(display,source->id);
    source->select(Selection("point",{{3,2}}));const auto navigated=source->snapshot();const auto history=source->history();
    const auto replayed=w.replayAxisExtraction(expected,{},source->id);
    check_output(w,replayed,captured,r,1,PixelType::Float32);unchanged(source,navigated,history);
    check(w.workflow.size()==3 && w.workflow[1]==desktop::metadataJson(expected) && w.workflow[2]==desktop::metadataJson(expected),
          "Strict replay did not preserve recorded T/C and append once without recursive entries");
    w.closeDocument(replayed,true);w.setActiveIndex(0);
    const auto action_count=w.workflow.size();
    w.action("replay_axis_extraction",{{"record",desktop::metadataJson(expected)},{"document_id",source->id}});
    const auto action_output=w.document()->id;
    check_output(w,action_output,captured,r,1,PixelType::Float32);unchanged(source,navigated,history);
    check(w.workflow.size()==action_count+1 && w.workflow.back()==desktop::metadataJson(expected),
          "Positive strict replay action failed captured T/C or exactly-once recording");
    w.closeDocument(action_output,true);w.setActiveIndex(0);w.recording=false;
    const auto saved_workflow=w.workflow;const auto off=w.replayAxisExtraction(expected,{},source->id);
    check(w.workflow==saved_workflow,"Recording-off replay appended a command");w.closeDocument(off,true);w.setActiveIndex(0);
    // Opaque native transport keeps both legacy/new records byte-equivalent in
    // JSON value form; reopened ownership remains deliberately strict.
    const auto path=dir/(range?"range-session.ijx":"plane-session.ijx");
    desktop::saveNativeSession(path.u8string(),w.documents(),w.results,w.workflow);
    Window reopened((dir/(range?"reopened-range.json":"reopened-plane.json")).u8string());reopened.openFile(path.u8string());
    check(reopened.documents().size()==1 && reopened.workflow==saved_workflow && reopened.results==w.results,
          "Native session lost exact mixed legacy/extraction workflow records/results");
    auto new_source=reopened.nativeDocument();const Collection reopened_before(reopened);
    rejects([&]{reopened.replayAxisExtraction(expected,{},new_source->id);},"Strict replay rebound a reopened owner ID");reopened_before.same(reopened);
    auto c=calibration();c.origin={11,-5,21};c.spacing={2,4,.5};c.direction={0,-1,.25,1,.5,0,0,0,-1};
    c.unit="mm";c.time_unit="s";c.time_origin=13;c.frame_interval=.25;
    auto edit=new_source->begin_edit();edit.set_calibration(c);new_source->commit(edit);
    auto view=new_source->snapshot().display();view.t=0;view.c=1;reopened.setNativeDisplay(view,new_source->id);
    const auto rebound_capture=new_source->snapshot();const auto rebound_history=new_source->history();
    const auto explicit_request=request(range,Plane::XZ,0,1);const auto rebound_record=rebind_axis_extraction_command(rebound_capture,expected,explicit_request).record();
    reopened.recording=true;const auto rebound=reopened.replayAxisExtraction(expected,explicit_request,new_source->id);
    check_output(reopened,rebound,rebound_capture,explicit_request,1,PixelType::Float32);unchanged(new_source,rebound_capture,rebound_history);
    check(reopened.workflow.size()==saved_workflow.size()+1 && reopened.workflow.back()==desktop::metadataJson(rebound_record) &&
          reopened.workflow[1]==desktop::metadataJson(expected),"Explicit reopened rebind changed original record or failed to append its new geometry/source once");
    reopened.closeDocument(rebound,true);reopened.setActiveIndex(0);
    const auto action_rebind_count=reopened.workflow.size();
    reopened.action("replay_axis_extraction",{{"record",desktop::metadataJson(expected)},{"document_id",new_source->id},
                                             {"rebind_request",explicit_request_json(explicit_request)}});
    const auto action_rebound=reopened.document()->id;
    check_output(reopened,action_rebound,rebound_capture,explicit_request,1,PixelType::Float32);unchanged(new_source,rebound_capture,rebound_history);
    check(reopened.workflow.size()==action_rebind_count+1 && reopened.workflow.back()==desktop::metadataJson(rebound_record) &&
          reopened.workflow[1]==desktop::metadataJson(expected),"Positive full rebind action lost request fields/geometry or mutated original record");
    // The generic legacy runner cannot silently skip or reinterpret typed
    // extraction rows. A failure may be synchronous or reported by its real job.
    const Collection mixed(reopened);reopened.clearError();bool rejected=false;
    try{reopened.action("run_workflow");}catch(const std::bad_alloc&){throw;}catch(const std::exception&){rejected=true;}
    if(!rejected){drain(reopened);rejected=!reopened.error().empty();}
    check(rejected,"Legacy runner accepted/skipped unsupported extraction records in a mixed workflow");mixed.same(reopened);
    reopened.closeDocument(action_rebound,true);reopened.setActiveIndex(0);reopened.clearError();
    const Collection valid(reopened);
    auto malformed=expected;malformed["destination"]=std::string("in_place");
    rejects([&]{reopened.replayAxisExtraction(malformed,explicit_request,new_source->id);},"Explicit desktop rebind repaired malformed original record");valid.same(reopened);
    auto derived=object(expected.at("derived"));derived["working_bytes"]=std::uint64_t(1);
    auto tampered=expected;tampered["derived"]=metadata_object(derived);
    rejects([&]{reopened.replayAxisExtraction(tampered,explicit_request,new_source->id);},"Desktop rebind trusted tampered original derived geometry");valid.same(reopened);
    rejects([&]{reopened.replayAxisExtraction(expected,request(range,Plane::XY,1,2),new_source->id);},"Desktop rebind accepted noncurrent explicit T/C");valid.same(reopened);
    for(const Json &p:std::vector<Json>{Json::object(),Json{{"record",desktop::metadataJson(expected)},{"rebind_request",true}},
         Json{{"record",desktop::metadataJson(expected)},{"rebind_request",Json::object()}},
         Json{{"record",desktop::metadataJson(expected)},{"unknown",true}}}){
      rejects([&]{reopened.action("replay_axis_extraction",p);},"Malformed replay action was admitted");valid.same(reopened);
    }
    // Real source edits, including a restored numeric value, stale the binding.
    w.recording=true;w.setActiveIndex(0);auto source_edit=source->begin_edit();source_edit.set_property("new metadata",true);source->commit(source_edit);source->undo();
    const Collection stale(w);rejects([&]{w.replayAxisExtraction(expected,{},source->id);},"Undo restoring equal metadata revived stale extraction replay");stale.same(w);
  }
  // Prospective workflow allocation/shape validation must precede adoption.
  for(bool range:{false,true}) {
    Window w((dir/(range?"bad-workflow-range.json":"bad-workflow-plane.json")).u8string());auto source=make_owner();w.addNativeDocument(source);
    w.recording=true;w.workflow=Json{{"invalid","array required"}};const Collection before(w);
    rejects([&]{extract(w,source->snapshot(),request(range,Plane::XY));},"Recording admitted a malformed workflow container");before.same(w);
  }
}
} // namespace
int main(int argc,char **argv) {
  try {
    check(argc==2 || argc==3,"Supply a fresh scientific-slicing artifact directory and optional group");
    const auto dir=fs::absolute(fs::u8path(argv[1]));check(!fs::exists(dir),"Scientific-slicing artifact directory must be fresh");fs::create_directories(dir);
    const std::string group=argc==3?argv[2]:"all";bool matched=false;
    const auto run=[&](const char*name,auto fn){if(group!="all" && group!=name)return;matched=true;const auto path=dir/name;fs::create_directory(path);fn(path);std::cout<<"PASS scientific-slicing."<<name<<'\n';};
    run("mapping",mapping);run("refusal",refusal);run("lifecycle",lifecycle);run("record_replay",record_replay);
    check(matched,"Unknown scientific-slicing native group");std::cout<<"scientific-slicing assertions="<<assertions<<'\n';return 0;
  }catch(const std::exception&e){std::cerr<<"FAIL scientific-slicing after "<<assertions<<" assertions: "<<e.what()<<'\n';return 1;}
}
