#include "headless_session_internal.hpp"
#include "window.hpp"

#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
using namespace imagejxx;
using namespace imagejxx::desktop;
using NativeDocument = imagejxx::Document;
constexpr auto opted = FloatingSamplePolicy::PreserveQuietNaNFloat32;
constexpr auto finite = FloatingSamplePolicy::FiniteOnly;
constexpr std::uint64_t quiet = 0x7ff8000020000000ULL;
constexpr std::uint64_t negative_quiet = 0xfff82468a0000000ULL;
constexpr std::array<std::uint32_t, 6> first32{
    0xc2000000u, 0x7fc00001u, 0x80000000u, 0x00000000u, 0x42000000u, 0x42800000u};
constexpr std::array<std::uint64_t, 6> first64{
    0xc040000000000000ULL, quiet, 0x8000000000000000ULL,
    0x0000000000000000ULL, 0x4040000000000000ULL, 0x4050000000000000ULL};
constexpr std::array<std::uint64_t, 6> second64{
    0xc030000000000000ULL, 0, negative_quiet, 0x4030000000000000ULL,
    0x8000000000000000ULL, 0x4048000000000000ULL};
static_assert(sizeof(double) == 8 && std::numeric_limits<double>::is_iec559,
              "Raw assertions require IEEE binary64 source storage");

void check(bool condition, const char *message) {
  if (!condition)
    throw std::runtime_error(message);
}
double represented(std::uint64_t bits) {
  double value;
  std::memcpy(&value, &bits, sizeof(value));
  return value;
}
std::uint64_t stored(double value) {
  std::uint64_t bits;
  std::memcpy(&bits, &value, sizeof(bits));
  return bits;
}
Image literal(const std::array<std::uint64_t, 6> &bits) {
  Image result(3, 2, 1, PixelType::Float32);
  for (std::size_t i = 0; i < bits.size(); ++i)
    result.pixels[i] = represented(bits[i]);
  return result;
}
void exact_image(const Image &actual, const std::array<std::uint64_t, 6> &bits) {
  check(actual.width == 3 && actual.height == 2 && actual.channels == 1 &&
            actual.type == PixelType::Float32 && actual.pixels.size() == bits.size(),
        "Actual source image shape/type changed");
  for (std::size_t i = 0; i < bits.size(); ++i)
    check(stored(actual.pixels[i]) == bits[i], "Actual source IEEE bytes changed");
}
std::vector<unsigned int> expected_bytes(const std::vector<std::uint32_t> &samples) {
  std::vector<unsigned int> result;
  for (auto value : samples)
    for (unsigned int byte = 0; byte < 4; ++byte)
      result.push_back((value >> (8 * byte)) & 0xffu);
  return result;
}
void finite_json(const Json &root) {
  std::vector<const Json *> pending{&root};
  while (!pending.empty()) {
    const auto *item = pending.back();
    pending.pop_back();
    if (item->is_number_float())
      check(std::isfinite(item->get<double>()), "Actual headless report contains nonfinite numeric JSON");
    else if (item->is_structured())
      for (const auto &child : *item)
        pending.push_back(&child);
  }
  check(Json::parse(root.dump()) == root, "Headless JSON dump silently substituted source NaN with null");
}
void source_readback(const Json &readback, int width, int height,
                     const std::vector<std::uint32_t> &bits) {
  check(readback.is_object() && readback.size() == 4 &&
            readback.at("shape") == Json::array({height, width, 1}) &&
            readback.at("dtype") == "float32" &&
            readback.at("encoding") == "IEEE754 Float32LE" &&
            !readback.contains("pixels"),
        "Actual opted CPU source cache lost its typed raw JSON representation");
  const auto &actual = readback.at("bytes");
  const auto expected = expected_bytes(bits);
  check(actual.is_array() && actual.size() == expected.size(), "Actual source byte count changed");
  for (std::size_t i = 0; i < expected.size(); ++i) {
    check(actual[i].is_number_integer() && !actual[i].is_boolean() &&
              actual[i] >= 0 && actual[i] <= 255 &&
              actual[i].get<unsigned int>() == expected[i],
          "Actual cached Float32LE payload/signed-zero byte differs from literal expectation");
  }
  finite_json(readback);
}
void rgb_readback(const Json &readback, int width, int height,
                  const std::vector<int> &gray) {
  check(readback.is_object() && readback.size() == 3 &&
            readback.at("shape") == Json::array({height, width, 3}) &&
            readback.at("dtype") == "uint8" && !readback.contains("encoding"),
        "Actual finite rendered RGB JSON representation changed");
  const auto &pixels = readback.at("pixels");
  check(pixels.is_array() && pixels.size() == gray.size() * 3, "Actual RGB byte count changed");
  for (std::size_t i = 0; i < gray.size(); ++i)
    for (int channel = 0; channel < 3; ++channel) {
      const auto &value = pixels[i * 3 + channel];
      check(value.is_number() && std::isfinite(value.get<double>()) &&
                value.get<double>() == gray[i], "Actual CPU rendered RGB/LUT0 differs");
    }
  finite_json(readback);
}
void ordinary(const Json &frame, const std::vector<std::uint32_t> &bits,
              const std::vector<int> &gray, std::uint64_t id) {
  const auto &pixels = frame.at("ordinary_pixels");
  check(pixels.at("pixels_source") == "live" && pixels.at("preview_generation") == 0 &&
            pixels.at("source_document_id") == id && pixels.at("readback").at("available") == true,
        "Ordinary source readback did not come from the actual live native document");
  source_readback(pixels.at("readback").at("source"), 3, 2, bits);
  rgb_readback(pixels.at("readback").at("rendered_rgb"), 3, 2, gray);
}

