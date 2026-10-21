// Fixed registered-workload observations in Window::run's normal GL loop.
// This file observes actual work; independent ImageJ files supply expectations.
#include "rank_runtime_probe.hpp"
#include "window.hpp"
#include <imagejxx/channel_display.hpp>
#include <imgui.h>
#include <imgui_internal.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <utility>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <unistd.h>
#endif

namespace imagejxx::desktop {
namespace {
constexpr std::uint64_t second = 1000000000ULL;
constexpr std::size_t artifactLimit = 64U * 1024U * 1024U;
constexpr std::size_t reportLimit = 16U * 1024U * 1024U;
constexpr std::size_t eventLimit = 4096;
constexpr std::uint64_t frameLimit = 100000;
constexpr const char *fixturePath =
    "build/p06f-001/benchmark-observer-prep001/fixture001/fixtures.json";
constexpr const char *scalarPath =
    "build/references/rank-benchmark-imagej154r-001/expectations.json";
constexpr const char *rgbPath =
    "build/references/rank-benchmark-display-imagej154r-001/rgb-expectations.json";

std::uint64_t currentProcessId() {
#ifdef _WIN32
  return static_cast<std::uint64_t>(GetCurrentProcessId());
#else
  return static_cast<std::uint64_t>(getpid());
#endif
}

void require(bool condition, const std::string &message) {
  if (!condition) throw std::runtime_error(message);
}
std::string hex(const std::vector<unsigned char> &bytes) {
  constexpr char digits[] = "0123456789abcdef";
  std::string result;
  result.reserve(bytes.size() * 2);
  for (const auto value : bytes) {
    result += digits[value >> 4]; result += digits[value & 15];
  }
  return result;
}
std::vector<unsigned char> unhex(const std::string &value) {
  require(value.size() % 2 == 0 && value.size() <= 4U * 1024U * 1024U,
          "Invalid or oversized rank view hex");
  const auto digit = [](char c) -> unsigned {
    if (c >= '0' && c <= '9') return unsigned(c - '0');
    if (c >= 'a' && c <= 'f') return unsigned(c - 'a' + 10);
    throw std::runtime_error("Noncanonical rank view hex");
  };
  std::vector<unsigned char> result;
  result.reserve(value.size() / 2);
  for (std::size_t i = 0; i < value.size(); i += 2)
    result.push_back(static_cast<unsigned char>((digit(value[i]) << 4) | digit(value[i + 1])));
  return result;
}
std::vector<unsigned char> fileBytes(const std::filesystem::path &path,
                                     std::size_t maximum = reportLimit) {
  const auto size = std::filesystem::file_size(path);
  require(size <= maximum, "Rank probe input exceeds its file bound: " + path.u8string());
  std::ifstream input(path, std::ios::binary);
  require(bool(input), "Cannot open rank probe input: " + path.u8string());
  std::vector<unsigned char> result(static_cast<std::size_t>(size));
  if (!result.empty()) input.read(reinterpret_cast<char *>(result.data()),
                                  static_cast<std::streamsize>(result.size()));
  require(bool(input) && input.peek() == std::char_traits<char>::eof(),
          "Rank probe input changed or could not be read: " + path.u8string());
  return result;
}
Json pinnedJson(const char *path, const char *hash, Json &pins) {
  const auto bytes = fileBytes(std::filesystem::u8path(path));
  const auto actual = rankRuntimeSha256(bytes);
  require(actual == hash, std::string("Rank probe pinned manifest mismatch: ") + path);
  pins.push_back({{"path", path}, {"bytes", bytes.size()}, {"sha256", actual}});
  return Json::parse(bytes.begin(), bytes.end());
}
std::vector<unsigned char> pinnedBytes(const Json &pin, Json &pins) {
  const auto path = pin.at("path").get<std::string>();
  auto bytes = fileBytes(std::filesystem::u8path(path), 4U * 1024U * 1024U);
  const auto actual = rankRuntimeSha256(bytes);
  require(bytes.size() == pin.at("bytes").get<std::size_t>() &&
              actual == pin.at("sha256").get<std::string>(),
          "Rank probe fixture payload mismatch: " + path);
  pins.push_back({{"path", path}, {"bytes", bytes.size()}, {"sha256", actual}});
  return bytes;
}
Json select(const Json &rows, const std::string &dtype, const std::string &method) {
  Json result;
  for (const auto &row : rows)
    if (row.at("dtype") == dtype && row.at("method") == method) {
      require(result.is_null(), "Duplicate rank reference cell"); result = row;
    }
  require(!result.is_null(), "Missing rank independent reference cell");
  return result;
}
RankFilterMethod methodValue(const std::string &method) {
  if (method == "minimum") return RankFilterMethod::Minimum;
  if (method == "maximum") return RankFilterMethod::Maximum;
  if (method == "median") return RankFilterMethod::Median;
  if (method == "mean") return RankFilterMethod::Mean;
  if (method == "variance") return RankFilterMethod::Variance;
  throw std::invalid_argument("Unknown ImageJ rank runtime method");
}
PixelType pixelValue(const std::string &dtype) {
  if (dtype == "uint8") return PixelType::UInt8;
  if (dtype == "uint16") return PixelType::UInt16;
  if (dtype == "float32") return PixelType::Float32;
  throw std::invalid_argument("Rank runtime dtype must be uint8/uint16/float32");
}
std::size_t sampleBytes(PixelType type) {
  return type == PixelType::UInt8 ? 1 : type == PixelType::UInt16 ? 2 : 4;
}
std::uint32_t word(const unsigned char *data, std::size_t size) {
  std::uint32_t result = 0;
  for (std::size_t i = 0; i < size; ++i) result |= std::uint32_t(data[i]) << (8 * i);
  return result;
}
void appendDeclared(std::vector<unsigned char> &bytes, const Image &image) {
  const auto width = sampleBytes(image.type);
  for (const auto sample : image.pixels) {
    require(std::isfinite(sample), "Nonfinite actual rank output");
    std::uint32_t value = 0;
    if (image.type == PixelType::Float32) {
      const auto typed = static_cast<float>(sample);
      std::memcpy(&value, &typed, sizeof(value));
    } else {
      require(sample >= 0 && sample <= (width == 1 ? 255 : 65535) &&
                  std::floor(sample) == sample, "Noncanonical integer rank output");
      value = static_cast<std::uint32_t>(sample);
    }
    for (std::size_t i = 0; i < width; ++i)
      bytes.push_back(static_cast<unsigned char>(value >> (8 * i)));
  }
}
Json stableDocument(const Json &document) {
  Json result = Json::object();
  for (const auto *key : {"id", "dtype", "axis_sizes", "shape", "current_tcz",
                          "data_identity", "revisions", "dirty", "undo_steps",
                          "redo_steps", "selection", "selection_sha256", "display",
                          "view", "calibration"})
    if (document.contains(key)) result[key] = document.at(key);
  return result;
}
const Json &lane(const Json &state, const std::string &role) {
  return state.at("gaussian_jobs").at(role);
}
const Json &usage(const Json &state) {
  return state.at("gaussian_jobs").at("resource_usage");
}
bool resourceZero(const Json &state) {
  const auto &u = usage(state);
  return u.at("running").get<std::uint64_t>() == 0 &&
      u.at("queued").get<std::uint64_t>() == 0 &&
      u.at("running_reserved_bytes").get<std::uint64_t>() == 0 &&
      u.at("retained_reserved_bytes").get<std::uint64_t>() == 0 &&
      u.at("reserved_bytes").get<std::uint64_t>() == 0;
}
bool laneReleased(const Json &state) {
  const auto &jobs = state.at("gaussian_jobs");
  return !jobs.at("preview").at("owns_job").get<bool>() &&
      !jobs.at("apply").at("owns_job").get<bool>() &&
      jobs.at("active_installed_generation").is_null() && resourceZero(state);
}
bool terminal(const Json &job) {
  if (!job.is_object()) return false;
  const auto state = job.value("state", "");
  return state == "completed" || state == "failed" || state == "canceled";
}
void exclusiveJson(const std::filesystem::path &path, const Json &value) {
  const auto bytes = value.dump(2) + "\n";
  require(bytes.size() <= reportLimit && bytes.size() <= artifactLimit,
          "Rank runtime report exceeds retained artifact cap");
  auto *file = std::fopen(path.u8string().c_str(), "wbx");
  require(file != nullptr, "Rank runtime output already exists or cannot be created");
  const auto written = std::fwrite(bytes.data(), 1, bytes.size(), file);
  const auto flushed = std::fflush(file);
  const auto closed = std::fclose(file);
  require(written == bytes.size() && flushed == 0 && closed == 0,
          "Rank runtime report could not be completely written");
}
} // namespace

std::uint64_t rankRuntimeSteadyNanoseconds() {
  return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
      std::chrono::steady_clock::now().time_since_epoch()).count());
}

