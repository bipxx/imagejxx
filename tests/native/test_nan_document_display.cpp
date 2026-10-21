#include "desktop.hpp"
#include "rendering.hpp"
#include <zlib.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {
using namespace imagejxx;
using namespace imagejxx::desktop;
using StackDocument = imagejxx::desktop::Document;
constexpr auto opted = FloatingSamplePolicy::PreserveQuietNaNFloat32;
constexpr auto finite = FloatingSamplePolicy::FiniteOnly;
constexpr std::uint64_t qnan = 0x7ff8000020000000ULL;
constexpr std::uint64_t negative_qnan = 0xfff82468a0000000ULL;
constexpr std::array<std::uint64_t, 6> finite_samples{
    0x0000000000000000ULL, 0x8000000000000000ULL, 0x3ff0000000000000ULL,
    0xc004000000000000ULL, 0x36a0000000000000ULL, 0x4008000000000000ULL};
constexpr std::array<std::uint64_t, 6> quiet_samples{
    0x0000000000000000ULL, 0x8000000000000000ULL, 0x3ff0000000000000ULL,
    0xc004000000000000ULL, 0x36a0000000000000ULL, qnan};
static_assert(sizeof(double) == 8 && std::numeric_limits<double>::is_iec559,
              "Literal tests require IEEE binary64 storage");

void check(bool condition, const char *message) {
  if (!condition)
    throw std::runtime_error(message);
}
template <class Function> void reject(Function run, const char *message) {
  bool rejected = false;
  try {
    run();
  } catch (const std::invalid_argument &) {
    rejected = true;
  }
  check(rejected, message);
}
double represented(std::uint64_t bits) {
  double value;
  std::memcpy(&value, &bits, sizeof(value));
  return value;
}
std::uint64_t raw_bits(double value) {
  std::uint64_t bits;
  std::memcpy(&bits, &value, sizeof(bits));
  return bits;
}
std::vector<std::uint64_t> samples(bool quiet = true) {
  const auto &literal = quiet ? quiet_samples : finite_samples;
  return {literal.begin(), literal.end()};
}
Image image(bool quiet = true) {
  Image result(3, 2, 1, PixelType::Float32);
  const auto expected = samples(quiet);
  for (std::size_t i = 0; i < expected.size(); ++i)
    result.pixels[i] = represented(expected[i]);
  return result;
}
void exact(const Image &actual, const std::vector<std::uint64_t> &expected,
           int width = 3, int height = 2) {
  check(actual.width == width && actual.height == height && actual.channels == 1 &&
            actual.type == PixelType::Float32 && actual.pixels.size() == expected.size(),
        "Actual source shape/type changed");
  for (std::size_t i = 0; i < expected.size(); ++i)
    check(raw_bits(actual.pixels[i]) == expected[i],
          "Actual source bytes changed a finite sample, signed zero or NaN payload");
}
std::vector<unsigned char> little_doubles(const std::vector<std::uint64_t> &bits) {
  std::vector<unsigned char> result;
  for (auto item : bits)
    for (int byte = 0; byte < 8; ++byte)
      result.push_back(static_cast<unsigned char>(item >> (8 * byte)));
  return result;
}
// Independent base64 expectation: no product encoder/decoder generates the
// expected wire bytes. Pixel NaNs exist only in this opaque typed payload.
std::string fixture_base64(const std::vector<unsigned char> &bytes) {
  constexpr char alphabet[] =
      "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  std::string result;
  for (std::size_t i = 0; i < bytes.size(); i += 3) {
    const auto count = std::min<std::size_t>(3, bytes.size() - i);
    const std::uint32_t word = (std::uint32_t(bytes[i]) << 16) |
        (count > 1 ? std::uint32_t(bytes[i + 1]) << 8 : 0) |
        (count > 2 ? std::uint32_t(bytes[i + 2]) : 0);
    result += alphabet[(word >> 18) & 63];
    result += alphabet[(word >> 12) & 63];
    result += count > 1 ? alphabet[(word >> 6) & 63] : '=';
    result += count > 2 ? alphabet[word & 63] : '=';
  }
  return result;
}
std::vector<unsigned char> fixture_session(const Json &root) {
  const auto text = root.dump();
  check(text.size() <= std::numeric_limits<std::uint32_t>::max(),
        "Owned session fixture is too large");
  std::vector<unsigned char> result{'I', 'M', 'A', 'G', 'E', 'J', 'X', 'X', '2', '\n'};
  for (int byte = 3; byte >= 0; --byte)
    result.push_back(static_cast<unsigned char>(text.size() >> (8 * byte)));
  const auto start = result.size();
  uLongf compressed = compressBound(static_cast<uLong>(text.size()));
  result.resize(start + compressed);
  check(compress2(result.data() + start, &compressed,
                  reinterpret_cast<const Bytef *>(text.data()),
                  static_cast<uLong>(text.size()), Z_DEFAULT_COMPRESSION) == Z_OK,
        "Independent session fixture compression failed");
  result.resize(start + compressed);
  return result;
}
Json fixture_root(const Json &record, const Json &results = Json::array(),
                  const Json &workflow = Json::array()) {
  return {{"format", "imagejxx-session"}, {"version", 2},
          {"documents", Json::array({record})}, {"results", results}, {"workflow", workflow}};
}
void write_fixture(const std::filesystem::path &path,
                   const std::vector<unsigned char> &bytes) {
  check(!std::filesystem::exists(path), "Fixture path must be fresh");
  std::ofstream file(path, std::ios::binary);
  file.write(reinterpret_cast<const char *>(bytes.data()),
             static_cast<std::streamsize>(bytes.size()));
  check(bool(file), "Independent fixture write failed");
  file.close();
  check(bool(file), "Independent fixture close failed");
}
class FreshDirectory {
public:
  explicit FreshDirectory(const std::filesystem::path &requested)
      : path_(std::filesystem::absolute(requested).lexically_normal()) {
    check(path_ != path_.root_path() && std::filesystem::is_directory(path_.parent_path()),
          "A fresh test directory needs an existing parent");
    check(std::filesystem::create_directory(path_), "Test directory must not already exist");
  }
  std::filesystem::path file(const std::string &name) const {
    const std::filesystem::path relative(name);
    check(!relative.is_absolute() && relative.filename() == relative &&
              name != "." && name != "..", "Fixture basename must stay inside owned directory");
    const auto target = (path_ / relative).lexically_normal();
    check(target.parent_path() == path_, "Fixture path escaped owned directory");
    return target;
  }
  // Keep observations on both success and failure; no recursive cleanup or
  // overwrite of historical artifacts. Each root gate supplies a fresh path.
private:
  std::filesystem::path path_;
};

