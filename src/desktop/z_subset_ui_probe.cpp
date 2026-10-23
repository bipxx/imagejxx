// Actual shared-ImGui control input. Fixture setup and session I/O use explicit
// APIs. Independent pixel/metadata expectations belong to the Python reader.
#include "z_subset_ui_probe.hpp"
#include "headless_session_internal.hpp"
#include "window.hpp"
#include <imagejxx/session.hpp>
#include <imagejxx/planar_annotations.hpp>
#include <imagejxx/planar_display_annotations.hpp>
#include <stdexcept>
namespace imagejxx::desktop {
namespace {
void require(bool v,const char *message){if(!v)throw std::runtime_error(message);}
Json state(Window &window, bool regional, bool display) {
  const auto source=window.nativeSnapshot();const auto &data=source.data();
  const auto shape=data.dataset.shape();const auto cal=data.dataset.calibration();Json pixels=Json::array(),rois=Json::array();
  for(int t=0;t<shape.t;++t)for(int c=0;c<shape.c;++c)for(int z=0;z<shape.z;++z)pixels.push_back(data.dataset.plane({t,c,z})->pixels);
  for(const auto &roi:data.rois)rois.push_back({{"name",roi.name},{"z",roi.z?Json(*roi.z):Json()},{"selection",selectionJson(roi.selection)}});
  Json result{{"shape",shape.dimensions()},{"pixels",pixels},{"origin",cal.origin},{"spacing",cal.spacing},
    {"rois",rois},{"crosshair",source.display().crosshair},{"t",source.display().t},{"c",source.display().c},
    {"undo",window.nativeDocument(source.id())->undo_steps()},{"workflow",window.workflow},
    {"document_count",window.documents().size()}};
  if(regional) {
    result["planar"]=metadataJson(encode_planar_annotations(shape,planar_annotations(data)));
    result["selection"]=data.selection?selectionJson(*data.selection):Json();
    result["previous_selection"]=data.previous_selection?selectionJson(*data.previous_selection):Json();
    result["extensions"]=metadataJson(data.metadata.extensions);
  }
  if(display)result["display_annotations"]=metadataJson(encode_planar_display_annotations(shape,planar_display_annotations(data)));
  return result;
}
}
Json zSubsetUiCase(Window &window,const std::string &context,const std::string &fresh_directory, bool regional, bool display) {
  namespace fs=std::filesystem;const auto directory=fs::absolute(fs::u8path(fresh_directory));
  require((context=="imgui_cpp" || context=="imgui_python") && !fs::exists(directory) && window.documents().empty(),"Fresh Z subset UI fixture required");
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
  if(regional) {
    auto pattern=[](int w,int h,std::array<int,2> origin) {
      std::vector<unsigned char> bytes((std::size_t(w)*h+7)/8,0);
      for(int y=0;y<h;++y)for(int x=0;x<w;++x)if(x==y%w) {
        const auto i=y*w+x;bytes[i/8]|=static_cast<unsigned char>(0x80u>>(i%8));
      }
      auto value=Selection::from_packed_region(w,h,std::move(bytes),origin);
      value.width=2.5;value.attributes={{"opaque",metadata_array({true,std::uint64_t(-1),std::string("kept")})}};
      return value;
    };
    const auto xz=pattern(3,9,{-2,-2}),yz=pattern(4,7,{-1,0});
    data.selection=xz;data.previous_selection=yz;
    data.metadata.extensions[planar_annotations_extension]=encode_planar_annotations(data.dataset.shape(),{
      {"xz",xz,Plane::XZ,2,1,1,{{"row",std::string("xz")}}},
      {"yz",yz,Plane::YZ,3,{},{},{{"row",std::string("yz")}}},
      {"xy kept",xz,Plane::XY,3,{},{},{}},
      {"xy removed",yz,Plane::XY,0,{},{},{}}});
  }
  if(display) {
    TextAnnotationPayload text;text.text="Affine label";text.bounds={-1,-1,7,3};text.font_size=3;text.angle=23;
    ImageAnnotationPayload image;image.bounds={1,-1,4,6};image.width=2;image.height=2;image.channels=4;
    image.pixels={255,0,0,128,0,255,0,255,0,0,0,255,0,0,255,64};image.opacity=123;image.zero_transparent=true;
    data.metadata.extensions[planar_display_annotations_extension]=encode_planar_display_annotations(data.dataset.shape(),{
      {"xz text",text,Plane::XZ,2,1,1,{{"opaque",std::uint64_t(-1)}}},
      {"yz image",image,Plane::YZ,3,{},{},{{"opaque",std::string("bytes retained")}}},
      {"xy kept",text,Plane::XY,3,{},{},{}},{"xy removed",image,Plane::XY,0,{},{},{}}});
  }
  auto owner=std::make_shared<imagejxx::Document>(std::move(data));auto view=owner->snapshot().display();view.t=1;view.c=1;view.crosshair={3,2,4};view.orthogonal=display;owner->set_display(view);
  window.addNativeDocument(owner);window.recording=true;
  Json report{{"contract",display?"p12u.display-remap-ui.001":regional?"p12t.planar-region-ui.001":"p06m.z-subset.ui.001"},{"context",context},{"acceptance_claim",false},
      {"frames",Json::array()},{"controls",Json::array()},{"states",{{"before",state(window,regional,display)}}},
      {"api_fixture_actions",{"dataset/ROI attachment","session Save/Open"}}};
  HeadlessInputSession session(window);Json last;
  auto frame=[&](const Json &events=Json::array()){
    require(report["frames"].size()<(display?300:150),"Z subset UI frame bound");last=session.frame(events);
    require(last.at("input_frame").at("raw_inputs")==events,"Z subset input mismatch");report["frames"].push_back({{"events",events},{"rendered",last}});
  };
  auto click=[&](const Json &control,const std::string &name){
    require(control.at("enabled").get<bool>() && control.at("id").get<std::uint64_t>()>0,"Z subset control unavailable");
    report["controls"].push_back({{"name",name},{"control",control}});
    frame(Json::array({{{"type","position"},{"space","screen"},{"point",control.at("point")}}}));
    frame(Json::array({{{"type","button"},{"name","left"},{"down",true}}}));
    frame(Json::array({{{"type","button"},{"name","left"},{"down",false}}}));frame();
  };
  auto edit=[&](const char *name,const char *value){
    click(last.at("dialog").at("controls").at(name),name);
    for(bool down:{true,false})frame(Json::array({{{"type","keys"},{"keys",{{{"name","Ctrl"},{"down",down}},{{"name","A"},{"down",down}}}}}}));
    frame(Json::array({{{"type","text"},{"value",value}}}));frame();
  };
  frame();frame();if(display)report["before_layers"]=last.at("orthogonal_planes");
  click(last.at("host_controls").at("menu.Image"),"menu.Image");
  click(last.at("host_controls").at("menu.Image.Stacks"),"menu.Image.Stacks");
  click(last.at("host_controls").at("menu.z_subset"),"menu.z_subset");
  edit("first","2");edit("last","5");edit("step","2");
  click(last.at("dialog").at("controls").at("replace"),"replace");
  click(last.at("dialog").at("controls").at("apply"),"apply");frame();report["states"]["applied"]=state(window,regional,display);
  if(display)report["applied_layers"]=last.at("orthogonal_planes");
  click(last.at("host_controls").at("menu.Edit"),"menu.Edit");
  click(last.at("host_controls").at("menu.Edit.Undo"),"undo");report["states"]["undone"]=state(window,regional,display);
  click(last.at("host_controls").at("menu.Edit"),"menu.Edit");
  click(last.at("host_controls").at("menu.Edit.Redo"),"redo");report["states"]["redone"]=state(window,regional,display);
  click(last.at("host_controls").at("workflow.z_subset.0.rebind"),"workflow.rebind");
  edit("first","1");edit("last","2");edit("step","1");
  click(last.at("dialog").at("controls").at("replace"),"new_document");
  click(last.at("dialog").at("controls").at("apply"),"rebind.apply");
  frame();report["states"]["rebound"]=state(window,regional,display);
  if(display) {
    click(last.at("host_controls").at("menu.File"),"menu.File");
    click(last.at("host_controls").at("menu.file.display_annotations"),"menu.file.display_annotations");
    auto panel_control=[&](const std::string &key) {return last.at("display_annotation_panel").at("controls").at(key);};
    auto panel_click=[&](const std::string &key) {
      for(int attempt=0;attempt<12;++attempt) {
        const auto item=panel_control(key);if(item.at("visible").get<bool>()){click(item,"display."+key);return;}
        const auto rect=last.at("display_annotation_panel").at("window");
        const double x=(rect[0].get<double>()+rect[2].get<double>())/2,y=(rect[1].get<double>()+rect[3].get<double>())/2;
        const double wheel=item.at("rect")[1].get<double>()>rect[3].get<double>()?-3:3;
        frame(Json::array({{{"type","position"},{"space","screen"},{"point",{x,y}}}}));
        frame(Json::array({{{"type","wheel"},{"x",0},{"y",wheel}}}));frame();
      }
      throw std::runtime_error("Display transform control could not be revealed");
    };
    panel_click("rows");panel_click("row.0");panel_click("transform.section");
    for(const auto &value:std::array<const char*,2>{"-0.5","-0.25"}) {
      panel_click("transform.5");
      for(bool down:{true,false})frame(Json::array({{{"type","keys"},{"keys",{{{"name","Ctrl"},{"down",down}},{{"name","A"},{"down",down}}}}}}));
      frame(Json::array({{{"type","text"},{"value",value}}}));frame();panel_click("apply");
      report["states"][std::string(value)=="-0.5"?"display_changed":"display_restored"]=state(window,regional,display);
    }
    panel_click("close");
  }
  session.close();
  window.action("save_document",{{"path",(directory/"subset.ijx").u8string()}});
  Window reopened((directory/"reopened.json").u8string());reopened.openFile((directory/"subset.ijx").u8string());
  report["states"]["reopened"]=state(reopened,regional,display);
  if(display) {
    auto current=window.nativeDocument(window.nativeSnapshot().id());
    auto view=current->snapshot().display();view.zoom=32;view.linkedZoom=false;view.planeZoom={32,32,32};
    current->set_display(view);
    const auto screenshot=directory/"native-gl.png";
    require(window.run({true,3,screenshot.u8string()})==0,"Affine display hidden GL capture failed");
    require(fs::is_regular_file(screenshot),"Affine display GL screenshot missing");
    report["screenshot"]=screenshot.u8string();
  }
  report["state"]="observed";report["error"]=window.error();
  require(report.dump().size()<=16*1024*1024,"Z subset report byte bound");
  writeJson((directory/"case.json").u8string(),report);return report;
}
}