RankRuntimeClockStamp rankRuntimeClockStamp() {
  RankRuntimeClockStamp result;
#ifdef _WIN32
  static const auto frequency = [] {
    LARGE_INTEGER value{};
    require(QueryPerformanceFrequency(&value) && value.QuadPart > 0,
            "Native rank QPC frequency is unavailable or nonpositive");
    return static_cast<std::uint64_t>(value.QuadPart);
  }();
  LARGE_INTEGER before{}, after{};
  const auto beforeOk = QueryPerformanceCounter(&before);
  result.steady_ns = rankRuntimeSteadyNanoseconds();
  const auto afterOk = QueryPerformanceCounter(&after);
  require(beforeOk && afterOk && before.QuadPart >= 0 && after.QuadPart >= before.QuadPart,
          "Native rank QPC observation failed or regressed");
  result.qpc_before = static_cast<std::uint64_t>(before.QuadPart);
  result.qpc_after = static_cast<std::uint64_t>(after.QuadPart);
  result.qpc_frequency = frequency;
#else
  // This carrier does not qualify a POSIX external-memory clock bridge.
  result.steady_ns = rankRuntimeSteadyNanoseconds();
#endif
  return result;
}

Json rankRuntimeClockJson(const RankRuntimeClockStamp &stamp) {
  return {{"steady_ns",stamp.steady_ns},{"qpc_before",stamp.qpc_before},
      {"qpc_after",stamp.qpc_after},{"qpc_frequency",stamp.qpc_frequency}};
}

// Identity only. Copied from the owned rank observation carrier; never a filter.
std::string rankRuntimeSha256(const std::vector<unsigned char> &input) {
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
  const auto rotate = [](std::uint32_t v, int n) { return (v >> n) | (v << (32 - n)); };
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
  for (const auto value : h) out << std::setw(8) << value;
  return out.str();
}

struct RankRuntimeProbe::Impl {
  enum class Stage { Prepare, Settle, Range, Baseline, Start, Await, Cancel,
                     Cleanup, Done };
  struct Spec {
    std::string endpoint, role, state, phase;
    int sequence = 1, attempt = 1;
  };
  struct Gesture {
    bool active = false, timed = false;
    std::string selector;
    Json control;
    float x = 0, y = 0;
    int step = 0;
  };
  struct TitleDrag {
    bool active = false, attempted = false, prepared = false, held = false;
    int step = 0;
    Json start;
    float x = 0, y = 0, targetY = 0, deltaY = 0;
  };
  Window &window;
  const std::filesystem::path directory;
  std::string context, mode, dtype, method, cell;
  PixelType type;
  RankFilterMethod rankMethod;
  Json report, fixture, expected, expectedRgb, originalRgb, last, trial;
  std::vector<unsigned char> source, roi;
  std::vector<Spec> specifications;
  std::size_t specIndex = 0;
  std::uint64_t begun = rankRuntimeSteadyNanoseconds(), frame = 0;
  std::uint64_t sourceId = 0, trialBegun = 0, cleanupBegun = 0;
  std::uint64_t releaseNs = 0, submittedJob = 0, submittedGeneration = 0;
  std::uint64_t inputEventStart = 0;
  std::uint64_t lastInstallNs = 0;
  std::uint64_t stateObservedNs = 0;
  RankRuntimeClockStamp inputClockBefore, inputClockAfter, stateObservedClock;
  Stage stage = Stage::Prepare;
  Gesture gesture;
  TitleDrag titleDrag;
  Json lastDisplaySize;
  int rangeAxis = 0;
  bool stopAfterCleanup = false, finishedReport = false, capturedView = false, jobsShown = false;
  std::string observerFailure;

  Impl(Window &owner, const Json &plan, std::string launchContext,
       std::filesystem::path output)
      : window(owner), directory(std::move(output)), context(std::move(launchContext)),
        mode(plan.value("mode", "feasibility")), dtype(plan.value("dtype", "uint8")),
        method(plan.value("method", "minimum")), type(pixelValue(dtype)),
        rankMethod(methodValue(method)) {
    require(plan.is_object(), "Rank runtime plan must be an object");
    require(plan.value("schema", 1) == 1, "Unknown rank runtime plan schema");
    require(context == "imgui_cpp" || context == "imgui_python", "Invalid rank runtime context");
    require(mode == "feasibility" || mode == "series", "Unknown rank runtime mode");
    require(mode != "feasibility" || (dtype == "uint8" && method == "minimum"),
            "Feasibility is fixed to UInt8 minimum");
    require(!plan.contains("radius") || plan.at("radius") == 2.0,
            "Rank runtime radius is fixed at2");
    for (const auto &item : plan.items())
      require(item.key() == "schema" || item.key() == "mode" || item.key() == "dtype" ||
                  item.key() == "method" || item.key() == "radius",
              "Unknown rank runtime plan field: " + item.key());
    require(window.documents().empty() && !window.busy(),
            "Rank runtime requires an empty fresh Window");
    cell = "C.G042.rank-imagej.processing-editor.001." + context + "." + dtype + "." + method;
    const auto setupClock = rankRuntimeClockStamp();
    report = {{"schema",1},{"producer_id","p06f.rank.normal-loop.raw.001"},
        {"context",context},{"process_id",currentProcessId()},
        {"qpc_frequency",setupClock.qpc_frequency},{"qpc_supported",setupClock.qpc_frequency != 0},
        {"memory_clock_status",setupClock.qpc_frequency ? "native QPC brackets retained; no steady/QPC epoch assumption"
                                                       : "unsupported POSIX QPC bridge; no memory performance claim"},
        {"mode",mode},{"cell_id",cell},{"acceptance",false},
        {"clock","steady_clock nanoseconds, one process, no wall-clock subtraction"},
        {"render_scope","CPU ImGui OpenGL draw submission return only; no GPU completion, presentation, OS input or platform credit"},
        {"budgets_status","proposed and unratified; no timing-pass claim"},
        {"reservation_metric","persisted actual ticket ledger charge peak; declared processing reservation, not allocation/private-byte peak"},
        {"external_obligations",{"matching installed qualification and toolchain pins",
            "actual CPU/OS/RAM/power/load/process-tree observations",
            "external sampled product private-byte series and peak delta"}},
        {"caps",{{"cell_seconds",30},{"preview_seconds",5},{"apply_seconds",15},
            {"cancel_seconds",5},{"cleanup_seconds",10},{"fresh_artifact_bytes",artifactLimit},
            {"report_bytes",reportLimit},{"max_frames",frameLimit},{"live_product_processes",1}}},
        {"source_pins",Json::array()},{"preparation",Json::array()},
        {"events",Json::array()},{"inputs",Json::array()},{"trials",Json::array()},
        {"missing_series",Json::array()},{"completion","running"}};
    auto &pins = report["source_pins"];
    pinnedJson("build/p06f-001/benchmark-observer-prep001/proposal.json",
        "5927a30c20fe8858d236c9b0924c461920149d4e82b4e8ea36f3b4bef5e99ee7", pins);
    pinnedBytes({{"path","build/p06f-001/benchmark-observer-prep001/OBSERVER-PROPOSAL.md"},
        {"bytes",16363},{"sha256","97c7d23664a033ef82fcc68fbf2ba109cd3c8b038f59748cc19677a5130695e2"}},pins);
    fixture = pinnedJson(fixturePath,
        "0ea7a9dd3344ee0e7a3d619ac150a783599130f1c3e6747ea21e135ea634f3a3", pins);
    const auto references = pinnedJson(scalarPath,
        "d1a8144cd1dc035eaba437328aa68f32a3cf696142ce313e920e21f9179bd311", pins);
    const auto colors = pinnedJson(rgbPath,
        "b9d00f9e9bff9ec9e3658102cc8a706d54a13fb43d1f32be4e2029c1a2e0cf8e", pins);
    expected = select(references.at("cells"), dtype, method);
    expectedRgb = select(colors.at("carriers"), dtype, method);
    originalRgb = select(colors.at("carriers"), dtype, "original");
    require(expected.at("shape") == Json::array({2,3,4,128,192,1}) &&
        expected.at("planes").at(18).at("t") == 1 &&
        expected.at("planes").at(18).at("c") == 1 &&
        expected.at("planes").at(18).at("z") == 2 &&
        expected.at("preview_masked_sha256") == expectedRgb.at("active_plane_sha256"),
        "Rank independent expectation coordinate/encoding mismatch");
    Json sourcePin;
    const auto declaredType = dtype == "uint8" ? "UInt8" : dtype == "uint16" ? "UInt16" : "Float32";
    for (const auto &pin : fixture.at("sources"))
      if (pin.at("dtype") == declaredType) sourcePin = pin;
    require(sourcePin.is_object() && sourcePin.at("shape") == Json::array({2,3,4,128,192,1}) &&
        sourcePin.at("byte_order") == "little", "Rank fixed fixture shape/type mismatch");
    source = pinnedBytes(sourcePin, pins);
    roi = pinnedBytes(fixture.at("roi"), pins);
    require(source.size() == 24U * 128U * 192U * sampleBytes(type) && roi.size() == 3072,
            "Rank fixed fixture payload size mismatch");
    std::size_t members = 0;
    for (int y = 0; y < 128; ++y) for (int x = 0; x < 192; ++x) {
      const auto index = std::size_t(y * 192 + x);
      const bool actual = (roi[index / 8] & (1U << (7 - index % 8))) != 0;
      const bool declared = x >= 16 && x < 176 && y >= 12 && y < 116 &&
          !(x >= 64 && x < 96 && y >= 40 && y < 72);
      require(actual == declared, "Rank fixed ROI membership mismatch"); members += actual;
    }
    require(members == 15616, "Rank fixed ROI write count mismatch");
    report["fixture"] = {{"input",sourcePin},{"roi",fixture.at("roi")},
        {"target",fixture.at("target")},{"calibration",fixture.at("fixture_spec").at("calibration")},
        {"active_tcz",{1,1,2}},{"display",colors.at("display_contract")},
        {"independent_scalar",{{"id",expected.at("id")},{"full",expected.at("masked_output")},
            {"active_sha256",expected.at("preview_masked_sha256")}}},
        {"independent_rgb",{{"id",expectedRgb.at("id")},{"rgb",expectedRgb.at("rgb")}}}};
    window.updateSettings({{"processing_workers",1},{"processing_memory_mb",128},
                           {"history_limit",8},{"history_memory_mb",128}}, false);
    addSpecs();
  }

