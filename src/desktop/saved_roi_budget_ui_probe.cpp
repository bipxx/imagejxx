#include "saved_roi_budget_ui_probe.hpp"
#include "headless_session_internal.hpp"
#include "window.hpp"
#include <imagejxx/saved_roi_geometry.hpp>
#include <imagejxx/selection_edit.hpp>
#include <cstring>

namespace imagejxx::desktop {
namespace {
void require(bool condition,const std::string &message) {
  if(!condition)throw std::runtime_error(message);
}
Json state(const std::shared_ptr<imagejxx::Document> &owner,Window &w) {
  const auto s=owner->snapshot();Json samples=Json::array();
  for(int z=0;z<2;++z) {
    Json words=Json::array();
    for(double d:s.data().dataset.plane({0,0,z})->pixels) {
      const float f=static_cast<float>(d);std::uint32_t word;
      std::memcpy(&word,&f,sizeof word);words.push_back(word);
    }
    samples.push_back(std::move(words));
  }
  const auto &r=s.data().rois.at(0);
  return {{"selection",selectionJson(*s.data().selection)},
      {"previous",selectionJson(*s.data().previous_selection)},
      {"roi",selectionJson(r.selection)},{"attributes",metadataJson(r.attributes)},
      {"position",{s.display().t,s.display().c,s.display().crosshair[2]}},
      {"undo",owner->undo_steps()},{"redo",owner->redo_steps()},
      {"workflow",w.workflow},{"samples",std::move(samples)}};
}
}
Json savedRoiBudgetUiCase(Window &w,const std::string &variant,
                         const std::string &context,const std::string &directory) {
  namespace fs=std::filesystem;
  require(variant=="finite-axis" || variant=="finite-rotated" ||
          variant=="ieee-axis" || variant=="ieee-rotated","Unknown saved-area budget case");
  const bool ieee=variant.rfind("ieee-",0)==0,rotated=variant.find("rotated")!=std::string::npos;
  const auto dir=fs::absolute(fs::u8path(directory));
  require(!fs::exists(dir) && w.documents().empty(),"Fresh saved-area budget fixture required");
  fs::create_directories(dir);Stack planes;
  const std::uint32_t bits[]{0,0x80000000u,0x7f800000u,0xff800000u,
                            0x7fc12345u,0xffc54321u,1,0x80000001u};
  for(int z=0;z<2;++z) {
    Image image(32,24,1,PixelType::Float32);
    for(std::size_t i=0;i<image.pixels.size();++i) {
      if(ieee && i%9<8) {float f;std::memcpy(&f,&bits[i%9],sizeof f);image.pixels[i]=f;}
      else image.pixels[i]=double((i+7*z)%251);
    }
    planes.push_back(std::move(image));
  }
  DocumentData data{Dataset::from_planes({1,1,2,24,32,1},std::move(planes),{},
      ieee?FloatingSamplePolicy::PreserveIEEEFloat32:FloatingSamplePolicy::FiniteOnly)};
  data.selection=Selection("rectangle",{{0,0},{1,1}});
  data.previous_selection=Selection("line",{{0,23},{31,0}});
  Selection area("rounded_rectangle",{{6,5},{24,19}});
  area.attributes={{"corner_diameter",std::int64_t(8)},{"label",std::string("retained")}};
  if(rotated)area=SelectionEditor(area,32,24).rotated(23);
  data.rois={{"owned area",area,0,0,0,{{"opaque",std::string(131072,'x')}}}};
  DisplayState view;view.crosshair={16,12,1};view.zoom=10;
  auto owner=std::make_shared<imagejxx::Document>(std::move(data),view);
  auto admissionView=view;admissionView.crosshair[2]=0;
  const auto estimate=estimate_saved_roi_geometry(owner->snapshot().with_display(admissionView),SavedRoiGeometryRequest(0,area));
  const auto high=static_cast<int>((estimate.working_bytes+1048575)/1048576+4);
  require(estimate.working_bytes>1048576 && high<=32,"Saved-area budget fixture estimate out of bounds");
  w.updateSettings({{"processing_memory_mb",1},{"processing_workers",1},{"show_rois",true}},false);
  w.addNativeDocument(owner);w.recording=true;
  Json report{{"contract","p08y.saved-area-budget.ui.001"},{"case_id",variant},
      {"context",context},{"acceptance_claim",false},{"estimate_bytes",estimate.working_bytes},
      {"high_mb",high},{"frames",Json::array()},{"controls",Json::array()},
      {"states",{{"before",state(owner,w)}}},
      {"api_fixture_actions",{"Reviewed eager Float32 source with two Z planes",
       "Opaque saved-row metadata forces the logical estimate above one MiB",
       "Initial one-MiB budget; subsequent changes use actual Preferences controls"}}};
  struct Capture {Json &r;fs::path dir;bool done=false;~Capture(){if(!done)try{
    r["state"]="failed";if(r.dump().size()<64*1048576)writeJson((dir/"failure.json").u8string(),r);
  }catch(...){}}} capture{report,dir};
  HeadlessInputSession session(w);Json last;
  auto frame=[&](const Json &events=Json::array()) {
    require(report["frames"].size()<500,"Saved-area budget frame bound");
    last=session.frame(events);Json projection;
    for(const auto *key:{"input_frame","selection_properties","saved_roi_geometry",
                        "preferences_panel","window_error","error_modal"})projection[key]=last.at(key);
    require(last.at("input_frame").at("raw_inputs")==events,"Saved-area budget input mismatch");
    report["frames"].push_back({{"events",events},{"rendered",std::move(projection)}});
  };
  auto control=[&](const std::string &key)->Json {
    if(key=="error.ok")return last.at("error_modal").at("controls").at("ok");
    if(key=="preferences.memory")return last.at("preferences_panel").at("fields").at("processing_memory_mb");
    if(key.rfind("preferences.",0)==0)return last.at("preferences_panel").at("widget_controls").at(key.substr(12));
    if(key.rfind("numeric.",0)==0) {
      const auto name=key.substr(8);auto c=last.at("selection_properties").at("control_states").at(name);
      c["point"]=last.at("selection_properties").at("controls").at(name);
      c["enabled"]=!c.at("disabled").get<bool>();return c;
    }
    return last.at("host_controls").at(key);
  };
  auto click=[&](const std::string &key) {
    const auto c=control(key);
    require(c.at("enabled").get<bool>() && c.at("id").get<std::uint64_t>(),"Unavailable saved-area control: "+key);
    report["controls"].push_back({{"name",key},{"control",c}});
    frame(Json::array({{{"type","position"},{"space","screen"},{"point",c.at("point")}}}));
    for(bool down:{true,false})frame(Json::array({{{"type","button"},{"name","left"},{"down",down}}}));
    frame();
  };
  auto text=[&](const std::string &key,const std::string &value) {
    click(key);
    for(bool down:{true,false})frame(Json::array({{{"type","keys"},{"keys",Json::array({
      {{"name","Ctrl"},{"down",down}},{{"name","A"},{"down",down}}})}}}));
    frame(Json::array({{{"type","text"},{"value",value}}}));frame();
  };
  auto budget=[&](int mb) {
    click("menu.Edit");click("menu.Edit.Preferences");frame();
    text("preferences.search","processing_memory_mb");text("preferences.memory",std::to_string(mb));
    click("preferences.apply");require(w.settings().integer("processing_memory_mb")==mb,"Preferences budget did not apply");
    click("preferences.close");frame();
  };
  auto refused=[&](const std::string &label) {
    report[label]={{"error",w.error()},{"modal",last.at("error_modal")},
      {"draft",last.at("saved_roi_geometry")},{"properties",last.at("selection_properties")}};
    report["states"][label]=state(owner,w);
    require(last.at("error_modal").value("open",false) && !w.error().empty(),
            "Saved-area budget refusal was not visible to the user");
    require(!last.at("saved_roi_geometry").value("active",true) &&
            !last.at("selection_properties").value("open",true),"Refused saved-area draft retained");
    require(owner->undo_steps()==0 && w.workflow.empty(),"Refused saved-area edit published");
    click("error.ok");frame();
  };
  frame();frame();frame();
  click("roi_manager.row.0.edit_saved_area");refused("entry_refused");
  budget(high);click("roi_manager.row.0.edit_saved_area");
  require(last.at("saved_roi_geometry").value("active",false),"Recovered saved-area entry absent");
  text("numeric.Corner diameter","3");report["states"]["preview"]=state(owner,w);
  budget(1);
  require(last.at("selection_properties").value("open",false),"Preferences unexpectedly discarded numeric draft");
  click("numeric.Apply");refused("apply_refused");
  budget(high);click("roi_manager.row.0.edit_saved_area");text("numeric.Corner diameter","3");
  click("numeric.Apply");report["states"]["applied"]=state(owner,w);
  require(owner->undo_steps()==1 && w.workflow.size()==1,"Recovered saved-area edit did not publish once");
  click("menu.Edit");click("menu.Edit.Undo");report["states"]["undone"]=state(owner,w);
  click("menu.Edit");click("menu.Edit.Redo");report["states"]["redone"]=state(owner,w);
  report["final_error"]=w.error();session.close();report["state"]="observed";
  require(report.dump().size()<64*1048576,"Saved-area budget report byte bound");
  writeJson((dir/"case.json").u8string(),report);capture.done=true;return report;
}
}