void D01_admission_and_inherited_owners(const FreshDirectory &) {
  auto caller = image();
  StackDocument document(Stack{caller}, "Owned quiet source", Json::object(), opted);
  check(document.samplePolicy() == opted && document.state().frames.sample_policy() == opted &&
            document.nativeSnapshot().data().dataset.sample_policy() == opted,
        "StackDocument did not retain immutable source policy");
  exact(document.frame(), samples());
  check(float32_source_sample_bits(document.frame().pixels.back(), opted) == 0x7fc00001u &&
            raw_bits(document.frame().pixels[1]) == 0x8000000000000000ULL,
        "StackDocument lost literal Float32 payload or signed zero");
  caller.pixels.back() = 9;
  exact(document.frame(), samples());
  StackDocument table(document.state().frames, "Inherited table");
  StackDocument state(document.state());
  StackDocument owner(document.nativeOwner());
  for (const auto *inherited : {&table, &state, &owner}) {
    check(inherited->samplePolicy() == opted, "Existing owner route silently changed policy");
    exact(inherited->frame(), samples());
  }
  reject([] { StackDocument invalid(Stack{image()}); },
         "Default StackDocument admitted a quiet NaN");
  StackDocument ordinary(Stack{image(false)});
  check(ordinary.samplePolicy() == finite, "Finite StackDocument default changed");
  exact(ordinary.frame(), samples(false));
}