  void addSpecs() {
    const std::array<Spec,5> series{{{"preview","preview","not_submitted","cold"},
        {"apply","apply","not_submitted","cold"},
        {"cancel","apply","queued","cold"},
        {"cancel","apply","running","cold"},
        {"cancel","preview","ready","cold"}}};
    for (const auto &base : series) {
      if (mode == "feasibility") { specifications.push_back(base); continue; }
      specifications.push_back(base);
      for (const auto &phase : {std::string("warmup"),std::string("measured")})
        for (int number = 1; number <= (phase == "warmup" ? 2 : 10); ++number) {
          auto spec = base; spec.phase = phase; spec.sequence = number;
          specifications.push_back(std::move(spec));
        }
    }
  }
  const Spec &spec() const { return specifications.at(specIndex); }
  Json inspect(bool bytes = false) { return RankRuntimeProbeAccess::inspect(window, bytes); }
  std::string trialId() const {
    const auto &s = spec();
    return cell + "." + s.endpoint + "." + s.role + "." + s.state + "." + s.phase +
        "." + std::to_string(s.sequence) + ".attempt" + std::to_string(s.attempt);
  }
  void beginTrial() {
    trialBegun = rankRuntimeSteadyNanoseconds();
    releaseNs = submittedJob = submittedGeneration = lastInstallNs = 0;
    inputClockBefore = {}; inputClockAfter = {}; stateObservedClock = {};
    inputEventStart = report.at("events").size(); rangeAxis = 0; gesture = {}; titleDrag = {};
    jobsShown = false;
    trial = {{"id",trialId()},{"endpoint",spec().endpoint},{"role",spec().role},
        {"required_initial_state",spec().state},{"phase",spec().phase},
        {"sequence",spec().sequence},{"attempt",spec().attempt},
        {"status","preparing"},{"eligible",false},{"acceptance",false},
        {"timing_scope","final actual mouse-release injection to CPU render submission / observed terminal-resource state"},
        {"begun_ns",trialBegun}};
  }
  void prepare() {
    beginTrial();
    require(resourceZero(last), "Rank previous trial resources did not reach zero");
    require(window.documents().empty(), "Rank previous trial source remains open");
    Stack planes;
    planes.reserve(24);
    const auto width = sampleBytes(type);
    std::size_t offset = 0;
    for (int plane = 0; plane < 24; ++plane) {
      Image image(192,128,1,type);
      for (auto &sample : image.pixels) {
        const auto bits = word(source.data() + offset, width); offset += width;
        if (type == PixelType::Float32) {
          float value; std::memcpy(&value, &bits, sizeof(value));
          require(std::isfinite(value), "Nonfinite pinned rank input"); sample = value;
        } else sample = bits;
      }
      planes.push_back(std::move(image));
    }
    SpatialCalibration calibration;
    calibration.spacing = {.5,1.25,2.5}; calibration.origin = {11,-7,19};
    calibration.unit = "um"; calibration.frame_interval = .75; calibration.time_origin = 2.25;
    DocumentData data{Dataset::from_planes({2,3,4,128,192,1},std::move(planes),calibration)};
    data.metadata.title = "Owned rank benchmark " + dtype + " " + trialId();
    data.selection = Selection::from_packed_mask(192,128,roi);
    auto owner = std::make_shared<imagejxx::Document>(std::move(data));
    sourceId = owner->id;
    window.addNativeDocument(owner);
    auto display = owner->snapshot().display();
    display.t = 1; display.c = 1; display.crosshair = {96,64,2};
    display.orthogonal = false;
    ChannelDisplayState channels; channels.mode = ChannelDisplayMode::Grayscale;
    const std::array<double,2> range = dtype == "uint8" ? std::array<double,2>{0,250} :
        dtype == "uint16" ? std::array<double,2>{1000,65250} : std::array<double,2>{-15.625,15.625};
    for (int c = 0; c < 3; ++c) channels.channels.push_back({true,range,{"Grays",{}}});
    display = with_channel_display(std::move(display),owner->snapshot().data().dataset,channels);
    window.setNativeDisplay(std::move(display),sourceId);
    owner->mark_saved();
    window.recording = true; window.workflow = Json::array(); window.results = Json::array();
    window.clearError();
    RankRuntimeProbeAccess::openRank(window,rankMethod);
    trial["source_id"] = sourceId;
    report["preparation"].push_back({{"trial_id",trialId()},{"source_id",sourceId},
        {"kind","untimed explicit API fixture/admission/display/history/settings/recording/openRank"},
        {"steady_ns",rankRuntimeSteadyNanoseconds()}});
    stage = Stage::Settle;
  }

