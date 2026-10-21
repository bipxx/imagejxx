// Bounded observation producer for G016.manager.area_boolean. Expected masks
// belong to the independent pinned-fixture reader, never to this producer.
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
#include <iomanip>
#include <iostream>
#include <mutex>
#include <sstream>
#include <stdexcept>

namespace imagejxx::desktop {
namespace {
using Clock = std::chrono::steady_clock;
constexpr int width = 13, height = 9, planes = 2;
constexpr std::size_t maxFrames = 160;
constexpr double maxSeconds = 15., aggregateSeconds = 180.;
constexpr const char *contract = "p05e.roi-boolean-ui.001";
constexpr std::array<const char *, 16> caseIds{{
    "R01", "R02", "R03", "R04", "R05", "R06", "R07", "R08",
    "R09", "R10", "R11", "R12", "R13", "R14", "R15", "R16"}};
constexpr std::array<const char *, 8> oracleIds{{
    "overlap_rectangles__union", "overlap_rectangles__intersection",
    "overlap_rectangles__subtraction", "overlap_rectangles__xor",
    "ring_and_inner_island__union", "disconnected_rectangle_ellipse__union",
    "entirely_outside__intersection", "duplicate_polygon__xor"}};

void require(bool condition, const char *message) {
  if (!condition) throw std::runtime_error(message);
}
double elapsed(Clock::time_point start) {
  return std::chrono::duration<double>(Clock::now() - start).count();
}
std::filesystem::path freshDirectory(const std::string &directory) {
  require(!directory.empty(), "ROI Boolean probe needs an output directory");
  const auto path = std::filesystem::absolute(std::filesystem::u8path(directory)).lexically_normal();
  require(path != path.root_path(), "ROI Boolean probe cannot own a filesystem root");
  std::filesystem::create_directories(path.parent_path());
  require(std::filesystem::create_directory(path), "ROI Boolean probe directory must be fresh");
  return path;
}
int caseNumber(const std::string &caseId) {
  for (std::size_t index = 0; index < caseIds.size(); ++index)
    if (caseId == caseIds[index]) return static_cast<int>(index + 1);
  throw std::invalid_argument("ROI Boolean UI case must be R01..R16");
}
Json key(const char *name, bool down) {
  return {{"type", "key"}, {"name", name}, {"down", down}};
}
Json simultaneousKeys(std::initializer_list<const char *> names, bool down) {
  Json atoms = Json::array();
  for (const auto name : names) atoms.push_back({{"name", name}, {"down", down}});
  return {{"type", "keys"}, {"keys", std::move(atoms)}};
}
Json exceptionJson(const std::exception &error) {
  const char *type = dynamic_cast<const RevisionConflict *>(&error) ? "revision_conflict"
      : dynamic_cast<const std::out_of_range *>(&error) ? "out_of_range"
      : dynamic_cast<const std::invalid_argument *>(&error) ? "invalid_argument" : "exception";
  return {{"type", type}, {"message", error.what()}};
}
// Standard SHA256 identifies the observed byte artifact only.
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
  for (int shift = 56; shift >= 0; shift -= 8)
    bytes.push_back(static_cast<unsigned char>(bits >> shift));
  const auto rotate = [](std::uint32_t value, int shift) {
    return (value >> shift) | (value << (32 - shift));
  };
  for (std::size_t offset = 0; offset < bytes.size(); offset += 64) {
    std::array<std::uint32_t, 64> words{};
    for (std::size_t i = 0; i < 16; ++i)
      for (std::size_t j = 0; j < 4; ++j)
        words[i] = (words[i] << 8) | bytes[offset + 4 * i + j];
    for (std::size_t i = 16; i < 64; ++i) {
      const auto a = words[i - 15], b = words[i - 2];
      words[i] = words[i - 16] + (rotate(a, 7) ^ rotate(a, 18) ^ (a >> 3)) +
          words[i - 7] + (rotate(b, 17) ^ rotate(b, 19) ^ (b >> 10));
    }
    auto work = hash;
    for (std::size_t i = 0; i < 64; ++i) {
      const auto first = work[7] + (rotate(work[4], 6) ^ rotate(work[4], 11) ^ rotate(work[4], 25)) +
          ((work[4] & work[5]) ^ (~work[4] & work[6])) + k[i] + words[i];
      const auto second = (rotate(work[0], 2) ^ rotate(work[0], 13) ^ rotate(work[0], 22)) +
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
Stack inputStack() {
  Stack result;
  for (int z = 0; z < planes; ++z) {
    Image plane(width, height, 1, PixelType::UInt8);
    for (int y = 0; y < height; ++y)
      for (int x = 0; x < width; ++x)
        plane.at(x, y) = (37*x + 61*y + 13*x*y + 43*z) % 251;
    result.push_back(std::move(plane));
  }
  return result;
}
std::vector<unsigned char> rawPixels(const Dataset &dataset) {
  const auto shape = dataset.shape();
  require(shape.t == 1 && shape.c == 1 && shape.z == planes &&
          shape.x == width && shape.y == height && shape.s == 1 &&
          dataset.pixel_type() == PixelType::UInt8, "ROI Boolean input shape/type changed");
  std::vector<unsigned char> result;
  result.reserve(planes * width * height);
  for (int z = 0; z < planes; ++z)
    for (const auto value : dataset.plane({0, 0, z})->pixels) {
      require(std::isfinite(value) && value >= 0 && value <= 255 && value == std::floor(value),
              "ROI Boolean source observer received a non-byte sample");
      result.push_back(static_cast<unsigned char>(value));
    }
  return result;
}
Selection operand(std::string kind, std::vector<std::array<double, 2>> points) {
  Selection value(std::move(kind), std::move(points), 3);
  value.attributes = metadataObjectFromJson({{"label", "Retained operand"},
      {"extension", {{"key", {1, "kept"}}}}});
  return value;
}
std::vector<Selection> operands(int number) {
  if (number == 5) {
    // Literal input bitmap from owned outer/hole coordinates, independent of
    // target Boolean output and the reference's expected result masks.
    constexpr std::array<const char *, height> ring{{
      "0000000000000", "0111111111100", "0111111111100",
      "0110000001100", "0110000001100", "0110000001100",
      "0111111111100", "0111111111100", "0000000000000"}};
    Image bitmap(width, height, 1, PixelType::UInt8);
    for (int y = 0; y < height; ++y)
      for (int x = 0; x < width; ++x) bitmap.at(x, y) = ring[y][x] == '1' ? 255 : 0;
    auto value = Selection::from_mask(bitmap);
    value.width = 3;
    value.attributes = operand("rectangle", {{1, 1}, {2, 2}}).attributes;
    return {std::move(value), operand("rectangle", {{5, 4}, {7, 5}})};
  }
  if (number == 6)
    return {operand("rectangle", {{1, 1}, {4, 3}}), operand("ellipse", {{8, 5}, {12, 8}})};
  if (number == 7)
    return {operand("rectangle", {{-7, -6}, {-4, -4}}), operand("ellipse", {{15, 10}, {19, 13}})};
  if (number == 8) {
    const auto value = operand("polygon", {{-.25, 1.5}, {8.75, .25}, {10.5, 7.75}, {2.25, 6.5}});
    return {value, value};
  }
  return {operand("rectangle", {{1, 1}, {7, 5}}), operand("rectangle", {{4, 3}, {10, 7}})};
}
DocumentData initialData(int number, const char *title = "Owned ROI Boolean source") {
  DocumentData data{Dataset::from_stack(inputStack())};
  data.metadata.title = title;
  data.metadata.properties = metadataObjectFromJson({{"custom", "retained"}});
  data.metadata.extensions = metadataObjectFromJson({{"extension", {{"values", {1, "kept"}}}}});
  data.selection = Selection("point", {{2, 8}}, 2);
  data.selection->attributes = metadataObjectFromJson({{"label", "before Boolean"}});
  data.previous_selection = Selection("point", {{0, 0}});
  data.previous_selection->attributes = metadataObjectFromJson({{"label", "earlier"}});
  auto geometry = operands(number);
  for (std::size_t index = 0; index < geometry.size(); ++index)
    data.rois.push_back({index == 0 ? "First operand" : "Second operand",
        geometry[index], {}, {}, index == 0 ? 1 : 0,
        metadataObjectFromJson({{"kept", {index, "position"}}})});
  if (number == 9 || number == 10) {
    data.rois.push_back({"Third row", operand("rectangle", {{0, 0}, {2, 2}}), {}, {}, {}, {}});
    data.rois.push_back({"Fourth row", operand("rectangle", {{10, 7}, {13, 9}}), {}, {}, {}, {}});
  }
  return data;
}
Json observedSelection(const std::optional<Selection> &value) {
  if (!value) return nullptr;
  Json rows = Json::array();
  const auto mask = value->mask(width, height);
  for (int y = 0; y < height; ++y) {
    std::string row;
    row.reserve(width);
    for (int x = 0; x < width; ++x) row += mask.at(x, y) != 0 ? '1' : '0';
    rows.push_back(std::move(row));
  }
  return {{"kind", value->kind}, {"points", value->points}, {"width", value->width},
      {"attributes", metadataJson(value->attributes)}, {"mask_width", value->mask_width()},
      {"mask_height", value->mask_height()},
      {"packed_mask", value->kind == "mask" ? Json(value->packed_mask()) : Json::array()},
      {"mask_rows", std::move(rows)}};
}
Json observedDisplay(const DisplayState &value) {
  return {{"t", value.t}, {"c", value.c}, {"crosshair", value.crosshair},
      {"orthogonal", value.orthogonal}, {"linked_zoom", value.linkedZoom},
      {"zoom", value.zoom}, {"plane_zoom", value.planeZoom}, {"pan", value.pan},
      {"properties", metadataJson(value.properties)}, {"revision", value.revision}};
}
Json observedCalibration(const SpatialCalibration &value) {
  return {{"spacing", value.spacing}, {"origin", value.origin}, {"direction", value.direction},
      {"unit", value.unit}, {"time_unit", value.time_unit},
      {"frame_interval", value.frame_interval}, {"time_origin", value.time_origin}};
}
Json observedRevisions(const Revisions &value) {
  return {{"pixels", value.pixels}, {"metadata", value.metadata},
      {"selection", value.selection}, {"display", value.display}};
}
Json observedUsage(const JobUsage &value) {
  return {{"running", value.running}, {"queued", value.queued},
      {"running_bytes", value.running_bytes}, {"retained_bytes", value.retained_bytes},
      {"reserved_bytes", value.reserved_bytes}};
}
const char *jobState(JobState state) {
  switch (state) {
  case JobState::Queued: return "queued";
  case JobState::Running: return "running";
  case JobState::Ready: return "ready";
  case JobState::Publishing: return "publishing";
  case JobState::Completed: return "completed";
  case JobState::Failed: return "failed";
  case JobState::Canceled: return "canceled";
  }
  return "unknown";
}

class RoiBooleanUiDriver {
public:
  RoiBooleanUiDriver(Window &window, int number, std::string context,
                     std::filesystem::path directory)
      : window_(window), number_(number), directory_(std::move(directory)), start_(Clock::now()),
        owner_(std::make_shared<imagejxx::Document>(initialData(number))) {
    report_ = {{"schema_version", 1}, {"contract_id", contract},
        {"case_id", caseIds[number - 1]}, {"context", std::move(context)},
        {"status", "observing"}, {"acceptance_claim", false},
        {"limits", {{"max_frames", maxFrames}, {"max_seconds", maxSeconds}}},
        {"frames", Json::array()}, {"control_observations", Json::array()},
        {"lifecycle", Json::array()}, {"api_inputs", Json::array()},
        {"rejections", Json::array()}, {"states", Json::object()},
        {"scope", "Area-manager observation only; no full G016 or scientific manager acceptance"}};
    if (number <= 8) report_["oracle_id"] = oracleIds[number - 1];
    window_.addNativeDocument(owner_);
    auto display = owner_->snapshot().display();
    display.crosshair = {6, 4, 0};
    display.zoom = 8;
    display.properties["display_range"] = metadata_array({0., 255.});
    window_.setNativeDisplay(display, owner_->id);
    owner_->mark_saved();
    original_ = owner_->snapshot();
    if (number == 11 || number == 16) {
      other_ = std::make_shared<imagejxx::Document>(initialData(1, "Other ROI owner"));
      window_.addNativeDocument(other_);
      other_->mark_saved();
      window_.setActiveIndex(0);
      report_["states"]["other_before"] = sourceSummary(other_);
      report_["api_inputs"].push_back({{"method", "addNativeDocument"}, {"document_id", other_->id},
          {"classification", "declared second-owner preparation"}});
    }
    capture("before");
    report_["source_before_raw"] = rawMember("source-before.u8", original_->data().dataset);
    report_["api_inputs"].push_back({{"method", "addNativeDocument"}, {"document_id", owner_->id},
        {"classification", "literal fixture preparation"},
        {"fixture_formula", "(37*x+61*y+13*x*y+43*z)%251"}, {"shape", {planes, height, width}},
        {"initial_state", report_["states"]["before"]}});
  }
  Json run() {
    try {
      HeadlessInputSession session(window_);
      report_["lifecycle"].push_back({{"label", "opened"}, {"observed", session.lifecycle()}});
      frame(session, "warmup.1");
      frame(session, "warmup.2");
      openManager(session);
      if (number_ <= 8) booleanCase(session);
      else if (number_ == 9) modifierCase(session);
      else if (number_ == 10) selectAllCase(session);
      else if (number_ == 11) switchCase(session);
      else if (number_ == 12) metadataCase(session);
      else if (number_ == 13) invalidCase(session);
      else if (number_ == 14) staleCase(session);
      else if (number_ == 15) busyCase(session);
      else closedCase(session);
      frame(session, "final");
      session.close();
      report_["lifecycle"].push_back({{"label", "closed"}, {"observed", session.lifecycle()}});
      capture("after");
      if (other_) report_["states"]["other_after"] = sourceSummary(other_);
      report_["source_after_raw"] = rawMember("source-after.u8", owner_->snapshot().data().dataset);
      report_["original_after_raw"] = rawMember("original-after.u8", original_->data().dataset);
      report_["final_busy"] = window_.busy();
      report_["window_error"] = window_.error();
      report_["final_context"] = {{"current_context_id", static_cast<std::uint64_t>(
          reinterpret_cast<std::uintptr_t>(ImGui::GetCurrentContext()))}};
      report_["elapsed_seconds"] = elapsed(start_);
      budget();
      report_["status"] = "observed";
      writeJson((directory_ / "case.json").u8string(), report_);
      return report_;
    } catch (const std::exception &error) {
      report_["status"] = "failed";
      report_["failure"] = exceptionJson(error);
      report_["elapsed_seconds"] = elapsed(start_);
      try { writeJson((directory_ / "case-failed.json").u8string(), report_); } catch (...) {}
      throw;
    }
  }
private:
  Window &window_;
  int number_;
  std::filesystem::path directory_;
  Clock::time_point start_;
  std::shared_ptr<imagejxx::Document> owner_, other_;
  std::optional<DocumentSnapshot> original_;
  Json report_, last_;
  std::size_t frameOrdinal_ = 0;

  void budget() const {
    require(frameOrdinal_ <= maxFrames && elapsed(start_) <= maxSeconds,
            "ROI Boolean UI case exceeded its declared frame/time limit");
  }
  Json rawMember(const std::string &name, const Dataset &dataset) {
    const auto bytes = rawPixels(dataset);
    const auto path = directory_ / name;
    require(!std::filesystem::exists(path), "ROI Boolean raw member already exists");
    writeBytes(path.u8string(), bytes);
    return {{"member_id", name}, {"path", path.u8string()}, {"encoding", "UInt8"},
        {"dtype", "uint8"}, {"shape", {planes, height, width}},
        {"sample_count", bytes.size()}, {"byte_count", bytes.size()}, {"sha256", sha256(bytes)}};
  }
  Json sourceSummary(const std::shared_ptr<imagejxx::Document> &owner) const {
    const auto snapshot = owner->snapshot();
    const auto revisions = snapshot.revisions();
    const auto &data = snapshot.data();
    const auto shape = data.dataset.shape();
    const auto position = [](const std::optional<int> &value) { return value ? Json(*value) : Json(nullptr); };
    Json rois = Json::array(), overlays = Json::array();
    for (const auto &roi : data.rois)
      rois.push_back({{"name", roi.name}, {"t", position(roi.t)}, {"c", position(roi.c)},
          {"z", position(roi.z)}, {"attributes", metadataJson(roi.attributes)},
          {"selection", observedSelection(roi.selection)}});
    for (const auto &overlay : data.overlays) overlays.push_back(metadataJson(overlay));
    return {{"document_id", owner->id},
        {"data_identity", snapshot.data_identity() ? Json(*snapshot.data_identity()) : Json(nullptr)},
        {"dirty", snapshot.dirty()}, {"undo_steps", owner->undo_steps()}, {"redo_steps", owner->redo_steps()},
        {"revisions", {{"pixels", revisions.pixels}, {"metadata", revisions.metadata},
            {"selection", revisions.selection}, {"display", revisions.display}}},
        {"selection", observedSelection(data.selection)},
        {"previous_selection", observedSelection(data.previous_selection)}, {"rois", std::move(rois)},
        {"metadata", {{"title", data.metadata.title}, {"properties", metadataJson(data.metadata.properties)},
            {"extensions", metadataJson(data.metadata.extensions)}}},
        {"record_layout", metadataJson(data.metadata.record_layout)}, {"overlays", std::move(overlays)},
        {"display", observedDisplay(snapshot.display())},
        {"dataset", {{"dtype", "uint8"}, {"axis_sizes", {{"T", shape.t}, {"C", shape.c},
            {"Z", shape.z}, {"Y", shape.y}, {"X", shape.x}, {"S", shape.s}}},
            {"calibration", observedCalibration(data.dataset.calibration())}}},
        {"workflow", window_.workflow}, {"results", window_.results}};
  }
  void capture(const std::string &name) { report_["states"][name] = sourceSummary(owner_); }
  void frame(HeadlessInputSession &session, const std::string &label,
             const Json &events = Json::array()) {
    require(frameOrdinal_ < maxFrames, "ROI Boolean UI frame limit reached before input");
    budget();
    last_ = session.frame(events);
    std::ostringstream name;
    name << "frame-" << std::setw(4) << std::setfill('0') << ++frameOrdinal_ << ".json";
    Json actual{{"ordinal", frameOrdinal_}, {"label", label}, {"member_id", name.str()},
        {"requested_events", events}, {"rendered", last_}, {"native_state", sourceSummary(owner_)}};
    writeJson((directory_ / name.str()).u8string(), actual);
    report_["frames"].push_back(std::move(actual));
    budget();
  }
  Json control(const std::string &selector) const {
    if (selector.rfind("document.", 0) == 0) {
      const auto id = std::stoull(selector.substr(9));
      for (const auto &tab : last_.at("document_tabs"))
        if (tab.at("document_id").get<std::uint64_t>() == id) return tab;
      throw std::runtime_error("Observed document tab is absent");
    }
    return last_.at("host_controls").at(selector);
  }
  void click(HeadlessInputSession &session, const std::string &selector) {
    const auto actual = control(selector);
    require(actual.at("enabled") == true && actual.at("id").get<std::uint64_t>() > 0,
            "ROI Boolean pointer target is not an observed enabled item");
    const auto &rect = actual.at("rect");
    const auto &point = actual.at("point");
    const double x = std::floor(point[0].get<double>()), y = std::floor(point[1].get<double>());
    require(rect.size() == 4 && x > rect[0].get<double>() && x < rect[2].get<double>() &&
            y > rect[1].get<double>() && y < rect[3].get<double>(),
            "ROI Boolean pointer is outside observed bounds");
    report_["control_observations"].push_back({{"selector", selector}, {"control", actual},
        {"position", {x, y}}, {"before_frame_ordinal", frameOrdinal_}});
    frame(session, selector + ".position", Json::array({
        Json{{"type", "position"}, {"point", {x, y}}, {"space", "screen"}}}));
    frame(session, selector + ".down", Json::array({Json{{"type", "button"}, {"name", "left"}, {"down", true}}}));
    frame(session, selector + ".up", Json::array({Json{{"type", "button"}, {"name", "left"}, {"down", false}}}));
  }
  void modifierClick(HeadlessInputSession &session, std::size_t index,
                     bool ctrl, bool shift, const char *label) {
    Json keys = Json::array();
    if (ctrl) keys.push_back({{"name", "Ctrl"}, {"down", true}});
    if (shift) keys.push_back({{"name", "Shift"}, {"down", true}});
    if (!keys.empty()) frame(session, std::string(label) + ".modifiers.down",
        Json::array({Json{{"type", "keys"}, {"keys", keys}}}));
    click(session, "roi_manager.row." + std::to_string(index));
    for (auto &atom : keys) atom["down"] = false;
    if (!keys.empty()) frame(session, std::string(label) + ".modifiers.up",
        Json::array({Json{{"type", "keys"}, {"keys", keys}}}));
    frame(session, label);
  }
  void pair(HeadlessInputSession &session) {
    modifierClick(session, 0, true, false, "operand0_selected");
    modifierClick(session, 1, true, false, "operands_selected");
  }
  void openManager(HeadlessInputSession &session) {
    click(session, "roi_manager.tab");
    frame(session, "manager_open.1");
    frame(session, "manager_open.2");
    require(last_.at("roi_manager").at("visible") == true,
            "Actual ROI manager dock-tab click did not reveal its controls");
  }
  Json parameters(const DocumentSnapshot &snapshot) const {
    return {{"indices", {0, 1}}, {"operation", "union"}, {"document_id", snapshot.id()},
        {"metadata_revision", snapshot.revisions().metadata}};
  }
  void reject(const char *label, Json parameters,
              const DocumentSnapshot *captured = nullptr) {
    const auto before = sourceSummary(owner_);
    Json exception = nullptr;
    try { window_.action("combine_rois", parameters); }
    catch (const std::exception &error) { exception = exceptionJson(error); }
    Json observed{{"label", label}, {"parameters", parameters},
        {"exception", std::move(exception)}, {"before", before}, {"after", sourceSummary(owner_)}};
    if (captured) {
      observed["captured_document_id"] = captured->id();
      observed["captured_revisions"] = observedRevisions(captured->revisions());
    }
    report_["rejections"].push_back(std::move(observed));
  }
  void booleanCase(HeadlessInputSession &session) {
    pair(session);
    capture("before_apply");
    const auto id = std::string(oracleIds[number_ - 1]);
    const auto operation = id.substr(id.find("__") + 2);
    click(session, "roi_manager." + operation);
    frame(session, "applied");
    capture("after_apply");
    click(session, "roi_manager." + operation);
    frame(session, "noop");
    capture("after_noop");
  }
  void modifierCase(HeadlessInputSession &session) {
    modifierClick(session, 0, true, false, "modifier_one");
    modifierClick(session, 1, true, false, "modifier_pair");
    modifierClick(session, 0, true, false, "modifier_remove");
    modifierClick(session, 0, true, false, "modifier_anchor");
    modifierClick(session, 3, false, true, "range_forward");
    modifierClick(session, 1, false, true, "range_shrink");
    modifierClick(session, 3, true, true, "range_ctrl_extend");
  }
  void selectAllCase(HeadlessInputSession &session) {
    click(session, "roi_manager.select_all");
    frame(session, "select_all_button");
    modifierClick(session, 1, true, false, "select_all_toggle");
    frame(session, "select_all_keyboard.down", Json::array({simultaneousKeys({"Ctrl", "A"}, true)}));
    frame(session, "select_all_keyboard.up", Json::array({simultaneousKeys({"Ctrl", "A"}, false)}));
    frame(session, "select_all_keyboard");
  }
  void switchCase(HeadlessInputSession &session) {
    pair(session);
    click(session, "document." + std::to_string(other_->id));
    frame(session, "other_document.settle");
    frame(session, "other_document");
    click(session, "document." + std::to_string(owner_->id));
    frame(session, "original_document.settle");
    frame(session, "original_document");
  }
  void metadataCase(HeadlessInputSession &session) {
    pair(session);
    capture("replace_before");
    const auto originalRows = window_.documentById(owner_->id)->state().metadata.at("rois");
    auto edit = window_.beginEdit(owner_->id);
    auto rois = owner_->snapshot().data().rois;
    rois[0].name = "Replacement first row";
    edit.set_rois(rois);
    window_.commitEdit(edit);
    report_["api_inputs"].push_back({{"method", "set_rois"}, {"classification", "same-length replacement"},
        {"document_id", owner_->id}, {"name", "Replacement first row"}});
    frame(session, "after_replace");
    capture("replace_after");
    pair(session);
    click(session, "menu.Edit");
    frame(session, "undo_menu_open");
    require(!last_.at("input_frame").at("popup_stack").empty(),
            "Observed Edit menu did not retain its popup before Undo");
    click(session, "menu.Edit.Undo");
    frame(session, "after_undo");
    capture("undo_after");
    pair(session);
    capture("import_before");
    const auto importPath = directory_ / "identical-roi-input.json";
    writeJson(importPath.u8string(), {{"format", "imagejxx-rois"}, {"version", 1},
        {"rois", originalRows}});
    report_["api_inputs"].push_back({{"method", "action"}, {"name", "load_rois"},
        {"parameters", {{"path", importPath.u8string()}}}, {"input_rois", originalRows}});
    window_.action("load_rois", {{"path", importPath.u8string()}});
    frame(session, "after_import");
    capture("import_after");
  }
  void invalidCase(HeadlessInputSession &session) {
    pair(session);
    auto p = parameters(owner_->snapshot());
    p["indices"] = {0}; reject("insufficient", p);
    p["indices"] = {0, 0}; reject("duplicate", p);
    p["indices"] = {0, 99}; reject("out_of_range", p);
    p["indices"] = {0, 1}; p["operation"] = "OR"; reject("operation", p);
    p["operation"] = "union"; p["extra"] = true; reject("extra_parameter", p);
    auto edit = window_.beginEdit(owner_->id);
    auto rois = owner_->snapshot().data().rois;
    rois[1].selection = operand("line", {{4, 3}, {10, 7}});
    edit.set_rois(rois); window_.commitEdit(edit);
    report_["api_inputs"].push_back({{"method", "set_rois"}, {"classification", "nonarea admission preparation"},
        {"document_id", owner_->id}, {"kind", "line"}});
    frame(session, "nonarea_replaced");
    pair(session);
    reject("nonarea", parameters(owner_->snapshot()));
    frame(session, "nonarea_disabled");
  }
  void staleCase(HeadlessInputSession &session) {
    pair(session);
    const auto captured = owner_->snapshot();
    auto edit = window_.beginEdit(owner_->id);
    auto rois = captured.data().rois;
    rois[0].name = "Stale replacement";
    edit.set_rois(rois); window_.commitEdit(edit);
    report_["api_inputs"].push_back({{"method", "set_rois"}, {"classification", "captured-revision invalidation"},
        {"captured_document_id", captured.id()}, {"captured_metadata_revision", captured.revisions().metadata}});
    reject("stale_revision", parameters(captured), &captured);
    frame(session, "stale_invalidated");
  }
  void busyCase(HeadlessInputSession &session) {
    pair(session);
    struct Gate {
      std::mutex mutex;
      std::condition_variable condition;
      bool entered = false, released = false;
    };
    auto gate = std::make_shared<Gate>();
    const auto job = GaussianCancellationAccess::startBusyPreparation(window_, [gate](const JobContext &context) {
      std::unique_lock<std::mutex> lock(gate->mutex);
      gate->entered = true;
      gate->condition.notify_all();
      if (!gate->condition.wait_for(lock, std::chrono::seconds(5), [&] { return gate->released; }))
        throw std::runtime_error("ROI Boolean busy gate was not released within five seconds");
      lock.unlock();
      context.checkpoint();
    });
    struct Release {
      Window &window;
      Job job;
      std::shared_ptr<Gate> gate;
      ~Release() {
        try {
          // A failed observation must also retire this genuine worker before
          // returning a live Python Window to its caller.
          job.cancel();
          {
            std::lock_guard<std::mutex> lock(gate->mutex);
            gate->released = true;
            gate->condition.notify_all();
          }
          if (job.wait_for(std::chrono::seconds(2))) window.wait();
        } catch (...) {}
      }
    } release{window_, job, gate};
    {
      std::unique_lock<std::mutex> lock(gate->mutex);
      require(gate->condition.wait_for(lock, std::chrono::seconds(2), [&] { return gate->entered; }),
              "ROI Boolean busy job did not reach its real worker gate");
    }
    const auto running = job.info();
    report_["job_before_cleanup"] = {{"busy", window_.busy()}, {"id", running.id},
        {"document_id", running.document_id}, {"state", jobState(running.state)},
        {"cancel_requested", running.cancel_requested},
        {"usage", observedUsage(GaussianCancellationAccess::scheduler(window_)->usage())}};
    report_["api_inputs"].push_back({{"method", "startBusyPreparation"},
        {"classification", "genuine add(value0) worker gate; canceled before compute"},
        {"job_id", running.id}});
    frame(session, "busy_disabled");
    reject("busy", parameters(owner_->snapshot()));
    window_.cancel();
    {
      std::lock_guard<std::mutex> lock(gate->mutex);
      gate->released = true;
      gate->condition.notify_all();
    }
    require(job.wait_for(std::chrono::seconds(2)), "ROI Boolean canceled preparation did not terminate");
    window_.wait();
    const auto canceled = job.info();
    report_["job_after_cleanup"] = {{"busy", window_.busy()}, {"id", canceled.id},
        {"document_id", canceled.document_id}, {"state", jobState(canceled.state)},
        {"cancel_requested", canceled.cancel_requested},
        {"usage", observedUsage(GaussianCancellationAccess::scheduler(window_)->usage())}};
    frame(session, "busy_cleaned");
  }
  void closedCase(HeadlessInputSession &session) {
    pair(session);
    const auto captured = owner_->snapshot();
    window_.closeDocument(owner_->id, true);
    report_["api_inputs"].push_back({{"method", "closeDocument"}, {"document_id", owner_->id},
        {"discard", true}, {"captured_metadata_revision", captured.revisions().metadata}});
    reject("closed", parameters(captured), &captured);
    frame(session, "closed_invalidated.settle");
    frame(session, "closed_invalidated");
  }
};
} // namespace

Json Window::roiBooleanUiCase(const std::string &caseId, const std::string &context,
                             const std::string &directory) {
  checkThread();
  if (context != "imgui_cpp" && context != "imgui_python")
    throw std::invalid_argument("ROI Boolean UI context must name a real launch");
  if (!documents().empty() || busy())
    throw std::invalid_argument("ROI Boolean UI case requires a fresh idle Window");
  const auto number = caseNumber(caseId);
  const auto root = freshDirectory(directory);
  RoiBooleanUiDriver driver(*this, number, context, root);
  return driver.run();
}
int Window::roiBooleanUiConsumerTest(const std::string &directory) {
  checkThread();
  const auto root = freshDirectory(directory);
  const auto start = Clock::now();
  Json report{{"schema_version", 1}, {"contract_id", contract}, {"context", "imgui_cpp"},
      {"status", "observing"}, {"acceptance_claim", false}, {"case_count", caseIds.size()},
      {"limits", {{"max_seconds", aggregateSeconds}, {"max_case_frames", maxFrames},
          {"max_case_seconds", maxSeconds}}}, {"cases", Json::array()}};
  try {
    for (const auto caseId : caseIds) {
      require(elapsed(start) < aggregateSeconds, "ROI Boolean UI aggregate time limit reached");
      Window child((root / (std::string(caseId) + "-preferences.json")).u8string());
      report["cases"].push_back(child.roiBooleanUiCase(caseId, "imgui_cpp", (root / caseId).u8string()));
    }
    report["elapsed_seconds"] = elapsed(start);
    require(report["elapsed_seconds"].get<double>() <= aggregateSeconds,
            "ROI Boolean UI aggregate exceeded its time limit");
    report["status"] = "observed";
    writeJson((root / "roi-boolean-ui.json").u8string(), report);
    std::cout << "16 ROI Boolean UI cases emitted actual physical-input observations\n";
    return 0;
  } catch (const std::exception &error) {
    report["status"] = "failed";
    report["failure"] = exceptionJson(error);
    report["elapsed_seconds"] = elapsed(start);
    try { writeJson((root / "roi-boolean-ui-failed.json").u8string(), report); } catch (...) {}
    throw;
  }
}
} // namespace imagejxx::desktop
