// Excluded source preparation for adopted shared-UI contract002 group19.
// Root must integrate, compile and run both installed launch contexts before
// granting any runtime credit. No native numerical primitive supplies an oracle.
#include "headless_session_internal.hpp"
#include "window.hpp"
#include <imagejxx/gaussian_command.hpp>
#include <imgui.h>

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
#include <sstream>
#include <stdexcept>
#include <thread>

namespace imagejxx::desktop {
namespace {
constexpr const char *groupId = "P02a.shared_ui.19.roi-exact-bounds";
constexpr const char *contractSha = "2652bcf1c0665e997fb1bbe55c7f616071359e6b99293b875b2b8555ff045255";
constexpr const char *referenceSha = "8885c41ded23f51e56b1c42e28245bf4b9320108dacfa09da631e14fbe9bf762";
enum class Geometry { SmallL, ClippedRectangle, LargeRectangle, LargeL, Line, Point };
struct Variant {
  const char *suffix, *oracle;
  int width, height, channels;
  PixelType type;
  double sigma;
  Geometry geometry;
  const char *inputSha, *ownedOutputSha, *rawReferenceSha;
  int rawReferenceExteriorChanges;
};
const std::array<Variant, 10> variants{{
  {"u8-irregular-l", "u8-roi-irregular-l-s2-runner-area-harness", 23, 17, 1, PixelType::UInt8, 2, Geometry::SmallL,
   "f75fcc667af37db1d4dd6e433ac48776a354fca253a6c9407e7a4d06f0b367e4", "3ba0d927b08393a4c627c79c464c92d14c1f354a5d757feb785b069e27e7e96c", "3ba0d927b08393a4c627c79c464c92d14c1f354a5d757feb785b069e27e7e96c", 0},
  {"rgb-irregular-l", "rgb-roi-irregular-l-s2-runner-area-harness", 23, 17, 3, PixelType::UInt8, 2, Geometry::SmallL,
   "f783a5242a27c90c40dcc59916cb52b1c1a2458dd23f9372cab7a9932137eebb", "081805381e739154d945dc901cf1f8ffc782cc28b07df1a4c16d2526716b4e87", "081805381e739154d945dc901cf1f8ffc782cc28b07df1a4c16d2526716b4e87", 0},
  {"u16-clipped", "u16-roi-rectangle-clipped-s2-runner-area-harness", 23, 17, 1, PixelType::UInt16, 2, Geometry::ClippedRectangle,
   "133aeaee8e454bcc4fc6d27be46034f670bf9fe8127e6217a4324722555e5323", "ba9089eb6aae3f25e16260e766ab4b9edec55cc423f5a1199b96d203381aeb6b", "08aa4444522d5aadafaa35bd2764eb8aff0684ee83cdc638f33860a1adcfc954", 17},
  {"f32-clipped", "f32-roi-rectangle-clipped-s2-runner-area-harness", 23, 17, 1, PixelType::Float32, 2, Geometry::ClippedRectangle,
   "7b334761d36148554ea3ab8743b4df82453d1b639d3a2fc8a8d20d1174baffd6", "0feaa0b615363f2aed358556b68ba3c49b5e58d95214f115077106a49490cf0b", "60b1db2afbda915a8646f3487a5a02f9b60e31720f5b81a5fe6c8bd0222fbaed", 17},
  {"f32-large-s8p5", "f32-large-rectangle-offcenter-s8p5-runner-area-harness", 173, 101, 1, PixelType::Float32, 8.5, Geometry::LargeRectangle,
   "1cf450ee484210c651aafa2b324c395ddfb04dc5550bd8d9464a7e69c19b4e39", "fecbb3af5394c94a81082df6d3a0e332ab1d3dd0a68fc031f96e9382652d11a9", "fecbb3af5394c94a81082df6d3a0e332ab1d3dd0a68fc031f96e9382652d11a9", 0},
  {"f32-large-s8p6", "f32-large-rectangle-offcenter-s8p6-runner-area-harness", 173, 101, 1, PixelType::Float32, 8.6, Geometry::LargeRectangle,
   "1cf450ee484210c651aafa2b324c395ddfb04dc5550bd8d9464a7e69c19b4e39", "914e51765c3aa033f0f9c9eeefc8f5300b41f0b75e1c9c4ea29f4d0966b71ba2", "914e51765c3aa033f0f9c9eeefc8f5300b41f0b75e1c9c4ea29f4d0966b71ba2", 0},
  {"f32-large-s12", "f32-large-rectangle-offcenter-s12-runner-area-harness", 173, 101, 1, PixelType::Float32, 12, Geometry::LargeRectangle,
   "1cf450ee484210c651aafa2b324c395ddfb04dc5550bd8d9464a7e69c19b4e39", "d0a6307eb3395f56738cea9f45ed458c13d207f44ea83adfe2bd546328717325", "d0a6307eb3395f56738cea9f45ed458c13d207f44ea83adfe2bd546328717325", 0},
  {"f32-large-irregular-s12", "f32-large-irregular-offcenter-s12-runner-area-harness", 173, 101, 1, PixelType::Float32, 12, Geometry::LargeL,
   "1cf450ee484210c651aafa2b324c395ddfb04dc5550bd8d9464a7e69c19b4e39", "a50e712143f733df595cec288d642201614176752bc0b1fad676ee500b5e5527", "a50e712143f733df595cec288d642201614176752bc0b1fad676ee500b5e5527", 0},
  {"u8-line", "u8-nonarea-line-s2", 23, 17, 1, PixelType::UInt8, 2, Geometry::Line,
   "f75fcc667af37db1d4dd6e433ac48776a354fca253a6c9407e7a4d06f0b367e4", "3d21e18911f89681c4f49f0a8d5ddba1664826c562600e1a5600d9491a9c7e2b", "3d21e18911f89681c4f49f0a8d5ddba1664826c562600e1a5600d9491a9c7e2b", 0},
  {"u8-point", "u8-nonarea-point-s2", 23, 17, 1, PixelType::UInt8, 2, Geometry::Point,
   "f75fcc667af37db1d4dd6e433ac48776a354fca253a6c9407e7a4d06f0b367e4", "3d21e18911f89681c4f49f0a8d5ddba1664826c562600e1a5600d9491a9c7e2b", "3d21e18911f89681c4f49f0a8d5ddba1664826c562600e1a5600d9491a9c7e2b", 0},
}};
void require(bool value, const char *message) {
  if (!value) throw std::runtime_error(message);
}
std::string variantId(const Variant &value) { return std::string(groupId) + "." + value.suffix; }
std::filesystem::path freshDirectory(const std::string &directory) {
  require(!directory.empty(), "ROI output directory is required");
  const auto path = std::filesystem::absolute(std::filesystem::u8path(directory)).lexically_normal();
  require(path != path.root_path(), "ROI output cannot own a filesystem root");
  std::filesystem::create_directories(path.parent_path());
  require(std::filesystem::create_directory(path), "ROI output directory must be fresh");
  return path;
}
std::string address(const void *value) {
  std::ostringstream out; out << "0x" << std::hex << reinterpret_cast<std::uintptr_t>(value); return out.str();
}
// Observation identity only; this never computes an expected numerical result.
std::string sha256(const std::vector<unsigned char> &input) {
  constexpr std::array<std::uint32_t,64> k{
      0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,
      0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
      0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
      0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
      0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,
      0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
      0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,
      0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2};
  std::array<std::uint32_t,8> hash{0x6a09e667,0xbb67ae85,0x3c6ef372,0xa54ff53a,0x510e527f,0x9b05688c,0x1f83d9ab,0x5be0cd19};
  auto bytes=input; bytes.push_back(0x80); while (bytes.size()%64!=56) bytes.push_back(0);
  const auto bits=std::uint64_t(input.size())*8;
  for (int n=56;n>=0;n-=8) bytes.push_back(static_cast<unsigned char>(bits>>n));
  auto rotate=[](std::uint32_t value,int n) { return (value>>n)|(value<<(32-n)); };
  for (std::size_t offset=0;offset<bytes.size();offset+=64) {
    std::array<std::uint32_t,64> words{};
    for (std::size_t i=0;i<16;++i) for (std::size_t j=0;j<4;++j) words[i]=(words[i]<<8)|bytes[offset+4*i+j];
    for (std::size_t i=16;i<64;++i) {
      const auto a=words[i-15],b=words[i-2];
      words[i]=words[i-16]+(rotate(a,7)^rotate(a,18)^(a>>3))+words[i-7]+(rotate(b,17)^rotate(b,19)^(b>>10));
    }
    auto work=hash;
    for (std::size_t i=0;i<64;++i) {
      const auto first=work[7]+(rotate(work[4],6)^rotate(work[4],11)^rotate(work[4],25))+
          ((work[4]&work[5])^(~work[4]&work[6]))+k[i]+words[i];
      const auto second=(rotate(work[0],2)^rotate(work[0],13)^rotate(work[0],22))+
          ((work[0]&work[1])^(work[0]&work[2])^(work[1]&work[2]));
      for (std::size_t j=7;j>0;--j) work[j]=work[j-1]; work[4]+=first; work[0]=first+second;
    }
    for (std::size_t i=0;i<8;++i) hash[i]+=work[i];
  }
  std::ostringstream out; out<<std::hex<<std::setfill('0'); for (auto value:hash) out<<std::setw(8)<<value; return out.str();
}
Image fixtureImage(const Variant &spec) {
  Image image(spec.width,spec.height,spec.channels,spec.type);
  // Owned asymmetric-analytic-v1 input formula. All fractional terms are
  // dyadic and exactly represented before the explicit Float32 admission.
  for (int y=0;y<spec.height;++y) for (int x=0;x<spec.width;++x) {
    const int b=(37*x+61*y+13*x*y+19*(x%3)+23*(y%5))%256;
    if (spec.channels==3) {
      image.at(x,y,0)=(b+3*x)%256;
      image.at(x,y,1)=(5*b+11*y+17)%256;
      image.at(x,y,2)=(255-b+7*x+19*y)%256;
    } else if (spec.type==PixelType::UInt8)
      image.at(x,y)=(b+(x==0?47:0)+(y==spec.height-1?83:0))%256;
    else if (spec.type==PixelType::UInt16) image.at(x,y)=(257*b+97*x+211*y+331*(x%7))%65536;
    else image.at(x,y)=static_cast<float>((b-127)/8.+(x-2*y)/16.+((x*y)%17)/256.);
  }
  return image;
}
Selection fixtureSelection(const Variant &spec) {
  switch (spec.geometry) {
  case Geometry::SmallL: return {"polygon",{{5,4},{16,4},{16,7},{10,7},{10,12},{5,12}}};
  case Geometry::ClippedRectangle: return {"rectangle",{{-3,2},{9,13}}};
  case Geometry::LargeRectangle: return {"rectangle",{{97,43},{118,62}}};
  case Geometry::LargeL: return {"polygon",{{97,43},{118,43},{118,50},{106,50},{106,62},{97,62}}};
  case Geometry::Line: return {"line",{{2,3},{double(spec.width-3),double(spec.height-4)}}};
  case Geometry::Point: return {"point",{{7,6}}};
  }
  throw std::logic_error("Unknown ROI fixture geometry");
}
Json expectedExtent(const Variant &spec) {
  switch (spec.geometry) {
  case Geometry::SmallL: return {5,4,11,8};
  case Geometry::ClippedRectangle: return {0,2,9,11};
  case Geometry::LargeRectangle: case Geometry::LargeL: return {97,43,21,19};
  case Geometry::Line: case Geometry::Point: return nullptr;
  }
  throw std::logic_error("Unknown ROI fixture extent");
}
bool selectedPixel(const Variant &spec,int x,int y) {
  switch (spec.geometry) {
  case Geometry::SmallL: return x>=5 && x<16 && y>=4 && y<12 && (y<7 || x<10);
  case Geometry::ClippedRectangle: return x>=0 && x<9 && y>=2 && y<13;
  case Geometry::LargeRectangle: return x>=97 && x<118 && y>=43 && y<62;
  case Geometry::LargeL: return x>=97 && x<118 && y>=43 && y<62 && (y<50 || x<106);
  case Geometry::Line: case Geometry::Point: return true;
  }
  throw std::logic_error("Unknown ROI fixture mask");
}
std::vector<unsigned char> rawPixels(const std::vector<double> &pixels,const std::string &dtype) {
  static_assert(sizeof(float)==4 && std::numeric_limits<float>::is_iec559,"ROI evidence requires IEEE754 Float32");
  const int size=dtype=="float32"?4:dtype=="uint16"?2:1;
  require(dtype=="float32" || dtype=="uint16" || dtype=="uint8","Unsupported ROI observation dtype");
  std::vector<unsigned char> bytes; bytes.reserve(pixels.size()*static_cast<std::size_t>(size));
  for (double value:pixels) {
    require(std::isfinite(value),"ROI source/readback contains nonfinite values");
    std::uint32_t bits=0;
    if (dtype=="float32") {
      const float sample=static_cast<float>(value);
      require(static_cast<double>(sample)==value,"ROI Float32 observation has noncanonical values");
      std::memcpy(&bits,&sample,sizeof(bits));
    } else {
      require(value>=0 && value<=(size==2?65535:255) && std::floor(value)==value,"ROI integer observation has noncanonical values");
      bits=static_cast<std::uint32_t>(value);
    }
    for (int n=0;n<size;++n) bytes.push_back(static_cast<unsigned char>(bits>>(8*n)));
  }
  return bytes;
}
Json calibrationJson(const SpatialCalibration &value) {
  return {{"spacing",value.spacing},{"origin",value.origin},{"direction",value.direction},{"unit",value.unit},
          {"time_unit",value.time_unit},{"frame_interval",value.frame_interval},{"time_origin",value.time_origin}};
}
Json displayJson(const DisplayState &value) {
  return {{"t",value.t},{"c",value.c},{"crosshair",value.crosshair},{"orthogonal",value.orthogonal},
          {"linked_zoom",value.linkedZoom},{"zoom",value.zoom},{"plane_zoom",value.planeZoom},{"pan",value.pan},
          {"revision",value.revision},{"properties",metadataJson(value.properties)}};
}
Json usageJson(const JobUsage &value) {
  return {{"running",value.running},{"queued",value.queued},{"running_bytes",value.running_bytes},
          {"retained_bytes",value.retained_bytes},{"reserved_bytes",value.reserved_bytes}};
}

class RoiDriver {
public:
  RoiDriver(Window &window,std::shared_ptr<JobScheduler> scheduler,const Variant &spec,
            std::string context,std::filesystem::path directory)
      : window_(window),scheduler_(std::move(scheduler)),spec_(spec),directory_(std::move(directory)) {
    report_={{"schema_version",1},{"group_id",groupId},{"case_id",variantId(spec_)},{"context",std::move(context)},
      {"status","unverified"},{"acceptance_claim",false},{"contract_sha256",contractSha},
      {"api_inputs",Json::array()},{"frames",Json::array()},{"control_observations",Json::array()},
      {"checkpoints",Json::array()},{"lifecycle",Json::array()},
      {"comparison_scope","Exact capture002 inside selected area and original source outside; no tolerance or native/scipy oracle"},
      {"observation_scope","Actual CPU texture-upload input/cache, native committed planes and job lineage; no GPU readback"},
      {"oracle",{{"id",spec_.oracle},{"reference_capture_sha256",referenceSha},{"input_sha256",spec_.inputSha},
                 {"owned_expected_output_sha256",spec_.ownedOutputSha},{"raw_reference_output_sha256",spec_.rawReferenceSha},
                 {"pinned_raw_reference_outside_changed_pixels",spec_.rawReferenceExteriorChanges},
                 {"expected_original_coordinate_extent",expectedExtent(spec_)},
                 {"oracle_reference_executed_by_this_UI_driver",false}}},
      {"full61_verified",false},{"full122_verified",false},{"family_complete",false},{"release_accepted",false}};
    const auto image=fixtureImage(spec_);
    report_["fixture_input_raw"]=rawImage("fixture-input.raw",image);
    writeMember("fixture-input.json",report_.at("fixture_input_raw"));
    // Pixel-unit identity is explicit; the Scaled Units control is unavailable.
    SpatialCalibration calibration; calibration.unit="pixels";
    DocumentData data{Dataset::from_stack({image},std::move(calibration))};
    data.metadata.title=std::string("Owned Gaussian ROI ")+spec_.suffix;
    data.metadata.properties["roi_fixture"]="owned-asymmetric-analytic-v1";
    data.metadata.extensions["roi_unknown"]="preserved";
    auto selection=fixtureSelection(spec_);
    selection.attributes["roi_shape_unknown"]="shape-attribute-kept";
    data.selection=selection;
    data.rois.push_back({"Gaussian ROI fixture",selection,std::nullopt,std::nullopt,0,
                        {{"roi_unknown","roi-attribute-kept"}}});
    data.overlays.push_back({{"kind","point"},{"points",metadata_array({metadata_array({1.,1.})})},
                            {"color","#ffcc0000"},{"roi_name","Gaussian ROI fixture"},
                            {"overlay_unknown","overlay-attribute-kept"}});
    owner_=std::make_shared<imagejxx::Document>(std::move(data));
    window_.addNativeDocument(owner_); window_.recording=false;
    auto display=owner_->snapshot().display(); display.crosshair={spec_.width/2,spec_.height/2,0};
    display.properties["display_range"]=metadata_array({spec_.type==PixelType::Float32?-32.:0.,
        spec_.type==PixelType::UInt16?65535.:spec_.type==PixelType::Float32?64.:255.});
    window_.setNativeDisplay(display); owner_->mark_saved(); original_=owner_->snapshot();
    report_["api_inputs"].push_back({{"purpose","explicit fixture/source/ROI/type/default preparation only"},
      {"methods",{"addNativeDocument","setNativeDisplay","mark_saved"}},
      {"recording",false},{"fixture_formula","owned-asymmetric-analytic-v1"},
      {"tested_changes_use_API_setters",false},{"input_oracle",spec_.oracle}});
  }
  ~RoiDriver() { if (!cleaned_) try { cleanupJobs(); } catch (...) {} }
  Json run() {
    HeadlessInputSession session(window_); session_=&session;
    deadline_=std::chrono::steady_clock::now()+std::chrono::seconds(30);
    try {
      report_["lifecycle"].push_back({{"label","opened"},{"observed",session.lifecycle()}});
      frame("warmup-1"); frame("warmup-2");
      const auto baseline=checkpoint("baseline"); report_["before"]=baseline.at("native_state");
      require(baseline.at("native_state").at("planes").at(0).at("raw").at("sha256")==spec_.inputSha &&
              owner_->undo_steps()==0 && owner_->redo_steps()==0 && !owner_->snapshot().dirty(),
              "ROI source bits/history do not match the adopted input fixture");
      openGaussian(); replaceSigma();
      require(last_.at("gaussian_panel").at("sigma")==spec_.sigma &&
              last_.at("gaussian_panel").at("scaled_available")==false &&
              last_.at("gaussian_panel").at("scaled_units")==false &&
              last_.at("gaussian_panel").at("preview")==false,
              "ROI physical Gaussian draft differs from the pinned pixel-unit intent");
      click("gaussian.preview");
      await("latest_preview_ready_and_rendered",[&] {
        return !last_.at("gaussian_jobs").at("active_installed_generation").is_null() &&
            last_.at("ordinary_pixels").at("pixels_source")=="preview" &&
            scheduler_->usage().running==0 && scheduler_->usage().queued==0;
      });
      // Keep the actual record, resolve only its observed region, and serialize
      // it before numerical assertions. No native kernel generates expectations.
      report_["preview_command_record"]=last_.at("gaussian_jobs").at("preview").at("command_record");
      const auto captured=gaussian_command_from_record(*original_,metadataObjectFromJson(report_.at("preview_command_record")));
      const auto region=gaussian_command_region(captured.target(),0);
      report_["captured_region"]={{"extent",region.extent?Json::array({region.extent->x,region.extent->y,
          region.extent->width,region.extent->height}):Json()},
          {"mask",region.mask?rawImage("captured-region-mask.u8",*region.mask):Json()},
          {"captured_target",metadataJson(captured.target().record())}};
      const auto preview=checkpoint("preview");
      verifyPreview(preview); verifyOutside(previewRaw(preview));
      require(report_.at("captured_region").at("extent")==expectedExtent(spec_),
              "ROI original-image coordinate bounds were cropped or replaced by occupancy bounds");
      click("gaussian.ok");
      // All group19 pinned inputs are planar. A stack prompt here would be an
      // actual mismatch, not a reason to invent an unrequested stack choice.
      require(last_.at("gaussian_panel").at("stack_prompt")==false,"Planar ROI fixture unexpectedly opened a stack-choice dialog");
      await("gaussian_apply_terminal",[&] {
        return !last_.at("gaussian_jobs").at("apply_busy").get<bool>() && !window_.busy() &&
            last_.at("gaussian_jobs").at("apply").at("exit")=="published";
      });
      report_["apply_command_record"]=last_.at("gaussian_jobs").at("apply").at("command_record");
      const auto applied=checkpoint("applied"); verifyApplied(applied);
      actualHistory("Undo"); const auto undone=checkpoint("undone");
      verifyStatePixels(undone.at("native_state"),spec_.inputSha); verifyPreserved(undone.at("native_state"));
      require(owner_->undo_steps()==0 && owner_->redo_steps()==1,"ROI physical Undo did not restore exactly one step");
      actualHistory("Redo"); const auto redone=checkpoint("redone");
      verifyStatePixels(redone.at("native_state"),spec_.ownedOutputSha); verifyPreserved(redone.at("native_state"));
      require(owner_->undo_steps()==1 && owner_->redo_steps()==0,"ROI physical Redo did not restore exactly one changed step");
      report_["after"]=nativeState("after");
      report_["original_after_raw"]=rawImage("original-after.raw",*original_->data().dataset.plane({0,0,0}));
      writeMember("case-unverified.json",report_);
      require(report_.at("original_after_raw").at("sha256")==spec_.inputSha,
              "ROI Preview/Apply/Undo/Redo mutated retained original source bytes");
      cleanupJobs(); frame("after_native_cleanup"); checkpoint("cleanup");
      report_["lifecycle"].push_back({{"label","before_close"},{"observed",session.lifecycle()}});
      session.close(); session_=nullptr;
      report_["lifecycle"].push_back({{"label","closed"},{"observed",session.lifecycle()}});
      report_["final_cleanup"]={{"usage",usageJson(scheduler_->usage())},{"lifecycle",session.lifecycle()},
          {"Gaussian_renderer_owns_native_jobs",false},{"observer_retained_result_aliases",0}};
      require(scheduler_->usage().running==0 && scheduler_->usage().queued==0 && scheduler_->usage().reserved_bytes==0 &&
              session.lifecycle().at("open")==false && session.lifecycle().at("context_id")==0,
              "ROI producer leaked native reservations or its renderer context");
      report_["status"]="passed"; writeMember("case.json",report_); return report_;
    } catch (const std::exception &error) {
      report_["status"]="failed"; report_["failure"]={{"message",error.what()}};
      try { writeMember("case-failed-before-cleanup.json",report_); } catch (...) {}
      try { cleanupJobs(); session.close(); session_=nullptr;
        report_["failure_cleanup"]={{"usage",usageJson(scheduler_->usage())},{"lifecycle",session.lifecycle()}};
        writeMember("case-failed.json",report_);
      } catch (...) { session_=nullptr; }
      throw;
    }
  }
private:
  Window &window_; std::shared_ptr<JobScheduler> scheduler_; const Variant &spec_;
  std::filesystem::path directory_; std::shared_ptr<imagejxx::Document> owner_;
  std::optional<DocumentSnapshot> original_; // Original committed source only, never a preview alias.
  HeadlessInputSession *session_=nullptr; Json report_,last_;
  std::map<std::string,Json> cacheRaw_; std::size_t frames_=0,rawOrdinal_=0;
  std::chrono::steady_clock::time_point deadline_; bool cleaned_=false;
  void writeMember(const std::string &name,const Json &value) const {
    const auto path=directory_/name; require(!std::filesystem::exists(path),"ROI JSON member already exists");
    writeJson(path.u8string(),value);
  }
  Json rawMember(const std::string &name,const std::vector<unsigned char> &bytes,const Json &shape,const std::string &dtype) {
    const auto path=directory_/name; require(!std::filesystem::exists(path),"ROI raw member already exists");
    writeBytes(path.u8string(),bytes);
    std::size_t samples=1; for (const auto &axis:shape) samples*=axis.get<std::size_t>();
    return {{"member_id",name},{"path",path.u8string()},{"shape",shape},{"dtype",dtype},
        {"encoding",dtype=="float32"?"IEEE754 Float32LE":dtype=="uint16"?"UInt16LE":"UInt8"},
        {"sample_count",samples},{"byte_count",bytes.size()},{"sha256",sha256(bytes)}};
  }
  Json rawImage(const std::string &name,const Image &image) {
    const auto shape=image.channels==1?Json::array({image.height,image.width}):Json::array({image.height,image.width,image.channels});
    return rawMember(name,rawPixels(image.pixels,typeName(image.type)),shape,typeName(image.type));
  }
  void compactImage(Json &image) {
    if (image.is_null()) return;
    const auto shape=image.at("shape"); const auto dtype=image.at("dtype").get<std::string>();
    const auto bytes=rawPixels(image.at("pixels").get<std::vector<double>>(),dtype);
    const auto key=dtype+shape.dump()+sha256(bytes);
    auto found=cacheRaw_.find(key);
    if (found==cacheRaw_.end()) found=cacheRaw_.emplace(key,rawMember("cache-"+std::to_string(++rawOrdinal_)+".raw",bytes,shape,dtype)).first;
    image.erase("pixels"); image["raw"]=found->second;
    image["observation_scope"]="actual CPU upload-input/cache; no GPU readback";
  }
  const Json &frame(const std::string &label,const Json &events=Json::array()) {
    require(session_ && frames_<1200 && std::chrono::steady_clock::now()<deadline_,"ROI frame watchdog exceeded30 seconds/1200 frames");
    auto value=session_->frame(events);
    auto &readback=value.at("ordinary_pixels").at("readback");
    compactImage(readback["source"]); compactImage(readback["rendered_rgb"]);
    if (value.contains("orthogonal_planes")) for (auto &plane:value["orthogonal_planes"])
      if (plane.contains("readback")) { compactImage(plane["readback"]["source"]); compactImage(plane["readback"]["rendered_rgb"]); }
    Json observed{{"ordinal",++frames_},{"label",label},{"requested_events",events},
        {"rendered",value},{"usage",usageJson(scheduler_->usage())}};
    writeMember("frame-"+std::to_string(frames_)+".json",observed);
    report_["frames"].push_back(std::move(observed)); last_=std::move(value);
    require(last_.at("input_frame").at("raw_inputs")==events &&
            last_.at("session").at("open")==true &&
            last_.at("session").at("context_id")==last_.at("session").at("current_context_id"),
            "ROI actual physical input lost its persistent renderer context or event order");
    return last_;
  }
  template<class Predicate> void await(const char *label,Predicate done) {
    do { frame(label); std::this_thread::yield(); } while (!done());
  }
  Json control(const std::string &selector) const {
    if (selector.compare(0,9,"gaussian.")==0) {
      const auto name=selector.substr(9); auto value=last_.at("gaussian_panel").at("control_states").at(name);
      value["rect"]=last_.at("gaussian_panel").at("control_rects").at(name);
      value["point"]=last_.at("gaussian_panel").at("controls").at(name); return value;
    }
    return last_.at("host_controls").at(selector);
  }
  void click(const std::string &selector) {
    const auto value=control(selector); const auto &point=value.at("point"),&rect=value.at("rect");
    require(value.at("enabled")==true && value.at("id").get<std::uint64_t>()!=0 &&
            point.is_array() && point.size()==2 && rect.is_array() && rect.size()==4,
            "ROI physical control is absent, disabled or lacks actual geometry");
    const double x=std::floor(point[0].get<double>()),y=std::floor(point[1].get<double>());
    require(std::isfinite(x) && std::isfinite(y) && x>=rect[0].get<double>() && x<rect[2].get<double>() &&
            y>=rect[1].get<double>() && y<rect[3].get<double>(),"ROI raster pointer lies outside the observed control");
    report_["control_observations"].push_back({{"selector",selector},{"control",value},
        {"position",{x,y}},{"before_frame_ordinal",frames_}});
    frame(selector+".position",Json::array({{{"type","position"},{"space","screen"},{"point",{x,y}}}}));
    frame(selector+".down",Json::array({{{"type","button"},{"name","left"},{"down",true}}}));
    frame(selector+".up",Json::array({{{"type","button"},{"name","left"},{"down",false}}}));
  }
  void openGaussian() {
    click("menu.Process"); frame("Process_popup_visible");
    click("menu.Process.Filters"); frame("Filters_popup_visible");
    click("menu.command.gaussian_blur_imagej"); frame("Gaussian_initial_focus");
    require(last_.at("gaussian_panel").at("open")==true,"ROI physical Gaussian menu failed to open the dialog");
  }
  void replaceSigma() {
    click("gaussian.sigma"); const auto id=control("gaussian.sigma").at("id");
    require(last_.at("input_frame").at("focus").at("active_id")==id &&
            last_.at("input_frame").at("input_text").at("id")==id,
            "ROI physical text input did not focus the actual Sigma item");
    frame("gaussian.sigma.select_all.down",Json::array({{{"type","keys"},{"keys",Json::array({
        {{"name","Ctrl"},{"down",true}},{{"name","A"},{"down",true}}})}}}));
    frame("gaussian.sigma.select_all.up",Json::array({{{"type","keys"},{"keys",Json::array({
        {{"name","Ctrl"},{"down",false}},{{"name","A"},{"down",false}}})}}}));
    std::ostringstream text; text<<std::setprecision(15)<<spec_.sigma;
    frame("gaussian.sigma.text",Json::array({{{"type","text"},{"value",text.str()}}})); frame("gaussian.sigma.text_settled");
  }
  void actualHistory(const std::string &name) {
    click("menu.Edit"); frame("Edit_popup_visible"); click("menu.Edit."+name); frame("history_"+name+"_settled");
  }
  Json nativeState(const std::string &label) {
    const auto source=owner_->snapshot(); const auto &data=source.data(); const auto shape=data.dataset.shape();
    const auto revisions=source.revisions(); const auto history=owner_->history();
    Json planes=Json::array(),rois=Json::array(),overlays=Json::array();
    for (int t=0;t<shape.t;++t) for (int c=0;c<shape.c;++c) for (int z=0;z<shape.z;++z) {
      const auto plane=data.dataset.plane({t,c,z});
      const auto member=label+"-plane-"+std::to_string(t)+"-"+std::to_string(c)+"-"+std::to_string(z)+".raw";
      planes.push_back({{"position",{t,c,z}},{"raw",rawImage(member,*plane)},
                        {"image_address",address(plane.get())},{"buffer_address",address(plane->pixels.data())}});
    }
    for (const auto &roi:data.rois) rois.push_back({{"name",roi.name},{"selection",selectionJson(roi.selection)},
        {"attributes",metadataJson(roi.attributes)},{"t",roi.t?Json(*roi.t):Json()},
        {"c",roi.c?Json(*roi.c):Json()},{"z",roi.z?Json(*roi.z):Json()}});
    for (const auto &overlay:data.overlays) overlays.push_back(metadataJson(overlay));
    return {{"document_id",source.id()},{"data_identity",source.data_identity()?Json(*source.data_identity()):Json()},
      {"shape_tcz_yxs",{shape.t,shape.c,shape.z,shape.y,shape.x,shape.s}},{"dtype",typeName(data.dataset.pixel_type())},
      {"dirty",source.dirty()},{"undo_steps",owner_->undo_steps()},{"redo_steps",owner_->redo_steps()},
      {"history",{{"undo_steps",history.undo_steps},{"redo_steps",history.redo_steps},
          {"step_limit",history.step_limit},{"pixel_budget",history.pixel_budget},
          {"pixels",{{"logical_bytes",history.pixels.logical_bytes},{"current_bytes",history.pixels.current_bytes},
          {"retained_bytes",history.pixels.retained_bytes},{"obsolete_bytes",history.pixels.obsolete_bytes},
          {"retained_planes",history.pixels.retained_planes},{"exact",history.pixels.exact}}}}},
      {"revisions",{{"pixels",revisions.pixels},{"metadata",revisions.metadata},{"selection",revisions.selection},{"display",revisions.display}}},
      {"calibration",calibrationJson(data.dataset.calibration())},{"planes",planes},
      {"selection",data.selection?selectionJson(*data.selection):Json()},
      {"previous_selection",data.previous_selection?selectionJson(*data.previous_selection):Json()},
      {"rois",rois},{"overlays",overlays},{"spectrum_present",data.spectrum.has_value()},
      {"metadata",{{"title",data.metadata.title},{"properties",metadataJson(data.metadata.properties)},
          {"extensions",metadataJson(data.metadata.extensions)},{"record_layout",metadataJson(data.metadata.record_layout)}}},
      {"display",displayJson(source.display())},{"document_info",window_.documentInfo()},
      {"workflow",window_.workflow},{"results",window_.results},{"recording",window_.recording}};
  }
  Json checkpoint(const std::string &name) {
    Json value{{"name",name},{"frame_ordinal",frames_},{"rendered",last_},
        {"native_state",nativeState(name)},{"usage",usageJson(scheduler_->usage())}};
    writeMember("checkpoint-"+name+".json",value); report_["checkpoints"].push_back(value); return value;
  }
  Json previewRaw(const Json &value) const { return value.at("rendered").at("ordinary_pixels").at("readback").at("source").at("raw"); }
  void verifyStatePixels(const Json &value,const char *expected) const {
    require(value.at("planes").size()==1 && value.at("planes").at(0).at("position")==Json::array({0,0,0}) &&
            value.at("planes").at(0).at("raw").at("sha256")==expected,
            "ROI committed raw output differs from the exact adopted capture002 composition");
  }
  void verifyPreserved(const Json &value) const {
    const auto &before=report_.at("before");
    for (const auto *field:{"document_id","shape_tcz_yxs","dtype","calibration","selection","previous_selection",
         "rois","overlays","metadata","spectrum_present","workflow","results","recording"})
      require(value.at(field)==before.at(field),"ROI operation changed preserved nonpixel source fields/attributes");
    for (const auto *field:{"t","c","crosshair","orthogonal","properties"})
      require(value.at("display").at(field)==before.at("display").at(field),"ROI command changed source navigation or scientific display policy");
  }
  void verifyPreview(const Json &value) const {
    const auto &native=value.at("native_state"),&before=report_.at("before");
    verifyStatePixels(native,spec_.inputSha); verifyPreserved(native);
    for (const auto *field:{"data_identity","revisions","dirty","undo_steps","redo_steps","history"})
      require(native.at(field)==before.at(field),"ROI Preview changed committed source identity/revisions/history");
    require(previewRaw(value).at("sha256")==spec_.ownedOutputSha &&
            value.at("rendered").at("ordinary_pixels").at("pixels_source")=="preview" &&
            value.at("rendered").at("ordinary_pixels").at("contrast_source")=="live" &&
            value.at("usage").at("running_bytes")==0 && value.at("usage").at("retained_bytes").get<std::size_t>()>0,
            "ROI installed native Preview/raw cache differs from its exact pinned output/provider");
  }
  void verifyApplied(const Json &value) const {
    const auto &native=value.at("native_state"); verifyStatePixels(native,spec_.ownedOutputSha); verifyPreserved(native);
    verifyOutside(native.at("planes").at(0).at("raw"));
    require(report_.at("preview_command_record")==report_.at("apply_command_record") &&
            previewRaw(value).at("sha256")==spec_.ownedOutputSha &&
            value.at("rendered").at("ordinary_pixels").at("pixels_source")=="live" &&
            owner_->undo_steps()==1 && owner_->redo_steps()==0 && owner_->snapshot().dirty() &&
            value.at("rendered").at("gaussian_jobs").at("apply").at("published_document_id")==owner_->id,
            "ROI Apply differs from Preview target/pixels or failed single-step native publication");
  }
  void verifyOutside(const Json &descriptor) const {
    const auto source=readBytes(report_.at("fixture_input_raw").at("path").get<std::string>());
    const auto actual=readBytes(descriptor.at("path").get<std::string>());
    require(source.size()==actual.size(),"ROI actual/raw source byte sizes differ");
    const auto stride=static_cast<std::size_t>(spec_.channels)*(spec_.type==PixelType::Float32?4:spec_.type==PixelType::UInt16?2:1);
    for (int y=0;y<spec_.height;++y) for (int x=0;x<spec_.width;++x) if (!selectedPixel(spec_,x,y)) {
      const auto offset=(static_cast<std::size_t>(y)*spec_.width+x)*stride;
      require(std::equal(source.begin()+offset,source.begin()+offset+stride,actual.begin()+offset),
              "ROI output changed source bits outside the effective selected extent/mask");
    }
  }
  void cleanupJobs() {
    report_["api_inputs"].push_back({{"purpose","bounded final/failure native-job cleanup only"},
        {"methods",{"Window.cancel","action(wait_jobs,timeout_ms=5000)","Window.poll"}}});
    window_.cancel(); window_.action("wait_jobs",{{"timeout_ms",5000}}); window_.poll(); cleaned_=true;
  }
};
} // namespace

Json Window::gaussianRoiCase(const std::string &variant,const std::string &context,const std::string &directory) {
  checkThread();
  if (context!="imgui_cpp" && context!="imgui_python") throw std::invalid_argument("ROI context must be imgui_cpp or imgui_python");
  const auto found=std::find_if(variants.begin(),variants.end(),[&](const Variant &value) { return variantId(value)==variant; });
  if (found==variants.end()) throw std::invalid_argument("Unknown adopted Gaussian ROI variant ID");
  require(documents_.empty() && workflow.empty() && results.empty() && ImGui::GetCurrentContext()==nullptr,
          "ROI case requires a fresh creator-owned Window without an existing ImGui context");
  RoiDriver driver(*this,controller_->job_scheduler(),*found,context,freshDirectory(directory)); return driver.run();
}
int Window::gaussianRoiConsumerTest(const std::string &directory) {
  checkThread(); require(documents_.empty() && ImGui::GetCurrentContext()==nullptr,"ROI standalone needs a fresh Window");
  const auto root=freshDirectory(directory);
  Json report{{"schema_version",1},{"group_id",groupId},{"context","imgui_cpp"},{"status","unverified"},
      {"acceptance_claim",false},{"contract_sha256",contractSha},{"cases",Json::array()},
      {"full61_verified",false},{"full122_verified",false},{"family_complete",false},{"release_accepted",false}};
  try {
    for (const auto &variant:variants) {
      const auto directory=root/variant.suffix; Window owner((directory/"preferences.json").u8string());
      report["cases"].push_back(owner.gaussianRoiCase(variantId(variant),"imgui_cpp",directory.u8string()));
      std::cout<<"PASS "<<variantId(variant)<<" imgui_cpp\n";
    }
    report["status"]="passed"; writeJson((root/"imgui-cpp.json").u8string(),report); return 0;
  } catch (const std::exception &error) {
    report["status"]="failed"; report["failure"]={{"message",error.what()}};
    writeJson((root/"imgui-cpp-failed.json").u8string(),report); throw;
  }
}
} // namespace imagejxx::desktop
