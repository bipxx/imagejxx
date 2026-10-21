#include "auto_threshold_ui_probe.hpp"
#include "headless_session_internal.hpp"
#include "window.hpp"
#include <stdexcept>
#include <algorithm>

namespace imagejxx::desktop {
namespace {
void require(bool value, const std::string &message) {
  if (!value) throw std::runtime_error(message);
}
Json state(Window &window, const std::shared_ptr<imagejxx::Document> &owner) {
  const auto snapshot=owner->snapshot();const auto &data=snapshot.data();
  return {{"pixels",data.dataset.plane({0,0,0})->pixels},
      {"selection",data.selection?selectionJson(*data.selection):Json()},
      {"previous",selectionJson(*data.previous_selection)},
      {"roi",selectionJson(data.rois.at(0).selection)},
      {"attributes",metadataJson(data.rois.at(0).attributes)},
      {"undo",owner->undo_steps()},{"redo",owner->redo_steps()},
      {"workflow",window.workflow}};
}
}
Json autoThresholdUiCase(Window &window,const std::string &case_id,
                   const std::string &context,const std::string &directory) {
  std::string method=case_id;bool ignore_black=false,ignore_white=false;
  const auto marker=method.find("-ignore-");
  if(marker!=std::string::npos) {
    const auto option=method.substr(marker+8);method.resize(marker);
    require(option=="black" || option=="white" || option=="both","Unknown threshold exclusion fixture");
    ignore_black=option=="black" || option=="both";ignore_white=option=="white" || option=="both";
  }
  validateRecord("auto_mask",{{"method",method},{"ignore_black",ignore_black},{"ignore_white",ignore_white}});
  namespace fs=std::filesystem;const auto output=fs::absolute(fs::u8path(directory));
  require(!fs::exists(output) && window.documents().empty(),"Fresh threshold fixture required");
  fs::create_directories(output);
  Image image(16,16,1,PixelType::UInt8);
  for(std::size_t i=0;i<image.pixels.size();++i)image.pixels[i]=static_cast<double>(i);
  DocumentData data{Dataset::from_stack({std::move(image)})};
  data.previous_selection=Selection("line",{{0,0},{6,4}});
  data.rois={{"retained",Selection("rectangle",{{1,1},{6,4}}),std::nullopt,std::nullopt,0,{{"opaque",std::string("retained")}}}};
  auto owner=std::make_shared<imagejxx::Document>(std::move(data));
  window.updateSettings({{"processing_workers",1},{"processing_memory_mb",64}},false);
  window.addNativeDocument(owner);window.recording=true;
  Json report{{"contract",ignore_black || ignore_white?"p10a.auto-threshold-options.ui.001":"p09z.auto-threshold.ui.001"},{"case_id",case_id},{"method",method},{"ignore_black",ignore_black},{"ignore_white",ignore_white},
      {"context",context},{"acceptance_claim",false},{"state","running"},
      {"frames",Json::array()},{"controls",Json::array()},
      {"states",{{"before",state(window,owner)}}},
      {"api_fixture_actions",{"Owned ordinary legacy-stack UInt8 16x16 uniform histogram with retained previous selection and saved row; one worker, 64 MiB budget"}}};
  struct Capture {Json &report;fs::path output;bool done=false;~Capture(){if(!done)try{
    report["state"]="failed";writeJson((output/"failure.json").u8string(),report);
  }catch(...){}}}capture{report,output};
  HeadlessInputSession session(window);Json last;
  const auto frame=[&](const Json &events=Json::array()) {
    require(report["frames"].size()<150,"Threshold UI frame bound");
    last=session.frame(events);Json rendered;
    for(const auto *key:{"input_frame","host_controls","operation_dialog","window_error"})rendered[key]=last.at(key);
    require(last.at("input_frame").at("raw_inputs")==events,"Threshold raw input mismatch");
    report["frames"].push_back({{"events",events},{"rendered",std::move(rendered)}});
    require(window.error().empty(),"Threshold UI reported an error");
  };
  const auto click=[&](const std::string &name) {
    const auto control=name.rfind("dialog.",0)==0
        ?last.at("operation_dialog").at("controls").at(name.substr(7))
        :last.at("host_controls").at(name);
    require(control.at("enabled").get<bool>() && control.at("id").get<std::uint64_t>(),"Threshold control unavailable: "+name);
    report["controls"].push_back({{"name",name},{"control",control}});
    frame(Json::array({{{"type","position"},{"space","screen"},{"point",control.at("point")}}}));
    for(bool down:{true,false})frame(Json::array({{{"type","button"},{"name","left"},{"down",down}}}));
    frame();
  };
  frame();frame();frame();
  click("menu.Process");click("menu.Process.Threshold");click("menu.command.auto_mask");
  require(last.at("operation_dialog").at("id")=="auto_mask","Threshold menu opened another operation");
  click("dialog.method");
  {auto point=last.at("operation_dialog").at("controls").at("method.choice.otsu").at("point");
   frame(Json::array({{{"type","position"},{"space","screen"},{"point",point}}}));
   frame(Json::array({{{"type","wheel"},{"x",0},{"y",-12}}}));frame();frame();}
  click("dialog.method.choice."+method);
  if(ignore_black)click("dialog.ignore_black");if(ignore_white)click("dialog.ignore_white");
  report["selected_parameters"]=last.at("operation_dialog").at("parameters");
  click("dialog.preview");window.wait();window.poll();frame();frame();
  require(last.at("operation_dialog").value("has_preview",false),"Threshold preview missing");
  report["preview_image"]=last.at("operation_dialog").at("preview_image");
  report["states"]["preview"]=state(window,owner);
  click("dialog.cancel");frame();report["states"]["canceled"]=state(window,owner);
  click("menu.Process");click("menu.Process.Threshold");click("menu.command.auto_mask");
  click("dialog.method");
  {auto point=last.at("operation_dialog").at("controls").at("method.choice.otsu").at("point");
   frame(Json::array({{{"type","position"},{"space","screen"},{"point",point}}}));
   frame(Json::array({{{"type","wheel"},{"x",0},{"y",-12}}}));frame();frame();}
  click("dialog.method.choice."+method);
  if(ignore_black)click("dialog.ignore_black");if(ignore_white)click("dialog.ignore_white");
  click("dialog.apply");window.wait();window.poll();frame();frame();
  report["states"]["applied"]=state(window,owner);
  require(owner->undo_steps()==1 && window.workflow.size()==1,"Threshold Apply did not publish once");
  click("menu.Edit");click("menu.Edit.Undo");report["states"]["undone"]=state(window,owner);
  click("menu.Edit");click("menu.Edit.Redo");report["states"]["redone"]=state(window,owner);
  report["final_error"]=window.error();session.close();report["state"]="observed";
  require(report.dump().size()<16*1048576,"Threshold UI report bound");
  writeJson((output/"case.json").u8string(),report);capture.done=true;return report;
}

}
