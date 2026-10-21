#include "shadow_ui_probe.hpp"
#include "headless_session_internal.hpp"
#include "window.hpp"
#include <chrono>
#include <thread>
#include <stdexcept>
namespace imagejxx::desktop {
Json shadowDemoUiCase(Window &window,const std::string &id,const std::string &context,const std::string &directory) {
  const bool complete=id=="byte" || id=="short" || id=="float" || id=="rgb";
  const bool guard=id=="guard-stack" || id=="guard-type" || id=="guard-policy";
  if(!complete && !guard && id!="escape" && id!="stop" && id!="source" && id!="budget" && id!="overflow")throw std::invalid_argument("Unknown shadow demo case");
  namespace fs=std::filesystem;auto output=fs::absolute(fs::u8path(directory));
  if(fs::exists(output) || !window.documents().empty())throw std::invalid_argument("Fresh shadow demo fixture required");
  fs::create_directories(output);
  const int width=id=="budget"?128:7,height=id=="budget"?128:5,samples=id=="rgb"?3:1;
  const auto type=id=="short"?PixelType::UInt16:id=="float" || id=="overflow" || id=="guard-policy"?PixelType::Float32:id=="guard-type"?PixelType::Float64:PixelType::UInt8;
  Image image(width,height,samples,type);
  auto value=[](std::size_t i){return double((i*37+13)%256);};
  for(int y=0;y<height;++y)for(int x=0;x<width;++x)for(int c=0;c<samples;++c) {
    const auto i=std::size_t(y)*width+x;
    image.at(x,y,c)=id=="overflow"?(y==0?1e38:y==2?-1e38:0):type==PixelType::Float32?double(float(int((i*37)%251)-123)/7.f):
        type==PixelType::UInt16?257*value(i):value(i+(c==1?3:c==2?7:0));
  }
  Stack planes{id=="guard-stack"?image:image};if(id=="guard-stack")planes.push_back(image);
  DocumentData data{Dataset::from_stack(std::move(planes),{},id=="guard-policy"?FloatingSamplePolicy::PreserveIEEEFloat32:FloatingSamplePolicy::FiniteOnly)};
  data.previous_selection=Selection("line",{{0,0},{6,4}});
  data.rois={{"retained",Selection("rectangle",{{1,1},{6,4}}),std::nullopt,std::nullopt,0,{{"opaque",std::string("retained")}}}};
  auto owner=std::make_shared<imagejxx::Document>(std::move(data));window.addNativeDocument(owner);
  window.updateSettings({{"processing_workers",1},{"processing_memory_mb",64}},false);
  window.workflow=Json::array({{{"operation","subtract"},{"parameters",{{"value",3}}}}});window.recording=true;
  const auto before=owner->snapshot();
  const auto state=[&] {
    const auto s=owner->snapshot();Json pixels=Json::array();const auto shape=s.data().dataset.shape();
    for(int z=0;z<shape.z;++z)pixels.push_back(s.data().dataset.plane({0,0,z})->pixels);
    return Json{{"pixels",pixels},{"selection",s.data().selection?selectionJson(*s.data().selection):Json()},
        {"previous",selectionJson(*s.data().previous_selection)},{"roi",selectionJson(s.data().rois[0].selection)},
        {"attributes",metadataJson(s.data().rois[0].attributes)},{"undo",owner->undo_steps()},{"redo",owner->redo_steps()},
        {"workflow",window.workflow},{"shares_original",s.shares_data(before)}};
  };
  Json report{{"contract","p09m.shadow-demo.ui.001"},{"case_id",id},{"context",context},{"state","running"},{"acceptance_claim",false},
      {"frames",Json::array()},{"effects",Json::array()},{"controls",Json::array()},{"before",state()}};
  struct Failure {Json &r;fs::path p;bool done=false;~Failure(){if(!done)try{r["state"]="failed";writeJson((p/"failure.json").u8string(),r);}catch(...){}}}failure{report,output};
  HeadlessInputSession session(window);Json last;std::size_t actualFrames=0;std::uint64_t seen=0;
  const auto started=std::chrono::steady_clock::now();
  auto frame=[&](const Json &events=Json::array(),bool retain=true) {
    if(++actualFrames>6000 || std::chrono::steady_clock::now()-started>std::chrono::seconds(25))throw std::runtime_error("Shadow demo observation bound");
    last=session.frame(events);
    if(last.at("input_frame").at("raw_inputs")!=events)throw std::runtime_error("Shadow demo raw input mismatch");
    if(retain)report["frames"].push_back({{"events",events},{"rendered",{{"input_frame",last.at("input_frame")},{"shadow_demo",last.at("shadow_demo")}}}});
    if(!window.error().empty())throw std::runtime_error("Shadow demo window error: "+window.error());
  };
  auto click=[&](const std::string &name) {
    const auto control=last.at("host_controls").at(name);
    if(!control.at("enabled").get<bool>() || !control.at("id").get<std::uint64_t>())throw std::runtime_error("Shadow demo control unavailable: "+name);
    report["controls"].push_back({{"name",name},{"control",control}});
    frame(Json::array({{{"type","position"},{"space","screen"},{"point",control.at("point")}}}));
    for(bool down:{true,false})frame(Json::array({{{"type","button"},{"name","left"},{"down",down}}}));frame();
  };
  frame();frame();frame();click("menu.Process");click("menu.Process.Shadows");
  if(guard) {
    report["guard"]=last.at("host_controls").at("menu.shadow.demo");
    if(report["guard"]["enabled"].get<bool>())throw std::runtime_error("Unsupported shadow demo was enabled");
  } else {
    click("menu.shadow.demo");
    while(last.at("shadow_demo").at("active").get<bool>()) {
      frame(Json::array(),false);
      const auto shown=last.at("shadow_demo").at("shown").get<std::uint64_t>();
      if(shown>seen) {
        if(shown!=seen+1)throw std::runtime_error("Shadow demo skipped a displayed effect");
        seen=shown;
        report["effects"].push_back({{"number",seen},{"direction",last.at("shadow_demo").at("direction")},
            {"elapsed_ms",std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-started).count()},
            {"canvas",last.at("ordinary_pixels")},{"source_unchanged",owner->snapshot().shares_data(before)}});
      }
      if((id=="escape" || id=="stop" || id=="source") && shown>=10) {
        if(id=="escape") {
          frame(Json::array({{{"type","key"},{"name","Escape"},{"down",true}}}));
          frame(Json::array({{{"type","key"},{"name","Escape"},{"down",false}}}));
        } else if(id=="stop")click("shadow_demo.stop");
        else {owner->select(Selection("point",{{1,1}}));report["api_fixture_action"]="Native owner selection changed after ten displayed effects";frame();}
      } else if(id=="budget" && shown>=1) {
        try {window.updateSettings({{"processing_memory_mb",1}},false);throw std::runtime_error("Unsafe memory limit was accepted");}
        catch(const std::invalid_argument &e){report["budget_rejection"]=e.what();}
        report["retained_memory_mb"]=window.settings().integer("processing_memory_mb");
        report["api_fixture_action"]="Attempt to lower processing memory below the retained preview reservation";
        click("shadow_demo.stop");
      }
      if(last.at("shadow_demo").at("active").get<bool>())std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
  }
  frame();frame();report["after"]=state();report["final_demo"]=last.at("shadow_demo");report["final_canvas"]=last.at("ordinary_pixels");
  report["actual_frames"]=actualFrames;report["final_error"]=window.error();session.close();report["state"]="observed";
  if(report.dump().size()>48*1048576)throw std::runtime_error("Shadow demo report retention bound");
  writeJson((output/"case.json").u8string(),report);failure.done=true;return report;
}
}
