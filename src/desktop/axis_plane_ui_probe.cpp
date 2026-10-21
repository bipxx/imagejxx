// Observation producer only. Literal sources are inputs; expected export pixels
// and full-affine results belong to the independent reader, never this driver.
#include "axis_plane_ui_probe.hpp"
#include "gaussian_cancellation_internal.hpp"
#include "headless_session_internal.hpp"
#include "window.hpp"
#include <imgui.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdint>
#include <cstring>
#include <exception>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <sstream>
#include <stdexcept>

namespace imagejxx::desktop {
namespace {
using Clock = std::chrono::steady_clock;
constexpr int width = 32, height = 17, depth = 9;
constexpr std::size_t maxFrames = 160;
constexpr std::size_t maxCaseJson = 64u*1024u*1024u, maxIndexJson = 64u*1024u;
constexpr double maxSeconds = 15., aggregateSeconds = 180.;
constexpr const char *contract = "p05f.axis-plane-export-ui.001";
constexpr std::array<const char *, 21> caseIds{{
  "E01","E02","E03","E04","E05","E06","E07","E08","E09","E10","E11",
  "E12","E13","E14","E15","E16","E17","E18","E19","E20","E21"}};
constexpr std::array<const char *, 21> profiles{{
  "u8","u16","f32","u8","u16","f32","u8","u16","f32","f64","rgb",
  "u8","u8","u16","f32","u8","u8","u16","f32","rgba","f32_nan"}};

void require(bool condition, const char *message) {
  if (!condition) throw std::runtime_error(message);
}
double elapsed(Clock::time_point start) {
  return std::chrono::duration<double>(Clock::now() - start).count();
}
std::filesystem::path freshDirectory(const std::string &directory) {
  require(!directory.empty(), "Axis-plane probe needs an output directory");
  const auto path = std::filesystem::absolute(std::filesystem::u8path(directory)).lexically_normal();
  require(path != path.root_path(), "Axis-plane probe cannot own a filesystem root");
  std::filesystem::create_directories(path.parent_path());
  require(std::filesystem::create_directory(path), "Axis-plane probe directory must be fresh");
  return path;
}
int caseNumber(const std::string &caseId) {
  for (std::size_t i = 0; i < caseIds.size(); ++i)
    if (caseId == caseIds[i]) return static_cast<int>(i + 1);
  throw std::invalid_argument("Axis-plane UI case must be E01..E21");
}
Json simultaneousKeys(bool down) {
  return {{"type", "keys"}, {"keys", Json::array({
      Json{{"name", "Ctrl"}, {"down", down}}, Json{{"name", "A"}, {"down", down}}})}};
}
Json exceptionJson(const std::exception &error) {
  const char *type = dynamic_cast<const RevisionConflict *>(&error) ? "revision_conflict"
      : dynamic_cast<const DocumentUnavailable *>(&error) ? "document_unavailable"
      : dynamic_cast<const std::out_of_range *>(&error) ? "out_of_range"
      : dynamic_cast<const std::invalid_argument *>(&error) ? "invalid_argument" : "exception";
  return {{"type", type}, {"message", error.what()}};
}
// SHA256 is only an identity of bytes actually written by this observer.
std::string sha256(const std::vector<unsigned char> &input) {
  constexpr std::array<std::uint32_t, 64> k{{
    0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,
    0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
    0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
    0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
    0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,
    0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
    0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,
    0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2}};
  std::array<std::uint32_t, 8> hash{{
    0x6a09e667,0xbb67ae85,0x3c6ef372,0xa54ff53a,0x510e527f,0x9b05688c,0x1f83d9ab,0x5be0cd19}};
  auto bytes = input;
  bytes.push_back(0x80);
  while (bytes.size() % 64 != 56) bytes.push_back(0);
  const auto bits = static_cast<std::uint64_t>(input.size()) * 8;
  for (int shift = 56; shift >= 0; shift -= 8) bytes.push_back(static_cast<unsigned char>(bits >> shift));
  const auto rotate = [](std::uint32_t value, int shift) {
    return (value >> shift) | (value << (32 - shift));
  };
  for (std::size_t offset = 0; offset < bytes.size(); offset += 64) {
    std::array<std::uint32_t, 64> words{};
    for (std::size_t i = 0; i < 16; ++i)
      for (std::size_t j = 0; j < 4; ++j) words[i] = (words[i] << 8) | bytes[offset + 4 * i + j];
    for (std::size_t i = 16; i < 64; ++i) {
      const auto a = words[i - 15], b = words[i - 2];
      words[i] = words[i - 16] + (rotate(a,7) ^ rotate(a,18) ^ (a >> 3)) +
          words[i - 7] + (rotate(b,17) ^ rotate(b,19) ^ (b >> 10));
    }
    auto work = hash;
    for (std::size_t i = 0; i < 64; ++i) {
      const auto first = work[7] + (rotate(work[4],6) ^ rotate(work[4],11) ^ rotate(work[4],25)) +
          ((work[4] & work[5]) ^ (~work[4] & work[6])) + k[i] + words[i];
      const auto second = (rotate(work[0],2) ^ rotate(work[0],13) ^ rotate(work[0],22)) +
          ((work[0] & work[1]) ^ (work[0] & work[2]) ^ (work[1] & work[2]));
      for (std::size_t j = 7; j > 0; --j) work[j] = work[j - 1];
      work[4] += first;
      work[0] = first + second;
    }
    for (std::size_t i = 0; i < 8; ++i) hash[i] += work[i];
  }
  std::ostringstream result;
  result << std::hex << std::setfill('0');
  for (const auto value : hash) result << std::setw(8) << value;
  return result.str();
}
const char *planeName(Plane plane) {
  return plane == Plane::XY ? "XY" : plane == Plane::XZ ? "XZ" : "YZ";
}
Json revisionsJson(const Revisions &value) {
  return {{"pixels", value.pixels}, {"metadata", value.metadata},
      {"selection", value.selection}, {"display", value.display}};
}
Json calibrationJson(const SpatialCalibration &value) {
  return {{"spacing", value.spacing}, {"origin", value.origin}, {"direction", value.direction},
      {"unit", value.unit}, {"time_unit", value.time_unit},
      {"frame_interval", value.frame_interval}, {"time_origin", value.time_origin}};
}
Json displayJson(const DisplayState &value) {
  return {{"t",value.t},{"c",value.c},{"crosshair",value.crosshair},
      {"orthogonal",value.orthogonal},{"linked_zoom",value.linkedZoom},{"zoom",value.zoom},
      {"plane_zoom",value.planeZoom},{"pan",value.pan},{"properties",metadataJson(value.properties)},
      {"revision",value.revision}};
}
Json selectionJson(const std::optional<Selection> &value) {
  if (!value) return nullptr;
  return {{"kind",value->kind},{"points",value->points},{"width",value->width},
      {"attributes",metadataJson(value->attributes)},{"mask_width",value->mask_width()},
      {"mask_height",value->mask_height()},
      {"packed_mask",value->kind == "mask" ? Json(value->packed_mask()) : Json::array()}};
}
Json usageJson(const JobUsage &value) {
  return {{"running",value.running},{"queued",value.queued},{"running_bytes",value.running_bytes},
      {"retained_bytes",value.retained_bytes},{"reserved_bytes",value.reserved_bytes}};
}
const char *jobState(JobState state) {
  switch (state) {
  case JobState::Queued:return "queued"; case JobState::Running:return "running";
  case JobState::Ready:return "ready"; case JobState::Publishing:return "publishing";
  case JobState::Completed:return "completed"; case JobState::Failed:return "failed";
  case JobState::Canceled:return "canceled";
  }
  return "unknown";
}
PixelType profileType(const std::string &profile) {
  if (profile == "u16") return PixelType::UInt16;
  if (profile == "f64") return PixelType::Float64;
  if (profile == "f32" || profile == "f32_nan") return PixelType::Float32;
  return PixelType::UInt8;
}
const char *typeName(PixelType type) {
  switch (type) {
  case PixelType::UInt8:return "UInt8"; case PixelType::UInt16:return "UInt16";
  case PixelType::Float32:return "Float32"; case PixelType::Float64:return "Float64";
  }
  throw std::invalid_argument("Unknown pixel type in axis observer");
}
const char *dtypeName(PixelType type) {
  switch (type) {
  case PixelType::UInt8:return "uint8"; case PixelType::UInt16:return "uint16";
  case PixelType::Float32:return "float32"; case PixelType::Float64:return "float64";
  }
  throw std::invalid_argument("Unknown dtype in axis observer");
}
SpatialCalibration inputCalibration() {
  SpatialCalibration value;
  value.spacing = {.4,1.2,3.}; value.origin = {11.,-7.,19.};
  value.direction = {1.,.25,-.5,.5,-1.,.125,0.,.75,1.5};
  value.unit = "um"; value.time_unit = "s";
  value.frame_interval = .75; value.time_origin = 2.25;
  return value;
}
Dataset inputDataset(const std::string &profile, bool other = false) {
  const int samples = profile == "rgb" ? 3 : profile == "rgba" ? 4 : 1;
  const auto type = profileType(profile);
  Stack stack;
  for (int z = 0; z < depth; ++z) {
    Image image(width,height,samples,type);
    for (int y = 0; y < height; ++y) for (int x = 0; x < width; ++x)
      for (int sample = 0; sample < samples; ++sample) {
        double value = 187.;
        if (!other) {
          if (profile == "u8") value = (37*x+61*y+13*x*y+43*z)%251;
          else if (profile == "u16") value = 6000*z+32*y+x;
          else if (profile == "rgb" || profile == "rgba") {
            const std::array<int,4> values{{(17*x+31*y+47*z)%256,(71*x+11*y+29*z)%256,
                (13*x+61*y+23*z)%256,(19*x+53*y+7*z+127)%256}};
            value = values[sample];
          } else value = 10000*z+100*y+x+.25;
          if (profile == "f32_nan" && z == 0 && y == 2 && x == 3)
            value = float32_source_sample(0x7fc12345u,FloatingSamplePolicy::PreserveQuietNaNFloat32);
        }
        image.at(x,y,sample) = value;
      }
    stack.push_back(std::move(image));
  }
  return Dataset::from_stack(std::move(stack),inputCalibration(),
      profile == "f32_nan" ? FloatingSamplePolicy::PreserveQuietNaNFloat32 : FloatingSamplePolicy::FiniteOnly);
}
Selection taggedSelection(const char *kind, std::vector<std::array<double,2>> points, const char *tag) {
  Selection result(kind,std::move(points),1);
  result.attributes = metadataObjectFromJson({{"tag",tag}});
  return result;
}
DocumentData inputData(const std::string &profile, int number, bool other = false) {
  DocumentData data{inputDataset(profile,other)};
  data.metadata.title = other ? "Other" : "Export Source";
  if (other) return data;
  data.metadata.properties = metadataObjectFromJson({{"source_only","retained property"}});
  data.metadata.extensions = metadataObjectFromJson({{"source_extension",{{"retained",true}}}});
  data.selection = taggedSelection("rectangle",{{2,3},{7,9}},"transient");
  data.previous_selection = taggedSelection("ellipse",{{1,1},{4,5}},"previous");
  data.rois.push_back({"Saved rectangle",taggedSelection("rectangle",{{1,2},{5,6}},"saved_first"),
      {},{},2,metadataObjectFromJson({{"source_only","first"}})});
  data.rois.push_back({"Saved second",taggedSelection("ellipse",{{10,3},{15,7}},"saved_second"),
      {},{},4,metadataObjectFromJson({{"source_only","second"}})});
  data.overlays.push_back(metadataObjectFromJson({{"source_overlay","retained"}}));
  if (number == 10) data.spectrum = FourierState(Spectrum{32,32,std::vector<std::complex<double>>(1024)},
      std::array<int,2>{17,32});
  return data;
}
void littleEndian(std::vector<unsigned char> &out, std::uint64_t bits, int count) {
  for (int i = 0; i < count; ++i) out.push_back(static_cast<unsigned char>(bits >> (8*i)));
}
std::vector<unsigned char> rawPixels(const Dataset &dataset) {
  const auto shape = dataset.shape();
  std::vector<unsigned char> bytes;
  const auto type = dataset.pixel_type();
  const int count = type == PixelType::UInt8 ? 1 : type == PixelType::UInt16 ? 2 :
      type == PixelType::Float32 ? 4 : 8;
  bytes.reserve(shape.pixel_count()*count);
  for (int t = 0; t < shape.t; ++t) for (int c = 0; c < shape.c; ++c)
    for (int z = 0; z < shape.z; ++z) for (const double value : dataset.plane({t,c,z})->pixels) {
      if (type == PixelType::UInt8 || type == PixelType::UInt16) {
        require(std::isfinite(value) && value >= 0 && value <= type_max(type) && value == std::floor(value),
                "Observed integer sample is not exactly typed");
        littleEndian(bytes,static_cast<std::uint64_t>(value),count);
      } else if (type == PixelType::Float32)
        littleEndian(bytes,float32_source_sample_bits(value,dataset.sample_policy()),4);
      else {
        require(std::isfinite(value),"Observed Float64 sample is nonfinite");
        std::uint64_t bits;
        static_assert(sizeof(bits) == sizeof(value),"Float64 byte observation needs IEEE64 width");
        std::memcpy(&bits,&value,sizeof(bits));
        littleEndian(bytes,bits,8);
      }
    }
  return bytes;
}

class AxisPlaneUiDriver {
public:
  AxisPlaneUiDriver(Window &window,int number,std::string context,std::filesystem::path directory)
      : window_(window),number_(number),profile_(profiles[number-1]),directory_(std::move(directory)),
        start_(Clock::now()),owner_(std::make_shared<imagejxx::Document>(inputData(profile_,number))) {
    report_ = {{"schema_version",1},{"contract_id",contract},{"case_id",caseIds[number-1]},
        {"context",std::move(context)},{"profile",profile_},{"acceptance_claim",false},
        {"observation_state","observing"},
        {"limits",{{"max_frames",maxFrames},{"max_seconds",maxSeconds}}},
        {"frames",Json::array()},{"control_observations",Json::array()},{"lifecycle",Json::array()},
        {"public_stimuli",Json::array()},{"publication_observations",Json::array()},
        {"states",Json::object()},{"rejections",Json::array()},{"raw_members",Json::array()}};
    window_.updateSettings({{"default_zoom",1.25},{"history_limit",5},{"history_memory_mb",8}},false);
    window_.addNativeDocument(owner_);
    owner_->set_history_policy(3,4096);
    auto edit = window_.beginEdit(owner_->id);
    edit.set_property("revision_probe",7);
    window_.commitEdit(edit);
    auto display = owner_->snapshot().display();
    display.crosshair = {7,5,4}; display.orthogonal = false; display.linkedZoom = true;
    display.zoom = 2.; display.planeZoom = {}; display.pan = {};
    display.properties = metadataObjectFromJson({{"display_range",{0,255}},{"lut","Ice"}});
    window_.setNativeDisplay(display,owner_->id);
    if (number >= 11 && number <= 19) {
      other_ = std::make_shared<imagejxx::Document>(inputData(profile_,number,true));
      window_.addNativeDocument(other_);
      window_.setActiveIndex(0);
    }
    captured_ = owner_->snapshot();
    capture("source_ready");
    report_["source_before_raw"] = rawMember("source-before",captured_->data().dataset);
    AxisPlaneUiProbeAccess::setPublicationProbe(window_,
        [this](const char *phase,const DocumentSnapshot &source,
               const AxisPlaneExportRequest &request,const std::shared_ptr<imagejxx::Document> &candidate) noexcept {
          try {
            if (std::strcmp(phase,"before_native_publish") == 0) {
              require(!beforePublication_,"Duplicate before-publication observation");
              beforePublication_ = true;
              publicationRequest_ = request;
              publicationSource_ = summary(owner_,source,true,false);
              Json observation{{"phase","before_native_publish"},{"captured_source",publicationSource_},
                  {"request",requestJson(request)},{"candidate",summary(candidate,candidate->snapshot(),true,true)},
                  {"state",collection(true)}};
              report_["publication_observations"].push_back(std::move(observation));
            } else if (std::strcmp(phase,"after_attach") == 0) {
              // After adoption there is no snapshot, JSON, I/O, container growth,
              // string copy, or throwing operation. Preallocated scalar/shared
              // ownership slots are serialized only after session.frame returns.
              if (afterPublication_) callbackFailure_ = duplicateCallbackFailure_;
              afterPublication_ = true;
              deferredCandidate_ = candidate;
            }
          } catch (...) { callbackFailure_ = std::current_exception(); }
        });
    try { throw std::runtime_error("Duplicate after-attach observation"); }
    catch (...) { duplicateCallbackFailure_ = std::current_exception(); }
  }
  ~AxisPlaneUiDriver() {
    try { AxisPlaneUiProbeAccess::setPublicationProbe(window_,{}); } catch (...) {}
  }
  Json run() {
    try {
      HeadlessInputSession session(window_);
      report_["lifecycle"].push_back({{"label","opened"},{"observed",session.lifecycle()}});
      frame(session,"warmup.1"); frame(session,"warmup.2");
      openDialog(session);
      if (number_ <= 9) ordinaryCase(session);
      else if (number_ == 10) defaultsCase(session);
      else if (number_ == 11) switchCase(session);
      else if (number_ == 12) cancelCase(session);
      else if (number_ >= 13 && number_ <= 15) invalidCase(session);
      else if (number_ == 16) busyCase(session);
      else if (number_ == 17) stalePixelsCase(session);
      else if (number_ == 18) staleCalibrationCase(session);
      else if (number_ == 19) closedCase(session);
      else if (number_ == 20) allowedEditsCase(session);
      else { editIndex(session,1,"index_edited"); submit(session); }
      checkpoint(session,"final_idle");
      session.close();
      report_["lifecycle"].push_back({{"label","closed"},{"observed",session.lifecycle()}});
      report_["source_after_raw"] = rawMember("source-after",owner_->snapshot().data().dataset);
      if (output_) report_["output_raw"] = rawMember("output-final",output_->snapshot().data().dataset);
      report_["final_busy"] = window_.busy(); report_["window_error"] = window_.error();
      report_["final_context"] = {{"current_context_id",static_cast<std::uint64_t>(
          reinterpret_cast<std::uintptr_t>(ImGui::GetCurrentContext()))}};
      report_["frame_count"] = frameOrdinal_;
      budget(); report_["observation_state"] = "observed";
      report_["artifact_tree"] = artifactTree();
      report_["elapsed_seconds"] = elapsed(start_); budget();
      writeJson((directory_/"case.json").u8string(),report_);
      budget();
      return report_;
    } catch (const std::exception &error) {
      report_["observation_state"] = "failed"; report_["failure"] = exceptionJson(error);
      report_["frame_count"] = frameOrdinal_; report_["elapsed_seconds"] = elapsed(start_);
      try { writeJson((directory_/"case-failed.json").u8string(),report_); } catch (...) {}
      throw;
    }
  }
private:
  Window &window_;
  int number_;
  std::string profile_;
  std::filesystem::path directory_;
  Clock::time_point start_;
  std::shared_ptr<imagejxx::Document> owner_,other_,output_,deferredCandidate_;
  std::optional<DocumentSnapshot> captured_;
  Json report_,last_,publicationSource_;
  AxisPlaneExportRequest publicationRequest_;
  bool beforePublication_ = false,afterPublication_ = false,afterSerialized_ = false;
  std::exception_ptr callbackFailure_,duplicateCallbackFailure_;
  std::size_t frameOrdinal_ = 0,rawOrdinal_ = 0;