void D02_state_edits_undo_redo(const FreshDirectory &) {
  StackDocument document(Stack{image(), image(false)}, "Initial", Json::object(), opted);
  const auto frozen = document.snapshot();
  const auto original_z1 = document.state().frames.shared(1);
  auto changed = document.state();
  changed.frames.replace(1, image());
  document.commit(std::move(changed));
  const auto changed_z1 = document.state().frames.shared(1);
  check(document.undoSteps() == 1 && document.redoSteps() == 0 && changed_z1 != original_z1,
        "Changed opted source was not one actual history step");
  auto metadata = document.state();
  metadata.metadata["title"] = "Renamed";
  metadata.metadata["calibration"]["pixel_width"] = .5;
  metadata.metadata["calibration"]["unit"] = "um";
  document.commit(std::move(metadata));
  check(document.undoSteps() == 2 && document.state().frames.shared(1) == changed_z1 &&
            document.spatialCalibration().spacing[0] == .5,
        "Metadata/calibration history changed immutable source storage");
  document.undo();
  document.undo();
  check(document.samplePolicy() == opted && document.undoSteps() == 0 &&
            document.redoSteps() == 2 && document.state().frames.shared(1) == original_z1,
        "Undo did not restore source policy and actual original alias");
  exact(document.state().frames[0], samples());
  exact(document.state().frames[1], samples(false));
  document.redo();
  document.redo();
  check(document.samplePolicy() == opted && document.title() == "Renamed" &&
            document.undoSteps() == 2 && document.redoSteps() == 0 &&
            document.state().frames.shared(1) == changed_z1,
        "Redo did not restore opted immutable state");
  exact(document.state().frames[0], samples());
  exact(document.state().frames[1], samples());
  exact(frozen.state().frames[0], samples());
  exact(frozen.state().frames[1], samples(false));
}

void D03_v2_json_policy_and_exact_payload(const FreshDirectory &) {
  StackDocument document(Stack{image()}, "JSON source", Json::object(), opted);
  auto record = documentJson(document);
  check(record.at("pixel_sample_policy") == "preserve_quiet_nan_float32" &&
            record.at("dtype") == "float32" && record.at("shape") == Json::array({2, 3}) &&
            record.at("pixels") == fixture_base64(little_doubles(samples())),
        "V2 record policy or independent IEEE pixel payload changed");
  auto restored = documentFromJson(record);
  check(restored->samplePolicy() == opted && documentJson(*restored) == record &&
            !restored->state().metadata.contains("pixel_sample_policy"),
        "V2 decode lost policy or leaked a reserved wire field into metadata");
  exact(restored->frame(), samples());
  exact(document.frame(), samples());
}

void D04_binary_session_save_reopen(const FreshDirectory &directory) {
  auto document = std::make_shared<StackDocument>(
      Stack{image(), image(false)}, "Session source", Json::object(), opted);
  auto state = document->state();
  state.metadata["unknown_extension"] = {{"retained", true}};
  state.metadata["rois"] = Json::array({{{"name", "source ROI"}, {"slice_index", 0},
      {"selection", {{"kind", "rectangle"}, {"points", {{0., 0.}, {2., 1.}}},
                     {"unknown_shape", "kept"}}}, {"unknown_roi", 17}}});
  document->commit(std::move(state));
  document->setView({{"crosshair", {1, 1, 1}}});
  const Json results = Json::array({{{"Label", "owned"}, {"Mean", 1.25}}});
  const Json workflow = Json::array({{{"operation", "add"}, {"parameters", {{"value", 1}}}}});
  const auto record = documentJson(*document);
  auto all_samples = samples();
  const auto finite_values = samples(false);
  all_samples.insert(all_samples.end(), finite_values.begin(), finite_values.end());
  check(record.at("pixels") == fixture_base64(little_doubles(all_samples)),
        "Stack wire pixel ordering differs from independent little-endian samples");
  const auto first = directory.file("opted-v2.ijxx");
  saveSession(first.string(), {document}, results, workflow);
  const auto expected = fixture_session(fixture_root(record, results, workflow));
  check(readBytes(first.string()) == expected, "Actual binary v2 framing/payload differs");
  Json reopened_results, reopened_workflow;
  auto reopened = loadSession(first.string(), reopened_results, reopened_workflow);
  check(reopened.size() == 1 && reopened_results == results && reopened_workflow == workflow &&
            reopened.front()->samplePolicy() == opted &&
            documentJson(*reopened.front()) == record,
        "Actual v2 save/reopen changed complete typed record or tables");
  exact(reopened.front()->state().frames[0], samples());
  exact(reopened.front()->state().frames[1], samples(false));
  const auto second = directory.file("opted-v2-again.ijxx");
  saveSession(second.string(), reopened, reopened_results, reopened_workflow);
  check(readBytes(second.string()) == expected, "Reopened v2 byte representation changed");
  exact(document->state().frames[0], samples());
}