  Json control(const std::string &selector) const {
    if (selector.rfind("jobs.cancel.",0) == 0) {
      const auto id = std::stoull(selector.substr(12));
      for (const auto &job : last.at("jobs_panel").at("jobs"))
        if (job.at("id").get<std::uint64_t>() == id) return job.at("cancel");
      return Json();
    }
    const auto dot = selector.find('.');
    const auto &panel = selector.rfind("target.",0) == 0
        ? last.at("rank_panel").at("operation_target") : last.at("rank_panel");
    const auto name = selector.substr(dot + 1);
    if (!panel.at("control_states").contains(name)) return Json();
    auto result = panel.at("control_states").at(name);
    result["point"] = panel.at("controls").at(name);
    result["rect"] = panel.at("control_rects").at(name);
    return result;
  }
  bool admittedControl(const Json &item, float x, float y) const {
    if (!item.is_object() || !item.value("enabled",false) ||
        !item.contains("id") || item.at("id").get<std::uint64_t>() == 0 ||
        !item.contains("rect") || !item.at("rect").is_array() || item.at("rect").size() != 4)
      return false;
    const auto &r = item.at("rect");
    const auto &io = ImGui::GetIO();
    return std::isfinite(x) && std::isfinite(y) && x >= 0 && y >= 0 &&
        x < io.DisplaySize.x && y < io.DisplaySize.y &&
        x >= r[0].get<float>() && y >= r[1].get<float>() &&
        x < r[2].get<float>() && y < r[3].get<float>();
  }
  bool startGesture(const std::string &selector, bool timed) {
    const auto item = control(selector);
    if (!item.is_object() || !item.contains("point") || item.at("point").size() != 2) return false;
    const auto x = std::floor(item.at("point")[0].get<float>());
    const auto y = std::floor(item.at("point")[1].get<float>());
    if (!admittedControl(item,x,y)) return false;
    gesture = {true,timed,selector,item,x,y,0};
    return true;
  }
  bool rectNear(const Json &actual, const Json &before, float deltaY = 0) const {
    if (!actual.is_array() || !before.is_array() || actual.size() != 4 || before.size() != 4)
      return false;
    for (std::size_t i = 0; i < 4; ++i) {
      const auto wanted = before[i].get<float>() + (i % 2 ? deltaY : 0);
      if (!std::isfinite(actual[i].get<float>()) || std::abs(actual[i].get<float>() - wanted) > 1.f)
        return false;
    }
    return true;
  }
  bool controlInsideWork(const std::string &selector, const Json &work) const {
    const auto item = control(selector);
    if (!item.is_object() || !item.contains("point") || !item.contains("rect") ||
        !admittedControl(item,item.at("point")[0].get<float>(),item.at("point")[1].get<float>()))
      return false;
    const auto &rect = item.at("rect");
    return rect[0].get<float>() >= work[0].get<float>() &&
        rect[1].get<float>() >= work[1].get<float>() &&
        rect[2].get<float>() <= work[2].get<float>() &&
        rect[3].get<float>() <= work[3].get<float>();
  }
  Json translatedRect(const Json &rect, float deltaY) const {
    return Json::array({rect[0],rect[1].get<float>() + deltaY,
                        rect[2],rect[3].get<float>() + deltaY});
  }
  bool prepareViewport() {
    if (titleDrag.prepared) return true;
    const auto &item = last.at("rank_panel").at("window_drag");
    require(item.is_object() && item.value("enabled",false) && item.value("visible",false) &&
        item.at("id").get<std::uint64_t>() != 0,
        "Rank viewport preparation requires its observed movable title bar");
    const auto &work = item.at("viewport_work_rect");
    require(work.is_array() && work.size() == 4, "Rank viewport work rectangle is missing");
    if (titleDrag.active) {
      require(titleDrag.step == 4 && item.at("id") == titleDrag.start.at("id") &&
          rectNear(item.at("rect"),titleDrag.start.at("rect"),titleDrag.deltaY) &&
          rectNear(item.at("window_rect"),titleDrag.start.at("window_rect"),titleDrag.deltaY) &&
          rectNear(work,titleDrag.start.at("viewport_work_rect")) &&
          !ImGui::GetIO().MouseDown[0] &&
          ImGui::GetCurrentContext()->ActiveId != item.at("id").get<ImGuiID>() &&
          (!ImGui::GetCurrentContext()->MovingWindow ||
           ImGui::GetCurrentContext()->MovingWindow->MoveId != item.at("id").get<ImGuiID>()),
          "Rank title-bar drag release/translated geometry was not observed");
      const auto &before = trial.at("viewport_preparation_before");
      require(stableDocument(last.at("document_info")) == before.at("document") &&
          last.at("workflow") == before.at("workflow") && last.at("results") == before.at("results") &&
          last.at("recording") == before.at("recording"),
          "Untimed Rank viewport preparation changed source/history/workflow/results");
      require(controlInsideWork("rank.preview",work) && controlInsideWork("rank.ok",work) &&
          controlInsideWork("rank.cancel",work),
          "Rank controls remain outside viewport after the one untimed drag");
      report["preparation"].push_back({{"trial_id",trialId()},
          {"kind","untimed observed title-bar drag completed"},{"frame",frame},
          {"move_id",item.at("id")},{"translated_title_rect",item.at("rect")},
          {"translated_window_rect",item.at("window_rect")},{"display_size",lastDisplaySize},
          {"steady_ns",rankRuntimeSteadyNanoseconds()}});
      titleDrag.active = false; titleDrag.prepared = true; return true;
    }
    if (controlInsideWork("rank.preview",work) && controlInsideWork("rank.ok",work) &&
        controlInsideWork("rank.cancel",work)) {
      titleDrag.prepared = true;
      report["preparation"].push_back({{"trial_id",trialId()},
          {"kind","untimed observed Rank controls already inside viewport; no drag"},
          {"frame",frame},{"window_drag",item},{"display_size",lastDisplaySize}});
      return true;
    }
    require(!titleDrag.attempted, "Rank viewport preparation allows only one drag per trial");
    const auto &windowRect = item.at("window_rect");
    require(windowRect.is_array() && windowRect.size() == 4 &&
        windowRect[0].get<float>() >= work[0].get<float>() &&
        windowRect[2].get<float>() <= work[2].get<float>(),
        "Rank viewport drag cannot repair a horizontal/invalid window rectangle");
    titleDrag.x = std::floor(item.at("point")[0].get<float>());
    titleDrag.y = std::floor(item.at("point")[1].get<float>());
    titleDrag.deltaY = std::floor(work[1].get<float>() + 8.f) - windowRect[1].get<float>();
    titleDrag.targetY = titleDrag.y + titleDrag.deltaY;
    require(admittedControl(item,titleDrag.x,titleDrag.y) && titleDrag.deltaY < -1.f &&
        windowRect[3].get<float>() + titleDrag.deltaY <= work[3].get<float>() &&
        titleDrag.targetY >= work[1].get<float>() && titleDrag.targetY < work[3].get<float>(),
        "Rank window cannot fit at viewport work top+8 with one upward title-bar drag");
    titleDrag.start = item; titleDrag.active = titleDrag.attempted = true;
    trial["viewport_preparation_before"] = stateSummary(last);
    report["preparation"].push_back({{"trial_id",trialId()},
        {"kind","untimed observed title-bar drag planned; no API repositioning"},
        {"frame",frame},{"window_drag",item},{"start_point",{titleDrag.x,titleDrag.y}},
        {"end_point",{titleDrag.x,titleDrag.targetY}},
        {"expected_title_rect",translatedRect(item.at("rect"),titleDrag.deltaY)},
        {"expected_window_rect",translatedRect(windowRect,titleDrag.deltaY)},
        {"display_size",lastDisplaySize},{"maximum_attempts_per_trial",1}});
    return false;
  }
  void titleDragInput(const char *kind, float y, std::uint64_t stamp, const Json &item) {
    require(report["inputs"].size() < 2048, "Rank input observation bound exhausted");
    report["inputs"].push_back({{"trial_id",trialId()},{"frame",frame},{"steady_ns",stamp},
        {"selector","rank.window_drag"},{"timed",false},{"control",item},
        {"input",{{"type",kind},{"point",{std::strcmp(kind,"aborted_release") == 0 ? -1.f : titleDrag.x,y}}}},
        {"actual_active_id",ImGui::GetCurrentContext()->ActiveId},
        {"scope","untimed actual ImGui title-bar preparation; no OS input credit"}});
  }
  void sendTitleDrag() {
    require(titleDrag.step >= 0 && titleDrag.step < 4, "Rank title-bar drag frame sequence is invalid");
    const auto item = last.at("rank_panel").at("window_drag");
    require(item.is_object() && item.value("enabled",false) && item.value("visible",false) &&
        item.at("id") == titleDrag.start.at("id"), "Rank title-bar move identity disappeared");
    const auto moved = titleDrag.step == 3;
    require(rectNear(item.at("rect"),titleDrag.start.at("rect"),moved ? titleDrag.deltaY : 0) &&
        rectNear(item.at("window_rect"),titleDrag.start.at("window_rect"),moved ? titleDrag.deltaY : 0),
        "Rank title-bar drag start/translated rectangle changed unexpectedly");
    if (titleDrag.step >= 2)
      require(ImGui::GetCurrentContext()->ActiveId == item.at("id").get<ImGuiID>(),
              "Actual Rank MoveID did not own the held title-bar drag");
    auto &io = ImGui::GetIO();
    const float y = titleDrag.step >= 2 ? titleDrag.targetY : titleDrag.y;
    io.AddMousePosEvent(titleDrag.x,y);
    const auto stamp = rankRuntimeSteadyNanoseconds();
    if (titleDrag.step == 1) { io.AddMouseButtonEvent(0,true); titleDrag.held = true; }
    if (titleDrag.step == 3) { io.AddMouseButtonEvent(0,false); titleDrag.held = false; }
    titleDragInput(titleDrag.step == 0 ? "position" : titleDrag.step == 1 ? "left_down" :
        titleDrag.step == 2 ? "held_move" : "left_up",y,stamp,item);
    ++titleDrag.step;
  }
  void logInput(const Json &input, std::uint64_t timestamp) {
    require(report["inputs"].size() < 2048, "Rank input observation bound exhausted");
    report["inputs"].push_back({{"trial_id",trialId()},{"frame",frame},
        {"steady_ns",timestamp},{"selector",gesture.selector},
        {"control",gesture.control},{"timed",gesture.timed},{"input",input}});
  }
  void sendGesture() {
    auto &io = ImGui::GetIO();
    const auto item = control(gesture.selector);
    if (!admittedControl(item,gesture.x,gesture.y) || item.at("id") != gesture.control.at("id")) {
      // Releasing a pressed pointer prevents it leaking into later preparation.
      if (gesture.step == 2) {
        const auto stamp = rankRuntimeSteadyNanoseconds();
        io.AddMousePosEvent(-1,-1); io.AddMouseButtonEvent(0,false);
        logInput({{"type","aborted_release"},{"point",{-1,-1}},{"down",false}},stamp);
      }
      gesture.active = false;
      complete("ineligible","Actual enabled widget identity/hitbox disappeared before release");
      return;
    }
    if (gesture.step == 0) {
      const auto stamp = rankRuntimeSteadyNanoseconds();
      io.AddMousePosEvent(gesture.x,gesture.y);
      logInput({{"type","position"},{"point",{gesture.x,gesture.y}}},stamp);
    } else {
      const bool down = gesture.step == 1;
      // GLFW may refresh cursor position in every backend frame. Reaffirm the
      // observed point before the separate down/up event; no OS pointer credit.
      if (!down && gesture.timed) trial["input_source_state"] = {
          {"scope","previous rendered frame observation, not callback-entry state"},
          {"sampled_frame",frame ? frame - 1 : 0},{"state",stateSummary(last)}};
      io.AddMousePosEvent(gesture.x,gesture.y);
      std::uint64_t stamp;
      if (!down && gesture.timed) {
        const auto before = rankRuntimeClockStamp();
        io.AddMouseButtonEvent(0,false);
        const auto after = rankRuntimeClockStamp();
        inputClockBefore = before; inputClockAfter = after; stamp = before.steady_ns;
        releaseNs = stamp; trial["actual_input_release_ns"] = stamp;
        trial["input_clock_before"] = rankRuntimeClockJson(before);
        trial["input_clock_after"] = rankRuntimeClockJson(after);
        trial["input_control"] = item;
      } else {
        stamp = rankRuntimeSteadyNanoseconds(); io.AddMouseButtonEvent(0,down);
      }
      logInput({{"type","button"},{"name","left"},{"down",down},
          {"position",{gesture.x,gesture.y}}},stamp);
    }
    if (++gesture.step == 3) gesture.active = false;
  }
  Json stateSummary(const Json &state) const {
    return {{"document",stableDocument(state.at("document_info"))},
        {"workflow",state.at("workflow")},{"results",state.at("results")},
        {"recording",state.at("recording")},{"gaussian_jobs",state.at("gaussian_jobs")}};
  }
  bool expectedView(const Json &consumed, bool filtered, bool preview,
                    bool previousFrame = false) const {
    if (!consumed.is_object() || !consumed.value("valid",false)) return false;
    if (previousFrame && frame < 2) return false;
    const auto observedFrame = previousFrame ? frame - 1 : frame;
    if (consumed.at("frame").get<std::uint64_t>() != observedFrame ||
        consumed.at("upload_serial").get<std::uint64_t>() == 0 ||
        consumed.at("document_id").get<std::uint64_t>() != sourceId ||
        consumed.at("tcz") != Json::array({1,1,2}) ||
        consumed.at("source_kind") != (preview ? "preview" : "source")) return false;
    const auto scalar = filtered ? expected.at("preview_masked_sha256") : originalRgb.at("active_plane_sha256");
    const auto rgb = filtered ? expectedRgb.at("rgb").at("sha256") : originalRgb.at("rgb").at("sha256");
    if (consumed.at("scalar_sha256") != scalar || consumed.at("rgb_sha256") != rgb) return false;
    if (preview) {
      if (!submittedGeneration || consumed.at("preview_generation").get<std::uint64_t>() != submittedGeneration)
        return false;
      for (std::size_t i = inputEventStart; i < report.at("events").size(); ++i) {
        const auto &event = report.at("events").at(i);
        if (event.at("kind") == "preview_install" &&
            event.value("job_id",std::uint64_t(0)) == submittedJob &&
            event.value("source_id",std::uint64_t(0)) == sourceId &&
            event.value("generation",std::uint64_t(0)) == submittedGeneration &&
            event.at("pixel_revision") == consumed.at("pixel_revision") &&
            event.at("data_identity") == consumed.at("data_identity")) return true;
      }
      return false;
    }
    const auto &doc = last.at("document_info");
    return consumed.at("pixel_revision") == doc.at("revisions").at("pixels") &&
        consumed.at("data_identity") == doc.at("data_identity");
  }
  void validateDraft() const {
    const auto &panel = last.at("rank_panel");
    require(panel.at("draft_valid").get<bool>() && panel.at("method") == method &&
        panel.at("radius") == 2.0 && panel.at("source_id").get<std::uint64_t>() == sourceId &&
        !panel.at("new_document").get<bool>() && panel.at("preview_position") == Json::array({1,1,2}),
        "Rank fixed draft differs from registered workload");
    const auto &target = panel.at("target");
    require(target.at("scope") == "ranges" && target.at("execution_mode") == "planes" &&
        target.at("destination") == "in_place" && target.at("roi_policy") == "active_selection",
        "Rank fixed Apply target policy mismatch");
    // The record names and axes are owned public target-record fields.
    require(target.at("ranges") == Json::array({Json::array({0,2,1}),
        Json::array({0,3,1}),Json::array({0,4,1})}),
        "Rank fixed Apply ranges must cover exact24TCZ planes");
  }
  void updateTicket() {
    const auto &current = lane(last,spec().role);
    const auto &job = current.at("job");
    if (job.is_object() && current.at("source_id").get<std::uint64_t>() == sourceId) {
      const auto id = job.at("id").get<std::uint64_t>();
      if (!submittedJob) submittedJob = id;
      require(id == submittedJob, "Rank trial ticket changed unexpectedly");
      trial["job_id"] = submittedJob;
      if (spec().role == "preview" && !current.at("request_generation").is_null()) {
        const auto generation = current.at("request_generation").get<std::uint64_t>();
        if (!submittedGeneration) submittedGeneration = generation;
        require(generation == submittedGeneration, "Rank trial preview generation changed");
        trial["generation"] = generation;
      }
    }
  }
  Json activeTickets() const {
    Json result = Json::array();
    for (const auto &job : window.jobs())
      if (job.state == JobState::Queued || job.state == JobState::Running ||
          job.state == JobState::Ready || job.state == JobState::Publishing)
        result.push_back(job.id);
    return result;
  }
  void validateClock(const RankRuntimeClockStamp &clock) const {
    require(clock.steady_ns > 0 && clock.qpc_frequency == report.at("qpc_frequency").get<std::uint64_t>(),
            "Rank native clock steady/frequency identity is invalid");
    require(clock.qpc_frequency ? clock.qpc_before > 0 && clock.qpc_after >= clock.qpc_before
                                : clock.qpc_before == 0 && clock.qpc_after == 0,
            "Rank native QPC bracket is invalid or unsupported fields are nonzero");
  }
  RankRuntimeClockStamp parseClock(const Json &value) const {
    for (const auto *key : {"steady_ns","qpc_before","qpc_after","qpc_frequency"})
      require(value.at(key).is_number_unsigned() ||
          (value.at(key).is_number_integer() && value.at(key) >= 0),
          "Rank clock fields must be exact nonnegative integers");
    RankRuntimeClockStamp result{value.at("steady_ns").get<std::uint64_t>(),
        value.at("qpc_before").get<std::uint64_t>(),value.at("qpc_after").get<std::uint64_t>(),
        value.at("qpc_frequency").get<std::uint64_t>()};
    validateClock(result); return result;
  }
  void qualifyReservation() {
    const auto &job = lane(last,spec().role).at("job");
    const auto active = activeTickets();
    const auto declared = job.at("resource_bytes").get<std::uint64_t>();
    const auto peak = job.at("peak_reserved_bytes").get<std::uint64_t>();
    const auto acquired = job.at("reservation_acquired").get<bool>();
    trial["declared_resource_bytes"] = declared;
    trial["peak_reserved_bytes"] = peak; trial["reservation_acquired"] = acquired;
    trial["endpoint_active_ticket_ids"] = active;
    trial["reservation_scope"] = "actual persisted native ticket ledger reservation; no allocation/private-byte inference";
    require(job.at("id").get<std::uint64_t>() == submittedJob &&
        job.at("document_id").get<std::uint64_t>() == sourceId && declared > 0 &&
        resourceZero(trial.at("baseline")) && trial.at("baseline_active_ticket_ids").empty(),
        "Rank reservation qualification lacks zero baseline and matching positive declared ticket");
    for (const auto &id : active)
      require(id.get<std::uint64_t>() == submittedJob, "Rank reservation window has another active scheduler ticket");
    const auto other = spec().role == "preview" ? "apply" : "preview";
    require(!lane(last,other).at("owns_job").get<bool>(), "Rank other lane owns a concurrent ticket");
    const bool queuedCancel = spec().endpoint == "cancel" && spec().state == "queued";
    require(acquired ? peak == declared : peak == 0,
            "Persisted rank ticket acquisition and actual ledger peak are inconsistent");
    require(queuedCancel || (acquired && peak > 0),
            "Eligible rank Preview/Apply/running/READY endpoint did not acquire its declared reservation");
  }

