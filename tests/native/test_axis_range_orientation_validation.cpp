#include <imagejxx/application.hpp>
#include <array>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <string_view>

using namespace imagejxx;
namespace {
void check(bool condition,const char *message) {
  if (!condition) throw std::runtime_error(message);
}
template <class Error,class Function>
void rejects(Function function,const char *message) {
  bool rejected=false;
  try {function();} catch (const Error &) {rejected=true;}
  check(rejected,message);
}
constexpr auto quiet=FloatingSamplePolicy::PreserveQuietNaNFloat32;
constexpr std::string_view validation="Immutable plane sample validation";
Dataset input(PixelType type,FloatingSamplePolicy policy) {
  Stack planes;
  const std::array<std::uint32_t,8> bits{0,0x80000000u,1,0x007fffffu,
      0x7fc12345u,0xffc34567u,0x3f800000u,0xbf800000u};
  for (int z=0;z<3;++z) {
    Image image(4097,2,1,type);
    for (std::size_t i=0;i<image.pixels.size();++i) {
      const int value=int((i+17*std::size_t(z))%251);
      if (policy==quiet) image.pixels[i]=float32_source_sample(bits[(i+17*z)%8],policy);
      else if (type==PixelType::UInt16) image.pixels[i]=40000+value;
      else if (type==PixelType::Float32) image.pixels[i]=value-125.25;
      else if (type==PixelType::Float64) image.pixels[i]=value-125.125;
      else image.pixels[i]=value;
    }
    planes.push_back(std::move(image));
  }
  return Dataset::from_planes({1,1,3,2,4097,1},std::move(planes),{},policy);
}
void unchanged(const Document &source,const DocumentSnapshot &before,
               const HistoryInfo &history,const Application &app,
               const std::vector<std::uint64_t> &ids,std::uint64_t active) {
  const auto after=source.snapshot();
  check(after.same_document(before) && after.shares_data(before) &&
        after.data_identity()==before.data_identity() && after.dirty()==before.dirty() &&
        after.revisions().matches(before.revisions(),Change::All) &&
        source.history().undo_steps==history.undo_steps && source.history().redo_steps==history.redo_steps &&
        app.document_ids()==ids && app.active_document_id()==active,
        "Rejected preparation changed source/data/history/membership/active context");
}
void positive_copy_and_final_validation_cancel() {
  const std::array<std::pair<PixelType,FloatingSamplePolicy>,5> policies{{
      {PixelType::UInt8,FloatingSamplePolicy::FiniteOnly},
      {PixelType::UInt16,FloatingSamplePolicy::FiniteOnly},
      {PixelType::Float32,FloatingSamplePolicy::FiniteOnly},
      {PixelType::Float64,FloatingSamplePolicy::FiniteOnly},{PixelType::Float32,quiet}}};
  for (auto profile:policies) for (int phase=0;phase<3;++phase) {
    Application app; auto source=std::make_shared<Document>(input(profile.first,profile.second),"Cancel source");
    app.adopt(source); const auto before=source->snapshot(); const auto history=source->history();
    const auto ids=app.document_ids(); const auto active=app.active_document_id();
    auto token=std::make_shared<CancellationToken>();
    std::size_t copies=0, positive=0, previous=0, total_work=0;
    ExecutionControl control; control.cancellation=token;
    control.progress=[&](std::size_t done,std::size_t total){
      check(done>=previous && done<=total,"Cancellation path progress was not monotonic");
      previous=done; total_work=total;};
    control.stage_progress=[&](std::string_view stage,std::size_t done,std::size_t total) {
      if (stage=="axis_range_orientation_copy" && done==total) ++copies;
      const bool extraction=phase==0 && stage=="plane-extract.copy" && done>=1024;
      const bool orientation=phase==1 && stage=="axis_range_orientation_copy" && done>=1024;
      const bool final=phase==2 && stage==validation && copies==2 && done>=1024;
      if (extraction || orientation || final) {positive=done;token->cancel();}
    };
    AxisRangeOrientationRequest request{{Plane::XY,0,1,1},true,true,true};
    rejects<OperationCancelled>([&]{app.prepare_axis_range_orientation(request,source->id,control);},
                                "Positive extraction/orientation/final-validation cancellation failed");
    check(positive>=1024 && previous<total_work && (phase!=2 || copies==2),
          "Cancellation happened before real work or after premature completion");
    unchanged(*source,before,history,app,ids,active);
  }
  std::cout<<"G029.orientation.validation.V01_positive_extract_copy_final_validation_cancel\n";
}
void checkpoint_exceptions_and_completion() {
  Application app; auto source=std::make_shared<Document>(input(PixelType::Float64,FloatingSamplePolicy::FiniteOnly),"Callback");
  app.adopt(source); const auto before=source->snapshot(); const auto history=source->history();
  const auto ids=app.document_ids(); const auto active=app.active_document_id();
  for (int phase=0;phase<2;++phase) {
    ExecutionControl control; std::size_t copies=0; bool threw_after_work=false;
    control.stage_progress=[&](std::string_view stage,std::size_t done,std::size_t total) {
      if (stage=="axis_range_orientation_copy" && done==total) ++copies;
      if ((phase==0 && stage=="axis_range_orientation_copy" && done>=1024) ||
          (phase==1 && stage==validation && copies==2 && done>=1024)) {
        threw_after_work=true;throw std::runtime_error("Owned callback failure");
      }
    };
    rejects<std::runtime_error>([&]{app.prepare_axis_range_orientation({{Plane::XY,0,1,1},true,true,true},source->id,control);},
                                "Owned positive-work callback exception was swallowed");
    check(threw_after_work,"Callback rejection did not exercise positive work");
    unchanged(*source,before,history,app,ids,active);
  }
  std::size_t copies=0, validations=0, previous=0; bool completed=false;
  ExecutionControl control;
  control.stage_progress=[&](std::string_view stage,std::size_t done,std::size_t total) {
    if (stage=="axis_range_orientation_copy" && done==total) ++copies;
    if (stage==validation && copies==2 && done==total) ++validations;
  };
  control.progress=[&](std::size_t done,std::size_t total) {
    check(total==5 && done>=previous && done<=total,"XY orientation progress total differs");
    if (done==total) {check(validations==2,"Completion preceded both final immutable validations");completed=true;}
    previous=done;
  };
  auto prepared=app.prepare_axis_range_orientation({{Plane::XY,0,1,1},true,true,true},source->id,control);
  check(completed && copies==2 && validations==2 && !app.contains(prepared.prepared_new_document()->id),
        "Preparation did not finish validation before staging an unregistered owner");
  unchanged(*source,before,history,app,ids,active);
  app.publish(prepared);
  std::cout<<"G029.orientation.validation.V02_callback_atomicity_completion_after_validation\n";
}
void geometry_and_pre_cancellation() {
  auto source=input(PixelType::UInt8,FloatingSamplePolicy::FiniteOnly);
  auto token=std::make_shared<CancellationToken>(); token->cancel();
  rejects<OperationCancelled>([&]{reslice_oriented_axis_range(source,{}, {token});},
                              "Pre-canceled orientation was admitted");
  auto cal=source.calibration();cal.origin={1e100,-1e100,1e100};
  cal.spacing={.4,1.2,3};cal.direction={1,.25,-.5,.5,-1,.125,0,.75,1.5};
  const auto result=reslice_oriented_axis_range(source.with_calibration(cal),{{Plane::XZ,0,1,1},true,true,true});
  check(result.first_plane_geometry.horizontal_step==std::array<double,3>{1.5,-.375,-4.5} &&
        result.first_plane_geometry.vertical_step==std::array<double,3>{.4,.2,0},
        "Large world origins canceled signed direct orientation basis");
  std::cout<<"G029.orientation.validation.V03_pre_cancel_large_origin_direct_basis\n";
}
}
int main() {
  try {positive_copy_and_final_validation_cancel();checkpoint_exceptions_and_completion();
       geometry_and_pre_cancellation();return 0;}
  catch (const std::exception &error) {std::cerr<<error.what()<<'\n';return 1;}
}
