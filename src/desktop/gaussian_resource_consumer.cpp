#include "headless_session_internal.hpp"
#include "window.hpp"
#include <imagejxx/gaussian.hpp>
#include <imgui_internal.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <limits>
#include <map>
#include <optional>
#include <set>
#include <sstream>
#include <stdexcept>
#include <thread>

namespace imagejxx::desktop {
namespace {
constexpr int width = 512, height = 512;
constexpr std::size_t mib = 1024 * 1024;
constexpr const char *caseId =
    "P02a.shared_ui.20.busy-failure-cancel-atomicity.held-preview-lease-lowered-budget";
void require(bool value, const char *message) {
  if (!value) throw std::runtime_error(message);
}
std::string threadId() {
  std::ostringstream out; out << std::this_thread::get_id(); return out.str();
}
std::string address(const void *pointer) {
  std::ostringstream out;
  out << "0x" << std::hex << reinterpret_cast<std::uintptr_t>(pointer);
  return out.str();
}
std::filesystem::path freshDirectory(const std::string &directory) {
  require(!directory.empty(), "R16 output directory is required");
  const auto path = std::filesystem::absolute(std::filesystem::u8path(directory)).lexically_normal();
  require(path != path.root_path(), "R16 cannot own a filesystem root");
  std::filesystem::create_directories(path.parent_path());
  require(std::filesystem::create_directory(path), "R16 output directory must be fresh");
  return path;
}
Json usageJson(const JobUsage &usage) {
  return {{"running", usage.running}, {"queued", usage.queued},
          {"running_bytes", usage.running_bytes}, {"retained_bytes", usage.retained_bytes},
          {"reserved_bytes", usage.reserved_bytes}};
}
Json limitsJson(const JobLimits &limits) {
  return {{"workers", limits.workers}, {"memory_bytes", limits.memory_budget},
          {"capacity", limits.capacity}};
}
const char *stateName(JobState state) {
  switch (state) {
  case JobState::Queued: return "Queued";
  case JobState::Running: return "Running";
  case JobState::Ready: return "Ready";
  case JobState::Publishing: return "Publishing";
  case JobState::Completed: return "Completed";
  case JobState::Failed: return "Failed";
  case JobState::Canceled: return "Canceled";
  }
  throw std::logic_error("Unknown R16 native job state");
}
Json scalarJobs(const JobScheduler &scheduler) {
  Json result = Json::array();
  for (const auto &job : scheduler.jobs())
    result.push_back({{"id",job.id},{"document_id",job.document_id},{"label",job.label},
        {"state",stateName(job.state)},{"error",job.error},{"resource_bytes",job.resource_bytes},
        {"cancel_requested",job.cancel_requested},{"completed",job.completed},{"total",job.total},
        {"checkpoints",job.checkpoints},{"stage",job.stage},
        {"stage_completed",job.stage_completed},{"stage_total",job.stage_total}});
  return result;
}
Image ownedInput() {
  Image image(width,height,1,PixelType::Float32);
  for (int y=0;y<height;++y)
    for (int x=0;x<width;++x) {
      const float value = static_cast<float>(((x+3*y)%251)/16.0 +
                                            (x==257 && y==193 ? 512.0 : 0.0));
      image.at(x,y)=value;
    }
  return image;
}
std::vector<unsigned char> rawFloat32(const std::vector<double> &pixels) {
  static_assert(sizeof(float)==4 && std::numeric_limits<float>::is_iec559,
                "R16 requires IEEE754 Float32");
  std::vector<unsigned char> bytes; bytes.reserve(pixels.size()*4);
  for (double value : pixels) {
    require(std::isfinite(value), "R16 source/readback is nonfinite");
    const float sample=static_cast<float>(value);
    require(static_cast<double>(sample)==value, "R16 Float32 observation has noncanonical values");
    std::uint32_t bits; std::memcpy(&bits,&sample,sizeof(bits));
    for (int shift=0;shift<32;shift+=8) bytes.push_back(static_cast<unsigned char>(bits>>shift));
  }
  return bytes;
}
// Observation identity only. Independent tests reconstruct and hash the source.
std::string sha256(const std::vector<unsigned char> &input) {
  constexpr std::array<std::uint32_t,64> constants{
      0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,
      0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
      0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
      0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
      0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,
      0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
      0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,
      0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2};
  std::array<std::uint32_t,8> hash{
      0x6a09e667,0xbb67ae85,0x3c6ef372,0xa54ff53a,0x510e527f,0x9b05688c,0x1f83d9ab,0x5be0cd19};
  auto bytes=input; bytes.push_back(0x80);
  while (bytes.size()%64!=56) bytes.push_back(0);
  const auto bits=std::uint64_t(input.size())*8;
  for (int shift=56;shift>=0;shift-=8) bytes.push_back(static_cast<unsigned char>(bits>>shift));
  auto rotate=[](std::uint32_t value,int n) { return (value>>n)|(value<<(32-n)); };
  for (std::size_t offset=0;offset<bytes.size();offset+=64) {
    std::array<std::uint32_t,64> words{};
    for (std::size_t i=0;i<16;++i)
      for (std::size_t j=0;j<4;++j) words[i]=(words[i]<<8)|bytes[offset+i*4+j];
    for (std::size_t i=16;i<64;++i) {
      const auto a=words[i-15], b=words[i-2];
      words[i]=words[i-16]+(rotate(a,7)^rotate(a,18)^(a>>3))+words[i-7]+
               (rotate(b,17)^rotate(b,19)^(b>>10));
    }
    auto work=hash;
    for (std::size_t i=0;i<64;++i) {
      const auto first=work[7]+(rotate(work[4],6)^rotate(work[4],11)^rotate(work[4],25))+
          ((work[4]&work[5])^(~work[4]&work[6]))+constants[i]+words[i];
      const auto second=(rotate(work[0],2)^rotate(work[0],13)^rotate(work[0],22))+
          ((work[0]&work[1])^(work[0]&work[2])^(work[1]&work[2]));
      for (std::size_t j=7;j>0;--j) work[j]=work[j-1];
      work[4]+=first; work[0]=first+second;
    }
    for (std::size_t i=0;i<8;++i) hash[i]+=work[i];
  }
  std::ostringstream out; out<<std::hex<<std::setfill('0');
  for (auto value:hash) out<<std::setw(8)<<value;
  return out.str();
}
Json calibrationJson(const SpatialCalibration &value) {
  return {{"spacing",value.spacing},{"origin",value.origin},{"direction",value.direction},
      {"unit",value.unit},{"time_unit",value.time_unit},
      {"frame_interval",value.frame_interval},{"time_origin",value.time_origin}};
}
Json displayJson(const DisplayState &value) {
  return {{"t",value.t},{"c",value.c},{"orthogonal",value.orthogonal},
      {"linked_zoom",value.linkedZoom},{"crosshair",value.crosshair},{"zoom",value.zoom},
      {"plane_zoom",value.planeZoom},{"pan",value.pan},{"revision",value.revision},
      {"properties",metadataJson(value.properties)}};
}

class ResourceDriver {
public:
  ResourceDriver(Window &window,std::shared_ptr<JobScheduler> scheduler,
                 std::string context,std::filesystem::path directory)
      : window_(window),scheduler_(std::move(scheduler)),directory_(std::move(directory)) {
    report_={{"schema_version",1},{"case_id",caseId},{"context",std::move(context)},
        {"status","unverified"},{"acceptance_claim",false},{"fixture_id","resource_f32_512x512"},
        {"fixture_formula","Float32(((x+3*y)%251)/16 + (512 if (x,y)==(257,193) else 0))"},
        {"frames",Json::array()},{"checkpoints",Json::array()},
        {"preferences_applies",Json::array()},{"control_observations",Json::array()},
        {"api_inputs",Json::array()},{"lifecycle",Json::array()},{"creator_thread",threadId()},
        {"observation_scope","Actual CPU texture-upload input/software cache; no GPU readback"},
        {"comparison_scope","Owned public native primitive integration; no independent upstream oracle"}};
    const auto source=ownedInput();
    DocumentData data{Dataset::from_stack({source})};
    data.metadata.title="Owned R16 source";
    data.metadata.properties["r16_property"]="preserve";
    data.metadata.extensions["r16_extension"]="unknown-kept";
    Selection roiShape("rectangle",{{11,17},{29,41}});
    roiShape.attributes["r16_shape_unknown"]="shape-attribute-kept";
    data.rois.push_back({"retained-r16-roi",std::move(roiShape),
                        std::nullopt,std::nullopt,0,
                        {{"r16_unknown","roi-attribute-kept"}}});
    data.overlays.push_back({{"kind","point"},{"color","#ffcc0000"},
        {"points",metadata_array({metadata_array({9.,13.})})},
        {"r16_unknown","overlay-attribute-kept"}});
    owner_=std::make_shared<imagejxx::Document>(std::move(data));
    window_.addNativeDocument(owner_);
    window_.recording=false;
    auto display=owner_->snapshot().display();
    display.crosshair={257,193,0};
    display.properties["display_range"]=metadata_array({0.,528.});
    window_.setNativeDisplay(display);
    owner_->mark_saved(); original_=owner_->snapshot();
    report_["api_inputs"].push_back({{"purpose","fixture/default preparation only"},
        {"methods",{"addNativeDocument","setNativeDisplay","mark_saved"}},
        {"recording",false},{"tested_changes_use_api_setters",false}});
    report_["owned_primitive_raw"]=rawImage("owned-primitive.f32le",gaussian_blur_imagej(source,2.,2.));
  }
  ~ResourceDriver() { if (!cleaned_) try { cleanupJobs(); } catch (...) {} }
  Json run() {
    try {
      HeadlessInputSession session(window_);
      session_=&session;
      report_["lifecycle"].push_back({{"label","opened"},{"observed",session.lifecycle()}});
      deadline_=std::chrono::steady_clock::now()+std::chrono::seconds(30);
      frame("warmup-1"); frame("warmup-2");
      preferencesPath_=std::filesystem::absolute(std::filesystem::u8path(
          last_.at("preferences_path").get<std::string>())).lexically_normal();
      require(preferencesPath_.parent_path()==directory_ ||
                  preferencesPath_.parent_path()==directory_.parent_path(),
              "R16 Preferences path must belong to the fresh case or parent directory");
      require(!std::filesystem::exists(preferencesPath_),
              "R16 Preferences preparation requires a fresh file, never an existing configuration");
      window_.updateSettings({{"processing_memory_mb",512}},true);
      report_["api_inputs"].push_back({{"purpose","initial Preferences preparation only"},
          {"method","updateSettings"},{"values",{{"processing_memory_mb",512}}},{"persist",true}});
      frame("initial_512_MiB_preferences_persisted_API_preparation");
      report_["before"]=nativeState("source-before.f32le");
      checkpoint("baseline");
      require(window_.settings().integer("processing_memory_mb")==512 &&
                  scheduler_->limits().memory_budget==512*mib,
              "R16 preparation did not persist the initial 512 MiB limit");
      openGaussian(); replaceText("gaussian.sigma","2");
      require(last_.at("gaussian_panel").at("sigma")==2 &&
                  last_.at("gaussian_panel").at("preview")==false,
              "R16 physical Gaussian draft differs from sigma2/PreviewOff");
      click("gaussian.preview");
      await("latest_preview_ready_and_rendered",[&] {
        const auto &jobs=last_.at("gaussian_jobs");
        return !jobs.at("active_installed_generation").is_null() &&
            jobs.at("preview").at("owns_job")==true &&
            scheduler_->usage().running_bytes==0 && scheduler_->usage().retained_bytes>0 &&
            last_.at("ordinary_pixels").at("pixels_source")=="preview";
      });
      const auto ready=checkpoint("retained_lease_ready");
      verifyHeld(ready);
      report_["retained_native_job_id"]=ready.at("rendered").at("gaussian_jobs").at("installed_preview_storage").at("job_id");
      report_["retained_request_generation"]=ready.at("rendered").at("gaussian_jobs").at("installed_preview_storage").at("request_generation");
      const auto charge=ready.at("usage").at("retained_bytes").get<std::size_t>();
      require(charge>mib,"R16 fixed workload has no positive integer MiB below its actual held charge");
      const auto lower=(charge-1)/mib;
      require(lower>=1 && lower<=static_cast<std::size_t>(std::numeric_limits<int>::max()),
              "R16 derived Preferences integer is outside the real control range");
      report_["derived_memory_mb"]=lower;
      openPreferences(); editMemory(lower); applyPreferences("lower_limit_rejected");
      const auto rejected=checkpoint("lower_limit_rejected");
      verifyRejected(ready,rejected);
      closePreferences(); click("gaussian.preview");
      await("preview_off_releases_lease",[&] {
        return scheduler_->usage().reserved_bytes==0 &&
            last_.at("gaussian_jobs").at("installed_preview_storage").is_null() &&
            last_.at("ordinary_pixels").at("pixels_source")=="live";
      });
      const auto released=checkpoint("lease_released");
      verifyUnchanged(released.at("native_state"));
      require(released.at("rendered").at("gaussian_panel").at("preview")==false &&
                  released.at("usage").at("retained_bytes")==0 &&
                  released.at("usage").at("running_bytes")==0 &&
                  released.at("limits")==ready.at("limits") &&
                  released.at("preferences_file")==ready.at("preferences_file"),
              "R16 PreviewOff did not release exactly the unchanged held reservation");
      openPreferences(); editMemory(lower); applyPreferences("lower_limit_accepted");
      const auto accepted=checkpoint("lower_limit_accepted");
      verifyUnchanged(accepted.at("native_state"));
      require(window_.settings().integer("processing_memory_mb")==static_cast<std::int64_t>(lower) &&
                  scheduler_->limits().memory_budget==lower*mib &&
                  accepted.at("usage").at("reserved_bytes")==0 &&
                  last_.at("preferences_panel").at("error")=="" &&
                  accepted.at("preferences_file").at("envelope")==window_.preferencesJson(),
              "R16 physical lower-limit Apply/save was not accepted atomically after release");
      const auto jobsBefore=accepted.at("actual_native_jobs");
      closePreferences(); click("gaussian.preview");
      await("gaussian_preview_resource_rejected",[&] {
        return !last_.at("gaussian_panel").at("preview_error").get<std::string>().empty();
      });
      const auto denied=checkpoint("admission_rejected");
      verifyUnchanged(denied.at("native_state"));
      require(denied.at("actual_native_jobs")==jobsBefore &&
                  denied.at("usage").at("reserved_bytes")==0 &&
                  denied.at("usage").at("running")==0 && denied.at("usage").at("queued")==0 &&
                  denied.at("rendered").at("gaussian_jobs").at("preview").at("owns_job")==false &&
                  denied.at("rendered").at("gaussian_jobs").at("preview").at("exit")=="admission_failed" &&
                  denied.at("rendered").at("gaussian_jobs").at("request_error")==
                      "Job reservation exceeds the processing memory budget" &&
                  denied.at("rendered").at("gaussian_jobs").at("installed_preview_storage").is_null() &&
                  denied.at("rendered").at("ordinary_pixels").at("pixels_source")=="live" &&
                  denied.at("limits")==accepted.at("limits") &&
                  denied.at("settings")==accepted.at("settings") &&
                  denied.at("preferences_file")==accepted.at("preferences_file"),
              "R16 oversized Preview admitted work/storage or changed source/settings before rejection");
      click("gaussian.cancel"); frame("after_physical_cancel");
      require(last_.at("gaussian_panel").at("open")==false &&
                  last_.at("gaussian_panel").at("exit_reason")=="cancel",
              "R16 actual Gaussian Cancel did not close the command");
      cleanupJobs(); frame("after_native_cleanup");
      const auto clean=checkpoint("cleanup"); verifyUnchanged(clean.at("native_state"));
      report_["after"]=nativeState("source-after.f32le");
      report_["original_after_raw"]=rawImage("original-after.f32le",
          *original_->data().dataset.plane({0,0,0}));
      report_["lifecycle"].push_back({{"label","before_close"},{"observed",session.lifecycle()}});
      session.close(); session_=nullptr;
      report_["lifecycle"].push_back({{"label","closed"},{"observed",session.lifecycle()}});
      report_["final_cleanup"]={{"usage",usageJson(scheduler_->usage())},
          {"limits",limitsJson(scheduler_->limits())},{"actual_native_jobs",scalarJobs(*scheduler_)},
          {"lifecycle",session.lifecycle()},{"observer_retained_result_aliases",0}};
      save("case-unverified.json");
      verifyUnchanged(report_.at("after"));
      require(report_.at("original_after_raw").at("sha256")==
                  report_.at("before").at("source_raw").at("sha256") &&
                  scheduler_->usage().reserved_bytes==0 && scheduler_->usage().running==0 &&
                  scheduler_->usage().queued==0 && ImGui::GetCurrentContext()==nullptr,
              "R16 cleanup leaked reservations/context or changed the retained original source");
      report_["status"]="passed"; save("case.json"); cleaned_=true; return report_;
    } catch (const std::exception &error) {
      session_=nullptr;
      report_["status"]="failed"; report_["failure"]={{"message",error.what()}};
      // Preserve already observed state before cleanup can release evidence.
      try { save("case-failed-before-cleanup.json"); } catch (...) {}
      try {
        cleanupJobs();
        report_["failure_cleanup"]={{"usage",usageJson(scheduler_->usage())},
                                    {"actual_native_jobs",scalarJobs(*scheduler_)}};
        save("case-failed.json");
      } catch (...) {}
      throw;
    }
  }
private:
  Window &window_;
  std::shared_ptr<JobScheduler> scheduler_;
  std::filesystem::path directory_,preferencesPath_;
  std::shared_ptr<imagejxx::Document> owner_;
  std::optional<DocumentSnapshot> original_; // Committed input, never a result alias.
  HeadlessInputSession *session_=nullptr;
  Json report_,last_;
  std::map<std::string,Json> rawDescriptors_;
  std::size_t frameOrdinal_=0,rawOrdinal_=0;
  std::chrono::steady_clock::time_point deadline_;
  bool cleaned_=false;

