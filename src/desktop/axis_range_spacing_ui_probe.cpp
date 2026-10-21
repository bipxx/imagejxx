// Diagnostic observation producer only. The literal sources and physical input
// requests below are inputs. Scientific, admission and publication validation
// belongs to the independent reader; this file contains no expected pixels.
#include "headless_session_internal.hpp"
#include "gaussian_cancellation_internal.hpp"
#include "window.hpp"
#include <imgui.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <map>
#include <set>
#include <sstream>
#include <stdexcept>

namespace imagejxx::desktop {
namespace {
using Clock = std::chrono::steady_clock;
constexpr std::size_t maxFrames = 160, maxFileBytes = 64u * 1024u * 1024u;
constexpr std::uint64_t maxTreeBytes = 2ull * 1024ull * 1024ull * 1024ull;
constexpr double maxSeconds = 15., aggregateSeconds = 180.;
constexpr const char *contract = "p05i.axis-range-spacing-ui.001";
struct LiteralCase {
  const char *id, *profile;
  int z, y, x;
  bool tailCalibration;
  const char *plane;
  int first, last, step;
  const char *spacing, *sampling;
  bool reverse, flip, transpose;
  const char *scenario;
  int memoryMiB;
};
// Form coordinates are deliberately literal, inclusive and one based.
constexpr std::array<LiteralCase, 20> cases{{
  {"S01","u16",9,17,32,false,"XZ",2,16,3,"2","Nearest",false,false,false,"submit",64},
  {"S02","u16",9,17,32,true, "YZ",2,31,3,"7.5","Linear",true,true,true,"submit",64},
  {"S03","rgb_u8",9,17,32,false,"XZ",2,16,3,"2","Linear",false,false,false,"submit",64},
  {"S04","rgba_f32",9,17,32,false,"YZ",2,31,3,"2","Nearest",false,false,false,"submit",64},
  {"S05","quiet_f32",9,17,32,false,"XZ",3,3,1,"2","Nearest",false,false,false,"submit",64},
  {"S06","u16",9,17,32,false,"XZ",2,16,3,"4","Nearest",true,false,false,"submit",64},
  {"S07","rgba_f32",9,17,32,true,"YZ",2,31,3,"7","Linear",false,false,false,"choice_changes",64},
  {"S08","u16",9,17,32,false,"XZ",2,16,3,"2","Linear",false,false,false,"cancel",64},
  {"S09","u16",9,17,32,false,"XZ",2,16,3,"2","Nearest",false,false,false,"focused_escape",64},
  {"S10","u16",9,17,32,false,"XZ",2,16,3,"0","Nearest",false,false,false,"submit_cleanup",64},
  {"S11","u16",9,17,32,false,"XZ",2,16,3,"-1","Nearest",false,false,false,"submit_cleanup",64},
  {"S12","u16",9,17,32,false,"XZ",2,16,3,"9.0949470177292824e-13","Nearest",false,false,false,"submit_cleanup",64},
  {"S13","u16",9,17,32,false,"XZ",2,16,3,"1000000000","Nearest",false,true,true,"submit",64},
  {"S14","u16",9,17,32,false,"XZ",0,16,3,"2","Nearest",false,false,false,"submit_cleanup",64},
  {"S15","u8",257,33,257,false,"XZ",1,33,1,"4","Nearest",false,false,false,"submit_cleanup",1},
  {"S16","u16",3,2,4097,false,"XZ",1,2,1,"4","Nearest",false,false,false,"submit_cleanup",64},
  {"S17","u16",9,17,32,false,"XZ",2,16,3,"2","Nearest",false,false,false,"stale_pixels",64},
  {"S18","u16",9,17,32,false,"XZ",2,16,3,"2","Nearest",false,false,false,"stale_calibration",64},
  {"S19","u16",9,17,32,false,"XZ",2,16,3,"2","Nearest",false,false,false,"closed_source",64},
  {"S20","u16",9,17,32,false,"XZ",2,16,3,"2","Nearest",false,false,false,"active_other",64}
}};

void require(bool value, const char *message) {
  if (!value) throw std::runtime_error(message);
}
double elapsed(Clock::time_point start) {
  return std::chrono::duration<double>(Clock::now() - start).count();
}
Json exceptionJson(const std::exception &error) {
  const char *type = dynamic_cast<const RevisionConflict *>(&error) ? "revision_conflict"
      : dynamic_cast<const DocumentUnavailable *>(&error) ? "document_unavailable"
      : dynamic_cast<const std::out_of_range *>(&error) ? "out_of_range"
      : dynamic_cast<const std::invalid_argument *>(&error) ? "invalid_argument" : "exception";
  return {{"type",type},{"message",error.what()}};
}
std::filesystem::path freshDirectory(const std::string &directory) {
  require(!directory.empty(),"Physical-spacing probe needs an output directory");
  const auto path = std::filesystem::absolute(std::filesystem::u8path(directory)).lexically_normal();
  require(path != path.root_path(),"Physical-spacing probe cannot own a filesystem root");
  std::filesystem::create_directories(path.parent_path());
  require(std::filesystem::create_directory(path),"Physical-spacing probe directory must be fresh");
  return path;
}
const LiteralCase &findCase(const std::string &id) {
  for (const auto &value : cases) if (id == value.id) return value;
  throw std::invalid_argument("Physical-spacing UI case must be S01..S20");
}
// SHA256 is an identity of observed bytes only, never a scientific oracle.
std::string sha256(const std::vector<unsigned char> &input) {
  constexpr std::array<std::uint32_t,64> k{{
    0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,
    0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
    0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
    0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
    0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,
    0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
    0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,
    0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2}};
  std::array<std::uint32_t,8> hash{{
    0x6a09e667,0xbb67ae85,0x3c6ef372,0xa54ff53a,0x510e527f,0x9b05688c,0x1f83d9ab,0x5be0cd19}};
  auto bytes = input; bytes.push_back(0x80);
  while (bytes.size() % 64 != 56) bytes.push_back(0);
  const auto bits = static_cast<std::uint64_t>(input.size()) * 8;
  for (int shift = 56; shift >= 0; shift -= 8) bytes.push_back(static_cast<unsigned char>(bits >> shift));
  const auto rotate = [](std::uint32_t value,int shift) {return (value >> shift) | (value << (32-shift));};
  for (std::size_t offset = 0; offset < bytes.size(); offset += 64) {
    std::array<std::uint32_t,64> words{};
    for (std::size_t i = 0; i < 16; ++i) for (std::size_t j = 0; j < 4; ++j)
      words[i] = (words[i] << 8) | bytes[offset + 4*i + j];
    for (std::size_t i = 16; i < 64; ++i) {
      const auto a = words[i-15], b = words[i-2];
      words[i] = words[i-16] + (rotate(a,7)^rotate(a,18)^(a>>3)) +
          words[i-7] + (rotate(b,17)^rotate(b,19)^(b>>10));
    }
    auto work = hash;
    for (std::size_t i = 0; i < 64; ++i) {
      const auto first = work[7]+(rotate(work[4],6)^rotate(work[4],11)^rotate(work[4],25))+
          ((work[4]&work[5])^(~work[4]&work[6]))+k[i]+words[i];
      const auto second = (rotate(work[0],2)^rotate(work[0],13)^rotate(work[0],22))+
          ((work[0]&work[1])^(work[0]&work[2])^(work[1]&work[2]));
      for (std::size_t j = 7; j > 0; --j) work[j] = work[j-1];
      work[4] += first; work[0] = first + second;
    }
    for (std::size_t i = 0; i < 8; ++i) hash[i] += work[i];
  }
  std::ostringstream result; result << std::hex << std::setfill('0');
  for (const auto value : hash) result << std::setw(8) << value;
  return result.str();
}
const char *typeName(PixelType type) {
  switch (type) {
  case PixelType::UInt8:return "UInt8"; case PixelType::UInt16:return "UInt16";
  case PixelType::Float32:return "Float32"; case PixelType::Float64:return "Float64";
  }
  throw std::invalid_argument("Unknown observed pixel type");
}
const char *dtypeName(PixelType type) {
  switch (type) {
  case PixelType::UInt8:return "uint8"; case PixelType::UInt16:return "uint16";
  case PixelType::Float32:return "float32"; case PixelType::Float64:return "float64";
  }
  throw std::invalid_argument("Unknown observed dtype");
}
Json revisionsJson(const Revisions &value) {
  return {{"pixels",value.pixels},{"metadata",value.metadata},{"selection",value.selection},{"display",value.display}};
}
Json calibrationJson(const SpatialCalibration &value) {
  return {{"spacing",value.spacing},{"origin",value.origin},{"direction",value.direction},{"unit",value.unit},
      {"time_unit",value.time_unit},{"frame_interval",value.frame_interval},{"time_origin",value.time_origin}};
}
Json displayJson(const DisplayState &value) {
  return {{"t",value.t},{"c",value.c},{"crosshair",value.crosshair},{"orthogonal",value.orthogonal},
      {"linked_zoom",value.linkedZoom},{"zoom",value.zoom},{"plane_zoom",value.planeZoom},{"pan",value.pan},
      {"properties",metadataJson(value.properties)},{"revision",value.revision}};
}
Json usageJson(const JobUsage &value) {
  return {{"running",value.running},{"queued",value.queued},{"running_bytes",value.running_bytes},
      {"retained_bytes",value.retained_bytes},{"reserved_bytes",value.reserved_bytes}};
}
Json selectionJson(const std::optional<Selection> &value) {
  if (!value) return nullptr;
  return {{"kind",value->kind},{"points",value->points},{"width",value->width},
      {"attributes",metadataJson(value->attributes)},{"mask_width",value->mask_width()},
      {"mask_height",value->mask_height()},
      {"packed_mask",value->kind == "mask" ? Json(value->packed_mask()) : Json::array()}};
}
SpatialCalibration inputCalibration(const LiteralCase &spec) {
  SpatialCalibration value;
  value.spacing = {.5,1.25,2.}; value.origin = {11.,-7.,19.};
  value.direction = {-1.,.25,spec.tailCalibration ? 1.5 : 0.,.5,1.,0.,0.,.75,2.};
  value.unit = "um"; value.time_unit = "s"; value.frame_interval = .75; value.time_origin = 2.25;
  return value;
}
PixelType profileType(const std::string &profile) {
  if (profile == "u16") return PixelType::UInt16;
  if (profile == "rgba_f32" || profile == "quiet_f32") return PixelType::Float32;
  return PixelType::UInt8;
}
Dataset inputDataset(const LiteralCase &spec,bool other) {
  const std::string profile = spec.profile;
  const int samples = profile == "rgb_u8" ? 3 : profile == "rgba_f32" ? 4 : 1;
  Stack stack;
  for (int z = 0; z < spec.z; ++z) {
    Image image(spec.x,spec.y,samples,profileType(profile));
    for (int y = 0; y < spec.y; ++y) for (int x = 0; x < spec.x; ++x)
      for (int sample = 0; sample < samples; ++sample) {
        double value = 187.;
        if (!other) {
          if (profile == "u8") value = (37*x+61*y+13*x*y+43*z)%251;
          else if (profile == "u16") value = 6000*z+32*y+x;
          else if (profile == "rgb_u8" || profile == "rgba_f32") {
            const std::array<int,4> channels{{(17*x+31*y+47*z)%256,(71*x+11*y+29*z)%256,
                (13*x+61*y+23*z)%256,(19*x+53*y+7*z+127)%256}};
            value = channels[sample] + (profile == "rgba_f32" ? .25 : 0.);
          } else {
            value = 10000*z+100*y+x+.25;
            if (z == 0 && y == 2 && x == 3)
              value = float32_source_sample(0x7fc12345u,FloatingSamplePolicy::PreserveQuietNaNFloat32);
            if (z == 2 && y == 2 && x == 5) value = 0.;
            if (z == 4 && y == 2 && x == 5) value = -0.;
          }
        }
        image.at(x,y,sample) = value;
      }
    stack.push_back(std::move(image));
  }
  return Dataset::from_stack(std::move(stack),inputCalibration(spec),profile == "quiet_f32"
      ? FloatingSamplePolicy::PreserveQuietNaNFloat32 : FloatingSamplePolicy::FiniteOnly);
}
Selection taggedSelection(const char *kind,std::vector<std::array<double,2>> points,const char *tag) {
  Selection value(kind,std::move(points),1);
  value.attributes = metadataObjectFromJson({{"tag",tag}});
  return value;
}
DocumentData inputData(const LiteralCase &spec,bool other = false) {
  DocumentData value{inputDataset(spec,other)};
  value.metadata.title = other ? "Other" : "Physical Spacing Source";
  if (other) return value;
  value.metadata.properties = metadataObjectFromJson({{"source_only","retained property"}});
  value.metadata.extensions = metadataObjectFromJson({{"source_extension",{{"retained",true}}}});
  // All source geometries are large enough for these finite annotations.
  value.selection = taggedSelection("rectangle",{{0,0},{1,1}},"transient");
  value.previous_selection = taggedSelection("ellipse",{{0,0},{1,1}},"previous");
  value.rois.push_back({"Saved rectangle",taggedSelection("rectangle",{{0,0},{1,1}},"saved_first"),
      {},{},0,metadataObjectFromJson({{"source_only","first"}})});
  value.rois.push_back({"Saved second",taggedSelection("ellipse",{{0,0},{1,1}},"saved_second"),
      {},{},1,metadataObjectFromJson({{"source_only","second"}})});
  value.overlays.push_back(metadataObjectFromJson({{"source_overlay","retained"}}));
  return value;
}
Json literalInput(const LiteralCase &spec) {
  return {{"source",{{"profile",spec.profile},{"shape",Json::array({1,1,spec.z,spec.y,spec.x,
            std::string(spec.profile) == "rgb_u8" ? 3 : std::string(spec.profile) == "rgba_f32" ? 4 : 1})},
          {"axes","TCZYXS"},{"calibration",calibrationJson(inputCalibration(spec))}}},
      {"form_1based",{{"plane",spec.plane},{"first",spec.first},{"last",spec.last},{"step",spec.step},
          {"physical_vertical_spacing_text",spec.spacing},{"sampling",spec.sampling},
          {"reverse_order",spec.reverse},{"flip_vertical",spec.flip},{"transpose",spec.transpose}}},
      {"scenario",spec.scenario},{"processing_memory_mb",spec.memoryMiB}};
}
Json simultaneousKeys(bool down) {
  return {{"type","keys"},{"keys",Json::array({
      Json{{"name","Ctrl"},{"down",down}},Json{{"name","A"},{"down",down}}})}};
}
void littleEndian(std::vector<unsigned char> &bytes,std::uint64_t bits,int count) {
  for (int i = 0; i < count; ++i) bytes.push_back(static_cast<unsigned char>(bits >> (8*i)));
}
std::vector<unsigned char> rawPixels(const Dataset &dataset,bool storage) {
  const auto shape = dataset.shape(); const auto type = dataset.pixel_type();
  const int width = storage || type == PixelType::Float64 ? 8 :
      type == PixelType::Float32 ? 4 : type == PixelType::UInt16 ? 2 : 1;
  require(shape.pixel_count() <= maxFileBytes/std::size_t(width),"Physical-spacing raw64MiB cap");
  std::vector<unsigned char> bytes; bytes.reserve(shape.pixel_count()*width);
  for (int t = 0; t < shape.t; ++t) for (int c = 0; c < shape.c; ++c)
    for (int z = 0; z < shape.z; ++z) for (const double value : dataset.plane({t,c,z})->pixels) {
      if (storage || type == PixelType::Float64) {
        std::uint64_t bits;
        static_assert(sizeof(bits) == sizeof(value),"Storage observation requires IEEE64 width");
        std::memcpy(&bits,&value,sizeof(bits)); littleEndian(bytes,bits,8);
      } else if (type == PixelType::Float32) {
        littleEndian(bytes,float32_source_sample_bits(value,dataset.sample_policy()),4);
      } else {
        require(std::isfinite(value) && value >= 0 && value <= type_max(type) && value == std::floor(value),
            "Observed integer sample is not exactly typed");
        littleEndian(bytes,static_cast<std::uint64_t>(value),width);
      }
    }
  return bytes;
}

class AxisRangeSpacingUiDriver {
public:
  AxisRangeSpacingUiDriver(Window &window,const LiteralCase &spec,std::string context,
                           std::filesystem::path directory,std::uint64_t *launchBytes = nullptr)
      : window_(window),spec_(spec),directory_(std::move(directory)),start_(Clock::now()),
        source_(std::make_shared<imagejxx::Document>(inputData(spec))),launchBytes_(launchBytes) {
    report_ = {{"schema_version",1},{"contract_id",contract},{"case_id",spec_.id},{"context",std::move(context)},
        {"literal_input",literalInput(spec_)},{"profile",spec_.profile},{"acceptance_claim",false},{"observation_state","observing"},
        {"limits",{{"max_frames",maxFrames},{"max_seconds",maxSeconds},{"max_file_bytes",maxFileBytes},
            {"max_tree_bytes",maxTreeBytes},{"max_world_observations_per_document",12}}},
        {"frames",Json::array()},{"control_observations",Json::array()},{"lifecycle",Json::array()},
        {"core_stimuli",Json::array()},{"submission_observations",Json::array()},
        {"states",Json::object()},{"raw_members",Json::array()}};
    window_.updateSettings({{"default_zoom",1.25},{"history_limit",5},{"history_memory_mb",8},
        {"processing_memory_mb",spec_.memoryMiB}},false);
    window_.addNativeDocument(source_); source_->set_history_policy(3,4096);
    auto edit = window_.beginEdit(source_->id);
    edit.set_property("revision_probe",7); window_.commitEdit(edit);
    auto display = source_->snapshot().display();
    display.crosshair = {std::min(7,spec_.x-1),std::min(5,spec_.y-1),std::min(4,spec_.z-1)};
    display.orthogonal = false; display.linkedZoom = true; display.zoom = 2.;
    display.planeZoom = {}; display.pan = {};
    display.properties = metadataObjectFromJson({{"display_range",{0,255}},{"lut","Ice"}});
    window_.setNativeDisplay(display,source_->id);
    if (std::string(spec_.scenario) == "active_other" || std::string(spec_.scenario) == "closed_source") {
      other_ = std::make_shared<imagejxx::Document>(inputData(spec_,true));
      window_.addNativeDocument(other_); window_.setActiveIndex(0);
    }
    captured_ = source_->snapshot();
    report_["captured_source"] = summary(source_,*captured_,true,false);
    report_["source_before"] = summary(source_,source_->snapshot(),true,false);
    report_["source_before_raw"] = report_["source_before"]["raw_member"];
    report_["source_before_stored_raw"] = report_["source_before"]["stored_raw_member"];
    capture("source_ready");
  }
  Json run() {
    // This stack-owned session remains alive for every observed input frame.
    // No context/session ownership escapes into a Python garbage collector.
    HeadlessInputSession session(window_);
    try {
      report_["lifecycle"].push_back({{"label","opened"},{"observed",session.lifecycle()}});
      frame(session,"warmup.1"); frame(session,"warmup.2"); openDialog(session);
      if (std::string(spec_.scenario) == "choice_changes") choiceChanges(session);
      else configure(session);
      const std::string scenario = spec_.scenario;
      if (scenario == "cancel") cancel(session,"requested_cancel");
      else if (scenario == "focused_escape") focusedEscape(session);
      else {
        if (scenario == "stale_pixels") stalePixels(session);
        if (scenario == "stale_calibration") staleCalibration(session);
        if (scenario == "closed_source") closeSource(session);
        if (scenario == "active_other") activateOther(session);
        submit(session);
        if (scenario == "submit_cleanup" || scenario == "stale_pixels" ||
            scenario == "stale_calibration" || scenario == "closed_source")
          cleanupDialog(session);
      }
      checkpoint(session,"final_idle"); session.close();
      report_["lifecycle"].push_back({{"label","closed"},{"observed",session.lifecycle()}});
      report_["source_after"] = summary(source_,source_->snapshot(),true,false);
      report_["source_after_raw"] = report_["source_after"]["raw_member"];
      report_["source_after_stored_raw"] = report_["source_after"]["stored_raw_member"];
      report_["captured_source_after"] = summary(source_,*captured_,true,false);
      report_["outputs"] = Json::array();
      for (const auto &owner : outputs_) report_["outputs"].push_back(summary(owner,owner->snapshot(),true,true));
      if (outputs_.size() == 1) {
        report_["output_raw"] = report_["outputs"][0]["raw_member"];
        report_["output_stored_raw"] = report_["outputs"][0]["stored_raw_member"];
      }
      report_["final_busy"] = window_.busy(); report_["window_error"] = window_.error();
      report_["final_context"] = {{"current_context_id",static_cast<std::uint64_t>(
          reinterpret_cast<std::uintptr_t>(ImGui::GetCurrentContext()))}};
      report_["frame_count"] = frameOrdinal_; report_["observation_state"] = "observed";
      report_["artifact_tree"] = artifactTree(); report_["elapsed_seconds"] = elapsed(start_); budget();
      const auto json = report_.dump();
      writeMember("case.json",std::vector<unsigned char>(json.begin(),json.end())); budget();
      return report_;
    } catch (const std::exception &error) {
      report_["observation_state"] = "failed"; report_["failure"] = exceptionJson(error);
      report_["frame_count"] = frameOrdinal_; report_["elapsed_seconds"] = elapsed(start_);
      try {session.close();report_["lifecycle"].push_back({{"label","failure_closed"},{"observed",session.lifecycle()}});} catch (...) {}
      try {const auto json = report_.dump();writeMember("case-failed.json",std::vector<unsigned char>(json.begin(),json.end()));} catch (...) {}
      throw;
    }
  }
private:
  Window &window_;
  const LiteralCase &spec_;
  std::filesystem::path directory_;
  Clock::time_point start_;
  std::shared_ptr<imagejxx::Document> source_,other_;
  std::optional<DocumentSnapshot> captured_;
  std::vector<std::shared_ptr<imagejxx::Document>> outputs_;
  std::map<std::pair<std::uint64_t,std::uint64_t>,std::array<Json,2>> rawCache_;
  Json report_,last_;
  std::size_t frameOrdinal_ = 0, rawOrdinal_ = 0;
  std::uint64_t treeBytes_ = 0;
  std::uint64_t *launchBytes_ = nullptr;
  void budget() const {
    require(frameOrdinal_ <= maxFrames && elapsed(start_) <= maxSeconds,
        "Physical-spacing UI case exceeded its declared frame/time limit");
  }
  void writeMember(const std::string &name,const std::vector<unsigned char> &bytes) {
    require(!bytes.empty() && bytes.size() <= maxFileBytes,"Physical-spacing artifact64MiB cap");
    require(bytes.size() <= maxTreeBytes-treeBytes_,"Physical-spacing artifact2GiB cap");
    if (launchBytes_)
      require(bytes.size() <= maxTreeBytes-*launchBytes_,"Physical-spacing launch artifact2GiB cap");
    const auto path = directory_/name;
    require(path.parent_path() == directory_ && !std::filesystem::exists(path),
        "Physical-spacing immutable member is not fresh");
    writeBytes(path.u8string(),bytes); treeBytes_ += bytes.size();
    if (launchBytes_) *launchBytes_ += bytes.size();
  }
  void writeJsonMember(const std::string &name,const Json &value) {
    const auto json = value.dump(2);
    writeMember(name,std::vector<unsigned char>(json.begin(),json.end()));
  }
  Json rawMember(const Dataset &dataset,bool storage) {
    const auto bytes = rawPixels(dataset,storage); const auto shape = dataset.shape();
    const auto type = storage ? PixelType::Float64 : dataset.pixel_type();
    std::ostringstream name;
    name << (storage ? "storage-f64-" : "typed-") << std::setw(4) << std::setfill('0') << ++rawOrdinal_ << ".bin";
    writeMember(name.str(),bytes);
    Json value{{"member_id",name.str()},{"path",name.str()},
        {"encoding",std::string(typeName(type)) + (type == PixelType::UInt8 ? "" : "LE")},
        {"dtype",dtypeName(type)},{"axes","TCZYXS"},{"shape",shape.dimensions()},
        {"sample_count",shape.pixel_count()},{"byte_count",bytes.size()},{"sha256",sha256(bytes)},
        {"endian","little"},{"transport",storage ? "immutable_core_float64_storage_bits" : "exact_declared_pixel_type_bits"}};
    report_["raw_members"].push_back(value); return value;
  }
  bool nativeAvailable(std::uint64_t id) const {
    try {return bool(window_.nativeDocument(id));}
    catch (const DocumentUnavailable &) {return false;}
    catch (const std::out_of_range &) {return false;}
  }
  Json summary(const std::shared_ptr<imagejxx::Document> &owner,const DocumentSnapshot &snapshot,
               bool raw,bool worlds) {
    const auto &data = snapshot.data(); const auto shape = data.dataset.shape();
    const auto position = [](const std::optional<int> &v) {return v ? Json(*v) : Json(nullptr);};
    Json rois = Json::array(), overlays = Json::array();
    for (const auto &roi : data.rois) rois.push_back({{"name",roi.name},{"t",position(roi.t)},
        {"c",position(roi.c)},{"z",position(roi.z)},{"attributes",metadataJson(roi.attributes)},
        {"selection",selectionJson(roi.selection)}});
    for (const auto &overlay : data.overlays) overlays.push_back(metadataJson(overlay));
    Json value{{"document_id",snapshot.id()},{"owner_id",owner->id},{"native_available",nativeAvailable(snapshot.id())},
        {"data_identity",snapshot.data_identity() ? Json(*snapshot.data_identity()) : Json(nullptr)},
        {"dirty",snapshot.dirty()},{"undo_steps",owner->undo_steps()},{"redo_steps",owner->redo_steps()},
        {"history_limit",owner->history_limit()},{"history_pixel_budget",owner->history_pixel_budget()},
        {"revisions",revisionsJson(snapshot.revisions())},{"selection",selectionJson(data.selection)},
        {"previous_selection",selectionJson(data.previous_selection)},{"rois",std::move(rois)},
        {"metadata",{{"title",data.metadata.title},{"properties",metadataJson(data.metadata.properties)},
            {"extensions",metadataJson(data.metadata.extensions)}}},
        {"record_layout",metadataJson(data.metadata.record_layout)},{"overlays",std::move(overlays)},
        {"spectrum",nullptr},{"display",displayJson(snapshot.display())},
        {"dataset",{{"dtype",dtypeName(data.dataset.pixel_type())},{"pixel_type",typeName(data.dataset.pixel_type())},
            {"axes","TCZYXS"},{"shape",shape.dimensions()},
            {"axis_sizes",{{"T",shape.t},{"C",shape.c},{"Z",shape.z},{"Y",shape.y},{"X",shape.x},{"S",shape.s}}},
            {"sample_policy",data.dataset.sample_policy() == FloatingSamplePolicy::FiniteOnly
                ? "FiniteOnly" : "PreserveQuietNaNFloat32"},
            {"calibration",calibrationJson(data.dataset.calibration())}}}};
    if (data.spectrum) {
      const auto &state = *data.spectrum;
      value["spectrum"] = {{"width",state.spectrum().width},{"height",state.spectrum().height},
          {"original_shape",state.original_shape() ? Json(*state.original_shape()) : Json(nullptr)}};
    }
    if (raw) {
      // The Pixels revision identifies unchanged retained storage. Metadata and
      // display snapshots share immutable byte members instead of rewriting them.
      const auto key = std::make_pair(snapshot.id(),snapshot.revisions().pixels);
      auto found = rawCache_.find(key);
      if (found == rawCache_.end()) {
        const std::array<Json,2> members{{rawMember(data.dataset,true),rawMember(data.dataset,false)}};
        found = rawCache_.emplace(key,members).first;
      }
      value["stored_raw_member"] = found->second[0]; value["raw_member"] = found->second[1];
    }
    if (worlds) {
      std::set<std::array<double,3>> voxels{{{0.,0.,0.}},{{1.,0.,0.}},{{0.,1.,0.}},{{0.,0.,1.}}};
      for (int x : {0,shape.x-1}) for (int y : {0,shape.y-1}) for (int z : {0,shape.z-1})
        voxels.insert({static_cast<double>(x),static_cast<double>(y),static_cast<double>(z)});
      Json points = Json::array();
      for (const auto &voxel : voxels)
        points.push_back({{"voxel_xyz",voxel},{"world_xyz",data.dataset.calibration().voxel_to_world(voxel)}});
      value["world_points"] = std::move(points);
      value["world_points_scope"] = "observed_affine_origin_unit_basis_and_corners";
    }
    return value;
  }
  void discoverOutputs() {
    for (const auto &facade : window_.documents()) {
      if (facade->id == source_->id || (other_ && facade->id == other_->id)) continue;
      const auto found = std::find_if(outputs_.begin(),outputs_.end(),
          [&](const auto &owner) {return owner->id == facade->id;});
      if (found == outputs_.end()) outputs_.push_back(window_.nativeDocument(facade->id));
    }
  }
  Json collection(bool raw) {
    auto &application = GaussianCancellationAccess::application(window_);
    Json ids = Json::array(), documents = Json::array(), outputs = Json::array();
    for (const auto &facade : window_.documents()) {
      ids.push_back(facade->id); documents.push_back(window_.documentInfo(facade->id));
    }
    for (const auto &owner : outputs_) outputs.push_back(summary(owner,owner->snapshot(),raw,raw));
    Json active = nullptr;
    if (window_.activeIndex() >= 0 && window_.activeIndex() < int(window_.documents().size()))
      active = window_.nativeSnapshot(window_.documents().at(std::size_t(window_.activeIndex()))->id).id();
    return {{"source",summary(source_,source_->snapshot(),raw,false)},
        {"other",other_ ? summary(other_,other_->snapshot(),raw,false) : Json(nullptr)},
        {"output",outputs_.size() == 1 ? outputs[0] : Json(nullptr)},
        {"outputs",std::move(outputs)},{"facade_ids",std::move(ids)},
        {"document_info",std::move(documents)},{"active_index",window_.activeIndex()},
        {"native_member_ids",application.document_ids()},
        {"active_document_id",application.active_document_id()},{"facade_active_document_id",active},{"busy",window_.busy()},
        {"scheduler_usage",usageJson(GaussianCancellationAccess::scheduler(window_)->usage())},
        {"workflow",window_.workflow},{"results",window_.results}};
  }
  void capture(const std::string &name) {report_["states"][name] = collection(true);}
  void frame(HeadlessInputSession &session,const std::string &label,const Json &events = Json::array()) {
    require(frameOrdinal_ < maxFrames,"Physical-spacing UI frame limit reached before input"); budget();
    last_ = session.frame(events); discoverOutputs();
    std::ostringstream name;
    name << "frame-" << std::setw(4) << std::setfill('0') << ++frameOrdinal_ << ".json";
    Json actual{{"ordinal",frameOrdinal_},{"label",label},{"member_id",name.str()},
        {"requested_events",events},{"rendered",last_},{"native_state",collection(false)}};
    writeJsonMember(name.str(),actual); report_["frames"].push_back(std::move(actual)); budget();
  }
  void checkpoint(HeadlessInputSession &session,const std::string &name) {frame(session,name);capture(name);}
  Json control(const std::string &selector) const {
    if (selector.rfind("dialog.",0) == 0) return last_.at("dialog").at("controls").at(selector.substr(7));
    if (selector.rfind("error_modal.",0) == 0) return last_.at("error_modal").at("controls").at(selector.substr(12));
    return last_.at("host_controls").at(selector);
  }
  void click(HeadlessInputSession &session,const std::string &selector) {
    const auto actual = control(selector);
    require(actual.at("enabled") == true && actual.at("id").get<std::uint64_t>() > 0,
        "Physical-spacing pointer target is not an observed eligible control");
    const auto &rect = actual.at("rect"), &point = actual.at("point");
    const double x = std::floor(point[0].get<double>()), y = std::floor(point[1].get<double>());
    require(rect.size() == 4 && x >= rect[0].get<double>() && x < rect[2].get<double>() &&
        y >= rect[1].get<double>() && y < rect[3].get<double>(),"Physical-spacing pointer is outside observed bounds");
    report_["control_observations"].push_back({{"selector",selector},{"control",actual},
        {"position",{x,y}},{"before_frame_ordinal",frameOrdinal_}});
    frame(session,selector+".position",Json::array({Json{{"type","position"},{"space","screen"},{"point",{x,y}}}}));
    frame(session,selector+".down",Json::array({Json{{"type","button"},{"name","left"},{"down",true}}}));
    frame(session,selector+".up",Json::array({Json{{"type","button"},{"name","left"},{"down",false}}}));
  }
  void openDialog(HeadlessInputSession &session) {
    click(session,"menu.Image"); frame(session,"image_menu_open");
    click(session,"menu.Image.Stacks"); frame(session,"stacks_menu_open");
    click(session,"menu.action.show_axis_range_spacing"); checkpoint(session,"dialog_opened");
  }
  void choose(HeadlessInputSession &session,const char *field,const char *value,const std::string &label) {
    click(session,std::string("dialog.")+field); frame(session,label+".combo_open");
    click(session,std::string("dialog.")+field+".choice."+value); checkpoint(session,label);
  }
  void editField(HeadlessInputSession &session,const char *field,const std::string &text,const std::string &label) {
    click(session,std::string("dialog.")+field);
    frame(session,label+".select_all.down",Json::array({simultaneousKeys(true)}));
    frame(session,label+".select_all.up",Json::array({simultaneousKeys(false)}));
    frame(session,label+".text",Json::array({Json{{"type","text"},{"value",text}}})); checkpoint(session,label);
  }
  void editRange(HeadlessInputSession &session,int first,int last,int step,const std::string &label) {
    editField(session,"first",std::to_string(first),label+".first");
    editField(session,"last",std::to_string(last),label+".last");
    editField(session,"step",std::to_string(step),label+".step"); checkpoint(session,label);
  }
  void configure(HeadlessInputSession &session) {
    choose(session,"plane",spec_.plane,"plane_selected");
    choose(session,"sampling",spec_.sampling,"sampling_selected");
    editRange(session,spec_.first,spec_.last,spec_.step,"range_edited");
    editField(session,"physical_vertical_spacing",spec_.spacing,"spacing_edited");
    setFlags(session);
    checkpoint(session,"draft_configured");
  }
  void choiceChanges(HeadlessInputSession &session) {
    editRange(session,2,16,3,"initial_xz_range");
    editField(session,"physical_vertical_spacing","7","spacing_edited");
    choose(session,"plane","YZ","yz_axis_reset.1"); choose(session,"sampling","Linear","linear_selected.1");
    choose(session,"plane","XZ","xz_axis_reset"); choose(session,"sampling","Nearest","nearest_selected");
    choose(session,"plane","YZ","yz_axis_reset.2"); choose(session,"sampling","Linear","linear_selected.2");
    editRange(session,2,31,3,"range_edited"); setFlags(session); checkpoint(session,"draft_configured");
  }
  void setFlags(HeadlessInputSession &session) {
    for (const auto &flag : std::array<std::pair<const char *,bool>,3>{{
        {"reverse_order",spec_.reverse},{"flip_vertical",spec_.flip},{"transpose",spec_.transpose}}}) {
      click(session,std::string("dialog.")+flag.first);
      if (!flag.second) click(session,std::string("dialog.")+flag.first);
      checkpoint(session,std::string("flag.")+flag.first+".set");
    }
  }
  Json draft() const {
    return {{"form_values",last_.at("dialog").at("values")},{"form_text",last_.at("dialog").at("text")},
        {"opened_document_id",last_.at("dialog").at("opened_document_id")},
        {"opened_document_id_semantics",last_.at("dialog").at("opened_document_id_semantics")},
        {"captured_document_id",captured_->id()},{"captured_revisions",revisionsJson(captured_->revisions())},
        {"spacing_preview",last_.at("dialog").at("spacing_preview")}};
  }
  void submit(HeadlessInputSession &session) {
    checkpoint(session,"before_submit"); const auto request = draft(), before = collection(true);
    click(session,"dialog.apply"); checkpoint(session,"after_submit");
    Json ids = Json::array(); for (const auto &owner : outputs_) ids.push_back(owner->id);
    report_["submission_observations"].push_back({{"draft",request},{"before",before},{"after",collection(true)},
        {"dialog_after",last_.at("dialog")},{"error_modal_after",last_.at("error_modal")},
        {"window_error",window_.error()},{"actual_output_ids",std::move(ids)},
        {"exception",nullptr},{"exception_scope","UI guard reports actual errors through modal and window_error"}});
  }
  void cancel(HeadlessInputSession &session,const std::string &label) {
    checkpoint(session,label+".before"); click(session,"dialog.cancel"); checkpoint(session,label+".after");
  }
  void cleanupDialog(HeadlessInputSession &session) {
    // Cleanup follows actual modal state. An unexpected successful output is
    // retained in the observation and cannot be converted to a rejection.
    if (last_.at("error_modal").value("open",false) || !window_.error().empty()) {
      frame(session,"error_modal.settled.1"); frame(session,"error_modal.settled.2");
      if (last_.at("error_modal").value("open",false)) {
        click(session,"error_modal.ok"); checkpoint(session,"error_acknowledged");
      }
    }
    if (last_.at("dialog").value("open",false)) cancel(session,"cleanup_cancel");
  }
  void focusedEscape(HeadlessInputSession &session) {
    click(session,"dialog.physical_vertical_spacing"); checkpoint(session,"before_focused_escape");
    frame(session,"focused_escape.down",Json::array({Json{{"type","key"},{"name","Escape"},{"down",true}}}));
    frame(session,"focused_escape.up",Json::array({Json{{"type","key"},{"name","Escape"},{"down",false}}}));
    checkpoint(session,"after_focused_escape");
    report_["escape_observation"] = {{"dialog_after",last_.at("dialog")},{"state_after",collection(true)}};
    if (last_.at("dialog").value("open",false)) cancel(session,"escape_cleanup_cancel");
  }
  template<class Function> void stimulus(const std::string &label,const char *method,Json arguments,Function operation) {
    Json value{{"ordinal",report_["core_stimuli"].size()+1},{"label",label},{"method",method},
        {"arguments",std::move(arguments)},{"classification","declared_core_state_stimulus_no_physical_credit"},
        {"before",collection(true)},{"after",nullptr},{"exception",nullptr}};
    try {operation();} catch (const std::exception &error) {value["exception"] = exceptionJson(error);}
    discoverOutputs(); value["after"] = collection(true); report_["core_stimuli"].push_back(std::move(value));
  }
  void stalePixels(HeadlessInputSession &session) {
    stimulus("pixel_edit","commitEdit.replace_plane",{{"document_id",source_->id},{"t",0},{"c",0},{"z",0},
        {"x",0},{"y",0},{"s",0},{"value",7}},[&] {
      auto plane = *source_->snapshot().data().dataset.plane({0,0,0}); plane.at(0,0) = 7;
      auto edit = window_.beginEdit(source_->id); edit.replace_plane({0,0,0},std::move(plane)); window_.commitEdit(edit);
    }); checkpoint(session,"after_pixel_edit");
  }
  void staleCalibration(HeadlessInputSession &session) {
    stimulus("calibration_edit","commitEdit.set_calibration",{{"document_id",source_->id},{"spacing",{1.,1.25,2.}}},[&] {
      auto value = source_->snapshot().data().dataset.calibration(); value.spacing[0] = 1.;
      auto edit = window_.beginEdit(source_->id); edit.set_calibration(value); window_.commitEdit(edit);
    }); checkpoint(session,"after_calibration_edit");
  }
  void closeSource(HeadlessInputSession &session) {
    stimulus("source_close","closeDocument",{{"document_id",source_->id},{"discard",true}},
        [&] {window_.closeDocument(source_->id,true);}); checkpoint(session,"after_source_close");
  }
  void activateOther(HeadlessInputSession &session) {
    stimulus("activate_other","setActiveIndex",{{"document_id",other_->id},{"index",1}},
        [&] {window_.setActiveIndex(1);}); checkpoint(session,"after_activate_other");
  }
  Json artifactTree() const {
    std::vector<std::string> names;
    for (const auto &entry : std::filesystem::directory_iterator(directory_)) {
      require(entry.is_regular_file() && !entry.is_symlink(),"Unexpected nonregular physical-spacing artifact");
      names.push_back(entry.path().filename().u8string());
    }
    std::sort(names.begin(),names.end()); Json members = Json::array(); std::uint64_t total = 0;
    for (const auto &name : names) {
      const auto size = std::filesystem::file_size(directory_/name);
      require(size > 0 && size <= maxFileBytes && size <= maxTreeBytes-total,"Physical-spacing artifact bounds");
      const auto bytes = readBytes((directory_/name).u8string()); total += bytes.size();
      require(bytes.size() == size,"Physical-spacing artifact changed during indexing");
      members.push_back(Json::array({name,bytes.size(),sha256(bytes)}));
    }
    const auto canonical = members.dump();
    return {{"members",members},{"member_count",members.size()},{"byte_count",total},
        {"sha256",sha256(std::vector<unsigned char>(canonical.begin(),canonical.end()))},{"excluded_members",{"case.json"}}};
  }
};

Json spacingUiCase(Window &window,const std::string &caseId,const std::string &context,
                   const std::string &directory,std::uint64_t *launchBytes) {
  window.checkThread();
  if (context != "imgui_cpp" && context != "imgui_python")
    throw std::invalid_argument("Physical-spacing UI context must name a real launch");
  if (!window.documents().empty() || window.busy())
    throw std::invalid_argument("Physical-spacing UI case requires a fresh idle Window");
  const auto &spec = findCase(caseId); const auto root = freshDirectory(directory);
  try {
    AxisRangeSpacingUiDriver driver(window,spec,context,root,launchBytes); return driver.run();
  } catch (const std::exception &error) {
    // Constructor failures also leave an owned diagnostic. Never replace the
    // complete frame/lifecycle failure report already written by run().
    try {
      const auto path = root/"case-failed.json";
      if (!std::filesystem::exists(path)) {
        const Json failure{{"schema_version",1},{"contract_id",contract},{"case_id",spec.id},
            {"context",context},{"acceptance_claim",false},{"observation_state","failed"},
            {"failure",exceptionJson(error)},{"failure_scope","setup_or_unserialized_case_failure"},
            {"literal_input",literalInput(spec)}};
        const auto json = failure.dump();
        std::uint64_t total = 0;
        for (const auto &entry : std::filesystem::directory_iterator(root)) {
          require(entry.is_regular_file() && !entry.is_symlink(),"Physical-spacing failure artifact ownership");
          const auto size = entry.file_size();
          require(size <= maxFileBytes && size <= maxTreeBytes-total,"Physical-spacing failure artifact bounds");
          total += size;
        }
        if (!json.empty() && json.size() <= maxFileBytes && json.size() <= maxTreeBytes-total &&
            (!launchBytes || json.size() <= maxTreeBytes-*launchBytes)) {
          writeBytes(path.u8string(),std::vector<unsigned char>(json.begin(),json.end()));
          if (launchBytes) *launchBytes += json.size();
        }
      }
    } catch (...) {}
    throw;
  }
}
} // namespace

Json axisRangeSpacingUiCase(Window &window,const std::string &caseId,const std::string &context,
                            const std::string &directory) {
  return spacingUiCase(window,caseId,context,directory,nullptr);
}
int axisRangeSpacingUiConsumerTest(Window &window,const std::string &directory) {
  window.checkThread(); const auto root = freshDirectory(directory); const auto start = Clock::now();
  Json report{{"schema_version",1},{"contract_id",contract},{"context","imgui_cpp"},{"observation_state","observing"},
      {"acceptance_claim",false},{"case_count",cases.size()},{"case_format","indexed_case_files"},
      {"limits",{{"max_seconds",aggregateSeconds},{"max_case_frames",maxFrames},{"max_case_seconds",maxSeconds},
          {"max_file_bytes",maxFileBytes},{"max_tree_bytes",maxTreeBytes}}},{"cases",Json::array()}};
  try {
    std::uint64_t total = 0;
    std::uint64_t indexedTotal = 0;
    for (const auto &spec : cases) {
      require(elapsed(start) < aggregateSeconds,"Physical-spacing UI aggregate time limit reached");
      Window child((root/(std::string(spec.id)+"-preferences.json")).u8string());
      spacingUiCase(child,spec.id,"imgui_cpp",(root/spec.id).u8string(),&total);
      const auto path = root/spec.id/"case.json";
      require(std::filesystem::is_regular_file(std::filesystem::symlink_status(path)) &&
          !std::filesystem::is_symlink(std::filesystem::symlink_status(root/spec.id)),
          "Physical-spacing index must refer to its ordinary owned case file");
      for (const auto &entry : std::filesystem::directory_iterator(root/spec.id)) {
        require(entry.is_regular_file() && !entry.is_symlink(),"Physical-spacing aggregate member ownership");
        const auto size = entry.file_size();
        require(size > 0 && size <= maxFileBytes && size <= maxTreeBytes-indexedTotal,"Physical-spacing aggregate2GiB cap");
        indexedTotal += size;
      }
      const auto size = std::filesystem::file_size(path);
      require(size > 0 && size <= maxFileBytes,"Physical-spacing case JSON64MiB cap");
      const auto bytes = readBytes(path.u8string()); require(bytes.size() == size,"Physical-spacing case changed during indexing");
      report["cases"].push_back({{"case_id",spec.id},{"context","imgui_cpp"},{"path",std::string(spec.id)+"/case.json"},
          {"bytes",bytes.size()},{"sha256",sha256(bytes)}});
      require(indexedTotal == total,"Physical-spacing aggregate byte observation disagrees with immutable writes");
    }
    report["elapsed_seconds"] = elapsed(start);
    require(elapsed(start) <= aggregateSeconds,"Physical-spacing UI aggregate exceeded its time limit");
    report["observation_state"] = "observed";
    const auto json = report.dump(2);
    require(!json.empty() && json.size() <= maxFileBytes && json.size() <= maxTreeBytes-total,
        "Physical-spacing index artifact bounds");
    writeBytes((root/"axis-range-spacing-ui.json").u8string(),std::vector<unsigned char>(json.begin(),json.end()));
    std::cout << "20 physical-spacing UI cases emitted actual physical-input observations\n"; return 0;
  } catch (const std::exception &error) {
    report["observation_state"] = "failed"; report["failure"] = exceptionJson(error); report["elapsed_seconds"] = elapsed(start);
    try {
      const auto json = report.dump(2);
      std::uint64_t total = 0;
      for (const auto &entry : std::filesystem::directory_iterator(root)) {
        require(!entry.is_symlink(),"Physical-spacing aggregate failure artifact ownership");
        if (entry.is_directory()) {
          for (const auto &member : std::filesystem::directory_iterator(entry.path())) {
            require(member.is_regular_file() && !member.is_symlink(),"Physical-spacing failure case member ownership");
            const auto size = member.file_size();
            require(size <= maxFileBytes && size <= maxTreeBytes-total,"Physical-spacing aggregate failure bounds");
            total += size;
          }
        } else {
          require(entry.is_regular_file(),"Physical-spacing failure root member ownership");
          const auto size = entry.file_size();
          require(size <= maxFileBytes && size <= maxTreeBytes-total,"Physical-spacing aggregate failure root bounds");
          total += size;
        }
      }
      if (!json.empty() && json.size() <= maxFileBytes && json.size() <= maxTreeBytes-total)
        writeBytes((root/"axis-range-spacing-ui-failed.json").u8string(),std::vector<unsigned char>(json.begin(),json.end()));
    } catch (...) {}
    throw;
  }
}
} // namespace imagejxx::desktop
