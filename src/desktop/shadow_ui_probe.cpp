#include "shadow_ui_probe.hpp"
#include "headless_session_internal.hpp"
#include "window.hpp"
#include <stdexcept>

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
Json shadowUiCase(Window &window,const std::string &direction,
                   const std::string &context,const std::string &directory) {
  validateRecord("shadow_imagej",{{"direction",direction}});
  namespace fs=std::filesystem;const auto output=fs::absolute(fs::u8path(directory));
  require(!fs::exists(output) && window.documents().empty(),"Fresh shadow fixture required");
  fs::create_directories(output);
  Image image(7,5,1,PixelType::UInt8);
  for(std::size_t i=0;i<image.pixels.size();++i)image.pixels[i]=static_cast<double>((i*37+13)%256);
  DocumentData data{Dataset::from_stack({std::move(image)})};
  data.previous_selection=Selection("line",{{0,0},{6,4}});
  data.rois={{"retained",Selection("rectangle",{{1,1},{6,4}}),std::nullopt,std::nullopt,0,{{"opaque",std::string("retained")}}}};
  auto owner=std::make_shared<imagejxx::Document>(std::move(data));
  window.updateSettings({{"processing_workers",1},{"processing_memory_mb",64}},false);
  window.addNativeDocument(owner);window.recording=true;
  Json report{{"contract","p09g.shadows.ui.001"},{"case_id",direction},
      {"context",context},{"acceptance_claim",false},{"state","running"},
      {"frames",Json::array()},{"controls",Json::array()},
      {"states",{{"before",state(window,owner)}}},
      {"api_fixture_actions",{"Owned ordinary legacy-stack UInt8 7x5 gradient with retained previous selection and saved row; one worker, 64 MiB budget"}}};
  struct Capture {Json &report;fs::path output;bool done=false;~Capture(){if(!done)try{
    report["state"]="failed";writeJson((output/"failure.json").u8string(),report);
  }catch(...){}}}capture{report,output};
  HeadlessInputSession session(window);Json last;
  const auto frame=[&](const Json &events=Json::array()) {
    require(report["frames"].size()<120,"Shadow UI frame bound");
    last=session.frame(events);Json rendered;
    for(const auto *key:{"input_frame","host_controls","operation_dialog","window_error"})rendered[key]=last.at(key);
    require(last.at("input_frame").at("raw_inputs")==events,"Shadow raw input mismatch");
    report["frames"].push_back({{"events",events},{"rendered",std::move(rendered)}});
    require(window.error().empty(),"Shadow UI reported an error");
  };
  const auto click=[&](const std::string &name) {
    const auto control=name.rfind("dialog.",0)==0
        ?last.at("operation_dialog").at("controls").at(name.substr(7))
        :last.at("host_controls").at(name);
    require(control.at("enabled").get<bool>() && control.at("id").get<std::uint64_t>(),"Shadow control unavailable: "+name);
    report["controls"].push_back({{"name",name},{"control",control}});
    frame(Json::array({{{"type","position"},{"space","screen"},{"point",control.at("point")}}}));
    for(bool down:{true,false})frame(Json::array({{{"type","button"},{"name","left"},{"down",down}}}));
    frame();
  };
  frame();frame();frame();
  click("menu.Process");click("menu.Process.Shadows");click("menu.command.shadow_imagej");
  require(last.at("operation_dialog").at("id")=="shadow_imagej","Shadow menu opened another operation");
  click("dialog.direction");click("dialog.direction.choice."+direction);
  report["selected_parameters"]=last.at("operation_dialog").at("parameters");
  click("dialog.preview");window.wait();window.poll();frame();frame();
  require(last.at("operation_dialog").value("has_preview",false),"Shadow preview missing");
  report["preview_image"]=last.at("operation_dialog").at("preview_image");
  report["states"]["preview"]=state(window,owner);
  click("dialog.cancel");frame();report["states"]["canceled"]=state(window,owner);
  click("menu.Process");click("menu.Process.Shadows");click("menu.command.shadow_imagej");
  click("dialog.direction");click("dialog.direction.choice."+direction);
  click("dialog.apply");window.wait();window.poll();frame();frame();
  report["states"]["applied"]=state(window,owner);
  require(owner->undo_steps()==1 && window.workflow.size()==1,"Shadow Apply did not publish once");
  click("menu.Edit");click("menu.Edit.Undo");report["states"]["undone"]=state(window,owner);
  click("menu.Edit");click("menu.Edit.Redo");report["states"]["redone"]=state(window,owner);
  report["final_error"]=window.error();session.close();report["state"]="observed";
  require(report.dump().size()<16*1048576,"Shadow UI report bound");
  writeJson((output/"case.json").u8string(),report);capture.done=true;return report;
}
}
