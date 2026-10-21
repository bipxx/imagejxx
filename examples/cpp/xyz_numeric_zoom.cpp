// Installed public-SDK state consumer. This is not a GUI or renderer test.
#include <imagejxx/application.hpp>
#include <imagejxx/operations.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <locale>
#include <memory>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace {
using namespace imagejxx;
namespace fs = std::filesystem;
using Json = std::string;
constexpr const char *caseId = "P05d.xyz_numeric_zoom.cpp_api";
constexpr const char *contractSha = "3b4c1840d00a773da7b3e5e7fffba08c2292dce00b395ab0dc18da124137480b";
constexpr const char *sourceSha = "b3e0f65e2f86dd2790085a5e7023ed9a336258c7bfb73e5a1820509ecc1ae51c";
static_assert(sizeof(float) == 4 && sizeof(double) == 8 &&
              std::numeric_limits<float>::is_iec559 && std::numeric_limits<double>::is_iec559,
              "The declared comparison requires IEEE binary32/binary64");

void check(bool value, const char *message) {
  if (!value) throw std::runtime_error(message);
}
Json jsonString(const std::string &text) {
  const char *hex = "0123456789abcdef";
  Json result = "\"";
  for (unsigned char c : text) {
    if (c == '"' || c == '\\') { result += '\\'; result += char(c); }
    else if (c < 32) { result += "\\u00"; result += hex[c >> 4]; result += hex[c & 15]; }
    else result += char(c);
  }
  return result + '"';
}
Json boolean(bool value) { return value ? "true" : "false"; }
template <class T> Json number(T value) {
  std::ostringstream out;
  out.imbue(std::locale::classic());
  if constexpr (std::is_floating_point_v<T>) {
    check(std::isfinite(value), "Nonfinite report number must use raw bits instead");
    out << std::setprecision(std::numeric_limits<T>::max_digits10);
  }
  out << value;
  return out.str();
}
Json object(const std::vector<std::pair<std::string, Json>> &fields) {
  Json out = "{";
  for (std::size_t i = 0; i < fields.size(); ++i) {
    if (i) out += ',';
    out += jsonString(fields[i].first) + ':' + fields[i].second;
  }
  return out + '}';
}
Json array(const std::vector<Json> &values) {
  Json out = "[";
  for (std::size_t i = 0; i < values.size(); ++i) {
    if (i) out += ',';
    out += values[i];
  }
  return out + ']';
}
template <class Range> Json numbers(const Range &values) {
  std::vector<Json> encoded;
  for (auto value : values) encoded.push_back(number(value));
  return array(encoded);
}
Json metadata(const MetadataValue &value);
Json metadata(const MetadataObject &values) {
  std::vector<std::pair<std::string, Json>> result;
  for (const auto &[key, value] : values) result.emplace_back(key, metadata(value));
  return object(result);
}
Json metadata(const MetadataArray &values) {
  std::vector<Json> result;
  for (const auto &value : values) result.push_back(metadata(value));
  return array(result);
}
Json metadata(const MetadataValue &value) {
  return std::visit([](const auto &item) -> Json {
    using T = std::decay_t<decltype(item)>;
    if constexpr (std::is_same_v<T, std::monostate>) return "null";
    else if constexpr (std::is_same_v<T, bool>) return boolean(item);
    else if constexpr (std::is_arithmetic_v<T>) return number(item);
    else if constexpr (std::is_same_v<T, std::string>) return jsonString(item);
    else if constexpr (std::is_same_v<T, std::vector<std::string>>) {
      std::vector<Json> result;
      for (const auto &text : item) result.push_back(jsonString(text));
      return array(result);
    } else {
      if (!item) return "null";
      return std::visit([](const auto &tree) { return metadata(tree); }, item->value);
    }
  }, static_cast<const MetadataVariant &>(value));
}
std::string doubleBits(double value) {
  std::uint64_t bits;
  std::memcpy(&bits, &value, sizeof(bits));
  std::ostringstream out;
  out << "0x" << std::hex << std::setfill('0') << std::setw(16) << bits;
  return out.str();
}
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
  std::array<std::uint32_t, 8> hash{0x6a09e667,0xbb67ae85,0x3c6ef372,0xa54ff53a,0x510e527f,0x9b05688c,0x1f83d9ab,0x5be0cd19};
  auto bytes = input;
  bytes.push_back(0x80);
  while (bytes.size() % 64 != 56) bytes.push_back(0);
  const auto bits = std::uint64_t(input.size()) * 8;
  for (int i = 56; i >= 0; i -= 8) bytes.push_back(static_cast<unsigned char>(bits >> i));
  auto rotate = [](std::uint32_t value, int n) { return (value >> n) | (value << (32 - n)); };
  for (std::size_t offset = 0; offset < bytes.size(); offset += 64) {
    std::array<std::uint32_t, 64> w{};
    for (std::size_t i = 0; i < 16; ++i)
      for (std::size_t j = 0; j < 4; ++j) w[i] = (w[i] << 8) | bytes[offset + 4 * i + j];
    for (std::size_t i = 16; i < 64; ++i) {
      const auto a = w[i - 15], b = w[i - 2];
      w[i] = w[i - 16] + (rotate(a, 7) ^ rotate(a, 18) ^ (a >> 3)) + w[i - 7] +
             (rotate(b, 17) ^ rotate(b, 19) ^ (b >> 10));
    }
    auto work = hash;
    for (std::size_t i = 0; i < 64; ++i) {
      const auto first = work[7] + (rotate(work[4], 6) ^ rotate(work[4], 11) ^ rotate(work[4], 25)) +
          ((work[4] & work[5]) ^ (~work[4] & work[6])) + k[i] + w[i];
      const auto second = (rotate(work[0], 2) ^ rotate(work[0], 13) ^ rotate(work[0], 22)) +
          ((work[0] & work[1]) ^ (work[0] & work[2]) ^ (work[1] & work[2]));
      for (std::size_t j = 7; j > 0; --j) work[j] = work[j - 1];
      work[4] += first;
      work[0] = first + second;
    }
    for (std::size_t i = 0; i < 8; ++i) hash[i] += work[i];
  }
  std::ostringstream out;
  out << std::hex << std::setfill('0');
  for (auto value : hash) out << std::setw(8) << value;
  return out.str();
}
std::vector<unsigned char> imageBytes(const Image &image) {
  check(image.type == PixelType::Float32 && image.channels == 1, "Expected gray Float32 actual samples");
  std::vector<unsigned char> out;
  out.reserve(image.pixels.size() * 4);
  for (double value : image.pixels) {
    const auto bits = float32_source_sample_bits(value);
    for (unsigned i = 0; i < 4; ++i) out.push_back(static_cast<unsigned char>(bits >> (8 * i)));
  }
  return out;
}
std::vector<unsigned char> datasetBytes(const Dataset &dataset) {
  std::vector<unsigned char> out;
  const auto shape = dataset.shape();
  for (int z = 0; z < shape.z; ++z) {
    const auto bytes = imageBytes(*dataset.plane({0, 0, z}));
    out.insert(out.end(), bytes.begin(), bytes.end());
  }
  return out;
}
Json calibrationJson(const SpatialCalibration &c) {
  return object({{"spacing", numbers(c.spacing)}, {"origin", numbers(c.origin)},
                 {"direction", numbers(c.direction)}, {"unit", jsonString(c.unit)},
                 {"frame_interval", number(c.frame_interval)}, {"time_origin", number(c.time_origin)},
                 {"time_unit", jsonString(c.time_unit)}});
}
Json displayJson(const DisplayState &v) {
  std::vector<Json> pans;
  for (const auto &pair : v.pan) pans.push_back(numbers(pair));
  return object({{"t", number(v.t)}, {"c", number(v.c)}, {"orthogonal", boolean(v.orthogonal)},
                 {"linked_zoom", boolean(v.linkedZoom)}, {"crosshair", numbers(v.crosshair)},
                 {"zoom", number(v.zoom)}, {"plane_zoom", numbers(v.planeZoom)}, {"pan", array(pans)},
                 {"revision", number(v.revision)}, {"properties", metadata(v.properties)}});
}
Json selectionJson(const Selection &v) {
  std::vector<Json> points;
  for (const auto &point : v.points) points.push_back(numbers(point));
  return object({{"kind", jsonString(v.kind)}, {"points", array(points)}, {"width", number(v.width)},
                 {"attributes", metadata(v.attributes)}});
}
Json historyJson(const HistoryInfo &v) {
  return object({{"undo_steps", number(v.undo_steps)}, {"redo_steps", number(v.redo_steps)},
                 {"step_limit", number(v.step_limit)}, {"pixel_budget", number(v.pixel_budget)},
                 {"pixels", object({{"logical_bytes", number(v.pixels.logical_bytes)},
                     {"current_bytes", number(v.pixels.current_bytes)}, {"retained_bytes", number(v.pixels.retained_bytes)},
                     {"obsolete_bytes", number(v.pixels.obsolete_bytes)}, {"retained_planes", number(v.pixels.retained_planes)},
                     {"exact", boolean(v.pixels.exact)}})}});
}
bool sameCamera(const DisplayState &a, const DisplayState &b) {
  if (a.t != b.t || a.c != b.c || a.orthogonal != b.orthogonal || a.linkedZoom != b.linkedZoom ||
      a.crosshair != b.crosshair || a.pan != b.pan || doubleBits(a.zoom) != doubleBits(b.zoom) ||
      !metadata_equal(a.properties, b.properties)) return false;
  for (int i = 0; i < 3; ++i)
    if (doubleBits(a.planeZoom[i]) != doubleBits(b.planeZoom[i])) return false;
  return true;
}
void unchangedData(const DocumentSnapshot &before, const DocumentSnapshot &after) {
  check(before.id() == after.id() && before.data_identity() == after.data_identity() &&
        before.shares_data(after) && before.dirty() == after.dirty() &&
        before.revisions().matches(after.revisions(), Change::Data), "Camera/invalid setter changed committed source");
  check(datasetBytes(before.data().dataset) == datasetBytes(after.data().dataset), "Committed source raw samples changed");
}
double compatibleZoom(float percent, double horizontalSpacing) {
  // Explicit SDK input derivation; the public DisplayState setter remains Double.
  const float bounded = std::clamp(percent, .01f, 6400.f);
  const volatile float fraction = bounded / 100.0f;
  return static_cast<double>(fraction) / horizontalSpacing;
}

