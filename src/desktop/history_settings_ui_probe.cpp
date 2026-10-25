#if defined(__linux__) && !defined(_GNU_SOURCE)
#define _GNU_SOURCE
#endif
#include "history_settings_ui_probe.hpp"
#include "actual_observation_transport.hpp"
#include "headless_session_internal.hpp"
#include "rank_runtime_probe.hpp"
#include "settings_io.hpp"
#include "window.hpp"
#include <imagejxx/session.hpp>
#include <imgui.h>
#include <imgui_internal.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>
#include <map>
#include <memory>
#include <set>
#include <stdexcept>
#include <utility>
#include <vector>
#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#elif defined(__linux__) || defined(__APPLE__)
#include <cstdint>
#include <dlfcn.h>
#include <unistd.h>
#if defined(__APPLE__)
#include <mach-o/dyld.h>
#endif
#endif

namespace imagejxx::desktop {
namespace {
namespace fs = std::filesystem;
using Clock = std::chrono::steady_clock;
using namespace std::chrono_literals;
constexpr std::size_t mib = 1048576;
constexpr std::size_t maxReport = 16 * mib, maxArchive = 32 * mib;
const std::array<const char *,7> frameFields{"session","input_frame","preferences_panel",
    "preferences_path","window_error","error_modal","host_controls"};
void require(bool value, const char *message) {
  if (!value) throw std::runtime_error(message);
}
const char *primary() {
#if defined(__APPLE__)
  return "Super";
#else
  return "Ctrl";
#endif
}
Json keyEvents(const std::vector<std::string> &names, bool down) {
  Json atoms = Json::array();
  for (const auto &name : names) atoms.push_back({{"name",name},{"down",down}});
  return Json::array({Json{{"type","keys"},{"keys",std::move(atoms)}}});
}
Json position(const Json &point) { return {{"type","position"},{"point",point}}; }
Json button(bool down) { return {{"type","button"},{"name","left"},{"down",down}}; }
std::string hex(const std::vector<unsigned char> &bytes) {
  constexpr char digits[] = "0123456789abcdef";
  std::string result; result.reserve(bytes.size()*2);
  for (const auto value : bytes) { result += digits[value>>4]; result += digits[value&15]; }
  return result;
}
Json pin(const fs::path &path) {
  const auto bytes = readBytes(path.u8string(),128*mib);
  return {{"path",path.u8string()},{"bytes",bytes.size()},{"sha256",rankRuntimeSha256(bytes)}};
}
Json runtimeIdentity() {
  Json value{{"imgui",IMGUI_VERSION},{"pointer_bits",sizeof(void*)*8},{"producer_source",__FILE__}};
#ifdef _WIN32
  value["process_id"] = GetCurrentProcessId();
  std::array<wchar_t,32768> path{};
  const auto n = GetModuleFileNameW(nullptr,path.data(),static_cast<DWORD>(path.size()));
  require(n>0 && n<path.size(),"Cannot observe history executable path");
  value["executable"] = pin(fs::path(std::wstring(path.data(),n)));
  const auto modulePin = [&](LPCWSTR address) {
    HMODULE module = nullptr;
    require(GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
        GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,address,&module)!=0,
        "Cannot identify actual history artifact");
    const auto count = GetModuleFileNameW(module,path.data(),static_cast<DWORD>(path.size()));
    require(count>0 && count<path.size(),"Cannot observe history artifact path");
    return pin(fs::path(std::wstring(path.data(),count)));
  };
  static const int anchor = 0;
  value["producer_artifact"] = modulePin(reinterpret_cast<LPCWSTR>(&anchor));
  value["native_artifact"] = modulePin(reinterpret_cast<LPCWSTR>(&imagejxx::shortcut_commands));
#elif defined(__linux__) || defined(__APPLE__)
  const auto pid = ::getpid();
  require(pid > 0,"Cannot observe history process identity");
  value["process_id"] = static_cast<std::uint64_t>(pid);
  const auto observedPin = [](const fs::path &observed) {
    // Loader/OS paths can name an interpreter symlink or a relative image.
    // Pin the actual canonical file, matching the external origin reader.
    std::error_code error;
    const auto resolved = fs::canonical(observed,error);
    require(!error && resolved.is_absolute() && fs::is_regular_file(resolved,error) && !error,
        "Cannot resolve observed history artifact to a regular file");
    return pin(resolved);
  };
  std::array<char,32768> path{};
#if defined(__APPLE__)
  auto size = static_cast<std::uint32_t>(path.size());
  require(_NSGetExecutablePath(path.data(),&size)==0 && path.front()!=0 &&
      std::find(path.begin(),path.end(),char{})!=path.end(),
      "Cannot observe bounded history executable path");
  value["executable"] = observedPin(fs::u8path(path.data()));
#else
  const auto count = ::readlink("/proc/self/exe",path.data(),path.size());
  require(count>0 && static_cast<std::size_t>(count)<path.size(),
      "Cannot observe bounded history executable path");
  value["executable"] = observedPin(fs::u8path(
      std::string(path.data(),static_cast<std::size_t>(count))));
#endif
  const auto modulePin = [&](const void *address) {
    Dl_info module{};
    require(::dladdr(address,&module)!=0 && module.dli_fname && *module.dli_fname,
        "Cannot identify actual history artifact");
    return observedPin(fs::u8path(module.dli_fname));
  };
  static const int anchor = 0;
  value["producer_artifact"] = modulePin(&anchor);
  value["native_artifact"] = modulePin(
      reinterpret_cast<const void *>(&imagejxx::shortcut_commands));
#else
  throw std::runtime_error("History artifact origins require Windows, Linux or macOS");
#endif
  return value;
}
Json displayJson(const DisplayState &d) {
  return {{"t",d.t},{"c",d.c},{"crosshair",d.crosshair},{"orthogonal",d.orthogonal},
      {"linked_zoom",d.linkedZoom},{"zoom",d.zoom},{"plane_zoom",d.planeZoom},
      {"pan",d.pan},{"revision",d.revision},{"properties",metadataJson(d.properties)}};
}
Json historyJson(const HistoryInfo &h) {
  return {{"undo_steps",h.undo_steps},{"redo_steps",h.redo_steps},{"step_limit",h.step_limit},
      {"pixel_budget",h.pixel_budget},{"pixels",{{"logical_bytes",h.pixels.logical_bytes},
          {"current_bytes",h.pixels.current_bytes},{"retained_bytes",h.pixels.retained_bytes},
          {"obsolete_bytes",h.pixels.obsolete_bytes},{"retained_planes",h.pixels.retained_planes},
          {"exact",h.pixels.exact}}}};
}
// Return detached JSON values only. Native snapshots/plane borrows end before
// any next edit or obsolete-buffer measurement; no external retained aliases.
Json snapshot(const std::shared_ptr<imagejxx::Document> &owner) {
  static_assert(sizeof(double)==8 && std::numeric_limits<double>::is_iec559,
      "History fixture requires explicit IEEE binary64 storage");
  const auto source = owner->snapshot(); const auto &data = source.data();
  const auto shape = data.dataset.shape(); const auto &cal = data.dataset.calibration();
  const auto revisions = source.revisions();
  std::vector<unsigned char> declared, storage;
  for (int t=0;t<shape.t;++t) for (int c=0;c<shape.c;++c) for (int z=0;z<shape.z;++z) {
    const auto plane=data.dataset.plane({t,c,z});
    require(plane->type==PixelType::UInt8 && plane->channels==1,"History declared fixture must remain U8/S1");
    for (const double value : plane->pixels) {
      require(std::isfinite(value) && value>=0 && value<=255 && std::trunc(value)==value,
          "History actual U8 fixture sample must remain a canonical integer");
      declared.push_back(static_cast<unsigned char>(value));
      std::uint64_t bits=0; std::memcpy(&bits,&value,sizeof bits);
      for(int byte=0;byte<8;++byte)storage.push_back(static_cast<unsigned char>(bits>>(byte*8)));
    }
  }
  Json result{{"id",source.id()},{"shape",shape.dimensions()},{"dtype","uint8"},
      {"data_identity",source.data_identity()?Json(*source.data_identity()):Json()},
      {"dirty",source.dirty()},{"revisions",{{"pixels",revisions.pixels},{"metadata",revisions.metadata},
          {"selection",revisions.selection},{"display",revisions.display}}},
      {"display",displayJson(source.display())},{"history",historyJson(owner->history())},
      {"metadata",{{"title",data.metadata.title},{"properties",metadataJson(data.metadata.properties)},
          {"extensions",metadataJson(data.metadata.extensions)},{"record_layout",metadataJson(data.metadata.record_layout)}}},
      {"calibration",{{"spacing",cal.spacing},{"origin",cal.origin},{"direction",cal.direction},
          {"unit",cal.unit},{"frame_interval",cal.frame_interval},{"time_origin",cal.time_origin},{"time_unit",cal.time_unit}}},
      {"selection",data.selection?selectionJson(*data.selection):Json()},
      {"previous_selection",data.previous_selection?selectionJson(*data.previous_selection):Json()},
      {"rois",Json::array()},{"overlays",Json::array()},
      {"sample_policy",data.dataset.sample_policy()==FloatingSamplePolicy::FiniteOnly?"finite_only":"preserve_quiet_nan_float32"},
      {"spectrum_present",data.spectrum.has_value()},
      {"pixels",{{"shape",shape.dimensions()},{"pixel_type","uint8"},{"order","TCZYXS"},
          {"hex",hex(declared)},{"storage_f64le_hex",hex(storage)}}}};
  for(const auto &roi:data.rois)result["rois"].push_back({{"name",roi.name},{"selection",selectionJson(roi.selection)},
      {"t",roi.t?Json(*roi.t):Json()},{"c",roi.c?Json(*roi.c):Json()},{"z",roi.z?Json(*roi.z):Json()},
      {"attributes",metadataJson(roi.attributes)}});
  for(const auto &overlay:data.overlays)result["overlays"].push_back(metadataJson(overlay));
  return result;
}
DocumentData fixture(bool large) {
  const int width=large?256:8, height=large?128:6, depth=large?1:2;
  Stack planes;
  for(int z=0;z<depth;++z) {
    Image image(width,height,1,PixelType::UInt8);
    for(int y=0;y<height;++y)for(int x=0;x<width;++x)image.at(x,y)=large?0:7*z+3*y+x;
    planes.push_back(std::move(image));
  }
  SpatialCalibration calibration;
  calibration.spacing={2,3,5}; calibration.origin={11,-7,4}; calibration.unit="um";
  DocumentData data{Dataset::from_planes({1,1,depth,height,width,1},std::move(planes),calibration)};
  data.metadata.title=large?"Owned history budget":"Owned history steps";
  data.metadata.properties["fixture_recipe"]=std::string(large?"zero;fill1..10":"7*z+3*y+x;add1x10");
  data.metadata.extensions["future"]=metadata_array({true,std::int64_t(17)});
  data.selection=Selection("rectangle",{{1,1},{4,4}});
  data.previous_selection=Selection("line",{{0,0},{2,2}});
  Roi roi{"retained area",Selection("rectangle",{{1,1},{4,4}}),{},{},0,{}};
  roi.attributes["future.style"]=metadata_object({{"rgba",metadata_array({
      std::int64_t(1),std::int64_t(2),std::int64_t(3),std::int64_t(255)})}});
  data.rois.push_back(std::move(roi)); return data;
}
std::shared_ptr<imagejxx::Document> makeOwner(bool large) {
  return std::make_shared<imagejxx::Document>(fixture(large));
}
void edits(const std::shared_ptr<imagejxx::Document> &owner,bool large,int first=1,int last=10) {
  for(int iteration=first;iteration<=last;++iteration) {
    const auto source=owner->snapshot(); auto transaction=source.begin_edit();
    const auto shape=source.data().dataset.shape();
    for(int z=0;z<shape.z;++z) {
      Image replacement=*source.data().dataset.plane({0,0,z});
      for(auto &value:replacement.pixels)value=large?iteration:value+1;
      transaction.replace_plane({0,0,z},std::move(replacement));
    }
    owner->commit(transaction);
  }
}
SessionLimits sessionLimits() {
  SessionLimits limits; limits.max_documents=4; limits.max_planes=8; limits.max_samples=1024*1024;
  limits.max_json_bytes=16*mib; limits.max_encoded_bytes=16*mib; limits.max_working_bytes=128*mib;
  return limits;
}
void writeText(const fs::path &path,const std::string &text) {
  std::ofstream file(path,std::ios::binary);
  require(file.is_open(),"Cannot write owned history fixture");
  file.write(text.data(),static_cast<std::streamsize>(text.size())); file.close();
  require(!file.fail(),"Cannot finish owned history fixture");
}

class Driver {
public:
  Driver(Window &window,const std::string &caseId,const std::string &context,fs::path directory)
      :window_(&window),directory_(std::move(directory)),start_(Clock::now()) {
    report_={{"schema","imagejxx.history-settings-ui-observations"},{"version",1},
        {"contract","p06h.history-settings-ui.001"},{"case_id",caseId},{"context",context},
        {"acceptance_claim",false},{"observation_state","running"},
        {"scope","Actual shared ImGui physical frames/native state; no OS input or GPU presentation claim"},
        {"runtime_before",runtimeIdentity()},{"frames",Json::array()},{"checkpoints",Json::array()},
        {"control_observations",Json::array()},{"api_fixture_setup",Json::array()},
        {"exceptions",Json::array()},{"reopen_observations",Json::array()},
        {"validator_activity",Json::array()},{"lifecycle",Json::array()},
        {"frame_projection",{{"schema","imagejxx.history-settings-ui-frame-projection"},{"version",1},
            {"keys",frameFields},{"policy","Complete actual source-present values; no expected-driven filtering"}}},
        {"limits",{{"max_frames",180},{"max_seconds",15},{"max_report_bytes",maxReport},
            {"max_archive_bytes",maxArchive},{"parent_seconds",60},{"codec_working_bytes",128*mib}}}};
  }
  ~Driver(){close();}
  Window &window(){return *window_;}
  const fs::path &directory()const{return directory_;}
  std::string preferencesPath()const{return last_.at("preferences_path").get<std::string>();}
  Json &report(){return report_;}
  void phase(const std::string &name){phase_=name;}
  void setup(const std::string &operation,Json values=Json::object()) {
    report_["api_fixture_setup"].push_back({{"phase",phase_},{"operation",operation},
        {"input_kind","api_fixture_setup"},{"values",std::move(values)}});
  }
  void open(){require(!session_,"History session already open");session_=std::make_unique<HeadlessInputSession>(*window_);}
  void close()noexcept {
    if(!session_)return;
    try{report_["lifecycle"].push_back(session_->lifecycle());session_->close();
      report_["lifecycle"].push_back(session_->lifecycle());}catch(...){}
    session_.reset();
  }
  void start(){window_->recording=true;setup("Window.recording",{{"value",true}});open();settle(3);}
  void use(std::unique_ptr<Window> window,const std::string &role) {
    close(); window_=window.get(); windows_.push_back(std::move(window));
    owners_.clear(); retained_.clear(); instanceRole_=role;
    setup("fresh_native_Window_instance",{{"instance_role",role},{"os_relaunch",false}});
    open();settle(3);
  }
  std::shared_ptr<imagejxx::Document> add(const std::string &role,bool large) {
    auto owner=makeOwner(large);window_->addNativeDocument(owner);owners_[role]=owner;
    setup("Window.addNativeDocument",{{"role",role},{"large",large},{"document_id",owner->id}});
    return owner;
  }
  void addLoaded(const std::string &role,std::shared_ptr<imagejxx::Document> owner) {
    window_->addNativeDocument(owner);owners_[role]=std::move(owner);
    setup("Window.addNativeDocument.decoded_session",{{"role",role},{"document_id",owners_.at(role)->id}});
  }
  void retained(const std::string &role,std::shared_ptr<imagejxx::Document> owner){retained_[role]=std::move(owner);}
  void edit(const std::shared_ptr<imagejxx::Document> &owner,bool large,int first=1,int last=10) {
    setup("native_changed_edits",{{"document_id",owner->id},{"large",large},{"first",first},{"last",last}});
    edits(owner,large,first,last);
  }
  void policy(const Json &patch,bool persist=false) {
    setup("Window.updateSettings",{{"patch",patch},{"persist",persist}});window_->updateSettings(patch,persist);
  }
  Json frame(const Json &events=Json::array()) {
    require(session_&&report_["frames"].size()<180&&Clock::now()-start_<15s,
        "History case exceeds bounded frames/time or lacks live session");
    last_=session_->frame(events);
    require(last_.at("input_frame").at("raw_inputs")==events,"History physical event readback differs");
    files_["preferences"]=fs::u8path(last_.at("preferences_path").get<std::string>());
    Json projected=Json::object();for(const auto *key:frameFields)if(last_.contains(key))projected[key]=last_.at(key);
    report_["frames"].push_back({{"phase",phase_},{"physical_inputs",events},{"report",std::move(projected)}});
    return last_;
  }
  void settle(int count=1){for(int i=0;i<count;++i)frame();}
  void tap(const std::vector<std::string> &names){frame(keyEvents(names,true));frame(keyEvents(names,false));}
  Json at(const std::vector<std::string> &path)const {
    Json result=last_;for(const auto &key:path)result=result.at(key);return result;
  }
  Json control(const std::vector<std::string> &path) {
    const auto valid=[](const Json &item) {
      require(item.at("id").get<std::uint64_t>()!=0&&item.at("enabled")==true,"History actual control missing/disabled");
      require(item.value("visible",true),"History control is clipped");
      const auto &p=item.at("point"),&r=item.at("rect");
      require(p.size()==2&&r.size()==4&&p[0].get<double>()>=r[0].get<double>()&&p[0].get<double>()<r[2].get<double>()&&
          p[1].get<double>()>=r[1].get<double>()&&p[1].get<double>()<r[3].get<double>(),"History pointer outside actual control");
      if(item.contains("clip_rect")){const auto &clip=item.at("clip_rect");
        require(p[0].get<double>()>=clip[0].get<double>()&&p[0].get<double>()<clip[2].get<double>()&&
            p[1].get<double>()>=clip[1].get<double>()&&p[1].get<double>()<clip[3].get<double>(),"History control center clipped");}
    };
    std::string selector;for(const auto &part:path){if(!selector.empty())selector+='.';selector+=part;}
    for(int layout=0;layout<=3;++layout) {
      const auto item=at(path);valid(item);const auto first=report_["frames"].size();
      frame(Json::array({position(item.at("point"))}));const auto current=at(path);valid(current);
      if(current.at("id")!=item.at("id")||current.at("rect")!=item.at("rect")||current.at("point")!=item.at("point")) {
        require(layout<3,"History actual widget geometry did not stabilize");continue;
      }
      report_["control_observations"].push_back({{"phase",phase_},{"selector",selector},{"observed_frame",first-1},
          {"control_path",path},{"input_frames",{first,first+1,first+2}}});
      frame(Json::array({button(true)}));frame(Json::array({button(false)}));return current;
    }
    throw std::runtime_error("History control has no stable geometry");
  }
  void text(const std::string &name,const std::string &value) {
    const auto item=control({"preferences_panel","fields",name});
    require(last_.at("input_frame").at("focus").at("active_id")==item.at("id")&&
        last_.at("input_frame").at("input_text").at("id")==item.at("id"),
        "Actual history InputInt did not acquire native text ownership");
    tap({primary(),"A"});frame(Json::array({Json{{"type","text"},{"value",value}}}));tap({"Enter"});
  }
  void preferences(){setup("Window.action.show_preferences",{{"search","history"}});
    window_->action("show_preferences",{{"search","history"}});settle(2);}
  void pair(const char *steps,const char *memory){text("history_limit",steps);text("history_memory_mb",memory);}
  void widget(const char *name){control({"preferences_panel","widget_controls",name});}
  void checkpoint(const std::string &name) {
    require(checkpoints_.insert(name).second,"Duplicate history checkpoint");phase(name);
    // Native edits/policy may have happened since the last rendered observation.
    frame();
    report_["checkpoints"].push_back({{"phase",name},{"frame_index",report_["frames"].size()-1},
        {"state",state()},{"files",fileState()}});
  }
  Json state()const {
    Json documents=Json::object(),retained=Json::object(),ids=Json::array();
    for(const auto &entry:owners_)documents[entry.first]=snapshot(entry.second);
    for(const auto &entry:retained_)retained[entry.first]=snapshot(entry.second);
    for(const auto &facade:window_->documents())ids.push_back(facade->id);
    const auto active=window_->documents().empty()?Json():Json(window_->nativeDocument()->id);
    return {{"instance_role",instanceRole_},{"settings",settingsJson(window_->settings())},
        {"preferences",window_->preferencesJson()},{"document_ids",ids},{"facade_ids",ids},
        {"active_document_id",active},{"documents",std::move(documents)},{"retained_documents",std::move(retained)},
        {"results",window_->results},{"workflow",window_->workflow},{"recording",window_->recording},
        {"busy",window_->busy()},{"preferences_path",last_.at("preferences_path")},
        {"shortcut_overrides",window_->shortcutProfile().override_lines()}};
  }
  void file(const std::string &role,const fs::path &path){files_[role]=path;}
  Json fileState()const {
    Json result=Json::array();std::size_t total=0;
    for(const auto &entry:files_) {
      Json row{{"role",entry.first},{"path",entry.second.u8string()},{"exists",fs::is_regular_file(entry.second)}};
      if(row.at("exists")==true){const auto bytes=readBytes(entry.second.u8string(),maxReport);total+=bytes.size();
        require(total<=maxArchive,"Owned history files exceed aggregate bound");
        row["byte_count"]=bytes.size();row["hex"]=hex(bytes);row["sha256"]=rankRuntimeSha256(bytes);
        try{row["json"]=Json::parse(bytes);}catch(const Json::exception&){}
      }else{row["byte_count"]=0;row["hex"]="";row["sha256"]=Json();}
      result.push_back(std::move(row));
    }return result;
  }
  void save(const std::shared_ptr<imagejxx::Document> &owner,const fs::path &path,const char *role) {
    setup("native.save_session",{{"path",path.u8string()},{"document_id",owner->id},{"logical_working_limit",128*mib}});
    {SessionCapture capture;capture.documents.push_back(owner->snapshot());save_session(path.u8string(),capture,sessionLimits());}
    file(role,path);
  }
  std::shared_ptr<imagejxx::Document> load(const fs::path &path) {
    setup("native.load_session",{{"path",path.u8string()},{"logical_working_limit",128*mib}});
    auto decoded=load_session(path.u8string(),sessionLimits());require(decoded.documents.size()==1,"History owned session must decode one owner");
    return decoded.documents.front();
  }
  void reopened(const std::string &phaseName) {
    report_["reopen_observations"].push_back({{"phase",phaseName},{"input_kind","api_fixture_setup"},
        {"instance_kind","fresh_native_window"},{"os_relaunch",false},{"state",state()},{"files",fileState()}});
  }
  void exception(const char *operation,const std::exception &error) {
    report_["exceptions"].push_back({{"phase",phase_},{"operation",operation},{"input_kind","api_fixture_setup"},
        {"type",dynamic_cast<const std::invalid_argument*>(&error)?"invalid_argument":"exception"},{"message",error.what()}});
  }
  Json finish(){report_["files"]=fileState();close();report_["runtime_after"]=runtimeIdentity();
    internActualObservationFields(report_);
    report_["elapsed_seconds"]=std::chrono::duration<double>(Clock::now()-start_).count();
    require(report_["elapsed_seconds"].get<double>()<=15,"History UI case exceeded15 seconds");
    report_["observation_state"]="observed";report_["raw_report_path"]=(directory_/"report.json").u8string();
    const auto raw=report_.dump();require(raw.size()<=maxReport,"History actual report exceeds16MiB");
    writeText(directory_/"report.json",raw);return report_;}
  void failed(const std::exception &error)noexcept{try{close();report_["observation_state"]="failed";
    exception("producer",error);const auto raw=report_.dump();if(raw.size()<=maxReport)writeText(directory_/"report-failed.json",raw);}catch(...){} }
private:
  Window *window_;fs::path directory_;Clock::time_point start_;
  std::vector<std::unique_ptr<Window>> windows_;
  std::unique_ptr<HeadlessInputSession> session_;
  std::map<std::string,std::shared_ptr<imagejxx::Document>> owners_,retained_;
  std::map<std::string,fs::path> files_;std::set<std::string> checkpoints_;
  Json report_,last_;std::string phase_="fixture_setup",instanceRole_="main";
};

void HU01(Driver &d) {
  d.start();auto source=d.add("source",false);d.edit(source,false);d.checkpoint("default");
  d.phase("before_apply");d.preferences();d.checkpoint("before_apply");
  d.phase("typed");d.text("history_limit","3");d.checkpoint("typed");
  d.phase("applied_existing");d.widget("apply");d.checkpoint("applied_existing");
  d.phase("changed");auto changed=d.add("changed",false);d.edit(changed,false);d.checkpoint("changed");
  d.checkpoint("before_prune");d.phase("pruned_two");d.policy({{"history_limit",2}});d.checkpoint("pruned_two");
  d.phase("undo");d.setup("native.undo",{{"document_id",changed->id}});changed->undo();d.checkpoint("undo");
  const auto preferencePath=d.preferencesPath();
  d.phase("persisted");d.use(std::make_unique<Window>(preferencePath),"persisted");
  auto persisted=d.add("persisted",false);d.edit(persisted,false);d.checkpoint("persisted");
  const auto session=d.directory()/"step.ijx";d.save(persisted,session,"session_step");
  d.phase("reopened");d.addLoaded("reopened",d.load(session));d.checkpoint("reopened");d.reopened("reopened");
}
void HU02(Driver &d) {
  d.policy({{"history_limit",128}});d.start();auto source=d.add("source",true);d.edit(source,true);d.checkpoint("default");
  d.phase("before_apply");d.preferences();d.text("history_limit","128");d.checkpoint("before_apply");
  d.phase("typed");d.text("history_memory_mb","1");d.checkpoint("typed");
  d.phase("changed");d.widget("apply");d.checkpoint("changed");
  d.phase("step_two");d.policy({{"history_limit",2}});d.checkpoint("step_two");
  d.phase("zero_budget");d.policy({{"history_memory_mb",0}});d.edit(source,true,11,13);d.checkpoint("zero_budget");
  const auto preferences=d.preferencesPath();
  d.phase("persisted");d.use(std::make_unique<Window>(preferences),"persisted");
  auto persisted=d.add("persisted",true);d.edit(persisted,true);d.checkpoint("persisted");
  const auto session=d.directory()/"budget.ijx";d.save(persisted,session,"session_budget");
  d.phase("reopened");d.addLoaded("reopened",d.load(session));d.checkpoint("reopened");d.reopened("reopened");
  d.phase("deduplicated");d.use(std::make_unique<Window>(preferences),"deduplicated");
  auto dedup=d.add("deduplicated",true);d.edit(dedup,true);
  for(int value=1;value<=2;++value){auto transaction=dedup->begin_edit();transaction.set_property("metadata_only",std::int64_t(value));dedup->commit(transaction);}
  d.setup("native.metadata_only_commits",{{"count",2},{"document_id",dedup->id}});d.checkpoint("deduplicated");
}
void HU03(Driver &d) {
  d.start();auto step=d.add("step",false),budget=d.add("budget",true);d.edit(step,false,1,3);d.edit(budget,true,1,3);
  d.checkpoint("before_detached");d.phase("after_detached");Settings detached;
  detached.update({{"history_limit",std::int64_t(2)},{"history_memory_mb",std::int64_t(1)}});
  d.report()["detached_settings"]=settingsJson(detached);d.setup("detached_Settings.update",{{"history_limit",2},{"history_memory_mb",1}});
  d.checkpoint("after_detached");d.phase("draft_changed");d.preferences();d.pair("3","1");d.checkpoint("draft_changed");
  d.phase("draft_closed");d.widget("close_button");d.checkpoint("draft_closed");
  d.phase("defaults");d.preferences();d.pair("3","1");d.widget("defaults");d.checkpoint("defaults");
  d.phase("revert");d.pair("3","1");d.widget("revert");d.checkpoint("revert");
  d.phase("pair_applied");d.pair("3","1");d.widget("apply");d.checkpoint("pair_applied");
  const auto preferences=d.preferencesPath();
  const auto stepPath=d.directory()/"pair-step.ijx",budgetPath=d.directory()/"pair-budget.ijx";
  {auto owner=makeOwner(false);edits(owner,false);d.save(owner,stepPath,"session_step");}
  {auto owner=makeOwner(true);owner->set_history_policy(128,0);edits(owner,true);d.save(owner,budgetPath,"session_budget");}
  d.phase("pair_reopened");d.use(std::make_unique<Window>(preferences),"pair_reopened");
  d.add("fresh",false);d.addLoaded("reopened_step",d.load(stepPath));d.addLoaded("reopened_budget",d.load(budgetPath));
  d.checkpoint("pair_reopened");d.reopened("pair_reopened");
}
void failureOwners(Driver &d,std::shared_ptr<imagejxx::Document> &step,std::shared_ptr<imagejxx::Document> &budget) {
  d.policy({{"history_limit",128}},false);step=d.add("step",false);budget=d.add("budget",true);
  d.edit(step,false);d.edit(budget,true);d.window().recording=true;
  d.window().workflow=Json::array({Json{{"sentinel","workflow"},{"value",7}}});
  d.window().results=Json::array({Json{{"sentinel","results"},{"value",11}}});
  d.setup("Window.recording_results_workflow.fixture");
}
void HU04(Driver &d) {
  d.start();d.policy({{"history_limit",128}},true);
  std::shared_ptr<imagejxx::Document> step,budget;failureOwners(d,step,budget);
  bool reject=false;int left=0,right=0;
  // Exercise the existing retained native validator contract; no new callback
  // seam, allocator hook or failure branch is added to product publication.
  const auto firstLease=step->constrain([&](const DocumentData&,const DisplayState&){if(reject)++left;});
  const auto secondLease=budget->constrain([&](const DocumentData&,const DisplayState&){if(reject){++right;throw std::invalid_argument("Owned history second-owner refusal");}});
  d.phase("before_validator");d.preferences();d.pair("2","1");d.checkpoint("before_validator");
  d.phase("validator_failed");reject=true;d.widget("apply");reject=false;
  d.report()["validator_activity"].push_back({{"phase","validator_failed"},{"first_calls",left},{"second_calls",right}});
  d.checkpoint("validator_failed");
  const auto parent=d.directory()/"absent-parent",failedPath=parent/"preferences.json";
  require(!fs::exists(parent),"History save blocker parent must start absent");
  d.phase("before_save");d.use(std::make_unique<Window>(failedPath.u8string()),"save_failure");
  require(!fs::exists(parent),"Constructing history Window must not create preferences parent");
  std::shared_ptr<imagejxx::Document> saveStep,saveBudget;failureOwners(d,saveStep,saveBudget);
  bool observe=false;int stagedLeft=0,stagedRight=0;
  const auto saveFirst=saveStep->constrain([&](const DocumentData&,const DisplayState&){if(observe)++stagedLeft;});
  const auto saveSecond=saveBudget->constrain([&](const DocumentData&,const DisplayState&){if(observe)++stagedRight;});
  d.preferences();d.pair("2","1");writeText(parent,"owned regular-file parent bytes must survive\n");
  d.file("parent_blocker",parent);d.setup("write_owned_regular_file_parent_after_Window_construction",{{"path",parent.u8string()}});
  d.checkpoint("before_save");d.phase("save_failed");observe=true;d.widget("apply");observe=false;
  d.report()["validator_activity"].push_back({{"phase","save_failed"},{"first_calls",stagedLeft},{"second_calls",stagedRight}});
  d.checkpoint("save_failed");
  d.phase("before_adoption");d.use(std::make_unique<Window>((d.directory()/"adoption-preferences.json").u8string()),"adoption_failure");
  d.policy({{"history_limit",2},{"history_memory_mb",1},{"default_zoom",4}},false);
  auto caller=std::make_shared<imagejxx::desktop::Document>(makeOwner(false));auto owner=caller->nativeOwner();
  edits(owner,false,1,5);owner->mark_saved();owner->undo();d.retained("caller",owner);
  const auto initialZoom=owner->snapshot().display().zoom;int zoomRefusals=0;
  const auto callerLease=owner->constrain([&](const DocumentData&,const DisplayState&display){
    if(display.zoom!=initialZoom){++zoomRefusals;throw std::invalid_argument("Caller refuses history candidate default zoom");}});
  d.setup("unattached_caller.five_edits_mark_saved_undo_and_zoom_validator");d.checkpoint("before_adoption");
  d.phase("adoption_failed");d.setup("Window.addDocument.refusing_candidate",{{"caller_id",owner->id}});
  try{d.window().addDocument(caller);}catch(const std::exception&error){d.exception("Window.addDocument",error);}
  d.report()["validator_activity"].push_back({{"phase","adoption_failed"},{"zoom_refusals",zoomRefusals}});
  d.checkpoint("adoption_failed");
}
} // namespace