  void before(std::uint64_t ordinal) {
    frame = ordinal;
    if (stage == Stage::Done) return;
    const auto now = rankRuntimeSteadyNanoseconds();
    if (frame > frameLimit || now - begun >= 30 * second) {
      if (stage != Stage::Cleanup) complete("timeout","Cell30second/frame cap won over remaining trials");
      stopAfterCleanup = true;
      cleanup(true); return;
    }
    if (!observerFailure.empty()) {
      if (stage != Stage::Cleanup) complete("failed",observerFailure);
      stopAfterCleanup = true;
    }
    if (stage == Stage::Cleanup) { cleanup(false); return; }
    if (stage != Stage::Prepare && trialBegun &&
        now - trialBegun >= (spec().endpoint == "apply" ? 15 : 5) * second) {
      complete("timeout","Trial deadline reached before its required observed endpoint");
      return;
    }
    if (titleDrag.active) { sendTitleDrag(); return; }
    if (gesture.active) { sendGesture(); return; }
    if (stage == Stage::Prepare) { last = inspect(); prepare(); return; }
    if (stage == Stage::Range) {
      constexpr std::array<const char *,3> names{"target.c.range","target.z.range","target.t.range"};
      if (rangeAxis < 3) {
        if (startGesture(names[std::size_t(rangeAxis)],false)) {
          ++rangeAxis; sendGesture();
        }
      } else stage = Stage::Baseline;
      return;
    }
    if (stage == Stage::Start) {
      const auto selector = spec().role == "preview" ? "rank.preview" : "rank.ok";
      if (startGesture(selector,spec().endpoint != "cancel")) {
        stage = Stage::Await; sendGesture();
      }
      return;
    }
    if (stage == Stage::Cancel) {
      const auto &job = lane(last,spec().role).at("job");
      if (job.is_object() && job.value("state","") == spec().state &&
          (spec().state != "ready" || expectedView(last.at("consumed"),true,true,true))) {
        if (startGesture("jobs.cancel." + std::to_string(submittedJob),true)) {
          stage = Stage::Await; sendGesture();
        }
      } else if (terminal(job) || (spec().state == "queued" && job.is_object() &&
                 job.value("state","") != "queued") ||
                 (spec().state == "running" && job.is_object() && job.value("state","") == "ready"))
        complete("ineligible","Required natural Cancel state was not available at an enabled Jobs control");
      return;
    }
  }

