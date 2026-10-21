// Installed core-only SDK consumer. Literal step responses establish pixels;
// no desktop, Python, Qt or product filtering helper supplies expected values.
#include <imagejxx/application.hpp>
#include <imagejxx/gaussian_command.hpp>
#include <imagejxx/session.hpp>

#include <chrono>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace {
using namespace imagejxx;
std::size_t checks = 0;
void require(bool condition, const char *message) {
  if (!condition) throw std::runtime_error(message);
  ++checks;
}
template<class Exception = std::invalid_argument, class F>
void rejects(F work, const char *message) {
  bool caught = false;
  try { work(); } catch (const Exception &) { caught = true; }
  require(caught, message);
}
bool same(PlanePosition a, PlanePosition b) {
  return a.t == b.t && a.c == b.c && a.z == b.z;
}
bool selected(const std::vector<PlanePosition> &positions, PlanePosition p) {
  for (const auto candidate : positions) if (same(candidate, p)) return true;
  return false;
}
SpatialCalibration calibration() {
  SpatialCalibration value;
  value.spacing = {.5, 2, 3}; value.origin = {11, -7, 5};
  value.direction = {0, -1, 0, 1, 0, 0, 0, 0, 1};
  value.unit = "um"; value.frame_interval = .25;
  value.time_origin = 8; value.time_unit = "min";
  return value;
}
double literal(PlanePosition p, int x, int s, bool blur) {
  static constexpr int response[23] = {
      0,0,0,0,0,0,0,0,0,6,30,70,94,100,100,100,100,100,100,100,100,100,100};
  return 48*p.t + 16*p.c + 4*p.z + 20*s +
      (blur ? response[x] : (x < 11 ? 0 : 100));
}
Dataset fixture(PixelType type, int samples = 1) {
  Stack planes;
  for (int t = 0; t < 2; ++t) for (int c = 0; c < 3; ++c) for (int z = 0; z < 4; ++z) {
    Image image(23, 17, samples, type);
    for (int y = 0; y < 17; ++y) for (int x = 0; x < 23; ++x) for (int s = 0; s < samples; ++s)
      image.at(x,y,s) = literal({t,c,z},x,s,false);
    planes.push_back(std::move(image));
  }
  return Dataset::from_planes({2,3,4,17,23,samples},std::move(planes),calibration());
}
void pixels(const DocumentSnapshot &source, const std::vector<PlanePosition> &changed) {
  const auto &data = source.data().dataset;
  require(data.shape().dimensions() == std::array<int,6>{2,3,4,17,23,data.shape().s},
          "SDK operation changed scientific dimensions");
  for (int t = 0; t < 2; ++t) for (int c = 0; c < 3; ++c) for (int z = 0; z < 4; ++z) {
    const PlanePosition p{t,c,z};
    const auto plane = data.plane(p);
    require(plane && plane->width == 23 && plane->height == 17 && plane->channels == data.shape().s,
            "SDK scientific plane layout");
    for (int y = 0; y < 17; ++y) for (int x = 0; x < 23; ++x) for (int s = 0; s < plane->channels; ++s)
      require(plane->at(x,y,s) == literal(p,x,s,selected(changed,p)), "SDK independent literal Gaussian pixel");
  }
}
void captureView(Document &owner) {
  auto view = owner.snapshot().display();
  view.t=1; view.c=2; view.crosshair={9,6,2}; view.orthogonal=true;
  view.zoom=1.25; view.linkedZoom=false;
  view.planeZoom={1.25,2,3}; view.pan={{{3,-2},{1,4},{-5,2}}};
  owner.set_display(view);
}
TargetRequest all() {
  TargetRequest request; request.scope=TargetScope::Ranges;
  request.t=AxisRange{0,2,1}; request.c=AxisRange{0,3,1}; request.z=AxisRange{0,4,1};
  request.roi=RoiPolicy::WholePlane;
  return request;
}
void exercise(PixelType type, int samples, const char *mode) {
  Application app;
  auto owner=std::make_shared<Document>(fixture(type,samples),"SDK scientific Gaussian");
  app.adopt(owner); captureView(*owner);
  auto edit=owner->begin_edit();
  edit.set_property("specimen",metadata_object({{"id",std::string("owned")},{"version",3}}));
  owner->commit(edit); owner->mark_saved();
  const auto before=owner->snapshot(); const auto history=owner->undo_steps();
  auto request=all(); std::vector<PlanePosition> expected;
  if (std::string(mode)=="current") {
    request=TargetRequest{}; request.roi=RoiPolicy::WholePlane; expected={{1,2,2}};
  } else if (std::string(mode)=="mixed") {
    request.c=AxisRange{0,3,2}; request.z=AxisRange{1,4,2};
    for (int t:{0,1}) for (int c:{0,2}) for (int z:{1,3}) expected.push_back({t,c,z});
  } else if (std::string(mode)=="positions") {
    request=TargetRequest{}; request.scope=TargetScope::Positions; request.roi=RoiPolicy::WholePlane;
    request.positions={{1,2,3},{0,0,0},{1,1,2}}; expected=request.positions;
  } else {
    for(int t=0;t<2;++t) for(int c=0;c<3;++c) for(int z=0;z<4;++z) expected.push_back({t,c,z});
  }
  const auto command=resolve_gaussian_command(before,request,{1,false});
  require(command.target().position_count()==expected.size(),"SDK full resolved target count");
  for(std::size_t i=0;i<expected.size();++i)
    require(same(command.target().position(i),expected[i]),"SDK independently ordered TCZ target");
  const auto record=command.record();
  auto view=before.display(); view.t=0; view.c=1; view.crosshair={4,5,0}; view.zoom=2.5;
  owner->set_display(view);
  const auto decoded=gaussian_command_from_record(owner->snapshot(),record);
  require(metadata_equal(decoded.record(),record),"SDK strict replay changed accepted command");
  JobOptions options; options.retain_result_resources=true;
  auto job=app.submit("gaussian_blur_imagej",decoded.target(),decoded.parameters(),{},options);
  require(job.wait_for(std::chrono::seconds(15)),"SDK bounded Gaussian job did not finish");
  require(job.info().state==JobState::Ready,"SDK Gaussian job not Ready");
  const auto prepared=job.prepared();
  pixels(prepared.preview(),expected); pixels(owner->snapshot(),{});
  require(owner->snapshot().shares_data(before) && owner->undo_steps()==history && !owner->snapshot().dirty(),
          "SDK preparation mutated scientific owner");
  for(int t=0;t<2;++t) for(int c=0;c<3;++c) for(int z=0;z<4;++z) {
    const PlanePosition p{t,c,z};
    if(!selected(expected,p))
      require(prepared.preview().data().dataset.plane(p)==before.data().dataset.plane(p),
              "SDK untargeted immutable plane lost identity");
  }
  const auto published=app.publish(job);
  require(published.document_id==owner->id && !published.created,"SDK scientific Apply published another owner");
  require(owner->undo_steps()==history+1 && owner->snapshot().dirty(),"SDK full Apply did not create exactly one history entry");
  pixels(owner->snapshot(),expected);
  require(owner->snapshot().data().metadata==before.data().metadata,"SDK scientific metadata changed");
  const auto &cal=owner->snapshot().data().dataset.calibration();
  require(cal.spacing==calibration().spacing && cal.origin==calibration().origin &&
          cal.direction==calibration().direction && cal.unit=="um" && cal.time(1)==8.25,
          "SDK scientific calibration/time changed");
  require(owner->snapshot().display().t==0 && owner->snapshot().display().c==1 &&
          owner->snapshot().display().crosshair==view.crosshair && owner->snapshot().display().pan==view.pan &&
          owner->snapshot().display().zoom==2.5,"SDK Apply changed concurrent cameras/position");
  app.undo(); pixels(owner->snapshot(),{});
  require(owner->undo_steps()==history && !owner->snapshot().dirty() && owner->snapshot().display().zoom==2.5,
          "SDK native scientific undo changed save/camera identity");
  app.redo(); pixels(owner->snapshot(),expected);
  rejects<RevisionConflict>([&]{gaussian_command_from_record(owner->snapshot(),record);},"SDK stale strict replay accepted");
  SessionLimits limits; limits.max_documents=16;
  limits.max_encoded_bytes=limits.max_json_bytes=32*1024*1024; limits.max_working_bytes=128*1024*1024;
  const auto session=decode_session(encode_session({{owner->snapshot()},{},{metadata_object(record)},{}},limits),limits);
  const auto reopened=session.documents.at(0);
  require(reopened->id!=owner->id && reopened->undo_steps()==0 && !reopened->snapshot().dirty(),
          "SDK session did not create a fresh clean owner");
  pixels(reopened->snapshot(),expected);
  require(session.workflow.size()==1 && metadata_equal(session.workflow.at(0),metadata_object(record)),
          "SDK session lost full accepted Gaussian command");
  TargetRequest rebind; rebind.scope=TargetScope::Positions; rebind.roi=RoiPolicy::WholePlane;
  rebind.positions={{0,0,1}};
  const auto rebound=rebind_gaussian_command(reopened->snapshot(),record,rebind);
  require(rebound.target().document_id()==reopened->id && rebound.target().position_count()==1 &&
          same(rebound.target().position(0),{0,0,1}) && rebound.options().sigma==1,
          "SDK explicit rebind reused old source/ranges");
}
void roiAndFailures() {
  Application app;
  auto owner=std::make_shared<Document>(fixture(PixelType::UInt16)); app.adopt(owner);
  captureView(*owner);
  auto edit=owner->begin_edit();
  edit.set_rois({{"explicit zero",Selection("rectangle",{{7,2},{17,15}}),0,0,0,{}}});
  owner->commit(edit); owner->mark_saved();
  const auto before=owner->snapshot(); const auto history=owner->undo_steps();
  TargetRequest none; none.scope=TargetScope::Positions; none.roi=RoiPolicy::PositionedRois;
  none.positions={{1,2,2}};
  const auto empty=resolve_gaussian_command(before,none,{1,false});
  auto prepared=app.prepare("gaussian_blur_imagej",empty.target(),empty.parameters());
  require(prepared.outputs().size()==1 && prepared.outputs()[0].skipped,"SDK unmatched positioned ROI became a whole-plane edit");
  app.publish(prepared);
  require(owner->snapshot().shares_data(before) && owner->undo_steps()==history && !owner->snapshot().dirty(),
          "SDK empty ROI no-op changed history");
  auto invalid=all(); invalid.z=AxisRange{0,4,0};
  rejects([&]{resolve_gaussian_command(before,invalid,{1,false});},"SDK zero-step range accepted");
  invalid=all(); invalid.mode=ExecutionMode::Volume;
  rejects([&]{resolve_gaussian_command(before,invalid,{1,false});},"SDK Gaussian volume accepted");
  invalid=TargetRequest{}; invalid.scope=TargetScope::Positions; invalid.positions={{1,2,2},{1,2,2}};
  rejects([&]{resolve_gaussian_command(before,invalid,{1,false});},"SDK duplicate explicit positions accepted");
  auto unsupported=std::make_shared<Document>(fixture(PixelType::Float64));
  rejects([&]{resolve_gaussian_command(unsupported->snapshot());},"SDK Gaussian Float64 accepted");
  unsupported=std::make_shared<Document>(fixture(PixelType::UInt8,4));
  rejects([&]{resolve_gaussian_command(unsupported->snapshot());},"SDK Gaussian RGBA accepted");
  auto foreign=std::make_shared<Document>(fixture(PixelType::UInt16));
  const auto foreignCommand=resolve_gaussian_command(foreign->snapshot());
  rejects<DocumentUnavailable>([&]{app.submit("gaussian_blur_imagej",foreignCommand.target(),foreignCommand.parameters());},
                               "SDK foreign application owner accepted");
  require(owner->snapshot().shares_data(before) && owner->undo_steps()==history && app.document_ids().size()==1,
          "SDK refusal changed data/history/membership");
  const auto zero=resolve_gaussian_command(before,all(),{0,false});
  prepared=app.prepare("gaussian_blur_imagej",zero.target(),zero.parameters()); app.publish(prepared);
  require(owner->snapshot().shares_data(before) && owner->undo_steps()==history,"SDK zero-sigma full-target no-op changed owner");
  pixels(owner->snapshot(),{});
}
void floatScientificTargets() {
  Application app;
  auto owner=std::make_shared<Document>(fixture(PixelType::Float32),"SDK scientific Float32");
  app.adopt(owner); captureView(*owner);
  const auto before=owner->snapshot();
  auto request=all(); request.t=AxisRange{1,2,1}; request.c=AxisRange{0,3,2};
  request.z=AxisRange{0,4,2};
  const std::vector<PlanePosition> expected={{1,0,0},{1,0,2},{1,2,0},{1,2,2}};
  const auto command=resolve_gaussian_command(before,request,{1,false});
  JobOptions options; options.retain_result_resources=true;
  auto job=app.submit("gaussian_blur_imagej",command.target(),command.parameters(),{},options);
  require(job.wait_for(std::chrono::seconds(15)) && job.info().state==JobState::Ready,
          "SDK Float32 target job did not become Ready within bound");
  // Independent analytic convolution of the literal nearest-edge step with
  // exp(-k^2/2), normalized in double over [-32,32]. Beyond that radius the
  // mathematical tail is below 1e-220. ImageJ's finite coefficient cutoff and
  // Float32 two-pass accumulation differ from this ideal by <1e-4 here; the
  // tolerance is absolute sample error, not agreement with another product call.
  double norm=0; std::array<double,23> response{};
  for(int k=-32;k<=32;++k) {
    const double weight=std::exp(-.5*double(k)*double(k)); norm+=weight;
    for(int x=0;x<23;++x) if(x+k>=11) response[x]+=100*weight;
  }
  for(auto &value:response) value/=norm;
  const auto prepared=job.prepared();
  for(int t=0;t<2;++t) for(int c=0;c<3;++c) for(int z=0;z<4;++z) {
    const PlanePosition p{t,c,z}; const bool changed=selected(expected,p);
    const auto plane=prepared.preview().data().dataset.plane(p);
    require(plane->type==PixelType::Float32,"SDK Float32 operation changed declared type");
    if(!changed)
      require(plane==before.data().dataset.plane(p),"SDK Float32 untargeted plane identity changed");
    for(int y=0;y<17;++y) for(int x=0;x<23;++x) {
      const double wanted=changed ? 48*t+16*c+4*z+response[x] : literal(p,x,0,false);
      require(changed ? std::abs(plane->at(x,y)-wanted)<=1e-4 : plane->at(x,y)==wanted,
              "SDK independent analytic Float32 step sample");
    }
  }
  require(owner->snapshot().shares_data(before) && owner->undo_steps()==0 && !owner->snapshot().dirty(),
          "SDK Float32 preparation edited source");
  app.publish(job);
  require(owner->undo_steps()==1 && owner->snapshot().dirty(),"SDK Float32 full target history");
  app.undo();
  require(owner->snapshot().shares_data(before) && !owner->snapshot().dirty(),"SDK Float32 undo identity");
  app.redo();
  require(owner->undo_steps()==1 && owner->snapshot().data().dataset.pixel_type()==PixelType::Float32,
          "SDK Float32 redo/type");
}
} // namespace

int main() {
  try {
    for(const auto mode:{"current","mixed","all","positions"}) {
      exercise(PixelType::UInt8,1,mode);
      exercise(PixelType::UInt16,1,mode);
      exercise(PixelType::UInt8,3,mode);
    }
    roiAndFailures();
    floatScientificTargets();
    std::cout<<"PASS scientific operation targets SDK checks="<<checks<<'\n';
    return 0;
  } catch(const std::exception &error) {
    std::cerr<<error.what()<<'\n'; return 1;
  }
}