void D05_absent_token_finite_compatibility(const FreshDirectory &directory) {
  for (auto type : {PixelType::UInt8, PixelType::UInt16, PixelType::Float32, PixelType::Float64}) {
    Image pixels(3, 2, 1, type);
    pixels.pixels = {0, 1, 2, 3, 4, 5};
    auto document = std::make_shared<StackDocument>(Stack{pixels}, "Finite v2 fixture");
    const auto record = documentJson(*document);
    check(!record.contains("pixel_sample_policy") && document->samplePolicy() == finite,
          "Finite-only v2 record acquired an extra policy field");
    auto decoded = documentFromJson(record);
    check(decoded->samplePolicy() == finite && documentJson(*decoded) == record &&
              decoded->frame().pixels == pixels.pixels,
          "Absent-token legacy finite record changed dtype/pixels/layout");
    const auto suffix = typeName(type);
    const auto fixture = directory.file("finite-" + suffix + ".ijxx");
    const auto independent = fixture_session(fixture_root(record));
    write_fixture(fixture, independent);
    Json rows, steps;
    const auto loaded = loadSession(fixture.string(), rows, steps);
    check(loaded.size() == 1 && loaded.front()->samplePolicy() == finite &&
              documentJson(*loaded.front()) == record && rows.empty() && steps.empty(),
          "Independent finite absent-token session did not reopen compatibly");
    const auto saved = directory.file("finite-saved-" + suffix + ".ijxx");
    saveSession(saved.string(), loaded, rows, steps);
    check(readBytes(saved.string()) == independent,
          "Finite v2 serializer changed old framing/field absence/pixel bytes");
  }
}

void D06_policy_token_storage_rejections(const FreshDirectory &) {
  StackDocument document(Stack{image()}, "Policy negatives", Json::object(), opted);
  const auto valid = documentJson(document);
  for (const auto &token : std::vector<Json>{nullptr, true, false, 1, 2.0, "finite_only",
          "preserve_nan", Json::object(), Json::array()}) {
    auto invalid = valid;
    invalid["pixel_sample_policy"] = token;
    reject([&] { (void)documentFromJson(invalid); }, "Malformed or unknown policy token accepted");
  }
  auto missing = valid;
  missing.erase("pixel_sample_policy");
  reject([&] { (void)documentFromJson(missing); }, "Absent policy token admitted quiet-NaN pixels");
  for (const auto &dtype : {"uint8", "uint16", "float64"}) {
    auto invalid = valid;
    invalid["dtype"] = dtype;
    reject([&] { (void)documentFromJson(invalid); }, "Opted token accepted a wrong dtype");
  }
  for (int channels : {3, 4}) {
    auto invalid = valid;
    invalid["shape"] = {2, 3, channels};
    reject([&] { (void)documentFromJson(invalid); }, "Opted token accepted RGB/RGBA storage");
  }
  for (auto type : {PixelType::UInt8, PixelType::UInt16, PixelType::Float64}) {
    Image wrong(3, 2, 1, type);
    reject([&] { StackDocument invalid(Stack{wrong}, "Wrong dtype", Json::object(), opted); },
           "Explicit StackDocument policy admitted a different declared dtype");
  }
  for (int channels : {3, 4}) {
    Image color(3, 2, channels, PixelType::Float32);
    reject([&] { StackDocument invalid(Stack{color}, "Color", Json::object(), opted); },
           "Explicit StackDocument policy admitted RGB/RGBA storage");
  }
  reject([] {
    StackDocument invalid(Stack{image(false)}, "Unknown", Json::object(),
                          static_cast<FloatingSamplePolicy>(73));
  }, "StackDocument accepted an unknown immutable policy");
  auto reserved = document.state();
  reserved.metadata["pixel_sample_policy"] = "preserve_quiet_nan_float32";
  reject([&] { document.commit(reserved); }, "Metadata admitted reserved pixel policy field");
  exact(document.frame(), samples());
  check(document.undoSteps() == 0, "Rejected policy metadata produced history");
}