  void after(std::uint64_t ordinal, const RankRuntimeClockStamp &renderClock) {
    frame = ordinal;
    if (stage == Stage::Done || stage == Stage::Prepare || stage == Stage::Cleanup) return;
    const auto renderSubmitNs = renderClock.steady_ns;
    last = inspect();
    const auto observedClock = rankRuntimeClockStamp();
    stateObservedClock = observedClock; stateObservedNs = observedClock.steady_ns;
    validateClock(renderClock); validateClock(stateObservedClock);
    lastDisplaySize = {ImGui::GetIO().DisplaySize.x,ImGui::GetIO().DisplaySize.y};
    if (!last.at("error").get<std::string>().empty()) {
      complete("failed","Window error: " + last.at("error").get<std::string>()); return;
    }
    if (stage == Stage::Settle) { stage = Stage::Range; return; }
    if (stateObservedNs - begun >= 30 * second ||
        (trialBegun && stateObservedNs - trialBegun >=
            (spec().endpoint == "apply" ? 15 : 5) * second)) {
      complete("timeout","Cell/trial deadline won before endpoint observation");
      return;
    }
    if (gesture.active) return;
    if (titleDrag.active && titleDrag.step < 4) return;
    if (stage == Stage::Baseline) {
      validateDraft();
      if (!prepareViewport()) return;
      if (!expectedView(last.at("consumed"),false,false)) return;
      require(resourceZero(last) && laneReleased(last), "Rank baseline carries old lane/resource owners");
      const auto active = activeTickets();
      require(active.empty(), "Rank baseline has an active scheduler ticket");
      require(last.at("document_info").at("calibration") == report.at("fixture").at("calibration"),
              "Actual native rank calibration differs from fixed fixture");
      require(last.at("workflow").empty() && last.at("results").empty() &&
          last.at("recording").get<bool>() && last.at("document_info").at("undo_steps") == 0 &&
          last.at("document_info").at("redo_steps") == 0,
          "Rank fresh trial baseline is not empty recorded history");
      const auto &settings = last.at("settings");
      require(settings.at("processing_workers") == 1 && settings.at("processing_memory_mb") == 128 &&
          settings.at("history_limit") == 8 && settings.at("history_memory_mb") == 128,
          "Rank benchmark effective settings differ from fixed protocol");
      trial["baseline"] = stateSummary(last);
      trial["baseline_active_ticket_ids"] = active;
      trial["settings"] = settings;
      trial["baseline_consumed"] = last.at("consumed");
      trial["baseline_render_submit_ns"] = renderSubmitNs;
      trial["baseline_render_clock"] = rankRuntimeClockJson(renderClock);
      stage = Stage::Start; return;
    }
    if (stage != Stage::Await && stage != Stage::Cancel) return;
    updateTicket();
    const auto &job = lane(last,spec().role).at("job");
    if (job.is_object() && job.value("state","") == "failed") {
      complete("failed","Actual rank ticket failed: " + job.value("error","")); return;
    }
    if (spec().endpoint == "cancel" && !releaseNs) {
      if (!submittedJob) return;
      if (!jobsShown) {
        // Opening before target editing would obscure the actual Rank controls.
        // This is untimed preparation; the ordinary scheduler keeps advancing.
        RankRuntimeProbeAccess::showJobs(window); jobsShown = true;
        report["preparation"].push_back({{"trial_id",trialId()},
            {"kind","untimed showJobs after actual submission; no scheduler suspension"},
            {"steady_ns",rankRuntimeSteadyNanoseconds()}});
        return;
      }
      if (spec().state == "ready") {
        if (expectedView(last.at("consumed"),true,true)) stage = Stage::Cancel;
        else if (terminal(job)) complete("ineligible","Preview ended before retained READY disposal input");
      } else stage = Stage::Cancel;
      return;
    }
    if (!releaseNs || !submittedJob) return;
    require(renderSubmitNs >= releaseNs, "Rank render stamp precedes final input release");
    if (spec().endpoint == "preview" && expectedView(last.at("consumed"),true,true)) {
      require(job.value("state","") == "ready", "Rank preview consumed without READY ticket");
      endpoint(renderClock); return;
    }
    if (spec().endpoint == "apply" && job.value("state","") == "completed" &&
        expectedView(last.at("consumed"),true,false)) {
      endpoint(renderClock); return;
    }
    if (spec().endpoint == "cancel") {
      const auto cancel = cancelEvent();
      if (!cancel.is_null()) {
        if (!cancel.contains("job") || !cancel.at("job").is_object() ||
            !cancel.at("job").contains("state")) {
          complete("failed","Actual Jobs Cancel callback initial-state evidence is missing"); return;
        }
        if (cancel.at("job").at("state") != spec().state) {
          complete("ineligible","Actual Jobs Cancel callback initial state differs from requested natural state"); return;
        }
      }
      if (terminal(job) && job.value("state","") != "canceled") {
        complete("ineligible","Rank ticket completed before actual Cancel could win"); return;
      }
      if (!cancel.is_null() && job.value("state","") == "canceled" && laneReleased(last) &&
          expectedView(last.at("consumed"),false,false)) {
        const auto returned = matchingEvent("cancel_return");
        if (returned.is_null()) return;
        const auto returnStamp = returned.at("steady_ns").get<std::uint64_t>();
        const auto returnClock = parseClock(returned.at("clock"));
        require(returnClock.steady_ns == returnStamp, "Cancel return scalar/clock timestamp mismatch");
        trial["render_submit_ns"] = renderSubmitNs;
        trial["render_submit_clock"] = rankRuntimeClockJson(renderClock);
        trial["terminal_resource_observed_ns"] = stateObservedNs;
        trial["state_observation_clock"] = rankRuntimeClockJson(stateObservedClock);
        trial["cancel_return_ns"] = returnStamp;
        trial["cancel_return_clock"] = rankRuntimeClockJson(returnClock);
        // The resource observation was made after render return. Its endpoint
        // must never be backdated to a frame that preceded physical release.
        auto latest = renderClock;
        if (stateObservedClock.steady_ns > latest.steady_ns) latest = stateObservedClock;
        if (returnClock.steady_ns > latest.steady_ns) latest = returnClock;
        endpoint(latest); return;
      }
    }
  }

