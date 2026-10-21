// Fixed actual-input rank observations. Independent readers own numerical,
// preservation and workflow assertions. No producer parity/PASS verdict.
#include "rank_ui_probe.hpp"
#include "window.hpp"
#include "headless_session_internal.hpp"
#include "gaussian_cancellation_internal.hpp"
#include "settings_io.hpp"
#include <imagejxx/channel_display.hpp>
#include <imagejxx/rank_command.hpp>
#include <imagejxx/manual_threshold.hpp>
#include <imagejxx/indexed_palette.hpp>
#include <imagejxx/session.hpp>
#include <imgui.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdint>
#include <cstring>
#include <exception>
#include <functional>
#include <iomanip>
#include <map>
#include <mutex>
#include <sstream>
#include <stdexcept>
#include <thread>

namespace imagejxx::desktop {
namespace {
using Clock = std::chrono::steady_clock;
constexpr const char *contract = "p06f.rank-ui.001";
constexpr std::size_t maxFrames = 180;
constexpr std::size_t maxReport = 16u * 1024u * 1024u;
constexpr std::size_t maxArchive = 32u * 1024u * 1024u;
constexpr std::array<const char *, 59> ids{
    "R01", "R02", "R03", "R04", "R05", "R06",
    "R07", "R08", "R09", "R10", "R11", "R12", "R13", "R14", "R15", "R16", "R17", "R18", "R19", "R20", "R21", "R22", "R23", "R24", "R25", "R26", "R27", "R28", "R29", "R30", "R31", "R32", "R33", "R34", "R35", "R36", "R37", "R38", "R39", "R40", "R41", "R42", "R43", "R44", "R45", "R46", "R47", "R48", "R49", "R50", "R51", "R52", "R53", "R54", "R55", "R56", "R57", "R58", "R59"};
bool ieeeNaNsCase(const std::string &id) { return id >= "R52" && id <= "R59"; }
std::string removeNaNsRecipe(const std::string &id) {
  if (id == "R59") return "R50"; // Signaling all-NaN edit, alongside canonical no-op R57.
  return ieeeNaNsCase(id) ? "R" + std::to_string(std::stoi(id.substr(1)) - 7) : id;
}
bool removeNaNsCase(const std::string &id) { return id >= "R45" && id <= "R59"; }
void require(bool value, const char *message) {
  if (!value) throw std::runtime_error(message);
}
std::string address(const void *pointer) {
  std::ostringstream out;
  out << "0x" << std::hex << reinterpret_cast<std::uintptr_t>(pointer);
  return out.str();
}
std::string hex(const std::vector<unsigned char> &bytes) {
  constexpr char digits[] = "0123456789abcdef";
  std::string out;
  out.reserve(bytes.size() * 2);
  for (auto byte : bytes) {
    out += digits[byte >> 4]; out += digits[byte & 15];
  }
  return out;
}
template<class Float, class Word>
std::string floatingHex(const std::vector<double> &values) {
  static_assert(sizeof(Float) == sizeof(Word), "Observation word size");
  std::vector<unsigned char> bytes;
  bytes.reserve(values.size() * sizeof(Float));
  for (const auto sample : values) {
    const auto value = static_cast<Float>(sample);
    Word word; std::memcpy(&word, &value, sizeof(word));
    for (std::size_t i = 0; i < sizeof(word); ++i)
      bytes.push_back(static_cast<unsigned char>(word >> (i * 8)));
  }
  return hex(bytes);
}
const char *dtype(PixelType value) {
  switch (value) {
  case PixelType::UInt8: return "uint8";
  case PixelType::UInt16: return "uint16";
  case PixelType::Float32: return "float32";
  case PixelType::Float64: return "float64";
  }
  throw std::invalid_argument("Unknown rank observation pixel type");
}
std::string declaredHex(const Image &image) {
  if (image.type == PixelType::Float32) {
    std::vector<unsigned char> bytes;bytes.reserve(image.pixels.size()*4);
    for(const auto value:image.pixels) {
      const auto word=float32_source_sample_bits(value,FloatingSamplePolicy::PreserveIEEEFloat32);
      for(int b=0;b<4;++b)bytes.push_back(static_cast<unsigned char>(word>>(8*b)));
    }
    return hex(bytes);
  }
  if (image.type == PixelType::Float64)
    return floatingHex<double, std::uint64_t>(image.pixels);
  std::vector<unsigned char> bytes;
  for (const auto sample : image.pixels) {
    const auto value = static_cast<std::uint16_t>(sample);
    bytes.push_back(static_cast<unsigned char>(value));
    if (image.type == PixelType::UInt16)
      bytes.push_back(static_cast<unsigned char>(value >> 8));
  }
  return hex(bytes);
}
// Only observation identity. This never computes expected filter samples.
std::string sha256(const std::vector<unsigned char> &input) {
  constexpr std::array<std::uint32_t, 64> k{
      0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,
      0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
      0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
      0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
      0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,
      0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
      0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,
      0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2};
  std::array<std::uint32_t, 8> h{
      0x6a09e667,0xbb67ae85,0x3c6ef372,0xa54ff53a,
      0x510e527f,0x9b05688c,0x1f83d9ab,0x5be0cd19};
  auto bytes = input; bytes.push_back(0x80);
  while (bytes.size() % 64 != 56) bytes.push_back(0);
  const auto bits = std::uint64_t(input.size()) * 8;
  for (int n = 56; n >= 0; n -= 8)
    bytes.push_back(static_cast<unsigned char>(bits >> n));
  auto rotate = [](std::uint32_t v, int n) { return (v >> n) | (v << (32 - n)); };
  for (std::size_t offset = 0; offset < bytes.size(); offset += 64) {
    std::array<std::uint32_t, 64> w{};
    for (std::size_t i = 0; i < 16; ++i)
      for (std::size_t j = 0; j < 4; ++j)
        w[i] = (w[i] << 8) | bytes[offset + 4 * i + j];
    for (std::size_t i = 16; i < 64; ++i) {
      const auto a = w[i - 15], b = w[i - 2];
      w[i] = w[i - 16] + (rotate(a,7)^rotate(a,18)^(a>>3)) +
             w[i - 7] + (rotate(b,17)^rotate(b,19)^(b>>10));
    }
    auto work = h;
    for (std::size_t i = 0; i < 64; ++i) {
      const auto a = work[7] + (rotate(work[4],6)^rotate(work[4],11)^rotate(work[4],25)) +
          ((work[4]&work[5])^(~work[4]&work[6])) + k[i] + w[i];
      const auto b = (rotate(work[0],2)^rotate(work[0],13)^rotate(work[0],22)) +
          ((work[0]&work[1])^(work[0]&work[2])^(work[1]&work[2]));
      for (std::size_t j = 7; j > 0; --j) work[j] = work[j - 1];
      work[4] += a; work[0] = a + b;
    }
    for (std::size_t i = 0; i < 8; ++i) h[i] += work[i];
  }
  std::ostringstream out; out << std::hex << std::setfill('0');
  for (auto value : h) out << std::setw(8) << value;
  return out.str();
}
Json calibrationJson(const SpatialCalibration &v) {
  return {{"spacing",v.spacing},{"origin",v.origin},{"direction",v.direction},
          {"unit",v.unit},{"time_unit",v.time_unit},
          {"frame_interval",v.frame_interval},{"time_origin",v.time_origin}};
}
Json displayJson(const DisplayState &v) {
  return {{"t",v.t},{"c",v.c},{"crosshair",v.crosshair},{"orthogonal",v.orthogonal},
          {"linked_zoom",v.linkedZoom},{"zoom",v.zoom},{"plane_zoom",v.planeZoom},
          {"pan",v.pan},{"revision",v.revision},{"properties",metadataJson(v.properties)}};
}
Json usageJson(const JobUsage &v) {
  return {{"running",v.running},{"queued",v.queued},{"running_bytes",v.running_bytes},
          {"retained_bytes",v.retained_bytes},{"reserved_bytes",v.reserved_bytes}};
}
Json jobJson(const JobInfo &v) {
  return {{"id",v.id},{"document_id",v.document_id},{"label",v.label},
          {"state_code",static_cast<int>(v.state)},{"error",v.error},
          {"cancel_requested",v.cancel_requested},{"resource_bytes",v.resource_bytes},
          {"completed",v.completed},{"total",v.total},{"checkpoints",v.checkpoints},
          {"stage",v.stage},{"stage_completed",v.stage_completed},{"stage_total",v.stage_total}};
}
Json snapshotJson(const std::shared_ptr<imagejxx::Document> &owner, bool full) {
  const auto snap = owner->snapshot();
  const auto &d = snap.data(); const auto shape = d.dataset.shape();
  const auto r = snap.revisions(); const auto h = owner->history();
  Json result{{"document_id",snap.id()},{"shape",shape.dimensions()},
      {"dtype",dtype(d.dataset.pixel_type())},
      {"data_identity",snap.data_identity()?Json(*snap.data_identity()):Json()},
      {"dirty",snap.dirty()},{"undo_steps",h.undo_steps},{"redo_steps",h.redo_steps},
      {"display",displayJson(snap.display())},
      {"revisions",{{"pixels",r.pixels},{"metadata",r.metadata},
                    {"selection",r.selection},{"display",r.display}}}};
  if (!full) return result;
  result["sample_policy"] = d.dataset.sample_policy() == FloatingSamplePolicy::FiniteOnly
      ? "finite_only" : d.dataset.sample_policy() == FloatingSamplePolicy::PreserveIEEEFloat32
      ? "preserve_ieee_float32" : "preserve_quiet_nan_float32";
  result["history"] = {{"undo_steps",h.undo_steps},{"redo_steps",h.redo_steps},
      {"step_limit",h.step_limit},{"pixel_budget",h.pixel_budget},
      {"pixels",{{"logical_bytes",h.pixels.logical_bytes},{"current_bytes",h.pixels.current_bytes},
        {"retained_bytes",h.pixels.retained_bytes},{"obsolete_bytes",h.pixels.obsolete_bytes},
        {"retained_planes",h.pixels.retained_planes},{"exact",h.pixels.exact}}}};
  if(d.dataset.sample_policy()==FloatingSamplePolicy::PreserveQuietNaNFloat32)result["sample_policy"]="preserve_quiet_nan_float32";
  result["planes"] = Json::array();
  for (int t=0;t<shape.t;++t) for (int c=0;c<shape.c;++c) for (int z=0;z<shape.z;++z) {
    const auto plane = d.dataset.plane({t,c,z});
    result["planes"].push_back({{"position",{t,c,z}},
        {"storage_f64_hex",floatingHex<double,std::uint64_t>(plane->pixels)},
        {"declared_hex",declaredHex(*plane)},
        {"image_address",address(plane.get())},{"buffer_address",address(plane->pixels.data())}});
  }
  result["calibration"] = calibrationJson(d.dataset.calibration());
  result["metadata"] = {{"title",d.metadata.title},{"properties",metadataJson(d.metadata.properties)},
      {"extensions",metadataJson(d.metadata.extensions)},{"record_layout",metadataJson(d.metadata.record_layout)}};
  result["selection"] = d.selection ? selectionJson(*d.selection) : Json();
  result["previous_selection"] = d.previous_selection ? selectionJson(*d.previous_selection) : Json();
  result["rois"] = Json::array();
  for (const auto &roi : d.rois)
    result["rois"].push_back({{"name",roi.name},{"selection",selectionJson(roi.selection)},
        {"t",roi.t?Json(*roi.t):Json()},{"c",roi.c?Json(*roi.c):Json()},
        {"z",roi.z?Json(*roi.z):Json()},{"attributes",metadataJson(roi.attributes)}});
  result["overlays"] = Json::array();
  for (const auto &overlay : d.overlays) result["overlays"].push_back(metadataJson(overlay));
  result["spectrum_present"] = d.spectrum.has_value();
  return result;
}
Selection ringMask() {
  std::vector<unsigned char> bytes(4,0);
  for (int y=1;y<4;++y) for (int x=1;x<4;++x)
    if (x!=2 || y!=2) {
      const auto index = std::size_t(y*5+x);
      bytes[index/8] |= static_cast<unsigned char>(1u << (7-index%8));
    }
  return Selection::from_packed_mask(5,5,std::move(bytes));
}
DocumentData fixture(PixelType type, bool scientific, bool constant, bool positioned) {
  const DatasetShape shape{scientific?2:1,scientific?2:1,3,5,5,1};
  Stack planes;
  for (int t=0;t<shape.t;++t) for (int c=0;c<shape.c;++c) for (int z=0;z<shape.z;++z) {
    Image image(5,5,1,type);
    for (int y=0;y<5;++y) for (int x=0;x<5;++x)
      image.at(x,y) = constant ? 7 : 10*t+20*c+30*z+(x==2&&y==2?80:0);
    planes.push_back(std::move(image));
  }
  SpatialCalibration cal;
  if (scientific) {
    cal.spacing={.5,1.25,2.5}; cal.origin={11,-7,19}; cal.unit="um";
    cal.frame_interval=.75; cal.time_origin=2.25;
  }
  DocumentData data{scientific ? Dataset::from_planes(shape,std::move(planes),cal)
                               : Dataset::from_stack(std::move(planes),cal)};
  data.metadata.title="Owned rank spike80 source";
  data.metadata.properties["rank_source_marker"]="retained";
  data.metadata.properties["rank_nested"] = metadata_array({true,std::int64_t(7),"retained"});
  data.selection=Selection("rectangle",{{1,1},{4,4}});
  data.previous_selection=Selection("line",{{0,0},{1,1}});
  data.rois.push_back({"marker",Selection("rectangle",{{0,0},{1,1}}),
      std::nullopt,std::nullopt,0,{{"retained",true}}});
  if (positioned) {
    data.rois.clear();
    data.rois.push_back({"positioned ring",ringMask(),1,std::nullopt,2,{{"retained",true}}});
  }
  // A drawable kind is required by the shared overlay renderer. Keep the
  // unrelated opaque attributes, and position the marker away from the fixed
  // numerical readbacks instead of introducing an unsupported overlay kind.
  data.overlays.push_back({{"kind","rectangle"},
      {"points",metadata_array({metadata_array({0,0}),metadata_array({1,1})})},
      {"t",0},{"c",0},{"z",2},{"slice_index",2},{"version",37},
      {"opaque",metadata_array({3,1,4})}});
  return data;
}
struct Gate {
  std::mutex mutex; std::condition_variable changed;
  bool entered=false, released=false;
  void block() {
    std::unique_lock<std::mutex> lock(mutex); entered=true; changed.notify_all();
    if (!changed.wait_for(lock,std::chrono::seconds(5),[&]{return released;}))
      throw std::runtime_error("Rank declared blocker exceeded5seconds");
  }
  void await() {
    std::unique_lock<std::mutex> lock(mutex);
    require(changed.wait_for(lock,std::chrono::seconds(5),[&]{return entered;}),
            "Rank declared blocker did not enter");
  }
  void release() { std::lock_guard<std::mutex> lock(mutex); released=true; changed.notify_all(); }
};

class Driver {
public:
  Driver(Window &,std::string,std::string,std::filesystem::path);
  Json run();
private:
  Window &window_; std::string id_; std::filesystem::path directory_;
  Clock::time_point started_=Clock::now();
  std::shared_ptr<JobScheduler> scheduler_; Application &app_;
  std::shared_ptr<imagejxx::Document> source_,other_;
  std::map<std::string,std::shared_ptr<imagejxx::Document>> owners_;
  std::shared_ptr<Gate> gate_; std::optional<Job> blocker_;
  Json report_,last_; std::size_t archiveBytes_=0;
  double seconds() const { return std::chrono::duration<double>(Clock::now()-started_).count(); }
  void frame(HeadlessInputSession &,const std::string &,const Json & = Json::array(),bool=false);
  void checkpoint(HeadlessInputSession &,const std::string &);
  Json collection(bool) const;
  Json control(const std::string &) const;
  std::size_t click(HeadlessInputSession &,const std::string &,bool=false);
  void text(HeadlessInputSession &,const std::string &,const std::string &,const std::string &);
  void key(HeadlessInputSession &,const std::string &,const std::string &);
  void method(HeadlessInputSession &,const std::string &);
  void open(HeadlessInputSession &,bool=false);
  void configure(HeadlessInputSession &,const std::string &,const std::string & = "0");
  void preview(HeadlessInputSession &,const std::string &);
  void apply(HeadlessInputSession &,const std::string &,const std::string & = "");
  void settled(HeadlessInputSession &,const std::string &);
  void await(HeadlessInputSession &,const std::string &,const std::function<bool()> &);
  bool api(const std::string &,const std::function<void(Json &)> &);
  void activate(const std::shared_ptr<imagejxx::Document> &);
  std::shared_ptr<imagejxx::Document> add(const std::string &,PixelType,bool,bool=false,bool=false);
  void navigate(HeadlessInputSession &,const std::string &,int,int,int,bool);
  void cleanup();
  void write(const std::string &,const Json &);
  void retainFile(const std::string &,const std::filesystem::path &);
  void observedPublication(const std::string &,std::size_t,const Json &);
  void ordinary(HeadlessInputSession &);
  void composite(HeadlessInputSession &);
  void linked(HeadlessInputSession &);
  void ranged(HeadlessInputSession &);
  void legacy(HeadlessInputSession &);
  void memory(HeadlessInputSession &);
  void newDocument(HeadlessInputSession &);
  void replay(HeadlessInputSession &);
  void supersession(HeadlessInputSession &);
  void unchanged(HeadlessInputSession &);
  void nonlandingRanges(HeadlessInputSession &);
  void rangePanelInView(HeadlessInputSession &);
  void observeRangeInput(const std::string &);
};

Driver::Driver(Window &window,std::string id,std::string context,std::filesystem::path directory)
    :window_(window),id_(std::move(id)),directory_(std::move(directory)),
     scheduler_(GaussianCancellationAccess::scheduler(window)),
     app_(GaussianCancellationAccess::application(window)) {
  report_={{"schema_version",1},{"contract",contract},{"case_id",id_},{"context",std::move(context)},
      {"acceptance_claim",false},{"observation_state","running"},
      {"observation_scope","actual ImGui input and CPU cache/upload-input values; no GPU or OS input"},
      {"limits",{{"max_frames",maxFrames},{"max_seconds",15},
          {"max_report_bytes",maxReport},{"max_archive_bytes",maxArchive},{"max_documents",16}}},
      {"frames",Json::array()},{"states",Json::object()},
      {"control_observations",Json::array()},{"api_observations",Json::array()},
      {"publication_observations",Json::array()},{"drag_observations",Json::array()},
      {"fixture_sources",Json::object()},{"fixture_recipes",Json::object()},
      {"files",Json::object()},{"lifecycle",Json::array()}};
  window_.updateSettings({{"processing_workers",1},{"processing_memory_mb",64}},false);
  const bool scientific=removeNaNsRecipe(id_)=="R47"||removeNaNsRecipe(id_)=="R48"||id_=="R40"||id_=="R41"||id_=="R02"||id_=="R03"||id_=="R04"||id_=="R09"||id_=="R13"||id_=="R23"||id_=="R24"||id_=="R25"||id_=="R26"||id_=="R29"||id_=="R34"||id_=="R37";
  const auto type=removeNaNsCase(id_)||id_=="R44"||id_=="R40"||id_=="R41"||id_=="R03"||id_=="R16"||id_=="R18"||id_=="R20"||id_=="R22"||id_=="R24"||id_=="R26"||id_=="R29"||id_=="R34"||id_=="R37"?PixelType::Float32:
      id_=="R39"||id_=="R43"||id_=="R02"||id_=="R04"||id_=="R06"||id_=="R09"||id_=="R10"||id_=="R13"||id_=="R15"||id_=="R17"||id_=="R19"||id_=="R21"||id_=="R23"||id_=="R25"||id_=="R28"||id_=="R31"||id_=="R33"||id_=="R36"?PixelType::UInt16:PixelType::UInt8;
  source_=add("source",type,scientific,id_=="R44"||id_=="R12"||id_=="R17"||id_=="R18"||id_=="R21"||id_=="R22"||id_=="R25"||id_=="R26",id_=="R04");
  if (id_=="R12") other_=add("empty",PixelType::UInt8,true,false,true);
  activate(source_);
  window_.recording=true;
  report_["source_document_id"]=source_->id;
  for (const auto &[label,owner]:owners_)
    report_["fixture_sources"][label]=snapshotJson(owner,true);
  report_["api_observations"].push_back({{"label","fixed_fixture_preparation"},
      {"role","explicit API fixture/history/display setup, no physical-input credit"},
      {"accepted",true},{"methods",{"addNativeDocument","setNativeDisplay","set_property/undo/mark_saved",
          "updateSettings","setActiveIndex","recording"}}});
}
std::shared_ptr<imagejxx::Document> Driver::add(const std::string &label,PixelType type,
                                              bool scientific,bool constant,bool positioned) {
  auto data=fixture(type,scientific,constant,positioned);
  if(id_=="R44")data.selection=Selection("polygon",{{0,0},{5,0},{0,5}});
  if(id_=="R17"||id_=="R18"||id_=="R21"||id_=="R22"||id_=="R25"||id_=="R26")data.selection.reset();
  if(id_=="R14"){
    IndexedPalette palette;palette.color_count=256;
    for(int i=0;i<256;++i)palette.palette[i]={static_cast<unsigned char>((i&1)*255),static_cast<unsigned char>(((i>>1)&1)*255),static_cast<unsigned char>(((i>>2)&1)*255)};
    data.metadata.extensions[indexed_palette_extension]=metadata_object(encode_indexed_palette(palette));
  }
  if(id_=="R30"||id_=="R35"||id_=="R42"||id_=="R44"){data.dataset=Dataset::from_planes({1,1,1,5,5,1},Stack{*data.dataset.plane({0,0,0})});data.overlays[0]["z"]=0;data.overlays[0]["slice_index"]=0;}
  if(removeNaNsCase(id_)) {
    auto shape=data.dataset.shape();if(removeNaNsRecipe(id_)=="R50"||removeNaNsRecipe(id_)=="R51")shape.z=1;
    Stack planes;
    const auto policy=ieeeNaNsCase(id_)?FloatingSamplePolicy::PreserveIEEEFloat32:FloatingSamplePolicy::PreserveQuietNaNFloat32;
    for(int t=0;t<shape.t;++t)for(int c=0;c<shape.c;++c)for(int z=0;z<shape.z;++z) {
      Image image(5,5,1,PixelType::Float32);image.pixels.assign(25,7.+10*t+20*c+30*z);
      image.at(1,1)=-0.;
      image.at(2,1)=float32_source_sample(ieeeNaNsCase(id_)?0x7f800002u:0x7fc00002u,policy);
      image.at(2,2)=float32_source_sample(ieeeNaNsCase(id_)?0xff812345u:0xffc12345u,policy);
      image.at(4,0)=float32_source_sample(ieeeNaNsCase(id_)?0x7f800001u:0x7fc00001u,policy);
      if(ieeeNaNsCase(id_)) {
        image.at(0,0)=float32_source_sample(0x7f800000u,policy);
        image.at(4,4)=float32_source_sample(0xff800000u,policy);
      }
      if(removeNaNsRecipe(id_)=="R51")image.at(3,3)=float32_source_sample(ieeeNaNsCase(id_)?0xff800003u:0xffc00003u,policy);
      if(removeNaNsRecipe(id_)=="R50")image.pixels.assign(25,float32_source_sample(id_=="R59"?0xff800001u:0x7fc00000u,policy));
      planes.push_back(std::move(image));
    }
    data.dataset=Dataset::from_planes(shape,std::move(planes),data.dataset.calibration(),policy);
    if(removeNaNsRecipe(id_)=="R50")data.selection.reset();
    if(removeNaNsRecipe(id_)=="R51")data.selection=Selection("polygon",{{0,0},{5,0},{0,5}});
    if(shape.z==1){data.overlays[0]["z"]=0;data.overlays[0]["slice_index"]=0;}
  }
  auto owner=std::make_shared<imagejxx::Document>(std::move(data));
  auto edit=owner->snapshot().begin_edit();
  edit.set_property("rank_redo_probe",true); owner->commit(edit); owner->undo(); owner->mark_saved();
  window_.addNativeDocument(owner);
  auto display=owner->snapshot().display();
  display.t=scientific?1:0;display.c=scientific?1:0;display.crosshair={2,2,1};
  if(id_=="R30"||id_=="R35"||id_=="R42"||id_=="R44")display.crosshair[2]=0;
  if(removeNaNsCase(id_) && owner->snapshot().data().dataset.shape().z==1)display.crosshair[2]=0;
  display.properties["display_range"]=metadata_array({0.,255.});
  if(removeNaNsCase(id_)) {
    display.properties["display_range"]=metadata_array({7.,127.});
    set_manual_threshold_state(display,owner->snapshot().data().dataset.shape(),{{4.,8.},{scientific?1:0,scientific?1:0,display.crosshair[2]}});
  }
  if(id_=="R44")display.properties["display_range"]=metadata_array({7.,127.});
  if (id_=="R14"||id_=="R15"||id_=="R16"||id_=="R19"||id_=="R20")display.properties["display_range"]=metadata_array({1.,127.});
  if (id_=="R17"||id_=="R18"||id_=="R21"||id_=="R22")display.properties["display_range"]=metadata_array({7.,7.});
  if (id_=="R19"||id_=="R20"||id_=="R21"||id_=="R22")
    set_manual_threshold_state(display,owner->snapshot().data().dataset.shape(),{{4.,8.},{0,0,1}});
  if (id_=="R23"||id_=="R24"||id_=="R25"||id_=="R26") {
    ChannelDisplayState state;
    state.mode=id_=="R24"?ChannelDisplayMode::Color:id_=="R25"?ChannelDisplayMode::Grayscale:ChannelDisplayMode::Composite;
    if(id_!="R23"){state.version=ChannelDisplayVersion::V2;state.projection=id_=="R24"?ChannelDisplayProjection::Max:ChannelDisplayProjection::Min;}
    state.channels.push_back({true,{0.,255.},{"Red",{}}});
    state.channels.push_back({true,id_=="R25"||id_=="R26"?std::array<double,2>{7.,7.}:std::array<double,2>{1.,127.},{"Green",{}}});
    display=with_channel_display(display,owner->snapshot().data().dataset,state);
    set_manual_threshold_state(display,owner->snapshot().data().dataset.shape(),{{4.,8.},{1,1,1}});
  }
  if (id_=="R02") {
    ChannelDisplayState state;
    state.channels.push_back({true,{0.,255.},{"Red",{}}});
    state.channels.push_back({true,{0.,255.},{"Green",{}}});
    display=with_channel_display(display,owner->snapshot().data().dataset,state);
  }
  if(id_=="R39"||id_=="R43"||id_=="R33"||id_=="R36")display.properties["lut"]=std::string("Inverted");
  if(id_=="R40"||id_=="R41"||id_=="R34"||id_=="R37"){
    ChannelDisplayState state;state.mode=ChannelDisplayMode::Color;state.version=ChannelDisplayVersion::V2;state.projection=ChannelDisplayProjection::Max;
    state.channels.push_back({true,{0,255},{"Red",{}}});
    ChannelDisplayTable table{};for(int i=0;i<256;i++)table[i]={std::uint8_t(255-i),std::uint8_t(255-i),std::uint8_t(255-i)};
    state.channels.push_back({true,{0,255},{"",table}});display=with_channel_display(display,owner->snapshot().data().dataset,state);
    set_manual_threshold_state(display,owner->snapshot().data().dataset.shape(),{{4.,8.},{1,1,1}});
  }
  window_.setNativeDisplay(std::move(display),owner->id);
  owners_.emplace(label,owner);
  report_["fixture_recipes"][label]={{"recipe",ieeeNaNsCase(id_)?"base7+10*t+20*c+30*z; -0 at1,1; SNaNs 7f800002 at2,1, ff812345 at2,2, 7f800001 at4,0; +Inf at0,0; -Inf at4,4; R57 all7fc00000; R58 ff800003 at3,3 outside triangle; R59 allff800001":removeNaNsCase(id_)?"base7+10*t+20*c+30*z; -0 at1,1; quiet NaNs 7fc00002 at2,1, ffc12345 at2,2, 7fc00001 at4,0; R50 all7fc00000; R51 ffc00003 at3,3 outside triangle":constant?"constant7":"10*t+20*c+30*z+(x==2&&y==2?80:0)"},
      {"axes",scientific?"TCZYXS":"ZYXS"},{"shape",owner->snapshot().data().dataset.shape().dimensions()},
      {"dtype",dtype(type)},{"positioned_ring",positioned}};
  return owner;
}
void Driver::activate(const std::shared_ptr<imagejxx::Document> &owner) {
  const auto &docs=window_.documents();
  const auto found=std::find_if(docs.begin(),docs.end(),[&](const auto &d){return d->id==owner->id;});
  require(found!=docs.end(),"Rank source is not attached");
  window_.setActiveIndex(static_cast<int>(found-docs.begin()));
}
Json Driver::collection(bool full) const {
  Json docs=Json::array(),facades=Json::array(),jobs=Json::array();
  for (const auto &facade:window_.documents()) {
    facades.push_back(facade->id);
    docs.push_back(snapshotJson(window_.nativeDocument(facade->id),full));
  }
  for (const auto &job:window_.jobs()) jobs.push_back(jobJson(job));
  return {{"documents",std::move(docs)},{"document_ids",app_.document_ids()},
      {"facade_ids",std::move(facades)},{"active_document_id",app_.active_document_id()},
      {"workflow",window_.workflow},{"results",window_.results},{"recording",window_.recording},
      {"busy",window_.busy()},{"jobs",std::move(jobs)},{"usage",usageJson(scheduler_->usage())},
      {"settings",settingsJson(window_.settings())},{"window_error",window_.error()}};
}
void Driver::frame(HeadlessInputSession &session,const std::string &label,const Json &events,bool readbacks) {
  require(report_["frames"].size()<maxFrames&&seconds()<15.,"Rank UI exceeded180frames/15seconds");
  last_=session.frame(events);
  require(last_.at("input_frame").at("raw_inputs")==events,"Rank physical input order was not observed");
  auto rendered=last_;
  if (!readbacks) {rendered.erase("ordinary_pixels");rendered.erase("orthogonal_planes");}
  report_["frames"].push_back({{"ordinal",report_["frames"].size()+1},
      {"label",label},{"requested_events",events},{"rendered",std::move(rendered)},
      {"native_state",collection(false)}});
  if (!window_.error().empty()||last_.at("error_modal").value("open",false))
    throw std::runtime_error("Unexpected Window error during rank frame "+label+": "+window_.error());
  require(window_.documents().size()<=16&&seconds()<=15.,"Rank UI document/time bound exceeded");
}
void Driver::checkpoint(HeadlessInputSession &session,const std::string &label) {
  require(!report_["states"].contains(label),"Rank checkpoint label duplicated");
  frame(session,label,Json::array(),true);
  auto state=collection(true);state["frame_ordinal"]=report_["frames"].size();
  report_["states"][label]=std::move(state);
}
Json Driver::control(const std::string &selector) const {
  auto panelControl=[&](const Json &panel,const std::string &name) {
    auto item=panel.at("control_states").at(name);
    item["point"]=panel.at("controls").at(name);item["rect"]=panel.at("control_rects").at(name);
    return item;
  };
  if (selector.rfind("rank.",0)==0)return panelControl(last_.at("rank_panel"),selector.substr(5));
  if (selector.rfind("gaussian.",0)==0)return panelControl(last_.at("gaussian_panel"),selector.substr(9));
  if (selector.rfind("target.",0)==0)
    return panelControl(last_.at("rank_panel").at("operation_target"),selector.substr(7));
  if (selector.rfind("dialog.",0)==0)return last_.at("dialog").at("controls").at(selector.substr(7));
  if (selector.rfind("error.",0)==0)return last_.at("error_modal").at("controls").at(selector.substr(6));
  if (selector.rfind("jobs.cancel.",0)==0) {
    const auto id=std::stoull(selector.substr(12));
    for (const auto &job:last_.at("jobs_panel").at("jobs"))
      if (job.at("id")==id)return job.at("cancel");
    throw std::runtime_error("Rank actual Jobs cancel control absent");
  }
  if (selector.rfind("document.",0)==0) {
    const auto id=std::stoull(selector.substr(9));
    for (const auto &tab:last_.at("document_tabs"))if(tab.at("document_id")==id)return tab;
    throw std::runtime_error("Rank actual document tab absent");
  }
  if (selector=="orthogonal.toggle") {
    const auto &v=last_.at("orthogonal_navigation");
    return {{"id",v.at("toggle_id")},{"point",v.at("toggle_point")},{"rect",nullptr},
        {"enabled",true},{"geometry_source","actual_toggle_point"}};
  }
  return last_.at("host_controls").at(selector);
}
std::size_t Driver::click(HeadlessInputSession &s,const std::string &selector,bool disabled) {
  const auto item=control(selector);
  require(item.is_object()&&item.at("id").get<std::uint64_t>()!=0,"Rank widget identity missing");
  require(item.at("enabled").get<bool>()==!disabled,"Rank widget admission differs from declared input");
  const auto &p=item.at("point"); const double x=std::floor(p[0].get<double>()),y=std::floor(p[1].get<double>());
  const auto &io=ImGui::GetIO();
  require(std::isfinite(x)&&std::isfinite(y)&&x>=0&&y>=0&&x<io.DisplaySize.x&&y<io.DisplaySize.y,
          "Rank actual pointer lies outside headless viewport");
  const auto fits=[&](const Json &value) {
    const auto &r=value.at("rect");
    return r.is_null()||(x>=r[0].get<double>()&&x<r[2].get<double>()&&
                       y>=r[1].get<double>()&&y<r[3].get<double>());
  };
  require(fits(item),"Rank actual point lies outside control rectangle");
  report_["control_observations"].push_back({{"selector",selector},{"control",item},
      {"position",{x,y}},{"disabled_attempt",disabled},
      {"before_frame_ordinal",report_["frames"].size()}});
  frame(s,selector+".position",Json::array({{{"type","position"},{"space","screen"},{"point",{x,y}}}}));
  const auto current=control(selector);
  require(current.at("id")==item.at("id")&&current.at("enabled")==item.at("enabled")&&fits(current),
          "Rank widget moved or changed before actual button down");
  frame(s,selector+".down",Json::array({{{"type","button"},{"name","left"},{"down",true}}}));
  frame(s,selector+".up",Json::array({{{"type","button"},{"name","left"},{"down",false}}}));
  return report_["frames"].size();
}
void Driver::key(HeadlessInputSession &s,const std::string &name,const std::string &label) {
  for (bool down:{true,false})frame(s,label+(down?".down":".up"),
      Json::array({{{"type","key"},{"name",name},{"down",down}}}));
}
void Driver::text(HeadlessInputSession &s,const std::string &selector,const std::string &value,const std::string &label) {
  click(s,selector); const auto id=control(selector).at("id");
  require(last_.at("input_frame").at("focus").at("active_id")==id&&
          last_.at("input_frame").at("input_text").at("id")==id,"Rank text input did not own focus");
  for (bool down:{true,false})frame(s,label+(down?".select_all.down":".select_all.up"),
      Json::array({{{"type","keys"},{"keys",Json::array({
          {{"name","Ctrl"},{"down",down}},{{"name","A"},{"down",down}}})}}}));
  if (value.empty())key(s,"Backspace",label+".clear");
  else frame(s,label+".text",Json::array({{{"type","text"},{"value",value}}}));
  if (selector.rfind("target.",0)==0)key(s,"Enter",label+".commit");
  frame(s,label+".settled");
}
void Driver::method(HeadlessInputSession &s,const std::string &name) {
  click(s,"rank.method");frame(s,"rank.method.combo_settled");
  click(s,"rank.method."+name);frame(s,"rank.method.selected");
}
bool Driver::api(const std::string &label,const std::function<void(Json &)> &body) {
  Json row{{"label",label},{"role","explicit API/action preparation or refusal; no widget-input credit"},
      {"before_frame_ordinal",report_["frames"].size()},{"before",collection(false)},
      {"accepted",false}};
  try {body(row);row["accepted"]=true;}
  catch (const std::exception &e) {row["error"]=e.what();}
  row["after"]=collection(false);const auto accepted=row["accepted"].get<bool>();
  report_["api_observations"].push_back(std::move(row));return accepted;
}
void Driver::open(HeadlessInputSession &s,bool gaussian) {
  require(api(gaussian?"gaussian.open_preparation":"rank.open_preparation",[&](Json &row){
    row["action"]="show_operation";row["operation"]=gaussian?"gaussian_blur_imagej":"rank_filter_imagej";
    window_.action("show_operation",{{"operation",gaussian?"gaussian_blur_imagej":"rank_filter_imagej"}});
  }),"Rank/Gaussian opening preparation failed");
  frame(s,gaussian?"gaussian.opened":"rank.opened");frame(s,"panel.layout_settled");
}
void Driver::configure(HeadlessInputSession &s,const std::string &name,const std::string &radius) {
  method(s,name);text(s,"rank.radius",radius,"rank.radius");
}
void Driver::await(HeadlessInputSession &s,const std::string &label,const std::function<bool()> &done) {
  do {frame(s,label);std::this_thread::yield();} while(!done());
}
void Driver::preview(HeadlessInputSession &s,const std::string &label) {
  click(s,"rank.preview");
  await(s,label+".wait",[&]{
    const auto &jobs=last_.at("gaussian_jobs");
    return !jobs.at("active_installed_generation").is_null()&&
        jobs.at("active_installed_generation")==last_.at("rank_panel").at("generation");
  });
  checkpoint(s,label);
}
void Driver::settled(HeadlessInputSession &s,const std::string &label) {
  await(s,label+".wait",[&]{return !window_.busy()&&scheduler_->usage().running==0&&scheduler_->usage().queued==0;});
  checkpoint(s,label);
}
void Driver::observedPublication(const std::string &label,std::size_t ordinal,const Json &before) {
  report_["publication_observations"].push_back({{"label",label},{"trigger","physical"},
      {"confirm_frame_ordinal",ordinal},{"state_before",before},{"state_after",collection(false)},
      {"apply_lane",last_.at("gaussian_jobs").at("apply")}});
}
void Driver::apply(HeadlessInputSession &s,const std::string &label,const std::string &stack) {
  const auto before=collection(false);auto ordinal=click(s,"rank.ok");
  if (!stack.empty()) {checkpoint(s,label+".stack_prompt");ordinal=click(s,"rank.stack_"+stack);}
  settled(s,label);observedPublication(label,ordinal,before);
}
void Driver::navigate(HeadlessInputSession &s,const std::string &label,int t,int c,int z,bool linked) {
  require(api(label+".navigation_preparation",[&](Json &row){
    auto display=window_.nativeSnapshot().display();display.t=t;display.c=c;
    display.crosshair[2]=z;display.orthogonal=linked;
    row["position"]=Json::array({t,c,z});row["orthogonal"]=linked;
    window_.setNativeDisplay(std::move(display));
  }),"Rank navigation preparation failed");checkpoint(s,label);
}
void Driver::cleanup() {
  window_.cancel();if(gate_)gate_->release();
  if(blocker_)require(blocker_->wait_for(std::chrono::seconds(5)),"Rank blocker cleanup timed out");
  blocker_.reset();gate_.reset();
  window_.action("wait_jobs",{{"timeout_ms",5000}});window_.poll();
}
void Driver::write(const std::string &name,const Json &value) {
  const auto bytes=value.dump();require(bytes.size()<=maxReport,"Rank report exceeded16MiB");
  require(bytes.size()<=maxArchive-archiveBytes_,"Rank archive exceeded32MiB");
  const auto path=directory_/name;require(!std::filesystem::exists(path),"Rank observation output already exists");
  writeBytes(path.u8string(),std::vector<unsigned char>(bytes.begin(),bytes.end()));archiveBytes_+=bytes.size();
}
void Driver::retainFile(const std::string &label,const std::filesystem::path &path) {
  const auto bytes=readBytes(path.u8string(),maxArchive);
  require(bytes.size()<=maxArchive-archiveBytes_,"Rank native-session archive exceeded32MiB");
  archiveBytes_+=bytes.size();
  report_["files"][label]={{"member",path.filename().u8string()},
      {"byte_count",bytes.size()},{"sha256",sha256(bytes)},{"hex",hex(bytes)}};
}

void Driver::ordinary(HeadlessInputSession &s) {
  open(s);configure(s,"minimum");preview(s,"minimum.preview");
  apply(s,"minimum.applied","no");
  click(s,"menu.Edit");click(s,"menu.Edit.Undo");checkpoint(s,"minimum.undo");
  click(s,"menu.Edit");click(s,"menu.Edit.Redo");checkpoint(s,"minimum.redo");
}
void Driver::composite(HeadlessInputSession &s) {
  open(s);configure(s,"maximum");preview(s,"maximum.preview");
  navigate(s,"maximum.nonactive_channel",1,0,1,false);
  navigate(s,"maximum.returned",1,1,1,false);
  // Apply remains bound to C1 even when another live channel is displayed.
  require(api("maximum.fresh_navigation_preparation",[&](Json &row){
    auto d=source_->snapshot().display();d.c=0;row["position"]={d.t,d.c,d.crosshair[2]};
    window_.setNativeDisplay(std::move(d),source_->id);
  }),"Rank composition navigation failed");
  frame(s,"maximum.fresh_navigation");apply(s,"maximum.applied");
}
void Driver::linked(HeadlessInputSession &s) {
  open(s);configure(s,"median");preview(s,"median.preview");
  navigate(s,"median.linked",1,1,1,true);
  navigate(s,"median.away",1,1,0,true);
  navigate(s,"median.returned",1,1,1,true);
  click(s,"rank.cancel");checkpoint(s,"median.cancelled");
}
void Driver::ranged(HeadlessInputSession &s) {
  open(s);configure(s,"mean");
  click(s,"target.t.all");click(s,"target.c.all");click(s,"target.z.range");
  frame(s,"mean.range.layout_settled");
  text(s,"target.z.first","1","mean.z.first");
  text(s,"target.z.last","3","mean.z.last");
  text(s,"target.z.step","2","mean.z.step");
  click(s,"target.roi.positioned_rois");
  checkpoint(s,"mean.target_ready");
  // This is actual metadata target observation, not an expected filter call.
  const auto target=target_from_record(source_->snapshot(),
      metadataObjectFromJson(last_.at("rank_panel").at("target")),"rank_filter_imagej");
  std::optional<std::size_t> selected;
  Json positions=Json::array();
  for (std::size_t i=0;i<target.position_count();++i) {
    const auto p=target.position(i);positions.push_back({p.t,p.c,p.z});
    if(p.t==1&&p.c==1&&p.z==2)selected=i;
  }
  require(selected.has_value(),"Requested contained rank preview plane not in actual target");
  report_["preview_selection"]={{"resolved_positions",std::move(positions)},
      {"requested_position",{1,1,2}},{"selected_zero_based_ordinal",*selected}};
  text(s,"target.preview.ordinal",std::to_string(*selected+1),"mean.preview.ordinal");
  preview(s,"mean.preview");
  navigate(s,"mean.fresh_navigation",0,0,1,false);
  click(s,"target.preview.show");checkpoint(s,"mean.shown");
  navigate(s,"mean.before_apply",0,0,1,false);
  apply(s,"mean.applied");
}
void Driver::legacy(HeadlessInputSession &s) {
  if(id_=="R05"||id_=="R06") {
    open(s);configure(s,"maximum");
    apply(s,id_=="R05"?"stack.yes":"stack.no",id_=="R05"?"yes":"no");
    return;
  }
  open(s);configure(s,"minimum","2.25");click(s,"rank.ok");
  checkpoint(s,"stack.cancel_prompt");click(s,"rank.stack_cancel");
  checkpoint(s,"stack.cancelled");
  open(s);method(s,"minimum");checkpoint(s,"stack.reopened");click(s,"rank.cancel");
}
void Driver::memory(HeadlessInputSession &s) {
  open(s);configure(s,"minimum","2.25");click(s,"rank.ok");
  frame(s,"memory.minimum.stack_prompt");click(s,"rank.stack_cancel");
  checkpoint(s,"memory.minimum.accepted");
  open(s);configure(s,"maximum","3.5");click(s,"rank.ok");
  frame(s,"memory.maximum.stack_prompt");click(s,"rank.stack_cancel");
  checkpoint(s,"memory.maximum.accepted");
  open(s);method(s,"minimum");checkpoint(s,"memory.minimum.reopened");
  text(s,"rank.radius","1000","memory.minimum.boundary_radius");
  checkpoint(s,"memory.minimum.boundary");
  method(s,"maximum");checkpoint(s,"memory.maximum.reopened");
  text(s,"rank.radius","1000","memory.maximum.boundary_radius");
  checkpoint(s,"memory.maximum.boundary");
  method(s,"mean");checkpoint(s,"memory.mean.default");
  text(s,"rank.radius","1000","memory.mean.boundary_radius");
  checkpoint(s,"memory.mean.boundary");
  method(s,"median");text(s,"rank.radius","100","memory.median.boundary_radius");
  checkpoint(s,"memory.median.boundary");
  text(s,"rank.radius","101","memory.invalid_radius");
  checkpoint(s,"memory.median.invalid");click(s,"rank.ok",true);
  text(s,"rank.radius","-","memory.partial_radius");checkpoint(s,"memory.median.partial");
  key(s,"Escape","memory.partial.escape");frame(s,"memory.escape.settled");
  open(s);checkpoint(s,"memory.median.reopened");
  click(s,"rank.new_document");checkpoint(s,"memory.destination.draft");
  key(s,"Escape","memory.destination.escape");frame(s,"memory.destination.closed");
  open(s);checkpoint(s,"memory.destination.reopened");click(s,"rank.cancel");
}
void Driver::newDocument(HeadlessInputSession &s) {
  open(s);configure(s,"median");click(s,"rank.new_document");
  preview(s,"new.preview");apply(s,"new.applied");
  const auto output=window_.nativeDocument();
  const auto sessionPath=directory_/"rank-new-document.ijx";
  require(api("new.native_save",[&](Json &row){
    row["path"]=sessionPath.filename().u8string();row["saved_owner_id"]=output->id;
    saveNativeSession(sessionPath.u8string(),{window_.document()},window_.results,window_.workflow);
  }),"Rank scientific new-document session save failed");
  retainFile("new_document_session",sessionPath);checkpoint(s,"new.saved");
  require(api("new.native_reopen",[&](Json &row){
    row["path"]=sessionPath.filename().u8string();window_.openFile(sessionPath.u8string());
    other_=window_.nativeDocument();owners_["new.reopened"]=other_;
    row["reopened_owner_id"]=other_->id;
  }),"Rank scientific new-document session reopen failed");
  report_["new_output_owner_id"]=output->id;report_["new_reopened_owner_id"]=other_->id;
  checkpoint(s,"new.reopened");
  require(api("new.close_reopened",[&](Json &row){
    row["closed_owner_id"]=other_->id;window_.closeDocument(other_->id);
    activate(output);
  }),"Rank scientific reopened document close failed");
  checkpoint(s,"new.reopened_closed");
}
void Driver::replay(HeadlessInputSession &s) {
  report_["replay_route_scope"]="ordinary T1/C1/Z3 nativeV3; strict replay API and actual legacy Workflow Run complete AllZ/WholePlane form; no scientific Workflow Run claim";
  Json original;
  require(api("strict_record_preparation",[&](Json &row){
    original=metadataJson(resolve_rank_command(source_->snapshot(),{},
        {RankFilterMethod::Median,0.}).record());
    row["record"]=original;row["source"]=snapshotJson(source_,true);
  }),"Rank strict record preparation failed");
  report_["original_record"]=original;report_["original_owner_id"]=source_->id;
  navigate(s,"replay.before",0,0,0,false);
  require(api("strict_replay",[&](Json &row){
    row["record"]=original;window_.action("rank_replay",{{"record",original}});
  }),"Rank strict replay failed");settled(s,"replay.strict");
  api("strict_repeat_refusal",[&](Json &row){
    row["record"]=original;window_.action("rank_replay",{{"record",original}});
  });checkpoint(s,"replay.repeat_refused");
  const auto sessionPath=directory_/"rank-session.ijx";
  require(api("native_save_preparation",[&](Json &row){
    row["path"]=sessionPath.filename().u8string();
    saveNativeSession(sessionPath.u8string(),window_.documents(),window_.results,window_.workflow);
  }),"Rank native session save failed");retainFile("native_session",sessionPath);
  require(api("native_reopen_preparation",[&](Json &row){
    row["path"]=sessionPath.filename().u8string();window_.openFile(sessionPath.u8string());
    other_=window_.nativeDocument();owners_["reopened"]=other_;
    row["reopened_owner_id"]=other_->id;
  }),"Rank native session reopen failed");report_["reopened_owner_id"]=other_->id;
  checkpoint(s,"session.reopened");
  api("reopened_strict_refusal",[&](Json &row){
    row["record"]=original;window_.action("rank_replay",{{"record",original}});
  });checkpoint(s,"replay.reopened_refused");
  click(s,"workflow.run");frame(s,"replay.rebind.layout_settled");checkpoint(s,"replay.rebind_opened");
  click(s,"dialog.processing_scope");frame(s,"replay.scope.combo_settled");
  click(s,"dialog.processing_scope.choice.All Z slices");
  click(s,"dialog.region");frame(s,"replay.region.combo_settled");
  click(s,"dialog.region.choice.Whole plane");checkpoint(s,"replay.rebind_values");
  const auto before=collection(false);const auto ordinal=click(s,"dialog.apply");
  settled(s,"replay.rebound");
  report_["publication_observations"].push_back({{"label","replay.rebound"},{"trigger","physical"},
      {"confirm_frame_ordinal",ordinal},{"state_before",before},{"state_after",collection(false)},
      {"workflow_replay",last_.value("workflow_replay",Json())}});
  auto malformed=original;malformed["schema_version"]=true;
  api("malformed_original_rebind_refusal",[&](Json &row){
    row["record"]=malformed;
    const Json request{{"scope","all_z"},{"roi","whole_plane"},{"mode","planes"},{"destination","in_place"}};
    row["complete_request"]=request;
    window_.action("rank_replay",{{"record",malformed},{"rank_rebind",request}});
  });checkpoint(s,"replay.malformed_refused");
}
void Driver::supersession(HeadlessInputSession &s) {
  open(s);configure(s,"minimum");preview(s,"supersession.rank_ready");
  click(s,"menu.Process");click(s,"menu.Process.Filters");
  click(s,"menu.command.gaussian_blur_imagej");frame(s,"supersession.gaussian.layout_settled");
  checkpoint(s,"supersession.gaussian_open");click(s,"gaussian.preview");
  await(s,"supersession.gaussian.wait",[&]{
    const auto &j=last_.at("gaussian_jobs");
    return !j.at("active_installed_generation").is_null()&&
        j.at("active_installed_generation")==last_.at("gaussian_panel").at("generation");
  });checkpoint(s,"supersession.gaussian_ready");
  click(s,"menu.Process");click(s,"menu.Process.Filters");
  click(s,"menu.command.rank_filter_imagej");frame(s,"supersession.rank.layout_settled");
  configure(s,"minimum");preview(s,"supersession.rank_again");click(s,"rank.cancel");
  open(s);configure(s,"minimum");
  require(api("busy_worker_preparation",[&](Json &row){
    gate_=std::make_shared<Gate>();
    JobOptions options;options.label="owned rank UI blocker";options.resource_bytes=1;
    blocker_=scheduler_->submit([gate=gate_](const JobContext &){gate->block();},options);
    gate_->await();row["blocker_job"]=jobJson(blocker_->info());
  }),"Rank declared worker preparation failed");
  click(s,"rank.ok");frame(s,"busy.stack_prompt");click(s,"rank.stack_no");checkpoint(s,"busy.queued");
  click(s,"menu.Process",true);checkpoint(s,"busy.disabled");
  require(api("jobs_open_preparation",[&](Json &){window_.action("show_jobs");}),"Rank Jobs opening failed");
  frame(s,"busy.jobs.layout_settled");checkpoint(s,"busy.jobs_open");
  const auto &lane=last_.at("gaussian_jobs").at("apply");
  require(lane.contains("job")&&!lane.at("job").is_null(),"Rank queued Apply lacks actual job identity");
  const auto job=lane.at("job").at("id").get<std::uint64_t>();report_["queued_apply_job_id"]=job;
  click(s,"jobs.cancel."+std::to_string(job));
  require(api("busy_worker_release",[&](Json &row){
    gate_->release();require(blocker_->wait_for(std::chrono::seconds(5)),"Rank blocker release timed out");
    row["blocker_job"]=jobJson(blocker_->info());blocker_.reset();gate_.reset();
  }),"Rank blocker release failed");settled(s,"busy.cancelled");
}
void Driver::unchanged(HeadlessInputSession &s) {
  open(s);configure(s,"minimum");apply(s,"unchanged.applied","no");
  require(api("positioned_empty_owner_preparation",[&](Json &row){
    activate(other_);row["owner_id"]=other_->id;
  }),"Rank empty target activation failed");checkpoint(s,"empty.initial");
  open(s);configure(s,"minimum");click(s,"target.roi.positioned_rois");apply(s,"empty.applied");
}

void Driver::rangePanelInView(HeadlessInputSession &s) {
  // Only R13 expands all three axes. Use actual title-bar input when the
  // existing fixed viewport cannot expose its action controls at the old Y.
  const auto before=last_.at("rank_panel").at("window_drag");
  require(before.is_object()&&before.value("enabled",false)&&before.value("visible",false),
          "R13 requires the observed movable Rank title bar");
  const auto work=before.at("viewport_work_rect");
  const auto inside=[&](const Json &r) {
    return r.is_array()&&r.size()==4&&r[0].get<float>()>=work[0].get<float>()&&
        r[1].get<float>()>=work[1].get<float>()&&r[2].get<float>()<=work[2].get<float>()&&
        r[3].get<float>()<=work[3].get<float>();
  };
  const auto actionsInside=[&] {
    return inside(control("rank.preview").at("rect"))&&
        inside(control("rank.ok").at("rect"))&&inside(control("rank.cancel").at("rect"));
  };
  Json row{{"label","range.panel_viewport"},{"kind","rank_title_bar"},
      {"performed",false},{"move_id",before.at("id")},{"before",before},
      {"before_frame_ordinal",report_["frames"].size()},
      {"source_before",collection(false)},{"input_frame_ordinals",Json::object()}};
  if (!actionsInside()) {
    const auto rect=before.at("window_rect");
    const double x=std::floor(before.at("point")[0].get<double>());
    const double y=std::floor(before.at("point")[1].get<double>());
    const double delta=std::floor(work[1].get<double>()+8.)-rect[1].get<double>();
    const double movedY=y+delta;
    require(before.at("id").get<std::uint64_t>()!=0&&inside(before.at("rect"))&&
        rect[0].get<double>()>=work[0].get<double>()&&rect[2].get<double>()<=work[2].get<double>()&&
        delta<0&&rect[3].get<double>()+delta<=work[3].get<double>()&&
        movedY>=work[1].get<double>()&&movedY<work[3].get<double>(),
        "R13 cannot fit the observed Rank window with one title-bar drag");
    row["performed"]=true;row["position"]={x,y};row["target_position"]={x,movedY};
    bool held=false;
    try {
      frame(s,"range.panel_viewport.position",Json::array({
          {{"type","position"},{"space","screen"},{"point",{x,y}}}}));
      row["input_frame_ordinals"]["position"]=report_["frames"].size();
      const auto positioned=last_.at("rank_panel").at("window_drag");
      require(positioned.at("id")==before.at("id")&&positioned.at("rect")==before.at("rect")&&
          positioned.at("window_rect")==before.at("window_rect"),
          "R13 observed title-bar geometry changed before actual button-down");
      held=true;
      frame(s,"range.panel_viewport.down",Json::array({
          {{"type","button"},{"name","left"},{"down",true}}}));
      row["input_frame_ordinals"]["down"]=report_["frames"].size();
      require(last_.at("input_frame").at("focus").at("active_id")==before.at("id"),
              "R13 title-bar MoveID did not own actual button-down input");
      frame(s,"range.panel_viewport.move",Json::array({
          {{"type","position"},{"space","screen"},{"point",{x,movedY}}}}));
      row["input_frame_ordinals"]["move"]=report_["frames"].size();
      require(last_.at("input_frame").at("focus").at("active_id")==before.at("id"),
              "R13 held title-bar move lost the actual MoveID");
      frame(s,"range.panel_viewport.up",Json::array({
          {{"type","button"},{"name","left"},{"down",false}}}));
      held=false;row["input_frame_ordinals"]["up"]=report_["frames"].size();
      frame(s,"range.panel_viewport.settled");
      row["input_frame_ordinals"]["settled"]=report_["frames"].size();
    } catch (...) {
      if (held) try {frame(s,"range.panel_viewport.aborted_release",Json::array({
          {{"type","button"},{"name","left"},{"down",false}}}));} catch (...) {}
      throw;
    }
    const auto after=last_.at("rank_panel").at("window_drag");
    const auto translated=[&](const char *key) {
      for (std::size_t i=0;i<4;++i)
        if (std::abs(after.at(key)[i].get<double>()-before.at(key)[i].get<double>()-
                     (i%2?delta:0.))>1.) return false;
      return true;
    };
    require(after.at("id")==before.at("id")&&translated("rect")&&translated("window_rect")&&
        !ImGui::GetIO().MouseDown[0]&&
        last_.at("input_frame").at("focus").at("active_id")!=before.at("id"),
        "R13 title-bar release or observed translated geometry changed");
  }
  require(actionsInside(),"R13 Rank action controls remain outside the viewport");
  row["after"]=last_.at("rank_panel").at("window_drag");
  row["after_frame_ordinal"]=report_["frames"].size();
  row["source_after"]=collection(false);
  report_["drag_observations"].push_back(std::move(row));
}

void Driver::observeRangeInput(const std::string &stage) {
  const auto &panel=last_.at("rank_panel");
  const auto target=target_from_record(source_->snapshot(),
      metadataObjectFromJson(panel.at("target")),"rank_filter_imagej");
  Json positions=Json::array();
  for (std::size_t i=0;i<target.position_count();++i) {
    const auto p=target.position(i);positions.push_back({p.t,p.c,p.z});
  }
  // These are actual widget strings/committed diagnostics and native record
  // membership, never producer-generated expected target or filter output.
  report_["range_input_observations"].push_back({{"stage",stage},
      {"frame_ordinal",report_["frames"].size()},
      {"panel_target",panel.at("target")},{"operation_target",panel.at("operation_target")},
      {"resolved_positions",std::move(positions)}});
}

void Driver::nonlandingRanges(HeadlessInputSession &s) {
  report_["range_input_observations"]=Json::array();
  open(s);configure(s,"minimum");
  click(s,"target.t.range");click(s,"target.c.range");click(s,"target.z.range");
  frame(s,"range.layout_settled");rangePanelInView(s);
  // Frozen draft A: one-based First1; Last T1/C1/Z2 with actual Step1.
  // Its two positions distinguish each Last from the complete axis extent.
  for (const auto *axis:{"t","c","z"}) {
    const std::string prefix=std::string("range.")+axis;
    text(s,std::string("target.")+axis+".first","1",prefix+".first");
    text(s,std::string("target.")+axis+".last",std::string(axis)=="z"?"2":"1",
         prefix+".last_limit");
  }
  click(s,"target.roi.whole_plane");
  checkpoint(s,"range.last_limits");observeRangeInput("range.last_limits");
  // Frozen draft B: inclusive Last2 and Step2 on every axis. Last is not
  // reached: each native [0,2,2] range selects index0 only. No API setters.
  text(s,"target.t.last","2","range.t.last_nonlanding");
  text(s,"target.c.last","2","range.c.last_nonlanding");
  for (const auto *axis:{"t","c","z"})
    text(s,std::string("target.")+axis+".step","2",std::string("range.")+axis+".step_nonlanding");
  checkpoint(s,"range.nonlanding_ready");observeRangeInput("range.nonlanding_ready");
  preview(s,"range.preview");
  click(s,"target.preview.show");checkpoint(s,"range.shown");
  apply(s,"range.applied");
  click(s,"menu.Edit");click(s,"menu.Edit.Undo");checkpoint(s,"range.undo");
  click(s,"menu.Edit");click(s,"menu.Edit.Redo");checkpoint(s,"range.redo");
}

Json Driver::run() {
  HeadlessInputSession session(window_);
  report_["lifecycle"].push_back({{"phase","opened"},{"state",session.lifecycle()}});
  try {
    frame(session,"initial.layout");frame(session,"initial.layout_settled");checkpoint(session,"initial");
    if(removeNaNsCase(id_)) {
      click(session,"menu.Process");click(session,"menu.Process.Noise");click(session,"menu.command.remove_nans_imagej");
      text(session,"rank.radius","1","remove_nans.radius");
      if(removeNaNsRecipe(id_)=="R48")click(session,"rank.new_document");
      checkpoint(session,"remove_nans.configured");preview(session,"remove_nans.preview");
      if(removeNaNsRecipe(id_)=="R49") {
        click(session,"rank.ok");checkpoint(session,"remove_nans.stack_prompt");click(session,"rank.stack_cancel");checkpoint(session,"remove_nans.cancelled");
      } else {
        apply(session,"remove_nans.applied",removeNaNsRecipe(id_)=="R45"?"no":removeNaNsRecipe(id_)=="R46"?"yes":"");
        if(removeNaNsRecipe(id_)!="R50" || id_=="R59") {
          click(session,"menu.Edit");click(session,"menu.Edit.Undo");checkpoint(session,"remove_nans.undo");
          click(session,"menu.Edit");click(session,"menu.Edit.Redo");checkpoint(session,"remove_nans.redo");
        }
      }
    }
    else if(id_=="R38"||id_=="R39"||id_=="R40"||id_=="R41"||id_=="R42"||id_=="R43"||id_=="R44") {
      click(session,"menu.Process");click(session,"menu.Process.Filters");click(session,"menu.command.top_hat_imagej");
      text(session,"rank.radius","1","tophat.radius");
      if(id_=="R39"||id_=="R40"||id_=="R41"||id_=="R43")click(session,"rank.light_background");
      if(id_=="R42")click(session,"rank.dont_subtract");
      if(id_=="R41")click(session,"rank.new_document");
      checkpoint(session,"tophat.configured");preview(session,"tophat.preview");
      if(id_=="R43") {
        click(session,"rank.ok");checkpoint(session,"tophat.stack_prompt");click(session,"rank.stack_cancel");checkpoint(session,"tophat.cancelled");
      } else {
        apply(session,"tophat.applied",id_=="R38"?"no":id_=="R39"?"yes":"");
        click(session,"menu.Edit");click(session,"menu.Edit.Undo");checkpoint(session,"tophat.undo");
        click(session,"menu.Edit");click(session,"menu.Edit.Redo");checkpoint(session,"tophat.redo");
      }
    }
    else if(id_=="R32"||id_=="R33"||id_=="R34"||id_=="R35"||id_=="R36"||id_=="R37") {
      click(session,"menu.Process");click(session,"menu.Process.Noise");click(session,"menu.command.remove_outliers_imagej");
      text(session,"rank.radius","1","outliers.radius");
      text(session,"rank.threshold",id_=="R35"?"80":"50","outliers.threshold");
      if(id_=="R33"||id_=="R34"||id_=="R36"||id_=="R37"){click(session,"rank.which");frame(session,"outliers.choice.layout_settled");click(session,"rank.outlier.dark");}
      if(id_=="R37")click(session,"rank.new_document");
      checkpoint(session,"outliers.configured");preview(session,"outliers.preview");
      if(id_=="R36"){
        click(session,"rank.ok");checkpoint(session,"outliers.stack_prompt");click(session,"rank.stack_cancel");checkpoint(session,"outliers.cancelled");
      }else{
        apply(session,"outliers.applied",id_=="R32"?"no":id_=="R33"?"yes":"");
        if(id_!="R35"){
          click(session,"menu.Edit");click(session,"menu.Edit.Undo");checkpoint(session,"outliers.undo");
          click(session,"menu.Edit");click(session,"menu.Edit.Redo");checkpoint(session,"outliers.redo");
        }
      }
    }
    else     if(id_=="R27"||id_=="R28"||id_=="R29"||id_=="R30"||id_=="R31") {
      const auto before=collection(false);
      click(session,"menu.Process");click(session,"menu.Process.Noise");
      const auto ordinal=click(session,"menu.action.despeckle_imagej");
      if(id_=="R29"){
        checkpoint(session,"despeckle.target");preview(session,"despeckle.preview");
        apply(session,"despeckle.applied");
      }else if(id_=="R30"||id_=="R35"||id_=="R42"||id_=="R44"){
        settled(session,"despeckle.applied");observedPublication("despeckle.applied",ordinal,before);
      }else{
        checkpoint(session,"despeckle.stack_prompt");
        const auto choice=click(session,id_=="R28"?"rank.stack_yes":id_=="R31"?"rank.stack_cancel":"rank.stack_no");
        if(id_=="R31")checkpoint(session,"despeckle.cancelled");
        else{settled(session,"despeckle.applied");observedPublication("despeckle.applied",choice,before);}
      }
      if(id_!="R31"){
        click(session,"menu.Edit");click(session,"menu.Edit.Undo");checkpoint(session,"despeckle.undo");
        click(session,"menu.Edit");click(session,"menu.Edit.Redo");checkpoint(session,"despeckle.redo");
      }
    }
    else if(id_=="R14"||id_=="R15"||id_=="R16"||id_=="R17"||id_=="R18"||id_=="R19"||id_=="R20"||id_=="R21"||id_=="R22"||id_=="R23"||id_=="R24"||id_=="R25"||id_=="R26"){
      open(session);configure(session,"variance","0");preview(session,"variance.preview");
      apply(session,"variance.applied",id_=="R23"||id_=="R24"||id_=="R25"||id_=="R26"?"":"no");
      click(session,"menu.Edit");click(session,"menu.Edit.Undo");checkpoint(session,"variance.undo");
      click(session,"menu.Edit");click(session,"menu.Edit.Redo");checkpoint(session,"variance.redo");
    }
    else if(id_=="R01")ordinary(session);
    else if(id_=="R02")composite(session);
    else if(id_=="R03")linked(session);
    else if(id_=="R04")ranged(session);
    else if(id_=="R05"||id_=="R06"||id_=="R07")legacy(session);
    else if(id_=="R08")memory(session);
    else if(id_=="R09")newDocument(session);
    else if(id_=="R10")replay(session);
    else if(id_=="R11")supersession(session);
    else if(id_=="R13")nonlandingRanges(session);
    else unchanged(session);
    checkpoint(session,"final");cleanup();checkpoint(session,"terminal.cleaned");
    session.close();report_["lifecycle"].push_back({{"phase","closed"},{"state",session.lifecycle()}});
    report_["final_usage"]=usageJson(scheduler_->usage());report_["frame_count"]=report_["frames"].size();
    report_["elapsed_seconds"]=seconds();report_["window_error"]=window_.error();
    report_["observation_state"]="observed";write("case.json",report_);
    const auto raw=readBytes((directory_/"case.json").u8string(),maxReport);
    write("case-index.json",{{"schema_version",1},{"contract",contract},{"context",report_["context"]},
        {"case_count",1},{"observation_state","observed"},{"acceptance_claim",false},
        {"cases",Json::array({{{"id",id_},{"member","case.json"},{"byte_count",raw.size()},
                               {"sha256",sha256(raw)}}})}});
    return report_;
  } catch (...) {
    const auto failure=std::current_exception();
    try {cleanup();} catch(const std::exception &e) {report_["cleanup_error"]=e.what();}
    session.close();report_["lifecycle"].push_back({{"phase","closed"},{"state",session.lifecycle()}});
    report_["final_usage"]=usageJson(scheduler_->usage());
    report_["observation_state"]="failed";report_["elapsed_seconds"]=seconds();
    try {std::rethrow_exception(failure);}catch(const std::exception &e){report_["error"]=e.what();}
    try {write("case-failed.json",report_);}catch(const std::exception &e){
      // Retain a small failure receipt within the remaining cap. An oversized
      // partial report is never certified or substituted for the original error.
      const Json small{{"contract",contract},{"case_id",id_},{"context",report_["context"]},
          {"observation_state","failed"},{"acceptance_claim",false},{"error",report_.value("error","")},
          {"retention_error",e.what()},{"frame_count",report_["frames"].size()},
          {"elapsed_seconds",seconds()},{"final_usage",report_["final_usage"]}};
      const auto bytes=small.dump();const auto path=directory_/"case-failure-receipt.json";
      if(bytes.size()<=maxArchive-archiveBytes_&&!std::filesystem::exists(path))
        writeBytes(path.u8string(),std::vector<unsigned char>(bytes.begin(),bytes.end()));
    }
    std::rethrow_exception(failure);
  }
}
} // namespace

Json rankUiCase(Window &window,const std::string &caseId,const std::string &context,
                const std::string &freshDirectory) {
  require(std::find(ids.begin(),ids.end(),caseId)!=ids.end(),"Unknown fixed rank UI case");
  require(context=="imgui_cpp"||context=="imgui_python","Unknown rank UI context");
  require(!freshDirectory.empty(),"Rank UI requires an explicit fresh directory");
  const auto directory=std::filesystem::absolute(std::filesystem::u8path(freshDirectory)).lexically_normal();
  require(directory.has_filename()&&directory!=directory.root_path(),"Invalid rank UI directory");
  require(!std::filesystem::exists(directory),"Rank UI directory already exists");
  require(window.documents().empty()&&window.workflow.empty()&&window.results.empty(),
          "Rank UI case requires a fresh Window");
  require(ImGui::GetCurrentContext()==nullptr,"Rank UI case requires exclusive context ownership");
  std::filesystem::create_directories(directory.parent_path());
  require(std::filesystem::create_directory(directory),"Rank UI directory already exists");
  try {return Driver(window,caseId,context,directory).run();}
  catch(const std::exception &e) {
    const auto path=directory/"setup-failed.json";
    if(!std::filesystem::exists(directory/"case-failed.json")&&
       !std::filesystem::exists(directory/"case-failure-receipt.json")&&!std::filesystem::exists(path)) {
      const auto bytes=Json{{"contract",contract},{"case_id",caseId},{"context",context},
          {"observation_state","failed"},{"acceptance_claim",false},{"error",e.what()}}.dump();
      writeBytes(path.u8string(),std::vector<unsigned char>(bytes.begin(),bytes.end()));
    }
    throw;
  }
}
} // namespace imagejxx::desktop
