#pragma once
#include <imagejxx/application.hpp>
#include <imagejxx/saved_roi_geometry.hpp>
#include <imagejxx/selection_edit.hpp>
#include <imagejxx/roi_style.hpp>
#include <atomic>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace rounded_saved_resources {
using namespace imagejxx;
inline std::size_t checks=0,cases=0;
inline void check(bool ok,const char *message) {++checks;if(!ok)throw std::runtime_error(message);}
template<class Error=std::exception,class F> void rejects(F work,const char *message) {
  bool refused=false;try{work();}catch(const Error &){refused=true;}check(refused,message);
}
struct Source final:PlaneSource {
  DatasetShape dimensions;PixelType type;mutable std::atomic<int> planes{0},regions{0};
  Source(int width=12,int height=10,PixelType type=PixelType::UInt16,int samples=1)
      :dimensions{2,2,3,height,width,samples},type(type){}
  DatasetShape shape()const override{return dimensions;}
  PixelType pixel_type()const override{return type;}
  std::shared_ptr<const Image> read_plane(PlanePosition)const override{++planes;throw std::runtime_error("Saved rounded path read source plane");}
  Image read_region(PlanePosition,int,int,int,int)const override{++regions;throw std::runtime_error("Saved rounded path read source region");}
};
inline Selection box(bool rotated,int diameter) {
  Selection s("rounded_rectangle",{{2,2},{10,8}},1.5);
  s.attributes={{"corner_diameter",std::int64_t(diameter)},{"future",metadata_object({{"unsigned",std::numeric_limits<std::uint64_t>::max()},{"label",std::string("kept")}})}};
  s=with_roi_style(s,{{"stroke_color",std::string("#ff00ff00")},{"fill_color",std::string("#800000ff")},{"stroke_width",2.5}});
  return rotated?SelectionEditor(s,12,10).rotated(23):s;
}
struct Fixture {
  std::shared_ptr<Source> source;std::shared_ptr<Document> owner;Application app;
  Fixture(bool rotated,int diameter,int width=12,int height=10,PixelType type=PixelType::UInt16,int samples=1) {
    source=std::make_shared<Source>(width,height,type,samples);
    DocumentData data{Dataset(source)};
    data.selection=Selection("line",{{0,0},{1,1}},2);data.previous_selection=Selection("point",{{1,1}});
    data.rois={{"rounded",box(rotated,diameter),0,{},0,{{"row",std::string("kept")}}},
               {"other",Selection("rectangle",{{0,0},{1,1}}),{},{},{},{}}};
    data.metadata.extensions["unrelated"]=std::string("retained");
    owner=std::make_shared<Document>(std::move(data));app.adopt(owner);
  }
  void unread()const{check(source->planes==0 && source->regions==0,"Rounded saved edit acquired provider data");}
};
inline Selection replacement(const Selection &s) {
  auto v=SelectionEditor(s,12,10).moved({.5,-.5},false);
  v.attributes["corner_diameter"]=std::int64_t(3);return v;
}
inline void controls(Fixture &f) {
  const auto before=f.owner->snapshot();const auto original=before.data().rois[0];
  const auto changed=replacement(original.selection);SavedRoiGeometryRequest request(0,changed);
  const auto estimate=estimate_saved_roi_geometry(before,request);
  check(estimate.xy_pixels==120 && estimate.vertices==original.selection.points.size()+changed.points.size(),"Rounded saved estimate geometry dimensions");
  std::size_t callbacks=0;ExecutionControl control;
  control.checkpoint=[&]{++callbacks;};control.progress=[&](auto,auto){++callbacks;};
  control.stage_progress=[&](auto,auto,auto){++callbacks;};
  for(int boundary=0;boundary<5;++boundary) {
    SavedRoiGeometryLimits low;
    if(boundary==0)low.max_xy_pixels=119;
    if(boundary==1)low.max_vertices=1;
    if(boundary==2)low.max_working_bytes=estimate.working_bytes-1;
    if(boundary==3)low.max_record_bytes=estimate.record_wire_bytes-1;
    if(boundary==4)low.metadata_depth=1;
    callbacks=0;rejects([&]{resolve_saved_roi_geometry(before,request,low,control);},"Rounded cap refusal missing");
    check(callbacks==0 && f.owner->snapshot().shares_data(before),"Rounded cap refusal dispatched callbacks or changed source");f.unread();
  }
  SavedRoiGeometryLimits exact;exact.max_xy_pixels=120;exact.max_vertices=original.selection.points.size();
  exact.max_working_bytes=estimate.working_bytes;exact.max_record_bytes=estimate.record_wire_bytes;
  auto command=resolve_saved_roi_geometry(before,request,exact);
  check(command.estimate().working_bytes==estimate.working_bytes,"Rounded command exact resource boundary");
  validate_saved_roi_geometry_record(command.record());
  check(std::get<std::int64_t>(command.record().at("version"))==2,"Rounded saved command must use version2");
  auto replay=saved_roi_geometry_from_record(before,command.record());
  check(replay.replacement()==changed && replay.is_replay(),"Rounded saved replay lost intent");
  auto rebound=rebind_saved_roi_geometry(before,command.record(),request);
  check(rebound.replacement()==changed && !rebound.is_replay(),"Rounded saved rebind lost intent");
  auto token=std::make_shared<CancellationToken>();token->cancel();control.cancellation=token;callbacks=0;
  rejects<OperationCancelled>([&]{resolve_saved_roi_geometry(before,request,{},control);},"Rounded pre-cancel missing");
  check(callbacks==0,"Rounded pre-cancel invoked a callback");control.cancellation.reset();
  const auto intended=request.replacement;
  SavedRoiGeometryLimits held;
  control.checkpoint=[&]{++callbacks;request.row_index=99;request.replacement=Selection("line",{{0,0},{1,1}});held.max_working_bytes=1;check(f.app.snapshot().id()==f.owner->id,"Rounded callback cannot reenter owner");};
  auto captured=resolve_saved_roi_geometry(before,request,held,control);
  check(captured.row_index()==0 && captured.replacement()==intended && captured.limits().max_working_bytes==268435456,"Rounded resolver failed to freeze caller input before callbacks");
  f.unread();
  auto cap=f.app.capture_saved_roi_geometry();std::size_t peak=0;
  {auto prepared=f.app.prepare_saved_roi_geometry(cap,command);peak=prepared.working_bytes();check(prepared.active() && prepared.retained_working_bytes()==peak,"Rounded preparation missing retained resource ownership");}
  SavedRoiGeometryLimits below;below.max_working_bytes=peak-1;callbacks=0;
  ExecutionControl none;none.checkpoint=[&]{++callbacks;};
  rejects([&]{f.app.prepare_saved_roi_geometry(cap,command,SavedRoiGeometryInteraction::Numeric,below,none);},"Rounded preparation cap boundary ignored");
  check(callbacks==0,"Rounded preparation cap invoked callback");
  SavedRoiGeometryLimits admitted;admitted.max_working_bytes=peak;
  auto prepared=f.app.prepare_saved_roi_geometry(cap,command,SavedRoiGeometryInteraction::Numeric,admitted);
  check(prepared.retained_working_bytes()==peak,"Rounded exact preparation boundary unstable");
  const auto published=f.app.publish_saved_roi_geometry(prepared);
  check(published.changed && !prepared.active() && prepared.retained_working_bytes()==0,"Rounded publication did not release prepared ownership");
  const auto after=f.owner->snapshot();
  check(after.data().rois[0].selection==changed && after.data().rois[0].attributes==original.attributes,"Rounded publication changed unrelated row state");
  check(after.data().selection==before.data().selection && after.data().previous_selection==before.data().previous_selection &&
        after.data().dataset.shares_pixels(before.data().dataset) && f.owner->undo_steps()==1,"Rounded metadata publication changed samples/global selection/history");
  f.owner->undo();check(f.owner->snapshot().data().rois[0].selection==original.selection,"Rounded undo lost original geometry");
  f.owner->redo();check(f.owner->snapshot().data().rois[0].selection==changed,"Rounded redo lost replacement");f.unread();++cases;
}
inline int run() {
  for(bool rotated:{false,true})for(int diameter:{0,6,std::numeric_limits<int>::max()}) {
    for(auto type:{PixelType::UInt8,PixelType::UInt16,PixelType::Float32,PixelType::Float64})
      for(int samples:{1,3,4}) {Fixture f(rotated,diameter,12,10,type,samples);controls(f);}
    Fixture small(rotated,diameter),large(rotated,diameter,4096,4096);
    const auto a=estimate_saved_roi_geometry(small.owner->snapshot(),SavedRoiGeometryRequest(0,replacement(small.owner->snapshot().data().rois[0].selection)));
    const auto b=estimate_saved_roi_geometry(large.owner->snapshot(),SavedRoiGeometryRequest(0,replacement(large.owner->snapshot().data().rois[0].selection)));
    check(a.working_bytes==b.working_bytes,"Metadata-only rounded geometry unexpectedly charges canvas raster storage");small.unread();large.unread();
  }
  std::cout<<"rounded_saved_resources: "<<cases<<" cases / "<<checks<<" checks; provider reads zero\n";return 0;
}
}
