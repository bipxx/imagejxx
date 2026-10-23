#include "window.hpp"
#include "headless_session_internal.hpp"
#include <filesystem>
#include <fstream>
#include <iostream>
using namespace imagejxx;
using imagejxx::desktop::Window;
using imagejxx::desktop::Json;
using imagejxx::desktop::HeadlessInputSession;
namespace {
void check(bool value,const std::string &message){if(!value)throw std::runtime_error(message);}
class Input {
public:
 Window &window;HeadlessInputSession session;Json report,deliveries=Json::array();
 explicit Input(Window &w):window(w),session(w){frame();frame();}
 void frame(const Json &events=Json::array()){report=session.frame(events);}
 Json control(const std::string &name){return name.rfind("dialog.",0)==0?report.at("dialog").at("controls").at(name.substr(7)):report.at("host_controls").at(name);}
 void click(const std::string &name){
  auto hit=control(name);check(hit.at("enabled").get<bool>(),"Disabled control "+name);
  for(int i=0;i<3;++i){frame(Json::array({{{"type","position"},{"space","screen"},{"point",hit.at("point")}}}));auto next=control(name);if(next.at("point")==hit.at("point"))break;hit=next;}
  deliveries.push_back({{"selector",name},{"control",hit}});
  for(bool down:{true,false})frame(Json::array({{{"type","button"},{"name","left"},{"down",down}}}));frame();
 }
 void menu(bool shrink=false){click("menu.Edit");click("menu.Edit.Selection");click(shrink?"menu.form.shrink_selection":"menu.form.grow_selection");}
 void text(const std::string &value){
  click("dialog.distance");
  check(report.at("input_frame").at("focus").at("active_id")==control("dialog.distance").at("id"),"Distance input did not acquire focus");
  for(bool down:{true,false})frame(Json::array({{{"type","keys"},{"keys",Json::array({{{"name","Ctrl"},{"down",down}},{{"name","A"},{"down",down}}})}}}));
  frame(Json::array({{{"type","text"},{"value",value}}}));
  for(bool down:{true,false})frame(Json::array({{{"type","key"},{"name","Enter"},{"down",down}}}));frame();
 }
};
Json run(const std::filesystem::path &root,int mode){
 Stack planes;for(int i=0;i<8;++i)planes.emplace_back(9,9,mode==1?3:1,mode==2?PixelType::Float32:PixelType::UInt8);
 SpatialCalibration cal;cal.spacing={.5,1.5,2};cal.unit="um";
 auto owner=std::make_shared<Document>(Dataset::from_planes({2,2,2,9,9,mode==1?3:1},std::move(planes),cal,
     mode==2?FloatingSamplePolicy::PreserveIEEEFloat32:FloatingSamplePolicy::FiniteOnly));
 Image mask(9,9,1,PixelType::UInt8);mask.at(4,4)=255;owner->select(Selection::from_mask(mask));
 auto view=owner->snapshot().display();view.t=view.c=view.crosshair[2]=1;owner->set_display(view);
 Window window((root/(std::to_string(mode)+"-prefs.json")).u8string());window.addVirtualDocument(owner);window.recording=true;
 window.updateSettings({{"selection_enlargement_distance",2.},{"selection_enlargement_units","pixels"}},false);
 Input input(window);const auto before=owner->snapshot();input.menu();
 check(input.report.at("dialog").at("values")==Json{{"distance",2.},{"units","pixels"}},"Dialog did not consume independent defaults");
 input.click("dialog.units");input.click("dialog.units.choice.calibrated");
 check(input.report.at("dialog").at("values").at("distance")==1.,"Changing units did not convert the distance");
 check(input.report.at("dialog").at("controls").contains("anisotropy_warning"),"Anisotropy warning missing");
 input.text("-");check(!input.control("dialog.apply").at("enabled").get<bool>() && input.report.at("dialog").at("text").at("distance")=="-","Invalid draft was normalized or enabled");
 check(owner->snapshot().shares_data(before) && window.workflow.empty(),"Invalid draft published geometry");
 input.text("1");input.click("dialog.apply");input.frame();
 check(window.error().empty(),"Dialog application failed: "+window.error());
 const auto after=owner->snapshot();const auto result=after.data().selection->mask(9,9);int cells=0;
 for(int y=0;y<9;++y)for(int x=0;x<9;++x){const int dx=x-4,dy=y-4;check((result.at(x,y)!=0)==(dx*dx+dy*dy<=6),"Dialog pixels differ from literal radius2");cells+=result.at(x,y)!=0;}
 check(cells==21 && after.data().dataset.shares_pixels(before.data().dataset),"Dialog changed source pixels");
 check(window.workflow.size()==1 && window.workflow[0].at("schema")=="imagejxx.selection-enlargement-command" && window.workflow[0].at("units")=="calibrated","Dialog did not record complete calibrated command");
 check(!after.dirty() && owner->undo_steps()==0 && after.display().t==1 && after.display().c==1 && after.display().crosshair[2]==1,"Dialog changed history/navigation");
 input.menu(true);check(input.report.at("dialog").at("values").at("distance")==-2.,"Shrink default sign differs");
 input.click("dialog.cancel");input.frame();check(owner->snapshot().shares_data(after) && window.workflow.size()==1,"Cancel published geometry");
 input.menu();view=owner->snapshot().display();view.t=0;owner->set_display(view);const auto stale=owner->snapshot();input.click("dialog.apply");input.frame();
 check(!window.error().empty() && owner->snapshot().shares_data(stale) && window.workflow.size()==1,"Stale captured source was published");
 return {{"mode",mode},{"state","passed"},{"deliveries",input.deliveries},{"record",window.workflow[0]}};
}
}
int main(int argc,char **argv){try{
 check(argc==2,"Supply a fresh owned output directory");const auto root=std::filesystem::absolute(std::filesystem::u8path(argv[1]));check(!std::filesystem::exists(root),"Output must be fresh");std::filesystem::create_directories(root);
 Json results=Json::array();for(int mode=0;mode<3;++mode){results.push_back(run(root,mode));std::cout<<"PASS enlargement dialog "<<mode<<'\n';}
 std::ofstream(root/"results.json")<<Json{{"state","passed"},{"cases",3},{"results",results}}.dump(2)<<'\n';return 0;
}catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 1;}}
