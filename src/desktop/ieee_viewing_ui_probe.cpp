#include "ieee_viewing_ui_probe.hpp"
#include "window.hpp"
#include "headless_session_internal.hpp"
#include <imagejxx/operations.hpp>
#include <imagejxx/session.hpp>
#include <cmath>
#include <filesystem>
#include <stdexcept>

namespace imagejxx::desktop {
namespace {
constexpr auto policy=FloatingSamplePolicy::PreserveIEEEFloat32;
void require(bool value,const char *message) { if(!value)throw std::runtime_error(message); }
std::uint32_t sourceWord(int profile,int t,int c,int z,int y,int x) {
  constexpr std::uint32_t mixed[]{0xff800000u,0x7f800000u,0x7f800001u,0xffc12345u,0x80000000u,0u,0x3f800000u,0x3f000000u};
  constexpr std::uint32_t nan[]{0x7f800001u,0xff812345u,0x7fc12345u,0xffc00001u};
  const auto i=unsigned(t*3+c*5+z*2+y*8+x);
  if(profile==1)return nan[i%4];
  if(profile==2)return i%2?0x7f800000u:0xff800000u;
  if(profile==3) {
    if(y==0 && x==0)return 0x7f800000u;
    if(y==0 && x==1)return 0xff800000u;
    return (i%2)?0x3f800000u:0u;
  }
  return mixed[i%8];
}
Json words(const Dataset &data) {
  Json result=Json::array();const auto shape=data.shape();
  for(int t=0;t<shape.t;++t)for(int c=0;c<shape.c;++c)for(int z=0;z<shape.z;++z)
    for(double sample:data.plane({t,c,z})->pixels)
      result.push_back(float32_source_sample_bits(sample,policy));
  return result;
}
}
Json ieeeViewingUiCase(Window &window,const std::string &case_id,
                       const std::string &context,const std::string &directory) {
  require(case_id=="IEEE01"||case_id=="IEEE02"||case_id=="IEEE03"||case_id=="IEEE04","Unknown IEEE viewing case");
  const int profile=case_id.back()-'1';
  const auto path=std::filesystem::absolute(std::filesystem::u8path(directory));
  require(!std::filesystem::exists(path),"IEEE viewing directory must be fresh");
  std::filesystem::create_directories(path);
  require(window.documents().empty(),"IEEE viewing case requires an empty window");
  Stack planes;
  for(int t=0;t<2;++t)for(int c=0;c<2;++c)for(int z=0;z<2;++z) {
    Image image(8,4,1,PixelType::Float32);
    for(int y=0;y<4;++y)for(int x=0;x<8;++x)
      image.at(x,y)=float32_source_sample(sourceWord(profile,t,c,z,y,x),policy);
    planes.push_back(std::move(image));
  }
  auto owner=std::make_shared<imagejxx::Document>(Dataset::from_planes({2,2,2,4,8,1},std::move(planes),{},policy));
  auto view=owner->snapshot().display();view.t=1;view.c=1;view.crosshair={3,2,1};
  owner->set_display(view);owner->mark_saved();
  const auto before=owner->snapshot();const auto beforeWords=words(before.data().dataset);
  window.addNativeDocument(owner);
  Json report={{"case_id",case_id},{"context",context},{"frames",Json::array()},
      {"captures",Json::object()},{"source_words",beforeWords},{"workflow",Json::array()}};
  report["state"]="running";
  struct PartialReport {
    const std::filesystem::path &path; Json &data;
    ~PartialReport() noexcept {
      if(data.value("state",std::string())=="passed")return;
      try { data["state"]="failed";writeJson((path/"partial-report.json").u8string(),data); }
      catch(...) {}
    }
  } partial{path,report};
  HeadlessInputSession session(window);
  Json last;
  auto frame=[&](const std::string &label,const Json &events=Json::array()) {
    last=session.frame(events);
    require(window.error().empty(),"IEEE viewing frame reported a host error");
    report["frames"].push_back({{"label",label},{"events",events},
        {"input_frame",last.at("input_frame")}});
  };
  auto capture=[&](const std::string &label) {
    frame(label+".settle1");frame(label+".settle2");
    report["captures"][label]=last;
  };
  auto click=[&](const std::string &selector) {
    const auto control=last.at("channel_display_panel").at("control_states").at(selector);
    require(control.at("enabled")==true && control.at("admitted")==true,"IEEE widget is not admitted");
    const auto point=control.at("point");
    const double x=std::floor(point.at(0).get<double>()),y=std::floor(point.at(1).get<double>());
    frame(selector+".position",Json::array({{{"type","position"},{"space","screen"},{"point",{x,y}}}}));
    frame(selector+".down",Json::array({{{"type","button"},{"name","left"},{"down",true}}}));
    frame(selector+".up",Json::array({{{"type","button"},{"name","left"},{"down",false}}}));
  };
  auto text=[&](const std::string &selector,const std::string &value) {
    click(selector);
    for(bool down:{true,false})frame(selector+".select_all",Json::array({{{"type","keys"},
        {"keys",Json::array({{{"name","Ctrl"},{"down",down}},{{"name","A"},{"down",down}}})}}}));
    frame(selector+".text",Json::array({{{"type","text"},{"value",value}}}));
  };
  capture("ordinary.auto");
  view=owner->snapshot().display();view.orthogonal=true;window.setNativeDisplay(view);
  capture("linked.auto");capture("linked.stable");
  window.enableChannelDisplay();window.action("show_channel_display");
  window.recording=true;capture("channels.auto");
  text("channel_display.minimum","0");text("channel_display.maximum","1");
  capture("channels.draft");
  require(last.at("channel_display_panel").at("minimum_text")=="0" &&
          last.at("channel_display_panel").at("maximum_text")=="1","IEEE draft was rebased over physical input");
  click("channel_display.apply");capture("channels.manual");
  click("channel_display.reset_current");capture("channels.reset");
  click("channel_display.mode");frame("mode.popup");
  click("channel_display.mode.composite_max");capture("channels.max");
  click("channel_display.mode");frame("mode.popup");
  click("channel_display.mode.composite_min");capture("channels.min");
  require(owner->snapshot().shares_data(before) && !owner->snapshot().dirty() &&
          owner->history().undo_steps==0 && owner->history().redo_steps==0,"IEEE viewing mutated source/history");
  report["final_words"]=words(owner->snapshot().data().dataset);
  require(report.at("final_words")==beforeWords,"IEEE viewing changed source words");
  report["workflow"]=window.workflow;
  SessionCapture saved{{owner->snapshot()},{},{},{}};
  for(const auto &item:window.workflow)saved.workflow.push_back(metadata_object(metadataObjectFromJson(item)));
  const auto sessionPath=path/"viewing.ijx";
  save_session(sessionPath.u8string(),saved);
  const auto restored=load_session(sessionPath.u8string());
  window.addNativeDocument(restored.documents[0]);capture("session.reopened");
  report["reopened_words"]=words(restored.documents[0]->snapshot().data().dataset);
  require(report.at("reopened_words")==beforeWords,"IEEE session changed source words");
  session.close();report["lifecycle"]=session.lifecycle();
  report["state"]="passed";
  writeJson((path/"report.json").u8string(),report);
  return report;
}
}