void D07_raw_and_compressed_payload_rejections(const FreshDirectory &directory) {
  StackDocument document(Stack{image()}, "Payload negatives", Json::object(), opted);
  const auto valid = documentJson(document);
  for (const auto &payload : std::vector<std::string>{"%%%", "", fixture_base64({1, 2, 3})}) {
    auto invalid = valid;
    invalid["pixels"] = payload;
    reject([&] { (void)documentFromJson(invalid); }, "Malformed pixel payload accepted");
  }
  for (auto bits : {0x7ff0000000000000ULL, 0xfff0000000000000ULL,
                   0x7ff0000020000000ULL, 0x7ff8000020000001ULL,
                   0x3fb999999999999aULL, 0x483d6329f1c35ca5ULL}) {
    auto values = samples();
    values.back() = bits;
    auto invalid = valid;
    invalid["pixels"] = fixture_base64(little_doubles(values));
    reject([&] { (void)documentFromJson(invalid); }, "Nonquiet/lossy raw payload accepted");
  }
  for (const auto &shape : std::vector<Json>{Json::array({0, 3}), Json::array({2, -1}),
          Json::array({2147483647, 2147483647}), Json::array({2, 3, 2})}) {
    auto invalid = valid;
    invalid["shape"] = shape;
    reject([&] { (void)documentFromJson(invalid); }, "Invalid/oversized shape accepted");
  }
  const auto good = fixture_session(fixture_root(valid));
  std::vector<std::vector<unsigned char>> bad;
  auto truncated = good;
  truncated.pop_back();
  bad.push_back(truncated);
  auto trailing = good;
  trailing.push_back(0);
  bad.push_back(trailing);
  auto wrong_size = good;
  ++wrong_size[13];
  bad.push_back(wrong_size);
  auto zero_size = good;
  std::fill(zero_size.begin() + 10, zero_size.begin() + 14, 0);
  bad.push_back(zero_size);
  for (std::size_t i = 0; i < bad.size(); ++i) {
    const auto file = directory.file("bad-compressed-" + std::to_string(i) + ".ijxx");
    write_fixture(file, bad[i]);
    Json rows = Json::array({"unchanged-results"});
    Json steps = Json::array({"unchanged-workflow"});
    reject([&] { (void)loadSession(file.string(), rows, steps); },
           "Truncated/trailing/misframed compressed session accepted");
    check(rows == Json::array({"unchanged-results"}) &&
              steps == Json::array({"unchanged-workflow"}),
          "Failed session decode mutated caller tables");
  }
  exact(document.frame(), samples());
}

void D08_strict_finite_json_outside_pixels(const FreshDirectory &directory) {
  auto document = std::make_shared<StackDocument>(Stack{image()}, "Finite JSON", Json::object(), opted);
  const auto valid = documentJson(*document);
  std::size_t serial = 0;
  for (double bad : {represented(qnan), std::numeric_limits<double>::infinity(),
                     -std::numeric_limits<double>::infinity()}) {
    for (const auto &key : {"unknown_extension", "display_range", "calibration", "view_state"}) {
      auto invalid = valid;
      invalid[key] = Json{{"nested", Json::array({Json{{"bad", bad}}})}};
      reject([&] { (void)documentFromJson(invalid); }, "Nonfinite numeric JSON document accepted");
    }
    for (bool bad_results : {false, true}) {
      const auto file = directory.file("nonfinite-json-" + std::to_string(serial++) + ".ijxx");
      const auto invalid = Json::array({{{"nested", Json::array({bad})}}});
      reject([&] {
        saveSession(file.string(), {document}, bad_results ? invalid : Json::array(),
                    bad_results ? Json::array() : invalid);
      }, "Session writer silently converted nonfinite JSON to null");
      check(!std::filesystem::exists(file), "Invalid numeric JSON touched output bytes");
    }
  }
  exact(document->frame(), samples());
  check(document->undoSteps() == 0, "Rejected JSON changed native history");
}

void D09_fourier_remains_finite(const FreshDirectory &) {
  StackDocument document(Stack{image()}, "Spectral negatives", Json::object(), opted);
  auto valid = documentJson(document);
  std::vector<std::uint64_t> spectrum(16, 0);
  valid["fft_shape"] = {2, 4};
  valid["spectrum"] = fixture_base64(little_doubles(spectrum));
  auto restored = documentFromJson(valid);
  check(restored->state().spectrum && restored->state().spectrum->spectrum().width == 4 &&
            restored->state().spectrum->spectrum().height == 2 &&
            documentJson(*restored).at("spectrum") == valid.at("spectrum"),
        "Finite Fourier payload failed alongside opted pixel storage");
  exact(restored->frame(), samples());
  for (auto bits : {qnan, std::uint64_t(0x7ff0000000000000ULL),
                   std::uint64_t(0x7ff0000020000000ULL)}) {
    auto invalid = valid;
    auto bad = spectrum;
    bad.front() = bits;
    invalid["spectrum"] = fixture_base64(little_doubles(bad));
    reject([&] { (void)documentFromJson(invalid); }, "Pixel policy leaked into finite Fourier decoding");
  }
  auto oversized = valid;
  oversized["spectrum_shape"] = {9, 9};
  reject([&] { (void)documentFromJson(oversized); }, "Invalid original FFT extents accepted");
}