  Json cancelEvent() const {
    for (std::size_t i = inputEventStart; i < report.at("events").size(); ++i) {
      const auto &row = report.at("events").at(i);
      if (row.at("kind") == "cancel_callback" && row.value("job_id",std::uint64_t(0)) == submittedJob)
        return row;
    }
    return Json();
  }
  bool hasEvent(const char *kind) const {
    for (std::size_t i = inputEventStart; i < report.at("events").size(); ++i) {
      const auto &row = report.at("events").at(i);
      if (row.at("kind") == kind && row.value("job_id",std::uint64_t(0)) == submittedJob &&
          row.value("source_id",std::uint64_t(0)) == sourceId) return true;
    }
    return false;
  }
  Json matchingEvent(const char *kind) const {
    for (std::size_t i = inputEventStart; i < report.at("events").size(); ++i) {
      const auto &row = report.at("events").at(i);
      if (row.at("kind") == kind && row.value("job_id",std::uint64_t(0)) == submittedJob &&
          row.value("source_id",std::uint64_t(0)) == sourceId) return row;
    }
    return Json();
  }
  void validateEvents(std::uint64_t endpointStamp) const {
    const auto submitted = matchingEvent("submitted");
    require(submitted.is_object() && submitted.at("role") == spec().role,
            "Actual submitted event is missing its ticket/source/role binding");
    const auto submittedStamp = submitted.at("steady_ns").get<std::uint64_t>();
    require(submittedStamp <= endpointStamp, "Submitted event follows observed endpoint");
    if (spec().role == "preview") require(
        submitted.at("generation").get<std::uint64_t>() == submittedGeneration,
        "Submitted preview generation differs from consumed generation");
    if (spec().endpoint != "cancel") {
      Json callback;
      for (std::size_t i = inputEventStart; i < report.at("events").size(); ++i) {
        const auto &row = report.at("events").at(i);
        if (row.at("kind") == "callback" && row.value("source_id",std::uint64_t(0)) == sourceId &&
            row.value("endpoint","") == spec().endpoint) { callback = row; break; }
      }
      require(callback.is_object(), "Actual input activation callback is missing");
      const auto callbackStamp = callback.at("steady_ns").get<std::uint64_t>();
      require(releaseNs <= callbackStamp && callbackStamp <= submittedStamp,
              "Actual input/callback/submission event order is invalid");
    } else {
      const auto callback = matchingEvent("cancel_callback");
      const auto returned = matchingEvent("cancel_return");
      require(callback.is_object() && returned.is_object() && callback.at("job").is_object() &&
          callback.at("job").at("id").get<std::uint64_t>() == submittedJob &&
          callback.at("job").at("state") == spec().state,
          "Actual Cancel callback lacks required ticket/initial-state binding");
      const auto callbackStamp = callback.at("steady_ns").get<std::uint64_t>();
      const auto returnStamp = returned.at("steady_ns").get<std::uint64_t>();
      require(submittedStamp <= releaseNs && releaseNs <= callbackStamp &&
          callbackStamp <= returnStamp && returnStamp <= endpointStamp,
          "Actual submit/Cancel-input/callback/return/endpoint order is invalid");
    }
    if (spec().endpoint == "apply") {
      const auto publication = matchingEvent("publication");
      require(publication.is_object(), "Actual native publication event missing");
      const auto stamp = publication.at("native_publish_return_steady_ns").get<std::uint64_t>();
      require(submittedStamp <= stamp && stamp <= endpointStamp,
              "Actual native publication did not precede consumed render submission");
      require(publication.at("published_document_id").get<std::uint64_t>() == sourceId &&
          publication.at("pixel_revision") == last.at("consumed").at("pixel_revision") &&
          publication.at("workflow") == last.at("workflow") &&
          publication.at("undo") == last.at("document_info").at("undo_steps"),
          "Actual native publication owner/revision/history differs from consumed view");
    }
    if (spec().role == "preview" && spec().state != "queued" && spec().state != "running") {
      const auto installed = matchingEvent("preview_install");
      require(installed.is_object(), "Actual preview installation event missing");
      const auto stamp = installed.at("steady_ns").get<std::uint64_t>();
      require(submittedStamp <= stamp && stamp <= endpointStamp &&
          installed.at("generation").get<std::uint64_t>() == submittedGeneration,
          "Actual preview installation event order/generation is invalid");
    }
  }
  Json actualPixels() {
    // This whole scope is outside the endpoint clock. No native owner escapes.
    std::vector<unsigned char> bytes;
    bytes.reserve(source.size());
    {
      const auto snapshot = window.nativeSnapshot(sourceId);
      const auto &dataset = snapshot.data().dataset;
      require(dataset.shape().dimensions() == std::array<int,6>{2,3,4,128,192,1} &&
          dataset.pixel_type() == type, "Actual rank source type/shape changed");
      for (int t = 0; t < 2; ++t) for (int c = 0; c < 3; ++c) for (int z = 0; z < 4; ++z)
        appendDeclared(bytes,*dataset.plane({t,c,z}));
    }
    require(bytes.size() == source.size(), "Actual rank canonical payload size changed");
    std::size_t outsideChanged = 0, holesChanged = 0;
    const auto width = sampleBytes(type);
    for (std::size_t pixel = 0; pixel < 24U * 128U * 192U; ++pixel) {
      const auto local = pixel % (128U * 192U);
      const bool selected = (roi[local / 8] & (1U << (7 - local % 8))) != 0;
      if (selected) continue;
      bool different = false;
      for (std::size_t byte = 0; byte < width; ++byte)
        different |= bytes[pixel * width + byte] != source[pixel * width + byte];
      outsideChanged += different;
      const auto x = local % 192U, y = local / 192U;
      if (x >= 64 && x < 96 && y >= 40 && y < 72) holesChanged += different;
    }
    return {{"full_declared_sha256",rankRuntimeSha256(bytes)},
        {"bytes",bytes.size()},{"exterior_or_hole_changed_samples",outsideChanged},
        {"hole_changed_samples",holesChanged}};
  }
  void endpoint(const RankRuntimeClockStamp &endpointClock) {
    // Keep the selected complete render/resource clock before any readbacks.
    const auto stamp = endpointClock.steady_ns;
    validateClock(inputClockBefore); validateClock(inputClockAfter); validateClock(endpointClock);
    trial["memory_window"] = {{"input_before",rankRuntimeClockJson(inputClockBefore)},
        {"input_after",rankRuntimeClockJson(inputClockAfter)},
        {"endpoint",rankRuntimeClockJson(endpointClock)}};
    trial["memory_window_status"] = endpointClock.qpc_frequency
        ? "native QPC event brackets; external sampler must use raw QPC and retain boundary uncertainty/gaps"
        : "unsupported POSIX QPC bridge; no memory performance claim";
    require(inputClockBefore.steady_ns == releaseNs && inputClockAfter.steady_ns >= releaseNs &&
        stamp >= inputClockAfter.steady_ns &&
        (!endpointClock.qpc_frequency ||
          (inputClockBefore.qpc_after <= inputClockAfter.qpc_before &&
           inputClockAfter.qpc_after <= endpointClock.qpc_before)),
        "Rank native input/endpoint clock brackets are not ordered");
    trial["endpoint_ns"] = stamp;
    trial["elapsed_ns"] = stamp - releaseNs;
    trial["endpoint_frame"] = frame;
    trial["consumed"] = last.at("consumed");
    trial["terminal"] = stateSummary(last);
    validateEvents(stamp);
    qualifyReservation();
    const auto &actualCalibration = last.at("document_info").at("calibration");
    trial["calibration"] = actualCalibration;
    require(actualCalibration == trial.at("baseline").at("document").at("calibration") &&
        actualCalibration == report.at("fixture").at("calibration"),
        "Rank endpoint did not preserve the complete actual native calibration");
    require(hasEvent("submitted"), "Actual rank submitted event missing for ticket/source");
    if (spec().endpoint == "apply") require(hasEvent("publication"), "Actual native publication event missing");
    if (spec().endpoint == "preview") require(hasEvent("preview_install"), "Actual preview installation event missing");
    if (spec().endpoint == "cancel") {
      require(hasEvent("cancel_callback") && hasEvent("cancel_return"), "Actual Jobs Cancel events missing");
      require(lane(last,spec().role).at("job").at("cancel_requested").get<bool>(),
              "Canceled rank ticket did not retain cancel_requested");
      for (std::size_t i = inputEventStart; i < report.at("events").size(); ++i) {
        const auto &row = report.at("events").at(i);
        require(!(row.at("kind") == "publication" && row.value("source_id",std::uint64_t(0)) == sourceId),
                "Canceled trial published native work");
        if (row.at("kind") == "preview_install" && row.value("source_id",std::uint64_t(0)) == sourceId)
          require(row.at("steady_ns").get<std::uint64_t>() < releaseNs,
                  "Preview installed after actual Cancel input");
      }
    }
    const auto pixels = actualPixels(); trial["actual_pixels"] = pixels;
    const auto fullHash = spec().endpoint == "apply" ? expected.at("masked_output").at("sha256")
                                                      : report.at("fixture").at("input").at("sha256");
    require(pixels.at("full_declared_sha256") == fullHash &&
        pixels.at("exterior_or_hole_changed_samples") == 0 && pixels.at("hole_changed_samples") == 0,
        "Actual rank full output/exterior/hole does not match independent fixed workload");
    if (spec().endpoint == "apply") {
      require(last.at("workflow").size() == 1 && last.at("document_info").at("undo_steps") == 1 &&
          last.at("document_info").at("redo_steps") == 0 && last.at("document_info").at("dirty").get<bool>() &&
          laneReleased(last), "Rank Apply publication history/workflow/resource endpoint mismatch");
      const auto &before = trial.at("baseline").at("document");
      const auto after = stableDocument(last.at("document_info"));
      for (const auto *key : {"id","dtype","shape","axis_sizes","current_tcz",
                              "selection_sha256","display","calibration"})
        if (before.contains(key)) require(after.at(key) == before.at(key),
            std::string("Rank Apply changed preserved source field: ") + key);
    } else require(stableDocument(last.at("document_info")) == trial.at("baseline").at("document") &&
        last.at("workflow") == trial.at("baseline").at("workflow") &&
        last.at("results") == trial.at("baseline").at("results"),
        "Preview/Cancel changed native source/history/workflow/results");
    if (!capturedView) {
      const auto detailed = inspect(true);
      const auto &view = detailed.at("consumed");
      const auto scalar = unhex(view.at("scalar_declared_hex").get<std::string>());
      const auto rgb = unhex(view.at("rgb_hex").get<std::string>());
      require(scalar.size() == 128U * 192U * sampleBytes(type) && rgb.size() == 128U * 192U * 3U &&
          rankRuntimeSha256(scalar) == trial.at("consumed").at("scalar_sha256") &&
          rankRuntimeSha256(rgb) == trial.at("consumed").at("rgb_sha256"),
          "Postclock actual cache readback differs from endpoint cache identity");
      report["actual_view"] = {{"trial_id",trialId()},{"scope","postclock copied cache/upload-input bytes"},
          {"dtype",dtype},{"scalar_shape",{128,192,1}},{"rgb_shape",{128,192,3}},
          {"scalar_declared_hex",hex(scalar)},{"rgb_hex",hex(rgb)},
          {"scalar_sha256",rankRuntimeSha256(scalar)},{"rgb_sha256",rankRuntimeSha256(rgb)}};
      capturedView = true;
    }
    complete("observed","Required independently matching actual endpoint observed");
  }