  void budget() const {
    require(frameOrdinal_ <= maxFrames && elapsed(start_) <= maxSeconds,
            "Axis-plane UI case exceeded its declared frame/time limit");
  }
  Json requestJson(const AxisPlaneExportRequest &value) const {
    return {{"plane",planeName(value.plane)},{"coordinate",value.coordinate},{"t",value.t},
        {"c",value.c},{"max_working_bytes",value.max_working_bytes}};
  }
  Json rawMember(const std::string &prefix,const Dataset &dataset) {
    const auto bytes = rawPixels(dataset);
    std::ostringstream name;
    name << prefix << '-' << std::setw(4) << std::setfill('0') << ++rawOrdinal_ << ".bin";
    const auto path = directory_/name.str();
    require(!std::filesystem::exists(path),"Axis-plane raw member already exists");
    writeBytes(path.u8string(),bytes);
    const auto type = dataset.pixel_type(); const auto shape = dataset.shape();
    const std::string encoding = std::string(typeName(type)) + (type == PixelType::UInt8 ? "" : "LE");
    Json descriptor{{"member_id",name.str()},{"path",name.str()},{"encoding",encoding},
        {"dtype",dtypeName(type)},{"axes","TCZYXS"},{"shape",shape.dimensions()},
        {"sample_count",shape.pixel_count()},{"byte_count",bytes.size()},{"sha256",sha256(bytes)},
        {"endian","little"}};
    report_["raw_members"].push_back(descriptor);
    return descriptor;
  }
  Json spectrumMember(const FourierState &state) {
    const auto &spectrum = state.spectrum();
    std::vector<unsigned char> bytes;
    bytes.reserve(spectrum.pixels.size()*16);
    for (const auto value : spectrum.pixels) for (const double part : {value.real(),value.imag()}) {
      std::uint64_t bits; std::memcpy(&bits,&part,sizeof(bits)); littleEndian(bytes,bits,8);
    }
    std::ostringstream name;
    name << "spectrum-" << std::setw(4) << std::setfill('0') << ++rawOrdinal_ << ".bin";
    writeBytes((directory_/name.str()).u8string(),bytes);
    Json descriptor{{"member_id",name.str()},{"path",name.str()},{"encoding","ComplexFloat64LE"},
        {"dtype","complex128"},{"axes","YX"},{"shape",{spectrum.height,spectrum.width}},
        {"sample_count",spectrum.pixels.size()},{"byte_count",bytes.size()},{"sha256",sha256(bytes)},
        {"endian","little"}};
    report_["raw_members"].push_back(descriptor);
    return descriptor;
  }
  Json summary(const std::shared_ptr<imagejxx::Document> &owner,const DocumentSnapshot &snapshot,
               bool raw,bool worlds) {
    const auto &data = snapshot.data(); const auto shape = data.dataset.shape();
    const auto position = [](const std::optional<int> &v) {return v ? Json(*v) : Json(nullptr);};
    Json rois = Json::array(),overlays = Json::array();
    for (const auto &roi : data.rois) rois.push_back({{"name",roi.name},{"t",position(roi.t)},
        {"c",position(roi.c)},{"z",position(roi.z)},{"attributes",metadataJson(roi.attributes)},
        {"selection",selectionJson(roi.selection)}});
    for (const auto &overlay : data.overlays) overlays.push_back(metadataJson(overlay));
    Json result{{"document_id",snapshot.id()},
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
            {"sample_policy",data.dataset.sample_policy() == FloatingSamplePolicy::FiniteOnly ?
                "FiniteOnly" : "PreserveQuietNaNFloat32"},
            {"calibration",calibrationJson(data.dataset.calibration())}}}};
    if (data.spectrum) {
      const auto &fourier = *data.spectrum;
      result["spectrum"] = {{"width",fourier.spectrum().width},{"height",fourier.spectrum().height},
          {"original_shape",fourier.original_shape() ? Json(*fourier.original_shape()) : Json(nullptr)}};
      if (raw) result["spectrum"]["raw_member"] = spectrumMember(fourier);
    }
    if (raw) result["raw_member"] = rawMember("document-"+std::to_string(snapshot.id()),data.dataset);
    if (worlds) {
      Json points = Json::array();
      for (int v = 0; v < shape.y; ++v) for (int u = 0; u < shape.x; ++u) {
        const std::array<double,3> voxel{{static_cast<double>(u),static_cast<double>(v),0.}};
        points.push_back({{"voxel_xyz",voxel},{"world_xyz",data.dataset.calibration().voxel_to_world(voxel)}});
      }
      result["world_points"] = std::move(points);
    }
    return result;
  }
  Json collection(bool raw) {
    auto &application = GaussianCancellationAccess::application(window_);
    Json ids = Json::array();
    for (const auto &facade : window_.documents()) ids.push_back(facade->id);
    return {{"source",summary(owner_,owner_->snapshot(),raw,false)},
        {"other",other_ ? summary(other_,other_->snapshot(),raw,false) : Json(nullptr)},
        {"output",output_ ? summary(output_,output_->snapshot(),raw,raw) : Json(nullptr)},
        {"native_member_ids",application.document_ids()},{"facade_ids",std::move(ids)},
        {"active_document_id",application.active_document_id()},{"busy",window_.busy()},
        {"scheduler_usage",usageJson(GaussianCancellationAccess::scheduler(window_)->usage())},
        {"workflow",window_.workflow},{"results",window_.results}};
  }
  void capture(const std::string &name) { report_["states"][name] = collection(true); }
  void flushPublication() {
    if (callbackFailure_) std::rethrow_exception(callbackFailure_);
    if (afterPublication_ && !afterSerialized_) {
      require(beforePublication_ && deferredCandidate_,"Missing staged publication observation");
      output_ = deferredCandidate_;
      report_["publication_observations"].push_back({{"phase","after_attach"},
          {"captured_source",publicationSource_},{"request",requestJson(publicationRequest_)},
          {"candidate",summary(output_,output_->snapshot(),true,true)},{"state",collection(true)}});
      afterSerialized_ = true;
    }
  }
  void frame(HeadlessInputSession &session,const std::string &label,const Json &events = Json::array()) {
    require(frameOrdinal_ < maxFrames,"Axis-plane UI frame limit reached before input"); budget();
    last_ = session.frame(events);
    flushPublication();
    std::ostringstream name;
    name << "frame-" << std::setw(4) << std::setfill('0') << ++frameOrdinal_ << ".json";
    Json actual{{"ordinal",frameOrdinal_},{"label",label},{"member_id",name.str()},
        {"requested_events",events},{"rendered",last_},{"native_state",collection(false)}};
    writeJson((directory_/name.str()).u8string(),actual);
    report_["frames"].push_back(std::move(actual)); budget();
  }
  void checkpoint(HeadlessInputSession &session,const std::string &name) {
    frame(session,name); capture(name);
  }
  Json control(const std::string &selector) const {
    if (selector.rfind("dialog.",0) == 0) return last_.at("dialog").at("controls").at(selector.substr(7));
    if (selector.rfind("error_modal.",0) == 0) return last_.at("error_modal").at("controls").at(selector.substr(12));
    return last_.at("host_controls").at(selector);
  }
  void click(HeadlessInputSession &session,const std::string &selector,bool disabled = false) {
    const auto actual = control(selector);
    require((actual.at("enabled") == true || disabled) && actual.at("id").get<std::uint64_t>() > 0,
            "Axis-plane pointer target is not an observed eligible control");
    const auto &rect = actual.at("rect"), &point = actual.at("point");
    const double x = std::floor(point[0].get<double>()), y = std::floor(point[1].get<double>());
    require(rect.size() == 4 && x >= rect[0].get<double>() && x < rect[2].get<double>() &&
            y >= rect[1].get<double>() && y < rect[3].get<double>(),"Axis-plane pointer is outside observed bounds");
    report_["control_observations"].push_back({{"selector",selector},{"control",actual},
        {"position",{x,y}},{"before_frame_ordinal",frameOrdinal_}});
    frame(session,selector+".position",Json::array({Json{{"type","position"},{"space","screen"},{"point",{x,y}}}}));
    frame(session,selector+".down",Json::array({Json{{"type","button"},{"name","left"},{"down",true}}}));
    frame(session,selector+".up",Json::array({Json{{"type","button"},{"name","left"},{"down",false}}}));
  }
  void openDialog(HeadlessInputSession &session) {
    click(session,"menu.Image"); frame(session,"image_menu_open");
    click(session,"menu.Image.Stacks"); frame(session,"stacks_menu_open");
    click(session,"menu.action.show_axis_plane_export"); checkpoint(session,"dialog_opened");
  }
  void choosePlane(HeadlessInputSession &session,const char *plane,const std::string &label = "plane_selected") {
    click(session,"dialog.plane"); frame(session,"plane_combo_open");
    click(session,std::string("dialog.plane.choice.")+plane); checkpoint(session,label);
  }
  void editIndex(HeadlessInputSession &session,int index,const std::string &label) {
    click(session,"dialog.index");
    frame(session,label+".select_all.down",Json::array({simultaneousKeys(true)}));
    frame(session,label+".select_all.up",Json::array({simultaneousKeys(false)}));
    frame(session,label+".text",Json::array({Json{{"type","text"},{"value",std::to_string(index)}}}));
    checkpoint(session,label);
  }
  void submit(HeadlessInputSession &session) {
    checkpoint(session,"before_submit"); click(session,"dialog.apply"); checkpoint(session,"after_submit");
  }
  void cancel(HeadlessInputSession &session) {
    click(session,"dialog.cancel"); checkpoint(session,"after_cancel");
  }
  template<class Function> Json stimulus(const std::string &label,const char *method,Json arguments,
                                         Function operation) {
    Json entry{{"ordinal",report_["public_stimuli"].size()+1},{"label",label},{"method",method},
        {"arguments",std::move(arguments)},{"classification","public_state_stimulus_no_physical_credit"},
        {"before",collection(true)},{"after",nullptr},{"exception",nullptr}};
    try { operation(); } catch (const std::exception &error) { entry["exception"] = exceptionJson(error); }
    entry["after"] = collection(true);
    report_["public_stimuli"].push_back(entry);
    return entry;
  }
  void tabSwitch(HeadlessInputSession &session,bool checkpoints = false) {
    if (checkpoints) checkpoint(session,"before_tab_switch");
    stimulus("tab_switch","setActiveIndex",{{"document_id",other_->id},{"index",1}},
        [&] {window_.setActiveIndex(1);});
    if (checkpoints) checkpoint(session,"after_tab_switch");
  }
  void navigate(const std::string &label,std::array<int,3> xyz) {
    stimulus(label,"setNativeDisplay",{{"document_id",owner_->id},{"crosshair",xyz}},[&] {
      auto display = owner_->snapshot().display(); display.crosshair = xyz;
      window_.setNativeDisplay(display,owner_->id);
    });
  }
  void ordinaryCase(HeadlessInputSession &session) {
    const int group = (number_-1)/3, level = (number_-1)%3;
    const char *plane = group == 0 ? "XY" : group == 1 ? "XZ" : "YZ";
    const std::array<int,3> indices = group == 0 ? std::array<int,3>{1,5,9} :
        group == 1 ? std::array<int,3>{1,6,17} : std::array<int,3>{1,8,32};
    if (group) choosePlane(session,plane); else checkpoint(session,"plane_selected");
    editIndex(session,indices[level],"index_edited"); submit(session);
  }
  void defaultsCase(HeadlessInputSession &session) {
    editIndex(session,2,"xy_index_edited"); navigate("navigation_1",{3,2,1});
    checkpoint(session,"after_navigation_1"); choosePlane(session,"XZ","xz_axis_reset");
    editIndex(session,10,"xz_index_edited"); navigate("navigation_2",{1,1,8});
    checkpoint(session,"after_navigation_2"); choosePlane(session,"YZ","yz_axis_reset");
    editIndex(session,12,"yz_index_edited"); choosePlane(session,"XY","xy_axis_reset"); submit(session);
  }
  void switchCase(HeadlessInputSession &session) {
    choosePlane(session,"YZ"); tabSwitch(session,true); submit(session);
  }
  void cancelCase(HeadlessInputSession &session) {
    choosePlane(session,"XZ"); editIndex(session,7,"index_edited"); tabSwitch(session,true);
    checkpoint(session,"before_cancel"); cancel(session);
  }
  Json draftRequest() const {
    const auto &values = last_.at("dialog").at("values");
    return {{"plane",values.at("plane")},{"coordinate",values.at("index").get<int>()-1},{"t",0},{"c",0}};
  }
  void rejectSubmit(HeadlessInputSession &session,const char *label,const char *category) {
    checkpoint(session,"before_rejection"); const auto before = report_["states"]["before_rejection"];
    const auto request = draftRequest(); click(session,"dialog.apply"); checkpoint(session,"after_rejection");
    report_["rejections"].push_back({{"label",label},{"expected_category",category},
        {"captured_document_id",captured_->id()},{"captured_revisions",revisionsJson(captured_->revisions())},
        {"request",request},{"before",before},{"after",report_["states"]["after_rejection"]},
        {"error_message",window_.error()},{"dialog_after",last_.at("dialog")}});
    // Let the newly appearing error modal finish its actual auto-size/position.
    frame(session,"error_modal.settled");
    click(session,"error_modal.ok"); checkpoint(session,"error_acknowledged");
  }
  void invalidCase(HeadlessInputSession &session) {
    if (number_ == 14) choosePlane(session,"XZ");
    else if (number_ == 15) choosePlane(session,"YZ");
    const int value = number_ == 13 ? 0 : number_ == 14 ? 18 : -1;
    editIndex(session,value,"invalid_index_edited"); tabSwitch(session);
    rejectSubmit(session,number_ == 13 ? "invalid_zero" : number_ == 14 ? "invalid_upper" : "invalid_negative",
        "invalid_index");
    checkpoint(session,"draft_restored");
    if (number_ == 13) {editIndex(session,1,"corrected_index"); submit(session);} else cancel(session);
  }
  Json jobJson(const Job &job,bool entered) {
    const auto value = job.info();
    return {{"busy",window_.busy()},{"id",value.id},{"document_id",value.document_id},
        {"state",jobState(value.state)},{"cancel_requested",value.cancel_requested},
        {"usage",usageJson(GaussianCancellationAccess::scheduler(window_)->usage())},{"worker_entered",entered}};
  }
  void busyCase(HeadlessInputSession &session) {
    struct Gate {std::mutex mutex;std::condition_variable condition;bool entered=false,released=false;};
    const auto gate = std::make_shared<Gate>();
    std::optional<Job> held;
    const auto start = stimulus("held_job_start","startBusyPreparation",{{"operation","add"},{"value",0},
        {"asynchronous",true},{"document_id",owner_->id}},[&] {
      held = GaussianCancellationAccess::startBusyPreparation(window_,[gate](const JobContext &context) {
        std::unique_lock<std::mutex> lock(gate->mutex); gate->entered = true; gate->condition.notify_all();
        if (!gate->condition.wait_for(lock,std::chrono::seconds(5),[&] {return gate->released;}))
          throw std::runtime_error("Axis-plane busy diagnostic gate timed out");
        lock.unlock(); context.checkpoint();
      });
      std::unique_lock<std::mutex> lock(gate->mutex);
      require(gate->condition.wait_for(lock,std::chrono::seconds(2),[&] {return gate->entered;}),
              "Axis-plane busy job did not enter its actual worker gate");
    });
    require(held.has_value(),"Axis-plane busy job was not created");
    const auto job = *held;
    struct Release {
      Window &window;Job job;std::shared_ptr<Gate> gate;
      ~Release() {try {job.cancel();{std::lock_guard<std::mutex> lock(gate->mutex);
        gate->released=true;gate->condition.notify_all();}
        if (job.wait_for(std::chrono::seconds(2))) window.wait();} catch (...) {}}
    } release{window_,job,gate};
    require(start.at("exception").is_null(),"Axis-plane held worker preparation failed");
    const auto workerEntered = [&] {
      std::lock_guard<std::mutex> lock(gate->mutex); return gate->entered;
    };
    report_["job_before_cleanup"] = jobJson(job,workerEntered()); checkpoint(session,"held_job_entered");
    tabSwitch(session); checkpoint(session,"before_disabled_submit");
    click(session,"dialog.apply",true); checkpoint(session,"after_disabled_submit");
    checkpoint(session,"before_busy_action");
    const auto rejected = stimulus("busy_action","action.export_axis_plane",{{"document_id",owner_->id},
        {"plane","XY"},{"coordinate",4}},[&] {
      window_.action("export_axis_plane",{{"document_id",owner_->id},{"plane","XY"},{"coordinate",4}});
    });
    checkpoint(session,"after_busy_action");
    report_["rejections"].push_back({{"label","busy"},{"expected_category","busy"},
        {"captured_document_id",captured_->id()},{"captured_revisions",revisionsJson(captured_->revisions())},
        {"request",{{"plane","XY"},{"coordinate",4},{"t",0},{"c",0}}},
        {"before",rejected.at("before")},{"after",rejected.at("after")},{"exception",rejected.at("exception")}});
    const auto id = job.info().id;
    stimulus("held_job_cancel","cancelJob",{{"job_id",id}},[&] {require(window_.cancelJob(id),"Busy job cancel rejected");});
    stimulus("held_job_release","diagnosticBarrier.release",{{"job_id",id}},[&] {
      std::lock_guard<std::mutex> lock(gate->mutex);gate->released=true;gate->condition.notify_all();});
    stimulus("held_job_wait","Job.wait_for",{{"job_id",id},{"timeout_ms",2000}},[&] {
      require(job.wait_for(std::chrono::seconds(2)),"Busy canceled worker did not finish");});
    stimulus("held_job_drain","wait",Json::object(),[&] {window_.wait();});
    report_["job_after_cleanup"] = jobJson(job,workerEntered()); checkpoint(session,"held_job_canceled"); cancel(session);
  }
  void stalePixelsCase(HeadlessInputSession &session) {
    choosePlane(session,"XZ");
    stimulus("pixel_edit","commitEdit.replace_plane",{{"document_id",owner_->id},{"t",0},{"c",0},{"z",0},
        {"x",0},{"y",0},{"s",0},{"value",7}},[&] {
      auto image = *owner_->snapshot().data().dataset.plane({0,0,0}); image.at(0,0)=7;
      auto edit = window_.beginEdit(owner_->id); edit.replace_plane({0,0,0},std::move(image)); window_.commitEdit(edit);
    });
    checkpoint(session,"after_pixel_edit"); tabSwitch(session);
    rejectSubmit(session,"stale_pixels","revision_conflict"); cancel(session);
  }
  void staleCalibrationCase(HeadlessInputSession &session) {
    choosePlane(session,"YZ");
    stimulus("calibration_edit","commitEdit.set_calibration",{{"document_id",owner_->id},{"spacing",{.8,1.2,3.}}},[&] {
      auto calibration = owner_->snapshot().data().dataset.calibration();calibration.spacing[0]=.8;
      auto edit=window_.beginEdit(owner_->id);edit.set_calibration(calibration);window_.commitEdit(edit);
    });
    checkpoint(session,"after_calibration_edit"); tabSwitch(session);
    rejectSubmit(session,"stale_calibration","revision_conflict"); cancel(session);
  }
  void closedCase(HeadlessInputSession &session) {
    checkpoint(session,"before_source_close");
    stimulus("source_close","closeDocument",{{"document_id",owner_->id},{"discard",true}},[&] {
      window_.closeDocument(owner_->id,true);
    });
    checkpoint(session,"after_source_close"); rejectSubmit(session,"closed","document_unavailable");
    if (last_.at("dialog").at("open") == true) cancel(session);
    else checkpoint(session,"after_cancel");
  }
  void allowedEditsCase(HeadlessInputSession &session) {
    choosePlane(session,"XZ");
    Selection selection("ellipse",{{8,4},{13,8}},1);
    stimulus("selection_edit","commitEdit.select",{{"document_id",owner_->id},
        {"selection",{{"kind","ellipse"},{"points",{{8,4},{13,8}}},{"width",1},{"attributes",Json::object()}}}},[&] {
      auto edit=window_.beginEdit(owner_->id);edit.select(selection);window_.commitEdit(edit);
    });
    checkpoint(session,"after_selection_edit");
    stimulus("display_edit","setNativeDisplay",{{"document_id",owner_->id},{"crosshair",{3,2,1}},
        {"zoom",3},{"properties",{{"display_range",{0,255}},{"lut","Ice"}}}},[&] {
      auto display=owner_->snapshot().display();display.crosshair={3,2,1};display.zoom=3.;
      window_.setNativeDisplay(display,owner_->id);
    });
    checkpoint(session,"after_display_edit"); submit(session);
  }
  Json artifactTree() const {
    std::vector<std::string> names;
    for (const auto &entry : std::filesystem::directory_iterator(directory_)) {
      require(entry.is_regular_file() && !entry.is_symlink(),"Unexpected nonregular case artifact");
      names.push_back(entry.path().filename().u8string());
    }
    std::sort(names.begin(),names.end());
    Json members=Json::array();std::size_t total=0;
    for (const auto &name:names) {
      const auto bytes=readBytes((directory_/name).u8string());total+=bytes.size();
      members.push_back(Json::array({name,bytes.size(),sha256(bytes)}));
    }
    const auto canonical=members.dump();
    return {{"members",members},{"member_count",members.size()},{"byte_count",total},
        {"sha256",sha256(std::vector<unsigned char>(canonical.begin(),canonical.end()))},
        {"excluded_members",{"case.json"}}};
  }
};
} // namespace