  void writeMember(const std::string &name,const Json &value) const {
    const auto path=directory_/name;
    require(!std::filesystem::exists(path),"R16 JSON observation member already exists");
    writeJson(path.u8string(),value);
  }
  void save(const std::string &name) const { writeMember(name,report_); }
  Json rawMember(const std::string &name,const std::vector<unsigned char> &bytes,
                 const Json &shape,const std::string &dtype) {
    const auto path=directory_/name;
    require(!std::filesystem::exists(path),"R16 raw observation member already exists");
    writeBytes(path.u8string(),bytes);
    std::size_t count=1;
    for (const auto &axis:shape) count*=axis.get<std::size_t>();
    return {{"member_id",name},{"path",path.u8string()},
        {"encoding",dtype=="float32"?"IEEE754 Float32LE":"UInt8"},
        {"dtype",dtype},{"shape",shape},{"sample_count",count},
        {"byte_count",bytes.size()},{"sha256",sha256(bytes)}};
  }
  Json rawImage(const std::string &name,const Image &image) {
    require(image.type==PixelType::Float32 && image.width==width && image.height==height &&
                image.channels==1,"R16 scientific image type/shape changed");
    return rawMember(name,rawFloat32(image.pixels),{height,width},"float32");
  }
  void compactReadback(Json &image) {
    if (image.is_null()) return;
    const auto shape=image.at("shape");
    const auto dtype=image.at("dtype").get<std::string>();
    const auto &values=image.at("pixels");
    std::vector<unsigned char> bytes;
    if (dtype=="float32") bytes=rawFloat32(values.get<std::vector<double>>());
    else {
      require(dtype=="uint8","R16 readback has unsupported native dtype");
      bytes.reserve(values.size());
      for (const auto &item:values) {
        const auto value=item.get<double>();
        require(std::isfinite(value) && value>=0 && value<=255 && std::floor(value)==value,
                "R16 rendered CPU cache contains a noncanonical UInt8 sample");
        bytes.push_back(static_cast<unsigned char>(value));
      }
    }
    const auto digest=sha256(bytes);
    const auto cacheKey=dtype+shape.dump()+digest;
    auto found=rawDescriptors_.find(cacheKey);
    if (found==rawDescriptors_.end()) {
      const auto name="cache-"+std::to_string(++rawOrdinal_)+(dtype=="float32"?".f32le":".u8");
      found=rawDescriptors_.emplace(cacheKey,rawMember(name,bytes,shape,dtype)).first;
    }
    image.erase("pixels"); image["raw"]=found->second;
    image["observation_scope"]="actual CPU upload-input/software cache; no GPU readback";
  }
  const Json &frame(const std::string &label,const Json &events=Json::array()) {
    require(session_!=nullptr,"R16 driver has no live creator-owned session");
    require(frameOrdinal_<1200 && std::chrono::steady_clock::now()<deadline_,
            "R16 actual physical-frame watchdog exceeded 30 seconds/1200 frames");
    auto rendered=session_->frame(events);
    if (rendered.contains("ordinary_pixels") && rendered["ordinary_pixels"].contains("readback")) {
      auto &readback=rendered["ordinary_pixels"]["readback"];
      compactReadback(readback["source"]); compactReadback(readback["rendered_rgb"]);
    }
    Json observed{{"ordinal",++frameOrdinal_},{"label",label},{"requested_events",events},
        {"rendered",rendered},{"usage",usageJson(scheduler_->usage())},
        {"limits",limitsJson(scheduler_->limits())}};
    writeMember("frame-"+std::to_string(frameOrdinal_)+".json",observed);
    report_["frames"].push_back(std::move(observed)); last_=std::move(rendered);
    require(last_.at("session").at("open")==true &&
                last_.at("session").at("context_id")==last_.at("session").at("current_context_id"),
            "R16 physical input lost its actual persistent ImGui context");
    require(last_.at("input_frame").at("raw_inputs")==events,
            "R16 actual input trace differs from requested physical events");
    return last_;
  }
  template<class Predicate> void await(const char *label,Predicate predicate) {
    do { frame(label); } while (!predicate());
  }
  Json control(const std::string &selector) const {
    if (selector.compare(0,9,"gaussian.")==0) {
      const auto name=selector.substr(9);
      auto value=last_.at("gaussian_panel").at("control_states").at(name);
      value["rect"]=last_.at("gaussian_panel").at("control_rects").at(name);
      value["point"]=last_.at("gaussian_panel").at("controls").at(name);
      return value;
    }
    if (selector=="preferences.memory")
      return last_.at("preferences_panel").at("fields").at("processing_memory_mb");
    if (selector.compare(0,12,"preferences.")==0)
      return last_.at("preferences_panel").at("widget_controls").at(selector.substr(12));
    return last_.at("host_controls").at(selector);
  }
  void click(const std::string &selector,bool applyFileCapture=false) {
    const auto value=control(selector);
    require(value.at("enabled")==true && value.at("id").get<std::uint64_t>()!=0,
            "R16 physical control is absent or disabled");
    const auto &rect=value.at("rect"), &point=value.at("point");
    require(rect.is_array() && rect.size()==4 && point.is_array() && point.size()==2,
            "R16 physical control has no current-frame interaction geometry");
    const double x=std::floor(point[0].get<double>()),y=std::floor(point[1].get<double>());
    require(x>=rect[0].get<double>() && x<rect[2].get<double>() &&
                y>=rect[1].get<double>() && y<rect[3].get<double>(),
            "R16 integral raster pointer lies outside the actual control");
    report_["control_observations"].push_back({{"selector",selector},{"control",value},
        {"position",{x,y}},{"before_frame_ordinal",frameOrdinal_}});
    frame(selector+".position",Json::array({{{"type","position"},{"space","screen"},{"point",{x,y}}}}));
    frame(selector+".down",Json::array({{{"type","button"},{"name","left"},{"down",true}}}));
    frame(selector+".up",Json::array({{{"type","button"},{"name","left"},{"down",false}}}));
    if (applyFileCapture) {
      // Read saved bytes immediately after the actual Apply release frame,
      // before an assertion, another input, or cleanup can change them.
      const auto file=preferencesFile();
      report_["preferences_applies"].push_back({{"label",applyLabel_},
          {"frame_ordinal",frameOrdinal_},{"immediate_file",file},
          {"settings",window_.preferencesJson()},{"limits",limitsJson(scheduler_->limits())},
          {"usage",usageJson(scheduler_->usage())},{"preferences_panel",last_.at("preferences_panel")}});
      writeMember(applyLabel_+"-immediate-apply.json",report_["preferences_applies"].back());
    }
  }
  std::string applyLabel_;
  void replaceText(const std::string &selector,const std::string &text) {
    click(selector);
    const auto id=control(selector).at("id");
    require(last_.at("input_frame").at("focus").at("active_id")==id &&
                last_.at("input_frame").at("input_text").at("id")==id,
            "R16 text replacement did not focus the actual editable item");
    frame(selector+".select_all.down",Json::array({{{"type","keys"},{"keys",
        Json::array({{{"name","Ctrl"},{"down",true}},{{"name","A"},{"down",true}}})}}}));
    frame(selector+".select_all.up",Json::array({{{"type","keys"},{"keys",
        Json::array({{{"name","Ctrl"},{"down",false}},{{"name","A"},{"down",false}}})}}}));
    frame(selector+".text",Json::array({{{"type","text"},{"value",text}}}));
    frame(selector+".text_settled");
  }
  void openGaussian() {
    click("menu.Process"); frame("Process_popup_visible");
    click("menu.Process.Filters"); frame("Filters_popup_visible");
    click("menu.command.gaussian_blur_imagej"); frame("Gaussian_initial_focus");
    require(last_.at("gaussian_panel").at("open")==true,
            "R16 physical Gaussian menu did not open the actual dialog");
  }
  void openPreferences() {
    click("menu.Edit"); frame("Edit_popup_visible");
    click("menu.Edit.Preferences"); frame("Preferences_initial_focus");
    require(last_.at("preferences_panel").at("open")==true,
            "R16 physical Edit/Preferences menu did not open the actual panel");
    replaceText("preferences.search","processing_memory_mb");
    require(last_.at("preferences_panel").at("search")=="processing_memory_mb" &&
                last_.at("preferences_panel").at("fields").contains("processing_memory_mb"),
            "R16 actual Preferences Search did not reveal the memory field");
  }
  void editMemory(std::size_t value) {
    replaceText("preferences.memory",std::to_string(value));
    require(control("preferences.memory").at("draft")==value,
            "R16 physical integer text did not change the real Preferences draft");
  }
  void applyPreferences(const char *label) {
    applyLabel_=label; click("preferences.apply",true);
  }
  void closePreferences() {
    click("preferences.close"); frame("Preferences_standard_Close_settled");
    require(last_.at("preferences_panel").at("open")==false,
            "R16 actual standard Preferences Close did not dismiss the panel");
  }
  Json preferencesFile() const {
    require(!preferencesPath_.empty(),"R16 actual Preferences filepath is not observed");
    const auto bytes=readBytes(preferencesPath_.u8string());
    const std::string text(bytes.begin(),bytes.end());
    return {{"path",preferencesPath_.u8string()},{"bytes_utf8",text},
            {"envelope",Json::parse(text)},{"sha256",sha256(bytes)}};
  }
  Json nativeState(const std::string &member) {
    const auto source=owner_->snapshot(); const auto &data=source.data();
    const auto revisions=source.revisions();
    const auto history=owner_->history();
    // This is the committed source only. No preview_snapshot()/prepared()
    // export is made by this observer, even transiently.
    const auto plane=data.dataset.plane({0,0,0});
    Json rois=Json::array(),overlays=Json::array();
    for (const auto &roi:data.rois)
      rois.push_back({{"name",roi.name},{"selection",selectionJson(roi.selection)},
          {"attributes",metadataJson(roi.attributes)},
          {"t",roi.t?Json(*roi.t):Json()},{"c",roi.c?Json(*roi.c):Json()},
          {"z",roi.z?Json(*roi.z):Json()}});
    for (const auto &overlay:data.overlays) overlays.push_back(metadataJson(overlay));
    return {{"document_id",source.id()},
        {"data_identity",source.data_identity()?Json(*source.data_identity()):Json()},
        {"dirty",source.dirty()},{"undo_steps",owner_->undo_steps()},{"redo_steps",owner_->redo_steps()},
        {"history",{{"undo_steps",history.undo_steps},{"redo_steps",history.redo_steps},
            {"step_limit",history.step_limit},{"pixel_budget",history.pixel_budget},
            {"pixels",{{"logical_bytes",history.pixels.logical_bytes},{"current_bytes",history.pixels.current_bytes},
                {"retained_bytes",history.pixels.retained_bytes},{"obsolete_bytes",history.pixels.obsolete_bytes},
                {"retained_planes",history.pixels.retained_planes},{"exact",history.pixels.exact}}}}},
        {"revisions",{{"pixels",revisions.pixels},{"metadata",revisions.metadata},
                      {"selection",revisions.selection},{"display",revisions.display}}},
        {"calibration",calibrationJson(data.dataset.calibration())},
        {"selection",data.selection?selectionJson(*data.selection):Json()},
        {"previous_selection",data.previous_selection?selectionJson(*data.previous_selection):Json()},
        {"spectrum_present",data.spectrum.has_value()},{"rois",rois},{"overlays",overlays},
        {"metadata",{{"title",data.metadata.title},{"properties",metadataJson(data.metadata.properties)},
                     {"extensions",metadataJson(data.metadata.extensions)},
                     {"record_layout",metadataJson(data.metadata.record_layout)}}},
        {"display",displayJson(source.display())},{"document_info",window_.documentInfo()},
        {"workflow",window_.workflow},{"results",window_.results},{"recording",window_.recording},
        {"source_image_address",address(plane.get())},{"source_buffer_address",address(plane->pixels.data())},
        {"source_raw",rawImage(member,*plane)}};
  }
  Json checkpoint(const char *name) {
    Json value{{"name",name},{"frame_ordinal",frameOrdinal_},{"rendered",last_},
        {"native_state",nativeState(std::string(name)+"-source.f32le")},
        {"usage",usageJson(scheduler_->usage())},{"limits",limitsJson(scheduler_->limits())},
        {"settings",window_.preferencesJson()},{"preferences_file",preferencesFile()},
        {"actual_native_jobs",scalarJobs(*scheduler_)}};
    writeMember(std::string("checkpoint-")+name+".json",value);
    report_["checkpoints"].push_back(value);
    return value;
  }
  void verifyUnchanged(const Json &observed) const {
    const auto &before=report_.at("before");
    for (const auto &field:before.items()) {
      if (field.key()=="source_raw") continue;
      require(observed.at(field.key())==field.value(),"R16 changed committed document or retained records");
    }
    require(observed.at("source_raw").at("sha256")==before.at("source_raw").at("sha256"),
            "R16 changed committed scientific source bytes");
  }
  void verifyHeld(const Json &ready) const {
    verifyUnchanged(ready.at("native_state"));
    const auto &rendered=ready.at("rendered"), &jobs=rendered.at("gaussian_jobs");
    const auto &storage=jobs.at("installed_preview_storage");
    const auto &raw=rendered.at("ordinary_pixels").at("readback").at("source").at("raw");
    require(raw.at("sha256")==report_.at("owned_primitive_raw").at("sha256") &&
                raw.at("sha256")!=report_.at("before").at("source_raw").at("sha256") &&
                ready.at("usage").at("running_bytes")==0 && ready.at("usage").at("running")==0 &&
                ready.at("usage").at("retained_bytes")==ready.at("usage").at("reserved_bytes") &&
                jobs.at("preview").at("job").at("state")=="ready" &&
                storage.is_object() && storage.at("eager")==true &&
                storage.at("native_sample_storage")=="double" &&
                storage.at("native_sample_bytes")==sizeof(double) &&
                storage.at("pixel_read_policy")=="no_pixel_value_or_virtual_plane_reads" &&
                storage.at("job_id")==jobs.at("preview").at("job").at("id") &&
                storage.at("request_generation")==jobs.at("active_installed_generation") &&
                !storage.at("guard_addresses").empty(),
            "R16 held changed Preview/storage/lease does not match real READY work");
    std::set<std::string> buffers,guards;
    std::size_t payload=0,capacity=0;
    for (const auto &plane:storage.at("planes")) {
      require(plane.at("guard_tag_present")==true &&
                  plane.at("width")==width && plane.at("height")==height && plane.at("channels")==1 &&
                  plane.at("buffer_address")!=ready.at("native_state").at("source_buffer_address") &&
                  plane.at("pixel_payload_bytes").get<std::size_t>()==
                      plane.at("pixel_count").get<std::size_t>()*sizeof(double) &&
                  plane.at("pixel_capacity_bytes").get<std::size_t>()==
                      plane.at("pixel_capacity").get<std::size_t>()*sizeof(double),
              "R16 actual storage bytes do not describe its observed native buffers");
      guards.insert(plane.at("guard_address").get<std::string>());
      if (buffers.insert(plane.at("buffer_address").get<std::string>()).second) {
        payload+=plane.at("pixel_payload_bytes").get<std::size_t>();
        capacity+=plane.at("pixel_capacity_bytes").get<std::size_t>();
      }
    }
    const auto reportedGuards=storage.at("guard_addresses").get<std::vector<std::string>>();
    require(payload==storage.at("pixel_payload_bytes").get<std::size_t>() &&
                capacity==storage.at("pixel_capacity_bytes").get<std::size_t>() && capacity>=payload &&
                buffers.size()==storage.at("unique_buffer_count").get<std::size_t>() &&
                guards==std::set<std::string>(reportedGuards.begin(),reportedGuards.end()) &&
                payload!=raw.at("byte_count").get<std::size_t>(),
            "R16 measured storage is a declared dtype-byte fiction or has inconsistent aliases");
  }
  void verifyRejected(const Json &ready,const Json &rejected) const {
    verifyUnchanged(rejected.at("native_state"));
    require(rejected.at("settings")==ready.at("settings") &&
                rejected.at("limits")==ready.at("limits") &&
                rejected.at("usage")==ready.at("usage") &&
                rejected.at("preferences_file")==ready.at("preferences_file") &&
                rejected.at("rendered").at("gaussian_jobs").at("installed_preview_storage")==
                    ready.at("rendered").at("gaussian_jobs").at("installed_preview_storage") &&
                rejected.at("rendered").at("ordinary_pixels").at("pixels_source")=="preview" &&
                rejected.at("rendered").at("ordinary_pixels").at("preview_generation")==
                    ready.at("rendered").at("ordinary_pixels").at("preview_generation") &&
                rejected.at("rendered").at("ordinary_pixels").at("readback").at("source").at("raw").at("sha256")==
                    ready.at("rendered").at("ordinary_pixels").at("readback").at("source").at("raw").at("sha256") &&
                !rejected.at("rendered").at("preferences_panel").at("error").get<std::string>().empty(),
            "R16 held-lease lower-limit rejection silently evicted or changed settings/file/source/resources");
  }
  void cleanupJobs() {
    report_["api_inputs"].push_back({{"purpose","bounded final/failure cleanup only"},
        {"methods",{"Window.cancel","action(wait_jobs,timeout_ms=5000)","Window.poll"}}});
    window_.cancel();
    window_.action("wait_jobs",{{"timeout_ms",5000}});
    window_.poll();
    cleaned_=true;
  }
};
} // namespace

Json Window::gaussianResourceCase(const std::string &context,const std::string &directory) {
  checkThread();
  if (context!="imgui_cpp" && context!="imgui_python")
    throw std::invalid_argument("R16 context must be imgui_cpp or imgui_python");
  require(documents_.empty() && workflow.empty() && results.empty() && ImGui::GetCurrentContext()==nullptr,
          "R16 case requires a fresh Window without a current ImGui context");
  ResourceDriver driver(*this,controller_->job_scheduler(),context,freshDirectory(directory));
  return driver.run();
}
int Window::gaussianResourceConsumerTest(const std::string &directory) {
  checkThread();
  require(documents_.empty() && ImGui::GetCurrentContext()==nullptr,
          "R16 standalone requires a fresh Window");
  const auto root=freshDirectory(directory),caseDirectory=root/"R16";
  Json report{{"schema_version",1},{"context","imgui_cpp"},{"case_id",caseId},
      {"status","unverified"},{"acceptance_claim",false},{"cases",Json::array()}};
  try {
    Window window((caseDirectory/"preferences.json").u8string());
    report["cases"].push_back(window.gaussianResourceCase("imgui_cpp",caseDirectory.u8string()));
    report["status"]="passed";
    writeJson((root/"imgui-cpp.json").u8string(),report);
    std::cout<<"PASS "<<caseId<<" imgui_cpp\n";
    return 0;
  } catch (const std::exception &error) {
    report["status"]="failed"; report["failure"]={{"message",error.what()}};
    writeJson((root/"imgui-cpp-failed.json").u8string(),report);
    throw;
  }
}
} // namespace imagejxx::desktop
