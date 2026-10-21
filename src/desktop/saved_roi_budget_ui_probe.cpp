#include "saved_roi_budget_ui_probe.hpp"
#include "headless_session_internal.hpp"
#include "window.hpp"
#include <imagejxx/saved_roi_geometry.hpp>
#include <imagejxx/selection_edit.hpp>
#include <cstring>
#include <imgui.h>
#include <imgui_internal.h>
#include <cmath>

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
static Json savedRoiBudgetUiCaseImpl(Window &w,const std::string &variant,
                         const std::string &context,const std::string &directory,bool pointer) {
  namespace fs=std::filesystem;
  const bool modifierCases=pointer && variant.size()>10 && variant.compare(variant.size()-10,10,"-modifiers")==0;
  const auto baseCase=modifierCases?variant.substr(0,variant.size()-10):variant;
  require(baseCase=="finite-axis" || baseCase=="finite-rotated" ||
          baseCase=="ieee-axis" || baseCase=="ieee-rotated","Unknown saved-area budget case");
  const bool ieee=baseCase.rfind("ieee-",0)==0,rotated=baseCase.find("rotated")!=std::string::npos;
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
  Json report{{"contract",modifierCases?"p09f.saved-area-modifiers.ui.001":pointer?"p08z.saved-area-pointer-budget.ui.002":"p08y.saved-area-budget.ui.001"},{"case_id",variant},
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
    if(pointer)projection["ordinary_canvas"]=last.at("ordinary_canvas");
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
  const auto history=[&] {
    require(owner->undo_steps()==1 && w.workflow.size()==1,"Recovered saved-area edit did not publish once");
    click("menu.Edit");click("menu.Edit.Undo");report["states"]["undone"]=state(owner,w);
    click("menu.Edit");click("menu.Edit.Redo");report["states"]["redone"]=state(owner,w);
  };
  frame();frame();frame();
  click("roi_manager.row.0.edit_saved_area");refused("entry_refused");
  budget(high);click("roi_manager.row.0.edit_saved_area");
  require(last.at("saved_roi_geometry").value("active",false),"Recovered saved-area entry absent");
  if(pointer) {
    report["api_fixture_actions"].push_back("Only the in-flight budget change uses updateSettings while left mouse is held; Preferences cannot be clicked during a drag");
    const auto screen=[&](std::array<double,2> image) {
      const auto c=last.at("ordinary_canvas");std::array<double,2> p{};
      for(int i=0;i<2;++i)p[i]=c.at("origin").at(i).get<double>()+image[i]*c.at("scale").at(i).get<double>();
      return p;
    };
    const auto position=[&](std::array<double,2> p) {
      frame(Json::array({{{"type","position"},{"space","screen"},{"point",p}}}));
    };
    const auto button=[&](bool down) {
      frame(Json::array({{{"type","button"},{"name","left"},{"down",down}}}));
    };
    const auto key=[&](const char *name,bool down) {
      frame(Json::array({{{"type","key"},{"name",name},{"down",down}}}));
    };
    const auto expose=[&](std::array<double,2> first={16,12},std::array<double,2> second={18,13}) {
      auto panel=ImGui::FindWindowByName("Saved area geometry");
      // A click without motion deliberately retains the keyboard/pointer draft
      // while the numeric panel stays closed. There is then no panel to move.
      if(!panel || !panel->Active || panel->Hidden) {
        require(last.at("saved_roi_geometry").value("active",false) &&
                !last.at("selection_properties").value("open",true),"Saved-area panel visibility mismatch");
        return;
      }
      const auto a=screen(first),b=screen(second);
      const auto contains=[&](ImVec2 p) {
        for(const auto &v:{a,b})if(v[0]>=p.x && v[0]<p.x+panel->Size.x &&
            v[1]>=p.y && v[1]<p.y+panel->Size.y)return true;
        return false;
      };
      if(!contains(panel->Pos))return;
      const auto viewport=ImGui::GetMainViewport();
      const ImVec2 target(viewport->WorkPos.x+viewport->WorkSize.x-panel->Size.x-8,viewport->WorkPos.y+8);
      require(target.x>=viewport->WorkPos.x && !contains(target),"Saved-area panel cannot expose pointer path");
      const auto title=panel->TitleBarRect();
      const std::array<double,2> from{std::floor((title.Min.x+title.Max.x)*.5),std::floor((title.Min.y+title.Max.y)*.5)};
      const std::array<double,2> to{from[0]+target.x-panel->Pos.x,from[1]+target.y-panel->Pos.y};
      const auto draft=last.at("saved_roi_geometry");const auto id=panel->MoveId;
      position(from);button(true);
      require(ImGui::GetCurrentContext()->MovingWindow==panel && ImGui::GetActiveID()==id,"Saved-area title bar did not own drag");
      position(to);button(false);frame();
      require(!contains(panel->Pos) && last.at("saved_roi_geometry")==draft,"Moving panel changed saved-area draft or failed to expose canvas");
      report["panel_moves"].push_back({{"id",id},{"from",from},{"to",to}});
    };
    const auto begin=[&](const std::string &label,bool accepted,bool shift=false,bool move=true) {
      expose();if(shift)key("Shift",true);
      const auto from=screen({16,12}),to=move?screen({18,13}):from;
      position(from);button(true);
      report["gestures"].push_back({{"label",label},{"from_image",{16,12}},
        {"to_image",move?Json::array({18,13}):Json::array({16,12})},
        {"from_screen",from},{"to_screen",to},{"shift",shift},{"motion_delivered",accepted && move},
        {"down_frame",report["frames"].size()},{"down_draft",last.at("saved_roi_geometry")}});
      require(last.at("saved_roi_geometry").value("pointer",false)==accepted,"Saved-area pointer admission ignored current budget");
      if(accepted && move) {
        position(to);report["gestures"].back()["preview"]=last.at("saved_roi_geometry");
        require(last.at("saved_roi_geometry").value("pointer",false),"Saved-area pointer capture lost during preview");
      }
    };
    budget(1);report["states"]["ready"]=state(owner,w);
    begin("low_begin",false);button(false);refused("begin_refused");
    budget(high);click("roi_manager.row.0.edit_saved_area");
    begin("low_release",true);report["states"]["pointer_preview"]=state(owner,w);
    w.updateSettings({{"processing_memory_mb",1}},false);
    report["inflight_api_budget_change"]={{"memory_mb",w.settings().integer("processing_memory_mb")},
      {"role","Explicit API transition while left mouse is held; no Preferences-input credit"}};
    button(false);frame();refused("release_refused");
    budget(high);click("roi_manager.row.0.edit_saved_area");
    begin("idle_low_release",true,false,false);
    report["states"]["idle_pointer_preview"]=state(owner,w);
    require(last.at("saved_roi_geometry").at("candidate").is_null(),"Idle gesture staged geometry");
    w.updateSettings({{"processing_memory_mb",1}},false);
    report["idle_inflight_api_budget_change"]={{"memory_mb",w.settings().integer("processing_memory_mb")},
      {"role","Explicit API transition while left mouse is held without delivered motion"}};
    button(false);frame();refused("idle_release_refused");
    budget(high);click("roi_manager.row.0.edit_saved_area");
    begin("idle_recovered",true,false,false);button(false);frame();
    report["states"]["idle_recovered"]=state(owner,w);
    report["idle_recovered_draft"]=last.at("saved_roi_geometry");
    report["idle_recovered_properties"]=last.at("selection_properties");
    require(last.at("saved_roi_geometry").value("active",false) &&
            !last.at("saved_roi_geometry").value("pointer",true) &&
            owner->undo_steps()==0 && w.workflow.empty(),"Idle recovery published or lost the editable draft");
    begin("escape_cancel",true,true);key("Escape",true);key("Escape",false);
    button(false);key("Shift",false);frame();report["states"]["canceled"]=state(owner,w);
    require(!last.at("saved_roi_geometry").value("active",true) && owner->undo_steps()==0,"Escape retained or published saved-area pointer edit");
    click("roi_manager.row.0.edit_saved_area");begin("recovered_shift_move",true,true);
    button(false);key("Shift",false);frame();
    report["states"]["applied"]=state(owner,w);
    if(modifierCases) {
      history();
      report["modifier_cases"]=Json::array();
      for(const std::string label:{"ctrl_resize","shift_resize","shift_rotate","alt_move"}) {
        Json item{{"label",label},{"before",state(owner,w)}};
        click("roi_manager.row.0.edit_saved_area");frame();
        const auto box=*owner->snapshot().data().rois.at(0).selection.box_frame();
        std::array<double,2> from=box.center,to{};
        const int kind=label=="shift_rotate"?int(SelectionHandleKind::Rotate):int(SelectionHandleKind::Resize);
        if(label!="alt_move") {
          bool found=false;
          for(const auto &handle:last.at("ordinary_canvas").at("saved_roi_geometry_handles"))
            if(handle.at("kind")==kind && handle.at("index")== (label=="shift_rotate"?0:4)) {
              from=handle.at("image_point").get<std::array<double,2>>();item["handle"]=handle;found=true;break;
            }
          require(found,"Saved-area modifier handle was not rendered");
        }
        if(label=="shift_rotate") {
          const auto radians=22*std::acos(-1.)/180;
          const auto x=from[0]-box.center[0],y=from[1]-box.center[1];
          to={box.center[0]+std::cos(radians)*x-std::sin(radians)*y,
              box.center[1]+std::sin(radians)*x+std::cos(radians)*y};
        } else if(label=="alt_move") to={31,12};
        else to={from[0]+(label=="ctrl_resize"?2:1),from[1]+1};
        expose(from,to);
        const char *modifier=label=="ctrl_resize"?"Ctrl":label=="alt_move"?"Alt":"Shift";
        key(modifier,true);position(screen(from));button(true);
        item["modifier"]=modifier;item["from_image"]=from;item["to_image"]=to;
        item["down_frame"]=report["frames"].size();
        item["down_input"]=last.at("ordinary_canvas").at("saved_roi_geometry_input");
        require(last.at("saved_roi_geometry").value("pointer",false),"Modifier gesture did not capture the saved area");
        position(screen(to));item["preview"]=last.at("saved_roi_geometry");
        item["preview_input"]=last.at("ordinary_canvas").at("saved_roi_geometry_input");
        item["preview_state"]=state(owner,w);
        require(last.at("saved_roi_geometry").value("pointer",false),"Modifier preview lost pointer capture");
        button(false);key(modifier,false);frame();item["applied"]=state(owner,w);
        require(w.error().empty() && !last.at("saved_roi_geometry").value("active",true),"Modifier release failed");
        click("menu.Edit");click("menu.Edit.Undo");item["undone"]=state(owner,w);
        click("menu.Edit");click("menu.Edit.Redo");item["redone"]=state(owner,w);
        click("menu.Edit");click("menu.Edit.Undo");item["restored"]=state(owner,w);
        report["modifier_cases"].push_back(std::move(item));
      }
      report["modifier_final"]=state(owner,w);
    }
  } else {
  text("numeric.Corner diameter","3");report["states"]["preview"]=state(owner,w);
  budget(1);
  require(last.at("selection_properties").value("open",false),"Preferences unexpectedly discarded numeric draft");
  click("numeric.Apply");refused("apply_refused");
  budget(high);click("roi_manager.row.0.edit_saved_area");text("numeric.Corner diameter","3");
  click("numeric.Apply");report["states"]["applied"]=state(owner,w);
  }
  if(!modifierCases)history();
  report["final_error"]=w.error();session.close();report["state"]="observed";
  require(report.dump().size()<64*1048576,"Saved-area budget report byte bound");
  writeJson((dir/"case.json").u8string(),report);capture.done=true;return report;
}
Json savedRoiBudgetUiCase(Window &w,const std::string &variant,
                         const std::string &context,const std::string &directory) {
  return savedRoiBudgetUiCaseImpl(w,variant,context,directory,false);
}
Json savedRoiPointerBudgetUiCase(Window &w,const std::string &variant,
                                const std::string &context,const std::string &directory) {
  return savedRoiBudgetUiCaseImpl(w,variant,context,directory,true);
}
}
