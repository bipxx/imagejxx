#include <imagejxx/application.hpp>

#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
using namespace imagejxx;
namespace fs = std::filesystem;
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

double value(const XYZ &xyz) {
  return 10000 * xyz[2] + 100 * xyz[1] + xyz[0];
}

std::array<double, 3> expectedWorld(const XYZ &xyz) {
  // Independent decimal-rational arithmetic; do not reuse voxel_to_world.
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

Dataset dataset() {
  DatasetShape shape;
  shape.x = sizes[0];
  shape.y = sizes[1];
  shape.z = sizes[2];
  Stack planes;
  for (int z = 0; z < sizes[2]; ++z) {
    Image image(sizes[0], sizes[1], 1, PixelType::Float32);
    for (int y = 0; y < sizes[1]; ++y)
      for (int x = 0; x < sizes[0]; ++x)
        image.at(x, y) = value({x, y, z});
    planes.push_back(std::move(image));
  }
  return Dataset::from_planes(shape, std::move(planes), calibration());
}

std::string quote(const std::string &text) {
  const char *hex = "0123456789abcdef";
  std::string result = "\"";
  for (unsigned char c : text) {
    if (c == '"' || c == '\\') {
      result += '\\';
      result += char(c);
    } else if (c < 32) {
      result += "\\u00";
      result += hex[c >> 4];
      result += hex[c & 15];
    } else
      result += char(c);
  }
  return result + '"';
}

template <typename T, std::size_t N>
std::string array(const std::array<T, N> &values) {
  std::ostringstream result;
  result << std::setprecision(17) << '[';
  for (std::size_t i = 0; i < N; ++i) {
    if (i)
      result << ',';
    result << values[i];
  }
  result << ']';
  return result.str();
}

void writeFloat32(const fs::path &path, const std::vector<double> &pixels) {
  static_assert(sizeof(float) == 4 && std::numeric_limits<float>::is_iec559,
                "Canonical fixture requires IEEE754 Float32");
  std::ofstream output(path, std::ios::binary);
  output.exceptions(std::ios::badbit | std::ios::failbit);
  for (double pixel : pixels) {
    const float packed = static_cast<float>(pixel);
    require(std::isfinite(pixel) && double(packed) == pixel,
            "Scientific fixture is not exact Float32");
    std::uint32_t bits;
    std::memcpy(&bits, &packed, sizeof(bits));
    const std::array<char, 4> bytes{
        char(bits & 255), char((bits >> 8) & 255),
        char((bits >> 16) & 255), char((bits >> 24) & 255)};
    output.write(bytes.data(), bytes.size());
  }
  output.close();
}

std::vector<double> sourcePixels(const DocumentSnapshot &snapshot) {
  std::vector<double> result;
  for (int z = 0; z < sizes[2]; ++z) {
    const auto image = snapshot.data().dataset.plane({0, 0, z});
    require(image->width == 32 && image->height == 17 && image->channels == 1 &&
                image->type == PixelType::Float32,
            "Source geometry/type changed");
    for (int y = 0; y < sizes[1]; ++y)
      for (int x = 0; x < sizes[0]; ++x) {
        require(image->at(x, y) == value({x, y, z}),
                "Canonical source pixels changed");
        result.push_back(image->at(x, y));
      }
  }
  return result;
}

void unchanged(const DocumentSnapshot &before, const DocumentSnapshot &after,
               const HistoryInfo &beforeHistory, const HistoryInfo &afterHistory) {
  const auto a = before.revisions(), b = after.revisions();
  require(before.same_document(after) && before.shares_data(after) &&
              a.pixels == b.pixels && a.metadata == b.metadata &&
              a.selection == b.selection && before.dirty() == after.dirty() &&
              beforeHistory.undo_steps == afterHistory.undo_steps &&
              beforeHistory.redo_steps == afterHistory.redo_steps &&
              before.display().t == after.display().t &&
              before.display().c == after.display().c &&
              before.display().zoom == after.display().zoom &&
              before.display().planeZoom == after.display().planeZoom &&
              before.display().pan == after.display().pan,
          "View setter changed source/data/history or unrelated display state");
}

std::string snapshotJson(const DocumentSnapshot &snapshot,
                         const HistoryInfo &history) {
  const auto revisions = snapshot.revisions();
  const auto &display = snapshot.display();
  const auto &cal = snapshot.data().dataset.calibration();
  const auto world = cal.voxel_to_world(
      {double(display.crosshair[0]), double(display.crosshair[1]),
       double(display.crosshair[2])});
  std::ostringstream result;
  result << std::setprecision(17)
         << "{\"id\":" << snapshot.id()
         << ",\"crosshair_xyz\":" << array(snapshot.display().crosshair)
         << ",\"world_xyz\":" << array(world)
         << ",\"orthogonal\":" << (snapshot.display().orthogonal ? "true" : "false")
         << ",\"t\":" << display.t << ",\"c\":" << display.c
         << ",\"zoom\":" << display.zoom
         << ",\"plane_zoom\":" << array(display.planeZoom)
         << ",\"pan\":[" << array(display.pan[0]) << ',' << array(display.pan[1])
         << ',' << array(display.pan[2]) << ']'
         << ",\"calibration\":{\"spacing\":" << array(cal.spacing)
         << ",\"origin\":" << array(cal.origin)
         << ",\"direction\":" << array(cal.direction)
         << ",\"unit\":" << quote(cal.unit) << '}'
         << ",\"dirty\":" << (snapshot.dirty() ? "true" : "false")
         << ",\"undo_steps\":" << history.undo_steps
         << ",\"redo_steps\":" << history.redo_steps
         << ",\"revisions\":{\"pixels\":" << revisions.pixels
         << ",\"metadata\":" << revisions.metadata
         << ",\"selection\":" << revisions.selection
         << ",\"display\":" << revisions.display << "}}";
  return result.str();
}

std::string observe(Application &application, const fs::path &directory,
                    const std::string &id, XYZ expected) {
  const auto before = application.snapshot();
  const auto historyBefore = application.history();
  auto display = before.display();
  display.orthogonal = true;
  display.crosshair = expected;
  application.set_display(display);
  const auto after = application.snapshot();
  const auto historyAfter = application.history();
  unchanged(before, after, historyBefore, historyAfter);
  require(after.display().crosshair == expected && after.display().orthogonal,
          "Public display state does not represent expected XYZ navigation");
  const auto expectedCalibration = calibration();
  const auto &actualCalibration = after.data().dataset.calibration();
  require(actualCalibration.spacing == expectedCalibration.spacing &&
              actualCalibration.origin == expectedCalibration.origin &&
              actualCalibration.direction == expectedCalibration.direction &&
              actualCalibration.unit == expectedCalibration.unit,
          "Public view state changed the rotated spatial calibration");
  const auto world = after.data().dataset.calibration().voxel_to_world(
      {double(expected[0]), double(expected[1]), double(expected[2])});
  const auto independent = expectedWorld(expected);
  for (int axis = 0; axis < 3; ++axis)
    require(std::abs(world[axis] - independent[axis]) <= 1e-12,
            "Rotated world coordinate differs from independent formula");
  std::ostringstream planes;
  planes << '[';
  for (int plane = 0; plane < 3; ++plane) {
    if (plane)
      planes << ',';
    const int coordinate = expected[fixed[plane]];
    const auto image = after.data().dataset.extract_plane(views[plane], coordinate);
    require(image.width == sizes[horizontal[plane]] &&
                image.height == sizes[vertical[plane]] && image.channels == 1 &&
                image.type == PixelType::Float32,
            "Extracted orthogonal plane geometry/type changed");
    for (int row = 0; row < image.height; ++row)
      for (int column = 0; column < image.width; ++column) {
        XYZ xyz = expected;
        xyz[horizontal[plane]] = column;
        xyz[vertical[plane]] = row;
        require(image.at(column, row) == value(xyz),
                "Extracted plane differs from independently enumerated voxels");
      }
    const auto geometry = after.data().dataset.plane_geometry(views[plane], coordinate);
    XYZ origin{0, 0, 0};
    origin[fixed[plane]] = coordinate;
    const auto originExpected = expectedWorld(origin);
    const std::array<std::array<double, 3>, 3> axisSteps{
        {{{0, .4, 0}}, {{-1.2, 0, 0}}, {{0, 0, 3}}}};
    for (int axis = 0; axis < 3; ++axis)
      require(std::abs(geometry.origin[axis] - originExpected[axis]) <= 1e-12 &&
                  std::abs(geometry.horizontal_step[axis] -
                           axisSteps[horizontal[plane]][axis]) <= 1e-12 &&
                  std::abs(geometry.vertical_step[axis] -
                           axisSteps[vertical[plane]][axis]) <= 1e-12,
              "Plane geometry differs from independent rotated calibration");
    const auto path = directory / (id + "-" + names[plane] + ".f32le");
    writeFloat32(path, image.pixels);
    planes << "{\"plane\":" << quote(names[plane])
           << ",\"coordinate\":" << coordinate
           << ",\"shape\":[" << image.height << ',' << image.width << ']'
           << ",\"raw_path\":" << quote(path.u8string())
           << ",\"landmark\":[" << expected[vertical[plane]] << ','
           << expected[horizontal[plane]] << ']'
           << ",\"landmark_value\":"
           << image.at(expected[horizontal[plane]], expected[vertical[plane]])
           << ",\"origin\":" << array(geometry.origin)
           << ",\"horizontal_step\":" << array(geometry.horizontal_step)
           << ",\"vertical_step\":" << array(geometry.vertical_step);
    if (expected == initial)
      planes << ",\"expected_sha256\":" << quote(initialPlaneSha[plane]);
    planes << '}';
  }
  planes << ']';
  return "{\"id\":" + quote(id) + ",\"before\":" +
         snapshotJson(before, historyBefore) + ",\"after\":" +
         snapshotJson(after, historyAfter) + ",\"expected\":{\"crosshair_xyz\":" +
         array(expected) + ",\"world_xyz\":" + array(independent) +
         ",\"sample\":" + std::to_string(int(value(expected))) +
         "},\"world_xyz\":" + array(world) + ",\"planes\":" + planes.str() +
         ",\"source_preserved\":true,\"history_preserved\":true}";
}
} // namespace