std::vector<std::uint64_t> display_samples() {
  return {0xc040000000000000ULL, qnan, 0, 0x4030000000000000ULL,
          0x4040000000000000ULL, 0x4050000000000000ULL}; // -32, NaN, 0,16,32,64.
}
Image display_source() {
  Image result(3, 2, 1, PixelType::Float32);
  const auto bits = display_samples();
  for (std::size_t i = 0; i < bits.size(); ++i)
    result.pixels[i] = represented(bits[i]);
  return result;
}
void rgb(const Image &actual, const std::vector<std::array<int, 3>> &expected,
         int width = 3, int height = 2) {
  check(actual.width == width && actual.height == height && actual.channels == 3 &&
            actual.type == PixelType::UInt8 && actual.pixels.size() == expected.size() * 3,
        "Actual CPU rendered RGB layout changed");
  for (std::size_t i = 0; i < expected.size(); ++i)
    for (int channel = 0; channel < 3; ++channel)
      check(actual.pixels[i * 3 + channel] == expected[i][channel],
            "CPU rendered RGB differs from independent owned display expectation");
}
std::vector<std::array<int, 3>> grayscale(const std::vector<int> &bytes) {
  std::vector<std::array<int, 3>> result;
  for (int byte : bytes)
    result.push_back({byte, byte, byte});
  return result;
}
Json custom_lut() {
  Json result = Json::array();
  for (int i = 0; i < 256; ++i)
    result.push_back({i, (3 * i) % 256, 255 - i});
  return result;
}
void D10_explicit_range_and_lut_zero(const FreshDirectory &) {
  const auto source = display_source();
  const Json metadata{{"display_range", {-32, 64}}};
  const std::vector<int> indices{0, 0, 85, 128, 170, 255};
  rgb(displayImage(source, metadata, opted), grayscale(indices));
  rgb(renderImage(source, metadata, false, opted), grayscale(indices));
  auto custom = metadata;
  custom["custom_lut"] = custom_lut();
  std::vector<std::array<int, 3>> expected;
  for (int index : indices)
    expected.push_back({index, (3 * index) % 256, 255 - index});
  rgb(displayImage(source, custom, opted), expected);
  auto labels = metadata;
  labels["labels"] = true;
  const auto colored = displayImage(source, labels, opted);
  for (int c = 0; c < 3; ++c) {
    check(colored.at(1, 0, c) == (c == 0 ? 255 : 0),
          "Admitted NaN did not select actual label/Spectrum LUT0");
    check(colored.at(2, 0, c) == 0, "Finite label-zero background behavior changed");
  }
  exact(source, display_samples());
}

void D11_automatic_range_ignores_nan(const FreshDirectory &) {
  const auto source = display_source();
  check(finiteDisplayRange(source, opted) == std::array<double, 2>{-32, 64},
        "Automatic range did not ignore quiet NaN and retain finite neighbors");
  rgb(displayImage(source, Json::object(), opted), grayscale({0, 0, 85, 128, 170, 255}));
  auto constant = source;
  for (auto &sample : constant.pixels)
    sample = 42;
  constant.pixels[1] = represented(qnan);
  check(finiteDisplayRange(constant, opted) == std::array<double, 2>{42, 43},
        "Constant finite-neighbor display range changed");
  rgb(displayImage(constant, Json::object(), opted), grayscale({0, 0, 0, 0, 0, 0}));
  exact(source, display_samples());
  check(raw_bits(constant.pixels[1]) == qnan, "Auto range mutated original NaN payload");
}