struct Output {
  fs::path report, directory;
  std::vector<Json> checkpoints, invalids, apiInputs;
  Json finalCleanup = "null", originalAfter = "null", pixelEdit = "null";
  std::chrono::steady_clock::time_point deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
  explicit Output(fs::path requested) {
    report = fs::absolute(std::move(requested)).lexically_normal();
    directory = report.parent_path();
    check(report.filename() == "cpp-api.json" && directory != directory.root_path(), "Require owned fresh cpp-api.json directory");
    fs::create_directories(directory.parent_path());
    check(fs::create_directory(directory), "SDK observation directory must be fresh; preserve old attempts");
  }
  void bound() const { check(std::chrono::steady_clock::now() < deadline, "SDK consumer exceeded 30 seconds"); }
  void bytes(const std::string &name, const std::vector<unsigned char> &value) const {
    bound();
    const auto path = directory / name;
    check(!fs::exists(path), "SDK observation member must be fresh");
    std::ofstream out(path, std::ios::binary);
    check(bool(out), "Cannot create SDK observation member");
    out.write(reinterpret_cast<const char *>(value.data()), static_cast<std::streamsize>(value.size()));
    out.flush();
    check(bool(out), "SDK observation write/flush failed");
    out.close();
    check(!out.fail(), "SDK observation close failed");
  }
  void text(const std::string &name, const Json &value) const {
    const auto terminated = value + '\n';
    bytes(name, std::vector<unsigned char>(terminated.begin(), terminated.end()));
  }
  Json raw(const std::string &name, const std::vector<unsigned char> &value, const Json &shape) const {
    bytes(name, value);
    return object({{"member_id", jsonString(name)}, {"path", jsonString((directory / name).u8string())},
                   {"dtype", jsonString("float32")}, {"encoding", jsonString("IEEE754 Float32LE")},
                   {"shape", shape}, {"sample_count", number(value.size() / 4)},
                   {"byte_count", number(value.size())}, {"sha256", jsonString(sha256(value))}});
  }
  Json state(const std::string &label, Application &app, std::uint64_t id) const {
    bound();
    const auto snapshot = app.snapshot(id);
    const auto &data = snapshot.data();
    const auto shape = data.dataset.shape();
    const auto &view = snapshot.display();
    const auto revisions = snapshot.revisions();
    const auto history = app.history(id);
    std::vector<Json> rois, overlays, planes, zoomBits;
    for (const auto &roi : data.rois)
      rois.push_back(object({{"name", jsonString(roi.name)}, {"selection", selectionJson(roi.selection)},
          {"t", roi.t ? number(*roi.t) : "null"}, {"c", roi.c ? number(*roi.c) : "null"},
          {"z", roi.z ? number(*roi.z) : "null"}, {"attributes", metadata(roi.attributes)}}));
    for (const auto &overlay : data.overlays) overlays.push_back(metadata(overlay));
    for (double value : view.planeZoom) zoomBits.push_back(jsonString(doubleBits(value)));
    const std::array<Plane, 3> orientations{Plane::XY, Plane::XZ, Plane::YZ};
    const std::array<const char *, 3> names{"XY", "XZ", "YZ"};
    const std::array<int, 3> coordinates{view.crosshair[2], view.crosshair[1], view.crosshair[0]};
    for (int i = 0; i < 3; ++i) {
      const auto image = data.dataset.extract_plane(orientations[i], coordinates[i], view.t, view.c);
      const auto geometry = data.dataset.plane_geometry(orientations[i], coordinates[i]);
      const int x = i == 2 ? view.crosshair[1] : view.crosshair[0];
      const int y = i == 0 ? view.crosshair[1] : view.crosshair[2];
      planes.push_back(object({{"plane", jsonString(names[i])}, {"coordinate", number(coordinates[i])},
          {"raw", raw(label + '-' + names[i] + ".f32le", imageBytes(image), numbers(std::array<int, 2>{image.height, image.width}))},
          {"geometry", object({{"origin", numbers(geometry.origin)}, {"horizontal_step", numbers(geometry.horizontal_step)},
              {"vertical_step", numbers(geometry.vertical_step)}, {"horizontal_axis", jsonString(geometry.horizontal_axis)},
              {"vertical_axis", jsonString(geometry.vertical_axis)}, {"unit", jsonString(geometry.unit)}})},
          {"landmark_sample", number(image.at(x, y))}}));
    }
    const auto world = data.dataset.calibration().voxel_to_world(
        {double(view.crosshair[0]), double(view.crosshair[1]), double(view.crosshair[2])});
    const auto image = data.dataset.plane({view.t, view.c, view.crosshair[2]});
    const auto observed = object({{"document_id", number(snapshot.id())},
        {"data_identity", snapshot.data_identity() ? number(*snapshot.data_identity()) : "null"},
        {"shape_tczyxs", numbers(std::array<int, 6>{shape.t, shape.c, shape.z, shape.y, shape.x, shape.s})},
        {"dtype", jsonString("float32")}, {"dirty", boolean(snapshot.dirty())},
        {"undo_steps", number(history.undo_steps)}, {"redo_steps", number(history.redo_steps)}, {"history", historyJson(history)},
        {"revisions", object({{"pixels", number(revisions.pixels)}, {"metadata", number(revisions.metadata)},
            {"selection", number(revisions.selection)}, {"display", number(revisions.display)}})},
        {"calibration", calibrationJson(data.dataset.calibration())}, {"display", displayJson(view)},
        {"camera_double_bits", object({{"zoom", jsonString(doubleBits(view.zoom))}, {"plane_zoom", array(zoomBits)}})},
        {"metadata", object({{"title", jsonString(data.metadata.title)}, {"properties", metadata(data.metadata.properties)},
            {"extensions", metadata(data.metadata.extensions)}, {"record_layout", metadata(data.metadata.record_layout)}})},
        {"selection", data.selection ? selectionJson(*data.selection) : "null"},
        {"previous_selection", data.previous_selection ? selectionJson(*data.previous_selection) : "null"},
        {"rois", array(rois)}, {"overlays", array(overlays)}, {"spectrum_present", boolean(data.spectrum.has_value())},
        {"source_raw", raw(label + "-source.f32le", datasetBytes(data.dataset), numbers(std::array<int, 3>{shape.z, shape.y, shape.x}))},
        {"planes", array(planes)}, {"world_xyz", numbers(world)},
        {"landmark_value", number(image->at(view.crosshair[0], view.crosshair[1]))}});
    text(label + "-state.json", observed);
    return observed;
  }
  void checkpoint(const std::string &name, Application &app, std::uint64_t id) {
    checkpoints.push_back(object({{"name", jsonString(name)}, {"state", state(name, app, id)}}));
  }
  Json reportJson(const std::string &status, const std::string &error = {}) const {
    return object({{"schema_version", number(1)}, {"context", jsonString("cpp_api")}, {"case_id", jsonString(caseId)},
        {"status", jsonString(status)}, {"contract_sha256", jsonString(contractSha)}, {"acceptance_claim", "false"},
        {"source_fixture_id", jsonString("plan_xyz_float32")}, {"source_formula", jsonString("10000*z+100*y+x")},
        {"raw_scope", jsonString("actual public Dataset.extract_plane; no renderer/cache/GPU")},
        {"percentage_math_scope", jsonString("explicit Float32 compatibility arithmetic derives Double public-setter inputs; no widget input claimed")},
        {"workflow_observation", object({{"available", "false"},
            {"scope", jsonString("public Application has no recorder API; actual metadata.record_layout retained")}})},
        {"GUI_observed", "false"}, {"cache_observed", "false"}, {"upstream_match_claim", "false"},
        {"checkpoints", array(checkpoints)}, {"invalid_displays", array(invalids)}, {"api_inputs", array(apiInputs)},
        {"pixel_edit", pixelEdit}, {"original_after_raw", originalAfter}, {"final_cleanup", finalCleanup},
        {"error", error.empty() ? "null" : jsonString(error)}});
  }
};