Json Window::axisPlaneExportUiCase(const std::string &caseId,const std::string &context,
                                 const std::string &directory) {
  checkThread();
  if (context != "imgui_cpp" && context != "imgui_python")
    throw std::invalid_argument("Axis-plane UI context must name a real launch");
  if (!documents().empty() || busy())
    throw std::invalid_argument("Axis-plane UI case requires a fresh idle Window");
  const auto number=caseNumber(caseId);const auto root=freshDirectory(directory);
  AxisPlaneUiDriver driver(*this,number,context,root);return driver.run();
}
int Window::axisPlaneExportUiConsumerTest(const std::string &directory) {
  checkThread();const auto root=freshDirectory(directory);const auto start=Clock::now();
  Json report{{"schema_version",2},{"contract_id",contract},{"context","imgui_cpp"},
      {"observation_state","observing"},{"acceptance_claim",false},{"case_count",caseIds.size()},
      {"case_format","indexed_case_files"},
      {"limits",{{"max_seconds",aggregateSeconds},{"max_case_frames",maxFrames},{"max_case_seconds",maxSeconds}}},
      {"cases",Json::array()}};
  try {
    for (const auto id:caseIds) {
      require(elapsed(start)<aggregateSeconds,"Axis-plane UI aggregate time limit reached");
      Window child((root/(std::string(id)+"-preferences.json")).u8string());
      // Keep each complete observation once in its original bounded case file.
      // The versioned parent contains identities and paths, not another copy of
      // every frame, raw descriptor and emitted world point from all 21 cases.
      child.axisPlaneExportUiCase(id,"imgui_cpp",(root/id).u8string());
      const auto path=root/id/"case.json";
      require(path.lexically_normal().parent_path()==root/id &&
              std::filesystem::is_regular_file(std::filesystem::symlink_status(path)) &&
              !std::filesystem::is_symlink(std::filesystem::symlink_status(root/id)),
              "Axis-plane index must refer to its ordinary owned case file");
      const auto size=std::filesystem::file_size(path);
      require(size>0 && size<=maxCaseJson,"Axis-plane case JSON exceeds its unchanged 64MiB limit");
      const auto bytes=readBytes(path.u8string(),maxCaseJson);
      require(bytes.size()==size,"Axis-plane case bytes changed during indexing");
      report["cases"].push_back({{"case_id",id},{"context","imgui_cpp"},
          {"path",std::string(id)+"/case.json"},{"bytes",bytes.size()},{"sha256",sha256(bytes)}});
    }
    report["elapsed_seconds"]=elapsed(start);
    require(elapsed(start)<=aggregateSeconds,"Axis-plane UI aggregate exceeded its time limit");
    report["observation_state"]="observed";
    require(report.dump().size()<=maxIndexJson,"Axis-plane case index exceeds its 64KiB limit");
    writeJson((root/"axis-plane-export-ui.json").u8string(),report);
    std::cout << "21 axis-plane UI cases emitted actual physical-input observations\n";return 0;
  } catch (const std::exception &error) {
    report["observation_state"]="failed";report["failure"]=exceptionJson(error);report["elapsed_seconds"]=elapsed(start);
    try {writeJson((root/"axis-plane-export-ui-failed.json").u8string(),report);} catch (...) {}
    throw;
  }
}
} // namespace imagejxx::desktop
