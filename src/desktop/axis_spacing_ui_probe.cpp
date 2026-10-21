// Actual shared-ImGui control input. Fixture setup and session I/O use explicit
// APIs. Independent pixel/metadata expectations belong to the Python reader.
#include "axis_spacing_ui_probe.hpp"
#include "headless_session_internal.hpp"
#include "window.hpp"
#include <imagejxx/session.hpp>
#include <stdexcept>
namespace imagejxx::desktop {
namespace {
void require(bool v,const char *message){if(!v)throw std::runtime_error(message);}
Json state(Window &window) {
  const auto source=window.nativeSnapshot();const auto &data=source.data();
  const auto shape=data.dataset.shape();const auto cal=data.dataset.calibration();Json pixels=Json::array(),rois=Json::array();
  for(int t=0;t<shape.t;++t)for(int c=0;c<shape.c;++c)for(int z=0;z<shape.z;++z)pixels.push_back(data.dataset.plane({t,c,z})->pixels);
  for(const auto &roi:data.rois)rois.push_back({{"name",roi.name},{"z",roi.z?Json(*roi.z):Json()},{"selection",selectionJson(roi.selection)}});
  return {{"shape",shape.dimensions()},{"pixels",pixels},{"origin",cal.origin},{"spacing",cal.spacing},
    {"rois",rois},{"crosshair",source.display().crosshair},{"t",source.display().t},{"c",source.display().c},
    {"undo",window.nativeDocument(source.id())->undo_steps()},{"workflow",window.workflow},
    {"document_count",window.documents().size()}};
}
}
Json axisSpacingUiCase(Window &window,const std::string &context,const std::string &fresh_directory) {
  namespace fs=std::filesystem;const auto directory=fs::absolute(fs::u8path(fresh_directory));
  require((context=="imgui_cpp" || context=="imgui_python") && !fs::exists(directory) && window.documents().empty(),"Fresh Physical spacing UI fixture required");
  fs::create_directories(directory);
  Stack planes;
  for(int t=0;t<2;++t)for(int c=0;c<2;++c)for(int z=0;z<5;++z){
    Image image(7,5,1,PixelType::UInt16);
    for(int y=0;y<5;++y)for(int x=0;x<7;++x)image.at(x,y)=10000*t+1000*c+100*z+10*y+x;
    planes.push_back(std::move(image));
  }
  SpatialCalibration cal;cal.spacing={2,3,5};cal.origin={10,20,30};
  DocumentData data{Dataset::from_planes({2,2,5,5,7,1},std::move(planes),cal)};
  Selection selection("rectangle",{{1,1},{5,4}});
  data.rois={{"zero",selection,0,0,0,{}},{"first",selection,0,0,1,{}},{"last",selection,1,1,3,{}},{"wild",selection,{},{},{},{}}};
  auto owner=std::make_shared<imagejxx::Document>(std::move(data));auto view=owner->snapshot().display();view.t=1;view.c=1;view.crosshair={3,2,4};owner->set_display(view);
  window.addNativeDocument(owner);window.recording=true;
  Json report{{"contract","p06n.axis-spacing.ui.001"},{"context",context},{"acceptance_claim",false},
      {"frames",Json::array()},{"controls",Json::array()},{"states",{{"before",state(window)}}},
      {"api_fixture_actions",{"dataset/ROI attachment","session Save/Open"}}};
  HeadlessInputSession session(window);Json last;
  auto frame=[&](const Json &events=Json::array()){
    require(report["frames"].size()<150,"Physical spacing UI frame bound");last=session.frame(events);
    require(last.at("input_frame").at("raw_inputs")==events,"Physical spacing input mismatch");report["frames"].push_back({{"events",events},{"rendered",last}});
  };
  auto click=[&](const Json &control,const std::string &name){
    require(control.at("enabled").get<bool>() && control.at("id").get<std::uint64_t>()>0,"Physical spacing control unavailable");
    report["controls"].push_back({{"name",name},{"control",control}});
    frame(Json::array({{{"type","position"},{"space","screen"},{"point",control.at("point")}}}));
    frame(Json::array({{{"type","button"},{"name","left"},{"down",true}}}));
    frame(Json::array({{{"type","button"},{"name","left"},{"down",false}}}));frame();
  };
  auto edit=[&](const char *name,const char *value){
    if (!last.at("dialog").at("controls").contains(name)) {
      writeJson((directory/"failure.json").u8string(),report);
      throw std::runtime_error(std::string("Missing physical spacing control: ")+name+" "+last.at("dialog").dump());
    }
    click(last.at("dialog").at("controls").at(name),name);
    for(bool down:{true,false})frame(Json::array({{{"type","keys"},{"keys",{{{"name","Ctrl"},{"down",down}},{{"name","A"},{"down",down}}}}}}));
    frame(Json::array({{{"type","text"},{"value",value}}}));frame();
  };
  frame();frame();click(last.at("host_controls").at("menu.Image"),"menu.Image");
  click(last.at("host_controls").at("menu.Image.Stacks"),"menu.Image.Stacks");
  click(last.at("host_controls").at("menu.action.show_axis_range_spacing"),"menu.action.show_axis_range_spacing");
  edit("first","2");edit("last","4");edit("step","2");
  edit("physical_vertical_spacing","2.5");
  click(last.at("dialog").at("controls").at("sampling"),"sampling");
  click(last.at("dialog").at("controls").at("sampling.choice.Linear"),"sampling.Linear");
  click(last.at("dialog").at("controls").at("reverse_order"),"reverse_order");
  click(last.at("dialog").at("controls").at("flip_vertical"),"flip_vertical");
  click(last.at("dialog").at("controls").at("transpose"),"transpose");
  report["configured"]=last.at("dialog");
  click(last.at("dialog").at("controls").at("apply"),"apply");frame();report["states"]["applied"]=state(window);
  click(last.at("host_controls").at("workflow.spacing.0.rebind"),"workflow.rebind");
  edit("physical_vertical_spacing","1.5");
  click(last.at("dialog").at("controls").at("cancel"),"cancel");
  frame();report["states"]["cancelled"]=state(window);
  session.close();
  window.action("save_document",{{"path",(directory/"spacing.ijx").u8string()}});
  Window reopened((directory/"reopened.json").u8string());reopened.openFile((directory/"spacing.ijx").u8string());
  report["states"]["reopened"]=state(reopened);
  report["state"]="observed";report["error"]=window.error();
  require(report.dump().size()<=16*1024*1024,"Physical spacing report byte bound");
  writeJson((directory/"case.json").u8string(),report);return report;
}
}