class Artifacts {
public:
  explicit Artifacts(const std::filesystem::path &requested)
      : root_(std::filesystem::absolute(requested).lexically_normal()),
        deadline_(std::chrono::steady_clock::now() + std::chrono::seconds(30)) {
    check(root_ != root_.root_path() && std::filesystem::is_directory(root_.parent_path()),
          "Native Window assertions require an existing artifact parent");
    check(std::filesystem::create_directory(root_), "Native Window artifact directory must be fresh");
  }
  std::filesystem::path file(const std::string &name) const {
    const std::filesystem::path basename(name);
    check(!basename.is_absolute() && basename.filename() == basename &&
              name != "." && name != "..", "Artifact name must be a direct child");
    const auto path = (root_ / basename).lexically_normal();
    check(path.parent_path() == root_, "Artifact escaped its owned directory");
    return path;
  }
  void admit_frame() {
    check(++frames_ <= 1200 && std::chrono::steady_clock::now() < deadline_,
          "Native Window test exceeded its 30s/1200-frame inner budget");
  }
private:
  std::filesystem::path root_;
  std::chrono::steady_clock::time_point deadline_;
  std::size_t frames_ = 0;
};
class Frames {
public:
  Frames(Window &window, Artifacts &artifacts, std::string prefix)
      : window_(window), artifacts_(artifacts), prefix_(std::move(prefix)), session_(window) {
    const auto opened = session_.lifecycle();
    writeJson(artifacts_.file(prefix_ + "-opened.json").u8string(), opened);
    check(opened.at("open") == true && opened.at("context_id") != 0 &&
              opened.at("context_id") == opened.at("current_context_id"),
          "No genuine creator-owned headless ImGui context was opened");
    session_id_ = opened.at("session_id").get<std::uint64_t>();
    context_id_ = opened.at("context_id").get<std::uint64_t>();
  }
  Json tick() {
    artifacts_.admit_frame();
    auto actual = session_.frame();
    ++ordinal_;
    // Persist the actual returned cache/input/context observation before the
    // scientific assertions, including any erroneous data for diagnosis.
    writeJson(artifacts_.file(prefix_ + "-frame-" + std::to_string(ordinal_) + ".json").u8string(), actual);
    const auto &life = actual.at("session");
    check(life.at("open") == true && life.at("session_id") == session_id_ &&
              life.at("context_id") == context_id_ &&
              life.at("current_context_id") == context_id_ &&
              life.at("imgui_frame").get<int>() > last_frame_,
          "Separate actual frames did not retain and advance the same headless context");
    last_frame_ = life.at("imgui_frame").get<int>();
    finite_json(actual);
    return actual;
  }
  Json warm() {
    Json result;
    for (int i = 0; i < 3; ++i)
      result = tick();
    return result;
  }
  void close() {
    session_.close();
    const auto closed = session_.lifecycle();
    writeJson(artifacts_.file(prefix_ + "-closed.json").u8string(), closed);
    check(closed.at("open") == false && closed.at("context_id") == 0 &&
              closed.at("current_context_id") == 0 && !window_.busy() && window_.jobs().empty(),
          "Creator cleanup retained a renderer context or native work");
  }
private:
  Window &window_;
  Artifacts &artifacts_;
  std::string prefix_;
  HeadlessInputSession session_;
  std::uint64_t session_id_ = 0, context_id_ = 0;
  int ordinal_ = 0, last_frame_ = 0;
};
std::shared_ptr<NativeDocument> add_source(Window &window, bool all_nan = false) {
  auto first = literal(first64), second = literal(second64);
  if (all_nan)
    for (std::size_t i = 0; i < 6; ++i) {
      first.pixels[i] = represented(i % 2 ? negative_quiet : quiet);
      second.pixels[i] = represented(i % 2 ? quiet : negative_quiet);
    }
  auto document = std::make_shared<NativeDocument>(
      Dataset::from_stack({first, second}, {}, opted), "Owned native Window quiet-NaN source");
  window.addNativeDocument(document);
  auto display = document->snapshot().display();
  display.crosshair = {2, 0, 0};
  if (!all_nan)
    display.properties["display_range"] = metadata_array({-32., 64.});
  window.setNativeDisplay(display, document->id);
  document->mark_saved();
  return document;
}
void unchanged(const std::shared_ptr<NativeDocument> &document,
               const DocumentSnapshot &before) {
  const auto now = document->snapshot();
  check(now.shares_data(before) && now.revisions().matches(before.revisions(), Change::Data) &&
            now.data().dataset.sample_policy() == opted && document->undo_steps() == 0 &&
            document->redo_steps() == 0 && !now.dirty(),
        "Rendering/clipboard preparation changed actual source data, policy or history");
  exact_image(*now.data().dataset.plane(), first64);
  exact_image(*now.data().dataset.plane({0, 0, 1}), second64);
  exact_image(*before.data().dataset.plane(), first64);
}

