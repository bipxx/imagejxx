#include "window.hpp"

#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace imagejxx::desktop {
namespace {
using XYZ = std::array<int, 3>;
constexpr XYZ initial{7, 5, 3}, sizes{32, 17, 9};
constexpr std::array<Plane, 3> views{Plane::XY, Plane::XZ, Plane::YZ};
constexpr std::array<int, 3> horizontal{0, 0, 1}, vertical{1, 2, 2},
    fixed{2, 1, 0};
constexpr std::array<const char *, 3> names{"XY", "XZ", "YZ"};
constexpr const char *sourceSha =
    "b3e0f65e2f86dd2790085a5e7023ed9a336258c7bfb73e5a1820509ecc1ae51c";
constexpr std::array<const char *, 3> initialPlaneSha{
    "00e642b096d4464a9be44cf649d867331f59532ef0aff46244ef65614f2f2091",
    "28de57c30d6e32e46829805f59e7a3d211c8678100aeff5b9bdffa0753052b68",
    "ae1c1a123840804ff5fe4e860eec68f7d45b48d72eb5b086e690a6e5ffa51add"};

void require(bool condition, const char *message) {
  if (!condition)
    throw std::runtime_error(message);
}

double voxel(const XYZ &xyz) {
  return 10000 * xyz[2] + 100 * xyz[1] + xyz[0];
}

std::array<double, 3> expectedWorld(const XYZ &xyz) {
  // Integer decimal numerators are independent of the native transform.
  return {(100 - 12 * xyz[1]) / 10., (-50 + 4 * xyz[0]) / 10.,
          double(2 + 3 * xyz[2])};
}

SpatialCalibration calibration() {
  SpatialCalibration result;
  result.spacing = {.4, 1.2, 3};
  result.origin = {10, -5, 2};
  result.direction = {0, -1, 0, 1, 0, 0, 0, 0, 1};
  result.unit = "um";
  return result;
}

Dataset scientificDataset() {
  DatasetShape shape;
  shape.x = sizes[0];
  shape.y = sizes[1];
  shape.z = sizes[2];
  Stack planes;
  for (int z = 0; z < sizes[2]; ++z) {
    Image image(sizes[0], sizes[1], 1, PixelType::Float32);
    for (int y = 0; y < sizes[1]; ++y)
      for (int x = 0; x < sizes[0]; ++x)
        image.at(x, y) = voxel({x, y, z});
    planes.push_back(std::move(image));
  }
  return Dataset::from_planes(shape, std::move(planes), calibration());
}

void addSource(Window &window, const std::string &title = "Rotated XYZ fixture",
               bool orthogonal = true, XYZ xyz = initial) {
  window.addNativeDocument(std::make_shared<imagejxx::Document>(
      scientificDataset(), title));
  window.action("display_range", {{"minimum", 0}, {"maximum", 81631}});
  window.document()->setView({{"orthogonal", orthogonal}, {"crosshair", xyz}});
}

void writeFloat32(const std::filesystem::path &path,
                  const std::vector<double> &pixels) {
  static_assert(sizeof(float) == 4 && std::numeric_limits<float>::is_iec559,
                "Canonical scientific fixture requires IEEE754 Float32");
  std::vector<unsigned char> raw;
  raw.reserve(pixels.size() * 4);
  for (double value : pixels) {
    const float packed = static_cast<float>(value);
    require(std::isfinite(value) && double(packed) == value,
            "Scientific fixture is not exact Float32");
    std::uint32_t bits;
    std::memcpy(&bits, &packed, sizeof(bits));
    for (unsigned shift : {0U, 8U, 16U, 24U})
      raw.push_back(static_cast<unsigned char>((bits >> shift) & 255));
  }
  writeBytes(path.u8string(), raw);
  require(readBytes(path.u8string()) == raw,
          "Scientific Float32 artifact bytes were not written exactly");
}

std::vector<double> sourcePixels(const DocumentSnapshot &snapshot) {
  std::vector<double> pixels;
  for (int z = 0; z < sizes[2]; ++z) {
    const auto image = snapshot.data().dataset.plane({0, 0, z});
    require(image->width == sizes[0] && image->height == sizes[1] &&
                image->channels == 1 && image->type == PixelType::Float32,
            "Canonical source geometry/type changed");
    for (int y = 0; y < sizes[1]; ++y)
      for (int x = 0; x < sizes[0]; ++x) {
        require(image->at(x, y) == voxel({x, y, z}),
                "Canonical source pixel changed");
        pixels.push_back(image->at(x, y));
      }
  }
  return pixels;
}

Json snapshotInfo(const DocumentSnapshot &snapshot) {
  const auto &cal = snapshot.data().dataset.calibration();
  const auto revisions = snapshot.revisions();
  return {{"id", snapshot.id()},
          {"data_identity", snapshot.data_identity()
                                ? Json(*snapshot.data_identity()) : Json(nullptr)},
          {"dirty", snapshot.dirty()},
          {"crosshair_xyz", snapshot.display().crosshair},
          {"orthogonal", snapshot.display().orthogonal},
          {"revisions", {{"pixels", revisions.pixels},
                          {"metadata", revisions.metadata},
                          {"selection", revisions.selection},
                          {"display", revisions.display}}},
          {"calibration", {{"spacing", cal.spacing}, {"origin", cal.origin},
                            {"direction", cal.direction}, {"unit", cal.unit}}},
          {"metadata", metadataJson(snapshot.data().metadata.extensions)},
          {"selection", snapshot.data().selection
                            ? selectionJson(*snapshot.data().selection) : Json(nullptr)},
          {"roi_count", snapshot.data().rois.size()},
          {"overlay_count", snapshot.data().overlays.size()}};
}

Json point(const Json &location) {
  require(location.is_array() && location.size() == 2 &&
              location[0].is_number() && location[1].is_number() &&
              number(location[0]) >= 0 && number(location[1]) >= 0,
          "XYZ keyboard producer has no visible input target");
  return location;
}

Json imagePoint(const Json &rendered, int plane, XYZ xyz) {
  const auto &panel = rendered.at("orthogonal_planes").at(plane);
  const auto &origin = panel.at("origin"), &scale = panel.at("scale");
  const auto &canvas = panel.at("canvas_point"), &extent = panel.at("extent");
  require(boolean(panel.at("visible")),
          "XYZ pointer target needs a currently visible plane canvas");
  const std::array<int, 2> axes{horizontal[plane], vertical[plane]};
  std::array<double, 2> raster{};
  for (int screenAxis = 0; screenAxis < 2; ++screenAxis) {
    const int index = xyz[axes[screenAxis]];
    const double start = number(origin[screenAxis]), pitch = number(scale[screenAxis]);
    require(index >= 0 && index < sizes[axes[screenAxis]] &&
                std::isfinite(start) && std::isfinite(pitch) && pitch > 0,
            "XYZ pointer target has invalid voxel geometry");
    const double lower = start + index * pitch;
    const double upper = start + (index + 1) * pitch;
    // ImGui rasterizes mouse positions. Supply an integral point rather than
    // letting its flooring move a fractional voxel-center click to a neighbor.
    raster[screenAxis] = std::round(start + (index + .5) * pitch);
    const double canvasLower = number(canvas[screenAxis]) - number(extent[screenAxis]) / 2;
    const double canvasUpper = number(canvas[screenAxis]) + number(extent[screenAxis]) / 2;
    require(std::isfinite(raster[screenAxis]) && raster[screenAxis] > lower &&
                raster[screenAxis] < upper && raster[screenAxis] > canvasLower &&
                raster[screenAxis] < canvasUpper,
            "Integral XYZ pointer target is outside the intended voxel or canvas");
  }
  return point(Json(raster));
}

Json key(const std::string &name, bool down, int frames = 1) {
  return {{"type", "key"}, {"name", name}, {"down", down}, {"frames", frames}};
}

Json batch(std::initializer_list<std::pair<std::string, bool>> entries,
           int frames = 1) {
  Json keys = Json::array();
  for (const auto &entry : entries)
    keys.push_back({{"name", entry.first}, {"down", entry.second}});
  return {{"type", "keys"}, {"keys", std::move(keys)}, {"frames", frames}};
}

void append(Json &destination, const Json &events) {
  for (const auto &event : events)
    destination.push_back(event);
}

Json click(const Json &location) {
  return Json::array({{{"type", "position"}, {"space", "screen"}, {"point", point(location)}},
                      {{"type", "button"}, {"name", "left"}, {"down", true}},
                      {{"type", "button"}, {"name", "left"}, {"down", false}},
                      {{"type", "wait"}, {"frames", 2}}});
}

Json press(const std::string &name, bool shift = false, int holdFrames = 1) {
  if (shift)
    return Json::array({batch({{"Shift", true}, {name, true}}, holdFrames),
                        batch({{name, false}, {"Shift", false}}, 2)});
  return Json::array({key(name, true, holdFrames), key(name, false, 2)});
}

Json focusedEvents(const Json &rendered, int plane, XYZ xyz = initial) {
  auto events = click(imagePoint(rendered, plane, xyz));
  events.push_back({{"type", "position"}, {"space", "screen"}, {"point", {1220, 790}}});
  return events;
}

int focusedPlane(const Json &rendered) {
  return integer(rendered.at("orthogonal_navigation").at("focused_plane"), -1);
}

void frameScience(const Json &rendered) {
  const auto &frames = rendered.at("input_frames");
  require(frames.is_array() && !frames.empty(),
          "XYZ keyboard producer has no actual per-frame evidence");
  for (const auto &frame : frames) {
    require(std::abs(number(frame.at("delta_time")) - 1. / 60.) < 1e-8,
            "XYZ keyboard input timestep differs from frozen 1/60 seconds");
    const XYZ xyz = frame.at("crosshair_xyz").get<XYZ>();
    const auto expected = expectedWorld(xyz);
    const auto actual = frame.at("world_xyz").get<std::array<double, 3>>();
    for (int axis = 0; axis < 3; ++axis)
      require(xyz[axis] >= 0 && xyz[axis] < sizes[axis] &&
                  std::abs(actual[axis] - expected[axis]) <= 1e-12,
              "Actual input frame has invalid XYZ or rotated world coordinates");
    require(frame.at("document_info").at("view").at("crosshair") == Json(xyz),
            "Actual input frame disagrees with the native display crosshair");
    if (boolean(frame.at("orthogonal_navigation").at("mode")))
      for (int plane = 0; plane < 3; ++plane) {
        const auto &cache = frame.at("orthogonal_planes").at(plane);
        if (!boolean(cache.at("visible")))
          continue;
        require(integer(cache.at("coordinate")) == xyz[fixed[plane]],
                "Linked plane cache describes a stale same-frame crosshair");
      }
  }
}

const Json &lastEventFrame(const Json &rendered, std::size_t eventIndex) {
  const Json *result = nullptr;
  for (const auto &frame : rendered.at("input_frames"))
    if (frame.at("event_index").is_number_integer() &&
        frame.at("event_index").get<std::int64_t>() ==
            static_cast<std::int64_t>(eventIndex))
      result = &frame;
  require(result != nullptr, "Requested input event has no actual frame evidence");
  return *result;
}

Json scientificPlanes(Window &window, const std::filesystem::path &directory,
                       const std::string &id, XYZ xyz) {
  const auto snapshot = window.nativeSnapshot();
  const auto actualWorld = snapshot.data().dataset.calibration().voxel_to_world(
      {double(xyz[0]), double(xyz[1]), double(xyz[2])});
  const auto expected = expectedWorld(xyz);
  for (int axis = 0; axis < 3; ++axis)
    require(std::abs(actualWorld[axis] - expected[axis]) <= 1e-12,
            "Scientific sample world transform differs from independent formula");
  Json planes = Json::array();
  for (int plane = 0; plane < 3; ++plane) {
    const auto image = snapshot.data().dataset.extract_plane(views[plane], xyz[fixed[plane]]);
    require(image.width == sizes[horizontal[plane]] &&
                image.height == sizes[vertical[plane]] && image.type == PixelType::Float32 &&
                image.channels == 1,
            "Scientific plane geometry/type changed");
    for (int row = 0; row < image.height; ++row)
      for (int column = 0; column < image.width; ++column) {
        XYZ expectedVoxel = xyz;
        expectedVoxel[horizontal[plane]] = column;
        expectedVoxel[vertical[plane]] = row;
        require(image.at(column, row) == voxel(expectedVoxel),
                "Actual plane differs from independent integer voxel enumeration");
      }
    const auto path = directory / (id + "-" + names[plane] + ".f32le");
    writeFloat32(path, image.pixels);
    Json row{{"plane", names[plane]}, {"coordinate", xyz[fixed[plane]]},
             {"shape", {image.height, image.width}}, {"raw_path", path.u8string()},
             {"landmark", {xyz[vertical[plane]], xyz[horizontal[plane]]}},
             {"landmark_value", image.at(xyz[horizontal[plane]], xyz[vertical[plane]])}};
    if (xyz == initial)
      row["expected_sha256"] = initialPlaneSha[plane];
    planes.push_back(std::move(row));
  }
  return {{"crosshair_xyz", xyz}, {"world_xyz", actualWorld},
          {"expected_world_xyz", expected}, {"sample", voxel(xyz)}, {"planes", planes}};
}

void diagnostic(const std::filesystem::path &directory, const std::string &id,
                const Json &events, const Json &before, const Json &rendered) {
  const auto path = directory / ("diagnostic-" + id + ".json");
  require(!std::filesystem::exists(path),
          "XYZ diagnostic path must not already exist");
  writeJson(path.u8string(),
            {{"schema_version", 1}, {"kind", "diagnostic_observation"},
             {"status", "unverified"}, {"acceptance_claim", false},
             {"context", "imgui_cpp"},
             {"requirement_id", "G027.xyz_keyboard_navigation"},
             {"scenario_id", id}, {"input_events", events},
             {"before", before}, {"rendered", rendered}});
}

Json runScenario(Window &window, const std::filesystem::path &directory,
         const std::string &id, const Json &events, XYZ expected,
         bool busy = false, bool expectedMode = true) {
  const auto original = window.nativeSnapshot();
  Json before = window.documentInfo();
  before["source_snapshot"] = snapshotInfo(original);
  before["source_record"] = documentJson(*window.document());
  const auto rendered = window.renderHeadless(4, false, false, events, false, busy);
  diagnostic(directory, id, events, before, rendered);
  require(window.error().empty(), "Actual XYZ input encountered an editor error");
  require(rendered.at("input_trace").size() == events.size(),
          "XYZ keyboard producer did not deliver every requested input");
  frameScience(rendered);
  XYZ previous = original.display().crosshair;
  for (const auto &frame : rendered.at("input_frames")) {
    const XYZ xyz = frame.at("crosshair_xyz").get<XYZ>();
    const auto index = frame.at("event_index").get<std::int64_t>();
    if (index >= 0) {
      require(static_cast<std::size_t>(index) < events.size(),
              "Actual input frame has an invalid event index");
      const auto &event = events[static_cast<std::size_t>(index)];
      const auto type = string(event.at("type"));
      bool releaseOnly = type == "key" && !boolean(event.at("down"));
      if (type == "keys") {
        releaseOnly = true;
        for (const auto &entry : event.at("keys"))
          releaseOnly &= !boolean(entry.at("down"));
      }
      if (releaseOnly)
        require(xyz == previous, "Key release moved the scientific crosshair");
    }
    previous = xyz;
  }
  const auto afterSnapshot = window.nativeSnapshot();
  Json after = window.documentInfo();
  after["source_snapshot"] = snapshotInfo(afterSnapshot);
  require(afterSnapshot.display().crosshair == expected &&
              afterSnapshot.display().orthogonal == expectedMode,
          ("Actual XYZ keyboard result differs from frozen expectation: " + id).c_str());
  for (const auto *field : {"id", "revision", "dirty", "undo_steps", "redo_steps", "selection"})
    require(before.at(field) == after.at(field),
            "XYZ navigation changed a document/history invariant");
  for (const auto *field : {"pixels", "metadata", "selection"})
    require(before.at("revisions").at(field) == after.at("revisions").at(field),
            "XYZ navigation changed a source/data revision");
  for (const auto *field : {"zoom", "plane_zoom", "pan", "linked_zoom"})
    require(before.at("view").at(field) == after.at("view").at(field),
            "XYZ navigation changed an unrelated camera/C/T value");
  require(original.same_document(afterSnapshot) && original.shares_data(afterSnapshot) &&
              original.display().t == afterSnapshot.display().t &&
              original.display().c == afterSnapshot.display().c &&
              sourcePixels(original) == sourcePixels(afterSnapshot),
          "XYZ navigation changed canonical source/calibration/annotations");
  return {{"id", id}, {"input_events", events}, {"before", before}, {"after", after},
          {"rendered", rendered},
          {"expected", {{"crosshair_xyz", expected}, {"world_xyz", expectedWorld(expected)},
                         {"sample", voxel(expected)}, {"orthogonal", expectedMode}}},
          {"science", scientificPlanes(window, directory, id, expected)}};
}

Json &addCase(Json &report, const std::string &id) {
  report["cases"].push_back({{"id", id}, {"scenarios", Json::array()}});
  return report["cases"].back()["scenarios"];
}

void assertSuppressed(const Json &row, bool busy = false) {
  bool observed = false;
  for (const auto &frame : row.at("rendered").at("input_frames")) {
    if (string(frame.at("phase")) != "input")
      continue;
    observed = true;
    require(!boolean(frame.at("orthogonal_navigation").at("keyboard_admitted")),
            "Blocked/text/numeric input was admitted to the plane handler");
    if (busy)
      require(boolean(frame.at("busy")) && string(frame.at("job_state")) == "running",
              "Busy suppression has no live running Window job evidence");
  }
  require(observed, "Suppression case has no actual input frames");
}
} // namespace

