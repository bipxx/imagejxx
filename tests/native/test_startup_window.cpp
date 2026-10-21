// Fresh Window integration assertions. No run()/render call occurs here:
// actual input, launcher and visible layout evidence have separate producers.
#include "window.hpp"
#include <imagejxx/preferences.hpp>
#include <imagejxx/session.hpp>
#include <imagejxx/startup.hpp>
#include <array>
#include <filesystem>
#include <fstream>
#include <iostream>

namespace {
using namespace imagejxx;
using imagejxx::desktop::Json;
using imagejxx::desktop::Window;
namespace fs = std::filesystem;
std::size_t checks = 0;
Json observations = Json::object();
void check(bool value, const char *message) {
  ++checks;
  if (!value) throw std::runtime_error(message);
}
std::string read(const fs::path &path) {
  check(fs::is_regular_file(path) && fs::file_size(path) <= 4*1024*1024, "Bounded owned regular file required");
  std::ifstream stream(path, std::ios::binary);
  return std::string((std::istreambuf_iterator<char>(stream)), {});
}
void write(const fs::path &path, const std::string &bytes) {
  check(bytes.size() <= 4*1024*1024, "Owned fixture exceeds4MiB");
  std::ofstream stream(path, std::ios::binary | std::ios::trunc);
  stream.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
  check(bool(stream), "Cannot write owned fixture");
}
Json state(const Window &window) { return window.startupState(); }
StartupRequest request(const fs::path &work) {
  StartupRequest result;
  result.launch_cwd = work.u8string();
  // Window supplies its selected write destination; callers cannot redirect it.
  return result;
}
SessionLimits limits() {
  SessionLimits result; result.max_documents = 4; result.max_planes = 14;
  result.max_samples = 672; result.max_json_bytes = result.max_encoded_bytes = 4*1024*1024;
  result.max_working_bytes = 32*1024*1024;
  return result;
}
SessionCapture capture(bool changed = false) {
  SessionCapture result;
  for (int number = 0; number < 2; ++number) {
    DatasetShape shape = number ? DatasetShape{2,3,2,6,8,1} : DatasetShape{1,1,2,6,8,1};
    Stack planes;
    for (int t=0;t<shape.t;++t) for (int c=0;c<shape.c;++c) for (int z=0;z<shape.z;++z) {
      Image image(8,6,1,number ? PixelType::UInt16 : PixelType::UInt8);
      for (int y=0;y<6;++y) for (int x=0;x<8;++x)
        image.at(x,y) = number ? 40*t+30*c+5*z+2*y+x : 7*z+3*y+x;
      if (changed && number && t==1 && c==2 && z==1) image.at(3,2) = 113;
      planes.push_back(std::move(image));
    }
    SpatialCalibration calibration; calibration.spacing={1.25,.5,2.5}; calibration.unit="um";
    DocumentData data{Dataset::from_planes(shape,std::move(planes),calibration)};
    data.metadata.title = number ? "SU U16 scientific" : "SU U8 stack";
    data.metadata.properties["original_path"] = std::string("deleted-owned-original.pgm");
    data.metadata.extensions["vendor"] = metadata_object({{"opaque",true},{"payload",std::string("retained")}});
    data.selection = Selection("rectangle",{{1,1},{5,4}});
    data.previous_selection = Selection("line",{{0,0},{7,5}});
    data.rois.push_back({"positioned",Selection("rectangle",{{2,1},{6,5}}),number?1:0,number?2:0,1,{{"inert",true}}});
    data.overlays.push_back({{"type",std::string("text")},{"text",std::string("owned overlay")}});
    DisplayState display; display.t=number?1:0; display.c=number?2:0; display.crosshair={3,2,1};
    display.zoom=2.25; display.orthogonal=number; display.linkedZoom=false;
    display.planeZoom={1.5,2.5,3.5}; display.pan={{{.25,-.5},{1.25,-1.5},{2.25,-2.5}}};
    auto owner=std::make_shared<imagejxx::Document>(std::move(data),display);
    result.documents.push_back(owner->snapshot());
  }
  result.results={metadata_object({{"Value",std::int64_t(17)}})};
  result.workflow={metadata_object({{"operation",std::string("must_never_execute")},{"historical_id",std::int64_t(777)}})};
  result.extensions=with_workspace_presentation({{"vendor",std::string("unchanged")}},0,2).extensions;
  return result;
}
fs::path preferences(const fs::path &work, const std::string &workspace = "", const char *filename="preferences.json") {
  PreferencesDocument document;
  document.extras_json=R"({"recent":["missing-recent.pgm"],"layout":"[Window][Owned]\nPos=13,17\nSize=400,300\n"})";
  if (!workspace.empty()) document.settings.update({{"startup_mode",std::string("workspace")},{"startup_workspace",workspace}});
  document.settings.update({{"history_limit",std::int64_t(3)},{"history_memory_mb",std::int64_t(1)},
                            {"shortcut_bindings",std::vector<std::string>{"analysis.measure=Ctrl+K"}}});
  const auto path=work/filename;
  check(document.save(path.u8string()),"Cannot save owned startup preferences");
  return path;
}
template<class Function> void refusal(Function function) {
  bool refused=false;
  try { function(); } catch (const std::exception &) { refused=true; }
  check(refused,"Expected startup/configuration refusal");
}
void empty(const Window &window) {
  check(window.documents().empty() && state(window)["active_index"].is_null() && window.results.empty() && window.workflow.empty(),
        "Failure/empty startup published owners, activation or tables");
}
void restored(Window &window, bool changed=false) {
  check(window.documents().size()==2 && window.activeIndex()==0,"Workspace order/first activation differs");
  Json samples=Json::array(), ids=Json::array(), revision_rows=Json::array();
  for (std::size_t index=0;index<2;++index) {
    const auto snapshot=window.documents()[index]->nativeSnapshot();
    const auto &data=snapshot.data();
    const auto shape=data.dataset.shape();
    check(shape.dimensions()==(index?DatasetShape{2,3,2,6,8,1}:DatasetShape{1,1,2,6,8,1}).dimensions(),"Shape mismatch");
    check(data.dataset.pixel_type()==(index?PixelType::UInt16:PixelType::UInt8),"PixelType mismatch");
    Json row=Json::array();
    for(int t=0;t<shape.t;++t)for(int c=0;c<shape.c;++c)for(int z=0;z<2;++z)
      for(int y=0;y<6;++y)for(int x=0;x<8;++x) {
        const auto value=data.dataset.plane({t,c,z})->at(x,y);
        const auto expected=changed&&index&&t==1&&c==2&&z==1&&y==2&&x==3?113:
                            index?40*t+30*c+5*z+2*y+x:7*z+3*y+x;
        check(value==expected,"Independent TCZYXS literal differs"); row.push_back(value);
      }
    const auto revisions=snapshot.revisions();
    check(!snapshot.dirty() && revisions.pixels==0 && revisions.metadata==0 && revisions.selection==0 && revisions.display==0 &&
          window.nativeDocument(snapshot.id())->undo_steps()==0 && window.nativeDocument(snapshot.id())->redo_steps()==0,
          "Restored owner contains dirty/revision/history state");
    check(window.nativeDocument(snapshot.id())->history().step_limit==3 &&
          window.nativeDocument(snapshot.id())->history().pixel_budget==1048576,"Current Window history policy not applied");
    check(data.dataset.calibration().spacing==std::array<double,3>{1.25,.5,2.5} && data.selection && data.previous_selection &&
          data.rois.size()==1 && data.rois[0].z==1 && data.overlays.size()==1 && data.metadata.extensions.count("vendor"),
          "Calibration/selection/positioned ROI/opaque state lost");
    check(snapshot.display().zoom==2.25 && !snapshot.display().linkedZoom &&
          snapshot.display().planeZoom==std::array<double,3>{1.5,2.5,3.5},"Session camera overridden by defaults");
    samples.push_back(std::move(row));ids.push_back(snapshot.id());
    revision_rows.push_back({revisions.pixels,revisions.metadata,revisions.selection,revisions.display});
  }
  const auto scientific=window.documents()[1]->nativeSnapshot();
  check(scientific.display().t==1 && scientific.display().c==2 && scientific.display().crosshair[2]==1,"Scientific TCZ lost");
  check(window.results==Json::array({{{"Value",17}}}) && window.workflow.size()==1 &&
        window.workflow[0]["operation"]=="must_never_execute","Session tables executed or replaced");
  observations["samples"]=std::move(samples);observations["document_ids"]=std::move(ids);
  observations["revisions"]=std::move(revision_rows);observations["state"]=state(window);
}
void SU01(const fs::path &work) {
  const auto prefs=preferences(work);
  Window window(prefs.u8string()); empty(window);
  check(state(window)["status"]=="fresh" && state(window)["attempted"]==false,"Bare Window constructor restored documents");
  window.startStartup(request(work)); empty(window);
  check(state(window)["source"]=="empty" && state(window)["completed"]==true,"Defaults/Recent selected startup documents");
  SessionCapture zero;zero.extensions=with_workspace_presentation({}, {}, 0).extensions;
  save_session((work/"empty.ijx").u8string(),zero,limits());
  Window explicit_empty(prefs.u8string());auto input=request(work);input.explicit_workspace="empty.ijx";
  explicit_empty.startStartup(input);empty(explicit_empty);
  check(state(explicit_empty)["active_index"].is_null(),"Zero-document active owner must be null");
  observations["default_state"]=state(window);observations["empty_workspace_state"]=state(explicit_empty);
  const auto saved_empty=work/"window-saved-empty.ijx";
  window.action("save_workspace",{{"path",saved_empty.u8string()}});
  const auto decoded_empty=load_session(saved_empty.u8string(),limits());
  const auto saved_empty_presentation=workspace_presentation(decoded_empty.extensions,decoded_empty.documents.size());
  check(decoded_empty.source_version==3 && decoded_empty.documents.empty() && decoded_empty.results.empty() &&
        decoded_empty.workflow.empty() && saved_empty_presentation.supported && !saved_empty_presentation.active_index,
        "Window Save workspace did not emit a supported empty workspace with null active ordinal");
  observations["window_saved_empty"]={{"path",saved_empty.u8string()},{"documents",decoded_empty.documents.size()},
                                      {"active_index",saved_empty_presentation.active_index ? Json(*saved_empty_presentation.active_index) : Json()},
                                      {"supported",saved_empty_presentation.supported}};
}
void SU02(const fs::path &work) {
  fs::create_directory(work/"selected");save_session((work/"selected"/"relative.ijx").u8string(),capture(),limits());
  const auto prefs=preferences(work/"selected","relative.ijx");
  Window window(prefs.u8string());window.startStartup(request(work));restored(window);
  check(state(window)["paths"]==Json::array({(work/"selected"/"relative.ijx").u8string()}) &&
        state(window)["preferences_write_path"]==prefs.u8string(),"Persisted relative path ignored selected write parent");
  const auto old=read(prefs);
  Window explicit_window(prefs.u8string());auto input=request(work);input.explicit_workspace="selected/relative.ijx";
  explicit_window.startStartup(input);restored(explicit_window);
  check(state(explicit_window)["source"]=="explicit_workspace" && read(prefs)==old,"Explicit path routing mutated selected preferences");
  observations["selected_path"]=prefs.u8string();observations["explicit_state"]=state(explicit_window);
}
void SU03(const fs::path &work) {
  const auto source=capture();save_session((work/"workspace.ijx").u8string(),source,limits());
  const auto prefs=preferences(work,"workspace.ijx");const auto original=read(prefs);
  Window window(prefs.u8string());window.startStartup(request(work));restored(window);
  for(std::size_t i=0;i<2;++i)check(window.documents()[i]->nativeSnapshot().id()!=source.documents[i].id(),"Historical runtime ID was rebound");
  check(read(prefs)==original && window.settings().strings("shortcut_bindings")==std::vector<std::string>{"analysis.measure=Ctrl+K"},
        "Restore wrote preferences or changed global shortcut configuration");
  const auto before_save_state=state(window);
  const auto original_workspace_bytes=read(work/"workspace.ijx");
  const auto saved_path=work/"window-saved-first-active.ijx";
  window.setActiveIndex(0);
  window.action("save_workspace",{{"path",saved_path.u8string()}});
  const auto saved_bytes=read(saved_path);
  {
    const auto saved=load_session(saved_path.u8string(),limits());
    const auto presentation=workspace_presentation(saved.extensions,saved.documents.size());
    check(saved.source_version==3 && saved.documents.size()==2 && presentation.supported && presentation.active_index==0 &&
          saved.documents[0]->snapshot().data().metadata.title=="SU U8 stack" &&
          saved.documents[1]->snapshot().data().metadata.title=="SU U16 scientific" &&
          metadata_equal(saved.results,source.results) && metadata_equal(saved.workflow,source.workflow),
          "Window Save workspace lost literal order/first active ordinal or inert session tables");
    observations["window_saved_workspace"]={{"path",saved_path.u8string()},{"active_index",*presentation.active_index},
                                            {"document_count",saved.documents.size()},{"source_version",saved.source_version}};
  }
  Window reopened(prefs.u8string());auto input=request(work);input.explicit_workspace=saved_path.u8string();
  reopened.startStartup(input);restored(reopened);
  for(std::size_t i=0;i<2;++i)
    check(reopened.documents()[i]->nativeSnapshot().id()!=window.documents()[i]->nativeSnapshot().id(),
          "Window-saved workspace reused a live runtime document ID");
  check(read(saved_path)==saved_bytes && read(work/"workspace.ijx")==original_workspace_bytes && read(prefs)==original,
        "Window-saved workspace reopen changed saved/source/preferences bytes");
  observations["before_window_save_state"]=before_save_state;
  observations["window_saved_reopen_state"]=state(reopened);
}
void SU04(const fs::path &work) {
  write(work/"first.pgm","P2\n2 2\n255\n1 2 3 4\n");write(work/"last.pgm","P2\n2 2\n255\n5 6 7 8\n");
  save_session((work/"middle.ijx").u8string(),capture(),limits());const auto prefs=preferences(work,"missing-saved.ijx");
  Window window(prefs.u8string());auto input=request(work);input.explicit_paths={"first.pgm","middle.ijx","last.pgm"};
  window.startStartup(input);
  check(window.documents().size()==4 && window.activeIndex()==3 && window.results==Json::array({{{"Value",17}}}),
        "Explicit flattened order/final owner/last-session tables differ");
  check(window.documents()[0]->nativeSnapshot().data().dataset.plane()->at(0,0)==1 &&
        window.documents()[3]->nativeSnapshot().data().dataset.plane()->at(0,0)==5,"Explicit argument order differs");
  Window duplicate(prefs.u8string());input.explicit_paths={"first.pgm","./first.pgm"};refusal([&]{duplicate.startStartup(input);});empty(duplicate);
  Window conflict(prefs.u8string());input.explicit_paths={"first.pgm"};input.explicit_workspace="middle.ijx";
  refusal([&]{conflict.startStartup(input);});empty(conflict);
  observations["state"]=state(window);observations["table_rows"]=window.results;
}
void SU05(const fs::path &work) {
  save_session((work/"good.ijx").u8string(),capture(),limits());const auto prefs=preferences(work,"good.ijx");
  const auto source=read(work/"good.ijx"),config=read(prefs);
  Window window(prefs.u8string());const auto before=window.preferencesJson();
  auto input=request(work);input.explicit_paths={"good.ijx","missing.ijx","good.ijx"};
  // Distinct normalized good paths avoid the duplicate-path planner refusal.
  write(work/"good-again.ijx",source);input.explicit_paths[2]="good-again.ijx";
  refusal([&]{window.startStartup(input);});empty(window);
  check(window.preferencesJson()==before && read(prefs)==config && read(work/"good.ijx")==source,
        "Explicit failure changed Recent/config/source bytes");
  auto unsupported=capture();auto display=unsupported.documents[1].display();
  display.properties["channel_display"]=metadata_object({{"version",std::int64_t(1)}});
  unsupported.documents[1]=unsupported.documents[1].with_display(display);
  save_session((work/"late-facade.ijx").u8string(),unsupported,limits());
  Window late(prefs.u8string());input=request(work);input.explicit_workspace="late-facade.ijx";
  refusal([&]{late.startStartup(input);});empty(late);
  check(late.preferencesJson()==before && read(prefs)==config,"Late facade refusal published Recent/settings");
  observations["failed_state"]=state(window);observations["late_facade_state"]=state(late);
  observations["preferences_before"]=before;observations["preferences_after"]=window.preferencesJson();
}
void SU06(const fs::path &work) {
  // Accepted Text can still fail path planning. Automatic configured intent
  // must retain a usable empty Window instead of escaping the constructor.
  const auto overlong_prefs=preferences(work,std::string(4096,'x'),"planning-limit-preferences.json");
  Window planning_limit(overlong_prefs.u8string());planning_limit.startStartup(request(work));empty(planning_limit);
  check(state(planning_limit)["status"]=="failed" &&
        state(planning_limit)["diagnostics"].back()["reason"]=="path_limit_exceeded",
        "Automatic resolved-path planning refusal escaped usable empty startup");
  observations["planning_limit_state"]=state(planning_limit);
#ifdef _WIN32
  for(const auto &invalid_path:std::vector<std::string>{"\\relative.ijx","C:relative.ijx"}) {
    const auto invalid_prefs=preferences(work,invalid_path,"planning-root-preferences.json");
    Window invalid(invalid_prefs.u8string());invalid.startStartup(request(work));empty(invalid);
    check(state(invalid)["status"]=="failed" && state(invalid)["diagnostics"].back()["stage"]=="planning" &&
          state(invalid)["diagnostics"].back()["reason"]=="invalid_path",
          "Automatic rooted/drive-relative configured path escaped failed empty startup");
    observations["rooted_planning_states"].push_back(state(invalid));
  }
#endif
  const auto prefs=preferences(work,"mutable.ijx");
  const auto config=read(prefs);
  Window window(prefs.u8string());window.startStartup(request(work));empty(window);
  check(state(window)["status"]=="failed" && state(window)["retry_allowed"]==true &&
        !state(window)["diagnostics"].empty(),"Automatic refusal lacks usable empty diagnostic/retry");
  observations["missing_state"]=state(window);
  write(work/"mutable.ijx","IMAGEJXX3\ntruncated");window.retryStartup();empty(window);
  observations["truncated_state"]=state(window);
  for(int kind=0;kind<2;++kind) {
    auto invalid=capture();
    if(kind==0) {
      auto display=invalid.documents[1].display();
      display.properties["channel_display"]=metadata_object({{"version",std::int64_t(1)}});
      invalid.documents[1]=invalid.documents[1].with_display(display);
    } else invalid.extensions["imagejxx_workspace"]=metadata_object({{"version",std::int64_t(1)},{"active_index",std::int64_t(9)}});
    save_session((work/"mutable.ijx").u8string(),invalid,limits());const auto refused_bytes=read(work/"mutable.ijx");
    Window refused_window(prefs.u8string());refused_window.startStartup(request(work));empty(refused_window);
    check(state(refused_window)["status"]=="failed" && read(work/"mutable.ijx")==refused_bytes && read(prefs)==config,
          "Automatic unsupported/malformed presentation did not refuse without writes");
    observations[kind==0?"unsupported_state":"malformed_presentation_state"]=state(refused_window);
  }
  write(work/"deleted-owned-original.pgm","P2\n1 1\n255\n9\n");
  save_session((work/"mutable.ijx").u8string(),capture(),limits());fs::remove(work/"deleted-owned-original.pgm");
  // Replace valid current named-file bytes before retry; no cached decode exists.
  save_session((work/"mutable.ijx").u8string(),capture(true),limits());const auto current=read(work/"mutable.ijx");
  window.retryStartup();restored(window,true);
  check(read(prefs)==config && read(work/"mutable.ijx")==current && state(window)["retry_allowed"]==false,
        "Retry changed source/config or retained retry permission after restore");
  refusal([&]{window.retryStartup();});
}
void SU07(const fs::path &work) {
  const auto prefs=preferences(work);Json active=Json::array();
  for(int kind=0;kind<4;++kind) {
    auto saved=capture();
    if(kind==0)saved.extensions.erase("imagejxx_workspace");
    if(kind==2)saved.extensions["imagejxx_workspace"]=metadata_object({{"version",std::int64_t(37)},{"opaque",true}});
    if(kind==3)saved.extensions["imagejxx_workspace"]=metadata_object({{"version",std::int64_t(1)},{"active_index",std::int64_t(9)}});
    const auto path=work/("compat-"+std::to_string(kind)+".ijx");save_session(path.u8string(),saved,limits());
    Window window(prefs.u8string());auto input=request(work);input.explicit_workspace=path.u8string();
    if(kind==3){
      bool refused=false;
      try{window.startStartup(input);}catch(const StartupError &error){
        refused=true;
        check(error.diagnostic().path==path.u8string() && error.diagnostic().reason==StartupReason::InvalidPresentation,
              "Malformed active metadata lost the selected workspace path/stage diagnostic");
        observations["malformed_failure_path"]=error.diagnostic().path;
      }
      check(refused,"Malformed active metadata must refuse whole explicit workspace");empty(window);active.push_back(nullptr);
    }
    else{window.startStartup(input);check(window.activeIndex()==(kind==1?0:1),"Missing/known/future activation differs");active.push_back(window.activeIndex());}
  }
  observations["compatibility_active_indices"]=active;
}
void SU08(const fs::path &work) {
  const auto prefs=preferences(work);
  const auto config=read(prefs);Window window(prefs.u8string());
  const auto before=window.preferencesJson();
  refusal([&]{window.updateSettings({{"startup_mode","workspace"},{"startup_workspace",""},
                                     {"shortcut_bindings",Json::array({"analysis.measure=Ctrl+L"})}},true);});
  const auto after_invalid=window.preferencesJson();
  check(after_invalid==before && read(prefs)==config,"Invalid compound update changed live/file state");
  window.updateSettings({{"startup_mode","workspace"},{"startup_workspace","next.ijx"}},false);
  check(read(prefs)==config && window.settings().text("startup_workspace")=="next.ijx","persist=False not Window-local");
  write(work/"blocked","file-parent");Window blocked((work/"blocked"/"preferences.json").u8string());
  const auto blocked_before=blocked.preferencesJson();
  refusal([&]{blocked.updateSettings({{"startup_mode","workspace"},{"startup_workspace","good.ijx"}},true);});
  check(blocked.preferencesJson()==blocked_before && read(work/"blocked")=="file-parent","Failed destination changed live/source state");
  observations["before"]=before;observations["after_invalid"]=after_invalid;observations["local_applied"]=window.preferencesJson();
  observations["blocked_before"]=blocked_before;observations["blocked_after"]=blocked.preferencesJson();
}
void SU09(const fs::path &work) {
  const auto path=work/"malformed.json";write(path,"{broken-original");const auto original=read(path);
  Window window(path.u8string());window.startStartup(request(work));empty(window);
  check(window.settings().text("startup_mode")=="empty" && read(path)==original,"Recovery did not select safe empty/preserve original");
  auto recovery=PreferencesDocument::load(path.u8string());check(recovery.write_blocked && !recovery.save(path.u8string()),"Recovery automatic write was allowed");
  check(recovery.save(path.u8string(),true) && read(fs::path(path.u8string()+".bak"))==original,"Explicit recovery backup differs");
  const auto prefs=preferences(work,"ignored.ijx","reset-preferences.json");Window live(prefs.u8string());
  live.addNativeDocument(std::make_shared<imagejxx::Document>(capture().documents.front()));const auto id=live.nativeSnapshot().id();
  const auto before=live.preferencesJson();
  live.updateSettings({{"startup_mode","empty"},{"startup_workspace",""}},false);
  check(live.nativeSnapshot().id()==id && live.preferencesJson()["extras"]==before["extras"] &&
        live.settings().strings("shortcut_bindings")==std::vector<std::string>{"analysis.measure=Ctrl+K"},"Startup reset changed owners/Recent/layout/shortcuts");
  observations["recovery_original"]=original;observations["recovery_backup"]=read(fs::path(path.u8string()+".bak"));
  observations["reset_before"]=before;observations["reset_after"]=live.preferencesJson();
}
void SU10(const fs::path &work) {
  const auto prefs=preferences(work);
  const auto original=read(prefs);Window window(prefs.u8string());
  window.resetLayout(false);check(read(prefs)==original,"One-shot layout reset wrote selected preferences");
  window.resetLayout(true);const auto after=PreferencesDocument::load(prefs.u8string(),false);
  check(!Json::parse(after.extras_json).contains("layout") && Json::parse(after.extras_json)["recent"]==Json::array({"missing-recent.pgm"}),
        "Persistent layout reset did not remove only layout");
  for(const auto &layout:std::vector<Json>{Json(17),Json(std::string(1024*1024+1,'x')),Json(std::string("A\0B",3))}) {
    Json root={{"format","imagejxx-preferences"},{"version",1},{"values",Json::object()},{"extras",{{"recent",Json::array()},{"layout",layout}}}};
    const auto invalid=PreferencesDocument::parse(root.dump());
    check(invalid.write_blocked && !Json::parse(invalid.extras_json).contains("layout"),"Invalid layout was not recovered/write blocked");
  }
  write(work/"blocked-layout-parent","owned blocking file");
  Window blocked((work/"blocked-layout-parent"/"preferences.json").u8string());
  const auto blocked_before=blocked.preferencesJson(),layout_before=state(blocked)["layout"];
  refusal([&]{blocked.resetLayout(true);});
  check(blocked.preferencesJson()==blocked_before && state(blocked)["layout"]==layout_before &&
        read(work/"blocked-layout-parent")=="owned blocking file","Failed persistent reset changed config/live layout generation");
  observations["before_json"]=Json::parse(original);observations["after_json"]=Json::parse(read(prefs));
  observations["failed_reset_before"]=layout_before;observations["failed_reset_after"]=state(blocked)["layout"];
  observations["scope"]="Host configuration/reset assertions only; actual visible ImGui load/reset requires paired UI evidence";
}
}
int main(int argc,char **argv) {
  try {
    check(argc==3,"Usage: imagejxx_startup_window_tests FRESH_DIRECTORY SU01..SU10");
    const auto work=fs::absolute(fs::u8path(argv[1])).lexically_normal();
    check(work.has_filename() && fs::is_directory(work.parent_path()) && !fs::exists(work),"Fresh exclusive owned directory required");
    for(auto parent=work.parent_path();!parent.empty();) {
      check(!fs::is_symlink(fs::symlink_status(parent)),"Symlink parent refused");
      const auto next=parent.parent_path();if(next==parent)break;parent=next;
    }
    check(fs::create_directory(work),"Cannot create owned host work directory");
    using Case=void(*)(const fs::path &);
    const std::map<std::string,Case> cases={{"SU01",SU01},{"SU02",SU02},{"SU03",SU03},{"SU04",SU04},{"SU05",SU05},
                                          {"SU06",SU06},{"SU07",SU07},{"SU08",SU08},{"SU09",SU09},{"SU10",SU10}};
    const auto found=cases.find(argv[2]);check(found!=cases.end(),"Unknown host startup case");found->second(work);
    Json output={{"schema_version",1},{"producer_id","p06j.startup.host.raw.001"},{"case_id",argv[2]},
                 {"complete",true},{"checks",checks},{"observations",observations}};
    const auto text=output.dump(2);check(text.size()<=16*1024*1024,"Host actual-value output exceeds16MiB");
    write(work/"startup-host.json",text+"\n");std::cout<<argv[2]<<" / "<<checks<<" checks\n";return 0;
  }catch(const std::exception &error){std::cerr<<error.what()<<'\n';return 1;}
}