void W01_ordinary_raw_lut0(Artifacts &artifacts) {
  Window window(artifacts.file("w01-prefs.json").u8string());
  const auto source = add_source(window);
  const auto before = source->snapshot();
  Frames frames(window, artifacts, "w01");
  const std::vector<std::uint32_t> bits(first32.begin(), first32.end());
  ordinary(frames.warm(), bits, {0, 0, 85, 85, 170, 255}, source->id);
  window.action("lut", {{"name", "Inverted"}}); // Actual existing display-only API.
  ordinary(frames.tick(), bits, {255, 255, 170, 170, 85, 0}, source->id);
  unchanged(source, before);
  frames.close();
}

void W02_xyz_actual_cache_raw(Artifacts &artifacts) {
  Window window(artifacts.file("w02-prefs.json").u8string());
  const auto source = add_source(window);
  const auto before = source->snapshot();
  Frames frames(window, artifacts, "w02");
  ordinary(frames.warm(), {first32.begin(), first32.end()}, {0, 0, 85, 85, 170, 255}, source->id);
  auto display = source->snapshot().display();
  display.orthogonal = true;
  window.setNativeDisplay(display, source->id); // Labelled API view preparation.
  const auto observed = frames.warm();
  const auto &planes = observed.at("orthogonal_planes");
  check(planes.is_array() && planes.size() == 3, "Actual XYZ cache rows are missing");
  const std::array<std::string, 3> names{"XY", "XZ", "YZ"};
  const std::array<int, 3> widths{3, 3, 2}, coordinates{0, 0, 2};
  const std::array<std::vector<std::uint32_t>, 3> raw{{
      {first32.begin(), first32.end()},
      {0xc2000000u, 0x7fc00001u, 0x80000000u, 0xc1800000u, 0, 0xffc12345u},
      {0x80000000u, 0x42800000u, 0xffc12345u, 0x42400000u}}};
  const std::array<std::vector<int>, 3> colors{{
      {0, 0, 85, 85, 170, 255}, {0, 0, 85, 43, 85, 0}, {85, 255, 0, 213}}};
  for (std::size_t i = 0; i < planes.size(); ++i) {
    const auto &plane = planes[i];
    check(plane.at("plane") == names[i] && plane.at("coordinate") == coordinates[i] &&
              plane.at("visible") == true && plane.at("builds") > 0 &&
              plane.at("pixels_source") == "live_document" &&
              plane.at("preview_generation") == 0 && plane.at("source_document_id") == source->id &&
              plane.at("range") == Json::array({-32, 64}) &&
              plane.at("readback").at("available") == true,
          "Actual visible XYZ cache/source/coordinate/range identity differs");
    source_readback(plane.at("readback").at("source"), widths[i], 2, raw[i]);
    rgb_readback(plane.at("readback").at("rendered_rgb"), widths[i], 2, colors[i]);
  }
  unchanged(source, before);
  frames.close();
}

