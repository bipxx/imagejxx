#include "window.hpp"
#include "headless_session_internal.hpp"
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>

using namespace imagejxx;
using imagejxx::desktop::Window;
using imagejxx::desktop::Json;
using imagejxx::desktop::HeadlessInputSession;
namespace {
void check(bool ok,const std::string &message){if(!ok)throw std::runtime_error(message);}
class Input {
public:
  HeadlessInputSession session;Json report,deliveries=Json::array();
  explicit Input(Window &window):session(window){frame();frame();}
  void frame(const Json &events=Json::array()){report=session.frame(events);}
  void click(const std::string &name){
    auto hit=report.at("host_controls").at(name);
    check(hit.value("visible",true)&&hit.at("enabled").get<bool>(),"Unavailable control "+name);
    frame(Json::array({{{"type","position"},{"space","screen"},{"point",hit.at("point")}}}));
    hit=report.at("host_controls").at(name);
    frame(Json::array({{{"type","position"},{"space","screen"},{"point",hit.at("point")}}}));
    deliveries.push_back({{"selector",name},{"control",hit}});
    for(bool down:{true,false})frame(Json::array({{{"type","button"},{"name","left"},{"down",down}}}));
    frame();
  }
  void menu(const char *action){click("menu.Edit");click("menu.Edit.Selection");click(std::string("menu.action.")+action);}
};
Json run(const std::filesystem::path &root,int mode){
  const auto type=mode==0 || mode==3?PixelType::UInt8:mode==1?PixelType::UInt16:PixelType::Float32;
  const auto policy=mode==4?FloatingSamplePolicy::PreserveQuietNaNFloat32:mode==5?FloatingSamplePolicy::PreserveIEEEFloat32:FloatingSamplePolicy::FiniteOnly;
  Stack planes;for(int i=0;i<8;++i){Image image(7,5,mode==3?3:1,type);std::fill(image.pixels.begin(),image.pixels.end(),30);planes.push_back(std::move(image));}
  DatasetReadObserver reads;
  auto owner=std::make_shared<Document>(Dataset::from_planes({2,2,2,5,7,mode==3?3:1},std::move(planes),{},policy).observe_reads(reads));
  Selection initial("rectangle",{{1,1},{6,4}});owner->select(initial);
  auto view=owner->snapshot().display();view.t=view.c=view.crosshair[2]=1;owner->set_display(view);
  const auto before=owner->snapshot();
  Window window((root/(std::to_string(mode)+"-preferences.json")).u8string());window.addVirtualDocument(owner);
  Input input(window);input.menu("invert_selection");
  auto after=owner->snapshot();check(after.data().selection.has_value(),"Inverse selection missing");
  const auto mask=after.data().selection->mask(7,5);
  for(int y=0;y<5;++y)for(int x=0;x<7;++x)
    check((mask.at(x,y)!=0)==!(x>=1 && x<6 && y>=1 && y<4),"Inverse differs from literal rectangle complement");
  check(after.data().previous_selection==initial,"Inverse did not capture Previous");
  check(after.data().dataset.shares_pixels(before.data().dataset),"Menu inverse changed pixels");
  check(after.display().t==1 && after.display().c==1 && after.display().crosshair[2]==1,"Menu inverse changed TCZ");
  check(owner->undo_steps()==0 && !after.dirty() && window.workflow.empty(),"Menu inverse changed persistent history");
  input.menu("restore_selection");check(owner->snapshot().data().selection==initial,"Restore did not recover exact geometry");
  input.menu("select_none");input.menu("invert_selection");
  check(owner->snapshot().data().selection==Selection("rectangle",{{0,0},{7,5}}),"No-selection inverse did not select all");
  // Rendering may read the visible plane. The API-only suite separately proves zero reads.
  return {{"mode",mode},{"state","passed"},{"deliveries",input.deliveries},{"plane_reads",reads.counts().plane_reads}};
}
}
int main(int argc,char **argv){try{
  check(argc==2,"Supply a fresh owned output directory");
  const auto root=std::filesystem::absolute(std::filesystem::u8path(argv[1]));check(!std::filesystem::exists(root),"Output must be fresh");std::filesystem::create_directories(root);
  Json results=Json::array();for(int mode=0;mode<6;++mode){results.push_back(run(root,mode));std::cout<<"PASS native selection menu "<<mode<<'\n';}
  std::ofstream(root/"results.json")<<Json{{"state","passed"},{"cases",results.size()},{"results",results}}.dump(2)<<'\n';return 0;
}catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 1;}}
