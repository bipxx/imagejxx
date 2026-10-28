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
  HeadlessInputSession session(w);Json last;
  auto frame=[&](const Json& events=Json::array()) {
    require(report["frames"].size()<240,"Projection UI frame bound");last=session.frame(events);
    report["frames"].push_back({{"events",events},{"rendered",last}});
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
  session.close();report["lifecycle"]=session.lifecycle();
  w.setActiveIndex(1);SessionCapture capture;capture.documents.push_back(w.nativeSnapshot());
  for(const auto& step:w.workflow)capture.workflow.push_back(metadata_object(metadataObjectFromJson(step)));
  save_session((directory/"result.ijx").u8string(),capture);w.openFile((directory/"result.ijx").u8string());
  report["reopened"]=state(w);report["error"]=w.error();report["state"]="observed";
  require(report.dump().size()<16*1024*1024,"Projection report byte limit");
  writeJson((directory/"case.json").u8string(),report);return report;
}
} // namespace imagejxx::desktop