void W03_all_nan_actual_window_range(Artifacts &artifacts) {
  Window window(artifacts.file("w03-prefs.json").u8string());
  const auto source = add_source(window, true);
  const auto before = source->snapshot();
  Frames frames(window, artifacts, "w03");
  const std::vector<std::uint32_t> bits{0x7fc00001u, 0xffc12345u, 0x7fc00001u,
                                     0xffc12345u, 0x7fc00001u, 0xffc12345u};
  ordinary(frames.warm(), bits, {0, 0, 0, 0, 0, 0}, source->id);
  auto display = source->snapshot().display();
  display.orthogonal = true;
  window.setNativeDisplay(display, source->id);
  const auto observed = frames.warm();
  const auto &planes = observed.at("orthogonal_planes");
  check(planes.size() == 3, "All-NaN actual XYZ caches are missing");
  const std::array<std::vector<std::uint32_t>, 3> expected_raw{{
      bits, bits, {0x7fc00001u, 0xffc12345u, 0xffc12345u, 0x7fc00001u}}};
  for (std::size_t i = 0; i < planes.size(); ++i) {
    const auto &plane = planes[i];
    check(plane.at("range") == Json::array({0, 1}) && plane.at("visible") == true &&
              plane.at("source_document_id") == source->id &&
              plane.at("readback").at("available") == true,
          "Actual all-NaN XYZ range did not use owned [0,1] fallback");
    const int width = plane.at("width").get<int>(), height = plane.at("height").get<int>();
    rgb_readback(plane.at("readback").at("rendered_rgb"), width, height,
                 std::vector<int>(static_cast<std::size_t>(width) * height, 0));
    source_readback(plane.at("readback").at("source"), i == 2 ? 2 : 3, 2, expected_raw[i]);
  }
  for (int z = 0; z < 2; ++z)
    for (std::size_t i = 0; i < 6; ++i) {
      const auto expected = (i % 2 == std::size_t(z)) ? quiet : negative_quiet;
      check(stored(source->snapshot().data().dataset.plane({0, 0, z})->pixels[i]) == expected &&
                stored(before.data().dataset.plane({0, 0, z})->pixels[i]) == expected,
            "All-NaN rendering mutated actual original payload bits");
    }
  check(source->snapshot().shares_data(before) && source->undo_steps() == 0,
        "All-NaN display changed source data/history");
  frames.close();
}

void W04_raw_clipboard_paste_new_policy(Artifacts &artifacts) {
  Window window(artifacts.file("w04-prefs.json").u8string());
  const auto source = add_source(window);
  const auto before = source->snapshot();
  Frames frames(window, artifacts, "w04");
  frames.warm();
  window.action("copy"); // Headless in-process clipboard; no OS call is claimed.
  window.action("paste_new");
  const auto pasted = window.document();
  check(window.documents().size() == 2 && pasted->id != source->id &&
            pasted->samplePolicy() == opted && pasted->state().frames.size() == 1,
        "Captured raw clipboard did not produce a distinct opted new document");
  exact_image(pasted->frame(), first64);
  ordinary(frames.warm(), {first32.begin(), first32.end()}, {0, 0, 85, 85, 170, 255}, pasted->id);
  writeJson(artifacts.file("w04-pasted-record.json").u8string(), documentJson(*pasted));
  check(documentJson(*pasted).at("pixel_sample_policy") == "preserve_quiet_nan_float32" &&
            pasted->undoSteps() == 0, "Pasted source policy/history differs");
  unchanged(source, before);
  frames.close();
}

