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
Json shadowPresetUiCase(Window &window,const std::string &caseId,
                       const std::string &context,const std::string &directory) {
  const auto split=caseId.find('.');require(split!=std::string::npos,"Shadow preset case requires a mode");
  const auto mode=caseId.substr(0,split),argument=caseId.substr(split+1);
  const bool guard=mode=="guard",failure=mode=="failure",scientific=mode=="scientific" || (guard && argument!="ordinary-f64");
  require(mode=="ordinary" || mode=="stack" || mode=="scientific" || guard || failure,"Unknown shadow preset mode");
  if(failure)require(argument=="float32","Unknown shadow failure case");
  else if(!guard)validateRecord("shadow_imagej",{{"direction",argument}});
  else require(argument=="ordinary-f64" || argument=="scientific-f64" || argument=="scientific-rgba" || argument=="scientific-ieee","Unknown shadow guard");
  namespace fs=std::filesystem;const auto output=fs::absolute(fs::u8path(directory));
  require(!fs::exists(output) && window.documents().empty(),"Fresh shadow preset fixture required");fs::create_directories(output);
  const int samples=argument=="scientific-rgba"?4:1;
  const auto type=failure?PixelType::Float32:guard?(argument=="scientific-ieee"?PixelType::Float32:argument=="scientific-rgba"?PixelType::UInt8:PixelType::Float64):PixelType::UInt8;
  const DatasetShape shape{scientific?2:1,scientific?2:1,scientific || mode=="stack"?3:1,5,7,samples};Stack planes;
  for(std::size_t p=0;p<shape.plane_count();++p) {
    Image image(7,5,samples,type);
    for(std::size_t i=0;i<image.pixels.size();++i)image.pixels[i]=failure?(i/7==0?1e38:i/7==2?-1e38:0):double((i*37+13)%256);
    planes.push_back(std::move(image));
  }
  const auto policy=argument=="scientific-ieee"?FloatingSamplePolicy::PreserveIEEEFloat32:FloatingSamplePolicy::FiniteOnly;
  DocumentData data{scientific?Dataset::from_planes(shape,std::move(planes),{},policy):Dataset::from_stack(std::move(planes))};
  data.previous_selection=Selection("line",{{0,0},{6,4}});
  data.rois={{"retained",Selection("rectangle",{{1,1},{6,4}}),scientific?std::optional<int>(1):std::nullopt,scientific?std::optional<int>(1):std::nullopt,shape.z>1?1:0,{{"opaque",std::string("retained")}}}};
  auto owner=std::make_shared<imagejxx::Document>(std::move(data));
  auto display=owner->snapshot().display();display.t=display.c=scientific?1:0;display.crosshair={3,2,shape.z>1?1:0};owner->set_display(display);
  window.addNativeDocument(owner);window.updateSettings({{"processing_workers",1},{"processing_memory_mb",64}},false);
  window.recording=true;window.workflow=Json::array({{{"operation","subtract"},{"parameters",{{"value",3}}},{"all_slices",false}}});
  const auto snapshot=[&] {
    auto result=state(window,owner);result["pixels"]=Json::array();const auto held=owner->snapshot();
    for(int t=0;t<shape.t;++t)for(int c=0;c<shape.c;++c)for(int z=0;z<shape.z;++z)result["pixels"].push_back(held.data().dataset.plane({t,c,z})->pixels);
    result["t"]=held.display().t;result["c"]=held.display().c;result["crosshair"]=held.display().crosshair;return result;
  };
  Json report{{"contract","p09l.shadow-presets.ui.001"},{"case_id",caseId},{"context",context},{"mode",mode},{"direction",guard?Json():Json(failure?"north":argument)},
      {"state","running"},{"acceptance_claim",false},{"shape",{shape.t,shape.c,shape.z,5,7,samples}},
      {"frames",Json::array()},{"controls",Json::array()},{"states",{{"before",snapshot()}}}};
  struct Capture {Json &r;fs::path p;bool done=false;~Capture(){if(!done)try{r["state"]="failed";writeJson((p/"failure.json").u8string(),r);}catch(...){}}}capture{report,output};
  HeadlessInputSession session(window);Json last;
  const auto frame=[&](const Json &events=Json::array()) {
    require(report["frames"].size()<240,"Shadow preset frame bound");last=session.frame(events);Json rendered;
    for(const auto *key:{"input_frame","host_controls","operation_dialog","native_workflow","window_error"})rendered[key]=last.at(key);
    require(last.at("input_frame").at("raw_inputs")==events,"Shadow preset raw input mismatch");
    report["frames"].push_back({{"events",events},{"rendered",std::move(rendered)}});if(!failure)require(window.error().empty(),"Shadow preset window error");
  };
  const auto click=[&](const std::string &name) {
    const Json control=name.rfind("dialog.",0)==0?last.at("operation_dialog").at("controls").at(name.substr(7)):
        name.rfind("review.",0)==0?last.at("native_workflow").at("controls").at(name.substr(7)):last.at("host_controls").at(name);
    require(control.value("enabled",true) && control.at("id").get<std::uint64_t>(),"Shadow preset control unavailable: "+name);
    report["controls"].push_back({{"name",name},{"control",control}});
    frame(Json::array({{{"type","position"},{"space","screen"},{"point",control.at("point")}}}));
    for(bool down:{true,false})frame(Json::array({{{"type","button"},{"name","left"},{"down",down}}}));frame();
  };
  const auto settle=[&] {window.wait();window.poll();frame();frame();};
  frame();frame();frame();click("menu.Process");click("menu.Process.Shadows");
  if(guard) {
    report["disabled_controls"]=Json::object();
    for(const auto *id:{"north","northeast","east","southeast","south","southwest","west","northwest"}) {
      const auto key=std::string("menu.shadow.")+id;const auto control=last.at("host_controls").at(key);
      require(!control.at("enabled").get<bool>(),"Unsupported shadow preset was enabled");report["disabled_controls"][key]=control;
    }
    const auto control=last.at("host_controls").at("menu.command.shadow_imagej");require(!control.at("enabled").get<bool>(),"Unsupported generic shadow menu was enabled");
    report["disabled_controls"]["menu.command.shadow_imagej"]=control;report["states"]["after"]=snapshot();
  } else if(failure) {
    require(window.error().empty(),"Shadow preset source failed before invocation");
    click("menu.shadow.north");settle();
    require(window.error().find("output is not finite")!=std::string::npos,"Shadow preset output error was not visible");
    report["states"]["after"]=snapshot();
  } else {
    click("menu.shadow."+argument);
    if(mode=="stack") {
      report["seeded_dialog"]=last.at("operation_dialog");report["states"]["seeded"]=snapshot();
      click("dialog.all_slices");report["stack_choice"]=last.at("operation_dialog");click("dialog.preview");settle();
      report["preview_image"]=last.at("operation_dialog").at("preview_image");report["states"]["preview"]=snapshot();click("dialog.apply");
    } else if(scientific) {
      require(last.at("native_workflow").at("visible").get<bool>(),"Scientific preset skipped target review");
      report["states"]["seeded"]=snapshot();click("review.confirm");click("review.prepare");settle();click("review.apply");
    } else require(!last.at("operation_dialog").at("open").get<bool>(),"Single-image preset opened a dialog");
    settle();report["states"]["applied"]=snapshot();if(scientific)click("review.cancel");
    click("menu.Edit");click("menu.Edit.Undo");report["states"]["undone"]=snapshot();
    click("menu.Edit");click("menu.Edit.Redo");report["states"]["redone"]=snapshot();
    require(owner->undo_steps()==1 && window.workflow.size()==2,"Shadow preset did not publish exactly once");
  }
  report["final_error"]=window.error();session.close();report["state"]="observed";require(report.dump().size()<32*1048576,"Shadow preset report bound");
  writeJson((output/"case.json").u8string(),report);capture.done=true;return report;
}
}
