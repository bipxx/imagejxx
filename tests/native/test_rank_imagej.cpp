#include <imagejxx/rank.hpp>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <filesystem>
#include <iostream>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

#ifndef IMAGEJXX_RANK_IMAGEJ_FIXTURE
#error "Define IMAGEJXX_RANK_IMAGEJ_FIXTURE to the retained ImageJ 1.54r JSON corpus"
#endif

using namespace imagejxx;
namespace {
using Json = nlohmann::json;
std::size_t checks = 0;
std::size_t filteredPlanes = 0;
std::size_t observedMembers = 0;
std::size_t sourcePlanes = 0;
std::vector<std::string> cases;

void check(bool condition, const std::string &message) {
  ++checks;
  if (!condition) throw std::runtime_error(message);
}
void named(const std::string &name) { cases.push_back(name); }
template<class F> void rejects(F &&work, const std::string &message) {
  bool refused = false;
  try { work(); } catch (const std::invalid_argument &) { refused = true; }
  check(refused, message);
}
bool sameStorage(const Image &a, const Image &b) {
  return a.width == b.width && a.height == b.height && a.channels == b.channels &&
         a.type == b.type && a.pixels.size() == b.pixels.size() &&
         std::memcmp(a.pixels.data(), b.pixels.data(), a.pixels.size() * sizeof(double)) == 0;
}
std::uint32_t floatBits(double value) {
  const auto narrow = static_cast<float>(value);
  std::uint32_t bits;
  std::memcpy(&bits, &narrow, sizeof(bits));
  return bits;
}
std::vector<std::uint8_t> unhex(const std::string &text) {
  check(text.size() % 2 == 0, "Odd observed byte encoding");
  std::vector<std::uint8_t> result;
  result.reserve(text.size() / 2);
  const auto nibble = [](char value) {
    if (value >= '0' && value <= '9') return value - '0';
    if (value >= 'a' && value <= 'f') return value - 'a' + 10;
    throw std::runtime_error("Noncanonical observed byte encoding");
  };
  for (std::size_t at = 0; at < text.size(); at += 2)
    result.push_back(static_cast<std::uint8_t>(16 * nibble(text[at]) + nibble(text[at + 1])));
  return result;
}
std::uint32_t word(const std::vector<std::uint8_t> &bytes, std::size_t at, std::size_t count) {
  std::uint32_t result = 0;
  for (std::size_t index = 0; index < count; ++index)
    result |= static_cast<std::uint32_t>(bytes.at(at + index)) << (8 * index);
  return result;
}
PixelType pixelType(const std::string &dtype) {
  if (dtype == "uint8") return PixelType::UInt8;
  if (dtype == "uint16") return PixelType::UInt16;
  if (dtype == "float32") return PixelType::Float32;
  throw std::runtime_error("Out-of-scope dtype in numerical fixture");
}
RankFilterMethod method(const std::string &name) {
  if (name == "minimum") return RankFilterMethod::Minimum;
  if (name == "maximum") return RankFilterMethod::Maximum;
  if (name == "median") return RankFilterMethod::Median;
  if (name == "mean") return RankFilterMethod::Mean;
  throw std::runtime_error("Unknown numerical fixture method");
}
Image imageFromBytes(const Json &run, const std::vector<std::uint8_t> &bytes) {
  const auto &shape = run.at("shape_tczyx");
  const auto type = pixelType(run.at("dtype").get<std::string>());
  Image image(shape.at(4).get<int>(), shape.at(3).get<int>(), 1, type);
  const std::size_t bytesPerPixel = type == PixelType::UInt8 ? 1 : type == PixelType::UInt16 ? 2 : 4;
  check(bytes.size() == bytesPerPixel * image.pixels.size(), "Observed byte count differs from plane metadata");
  for (std::size_t index = 0; index < image.pixels.size(); ++index) {
    const auto bits = word(bytes, index * bytesPerPixel, bytesPerPixel);
    if (type == PixelType::Float32) {
      float sample;
      std::memcpy(&sample, &bits, sizeof(sample));
      check(std::isfinite(sample), "Nonfinite source entered finite scalar fixture");
      image.pixels[index] = static_cast<double>(sample);
    } else image.pixels[index] = static_cast<double>(bits);
  }
  return image;
}
std::array<int, 4> rectangle(const Json &run) {
  const auto &roi = run.at("roi");
  if (!roi.is_null()) {
    if (roi.at("kind") == "rectangle") return roi.at("xywh").get<std::array<int, 4>>();
    if (roi.at("kind") == "holed_area") return roi.at("outer_xywh").get<std::array<int, 4>>();
  }
  // The original runner treats point/line/absent selections as whole image.
  return {0, 0, run.at("shape_tczyx").at(4).get<int>(), run.at("shape_tczyx").at(3).get<int>()};
}
bool inside(const std::array<int, 4> &bounds, int x, int y) {
  return x >= bounds[0] && y >= bounds[1] && x < bounds[0] + bounds[2] && y < bounds[1] + bounds[3];
}
bool member(const Json &run, const std::array<int, 4> &bounds, int x, int y) {
  if (!inside(bounds, x, y)) return false;
  const auto &roi = run.at("roi");
  return roi.is_null() || roi.at("kind") != "holed_area" ||
         !inside(roi.at("hole_xywh").get<std::array<int, 4>>(), x, y);
}
bool sampleEquals(const Image &image, std::size_t index,
                  const std::vector<std::uint8_t> &expected) {
  const auto value = image.pixels.at(index);
  if (image.type == PixelType::Float32)
    return std::isfinite(value) && static_cast<double>(static_cast<float>(value)) == value &&
           floatBits(value) == word(expected, index * 4, 4);
  const auto bytesPerPixel = image.type == PixelType::UInt8 ? 1u : 2u;
  return !std::signbit(value) && value == static_cast<double>(word(expected, index * bytesPerPixel, bytesPerPixel));
}
void fixtureIdentity(const Json &fixture) {
  named("fixture/provenance");
  check(fixture.at("schema_version") == 1 && fixture.at("imagej_version") == "1.54r" &&
            fixture.at("imagej_commit") == "62db90a269cccc1ac79f658e4602043214ed055b",
        "Wrong ImageJ numerical reference identity");
  const auto &provenance = fixture.at("provenance");
  check(provenance.at("raw").at("sha256") == "f2a3677079d355b53339b11612fc39ca809e5b070b22caa80a5d4fcc6b739df5" &&
            provenance.at("raw").at("bytes") == 865012,
        "Original retained observation pin changed");
  check(provenance.at("input_plan").at("sha256") == "1f64bf082e5e8ccd819fdec87598d765acb571aaaf6fa15b42abd0e3547e4b60" &&
            provenance.at("execution_inputs").at("sha256") == "5bd642c6720ff28059769757a82123c51a521576e0ec0829421c198953a750d8",
        "Consumed input metadata pins changed");
  check(provenance.at("rank_source_members").at("ij/plugin/filter/RankFilters.java").at("sha256") ==
            "284e7647e6619a3cc62887c57992056ccfd1dd99661868571fff005afff7d6b7",
        "Observed original RankFilters source pin changed");
  const std::array<int, 16> runCounts{0, 3, 3, 3, 3, 8, 1, 1, 108, 8, 12, 4, 12, 8, 0, 0};
  check(provenance.at("original_case_ids").size() == 16 &&
            provenance.at("original_case_inventory").size() == 16 &&
            provenance.at("original_numerical_run_count") == 174,
        "Full original sixteen/174 reference membership was lost");
  for (std::size_t index = 0; index < 16; ++index) {
    const auto id = std::string("K") + (index < 9 ? "0" : "") + std::to_string(index + 1);
    check(provenance.at("original_case_ids").at(index) == id &&
              provenance.at("original_case_inventory").at(index).at("id") == id &&
              provenance.at("original_case_inventory").at(index).at("numerical_run_count") == runCounts[index],
          "Original ordered case IDs/run counts changed");
  }
  check(provenance.at("observational_only").size() == 4 &&
            provenance.at("observational_only").contains("K12") && provenance.at("observational_only").contains("K13") &&
            provenance.at("original_case_inventory").at(11).at("native_scope") == "observational_only" &&
            provenance.at("original_case_inventory").at(12).at("native_scope") == "observational_only",
        "RGB/nonfinite observations must remain explicit and outside finite scalar acceptance");
  check(fixture.at("kernel_count") == 10 && fixture.at("kernels").size() == 10 &&
            fixture.at("original_selected_run_count") == 158 && fixture.at("supplement_run_count") == 3 &&
            fixture.at("run_count") == 161 && fixture.at("runs").size() == 161,
        "Finite scalar selection or three-run supplement membership changed");
  check(provenance.at("supplement").at("sha256") == "bb18f7476fdb181964f1d24efa5ce8492c903d91beeadd0aed2c8609ed6a1b10" &&
            provenance.at("supplement").at("bytes") == 10881,
        "Actual finite signed-zero supplemental observation pin changed");
}
void geometry(const Json &fixture) {
  const std::array<double, 10> radii{0, .5, 1, 1.5, 1.75, 2, 2.5, 2.85, 3, 10};
  std::size_t index = 0;
  for (const auto &observation : fixture.at("kernels")) {
    named(observation.at("id").get<std::string>());
    const auto radius = observation.at("radius").get<double>();
    check(radius == radii.at(index++), "Kernel radius membership/order changed");
    const auto actual = rank_filter_kernel_imagej(radius);
    check(actual.half_widths == observation.at("half_widths").get<std::vector<int>>() &&
              actual.point_count == observation.at("point_count").get<std::size_t>(),
          "Native circular geometry differs from actual makeLineRadii observation");
    Json offsets = Json::array();
    const int extent = static_cast<int>(actual.half_widths.size() / 2);
    for (std::size_t row = 0; row < actual.half_widths.size(); ++row)
      for (int dx = -actual.half_widths[row]; dx <= actual.half_widths[row]; ++dx)
        offsets.push_back(Json::array({dx, static_cast<int>(row) - extent}));
    check(offsets == observation.at("offsets_xy"), "Kernel point visit order differs from actual ImageJ offsets");
  }
}
void observedRuns(const Json &fixture) {
  std::vector<std::string> expectedIds;
  const std::array<std::string, 4> methods{"minimum", "maximum", "median", "mean"};
  for (const auto &id : {"K02", "K03", "K04", "K05"})
    for (const auto &dtype : {"uint8", "uint16", "float32"}) expectedIds.push_back(std::string(id) + "/" + dtype);
  for (const auto &radius : {"0", "0.5", "1.5", "1.75", "2", "2.5", "2.85", "3"})
    expectedIds.push_back(std::string("K06/radius.") + radius);
  expectedIds.push_back("K07/integer_rounding");
  expectedIds.push_back("K08/incremental_cancellation");
  for (const auto &dtype : {"uint8", "uint16", "float32"})
    for (const auto &shape : {"1x1", "1x7", "7x1"})
      for (const auto &operation : methods)
        for (const auto &radius : {"0", "1", "2"})
          expectedIds.push_back(std::string("K09/") + dtype + "." + shape + "." + operation + "." + radius);
  for (const auto &roi : {"rectangle", "hole"})
    for (const auto &operation : methods) expectedIds.push_back(std::string("K10/") + roi + "." + operation);
  for (const auto &roi : {"absent", "point", "line"})
    for (const auto &operation : methods) expectedIds.push_back(std::string("K11/") + roi + "." + operation);
  for (const auto &scope : {"current", "all_stack"})
    for (const auto &operation : methods) expectedIds.push_back(std::string("K14/") + scope + "." + operation);
  for (const auto &operation : {"minimum", "maximum", "median"})
    expectedIds.push_back(std::string("SZ01/") + operation + ".radius2.signedzero");
  std::array<std::size_t, 17> perCase{};
  std::size_t runIndex = 0;
  for (const auto &run : fixture.at("runs")) {
    const auto label = run.at("id").get<std::string>();
    named(label);
    check(label == expectedIds.at(runIndex++), "Observed numerical run membership/order changed");
    const auto sourceCase = run.at("source_case_id").get<std::string>();
    if (sourceCase.size() == 3 && sourceCase[0] == 'K') ++perCase.at(static_cast<std::size_t>(std::stoi(sourceCase.substr(1))));
    const auto active = run.at("position_tcz").get<std::array<int, 3>>();
    const bool all = run.at("all_stack").get<bool>();
    const auto bounds = rectangle(run);
    const auto roi = run.at("roi");
    const bool useExtent = !roi.is_null() && (roi.at("kind") == "rectangle" || roi.at("kind") == "holed_area");
    std::optional<RankFilterExtent> extent;
    if (useExtent) extent = RankFilterExtent{bounds[0], bounds[1], bounds[2], bounds[3]};
    std::size_t selected = 0;
    std::size_t planeIndex = 0;
    const auto dims = run.at("shape_tczyx").get<std::array<int, 5>>();
    check(run.at("planes").size() == static_cast<std::size_t>(dims[0] * dims[1] * dims[2]),
          label + ": retained plane membership changed");
    for (const auto &plane : run.at("planes")) {
      ++sourcePlanes;
      const auto before = unhex(plane.at("before_hex").get<std::string>());
      const auto after = unhex(plane.at("after_hex").get<std::string>());
      const auto source = imageFromBytes(run, before);
      const auto retained = source;
      const std::array<int, 3> expectedPosition{
          static_cast<int>(planeIndex) / (dims[1] * dims[2]),
          static_cast<int>(planeIndex) % dims[1],
          static_cast<int>(planeIndex) / dims[1] % dims[2]};
      ++planeIndex;
      check(plane.at("tcz").get<std::array<int, 3>>() == expectedPosition,
            label + ": original TZC plane enumeration changed");
      const bool target = all || plane.at("tcz").get<std::array<int, 3>>() == active;
      if (!target) {
        // This checks the retained upstream target observation, not native stack dispatch.
        check(before == after, label + ": original current-plane operation changed an inactive plane");
        continue;
      }
      ++selected;
      ++filteredPlanes;
      const auto result = rank_filter_imagej(source, method(run.at("method").get<std::string>()), run.at("radius").get<double>(), extent);
      check(sameStorage(source, retained), label + ": rank filtering mutated its input");
      check(result.width == source.width && result.height == source.height && result.channels == 1 &&
                result.type == source.type && result.pixels.size() == source.pixels.size(),
            label + ": result metadata/storage changed");
      for (int y = 0; y < source.height; ++y)
        for (int x = 0; x < source.width; ++x) {
          const auto at = static_cast<std::size_t>(y) * source.width + x;
          if (member(run, bounds, x, y)) {
            ++observedMembers;
            check(sampleEquals(result, at, after), label + ": observed pixel mismatch at " + std::to_string(x) + "," + std::to_string(y));
          } else if (!inside(bounds, x, y)) {
            check(std::memcmp(&result.pixels[at], &source.pixels[at], sizeof(double)) == 0,
                  label + ": rectangle exterior changed bits");
            check(sampleEquals(source, at, after), label + ": original exterior observation changed");
          } else {
            // Mask holes are restored by the command executor after rectangle filtering.
            // This core API deliberately does not promise mask restoration.
            check(sampleEquals(source, at, after), label + ": original runner did not restore mask hole");
          }
        }
    }
    check(selected == (all ? run.at("planes").size() : 1), label + ": active/all-stack target membership changed");
  }
  const std::array<std::size_t, 17> expected{0, 0, 3, 3, 3, 3, 8, 1, 1, 108, 8, 12, 0, 0, 8, 0, 0};
  check(perCase == expected, "Finite scalar K02-K14 source case membership changed");
  check(runIndex == expectedIds.size(), "Observed numerical run count changed");
  check(filteredPlanes == 253 && sourcePlanes == 345 && observedMembers == 10539,
        "Observed filtered/retained plane or true-write member counts changed");
}

void nonbyteVariance() {
  named("variance/nonbyte_pinned_pixels_and_finite_output");
  const auto path = std::filesystem::path(IMAGEJXX_RANK_IMAGEJ_FIXTURE).parent_path() / "nonbyte-variance-imagej154r.json";
  std::ifstream file(path);
  check(bool(file), "Nonbyte variance fixture missing");
  const auto fixture = Json::parse(file);
  check(fixture.at("imagej_commit") == "62db90a269cccc1ac79f658e4602043214ed055b", "Wrong variance reference commit");
  check(fixture.at("cases").size() == 504, "Nonbyte variance matrix changed");
  std::size_t overflow = 0;
  for (const auto &row : fixture.at("cases")) {
    const bool floating = row.at("dtype") == "float32";
    Image source(row.at("width"), row.at("height"), 1, floating ? PixelType::Float32 : PixelType::UInt16);
    bool nonfinite = false;
    for (std::size_t i = 0; i < source.pixels.size(); ++i) {
      const auto bits = row.at("input_bits").at(i).get<std::uint32_t>();
      if (floating) { float value; std::memcpy(&value, &bits, 4); source.pixels[i] = double(value); }
      else source.pixels[i] = double(bits);
      if (floating && (row.at("output_bits").at(i).get<std::uint32_t>() & 0x7f800000u) == 0x7f800000u) nonfinite = true;
    }
    const auto saved = source;
    std::optional<RankFilterExtent> extent;
    if (!row.at("extent").is_null()) {
      const auto &e = row.at("extent");extent = RankFilterExtent{e.at(0),e.at(1),e.at(2),e.at(3)};
    }
    if (nonfinite) {
      ++overflow;
      rejects([&] { (void)rank_filter_imagej(source, RankFilterMethod::Variance, row.at("radius"), extent); }, "Float overflow returned nonfinite image");
    } else {
      const auto result = rank_filter_imagej(source, RankFilterMethod::Variance, row.at("radius"), extent);
      check(result.type == source.type, "Variance changed input pixel type");
      for (std::size_t i = 0; i < result.pixels.size(); ++i)
        check((floating ? floatBits(result.pixels[i]) : std::uint32_t(result.pixels[i])) == row.at("output_bits").at(i).get<std::uint32_t>(), "Nonbyte variance differs from observed ImageJ bits");
    }
    check(sameStorage(source, saved), "Variance changed caller source storage");
  }
  check(overflow == 42, "Expected float overflow matrix not exercised");
  for (const auto type : {PixelType::UInt16, PixelType::Float32}) {
    Image constant(3, 4, 1, type);std::fill(constant.pixels.begin(),constant.pixels.end(),77.0);
    const auto result = rank_filter_imagej(constant,RankFilterMethod::Variance,2.85);
    check(std::all_of(result.pixels.begin(),result.pixels.end(),[](double v){return v==0.0;}), "Constant variance not zero");
    check(sameStorage(rank_filter_imagej(constant,RankFilterMethod::Variance,1000,RankFilterExtent{3,4,0,0}),constant), "Empty nonbyte variance extent changed storage");
  }
}
Image validSource(PixelType type = PixelType::UInt16) {
  Image source(9, 7, 1, type);
  for (std::size_t index = 0; index < source.pixels.size(); ++index) source.pixels[index] = static_cast<double>(index);
  return source;
}
void emptyAndDefault() {
  named("api/empty_extent_and_default");
  for (const auto type : {PixelType::UInt8, PixelType::UInt16, PixelType::Float32}) {
    auto source = validSource(type);
    if (type == PixelType::Float32) source.pixels[0] = -0.0;
    for (const auto extent : {RankFilterExtent{9, 7, 0, 0}, {4, 3, 0, 4}, {4, 3, 5, 0}})
      for (const auto operation : {RankFilterMethod::Minimum, RankFilterMethod::Maximum, RankFilterMethod::Median, RankFilterMethod::Mean})
        check(sameStorage(rank_filter_imagej(source, operation, 2, extent), source), "Valid empty extent changed source bits");
    check(sameStorage(rank_filter_imagej(source), rank_filter_imagej(source, RankFilterMethod::Median, 2)),
          "Public default is not median radius2");
    check(sameStorage(rank_filter_imagej(source, RankFilterMethod::Mean, 0), rank_filter_imagej(source, RankFilterMethod::Mean, -0.0)),
          "Equivalent signed-zero radii differ");
  }
  const auto source = validSource();
  check(!sameStorage(rank_filter_imagej(source, RankFilterMethod::Maximum, 0), source), "Radius0 was incorrectly treated as identity");
  for (const auto operation : {RankFilterMethod::Minimum, RankFilterMethod::Maximum, RankFilterMethod::Mean})
    check(sameStorage(rank_filter_imagej(source, operation, 1000, RankFilterExtent{9, 7, 0, 0}), source),
          "Valid nonmedian radius upper bound was rejected");
  check(sameStorage(rank_filter_imagej(source, RankFilterMethod::Median, 100, RankFilterExtent{9, 7, 0, 0}), source),
        "Valid median radius upper bound was rejected");
}
void validation() {
  const auto source = validSource();
  named("api/radius_and_method_validation");
  for (const auto radius : {-1.0, std::numeric_limits<double>::quiet_NaN(), std::numeric_limits<double>::infinity(),
                            -std::numeric_limits<double>::infinity(), 1000.000001, std::numeric_limits<double>::max()}) {
    rejects([&] { (void)rank_filter_kernel_imagej(radius); }, "Invalid public kernel radius accepted");
    rejects([&] { (void)rank_filter_imagej(source, RankFilterMethod::Mean, radius); }, "Invalid filter radius accepted");
    rejects([&] { (void)rank_filter_imagej(source, RankFilterMethod::Mean, radius, RankFilterExtent{9, 7, 0, 0}); },
            "Empty extent bypassed radius validation");
  }
  rejects([&] { (void)rank_filter_imagej(source, RankFilterMethod::Median, 100.000001); }, "Median radius limit accepted");
  rejects([&] { (void)rank_filter_imagej(source, static_cast<RankFilterMethod>(-1)); }, "Invalid negative method accepted");
  rejects([&] { (void)rank_filter_imagej(source, static_cast<RankFilterMethod>(99)); }, "Invalid upper method accepted");
  const std::array<RankFilterMethod, 4> methods{RankFilterMethod::Minimum, RankFilterMethod::Maximum, RankFilterMethod::Median, RankFilterMethod::Mean};
  const std::array<std::string, 4> names{"minimum", "maximum", "median", "mean"};
  for (std::size_t index = 0; index < names.size(); ++index)
    check(parse_rank_filter_method(names[index]) == methods[index] && rank_filter_method_name(methods[index]) == names[index],
          "Public lowercase rank method roundtrip changed");
  for (const auto &name : {"", "Mean", "mean ", "min", "maximum.extra"})
    rejects([&] { (void)parse_rank_filter_method(name); }, "Unknown/nonexact method string accepted");
  rejects([&] { (void)rank_filter_method_name(static_cast<RankFilterMethod>(-1)); }, "Invalid method enum name accepted");
  named("api/extent_validation");
  for (const auto extent : {RankFilterExtent{-1, 0, 1, 1}, {0, -1, 1, 1}, {0, 0, -1, 1}, {0, 0, 1, -1},
                            {9, 0, 1, 1}, {0, 7, 1, 1}, {10, 0, 0, 0}, {0, 8, 0, 0},
                            {1, 1, std::numeric_limits<int>::max(), 1}, {std::numeric_limits<int>::max(), 0, 1, 1}})
    rejects([&] { (void)rank_filter_imagej(source, RankFilterMethod::Median, 2, extent); }, "Invalid/overflowing rectangle accepted");
  named("api/metadata_validation");
  for (int variant = 0; variant < 8; ++variant) {
    auto malformed = source;
    if (variant == 0) malformed.width = 0;
    if (variant == 1) malformed.height = -1;
    if (variant == 2) malformed.channels = 0;
    if (variant == 3) malformed.channels = 3;
    if (variant == 4) malformed.pixels.pop_back();
    if (variant == 5) malformed.pixels.push_back(0);
    if (variant == 6) malformed.type = static_cast<PixelType>(-1);
    if (variant == 7) malformed.width = std::numeric_limits<int>::max();
    rejects([&] { (void)rank_filter_imagej(malformed); }, "Malformed image metadata/storage accepted");
  }
  for (const auto type : {PixelType::UInt8, PixelType::UInt16, PixelType::Float32})
    rejects([&] { (void)rank_filter_imagej(Image(2, 2, 3, type)); }, "Multichannel image entered scalar profile");
  rejects([&] { (void)rank_filter_imagej(Image(2, 2, 1, PixelType::Float64)); }, "Float64 image entered finite scalar S1 profile");
  named("api/canonical_sample_validation");
  for (const auto type : {PixelType::UInt8, PixelType::UInt16})
    for (const auto value : {-0.0, -1.0, 0.5, type_max(type) + 1, std::numeric_limits<double>::quiet_NaN(),
                             std::numeric_limits<double>::infinity()}) {
      auto malformed = validSource(type);
      malformed.pixels.back() = value;
      rejects([&] { (void)rank_filter_imagej(malformed); }, "Noncanonical integer sample accepted");
      rejects([&] { (void)rank_filter_imagej(malformed, RankFilterMethod::Median, 2, RankFilterExtent{9, 7, 0, 0}); },
              "Empty extent bypassed full source canonical validation");
    }
  for (const auto value : {std::numeric_limits<double>::quiet_NaN(), std::numeric_limits<double>::infinity(),
                           -std::numeric_limits<double>::infinity(), std::numeric_limits<double>::max(),
                           std::numeric_limits<double>::denorm_min(), 1.0 / 3.0}) {
    auto malformed = validSource(PixelType::Float32);
    malformed.pixels.back() = value;
    rejects([&] { (void)rank_filter_imagej(malformed); }, "Noncanonical/nonfinite Float32 sample accepted");
    rejects([&] { (void)rank_filter_imagej(malformed, RankFilterMethod::Median, 2, RankFilterExtent{9, 7, 0, 0}); },
            "Empty extent bypassed full Float32 canonical validation");
  }
  auto finite = validSource(PixelType::Float32);
  finite.pixels[0] = -0.0;
  finite.pixels[1] = static_cast<double>(std::numeric_limits<float>::denorm_min());
  finite.pixels[2] = static_cast<double>(std::numeric_limits<float>::max());
  check(sameStorage(rank_filter_imagej(finite, RankFilterMethod::Median, 2, RankFilterExtent{9, 7, 0, 0}), finite),
        "Canonical Float32 zero/subnormal/maximum were not preserved by empty extent");
}
} // namespace

int main() {
  try {
    std::ifstream file(IMAGEJXX_RANK_IMAGEJ_FIXTURE, std::ios::binary | std::ios::ate);
    check(bool(file) && file.tellg() == std::streampos(311070), "Retained 311070-byte ImageJ rank fixture is missing or replaced");
    file.seekg(0);
    const auto fixture = Json::parse(file);
    fixtureIdentity(fixture);
    geometry(fixture);
    observedRuns(fixture);
    emptyAndDefault();
    validation();
    nonbyteVariance();
    for (const auto &name : cases) std::cout << "[PASS] " << name << '\n';
    std::cout << "Rank ImageJ numerical: " << cases.size() << " cases, " << checks
              << " checks, " << filteredPlanes << " filtered planes, " << sourcePlanes
              << " retained source planes, " << observedMembers << " observed members; passed\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "Rank ImageJ numerical failed: " << error.what() << '\n';
    return 1;
  }
}