Json historySettingsUiCase(Window &window,const std::string &caseId,
                           const std::string &context,const std::string &freshDirectory) {
  window.checkThread();require(context=="imgui_cpp"||context=="imgui_python","History context must identify actual launch");
  const std::array<const char*,4> ids{"HU01","HU02","HU03","HU04"};
  const auto found=std::find_if(ids.begin(),ids.end(),[&](const char*id){return caseId==id;});
  require(found!=ids.end(),"History UI case must be HU01..HU04");
  require(!ImGui::GetCurrentContext()&&!window.busy()&&window.documents().empty()&&window.results.empty()&&window.workflow.empty(),
      "History UI case requires fresh idle empty Window without ImGui context");
  auto directory=fs::absolute(fs::u8path(freshDirectory)).lexically_normal();
  require(!freshDirectory.empty()&&directory.has_filename()&&directory!=directory.root_path()&&!fs::exists(directory),
      "History owned directory must be fresh absent nonroot");
  fs::create_directories(directory.parent_path());require(fs::create_directory(directory),"Cannot exclusively create history directory");
  Driver driver(window,caseId,context,directory);
  using Case=void(*)(Driver&);const std::array<Case,4> cases{HU01,HU02,HU03,HU04};
  try{cases[static_cast<std::size_t>(found-ids.begin())](driver);return driver.finish();}
  catch(const std::exception&error){driver.failed(error);throw;}
}
} // namespace imagejxx::desktop