std::shared_ptr<Document> fixture() {
  Stack stack;
  for (int z = 0; z < 9; ++z) {
    Image image(32, 17, 1, PixelType::Float32);
    for (int y = 0; y < 17; ++y)
      for (int x = 0; x < 32; ++x) image.at(x, y) = 10000 * z + 100 * y + x;
    stack.push_back(std::move(image));
  }
  SpatialCalibration calibration;
  calibration.spacing = {.4, 1.2, 3};
  calibration.origin = {10, -5, 2};
  calibration.direction = {0, -1, 0, 1, 0, 0, 0, 0, 1};
  calibration.unit = "um";
  DocumentData data{Dataset::from_stack(std::move(stack), calibration)};
  data.metadata.title = "P05d owned calibrated XYZ numeric zoom";
  data.metadata.properties["zoom_property"] = "preserve";
  data.metadata.extensions["zoom_unknown"] = "keep";
  data.rois.push_back({"retained-zoom-roi", Selection("rectangle", {{2, 3}, {10, 11}}),
                      std::nullopt, std::nullopt, 3, {{"zoom_unknown", "roi-kept"}}});
  data.overlays.push_back({{"kind", "point"}, {"points", metadata_array({metadata_array({7., 5.})})},
                           {"z", 3}, {"zoom_unknown", "overlay-kept"}});
  DisplayState view;
  view.orthogonal = true;
  view.linkedZoom = false;
  view.crosshair = {7, 5, 3};
  view.zoom = 1.25;
  view.planeZoom = {2, 3, 4};
  view.pan = {{{3.25, -2.5}, {5.5, -4.25}, {7.75, -6.5}}};
  view.properties["display_range"] = metadata_array({0., 81631.});
  return std::make_shared<Document>(std::move(data), std::move(view));
}
void run(Output &out) {
  Application app;
  auto owner = fixture();
  const auto id = app.adopt(owner);
  app.mark_saved(id);
  const auto original = app.snapshot(id);
  const auto sourceBytes = datasetBytes(original.data().dataset);
  out.apiInputs.push_back(object({{"method", jsonString("Application.adopt/mark_saved")}, {"document_id", number(id)},
                                 {"purpose", jsonString("owned calibrated fixture preparation")}}));
  out.checkpoint("baseline", app, id);
  check(sha256(sourceBytes) == sourceSha && !original.dirty() && app.history(id).undo_steps == 0,
        "SDK baseline source/history differs from declared fixture");
  auto view = original.display();
  view.planeZoom[1] = compatibleZoom(150.f, original.data().dataset.calibration().spacing[0]);
  app.set_display(view, id);
  out.checkpoint("independent_xz150", app, id);
  auto after = app.snapshot(id);
  unchangedData(original, after);
  check(doubleBits(after.display().planeZoom[1]) == doubleBits(3.75) &&
        after.display().planeZoom[0] == 2 && after.display().planeZoom[2] == 4 &&
        after.display().zoom == original.display().zoom && after.display().pan == original.display().pan &&
        after.revisions().display == original.revisions().display + 1 && app.history(id).undo_steps == 0,
        "SDK independent XZ update changed another camera or pixel history");
  const auto xz = after;
  view = xz.display();
  view.planeZoom[2] = compatibleZoom(60.f, original.data().dataset.calibration().spacing[1]);
  app.set_display(view, id);
  out.checkpoint("independent_yz60", app, id);
  after = app.snapshot(id);
  unchangedData(original, after);
  check(doubleBits(after.display().planeZoom[2]) == doubleBits(0.50000001986821496) &&
        after.display().planeZoom[0] == xz.display().planeZoom[0] &&
        after.display().planeZoom[1] == xz.display().planeZoom[1] &&
        after.display().zoom == xz.display().zoom && after.display().pan == xz.display().pan,
        "SDK independent YZ update changed another camera");
  view = after.display();
  view.linkedZoom = true;
  app.set_display(view, id);
  out.checkpoint("linked_baseline", app, id);
  const auto linkedBefore = app.snapshot(id);
  view = linkedBefore.display();
  view.zoom = compatibleZoom(120.f, original.data().dataset.calibration().spacing[1]);
  app.set_display(view, id);
  out.checkpoint("linked_yz120", app, id);
  const auto cameraFinal = app.snapshot(id);
  unchangedData(original, cameraFinal);
  check(doubleBits(cameraFinal.display().zoom) == doubleBits(1.0000000397364299) &&
        cameraFinal.display().planeZoom == linkedBefore.display().planeZoom &&
        cameraFinal.display().pan == linkedBefore.display().pan && app.history(id).undo_steps == 0,
        "SDK linked update changed independent cameras/pan/history");
  out.apiInputs.push_back(object({{"method", jsonString("Application.set_display")},
      {"purpose", jsonString("public Double camera updates from explicitly derived compatibility values")},
      {"percentages", numbers(std::array<int, 3>{150, 60, 120})},
      {"axis_spacing", numbers(std::array<double, 3>{.4, 1.2, 1.2})},
      {"actual_camera_inputs", numbers(std::array<double, 3>{xz.display().planeZoom[1],
          linkedBefore.display().planeZoom[2], cameraFinal.display().zoom})}}));
  const std::array<const char *, 5> names{"nan", "positive_infinity", "negative_infinity", "negative", "above_maximum"};
  const std::array<double, 5> invalids{std::numeric_limits<double>::quiet_NaN(),
      std::numeric_limits<double>::infinity(), -std::numeric_limits<double>::infinity(), -1., 1e9 + 1};
  for (std::size_t i = 0; i < invalids.size(); ++i) {
    const auto before = app.snapshot(id);
    const auto beforeJson = out.state(std::string("invalid_") + names[i] + "_before", app, id);
    auto proposed = before.display();
    proposed.zoom = invalids[i];
    std::string error;
    try { app.set_display(proposed, id); }
    catch (const std::invalid_argument &failure) { error = failure.what(); }
    const auto afterJson = out.state(std::string("invalid_") + names[i] + "_after", app, id);
    out.invalids.push_back(object({{"name", jsonString(names[i])}, {"requested_zoom_bits", jsonString(doubleBits(invalids[i]))},
        {"exception_class", error.empty() ? "null" : jsonString("invalid_argument")},
        {"error", error.empty() ? "null" : jsonString(error)}, {"before", beforeJson}, {"after", afterJson}}));
    out.text(std::string("invalid_") + names[i] + ".json", out.invalids.back());
    const auto unchanged = app.snapshot(id);
    check(!error.empty(), "SDK invalid direct display was not rejected");
    unchangedData(before, unchanged);
    check(sameCamera(before.display(), unchanged.display()) &&
          before.display().revision == unchanged.display().revision &&
          before.revisions().matches(unchanged.revisions(), Change::All), "SDK invalid setter was not atomic");
  }
  TargetRequest request;
  request.scope = TargetScope::CurrentPlane;
  request.roi = RoiPolicy::WholePlane;
  const auto target = app.resolve_target(request, id, "add");
  check(target.position_count() == 1 && target.position(0).t == 0 && target.position(0).c == 0 &&
        target.position(0).z == 3, "SDK pixel transaction did not capture Z3");
  auto replacement = *target.source().data().dataset.plane(target.position(0));
  for (double &value : replacement.pixels) value += 1;
  auto transaction = target.begin_edit();
  transaction.replace_plane(target.position(0), std::move(replacement));
  app.validate_target(target);
  app.commit(transaction);
  out.pixelEdit = object({{"method", jsonString("target.begin_edit/replace_plane/Application.commit")},
      {"target", metadata(target.record())}, {"position", numbers(std::array<int, 3>{0, 0, 3})},
      {"delta", number(1)}, {"scope", jsonString("every sample of captured Z3, one transaction")}});
  out.checkpoint("changededit", app, id);
  const auto changed = app.snapshot(id);
  check(app.history(id).undo_steps == 1 && changed.dirty() && sameCamera(changed.display(), cameraFinal.display()) &&
        changed.data().dataset.plane({0, 0, 3})->at(7, 5) == 30508, "SDK changed Apply did not retain cameras/one history step");
  app.undo(id);
  out.checkpoint("undo", app, id);
  const auto undone = app.snapshot(id);
  check(datasetBytes(undone.data().dataset) == sourceBytes && !undone.dirty() &&
        app.history(id).undo_steps == 0 && app.history(id).redo_steps == 1 &&
        sameCamera(undone.display(), cameraFinal.display()), "SDK Undo did not restore pixels/preserve cameras");
  app.redo(id);
  out.checkpoint("redo", app, id);
  check(datasetBytes(app.snapshot(id).data().dataset) == datasetBytes(changed.data().dataset) &&
        app.history(id).undo_steps == 1 && app.history(id).redo_steps == 0 &&
        sameCamera(app.snapshot(id).display(), cameraFinal.display()), "SDK Redo did not restore the changed data/cameras");
  out.originalAfter = out.raw("original-after.f32le", datasetBytes(original.data().dataset), numbers(std::array<int, 3>{9, 17, 32}));
  check(datasetBytes(original.data().dataset) == sourceBytes, "SDK transaction changed retained original buffers");
  app.close(id, true);
  const auto scheduler = app.job_scheduler();
  const auto usage = scheduler->usage();
  std::vector<Json> jobs;
  for (const auto &job : scheduler->jobs())
    jobs.push_back(object({{"id", number(job.id)}, {"state_enum", number(static_cast<int>(job.state))},
                           {"document_id", number(job.document_id)}, {"error", jsonString(job.error)}}));
  out.finalCleanup = object({{"application_document_ids", numbers(app.document_ids())},
      {"active_document_id", number(app.active_document_id())},
      {"usage", object({{"running", number(usage.running)}, {"queued", number(usage.queued)},
          {"running_bytes", number(usage.running_bytes)}, {"retained_bytes", number(usage.retained_bytes)},
          {"reserved_bytes", number(usage.reserved_bytes)}})}, {"actual_native_jobs", array(jobs)},
      {"original_after_close_raw", out.raw("original-after-close.f32le", datasetBytes(original.data().dataset), numbers(std::array<int, 3>{9, 17, 32}))},
      {"source_snapshot_readable_after_close", boolean(datasetBytes(original.data().dataset) == sourceBytes)}});
  out.text("case-unverified.json", out.reportJson("unverified"));
  check(app.document_ids().empty() && app.active_document_id() == 0 && !app.contains(id) &&
        usage.running == 0 && usage.queued == 0 && usage.reserved_bytes == 0 &&
        usage.running_bytes == 0 && usage.retained_bytes == 0 && jobs.empty(), "SDK cleanup retained membership/native work");
  out.text("cpp-api.json", out.reportJson("passed"));
}
} // namespace

int main() {
  std::unique_ptr<Output> output;
  try {
    const char *configured = std::getenv("IMAGEJXX_XYZ_NUMERIC_ZOOM_CPP_API_REPORT");
    auto path = configured && *configured ? fs::u8path(configured) : fs::current_path() / "xyz-numeric-zoom-report" / "cpp-api.json";
    output = std::make_unique<Output>(std::move(path));
    run(*output);
    std::cout << "PASS " << caseId << '\n';
    return 0;
  } catch (const std::exception &error) {
    if (output) try { output->text("cpp-api-failed.json", output->reportJson("failed", error.what())); } catch (...) {}
    std::cerr << "FAIL " << caseId << ": " << error.what() << '\n';
    return 1;
  }
}