void D12_all_nan_owned_fallback(const FreshDirectory &) {
  Image source(3, 2, 1, PixelType::Float32);
  std::vector<std::uint64_t> expected{qnan, negative_qnan, qnan, negative_qnan, qnan, negative_qnan};
  for (std::size_t i = 0; i < expected.size(); ++i)
    source.pixels[i] = represented(expected[i]);
  check(finiteDisplayRange(source, opted) == std::array<double, 2>{0, 1},
        "Owned all-quiet-NaN fallback changed from [0,1]");
  rgb(displayImage(source, Json::object(), opted), grayscale({0, 0, 0, 0, 0, 0}));
  rgb(displayImage(source, {{"lut", "Inverted"}}, opted), grayscale({255, 255, 255, 255, 255, 255}));
  rgb(displayImage(source, {{"custom_lut", custom_lut()}}, opted),
      std::vector<std::array<int, 3>>(6, {0, 0, 255}));
  rgb(displayImage(source, {{"lut", "Spectrum"}}, opted),
      std::vector<std::array<int, 3>>(6, {255, 0, 0}));
  exact(source, expected);
  // This is the explicit project-owned fallback, not ImageJ NaN-display parity.
}

void D13_xy_xz_yz_cpu_projection(const FreshDirectory &) {
  const auto first = display_source();
  Image second(3, 2, 1, PixelType::Float32);
  const std::vector<std::uint64_t> second_bits{0xc030000000000000ULL, 0, negative_qnan,
      0x4040000000000000ULL, 0x4050000000000000ULL, 0x4030000000000000ULL};
  for (std::size_t i = 0; i < second_bits.size(); ++i)
    second.pixels[i] = represented(second_bits[i]);
  StackDocument source(Stack{first, second}, "XYZ source", Json::object(), opted);
  const auto policy = source.nativeSnapshot().data().dataset.sample_policy();
  const Json range{{"display_range", {-32, 64}}};
  const auto xy = source.plane(Plane::XY, 0);
  const auto xz = source.plane(Plane::XZ, 0);
  const auto yz = source.plane(Plane::YZ, 1);
  exact(xy, display_samples());
  exact(xz, {0xc040000000000000ULL, qnan, 0, 0xc030000000000000ULL, 0, negative_qnan});
  exact(yz, {qnan, 0x4040000000000000ULL, 0, 0x4050000000000000ULL}, 2, 2);
  rgb(displayImage(xy, range, policy), grayscale({0, 0, 85, 128, 170, 255}));
  rgb(displayImage(xz, range, policy), grayscale({0, 0, 85, 43, 85, 0}));
  rgb(displayImage(yz, range, policy), grayscale({0, 170, 85, 255}), 2, 2);
  check(finiteDisplayRange(xz, policy) == std::array<double, 2>{-32, 0} &&
            finiteDisplayRange(yz, policy) == std::array<double, 2>{0, 64},
        "Orthogonal extracted-plane range did not ignore quiet NaNs");
  exact(source.state().frames[0], display_samples());
  exact(source.state().frames[1], second_bits);
  check(source.undoSteps() == 0, "CPU display allocated native history");
}

