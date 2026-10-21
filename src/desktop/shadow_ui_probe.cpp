#include "shadow_ui_probe.hpp"
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
Json scientificShadowUiCase(Window &window,const std::string &direction,
                           const std::string &context,const std::string &directory) {
  validateRecord("shadow_imagej",{{"direction",direction}});
  namespace fs=std::filesystem;const auto output=fs::absolute(fs::u8path(directory));
  require(!fs::exists(output) && window.documents().empty(),"Fresh scientific shadow fixture required");
  fs::create_directories(output);const DatasetShape shape{2,2,3,5,7,1};Stack planes;
  for(std::size_t p=0;p<shape.plane_count();++p) {
    Image image(7,5,1,PixelType::UInt8);
    for(std::size_t i=0;i<image.pixels.size();++i)image.pixels[i]=double((i*37+13)%256);
    planes.push_back(std::move(image));
  }
  DocumentData data{Dataset::from_planes(shape,std::move(planes))};
  data.previous_selection=Selection("line",{{0,0},{6,4}});
  data.rois={{"retained",Selection("rectangle",{{1,1},{6,4}}),1,1,1,{{"opaque",std::string("retained")}}}};
  auto owner=std::make_shared<imagejxx::Document>(std::move(data));
  auto display=owner->snapshot().display();display.t=display.c=1;display.crosshair={3,2,1};display.orthogonal=true;
  owner->set_display(display);window.addNativeDocument(owner);
  window.updateSettings({{"processing_workers",1},{"processing_memory_mb",64}},false);
  window.workflow=Json::array({{{"operation","subtract"},{"parameters",{{"value",3}}},{"all_slices",false}}});
  window.recording=true;
  const auto snapshot=[&] {
    auto result=state(window,owner);result["pixels"]=Json::array();const auto held=owner->snapshot();
    for(int t=0;t<2;++t)for(int c=0;c<2;++c)for(int z=0;z<3;++z)
      result["pixels"].push_back(held.data().dataset.plane({t,c,z})->pixels);
    result["t"]=held.display().t;result["c"]=held.display().c;result["crosshair"]=held.display().crosshair;
    return result;
  };
  Json report{{"contract","p09k.scientific-shadows.ui.001"},{"case_id",direction},{"context",context},
      {"state","running"},{"acceptance_claim",false},{"frames",Json::array()},
      {"controls",Json::array()},{"states",{{"before",snapshot()}}}};
  struct Capture {Json &r;fs::path p;bool done=false;~Capture(){if(!done)try{
    r["state"]="failed";writeJson((p/"failure.json").u8string(),r);
  }catch(...){}}}capture{report,output};
  HeadlessInputSession session(window);Json last;
  const auto frame=[&](const Json &events=Json::array()) {
    require(report["frames"].size()<300,"Scientific shadow frame bound");
    last=session.frame(events);Json rendered;
    for(const auto *key:{"input_frame","host_controls","native_workflow","window_error"})rendered[key]=last.at(key);
    require(last.at("input_frame").at("raw_inputs")==events,"Scientific shadow raw input mismatch");
    report["frames"].push_back({{"events",events},{"rendered",std::move(rendered)}});
    require(window.error().empty(),"Scientific shadow window error");
  };
  const auto click=[&](const std::string &name) {
    Json control;
    if(name.rfind("editor.",0)==0)control=last.at("native_workflow").at("editor").at("controls").at(name.substr(7));
    else if(name.rfind("review.",0)==0)control=last.at("native_workflow").at("controls").at(name.substr(7));
    else if(name=="viewport.close")control=last.at("native_workflow").at("viewport").at("controls").at("close");
    else control=last.at("host_controls").at(name);
    require(control.value("enabled",true) && control.at("id").get<std::uint64_t>(),"Scientific shadow control unavailable: "+name);
    report["controls"].push_back({{"name",name},{"control",control}});
    frame(Json::array({{{"type","position"},{"space","screen"},{"point",control.at("point")}}}));
    for(bool down:{true,false})frame(Json::array({{{"type","button"},{"name","left"},{"down",down}}}));
    frame();
  };
  const auto settle=[&] {window.wait();window.poll();frame();frame();};
  const auto open=[&] {
    click("menu.Process");click("menu.Process.Shadows");click("menu.command.shadow_imagej");
    require(last.at("native_workflow").at("visible").get<bool>(),"Scientific shadow review missing");
    click("review.edit_parameters");settle();click("editor.direction");
    const std::array<std::string,8> directions{"north","northeast","east","southeast","south","southwest","west","northwest"};
    const auto index=std::find(directions.begin(),directions.end(),direction)-directions.begin();
    click("editor.direction.choice."+std::to_string(index));click("editor.keep_parameters");frame();
  };
  frame();frame();frame();open();click("review.confirm");click("review.prepare_preview");settle();
  require(last.at("native_workflow").at("preview_only").get<bool>(),"Scientific shadow preview was not private");
  click("review.view_preview");frame();frame();report["preview_viewport"]=last.at("native_workflow").at("viewport");
  report["states"]["preview"]=snapshot();click("viewport.close");click("review.cancel");frame();
  report["states"]["canceled"]=snapshot();open();
  click("review.scope");click("review.scope.1");click("review.region");click("review.region.0");
  click("review.confirm");click("review.prepare");settle();report["states"]["prepared"]=snapshot();
  click("review.apply");settle();report["states"]["applied"]=snapshot();click("review.cancel");
  click("menu.Edit");click("menu.Edit.Undo");report["states"]["undone"]=snapshot();
  click("menu.Edit");click("menu.Edit.Redo");report["states"]["redone"]=snapshot();
  require(owner->undo_steps()==1 && window.workflow.size()==2,"Scientific shadow did not publish exactly once");
  report["final_error"]=window.error();session.close();report["state"]="observed";
  require(report.dump().size()<32*1048576,"Scientific shadow report bound");
  writeJson((output/"case.json").u8string(),report);capture.done=true;return report;
}
}
