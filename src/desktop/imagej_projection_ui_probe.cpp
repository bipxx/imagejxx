// Actual shared ImGui physical inputs. Inputs/observations only: independent
// pixel, record, memory and metadata assertions live in the Python reader.
#include "imagej_projection_ui_probe.hpp"
#include "headless_session_internal.hpp"
#include "gaussian_cancellation_internal.hpp"
#include "window.hpp"
#include <imagejxx/session.hpp>
#include <imagejxx/channel_display.hpp>
#include <chrono>
#include <thread>
#include <condition_variable>
#include <mutex>
namespace imagejxx::desktop {
namespace {
void require(bool v,const char* message){if(!v)throw std::runtime_error(message);}
Json state(Window& w) {
  const auto s=w.nativeSnapshot();const auto& d=s.data();const auto shape=d.dataset.shape();
  Json pixels=Json::array();
  for(int t=0;t<shape.t;++t)for(int c=0;c<shape.c;++c)for(int z=0;z<shape.z;++z)
    pixels.push_back(d.dataset.plane({t,c,z})->pixels);
  Json overlays=Json::array();for(const auto& o:d.overlays)overlays.push_back(metadataJson(o));
  return {{"shape",shape.dimensions()},{"pixels",pixels},{"title",d.metadata.title},
    {"properties",metadataJson(d.metadata.properties)},{"presentation",metadataJson(s.display().properties)},
    {"selection",bool(d.selection)},{"previous_selection",bool(d.previous_selection)},
    {"rois",d.rois.size()},{"overlays",overlays},{"document_count",w.documents().size()},
    {"workflow",w.workflow},{"undo",w.nativeDocument(s.id())->undo_steps()}};
}
}
Json imagejProjectionUiCase(Window& w,const std::string& context,const std::string& output) {
  namespace fs=std::filesystem;const auto directory=fs::absolute(fs::u8path(output));
  require(!fs::exists(directory),"Projection UI output must be fresh");fs::create_directories(directory);
  Stack planes;
  for(int t=0;t<2;++t)for(int c=0;c<2;++c)for(int z=0;z<4;++z){
    Image image(4,3,1,PixelType::UInt16);
    for(int y=0;y<3;++y)for(int x=0;x<4;++x)image.at(x,y)=100*t+10*c+2*z+x+y;
    planes.push_back(std::move(image));
  }
  DocumentData data{Dataset::from_planes({2,2,4,3,4,1},std::move(planes)),{},{},{},{},{},{}};
  data.metadata.title="projection-ui";data.metadata.properties["note"]=std::string("source only");
  data.selection=Selection("rectangle",{{0,0},{2,2}});data.previous_selection=data.selection;
  auto overlay=[](const char* name){
    auto geometry=with_roi_style(Selection("rectangle",{{0,0},{2,2}}),
        {{"stroke_color",std::string("#123456")},{"stroke_width",1.0}});
    auto value=selectionJson(geometry);value["name"]=name;value["color"]="#123456";
    return metadataObjectFromJson(value);
  };
  auto global=overlay("global"),inside=overlay("inside"),outside=overlay("wild-c-out-z");
  inside["t"]=1;inside["c"]=1;inside["z"]=2;outside["z"]=0;
  data.overlays={global,inside,outside};
  auto owner=std::make_shared<imagejxx::Document>(std::move(data));
  auto display=owner->snapshot().display();display.t=1;display.c=1;display.crosshair={1,1,2};
  ChannelDisplayState channels;channels.channels.resize(2);
  channels.channels[0].lut.name="Red";channels.channels[0].range={11,111};
  channels.channels[1].lut.name="Green";channels.channels[1].range={22,222};channels.channels[1].visible=false;
  display=with_channel_display(display,owner->snapshot().data().dataset,channels);owner->set_display(display);
  w.addNativeDocument(owner);w.recording=true;
  Json report{{"contract","g034.imagej-projection-ui.001"},{"context",context},{"acceptance_claim",false},
    {"frames",Json::array()},{"controls",Json::array()},
    {"api_fixture_actions",{"source attachment","tab navigation","session Save/Open"}}};
  struct FailureCapture {
    Json& report;const fs::path& directory;bool done=false;
    ~FailureCapture(){if(!done)try{writeJson((directory/"failure.json").u8string(),report);}catch(...){}}
  } failure{report,directory};
  HeadlessInputSession session(w);Json last;
  auto frame=[&](const Json& events=Json::array()) {
    require(report["frames"].size()<240,"Projection UI frame bound");last=session.frame(events);
    // Keep physical input and form/error observations; avoid repeating full
    // texture/host diagnostics in every frame. Final render is retained below.
    Json observed{{"input_frame",last.at("input_frame")},{"dialog",last.at("dialog")},
      {"error_modal",last.at("error_modal")},{"window_error",last.at("window_error")}};
    report["frames"].push_back({{"events",events},{"rendered",std::move(observed)}});
  };
  auto click=[&](const Json& control,const std::string& name,bool after=true) {
    require(control.at("enabled").get<bool>() && control.at("id").get<std::uint64_t>()>0,"Projection control unavailable");
    report["controls"].push_back({{"name",name},{"control",control}});
    frame(Json::array({{{"type","position"},{"space","screen"},{"point",control.at("point")}}}));
    frame(Json::array({{{"type","button"},{"name","left"},{"down",true}}}));
    frame(Json::array({{{"type","button"},{"name","left"},{"down",false}}}));
    if(after)frame();
  };
  auto edit=[&](const char* field,const char* text) {
    click(last.at("dialog").at("controls").at(field),field);
    for(bool down:{true,false})frame(Json::array({{{"type","keys"},{"keys",{{{"name","Ctrl"},{"down",down}},{{"name","A"},{"down",down}}}}}}));
    frame(Json::array({{{"type","text"},{"value",text}}}));frame();
  };
  auto choose=[&](const char* field,const char* option) {
    click(last.at("dialog").at("controls").at(field),field);
    click(last.at("dialog").at("controls").at(std::string(field)+".choice."+option),
        std::string(field)+".choice."+option);
  };
  auto menu=[&](const char* action) {
    click(last.at("host_controls").at("menu.Image"),"menu.Image");
    click(last.at("host_controls").at("menu.Image.Stacks"),"menu.Image.Stacks");
    click(last.at("host_controls").at(std::string("menu.action.")+action),action);
  };
  frame();frame();const auto source=owner->snapshot();report["before"]=state(w);
  menu("show_imagej_projection");edit("first","2");edit("last","4");
  click(last.at("dialog").at("controls").at("all_time_frames"),"all_time_frames");
  click(last.at("dialog").at("controls").at("apply"),"apply",false);
  const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(10);
  while(w.jobInfo() && (w.jobInfo()->state==JobState::Queued || w.jobInfo()->state==JobState::Running)) {
    require(std::chrono::steady_clock::now()<deadline,"Projection worker timeout");
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  const auto ready=w.jobInfo();require(bool(ready),"Projection job missing after Apply");
  const auto usage=GaussianCancellationAccess::scheduler(w)->usage();
  report["ready"]={{"state",int(ready->state)},{"resource_bytes",ready->resource_bytes},
    {"peak_reserved_bytes",ready->peak_reserved_bytes},{"retained_bytes",usage.retained_bytes},
    {"documents",w.documents().size()}};
  w.wait();frame();frame();report["applied"]=state(w);
  report["applied_render"]=last.at("ordinary_canvas");
  report["source_data_unchanged"]=owner->snapshot().shares_data(source);
  report["source_revisions_unchanged"]=owner->snapshot().revisions().matches(source.revisions(),Change::All);
  report["reserved_after_publish"]=GaussianCancellationAccess::scheduler(w)->usage().reserved_bytes;
  w.setActiveIndex(0);frame();menu("show_imagej_grouped_projection");
  report["grouped_hyperstack_dialog"]=last.at("dialog");
  click(last.at("dialog").at("controls").at("cancel"),"cancel");report["after_cancel"]=state(w);
  click(last.at("host_controls").at("workflow.projection.0.replay"),"replay",false);
  w.wait();frame();frame();report["replayed"]=state(w);
  const auto reboundSource=w.nativeSnapshot();
  click(last.at("host_controls").at("workflow.projection.0.rebind"),"rebind");
  report["rebind_dialog"]=last.at("dialog");
  edit("first","1");edit("last","2");
  click(last.at("dialog").at("controls").at("apply"),"rebind_apply",false);
  w.wait();frame();frame();report["rebound"]=state(w);
  report["rebind_source_unchanged"]=w.nativeDocument(reboundSource.id())->snapshot().shares_data(reboundSource);
  w.setActiveIndex(2);frame();menu("show_imagej_grouped_projection");
  edit("group_size","0");
  click(last.at("dialog").at("controls").at("apply"),"invalid_group_apply");
  report["invalid_group"]={{"state",state(w)},{"error",last.at("error_modal")},{"busy",w.busy()}};
  frame();frame(); // Let the newly opened error popup finish its initial layout.
  click(last.at("error_modal").at("controls").at("ok"),"error_ok");
  edit("group_size","2");
  click(last.at("dialog").at("controls").at("apply"),"group_apply",false);
  w.wait();frame();frame();report["grouped"]=state(w);
  for(const auto* axis:{"time","channels"}) {
    w.setActiveIndex(0);frame();menu("show_imagej_projection");
    choose("axis",axis);edit("last","2");
    report[std::string(axis)+"_dialog"]=last.at("dialog");
    click(last.at("dialog").at("controls").at("apply"),std::string(axis)+"_apply",false);
    w.wait();frame();frame();report[std::string(axis)+"_output"]=state(w);
  }
  report["axis_source_unchanged"]=owner->snapshot().shares_data(source) &&
      owner->snapshot().revisions().matches(source.revisions(),Change::All);
  Stack rgbPlanes;
  for(int z=0;z<2;++z){
    Image image(1,1,3,PixelType::UInt8);
    image.at(0,0,0)=200;image.at(0,0,1)=100;image.at(0,0,2)=20;
    rgbPlanes.push_back(std::move(image));
  }
  DocumentData rgbData{Dataset::from_planes({1,1,2,1,1,3},std::move(rgbPlanes)),{},{},{},{},{},{}};
  rgbData.metadata.title="rgb-projection";
  auto rgbOwner=std::make_shared<imagejxx::Document>(std::move(rgbData));
  const auto rgbSource=rgbOwner->snapshot();w.addNativeDocument(rgbOwner);frame();frame();
  menu("show_imagej_projection");choose("method","sum");
  click(last.at("dialog").at("controls").at("clip_when_summing"),"clip_when_summing");
  report["rgb_dialog"]=last.at("dialog");
  click(last.at("dialog").at("controls").at("apply"),"rgb_apply",false);
  w.wait();frame();frame();report["rgb_output"]=state(w);
  report["rgb_source_unchanged"]=rgbOwner->snapshot().shares_data(rgbSource) &&
      rgbOwner->snapshot().revisions().matches(rgbSource.revisions(),Change::All);
  session.close();report["lifecycle"]=session.lifecycle();
  w.setActiveIndex(1);SessionCapture capture;capture.documents.push_back(w.nativeSnapshot());
  for(const auto& step:w.workflow)capture.workflow.push_back(metadata_object(metadataObjectFromJson(step)));
  save_session((directory/"result.ijx").u8string(),capture);w.openFile((directory/"result.ijx").u8string());
  report["reopened"]=state(w);report["error"]=w.error();report["state"]="observed";
  require(report.dump().size()<16*1024*1024,"Projection report byte limit");
  writeJson((directory/"case.json").u8string(),report);failure.done=true;return report;
}
Json intensityCalibrationUiCase(Window& w,const std::string& context,const std::string& output,bool nonlinear_fit) {
  namespace fs=std::filesystem;const auto directory=fs::absolute(fs::u8path(output));
  require(!fs::exists(directory),"Calibration UI output must be fresh");fs::create_directories(directory);
  Stack planes;for(int z=0;z<2;++z){Image image(3,1,1,PixelType::UInt16);
    image.pixels={0,32768,65535};planes.push_back(std::move(image));}
  DocumentData data{Dataset::from_planes({1,1,2,1,3,1},std::move(planes)),{},{},{},{},{},{}};
  data.metadata.title="intensity-ui";
  auto owner=std::make_shared<imagejxx::Document>(std::move(data));w.addNativeDocument(owner);
  const auto original=owner->snapshot();
  Json report{{"contract",nonlinear_fit?"intensity-nonlinear-ui.001":"intensity-calibration-ui.001"},{"context",context},{"acceptance_claim",false},
    {"frames",Json::array()},{"controls",Json::array()},
    {"api_fixture_actions",{"source attachment","tab navigation","session Save/Open"}}};
  struct FailureCapture {Json& report;const fs::path& directory;bool done=false;
    ~FailureCapture(){if(!done)try{writeJson((directory/"failure.json").u8string(),report);}catch(...){}}
  } failure{report,directory};
  HeadlessInputSession session(w);Json last;
  auto frame=[&](const Json& events=Json::array()) {
    require(report["frames"].size()<256,"Calibration UI frame bound");last=session.frame(events);
    report["frames"].push_back({{"events",events},{"rendered",{
      {"input_frame",last.at("input_frame")},{"dialog",last.at("dialog")},
      {"error_modal",last.at("error_modal")},{"window_error",last.at("window_error")}}}});
  };
  auto click=[&](Json control,const std::string& name,bool after=true) {
    require(control.at("enabled").get<bool>() && control.at("id").get<std::uint64_t>()>0,"Calibration control unavailable");
    report["controls"].push_back({{"name",name},{"control",control}});
    frame(Json::array({{{"type","position"},{"space","screen"},{"point",control.at("point")}}}));
    frame(Json::array({{{"type","button"},{"name","left"},{"down",true}}}));
    frame(Json::array({{{"type","button"},{"name","left"},{"down",false}}}));if(after)frame();
  };
  auto menu=[&](const char* action){
    click(last.at("host_controls").at("menu.Analyze"),"menu.Analyze");
    click(last.at("host_controls").at(std::string("menu.action.")+action),action);
  };
  auto choose=[&](const char* field,const char* option){
    click(last.at("dialog").at("controls").at(field),field);
    click(last.at("dialog").at("controls").at(std::string(field)+".choice."+option),std::string(field)+".choice."+option);
  };
  auto editText=[&](const char* field,const char* value){
    click(last.at("dialog").at("controls").at(field),field);
    for(bool down:{true,false})frame(Json::array({{{"type","keys"},{"keys",{{{"name","Ctrl"},{"down",down}},{{"name","A"},{"down",down}}}}}}));
    frame(Json::array({{{"type","text"},{"value",value}}}));frame();
  };
  auto history=[&](const char* name){
    click(last.at("host_controls").at("menu.Edit"),"menu.Edit");
    click(last.at("host_controls").at(std::string("menu.Edit.")+name),name);
  };
  frame();frame();report["before"]=state(w);menu("show_intensity_calibration");
  choose("mode","signed16");editText("value_unit","HU");
  click(last.at("dialog").at("controls").at("apply"),"calibration_apply");report["applied"]=state(w);
  history("Undo");report["undone"]=state(w);history("Redo");report["redone"]=state(w);
  menu("show_intensity_calibration");report["reopened_dialog"]=last.at("dialog");editText("value_unit","discarded");
  click(last.at("dialog").at("controls").at("cancel"),"calibration_cancel");report["cancelled"]=state(w);
  menu("show_intensity_calibration");choose("mode","none");editText("value_unit","Gray Value");
  click(last.at("dialog").at("controls").at("apply"),"calibration_clear");report["cleared"]=state(w);
  history("Undo");report["restored"]=state(w);
  report["source_pixels_unchanged"]=owner->snapshot().data().dataset.shares_pixels(original.data().dataset) &&
      owner->snapshot().revisions().pixels==original.revisions().pixels;
  const auto calibrated=owner->snapshot();
  click(last.at("host_controls").at("menu.Image"),"menu.Image");
  click(last.at("host_controls").at("menu.Image.Stacks"),"menu.Image.Stacks");
  click(last.at("host_controls").at("menu.action.show_imagej_projection"),"show_imagej_projection");
  choose("method","sum");click(last.at("dialog").at("controls").at("apply"),"projection_apply");
  w.wait();frame();frame();report["projected"]=state(w);
  report["projection_source_unchanged"]=owner->snapshot().shares_data(calibrated);
  w.setActiveIndex(0);frame();menu("show_intensity_function");editText("coefficients","[5,2]");
  click(last.at("dialog").at("controls").at("zero_clip"),"zero_clip");
  click(last.at("dialog").at("controls").at("apply"),"function_apply");report["function_applied"]=state(w);
  menu("show_intensity_function");report["function_dialog"]=last.at("dialog");
  click(last.at("dialog").at("controls").at("cancel"),"function_cancel");
  const auto functionSnapshot=owner->snapshot();
  const auto fitAction=nonlinear_fit?"show_intensity_nonlinear_fit":"show_intensity_polynomial_fit";
  menu(fitAction);
  if(nonlinear_fit)editText("initial_coefficients","[4,0.5]");else choose("function","poly2");
  editText("measured","[0,1,2]");editText("known","[5,7]");
  click(last.at("dialog").at("controls").at("apply"),"invalid_fit_apply");
  report["invalid_fit"]={{"state",state(w)},{"error",last.at("error_modal")},{"busy",w.busy()}};
  frame();frame();click(last.at("error_modal").at("controls").at("ok"),"fit_error_ok");
  editText("known",nonlinear_fit?"[5,10,20]":"[1,4,9]");
  click(last.at("dialog").at("controls").at("apply"),"fit_apply",false);
  const auto fitDeadline=std::chrono::steady_clock::now()+std::chrono::seconds(10);
  while(w.jobInfo() && (w.jobInfo()->state==JobState::Queued || w.jobInfo()->state==JobState::Running)) {
    require(std::chrono::steady_clock::now()<fitDeadline,"Fit worker timeout");std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  const auto fitReady=w.jobInfo();require(bool(fitReady),"Fit job missing after physical Apply");
  const auto fitUsage=GaussianCancellationAccess::scheduler(w)->usage();
  report["fit_ready"]={{"state",int(fitReady->state)},{"resource_bytes",fitReady->resource_bytes},
    {"retained_bytes",fitUsage.retained_bytes},{"stage",fitReady->stage},
    {"stage_completed",fitReady->stage_completed},{"stage_total",fitReady->stage_total},
    {"source_unchanged",owner->snapshot().shares_data(functionSnapshot)}};
  w.wait();frame();frame();report["fitted"]=state(w);
  report["fit_reserved_after_publish"]=GaussianCancellationAccess::scheduler(w)->usage().reserved_bytes;
  report["fit_status"]=last.at("status");
  menu(fitAction);report["fit_dialog"]=last.at("dialog");
  click(last.at("dialog").at("controls").at("cancel"),"fit_cancel");
  const auto fittedSnapshot=owner->snapshot();

  // A one-shot private gate holds the genuine fit job before computation.
  // It makes Running observable without slowing normal fits or claiming a
  // numerical-stage timing measurement. Only pointer/button events cancel it.
  struct FitGate {
    std::mutex mutex;std::condition_variable changed;bool entered=false,released=false;
    void hold(const JobContext& context) {
      std::unique_lock<std::mutex> lock(mutex);entered=true;changed.notify_all();
      if(!changed.wait_for(lock,std::chrono::seconds(10),[&]{return released;}))
        throw std::runtime_error("Fit cancellation fixture release timeout");
      lock.unlock();context.checkpoint();
    }
    void release(){std::lock_guard<std::mutex> lock(mutex);released=true;changed.notify_all();}
  };
  auto gate=std::make_shared<FitGate>();
  struct FitGateCleanup {
    Window& window;std::shared_ptr<FitGate> gate;
    ~FitGateCleanup(){try{window.cancel();gate->release();window.wait();
      GaussianCancellationAccess::setNextPreparationGate(window,{});}catch(...){}}
  } gateCleanup{w,gate};
  report["api_fixture_actions"].push_back("one-shot pre-compute fit worker barrier");
  menu(fitAction);
  GaussianCancellationAccess::setNextPreparationGate(w,[gate](const JobContext& context){gate->hold(context);});
  click(last.at("dialog").at("controls").at("apply"),"fit_cancel_apply",false);
  {
    std::unique_lock<std::mutex> lock(gate->mutex);
    require(gate->changed.wait_for(lock,std::chrono::seconds(5),[&]{return gate->entered;}),"Fit worker did not enter cancellation fixture");
  }
  const auto running=w.jobInfo();require(running && running->state==JobState::Running,"Fit cancellation requires running job");
  frame();report["fit_running_cancel"]={{"running_state",int(running->state)},{"job_id",running->id},
    {"resource_bytes",running->resource_bytes},{"reserved_before",GaussianCancellationAccess::scheduler(w)->usage().reserved_bytes},
    {"source_before",state(w)},{"control",last.at("host_controls").at("processing.cancel")}};
  click(last.at("host_controls").at("processing.cancel"),"fit_running_cancel",false);
  const auto requested=w.jobInfo();
  report["fit_running_cancel"]["cancel_requested"]=requested && requested->id==running->id && requested->cancel_requested;
  gate->release();w.wait();frame();
  for(const auto& job:w.jobs())if(job.id==running->id)report["fit_running_cancel"]["terminal_state"]=int(job.state);
  report["fit_running_cancel"]["reserved_after"]=GaussianCancellationAccess::scheduler(w)->usage().reserved_bytes;
  report["fit_running_cancel"]["source_after"]=state(w);
  report["fit_running_cancel"]["source_data_unchanged"]=owner->snapshot().shares_data(fittedSnapshot);
  report["fit_running_cancel"]["source_revisions_unchanged"]=owner->snapshot().revisions().matches(fittedSnapshot.revisions(),Change::All);
  report["fit_running_cancel"]["busy_after"]=w.busy();
  report["fit_running_cancel"]["error_after"]=w.error();
  GaussianCancellationAccess::setNextPreparationGate(w,{});

  session.close();report["lifecycle"]=session.lifecycle();
  SessionCapture capture;capture.documents.push_back(calibrated);
  save_session((directory/"calibrated.ijx").u8string(),capture);w.openFile((directory/"calibrated.ijx").u8string());
  report["reopened"]=state(w);
  SessionCapture functionCapture;functionCapture.documents.push_back(functionSnapshot);
  save_session((directory/"function.ijx").u8string(),functionCapture);w.openFile((directory/"function.ijx").u8string());
  report["function_reopened"]=state(w);
  SessionCapture fitCapture;fitCapture.documents.push_back(fittedSnapshot);
  save_session((directory/"fitted.ijx").u8string(),fitCapture);w.openFile((directory/"fitted.ijx").u8string());
  report["fit_reopened"]=state(w);report["state"]="observed";report["error"]=w.error();
  require(report.dump().size()<16*1024*1024,"Calibration report byte limit");
  writeJson((directory/"case.json").u8string(),report);failure.done=true;return report;
}
} // namespace imagejxx::desktop