  void complete(const std::string &status, const std::string &reason) {
    if (stage == Stage::Cleanup || stage == Stage::Done) return;
    if (trial.is_null()) beginTrial();
    if (((gesture.active && gesture.step == 2) || titleDrag.held) && ImGui::GetCurrentContext()) {
      const auto stamp = rankRuntimeSteadyNanoseconds();
      ImGui::GetIO().AddMousePosEvent(-1,-1);
      ImGui::GetIO().AddMouseButtonEvent(0,false);
      titleDrag.held = false;
      if (titleDrag.active) titleDragInput("aborted_release",-1,stamp,titleDrag.start);
      else logInput({{"type","aborted_release"},{"point",{-1,-1}},{"down",false},
          {"scope","untimed cleanup, no widget activation credit"}},stamp);
    }
    gesture.active = titleDrag.active = false;
    trial["status"] = status; trial["reason"] = reason;
    trial["eligible"] = status == "observed";
    trial["ended_ns"] = rankRuntimeSteadyNanoseconds();
    trial["event_range"] = {inputEventStart,report.at("events").size()};
    if (!last.is_null() && last.contains("gaussian_jobs")) {
      trial["last_resources"] = usage(last);
      trial["last_gaussian_jobs"] = last.at("gaussian_jobs");
      if (last.contains("consumed")) trial["last_consumed"] = last.at("consumed");
      if (last.contains("document_info")) trial["last_document"] = stableDocument(last.at("document_info"));
      if (last.contains("rank_panel") && status != "observed") trial["last_rank_panel"] = last.at("rank_panel");
      if (status != "observed") trial["display_size"] = lastDisplaySize;
    }
    cleanupBegun = rankRuntimeSteadyNanoseconds(); stage = Stage::Cleanup;
  }
  void cleanup(bool finalBound) {
    // Explicit untimed teardown never supplies Cancel timing or success credit.
    if (sourceId) {
      window.cancel();
      const auto found = std::any_of(window.documents().begin(),window.documents().end(),
          [&](const auto &document) { return document->id == sourceId; });
      if (found) window.closeDocument(sourceId,true);
      report["preparation"].push_back({{"trial_id",trialId()},
          {"kind","untimed owned cleanup: cancel and closeDocument(discard=true)"},
          {"steady_ns",rankRuntimeSteadyNanoseconds()}});
      sourceId = 0;
    }
    last = inspect();
    const bool zero = resourceZero(last);
    const auto now = rankRuntimeSteadyNanoseconds();
    if (!zero && !finalBound && now - cleanupBegun < 10 * second) return;
    trial["cleanup"] = {{"steady_ns",now},{"usage",usage(last)},
        {"documents",window.documents().size()},{"resource_zero",zero},
        {"bounded_out",!zero}};
    report["trials"].push_back(trial);
    if (report.dump().size() >= reportLimit - 1024U * 1024U) {
      observerFailure = "Rank retained report bound reached"; stage = Stage::Done; return;
    }
    if (!zero || finalBound || stopAfterCleanup) { stage = Stage::Done; return; }
    const bool retry = spec().endpoint == "cancel" && trial.at("status") == "ineligible" && spec().attempt < 3;
    if (retry) ++specifications.at(specIndex).attempt;
    else ++specIndex;
    trial = Json(); trialBegun = 0;
    stage = specIndex == specifications.size() ? Stage::Done : Stage::Prepare;
  }
  void record(const std::string &kind, Json fields) {
    if (stage == Stage::Done) return;
    require(fields.is_object(), "Rank runtime event must be copied JSON fields");
    require(report["events"].size() < eventLimit, "Rank runtime event bound exhausted");
    fields["kind"] = kind; fields["observer_frame"] = frame;
    if (!fields.contains("steady_ns")) fields["observed_steady_ns"] = rankRuntimeSteadyNanoseconds();
    if (!trial.is_null()) fields["trial_id"] = trial.value("id","");
    if (kind == "preview_install" && fields.contains("steady_ns"))
      lastInstallNs = fields.at("steady_ns").get<std::uint64_t>();
    report["events"].push_back(std::move(fields));
  }
  Json finish() {
    if (finishedReport) return report;
    if (stage != Stage::Done) {
      if (stage != Stage::Cleanup) complete("incomplete","Normal loop ended before required endpoint");
      stopAfterCleanup = true; cleanup(true);
    }
    if (!observerFailure.empty()) report["observer_error"] = observerFailure;
    for (const auto &s : specifications) {
      bool observed = false;
      for (const auto &row : report.at("trials"))
        if (row.at("endpoint") == s.endpoint && row.at("role") == s.role &&
            row.at("required_initial_state") == s.state && row.at("phase") == s.phase &&
            row.at("sequence") == s.sequence && row.at("eligible").get<bool>()) observed = true;
      if (!observed) report["missing_series"].push_back({{"endpoint",s.endpoint},{"role",s.role},
          {"initial_state",s.state},{"phase",s.phase},{"sequence",s.sequence}});
    }
    report["completion"] = report.at("missing_series").empty() && observerFailure.empty()
        ? "all_requested_observations_retained" : "incomplete_or_ineligible_observations_retained";
    report["ended_ns"] = rankRuntimeSteadyNanoseconds();
    report["elapsed_ns"] = report.at("ended_ns").get<std::uint64_t>() - begun;
    report["frames"] = frame;
    report["cleanup"] = {{"usage",usage(last)},{"resource_zero",resourceZero(last)},
        {"documents",window.documents().size()}};
    report["statistics"] = Json::array();
    if (mode == "series") for (const auto &s : specifications) {
      if (s.phase != "cold") continue;
      std::vector<std::uint64_t> samples;
      for (const auto &row : report.at("trials"))
        if (row.at("phase") == "measured" && row.at("eligible").get<bool>() &&
            row.at("endpoint") == s.endpoint && row.at("role") == s.role &&
            row.at("required_initial_state") == s.state)
          samples.push_back(row.at("elapsed_ns").get<std::uint64_t>());
      std::sort(samples.begin(),samples.end());
      Json row{{"endpoint",s.endpoint},{"role",s.role},{"initial_state",s.state},
          {"n",samples.size()},{"samples_ns",samples},{"acceptance",false}};
      if (samples.size() == 10) {
        row["nearest_rank_p95_ns"] = samples.at(9);
        row["proposed_budget_ms"] = s.endpoint == "preview" ? 250 : s.endpoint == "apply" ? 1500 : 150;
        row["proposed_comparison_only"] = samples.at(9) <=
            std::uint64_t(row.at("proposed_budget_ms").get<int>()) * 1000000ULL;
      } else row["p95_status"] = "missing10eligiblemeasuredsamples";
      report["statistics"].push_back(std::move(row));
    }
    exclusiveJson(directory / "rank-runtime.json",report);
    finishedReport = true; return report;
  }
};

RankRuntimeProbe::RankRuntimeProbe(Window &window, const Json &plan,
    const std::string &context, const std::filesystem::path &directory)
    : impl_(std::make_unique<Impl>(window,plan,context,directory)) {}
RankRuntimeProbe::~RankRuntimeProbe() = default;
void RankRuntimeProbe::beforeFrame(std::uint64_t frame) {
  try { impl_->before(frame); }
  catch (const std::exception &error) { if (impl_->observerFailure.empty()) impl_->observerFailure = error.what(); }
  catch (...) { if (impl_->observerFailure.empty()) impl_->observerFailure = "Unknown rank normal-loop beforeFrame failure"; }
}
void RankRuntimeProbe::afterRender(std::uint64_t frame, const RankRuntimeClockStamp &renderSubmit) {
  try { impl_->after(frame,renderSubmit); }
  catch (const std::exception &error) { if (impl_->observerFailure.empty()) impl_->observerFailure = error.what(); }
  catch (...) { if (impl_->observerFailure.empty()) impl_->observerFailure = "Unknown rank normal-loop afterRender failure"; }
}
bool RankRuntimeProbe::finished() const { return impl_->stage == Impl::Stage::Done; }
void RankRuntimeProbe::event(const std::string &kind, Json fields) {
  try { impl_->record(kind,std::move(fields)); }
  catch (const std::exception &error) { if (impl_->observerFailure.empty()) impl_->observerFailure = error.what(); }
  catch (...) { if (impl_->observerFailure.empty()) impl_->observerFailure = "Unknown rank normal-loop event failure"; }
}
Json RankRuntimeProbe::finish() { return impl_->finish(); }

Json rankRuntimeCase(Window &window, const Json &plan, const std::string &context,
                     const std::string &freshDirectory) {
  require(!freshDirectory.empty(), "Rank runtime requires a fresh scratch directory");
  const auto directory = std::filesystem::absolute(std::filesystem::u8path(freshDirectory)).lexically_normal();
  require(!std::filesystem::exists(directory), "Rank runtime scratch directory must not exist");
  require(std::filesystem::create_directories(directory), "Cannot create rank runtime scratch directory");
  RankRuntimeClockStamp processClock;
  try {
    processClock = rankRuntimeClockStamp();
    exclusiveJson(directory / "process.json",{{"schema",1},
        {"producer_id","p06f.rank.normal-loop.process.001"},{"context",context},
        {"process_id",currentProcessId()},{"steady_ns",processClock.steady_ns},
        {"qpc_frequency",processClock.qpc_frequency},{"qpc_supported",processClock.qpc_frequency != 0},
        {"clock",rankRuntimeClockJson(processClock)}});
    RankRuntimeProbe probe(window,plan,context,directory);
    RunOptions options;
    options.rank_probe = &probe;
    options.frames = static_cast<int>(frameLimit);
    const auto code = window.run(options);
    if (code != 0) probe.event("normal_loop_return",{{"code",code},
        {"steady_ns",rankRuntimeSteadyNanoseconds()}});
    return probe.finish();
  } catch (const std::exception &error) {
    Json failure{{"schema",1},{"producer_id","p06f.rank.normal-loop.raw.001"},
        {"context",context},{"process_id",currentProcessId()},
        {"qpc_frequency",processClock.qpc_frequency},{"qpc_supported",processClock.qpc_frequency != 0},
        {"acceptance",false},{"completion","setup_or_finish_failed"},
        {"error",error.what()},{"steady_ns",rankRuntimeSteadyNanoseconds()}};
    if (!std::filesystem::exists(directory / "rank-runtime.json"))
      exclusiveJson(directory / "rank-runtime.json",failure);
    throw;
  }
}
} // namespace imagejxx::desktop
