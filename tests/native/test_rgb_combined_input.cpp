#include "window.hpp"
#include "headless_session_internal.hpp"
#include "rank_runtime_probe.hpp"
#include <imgui.h>
#include <filesystem>
#include <fstream>
#include <iostream>

using namespace imagejxx;
using imagejxx::desktop::Window;
using imagejxx::desktop::Json;
using imagejxx::desktop::HeadlessInputSession;
using imagejxx::desktop::rankRuntimeSha256;
namespace {
void check(bool value,const std::string &message){if(!value)throw std::runtime_error(message);}
Json key(const char *name,bool down){return {{"type","key"},{"name",name},{"down",down}};}
std::string digest(const Dataset &data){
  std::vector<unsigned char> bytes;const auto s=data.shape();bytes.reserve(s.pixel_count());
  for(int t=0;t<s.t;++t)for(int c=0;c<s.c;++c)for(int z=0;z<s.z;++z){const auto image=data.plane({t,c,z});for(double value:image->pixels)bytes.push_back(static_cast<unsigned char>(value));}
  return rankRuntimeSha256(bytes);
}
std::shared_ptr<Document> source(int origin){
  Stack planes;for(int t=0;t<2;++t)for(int c=0;c<2;++c)for(int z=0;z<2;++z){Image image(256,8,3,PixelType::UInt8);
    for(int y=0;y<8;++y)for(int x=0;x<256;++x){image.at(x,y,0)=(x+11*c+23*z+41*t)&255;image.at(x,y,1)=(73*x+19*y+17*c+31*z+47*t)&255;image.at(x,y,2)=(255-x+53*c+59*z+61*t)&255;}planes.push_back(std::move(image));}
  auto owner=std::make_shared<Document>(Dataset::from_planes({2,2,2,8,256,3},std::move(planes)));
  auto view=owner->snapshot().display();view.t=view.c=origin;view.crosshair[2]=origin;view.linkedZoom=true;view.zoom=1;view.pan[0]={-160,0};owner->set_display(view);return owner;
}
class Input {
public:
  Window &window;HeadlessInputSession session;Json report,selectors=Json::array();std::size_t frames=0,buttons=0,texts=0;
  explicit Input(Window &w):window(w),session(w){frame();frame();}
  void frame(const Json &events=Json::array()){report=session.frame(events);++frames;}
  void wait(){window.action("wait_jobs");frame();frame();}
  Json control(const std::string &name){
    if(name.rfind("contrast.",0)==0)return report.at("contrast_panel").at("controls").at(name.substr(9));
    if(name.rfind("roi.",0)==0)return report.at("roi_manager").at("controls").at(name.substr(4));
    return report.at("host_controls").at(name);
  }
  void click(const std::string &name,bool scalar=false){
    auto hit=control(name);
    if(name.rfind("contrast.",0)==0){
      for(int i=0;i<40 && (!hit.at("visible").get<bool>() || hit.at("point")[1].get<double>()<100 || hit.at("point")[1].get<double>()>700);++i){
        frame(Json::array({{{"type","position"},{"space","screen"},{"point",{1200,350}}}}));frame(Json::array({{{"type","wheel"},{"x",0},{"y",hit.at("point")[1].get<double>()>700?-3:3}}}));frame();hit=control(name);
      }
    }
    check(hit.value("visible",true)&&hit.at("enabled").get<bool>(),"Unavailable control: "+name+" "+hit.dump());
    frame(Json::array({{{"type","position"},{"space","screen"},{"point",hit.at("point")}}}));
    for(int retry=0;retry<5 && control(name).at("point")!=hit.at("point");++retry){hit=control(name);frame(Json::array({{{"type","position"},{"space","screen"},{"point",hit.at("point")}}}));}
    selectors.push_back({{"selector",name},{"control",hit},{"frame",frames}});
    if(scalar)frame(Json::array({key("Ctrl",true)}));
    frame(Json::array({{{"type","position"},{"space","screen"},{"point",hit.at("point")}}}));
    for(bool down:{true,false})frame(Json::array({{{"type","button"},{"name","left"},{"down",down}}}));
    if(scalar)frame(Json::array({key("Ctrl",false)}));frame();++buttons;
  }
  void text(const std::string &name,const std::string &value,bool scalar=false){
    bool focused=false;for(int retry=0;retry<3 && !focused;++retry){click(name,scalar);const auto id=control(name).at("id");focused=report.at("input_frame").at("focus").at("active_id")==id && report.at("input_frame").at("input_text").at("id")==id;}
    check(focused,"Text control did not acquire focus: "+name+" "+report.at("input_frame").dump());
    frame(Json::array({key("Ctrl",true),key("A",true)}));frame(Json::array({key("A",false),key("Ctrl",false)}));
    frame(Json::array({{{"type","text"},{"value",value}}}));frame(Json::array({key("Enter",true)}));frame(Json::array({key("Enter",false)}));frame();++texts;
  }
  void draw(const char *tool){
    click(std::string("tools.tool.")+tool);
    frame(Json::array({{{"type","position"},{"space","image"},{"point",{17,0}}}}));
    frame(Json::array({{{"type","button"},{"name","left"},{"down",true}}}));
    frame(Json::array({{{"type","position"},{"space","image"},{"point",{236,8}}}}));
    frame(Json::array({{{"type","button"},{"name","left"},{"down",false}}}));wait();
    const auto selected=window.nativeSnapshot().data().selection;
    check(selected && selected->kind==tool && selected->points==std::vector<std::array<double,2>>{{17,0},{236,8}},"Canvas gesture did not create exact "+std::string(tool)+": "+report.at("selection").dump());
  }
  void navigate(int axis,int value){
    text(axis==0?"navigation.time":axis==1?"navigation.channel":"image.slice",std::to_string(value+1),axis==2);wait();
    const auto v=window.nativeSnapshot().display();check((axis==0?v.t:axis==1?v.c:v.crosshair[2])==value,"Physical navigation did not commit");
  }
  void mapping(const Json &range){text("contrast.minimum",std::to_string(range[0].get<double>()));text("contrast.maximum",std::to_string(range[1].get<double>()));wait();check(report.at("contrast_panel").at("range")==range,"Physical range fields differ: expected "+range.dump()+" actual "+report.at("contrast_panel").at("range").dump()+" error "+report.at("contrast_panel").at("error").dump()+" input "+report.at("input_frame").dump());}
};
Json run(const Json &row,const Json &ranges,const std::filesystem::path &root,std::size_t index){
  const auto origin=row.at("origin").get<int>(),axis=row.at("axis").get<int>();auto owner=source(origin);const auto original=owner->snapshot();
  Window window((root/(std::to_string(index)+"-preferences.json")).u8string());window.addVirtualDocument(owner);
  Input input(window);
  if(!input.report.at("roi_manager").at("controls").contains("create_roi") || !input.report.at("roi_manager").at("controls").at("create_roi").value("visible",false))input.click("roi.tab");
  input.click("roi.create_roi");input.draw("ellipse");
  const auto ellipse=owner->snapshot().data().selection;
  for(const char *action:{"select_none","restore_selection","select_all","restore_selection"}){input.click("menu.Edit");input.click("menu.Edit.Selection");input.click(std::string("menu.action.")+action);input.wait();}
  check(owner->snapshot().data().selection==ellipse,"Native selection menu cycle did not restore initial ellipse");
  window.action("show_contrast");input.frame();ImGui::SetWindowPos("Brightness/Contrast",{790,60});input.frame();input.frame();input.wait();
  input.click("contrast.rgb_stack");input.click("contrast.rgb_components_"+std::to_string(row.at("first_channels").get<int>()));input.mapping(ranges.at("first"));
  input.click("contrast.rgb_components_"+std::to_string(row.at("second_channels").get<int>()));
  input.click("menu.Edit");input.click("menu.Edit.Selection");input.click("menu.action.select_none");input.wait();check(!owner->snapshot().data().selection,"Deselect did not clear native selection");
  input.mapping(ranges.at("selected"));input.click("contrast.rgb_components_"+std::to_string(row.at("current_channels").get<int>()));
  input.navigate(axis,1-origin);input.draw("rectangle");input.mapping(ranges.at("other"));input.navigate(axis,origin);input.mapping(ranges.at("replaced_return_draft"));
  input.click("contrast.reset");input.wait();check(input.report.at("contrast_panel").at("range")==Json::array({0.,255.}),"Physical returning Reset did not reset draft");
  if(row.at("automatic").get<int>()){input.click("contrast.auto");input.wait();}else input.mapping(ranges.at("final"));
  check(input.report.at("contrast_panel").at("range")==Json::array({row.at("minimum"),row.at("maximum")}),"Physical final range differs from ImageJ");
  check(input.report.at("contrast_panel").at("rgb").at("auto_divisor")==row.at("auto_threshold"),"Physical Auto cycle differs from ImageJ");
  check(owner->snapshot().data().dataset.shares_pixels(original.data().dataset),"Gesture preview published source pixels");
  const auto finalRange=input.report.at("contrast_panel").at("range"),finalCycle=input.report.at("contrast_panel").at("rgb").at("auto_divisor");
  const auto before=owner->snapshot();const auto history=owner->undo_steps();window.recording=true;input.click("contrast.apply");input.wait();const auto after=owner->snapshot();
  const auto actual=digest(after.data().dataset);check(actual==row.at("applied_stack_sha256"),"Gesture stack pixels differ from ImageJ: "+actual);
  check(owner->undo_steps()==history+1 && window.workflow.size()==1 && window.workflow[0].at("schema")=="imagejxx.rgb-contrast-session-command","Combined gesture Apply did not record one complete edit");
  check(after.data().selection==before.data().selection && after.display().crosshair==before.display().crosshair && after.display().t==before.display().t && after.display().c==before.display().c,"Gesture Apply changed live selection/navigation");
  owner->undo();check(owner->snapshot().shares_data(before),"Gesture Undo did not restore complete pre-Apply state");owner->redo();check(owner->snapshot().shares_data(after),"Gesture Redo differs");
  const auto pixelsName=std::to_string(index)+"-applied.bin";std::ofstream pixels(root/pixelsName,std::ios::binary);const auto shape=after.data().dataset.shape();
  for(int t=0;t<shape.t;++t)for(int c=0;c<shape.c;++c)for(int z=0;z<shape.z;++z)for(double value:after.data().dataset.plane({t,c,z})->pixels)pixels.put(static_cast<char>(static_cast<unsigned char>(value)));pixels.close();check(bool(pixels),"Cannot retain observed pixel bytes");
  Json result{{"pixels_file",pixelsName},{"current_channels",row.at("current_channels")},{"range",finalRange},{"auto_divisor",finalCycle},{"controls",input.selectors},{"axis",axis},{"origin",origin},{"first_channels",row.at("first_channels")},{"second_channels",row.at("second_channels")},{"automatic",row.at("automatic")},{"actual_stack_sha256",actual},{"frames",input.frames},{"physical_buttons",input.buttons},{"physical_text_fields",input.texts},{"record",window.workflow[0]}};
  window.action("close_contrast");return result;
}
}
int main(int argc,char **argv){try{
  check(argc==2||argc==3||argc==4,"Supply a fresh owned output directory and optional trace limit");const auto root=std::filesystem::absolute(std::filesystem::u8path(argv[1]));check(!std::filesystem::exists(root),"Input fixture output must be fresh");std::filesystem::create_directories(root);
  const auto path=std::filesystem::path(__FILE__).parent_path().parent_path()/"fixtures/rgb-contrast-combined-imagej154r.json";std::ifstream file(path);check(bool(file),"Missing independent ImageJ fixture");Json fixture;file>>fixture;
  const auto limit=argc>=3?std::stoul(argv[2]):12UL,start=argc==4?std::stoul(argv[3]):0UL;std::size_t ordinal=0;Json results=Json::array();
  for(const auto &row:fixture.at("cases")){if(row.at("roi")!=8)continue;if(ordinal++<start)continue;if(limit==12 && (row.at("first_channels")!=1 || row.at("second_channels")!=2))continue;
    results.push_back(run(row,fixture.at("ranges"),root,results.size()));std::cout<<"PASS combined RGB gesture "<<results.size()<<"\n"<<std::flush;if(results.size()==limit)break;
  }
  check(results.size()==limit,"Requested trace count differs");std::ofstream(root/"results.json")<<Json{{"state","passed"},{"cases",results.size()},{"results",results}}.dump(2)<<'\n';return 0;
}catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 1;}}