void D14_display_preflight_and_finite_defaults(const FreshDirectory &) {
  const auto source = display_source();
  reject([&] { (void)displayImage(source, {{"display_range", {-32, 64}}}); },
         "Legacy finite-only display wrapper admitted quiet-NaN source");
  for (auto bits : {std::uint64_t(0x7ff0000000000000ULL),
                   std::uint64_t(0x7ff0000020000000ULL), std::uint64_t(0x7ff8000020000001ULL)}) {
    auto invalid = source;
    invalid.pixels[1] = represented(bits);
    reject([&] { (void)displayImage(invalid, {{"display_range", {-32, 64}}}, opted); },
           "Opted display admitted invalid raw sample bits");
    reject([&] { (void)finiteDisplayRange(invalid, opted); },
           "Opted range admitted invalid raw sample bits");
    check(raw_bits(invalid.pixels[1]) == bits, "Rejected projection mutated original raw bits");
  }
  auto wrong_dtype = source;
  wrong_dtype.type = PixelType::Float64;
  reject([&] { (void)displayImage(wrong_dtype, Json::object(), opted); },
         "Display policy admitted a different declared dtype");
  reject([&] { (void)displayImage(source, Json::object(), static_cast<FloatingSamplePolicy>(73)); },
         "Display accepted an unknown source policy");
  for (auto type : {PixelType::UInt8, PixelType::UInt16, PixelType::Float32, PixelType::Float64}) {
    Image pixels(3, 2, 1, type);
    pixels.pixels = {0, 1, 2, 0, 1, 2};
    const Json range{{"display_range", {0, 2}}};
    const auto expected = grayscale({0, 128, 255, 0, 128, 255});
    rgb(displayImage(pixels, range), expected);
    rgb(displayImage(pixels, range, finite), expected);
    rgb(renderImage(pixels, range, false), expected);
    check(pixels.pixels == std::vector<double>({0, 1, 2, 0, 1, 2}),
          "Legacy finite CPU display mutated input");
  }
  exact(source, display_samples());
}
void D15_equal_typed_ranges(const FreshDirectory &) {
  const auto path = std::filesystem::path(__FILE__).parent_path().parent_path() / "fixtures/equal-range-imagej154r.json";
  std::ifstream file(path);check(bool(file),"Equal-range reference missing");
  const auto fixture = Json::parse(file);check(fixture.at("cases").size()==8,"Equal-range matrix changed");
  for (const auto &row : fixture.at("cases")) {
    const auto type = row.at("dtype")=="uint16" ? PixelType::UInt16 : PixelType::Float32;
    Image image(8,1,1,type);
    for(std::size_t i=0;i<8;++i) image.pixels[i]=type==PixelType::Float32
        ? double(static_cast<float>(row.at("input").at(i).get<double>())) : row.at("input").at(i).get<double>();
    const auto saved=image.pixels;const double cutoff=row.at("requested");
    const Json metadata{{"display_range",{cutoff,cutoff}}};
    const auto actual=displayImage(image,metadata);
    for(std::size_t i=0;i<8;++i)for(int c=0;c<3;++c)
      check(actual.pixels[i*3+c]==row.at("display").at(i).get<double>(),"Equal typed display differs from processor readback");
    check(std::memcmp(saved.data(),image.pixels.data(),saved.size()*sizeof(double))==0,"Display rewrote source pixels");
  }
}
struct Test {
  const char *name;
  void (*run)(const FreshDirectory &);
};
constexpr std::array<Test, 15> tests{{
    {"NAN_DOC_D15_equal_typed_ranges", D15_equal_typed_ranges},
    {"NAN_DOC_D01_admission_and_inherited_owners", D01_admission_and_inherited_owners},
    {"NAN_DOC_D02_state_edits_undo_redo", D02_state_edits_undo_redo},
    {"NAN_DOC_D03_v2_json_policy_and_exact_payload", D03_v2_json_policy_and_exact_payload},
    {"NAN_DOC_D04_binary_session_save_reopen", D04_binary_session_save_reopen},
    {"NAN_DOC_D05_absent_token_finite_compatibility", D05_absent_token_finite_compatibility},
    {"NAN_DOC_D06_policy_token_storage_rejections", D06_policy_token_storage_rejections},
    {"NAN_DOC_D07_raw_and_compressed_payload_rejections", D07_raw_and_compressed_payload_rejections},
    {"NAN_DOC_D08_strict_finite_json_outside_pixels", D08_strict_finite_json_outside_pixels},
    {"NAN_DOC_D09_fourier_remains_finite", D09_fourier_remains_finite},
    {"NAN_DOC_D10_explicit_range_and_lut_zero", D10_explicit_range_and_lut_zero},
    {"NAN_DOC_D11_automatic_range_ignores_nan", D11_automatic_range_ignores_nan},
    {"NAN_DOC_D12_all_nan_owned_fallback", D12_all_nan_owned_fallback},
    {"NAN_DOC_D13_xy_xz_yz_cpu_projection", D13_xy_xz_yz_cpu_projection},
    {"NAN_DOC_D14_display_preflight_and_finite_defaults", D14_display_preflight_and_finite_defaults},
}};
} // namespace

int main(int argc, char **argv) {
  try {
    check(argc == 2, "Supply a new native-test artifact directory");
    const FreshDirectory directory(argv[1]);
    for (const auto &test : tests) {
      try {
        test.run(directory);
        std::cout << "PASS " << test.name << '\n';
      } catch (const std::exception &error) {
        std::cerr << "FAIL " << test.name << ": " << error.what() << '\n';
        return 1;
      }
    }
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "FAIL NAN_DOC_entry: " << error.what() << '\n';
    return 1;
  }
}