void W05_flatten_is_finite_rendered_copy(Artifacts &artifacts) {
  Window window(artifacts.file("w05-prefs.json").u8string());
  const auto source = add_source(window);
  const auto before = source->snapshot();
  Frames frames(window, artifacts, "w05");
  frames.warm();
  window.action("flatten");
  const auto flattened = window.document();
  check(window.documents().size() == 2 && flattened->id != source->id &&
            flattened->samplePolicy() == finite && flattened->frame().type == PixelType::UInt8 &&
            flattened->frame().channels == 3 && flattened->undoSteps() == 0,
        "Rendered flatten did not produce a distinct ordinary finite RGB document");
  const std::vector<int> gray{0, 0, 85, 85, 170, 255};
  for (std::size_t i = 0; i < gray.size(); ++i)
    for (int c = 0; c < 3; ++c)
      check(flattened->frame().pixels[i * 3 + c] == gray[i],
            "Actual flattened RGB differs from literal LUT0/finite neighbor expectation");
  const auto observed = frames.warm();
  const auto &readback = observed.at("ordinary_pixels").at("readback");
  check(readback.at("available") == true, "Flattened ordinary RGB readback is absent");
  rgb_readback(readback.at("source"), 3, 2, gray);
  rgb_readback(readback.at("rendered_rgb"), 3, 2, gray);
  const auto record = documentJson(*flattened);
  writeJson(artifacts.file("w05-flattened-record.json").u8string(), record);
  check(!record.contains("pixel_sample_policy"), "Finite flatten acquired a quiet-NaN policy token");
  unchanged(source, before);
  frames.close();
}

void W06_finite_recipient_paste_rejects_atomically(Artifacts &artifacts) {
  Window window(artifacts.file("w06-prefs.json").u8string());
  const auto source = add_source(window);
  const auto original = source->snapshot();
  Frames frames(window, artifacts, "w06");
  frames.warm();
  window.action("copy");
  Image pixels(3, 2, 1, PixelType::Float32);
  pixels.pixels.assign(6, 3.0);
  const auto recipient = std::make_shared<NativeDocument>(Dataset::from_stack({pixels}), "Finite recipient");
  window.addNativeDocument(recipient);
  recipient->mark_saved();
  frames.warm();
  const auto before = recipient->snapshot();
  const auto record_before = documentJson(*window.document());
  bool rejected = false;
  try {
    window.action("paste", {{"x", 0}, {"y", 0}});
  } catch (const std::invalid_argument &error) {
    rejected = true;
    writeJson(artifacts.file("w06-paste-rejected.json").u8string(),
              {{"exception", "invalid_argument"}, {"message", error.what()},
               {"observed_recipient_record", documentJson(*window.document())}});
  }
  check(rejected, "Finite recipient silently admitted opted raw clipboard pixels");
  frames.tick();
  const auto after = recipient->snapshot();
  check(after.shares_data(before) && after.revisions().matches(before.revisions(), Change::All) &&
            after.data_identity() == before.data_identity() && after.dirty() == before.dirty() &&
            after.data().dataset.sample_policy() == finite && recipient->undo_steps() == 0 &&
            recipient->redo_steps() == 0 && documentJson(*window.document()) == record_before &&
            window.documents().size() == 2,
        "Rejected finite paste changed actual source/revisions/record/history/policy");
  for (double actual : after.data().dataset.plane()->pixels)
    check(stored(actual) == 0x4008000000000000ULL, "Rejected paste mutated finite recipient bytes");
  unchanged(source, original);
  frames.close();
}

struct Test {
  const char *name;
  void (*run)(Artifacts &);
};
constexpr std::array<Test, 6> tests{{
    {"NAN_WINDOW_W01_ordinary_raw_lut0", W01_ordinary_raw_lut0},
    {"NAN_WINDOW_W02_xyz_actual_cache_raw", W02_xyz_actual_cache_raw},
    {"NAN_WINDOW_W03_all_nan_actual_window_range", W03_all_nan_actual_window_range},
    {"NAN_WINDOW_W04_raw_clipboard_paste_new_policy", W04_raw_clipboard_paste_new_policy},
    {"NAN_WINDOW_W05_flatten_is_finite_rendered_copy", W05_flatten_is_finite_rendered_copy},
    {"NAN_WINDOW_W06_finite_recipient_paste_rejects_atomically", W06_finite_recipient_paste_rejects_atomically},
}};
} // namespace

int main(int argc, char **argv) {
  try {
    check(argc == 2, "Supply a fresh native Window artifact directory");
    Artifacts artifacts(argv[1]);
    for (const auto &test : tests) {
      try {
        test.run(artifacts);
        std::cout << "PASS " << test.name << '\n';
      } catch (const std::exception &error) {
        std::cerr << "FAIL " << test.name << ": " << error.what() << '\n';
        return 1;
      }
    }
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "FAIL NAN_WINDOW_entry: " << error.what() << '\n';
    return 1;
  }
}
