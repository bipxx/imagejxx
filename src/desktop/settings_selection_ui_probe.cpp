#include "settings_selection_ui_probe.hpp"
#include "gaussian_cancellation_internal.hpp"
#include "headless_session_internal.hpp"
#include "rank_runtime_probe.hpp"
#include "settings_io.hpp"
#include "window.hpp"
#include <imgui_internal.h>

#include <array>
#include <chrono>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <initializer_list>
#include <stdexcept>
#include <string>
#include <vector>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <tlhelp32.h>
#else
#include <unistd.h>
#endif

namespace imagejxx::desktop {
namespace {
namespace fs = std::filesystem;
using Clock = std::chrono::steady_clock;
constexpr std::size_t reportLimit=16*1024*1024, preferenceLimit=1024*1024, frameLimit=750;
void require(bool value,const std::string &message) {
  if(!value)throw std::runtime_error(message);
}
Json processIdentity() {
#ifdef _WIN32
  const auto pid=GetCurrentProcessId();DWORD parent=0;
  const auto snapshot=CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS,0);
  require(snapshot!=INVALID_HANDLE_VALUE,"Cannot observe settings selection process identity");
  PROCESSENTRY32 entry{};entry.dwSize=sizeof(entry);
  if(Process32First(snapshot,&entry))do {
    if(entry.th32ProcessID==pid){parent=entry.th32ParentProcessID;break;}
  }while(Process32Next(snapshot,&entry));
  CloseHandle(snapshot);require(parent!=0,"Settings selection parent identity unavailable");
  return {{"process_id",pid},{"parent_process_id",parent}};
#else
  return {{"process_id",getpid()},{"parent_process_id",getppid()}};
#endif
}
template<class Word>void appendWord(std::string &text,Word word) {
  constexpr char digits[]="0123456789abcdef";
  for(std::size_t i=0;i<sizeof(word);++i) {
    const auto byte=static_cast<unsigned char>(word>>(8*i));
    text+=digits[byte>>4];text+=digits[byte&15];
  }
}
Json fields(const Json &value,std::initializer_list<const char *> keys) {
  Json out=Json::object();for(const auto *key:keys)if(value.contains(key))out[key]=value.at(key);return out;
}
Json viewportState(const Json &value) {
  auto out=fields(value,{"source_document_id","pixels_source","shape","range","position",
                         "contrast_source","preview_generation","preview_active"});
  if(value.contains("readback"))out["readback"]=fields(value.at("readback"),{"available","source_tcz"});
  return out;
}
Json selectionObservation(const std::optional<Selection> &selection) {
  if(!selection)return Json();
  const auto mask=selection->mask(17,17);std::string dense,packed;
  require(mask.width==17 && mask.height==17 && mask.channels==1 && mask.pixels.size()==289,
          "Selection observation exceeds the fixed17x17 fixture");
  for(const auto value:mask.pixels) {
    require(value==0 || value==255,"Observed selection membership is noncanonical");
    appendWord(dense,static_cast<unsigned char>(value));
  }
  if(selection->is_mask())for(const auto byte:selection->packed_mask())appendWord(packed,byte);
  return {{"mask_shape",{17,17}},{"mask_u8_hex",std::move(dense)},
          {"packed_mask_hex",selection->is_mask()?Json(std::move(packed)):Json()}};
}
Json ownerState(Window &window,std::uint64_t id) {
  const auto source=window.nativeSnapshot(id);const auto &data=source.data();
  const auto shape=data.dataset.shape();
  require(shape.y==17 && shape.x==17 && shape.s==1 && shape.t<=2 && shape.c<=2 && shape.z<=2 &&
      data.dataset.pixel_type()==PixelType::UInt8 && data.dataset.sample_policy()==FloatingSamplePolicy::FiniteOnly,
      "Settings selection source differs from the finite UInt8 fixture");
  Json planes=Json::array();
  for(int t=0;t<shape.t;++t)for(int c=0;c<shape.c;++c)for(int z=0;z<shape.z;++z) {
    const auto image=data.dataset.plane({t,c,z});std::string declared,storage;
    require(image->width==17 && image->height==17 && image->channels==1 && image->pixels.size()==289,
            "Settings selection plane differs from its declared extent");
    for(const auto value:image->pixels) {
      require(std::isfinite(value) && value>=0 && value<=255 && std::floor(value)==value,
              "Settings selection source contains a noncanonical UInt8 sample");
      appendWord(declared,static_cast<unsigned char>(value));std::uint64_t word;
      static_assert(sizeof(word)==sizeof(value),"Source storage word size");
      std::memcpy(&word,&value,sizeof(word));appendWord(storage,word);
    }
    planes.push_back({{"position",{t,c,z}},{"shape",{17,17,1}},
                      {"declared_u8_hex",std::move(declared)},{"storage_f64_hex",std::move(storage)}});
  }
  const auto index=[](const std::optional<int> &n){return n?Json(*n):Json();};
  Json rois=Json::array(),overlays=Json::array();
  for(const auto &roi:data.rois)rois.push_back({{"name",roi.name},{"selection",selectionJson(roi.selection)},
      {"t",index(roi.t)},{"c",index(roi.c)},{"z",index(roi.z)},{"attributes",metadataJson(roi.attributes)}});
  for(const auto &overlay:data.overlays)overlays.push_back(metadataJson(overlay));
  const auto &view=source.display();const auto &cal=data.dataset.calibration();
  const auto r=source.revisions();const auto h=window.nativeDocument(id)->history();
  return {{"document_id",id},{"data_identity",source.data_identity()?Json(*source.data_identity()):Json()},
    {"scientific",window.documentById(id)->scientific()},{"shape",shape.dimensions()},
    {"dtype","uint8"},{"sample_policy","finite_only"},{"planes",std::move(planes)},
    {"selection",data.selection?selectionJson(*data.selection):Json()},
    {"previous",data.previous_selection?selectionJson(*data.previous_selection):Json()},
    {"selection_observation",selectionObservation(data.selection)},
    {"previous_observation",selectionObservation(data.previous_selection)},
    {"metadata",{{"title",data.metadata.title},{"properties",metadataJson(data.metadata.properties)},
      {"extensions",metadataJson(data.metadata.extensions)},{"record_layout",metadataJson(data.metadata.record_layout)}}},
    {"calibration",{{"spacing",cal.spacing},{"origin",cal.origin},{"direction",cal.direction},{"unit",cal.unit},
      {"frame_interval",cal.frame_interval},{"time_origin",cal.time_origin},{"time_unit",cal.time_unit}}},
    {"rois",std::move(rois)},{"overlays",std::move(overlays)},
    {"display",{{"t",view.t},{"c",view.c},{"crosshair",view.crosshair},{"orthogonal",view.orthogonal},
      {"linked_zoom",view.linkedZoom},{"zoom",view.zoom},{"plane_zoom",view.planeZoom},{"pan",view.pan},
      {"revision",view.revision},{"properties",metadataJson(view.properties)}}},
    {"revisions",{{"pixels",r.pixels},{"metadata",r.metadata},{"selection",r.selection},{"display",r.display}}},
    {"dirty",source.dirty()},{"undo",h.undo_steps},{"redo",h.redo_steps},
    {"history",{{"undo_steps",h.undo_steps},{"redo_steps",h.redo_steps},{"step_limit",h.step_limit},
      {"pixel_budget",h.pixel_budget},{"pixels",{{"logical_bytes",h.pixels.logical_bytes},
      {"current_bytes",h.pixels.current_bytes},{"retained_bytes",h.pixels.retained_bytes},
      {"obsolete_bytes",h.pixels.obsolete_bytes},{"retained_planes",h.pixels.retained_planes},{"exact",h.pixels.exact}}}}}};
}
Json usageState(const JobUsage &u) {
  return {{"running",u.running},{"queued",u.queued},{"running_bytes",u.running_bytes},
          {"retained_bytes",u.retained_bytes},{"reserved_bytes",u.reserved_bytes}};
}

class Driver {
  Window &window_;
  std::shared_ptr<JobScheduler> scheduler_;
  std::string caseId_;
  fs::path directory_,preferencesPath_;
  Json report_,last_;
  std::vector<std::uint64_t> owners_;
  HeadlessInputSession *session_=nullptr;
  Clock::time_point deadline_;
  std::size_t ordinal()const {require(!report_["frames"].empty(),"No CFG2 frame");return report_["frames"].size()-1;}
  void seed(bool scientific);
  void frame(const std::string &,const Json &events=Json::array());
  Json control(const std::string &)const;
  void click(const std::string &);
  void text(const std::string &,const std::string &);
  void preferences();
  void closePreferences();
  void pair(const char *,const char *);
  void menu(bool shrink=false);
  void restore();
  void activate(std::uint64_t);
  Json fileState()const;
  Json state()const;
  void remember(const std::string &);
  void milestone(const std::string &);
  void consume();
  void persist();
  void reopen();
  void write(const char *);
public:
  Driver(Window &window,std::string id,std::string context,fs::path directory,const Json &runtime)
    :window_(window),scheduler_(GaussianCancellationAccess::scheduler(window)),caseId_(std::move(id)),directory_(std::move(directory)) {
    report_={{"schema","cfg2.settings-selection.ui.001"},{"acceptance_claim",false},{"case_id",caseId_},
      {"context",std::move(context)},{"status","failed"},{"error",""},
      {"fixture",{{"ordinary_shape",{1,1,1,17,17,1}},{"scientific_shape",{2,2,2,17,17,1}},
        {"dtype","uint8"},{"sample_formula","(19*t+31*c+47*z+3*y+x)%256"},
        {"initial_rectangle",{{4,4},{12,11}}},{"spacing",{.5,1.5,2.}},
        {"runtime",runtime},{"imgui",IMGUI_VERSION},{"producer_source",__FILE__}}},
      {"setup_events",Json::array()},{"events",Json::array()},{"frames",Json::array()},
      {"states",Json::object()},{"milestones",Json::object()},
      {"bounds",{{"frames",frameLimit},{"seconds",60},{"report_bytes_exclusive",reportLimit},{"preference_bytes",preferenceLimit}}}};
  }
  Json run();
};

void Driver::seed(bool scientific) {
  const int n=scientific?2:1;Stack planes;
  for(int t=0;t<n;++t)for(int c=0;c<n;++c)for(int z=0;z<n;++z) {
    Image image(17,17,1,PixelType::UInt8);
    for(int y=0;y<17;++y)for(int x=0;x<17;++x)image.at(x,y)=(19*t+31*c+47*z+3*y+x)%256;
    planes.push_back(std::move(image));
  }
  SpatialCalibration cal;cal.spacing={.5,1.5,2};cal.unit="um";
  DocumentData data{Dataset::from_planes({n,n,n,17,17,1},std::move(planes),cal)};
  data.metadata.title=scientific?"CFG2 scientific":"CFG2 ordinary";
  data.metadata.properties["cfg2"]=metadataFromJson({{"label","retain properties"},{"value",17}});
  data.metadata.extensions["cfg2"]="retain extension";data.metadata.record_layout["cfg2"]="retain layout";
  data.selection=Selection("rectangle",{{4,4},{12,11}});
  data.previous_selection=Selection("line",{{0,16},{16,0}});
  data.rois={{"cfg2 retained ROI",Selection("rectangle",{{0,0},{1,1}}),{},{},{},{{"cfg2",std::string("retain ROI")}}}};
  data.overlays.push_back(metadataObjectFromJson({{"kind","point"},{"points",{{16.,0.}}},{"cfg2","retain overlay"}}));
  auto owner=std::make_shared<imagejxx::Document>(std::move(data));auto display=owner->snapshot().display();
  display.t=display.c=scientific?1:0;display.crosshair={8,8,scientific?1:0};display.zoom=12;
  display.properties["display_range"]=metadata_array({0.,255.});owner->set_display(std::move(display));
  window_.addNativeDocument(owner);owners_.push_back(owner->snapshot().id());window_.recording=false;
  report_["setup_events"].push_back({{"kind","fixture.source"},
    {"methods",{"DocumentData construction","Document.set_display","Window.addNativeDocument","recording=false"}},
    {"purpose","Labeled source preparation; no selection-authoring or preferences action credit"},
    {"settings_mutation",false},{"owner",ownerState(window_,owners_.back())}});
}
void Driver::frame(const std::string &label,const Json &events) {
  require(session_ && report_["frames"].size()<frameLimit && Clock::now()<deadline_,
          "CFG2 observation exceeded750 actual frames or60 seconds");
  last_=session_->frame(events);
  require(last_.at("input_frame").at("raw_inputs")==events,"CFG2 actual input differs from its request");
  Json rendered=fields(last_,{"input_frame","session","preferences_panel","dialog","document_tabs","window_error","error_modal"});
  Json host=Json::object();for(auto it=last_.at("host_controls").begin();it!=last_.at("host_controls").end();++it)
    if(it.key().rfind("menu.Edit",0)==0 || it.key()=="menu.form.grow_selection" ||
       it.key()=="menu.form.shrink_selection" || it.key()=="menu.action.restore_selection")host[it.key()]=it.value();
  rendered["host_controls"]=std::move(host);
  const auto &context=*ImGui::GetCurrentContext();
  rendered["actual_hover"]={{"id",context.HoveredId},{"window",context.HoveredWindow?context.HoveredWindow->Name:""}};
  report_["frames"].push_back({{"ordinal",report_["frames"].size()},{"label",label},{"events",events},{"rendered",std::move(rendered)}});
  require(window_.error().empty(),"CFG2 actual UI error: "+window_.error());
}
Json Driver::control(const std::string &name)const {
  Json item;ImGuiWindow *owner=nullptr;
  if(name.rfind("document.",0)==0) {
    const auto id=std::stoull(name.substr(9));
    for(const auto &tab:last_.at("document_tabs"))if(tab.at("document_id")==id){item=tab;owner=ImGui::FindWindowByName("Images");break;}
    require(!item.is_null(),"CFG2 document tab is missing");
  }else if(name.rfind("host.",0)==0)item=last_.at("host_controls").at(name.substr(5));
  else if(name.rfind("dialog.",0)==0) {
    item=last_.at("dialog").at("controls").at(name.substr(7));
  }else {
    const auto &panel=last_.at("preferences_panel");
    require(panel.at("open").get<bool>(),"CFG2 Preferences control is closed: "+name);
    auto *preferences=ImGui::FindWindowByName("Preferences");
    require(preferences && preferences->Active && !preferences->Hidden,"CFG2 Preferences owning window unavailable");
    if(name.rfind("field.",0)==0 || name=="units.combo") {
      item=name=="units.combo"?panel.at("fields").at("selection_enlargement_units").at("combo"):
          panel.at("fields").at(name.substr(6));
      for(auto *candidate:ImGui::GetCurrentContext()->Windows)
        if(candidate->ParentWindow==preferences && candidate->ChildId==preferences->GetID("settings") && candidate->Active && !candidate->Hidden){owner=candidate;break;}
      require(owner,"CFG2 Preferences settings child unavailable");
    }else if(name.rfind("units.choice.",0)==0 || name.rfind("group.",0)==0) {
      item=name.rfind("units.choice.",0)==0?
          panel.at("fields").at("selection_enlargement_units").at("choices").at(name.substr(13)):
          panel.at("widget_controls").at("group_choices").at(name.substr(6));
      const auto &popups=ImGui::GetCurrentContext()->OpenPopupStack;
      for(int i=popups.Size-1;i>=0;--i)if(popups[i].Window && popups[i].Window->Active && !popups[i].Window->Hidden){owner=popups[i].Window;break;}
      require(owner,"CFG2 choice popup unavailable");
    }else {
      require(name.rfind("preferences.",0)==0,"Unknown CFG2 control: "+name);
      item=panel.at("widget_controls").at(name.substr(12));owner=preferences;
    }
  }
  if(!owner && item.contains("window_name"))owner=ImGui::FindWindowByName(item.at("window_name").get<std::string>().c_str());
  if(owner) {
    require(owner->Active && !owner->Hidden,"CFG2 owning control window hidden: "+name);item["window_name"]=owner->Name;
    if(!item.contains("clip_rect"))item["clip_rect"]={owner->InnerClipRect.Min.x,owner->InnerClipRect.Min.y,owner->InnerClipRect.Max.x,owner->InnerClipRect.Max.y};
  }
  return item;
}
void Driver::click(const std::string &name) {
  const auto before=ordinal();Json item;
  for(int attempt=0;attempt<30;++attempt) {
    item=control(name);
    if(!item.value("enabled",true) || item.at("id").get<ImGuiID>()==0) {
      report_["failure_control"]={{"control",name},{"frame",ordinal()},{"actual_control",item}};
      throw std::runtime_error("Unavailable CFG2 control: "+name);
    }
    const auto p=item.at("point").get<std::array<float,2>>();const auto r=item.at("rect").get<std::array<float,4>>();
    const float x=std::floor(p[0]),y=std::floor(p[1]);
    require(x>=r[0] && x<r[2] && y>=r[1] && y<r[3],"CFG2 control point outside actual item: "+name);
    if(!item.contains("clip_rect"))break;
    const auto clip=item.at("clip_rect").get<std::array<float,4>>();
    require(x>=clip[0] && x<clip[2],"CFG2 control is horizontally clipped: "+name);
    if(y>=clip[1] && y<clip[3] && r[1]>=clip[1] && r[3]<=clip[3] && item.value("visible",true))break;
    require(attempt<29 && item.contains("window_name"),"Cannot expose CFG2 control: "+name);
    frame(name+".scroll.position",Json::array({{{"type","position"},{"point",{clip[0]+2,(clip[1]+clip[3])*.5f}}}}));
    require(ImGui::GetCurrentContext()->HoveredWindow &&
        ImGui::GetCurrentContext()->HoveredWindow->Name==item.at("window_name").get<std::string>(),"CFG2 scroll does not own its window");
    frame(name+".scroll",Json::array({{{"type","wheel"},{"x",0},{"y",r[3]>clip[3]?-3:3}}}));
  }
  for(int attempt=0;attempt<4;++attempt) {
    frame(name+".position",Json::array({{{"type","position"},{"point",item.at("point")}}}));
    auto current=control(name);
    if(current.at("id")==item.at("id") && current.at("point")==item.at("point") && current.at("rect")==item.at("rect")){item=std::move(current);break;}
    require(attempt<3,"CFG2 actual control did not stabilize: "+name);item=std::move(current);
  }
  const auto &context=*ImGui::GetCurrentContext();
  require(context.HoveredId==item.at("id").get<ImGuiID>() && context.HoveredWindow,
          "CFG2 control is not the actual hovered item: "+name);
  const auto clip=context.HoveredWindow->ClipRect;
  require(clip.Contains(context.IO.MousePos),"CFG2 actual mouse is outside owning clip: "+name);
  const Json mouse{context.IO.MousePos.x,context.IO.MousePos.y};const auto hovered=context.HoveredId;
  const auto window=std::string(context.HoveredWindow->Name);
  frame(name+".press",Json::array({{{"type","button"},{"name","left"},{"down",true}}}));
  frame(name+".release",Json::array({{{"type","button"},{"name","left"},{"down",false}}}));frame(name+".idle");
  report_["events"].push_back({{"kind","control_click"},{"control",name},{"frame_before",before},{"frame_after",ordinal()},
    {"actual_control",std::move(item)},{"actual_mouse",mouse},{"hovered_id",hovered},{"hovered_window",window},
    {"actual_clip",{clip.Min.x,clip.Min.y,clip.Max.x,clip.Max.y}}});
}
void Driver::text(const std::string &name,const std::string &value) {
  const auto before=ordinal();click(name);const auto id=control(name).at("id");
  require(last_.at("input_frame").at("focus").at("active_id")==id &&
      last_.at("input_frame").at("input_text").at("id")==id,"CFG2 input did not acquire keyboard ownership: "+name);
  for(bool down:{true,false})frame(name+".select_all",Json::array({{{"type","keys"},{"keys",{
    {{"name","Ctrl"},{"down",down}},{{"name","A"},{"down",down}}}}}}));
  frame(name+".text",Json::array({{{"type","text"},{"value",value}}}));
  for(bool down:{true,false})frame(name+".enter",Json::array({{{"type","key"},{"name","Enter"},{"down",down}}}));
  frame(name+".text.idle");
  report_["events"].push_back({{"kind","text_input"},{"control",name},{"text",value},{"input_id",id},
    {"frame_before",before},{"frame_after",ordinal()}});
}
void Driver::preferences() {
  click("host.menu.Edit");click("host.menu.Edit.Preferences");
  require(last_.at("preferences_panel").at("open").get<bool>(),"Actual Preferences did not open");
  text("preferences.search","enlargement");click("preferences.group");click("group.Selection");
  const auto &panel=last_.at("preferences_panel");
  require(panel.at("search")=="enlargement" && panel.at("group")=="Selection" &&
      panel.at("fields").contains("selection_enlargement_distance") && panel.at("fields").contains("selection_enlargement_units"),
      "Preferences search/group did not expose both enlargement defaults");
}
void Driver::closePreferences() {
  click("preferences.close_button");require(!last_.at("preferences_panel").at("open").get<bool>(),"Preferences Close did not dismiss its draft");
}
void Driver::pair(const char *distance,const char *units) {
  text("field.selection_enlargement_distance",distance);click("units.combo");click(std::string("units.choice.")+units);
  const auto &f=last_.at("preferences_panel").at("fields");
  require(f.at("selection_enlargement_distance").at("draft").get<double>()==std::stod(distance) &&
      f.at("selection_enlargement_units").at("draft")==units,"Actual Preferences did not retain requested enlargement pair");
}
void Driver::menu(bool shrink) {
  click("host.menu.Edit");click("host.menu.Edit.Selection");
  click(shrink?"host.menu.form.shrink_selection":"host.menu.form.grow_selection");
  require(last_.at("dialog").at("open").get<bool>() && last_.at("dialog").at("title")=="Enlarge selection",
          "Actual enlargement menu did not open its dialog");
}
void Driver::restore() {click("host.menu.Edit");click("host.menu.Edit.Selection");click("host.menu.action.restore_selection");}
void Driver::activate(std::uint64_t id) {
  click("document."+std::to_string(id));
  for(int attempt=0;attempt<12;++attempt) {
    frame("document.render");const auto &p=last_.at("ordinary_pixels");
    if(window_.nativeSnapshot().id()==id && p.value("source_document_id",std::uint64_t{})==id &&
        p.contains("readback") && p.at("readback").value("available",false))return;
  }
  throw std::runtime_error("Actual document tab did not produce current-owner render readback");
}
Json Driver::fileState()const {
  require(!preferencesPath_.empty(),"Preferences path has not been observed");
  Json out{{"path",preferencesPath_.u8string()},{"exists",false},{"byte_count",0},{"utf8",Json()},{"sha256",Json()}};
  if(!fs::exists(preferencesPath_))return out;
  require(fs::is_regular_file(preferencesPath_) && !fs::is_symlink(fs::symlink_status(preferencesPath_)),"Preferences evidence must be an owned regular file");
  const auto raw=readBytes(preferencesPath_.u8string(),preferenceLimit);(void)Json::parse(raw.begin(),raw.end());
  out["exists"]=true;out["byte_count"]=raw.size();out["utf8"]=std::string(raw.begin(),raw.end());out["sha256"]=rankRuntimeSha256(raw);return out;
}
Json Driver::state()const {
  Json owners=Json::array();for(auto id:owners_)owners.push_back(ownerState(window_,id));
  return {{"frame",ordinal()},{"active_id",window_.nativeSnapshot().id()},{"settings",settingsJson(window_.settings())},
    {"preferences",window_.preferencesJson()},{"preferences_file",fileState()},{"owners",std::move(owners)},
    {"workflow",window_.workflow},{"results",window_.results},{"recording",window_.recording},
    {"usage",usageState(scheduler_->usage())},{"error",window_.error()},
    {"observations",{{"preferences_panel",last_.at("preferences_panel")},{"dialog",last_.at("dialog")},
      {"ordinary_pixels",viewportState(last_.at("ordinary_pixels"))}}}};
}
void Driver::remember(const std::string &name) {
  require(!report_["states"].contains(name),"Duplicate CFG2 checkpoint");frame("checkpoint."+name);report_["states"][name]=state();
}
void Driver::milestone(const std::string &name) {
  require(!report_["milestones"].contains(name),"Duplicate CFG2 milestone");frame("milestone."+name);
  auto value=state();for(auto &owner:value["owners"]){owner.erase("planes");owner.erase("selection_observation");owner.erase("previous_observation");}
  report_["milestones"][name]=std::move(value);
}
void Driver::consume() {
  remember("before");preferences();
  if(caseId_=="enlargement-consume-ordinary") {
    pair("-1","pixels");click("preferences.apply");
    require(!last_.at("preferences_panel").at("error").get<std::string>().empty(),"Negative enlargement preference did not expose its refusal");
    milestone("negative_refused");text("field.selection_enlargement_distance","2");milestone("negative_corrected");
  }else pair("2","pixels");
  click("preferences.apply");closePreferences();remember("pixels_applied");
  menu();remember("expand_opened");click("dialog.units");click("dialog.units.choice.calibrated");remember("units_converted");
  click("dialog.cancel");remember("cancelled");menu();click("dialog.apply");remember("expanded");restore();remember("restored");
  preferences();pair("3","calibrated");click("preferences.apply");closePreferences();remember("calibrated_applied");
  menu(true);remember("shrink_opened");click("dialog.apply");remember("shrunk");restore();remember("restored_again");
  menu();remember("override_opened");text("dialog.distance","1");click("dialog.apply");remember("override_applied");
  menu();remember("default_reopened");click("dialog.cancel");remember("final_cancel");
}
void Driver::persist() {
  remember("before");preferences();pair("3","calibrated");remember("draft");
  click("preferences.apply");remember("saved");closePreferences();remember("closed");
}
void Driver::reopen() {
  activate(owners_.at(0));remember("loaded");
  for(std::size_t i=0;i<owners_.size();++i) {
    const std::string prefix=i?"scientific":"ordinary";if(i)activate(owners_[i]);
    menu();remember(prefix+"_expand_opened");click("dialog.apply");remember(prefix+"_expanded");
    restore();remember(prefix+"_restored");menu(true);remember(prefix+"_shrink_opened");
    click("dialog.apply");remember(prefix+"_shrunk");
  }
  preferences();remember("shown");closePreferences();remember("final_cancel");
}
void Driver::write(const char *name) {
  const auto bytes=report_.dump();require(bytes.size()<reportLimit,"CFG2 report exceeds its16MiB encoded byte bound");
  const auto path=directory_/name;writeBytes(path.u8string(),std::vector<unsigned char>(bytes.begin(),bytes.end()));
  require(fs::file_size(path)==bytes.size(),"CFG2 written byte count differs");
}
Json Driver::run() {
  deadline_=Clock::now()+std::chrono::seconds(60);
  try {
    seed(caseId_=="enlargement-consume-scientific");if(caseId_=="enlargement-reopen")seed(true);
    HeadlessInputSession session(window_);session_=&session;
    frame("initial.0");frame("initial.1");frame("initial.2");
    preferencesPath_=fs::absolute(fs::u8path(last_.at("preferences_path").get<std::string>())).lexically_normal();
    require(preferencesPath_.has_filename() && preferencesPath_.parent_path()==directory_.parent_path(),"CFG2 Preferences must share the explicitly owned capture parent");
    const bool paired=caseId_=="enlargement-persist" || caseId_=="enlargement-reopen";
    const bool reopened=caseId_=="enlargement-reopen";
    require(fs::exists(preferencesPath_)==reopened,reopened?"CFG2 reopen needs the actual UI-saved preferences file":"CFG2 initial route requires an absent preferences file");
    report_["setup_events"].push_back({{"kind","fixture.preferences_input"},
      {"method","Window constructor loaded explicit preferences path; no API mutation"},{"paired",paired},
      {"preferences_file",fileState()},{"settings",settingsJson(window_.settings())},{"preferences",window_.preferencesJson()}});
    if(caseId_=="enlargement-persist")persist();else if(caseId_=="enlargement-reopen")reopen();else consume();
    const std::size_t expected=caseId_=="enlargement-persist"?4:caseId_=="enlargement-reopen"?13:15;
    require(report_["states"].size()==expected,"CFG2 recipe omitted a required full checkpoint");
    require(report_["milestones"].size()==(caseId_=="enlargement-consume-ordinary"?2u:0u),"CFG2 recipe omitted a required scalar checkpoint");
    session.close();report_["setup_events"].push_back({{"kind","session.closed"},{"lifecycle",session.lifecycle()}});
    session_=nullptr;report_["status"]="observed";write("case.json");return report_;
  }catch(const std::exception &error) {
    session_=nullptr;report_["status"]="failed";report_["error"]=error.what();
    try{if(!last_.is_null() && !preferencesPath_.empty())report_["milestones"]["failure"]=state();}
    catch(const std::exception &observationError){report_["milestones"]["failure_observation_error"]=observationError.what();}
    write("failure.json");throw;
  }
}
} // namespace
Json settingsSelectionUiCase(Window &window,const std::string &id,const std::string &context,const std::string &fresh) {
  window.checkThread();require(context=="imgui_cpp" || context=="imgui_python","Unknown CFG2 launch context");
  require(id=="enlargement-consume-ordinary" || id=="enlargement-consume-scientific" || id=="enlargement-persist" || id=="enlargement-reopen","Unknown CFG2 recipe");
  require(!ImGui::GetCurrentContext() && !window.busy() && window.documents().empty() && window.results.empty() && window.workflow.empty(),"CFG2 needs a fresh idle empty Window");
  const auto directory=fs::absolute(fs::u8path(fresh)).lexically_normal();
  require(!fresh.empty() && directory.has_filename() && directory!=directory.root_path() && !fs::exists(directory),"CFG2 capture directory must be fresh, absent and nonroot");
  fs::create_directories(directory.parent_path());require(fs::create_directory(directory),"Cannot exclusively create CFG2 capture directory");
  const auto runtime=processIdentity();const auto bytes=runtime.dump();
  writeBytes((directory/"runtime.json").u8string(),std::vector<unsigned char>(bytes.begin(),bytes.end()));
  return Driver(window,id,context,directory,runtime).run();
}
} // namespace imagejxx::desktop