int Window::orthogonalKeyboardTest(const std::string &directory) {
  checkThread();
  require(!directory.empty() && documents().empty(),
          "XYZ keyboard producer requires a fresh Window and directory");
  const auto root = std::filesystem::absolute(std::filesystem::u8path(directory)).lexically_normal();
  std::filesystem::create_directories(root.parent_path());
  require(std::filesystem::create_directory(root), "XYZ keyboard directory must be fresh");
  addSource(*this);
  writeFloat32(root / "source.f32le", sourcePixels(nativeSnapshot()));
  Json report{{"schema_version", 1}, {"context", "imgui_cpp"},
              {"requirement_id", "G027.xyz_keyboard_navigation"},
              {"source", {{"shape_zyx", {9, 17, 32}}, {"dtype", "Float32"},
                           {"encoding", "IEEE754 little-endian ZYX"},
                           {"raw_path", (root / "source.f32le").u8string()},
                           {"expected_sha256", sourceSha}}},
              {"world_atol", 1e-12}, {"world_rtol", 0},
              {"repeat", {{"delta_time", 1. / 60.}, {"delay", .275},
                           {"rate", .050}, {"hold_frames", 40}, {"expected_steps", 9}}},
              {"cases", Json::array()}};

  {
    auto &rows = addCase(report, "activation-keyboard");
    Window window;
    addSource(window, "Mode activation", false);
    const auto initialRender = window.renderHeadless(4);
    auto events = click(initialRender.at("orthogonal_navigation").at("toggle_point"));
    append(events, press("Escape"));
    append(events, press("Tab", true));
    const auto toggleFocusEvent = events.size() - 1;
    append(events, press("Space"));
    const auto activationEvent = events.size() - 1;
    append(events, press("Up"));
    auto row = runScenario(window, root, "activation", events, {7, 5, 4});
    require(boolean(lastEventFrame(row["rendered"], toggleFocusEvent)
                        .at("orthogonal_navigation").at("toggle_focused")),
            "Keyboard activation did not focus the actual orthogonal checkbox");
    const auto &activated = lastEventFrame(row["rendered"], activationEvent);
    require(boolean(activated.at("orthogonal_navigation").at("mode")) &&
                activated.at("crosshair_xyz") == Json(initial) &&
                focusedPlane(activated) == 0,
            "Space activation failed to enable XY while preserving the crosshair");
    require(focusedPlane(row["rendered"]) == 0, "Mode enable did not focus actual XY canvas");
    rows.push_back(std::move(row));
  }
  {
    auto &rows = addCase(report, "tab-order-and-exit");
    Window window;
    addSource(window);
    Json events = press("Tab", true);
    for (int i = 0; i < 6; ++i)
      append(events, press("Tab"));
    auto row = runScenario(window, root, "tab-order", events, initial);
    const std::array<int, 7> expectedPlanes{-1, 0, -1, 1, -1, 2, -1};
    const std::array<int, 7> expectedSlices{0, -1, 1, -1, 2, -1, -1};
    int step = 0;
    for (std::size_t eventIndex = 0; eventIndex < events.size(); ++eventIndex) {
      const auto &event = events[eventIndex];
      const bool released = event["type"] == "key"
                                ? event["name"] == "Tab" && event["down"] == false
                                : event["type"] == "keys" && event["keys"][0]["name"] == "Tab" &&
                                      event["keys"][0]["down"] == false;
      if (!released)
        continue;
      const Json *observed = nullptr;
      for (const auto &frame : row["rendered"]["input_frames"])
        if (frame["event_index"].is_number_integer() &&
            frame["event_index"].get<std::size_t>() == eventIndex)
          observed = &frame;
      require(observed != nullptr, "Tab release event has no actual frame evidence");
      const auto &frame = *observed;
      require(step < 7 && focusedPlane(frame) == expectedPlanes[step],
              "Actual Tab traversal did not follow the declared canvas order");
      for (int plane = 0; plane < 3; ++plane)
        require(boolean(frame["orthogonal_planes"][plane]["slice_focused"]) ==
                    (plane == expectedSlices[step]),
                "Actual Tab traversal did not follow declared slice/canvas order");
      ++step;
    }
    require(step == 7, "Tab producer did not observe every actual traversal");
    rows.push_back(std::move(row));
    Window reverse;
    addSource(reverse);
    events = focusedEvents(reverse.renderHeadless(4), 2);
    const auto prefixSize = events.size();
    for (int i = 0; i < 6; ++i)
      append(events, press("Tab", true));
    auto reverseRow = runScenario(reverse, root, "reverse-tab-order", events, initial);
    const std::array<int, 6> reversePlanes{-1, 1, -1, 0, -1, -1};
    const std::array<int, 6> reverseSlices{2, -1, 1, -1, 0, -1};
    step = 0;
    for (std::size_t eventIndex = prefixSize + 1; eventIndex < events.size(); eventIndex += 2) {
      const Json *observed = nullptr;
      for (const auto &frame : reverseRow["rendered"]["input_frames"])
        if (frame["event_index"].is_number_integer() &&
            frame["event_index"].get<std::size_t>() == eventIndex)
          observed = &frame;
      require(observed != nullptr && focusedPlane(*observed) == reversePlanes[step],
              "Reverse Tab traversal did not follow declared canvas order");
      for (int plane = 0; plane < 3; ++plane)
        require(boolean((*observed)["orthogonal_planes"][plane]["slice_focused"]) ==
                    (plane == reverseSlices[step]),
                "Reverse Tab traversal did not follow declared slice order");
      ++step;
    }
    require(step == 6, "Reverse Tab traversal omitted a control or group exit");
    rows.push_back(std::move(reverseRow));
  }
  {
    auto &rows = addCase(report, "enter-space-plane");
    for (int plane = 0; plane < 3; ++plane) {
      Window window;
      addSource(window);
      auto events = focusedEvents(window.renderHeadless(4), plane);
      append(events, press("Enter"));
      append(events, press("Space"));
      auto row = runScenario(window, root, "activation-plane-" + std::to_string(plane), events, initial);
      require(focusedPlane(row["rendered"]) == plane,
              "Enter/Space activation changed the actual focused plane");
      rows.push_back(std::move(row));
    }
  }
  {
    auto &rows = addCase(report, "click-activation");
    for (int plane = 0; plane < 3; ++plane) {
      Window window;
      addSource(window);
      XYZ selected = initial;
      selected[horizontal[plane]] += 1;
      auto events = focusedEvents(window.renderHeadless(4), plane, selected);
      append(events, press("Up"));
      selected[fixed[plane]] += 1;
      auto row = runScenario(window, root, "click-plane-" + std::to_string(plane), events, selected);
      require(focusedPlane(row["rendered"]) == plane,
              "Pointer activation did not retain focus after pointer departure");
      rows.push_back(std::move(row));
    }
  }
  {
    auto &rows = addCase(report, "hover-is-not-focus");
    for (int hoverPlane : {1, 2}) {
      Window window;
      addSource(window);
      const auto rendered = window.renderHeadless(4);
      Json events = Json::array({{{"type", "position"}, {"space", "screen"},
                                  {"point", imagePoint(rendered, hoverPlane, initial)}}});
      append(events, press("Up"));
      auto row = runScenario(window, root, "hover-plane-" + std::to_string(hoverPlane), events, {7, 5, 4});
      require(focusedPlane(row["rendered"]) == 0, "Hover transferred keyboard focus");
      rows.push_back(std::move(row));
    }
  }
  {
    auto &rows = addCase(report, "plain-direction-all-planes");
    for (int plane = 0; plane < 3; ++plane)
      for (const auto &entry : std::array<std::pair<const char *, int>, 4>{{
               {"Up", 1}, {"PageUp", 1}, {"Down", -1}, {"PageDown", -1}}}) {
        Window window;
        addSource(window);
        auto events = focusedEvents(window.renderHeadless(4), plane);
        append(events, press(entry.first));
        XYZ expected = initial;
        expected[fixed[plane]] += entry.second;
        rows.push_back(runScenario(window, root, "plain-" + std::to_string(plane) + '-' + entry.first,
                           events, expected));
      }
  }
  {
    auto &rows = addCase(report, "shift-reference-direction-all-planes");
    for (int plane = 0; plane < 3; ++plane)
      for (const auto &entry : std::array<std::pair<const char *, XYZ>, 4>{{
               {"Left", {-1, 0, 0}}, {"Right", {1, 0, 0}},
               {"Up", {0, -1, 0}}, {"Down", {0, 1, 0}}}}) {
        Window window;
        addSource(window);
        auto events = focusedEvents(window.renderHeadless(4), plane);
        append(events, press(entry.first, true));
        XYZ expected = initial;
        for (int axis = 0; axis < 3; ++axis)
          expected[axis] += entry.second[axis];
        rows.push_back(runScenario(window, root, "shift-" + std::to_string(plane) + '-' + entry.first,
                           events, expected));
      }
  }
  {
    auto &rows = addCase(report, "edge-clamp-plain-and-shift");
    for (int plane = 0; plane < 3; ++plane)
      for (bool maximum : {false, true}) {
        Window window;
        XYZ endpoint = initial;
        endpoint[fixed[plane]] = maximum ? sizes[fixed[plane]] - 1 : 0;
        addSource(window, "Plain endpoint", true, endpoint);
        auto events = focusedEvents(window.renderHeadless(4), plane, endpoint);
        append(events, press(maximum ? "Up" : "Down"));
        rows.push_back(runScenario(window, root, "bound-plain-" + std::to_string(plane) + '-' +
                             std::to_string(maximum), events, endpoint));
      }
    for (int plane = 0; plane < 3; ++plane)
      for (int axis : {0, 1})
        for (bool maximum : {false, true}) {
          Window window;
          XYZ endpoint = initial;
          endpoint[axis] = maximum ? sizes[axis] - 1 : 0;
          addSource(window, "Shift endpoint", true, endpoint);
          auto events = focusedEvents(window.renderHeadless(4), plane, endpoint);
          const auto name = axis == 0 ? (maximum ? "Right" : "Left")
                                      : (maximum ? "Down" : "Up");
          append(events, press(name, true));
          rows.push_back(runScenario(window, root, "bound-shift-" + std::to_string(plane) + '-' +
                               std::to_string(axis) + '-' + std::to_string(maximum), events, endpoint));
        }
  }
  {
    auto &rows = addCase(report, "combined-keys-and-release");
    const std::array<Json, 4> presses{
        batch({{"Up", true}, {"PageUp", true}}),
        batch({{"Up", true}, {"Down", true}}),
        batch({{"Shift", true}, {"Right", true}, {"Down", true}}),
        batch({{"Shift", true}, {"Left", true}, {"Right", true}, {"Up", true}, {"Down", true}})};
    const std::array<XYZ, 4> expected{{{7, 5, 4}, initial, {8, 6, 3}, initial}};
    for (int i = 0; i < 4; ++i) {
      Window window;
      addSource(window);
      Json events = Json::array({presses[i]});
      Json released = presses[i];
      for (auto &entry : released["keys"])
        entry["down"] = false;
      released["frames"] = 5;
      events.push_back(std::move(released));
      rows.push_back(runScenario(window, root, "combined-" + std::to_string(i), events, expected[i]));
    }
    Window window;
    addSource(window);
    Json events = Json::array();
    for (const auto *name : {"Left", "Right", "Home", "End"})
      append(events, press(name));
    append(events, press("PageUp", true));
    append(events, press("PageDown", true));
    rows.push_back(runScenario(window, root, "unassigned-keys", events, initial));
  }
  {
    auto &rows = addCase(report, "repeat-bounded");
    Window window;
    addSource(window);
    auto events = focusedEvents(window.renderHeadless(4), 2);
    append(events, press("Up", false, 40));
    events.push_back({{"type", "wait"}, {"frames", 5}});
    auto row = runScenario(window, root, "repeat-40-frames", events, {16, 5, 3});
    int steps = 0;
    XYZ previous = initial;
    for (const auto &frame : row["rendered"]["input_frames"]) {
      const auto xyz = frame["crosshair_xyz"].get<XYZ>();
      require(std::abs(xyz[0] - previous[0]) <= 1 && xyz[1] == 5 && xyz[2] == 3,
              "Repeat changed more than one voxel per accepted frame");
      steps += xyz[0] - previous[0];
      previous = xyz;
    }
    require(steps == 9, "Forty repeat frames did not produce frozen nine steps");
    row["observed_steps"] = steps;
    rows.push_back(std::move(row));
    Window diagonal;
    addSource(diagonal);
    events = focusedEvents(diagonal.renderHeadless(4), 1);
    events.push_back(batch({{"Shift", true}, {"Right", true}, {"Down", true}}, 40));
    events.push_back(batch({{"Right", false}, {"Down", false}, {"Shift", false}}, 5));
    auto diagonalRow = runScenario(diagonal, root, "repeat-shift-diagonal-40-frames", events, {16, 14, 3});
    previous = initial;
    std::array<int, 2> diagonalSteps{};
    for (const auto &frame : diagonalRow["rendered"]["input_frames"]) {
      const auto xyz = frame["crosshair_xyz"].get<XYZ>();
      require(xyz[2] == 3, "Shift repeat changed unrelated Z");
      for (int axis : {0, 1}) {
        const int delta = xyz[axis] - previous[axis];
        require(delta >= 0 && delta <= 1, "Shift repeat changed more than one voxel per axis/frame");
        diagonalSteps[axis] += delta;
      }
      previous = xyz;
    }
    require(diagonalSteps == std::array<int, 2>{9, 9},
            "Forty Shift repeat frames did not produce nine steps on each axis");
    diagonalRow["observed_steps_xy"] = diagonalSteps;
    rows.push_back(std::move(diagonalRow));
  }
  {
    auto &rows = addCase(report, "ctrl-alt-super-and-key-ownership");
    const std::array<const char *, 3> modifiers{"Ctrl", "Alt", "Super"};
    for (int mask = 1; mask < 8; ++mask)
      for (bool shift : {false, true}) {
        Window window;
        addSource(window);
        Json events = Json::array();
        for (const auto *name : {"Left", "Right", "Up", "Down", "PageUp", "PageDown", "Escape"}) {
          Json entries = Json::array({{{"name", name}, {"down", true}}});
          for (int modifier = 0; modifier < 3; ++modifier)
            if (mask & (1 << modifier))
              entries.push_back({{"name", modifiers[modifier]}, {"down", true}});
          if (shift)
            entries.push_back({{"name", "Shift"}, {"down", true}});
          events.push_back({{"type", "keys"}, {"keys", entries}});
          for (auto &entry : entries)
            entry["down"] = false;
          events.push_back({{"type", "keys"}, {"keys", entries}, {"frames", 2}});
        }
        rows.push_back(runScenario(window, root, "reserved-mask-" + std::to_string(mask) + '-' +
                             std::to_string(shift), events, initial));
      }
  }
  {
    auto &rows = addCase(report, "text-focus-suppression");
    Window window;
    addSource(window);
    window.action("show_preferences", {{"search", ""}});
    const auto rendered = window.renderHeadless(4);
    auto events = click(rendered.at("preferences_panel").at("search_point"));
    events.push_back({{"type", "text"}, {"value", "xyz"}});
    append(events, press("Up"));
    append(events, press("Escape"));
    auto row = runScenario(window, root, "preferences-text-owner", events, initial);
    assertSuppressed(row);
    const auto &suppressedRender = row["rendered"];
    auto recovery = click(suppressedRender.at("preferences_panel").at("close_point"));
    append(recovery, focusedEvents(suppressedRender, 1));
    append(recovery, press("Up"));
    rows.push_back(std::move(row));
    auto recovered = runScenario(window, root, "preferences-text-focus-recovery", recovery, {7, 6, 3});
    require(focusedPlane(recovered["rendered"]) == 1 &&
                !boolean(recovered["rendered"]["preferences_panel"]["open"]),
            "Closing Preferences did not allow actual plane focus recovery");
    rows.push_back(std::move(recovered));
  }
  {
    auto &rows = addCase(report, "numeric-control-ownership");
    Window window;
    addSource(window);
    const auto rendered = window.renderHeadless(4);
    Json events = Json::array({key("Ctrl", true)});
    append(events, click(rendered.at("orthogonal_planes").at(0).at("slice_point")));
    events.push_back(key("Ctrl", false));
    append(events, press("Up"));
    auto row = runScenario(window, root, "slice-numeric-owner", events, initial);
    // Only frames after Ctrl-click are owned by the numeric editor; initial
    // control-admission frames remain actual evidence in the full trace.
    bool sawOwner = false;
    for (const auto &frame : row["rendered"]["input_frames"])
      if (boolean(frame["orthogonal_planes"][0]["slice_focused"])) {
        sawOwner = true;
        require(!boolean(frame["orthogonal_navigation"]["keyboard_admitted"]),
                "Numeric slice owner admitted the plane key handler");
      }
    require(sawOwner, "Numeric suppression did not focus the actual slice control");
    rows.push_back(std::move(row));
  }
  {
    auto &rows = addCase(report, "blocked-hidden-and-busy");
    Window hidden;
    addSource(hidden, "Hidden orthogonal", false);
    Json events = press("Up");
    append(events, press("PageUp", true));
    auto hiddenRow = runScenario(hidden, root, "hidden-mode", events, initial, false, false);
    assertSuppressed(hiddenRow);
    rows.push_back(std::move(hiddenRow));
    Window busy;
    addSource(busy, "Running barrier");
    events = press("Up");
    append(events, press("Right", true));
    append(events, press("Escape"));
    auto busyRow = runScenario(busy, root, "real-running-job", events, initial, true);
    assertSuppressed(busyRow, true);
    require(!busy.busy(), "Diagnostic running job barrier was not released/joined");
    rows.push_back(std::move(busyRow));
    Window modal;
    addSource(modal, "Modal operation");
    modal.action("show_operation", {{"operation", "add"}});
    events = press("Up");
    auto modalRow = runScenario(modal, root, "blocking-operation-dialog", events, initial);
    assertSuppressed(modalRow);
    rows.push_back(std::move(modalRow));
  }
  {
    auto &rows = addCase(report, "document-switch");
    Window window;
    addSource(window, "First XYZ");
    const auto firstId = window.nativeSnapshot().id();
    addSource(window, "Second XYZ");
    const auto secondId = window.nativeSnapshot().id();
    window.setActiveIndex(0);
    const auto rendered = window.renderHeadless(4);
    const auto &tabs = rendered.at("document_tabs");
    require(tabs.size() == 2, "Document-switch producer has no actual tab targets");
    auto events = focusedEvents(rendered, 2);
    append(events, click(tabs[1].at("point")));
    append(events, press("Up"));
    auto beforeFirst = window.documentInfo(firstId);
    auto beforeSecond = window.documentInfo(secondId);
    beforeFirst["source_snapshot"] = snapshotInfo(window.nativeSnapshot(firstId));
    beforeFirst["source_record"] = documentJson(*window.documentById(firstId));
    beforeSecond["source_snapshot"] = snapshotInfo(window.nativeSnapshot(secondId));
    beforeSecond["source_record"] = documentJson(*window.documentById(secondId));
    const auto input = window.renderHeadless(4, false, false, events);
    diagnostic(root, "actual-document-tab", events,
               {{"first", beforeFirst}, {"second", beforeSecond}}, input);
    frameScience(input);
    require(window.activeIndex() == 1 &&
                window.nativeSnapshot(firstId).display().crosshair == initial &&
                window.nativeSnapshot(secondId).display().crosshair == XYZ{7, 5, 4} &&
                focusedPlane(input) == 0,
            "Document switch dispatched to a stale plane/document or failed XY reacquisition");
    rows.push_back({{"id", "actual-document-tab"}, {"input_events", events},
                    {"before", {{"first", beforeFirst}, {"second", beforeSecond}}},
                    {"after", {{"first", window.documentInfo(firstId)},
                               {"second", window.documentInfo(secondId)}}},
                    {"rendered", input},
                    {"expected", {{"active_document_id", secondId},
                                   {"first_xyz", initial}, {"second_xyz", {7, 5, 4}},
                                   {"focused_plane", 0}}}});
  }
  {
    auto &rows = addCase(report, "exact-scientific-planes-and-cache");
    for (int plane = 0; plane < 3; ++plane) {
      Window window;
      addSource(window);
      auto events = focusedEvents(window.renderHeadless(4), plane);
      append(events, press("Up"));
      events.push_back({{"type", "wait"}, {"frames", 3}});
      XYZ expected = initial;
      ++expected[fixed[plane]];
      auto row = runScenario(window, root, "cache-axis-" + std::to_string(fixed[plane]), events, expected);
      const auto &frames = row["rendered"]["input_frames"];
      bool changed = false;
      Json previousBuilds;
      for (const auto &frame : frames) {
        Json builds = Json::array();
        for (const auto &cache : frame["orthogonal_planes"])
          builds.push_back(cache["builds"]);
        if (!previousBuilds.is_null())
          for (int index = 0; index < 3; ++index) {
            const int delta = integer(builds[index]) - integer(previousBuilds[index]);
            require(delta == 0 || (index == plane && delta == 1),
                    "Navigation/focus rebuilt an unrelated plane cache");
            changed |= delta == 1;
          }
        previousBuilds = builds;
      }
      require(changed, "Fixed-axis navigation did not refresh its changed scientific plane");
      rows.push_back(std::move(row));
    }
    Window initialWindow;
    addSource(initialWindow);
    rows.push_back(runScenario(initialWindow, root, "canonical-initial-planes",
                       Json::array({{{"type", "wait"}, {"frames", 3}}}), initial));
  }
  {
    auto &rows = addCase(report, "state-invariants");
    Window window;
    addSource(window);
    auto display = window.nativeSnapshot().display();
    display.zoom = 3;
    display.linkedZoom = false;
    display.planeZoom = {3, 4, 5};
    display.pan = {{{1, 2}, {3, 4}, {5, 6}}};
    window.setNativeDisplay(display);
    auto events = focusedEvents(window.renderHeadless(4), 1);
    append(events, press("Right", true));
    rows.push_back(runScenario(window, root, "source-camera-history-invariants", events, {8, 5, 3}));
  }
  {
    auto &rows = addCase(report, "session-and-pixel-undo");
    Window window;
    addSource(window);
    auto edit = window.beginEdit();
    auto image = *window.nativeSnapshot().data().dataset.plane({0, 0, 3});
    for (auto &pixel : image.pixels)
      pixel += 1;
    edit.replace_plane({0, 0, 3}, std::move(image));
    window.commitEdit(edit);
    auto events = focusedEvents(window.renderHeadless(4), 2);
    append(events, press("Up"));
    auto before = window.documentInfo();
    before["source_snapshot"] = snapshotInfo(window.nativeSnapshot());
    before["source_record"] = documentJson(*window.document());
    const auto rendered = window.renderHeadless(4, false, false, events);
    diagnostic(root, "undo-and-session", events, before, rendered);
    frameScience(rendered);
    require(window.nativeSnapshot().display().crosshair == XYZ{8, 5, 3},
            "Undo scenario did not navigate through actual plane input");
    const auto beforeUndo = window.documentInfo();
    window.action("undo");
    require(window.nativeSnapshot().display().crosshair == XYZ{8, 5, 3} &&
                window.nativeDocument()->undo_steps() == 0 &&
                window.nativeDocument()->redo_steps() == 1,
            "Pixel undo did not preserve the current keyboard view");
    writeFloat32(root / "undo-source.f32le", sourcePixels(window.nativeSnapshot()));
    const auto session = root / "rotated-xyz.ijx";
    saveSession(session.u8string(), window.documents(), window.results, window.workflow);
    Json restoredResults, restoredWorkflow;
    auto restored = loadSession(session.u8string(), restoredResults, restoredWorkflow);
    require(restored.size() == 1 && restored[0]->viewJson() == window.document()->viewJson(),
            "Session did not persist the current rotated orthogonal view");
    Window reopened;
    reopened.addDocument(restored[0]);
    const auto reopenedSnapshot = reopened.nativeSnapshot();
    auto beforeReopened = reopened.documentInfo();
    beforeReopened["source_snapshot"] = snapshotInfo(reopenedSnapshot);
    beforeReopened["source_record"] = documentJson(*reopened.document());
    const auto reopenedRender = reopened.renderHeadless(4);
    diagnostic(root, "session-reopened", Json::array(), beforeReopened, reopenedRender);
    sourcePixels(reopenedSnapshot);
    const auto &reopenedCalibration = reopenedSnapshot.data().dataset.calibration();
    const auto expectedCalibration = calibration();
    require(reopenedCalibration.spacing == expectedCalibration.spacing &&
                reopenedCalibration.origin == expectedCalibration.origin &&
                reopenedCalibration.direction == expectedCalibration.direction &&
                reopenedCalibration.unit == expectedCalibration.unit,
            "Session reopen lost the rotated spatial calibration");
    const auto reopenedWorld = reopenedCalibration.voxel_to_world({8, 5, 3});
    const auto independentWorld = expectedWorld({8, 5, 3});
    for (int axis = 0; axis < 3; ++axis)
      require(std::abs(reopenedWorld[axis] - independentWorld[axis]) <= 1e-12,
              "Session reopen world coordinates differ from independent formula");
    frameScience(reopenedRender);
    require(focusedPlane(reopenedRender) == 0,
            "Reopened session retained old plane focus instead of reacquiring XY");
    rows.push_back({{"id", "undo-and-session"}, {"input_events", events},
                    {"before", before}, {"after", window.documentInfo()},
                    {"before_undo", beforeUndo}, {"rendered", rendered},
                    {"reopened_rendered", reopenedRender},
                    {"reopened", reopened.documentInfo()},
                    {"reopened_source_snapshot", snapshotInfo(reopenedSnapshot)},
                    {"reopened_world_xyz", reopenedWorld},
                    {"session_path", session.u8string()},
                    {"expected", {{"crosshair_xyz", {8, 5, 3}},
                                   {"world_xyz", expectedWorld({8, 5, 3})},
                                   {"focus_transient", true}, {"source_restored", true}}}});
  }
  {
    auto &rows = addCase(report, "escape-exits-orthogonal");
    Window window;
    addSource(window);
    auto events = focusedEvents(window.renderHeadless(4), 1);
    append(events, press("Escape"));
    auto row = runScenario(window, root, "escape-visible-plane", events, initial, false, false);
    require(focusedPlane(row["rendered"]) == -1 && window.documents().size() == 1,
            "Escape retained plane focus or closed the document");
    const auto &navigation = row["rendered"]["orthogonal_navigation"];
    require(point(navigation["ordinary_canvas_point"]).size() == 2 &&
                boolean(navigation.at("ordinary_canvas_focused")) &&
                navigation.at("ordinary_canvas_id").is_number_unsigned() &&
                navigation.at("ordinary_canvas_id").get<std::uint64_t>() != 0,
            "Escape did not hand actual focus to the ordinary canvas");
    auto reentry = click(navigation["toggle_point"]);
    append(reentry, press("Up"));
    rows.push_back(std::move(row));
    auto reopened = runScenario(window, root, "escape-reentry", reentry, {7, 5, 4});
    require(focusedPlane(reopened["rendered"]) == 0,
            "Orthogonal reentry did not focus XY instead of a stale plane");
    rows.push_back(std::move(reopened));
  }
  {
    auto &rows = addCase(report, "installed-two-contexts");
    // This producer establishes only its own actual standalone launch. The
    // independently installed Python producer is joined by the root verifier.
    Window window;
    addSource(window, "Installed standalone input");
    auto events = focusedEvents(window.renderHeadless(4), 2);
    append(events, press("Right", true));
    auto row = runScenario(window, root, "standalone-producer", events, {8, 5, 3});
    row["expected"]["context"] = "imgui_cpp";
    row["expected"]["independent_python_producer_required"] = true;
    rows.push_back(std::move(row));
  }
  require(report["cases"].size() == 20, "XYZ producer omitted a declared acceptance case");
  const auto reportPath = root / "imgui-cpp.json";
  writeJson(reportPath.u8string(), report);
  require(readJson(reportPath.u8string()) == report, "XYZ producer report was not written exactly");
  std::cout << "Standalone XYZ keyboard: actual focus/Tab/plain/Shift/bounds/combined/repeat/"
               "ownership/suppression/document switch/rotated science/history/session/Escape passed\n";
  return 0;
}
} // namespace imagejxx::desktop