int main(int argc, char **argv) {
  try {
    require(argc == 3 && std::string(argv[1]) == "--consumer-dir",
            "Use --consumer-dir <fresh-directory>");
    const auto directory = fs::absolute(fs::u8path(argv[2])).lexically_normal();
    fs::create_directories(directory.parent_path());
    require(fs::create_directory(directory), "Consumer directory must be fresh");
    Application application;
    const auto id = application.create(dataset(), "Rotated XYZ API fixture");
    const auto source = application.snapshot(id);
    writeFloat32(directory / "source.f32le", sourcePixels(source));
    std::vector<std::string> rows;
    rows.push_back(observe(application, directory, "initial", initial));
    for (int plane = 0; plane < 3; ++plane) {
      for (int delta : {-1, 1}) {
        XYZ xyz = initial;
        xyz[fixed[plane]] += delta;
        const std::string direction = delta > 0 ? "increase" : "decrease";
        rows.push_back(observe(application, directory,
                               std::string("plain-") + names[plane] + '-' + direction,
                               xyz));
      }
      for (int axis : {0, 1})
        for (int delta : {-1, 1}) {
          XYZ xyz = initial;
          xyz[axis] += delta;
          rows.push_back(observe(application, directory,
                                 std::string("shift-") + names[plane] + '-' +
                                     (axis == 0 ? "X" : "Y") +
                                     (delta > 0 ? "-increase" : "-decrease"), xyz));
        }
    }
    for (int axis = 0; axis < 3; ++axis)
      for (int endpoint : {0, sizes[axis] - 1}) {
        XYZ xyz = initial;
        xyz[axis] = endpoint;
        rows.push_back(observe(application, directory,
                               "bound-" + std::to_string(axis) + '-' +
                                   std::to_string(endpoint), xyz));
      }
    rows.push_back(observe(application, directory, "reset-for-invalid", initial));
    std::ostringstream rejected;
    rejected << '[';
    bool first = true;
    for (int axis = 0; axis < 3; ++axis)
      for (int invalid : {-1, sizes[axis]}) {
        const auto before = application.snapshot();
        const auto history = application.history();
        auto display = before.display();
        display.crosshair[axis] = invalid;
        display.orthogonal = !before.display().orthogonal;
        display.zoom = before.display().zoom + 1;
        bool threw = false;
        try {
          application.set_display(display);
        } catch (const std::invalid_argument &) {
          threw = true;
        } catch (const std::out_of_range &) {
          threw = true;
        }
        const auto after = application.snapshot();
        require(threw && after.display().crosshair == before.display().crosshair &&
                    after.display().orthogonal == before.display().orthogonal &&
                    after.revisions().display == before.revisions().display,
                "Invalid public XYZ setter did not reject atomically");
        unchanged(before, after, history, application.history());
        if (!first)
          rejected << ',';
        first = false;
        rejected << "{\"axis\":" << axis << ",\"value\":" << invalid
                 << ",\"rejected\":true,\"before\":" << snapshotJson(before, history)
                 << ",\"after\":" << snapshotJson(after, application.history()) << '}';
      }
    rejected << ']';
    auto edit = application.begin_edit();
    auto replacement = *application.snapshot().data().dataset.plane({0, 0, 3});
    for (auto &pixel : replacement.pixels)
      pixel += 1;
    edit.replace_plane({0, 0, 3}, std::move(replacement));
    application.commit(edit);
    auto moved = application.snapshot().display();
    moved.crosshair = {16, 9, 4};
    application.set_display(moved);
    const auto beforeUndo = application.snapshot();
    const auto historyBeforeUndo = application.history();
    application.undo();
    const auto afterUndo = application.snapshot();
    require(afterUndo.display().crosshair == moved.crosshair &&
                afterUndo.display().orthogonal == moved.orthogonal &&
                application.history().undo_steps == 0 &&
                application.history().redo_steps == 1,
            "Pixel undo did not retain the current orthogonal view");
    require(sourcePixels(afterUndo) == sourcePixels(source),
            "Pixel undo did not restore the independent source");
    writeFloat32(directory / "after-undo-source.f32le", sourcePixels(afterUndo));
    std::ofstream output(directory / "cpp-api.json", std::ios::binary);
    output.exceptions(std::ios::badbit | std::ios::failbit);
    output << "{\n\"schema_version\":1,\"context\":\"cpp_api\","
              "\"requirement_id\":\"G027.xyz_keyboard_navigation\","
              "\"evidence_scope\":\"Public headless view/result representation; "
              "no keyboard dispatch proof\",\"world_atol\":1e-12,\"world_rtol\":0,"
              "\"source\":{\"shape_zyx\":[9,17,32],\"dtype\":\"Float32\","
              "\"encoding\":\"IEEE754 little-endian ZYX\",\"raw_path\":"
           << quote((directory / "source.f32le").u8string())
           << ",\"expected_sha256\":" << quote(sourceSha)
           << "},\"scenarios\":[\n";
    for (std::size_t i = 0; i < rows.size(); ++i)
      output << (i ? ",\n" : "") << rows[i];
    output << "\n],\"invalid_setters\":" << rejected.str()
           << ",\"pixel_undo\":{\"before\":"
           << snapshotJson(beforeUndo, historyBeforeUndo)
           << ",\"after\":" << snapshotJson(afterUndo, application.history())
           << ",\"current_view_retained\":true,\"source_restored\":true}}\n";
    output.close();
    std::cout << "Installed C++ XYZ state: rotated Float32 planes/world, atomic "
                 "invalid setters, source/history invariants and view-preserving undo passed\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
