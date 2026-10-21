// Independent scalar codec checks. SOURCE PREPARATION ONLY until root adopts.
// Link only the static core; add src/native and the reviewed JSON include as
// PRIVATE test include directories. No installed public header is added.
#include "session_metadata.hpp"
#include <algorithm>
#include <array>
#include <climits>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <limits>
#include <new>
#include <stdexcept>
#include <string>
#include <vector>
#if defined(_MSC_VER)
#include <malloc.h>
#endif

// Test-executable-only allocation instrumentation. No allocation occurs while
// recording; product code never sees or calls these counters. All ordinary,
// array, sized, aligned and nothrow new/delete forms are covered. The guard is
// thread-local and used around a synchronous scalar call on this thread only.
namespace allocation_probe {
struct Stats {
  std::size_t calls = 0, bytes = 0, maximum = 0, denied = 0;
};
thread_local Stats *active = nullptr;
constexpr std::size_t MaxOne = 64 * 1024;
constexpr std::size_t MaxTotal = 512 * 1024;
void request(std::size_t bytes) {
  if (!active)
    return;
  ++active->calls;
  active->maximum = std::max(active->maximum, bytes);
  if (bytes > MaxOne || bytes > MaxTotal - active->bytes) {
    ++active->denied;
    throw std::bad_alloc();
  }
  active->bytes += bytes;
}
void *allocate(std::size_t bytes) {
  bytes = std::max<std::size_t>(bytes, 1);
  request(bytes);
  if (auto memory = std::malloc(bytes))
    return memory;
  throw std::bad_alloc();
}
void *aligned(std::size_t bytes, std::size_t alignment) {
  bytes = std::max<std::size_t>(bytes, 1);
  request(bytes);
#if defined(_MSC_VER)
  if (auto memory = _aligned_malloc(bytes, alignment))
    return memory;
#else
  if (bytes > std::numeric_limits<std::size_t>::max() - alignment + 1)
    throw std::bad_alloc();
  const auto rounded = (bytes + alignment - 1) / alignment * alignment;
  if (auto memory = std::aligned_alloc(alignment, rounded))
    return memory;
#endif
  throw std::bad_alloc();
}
void releaseAligned(void *memory) noexcept {
#if defined(_MSC_VER)
  _aligned_free(memory);
#else
  std::free(memory);
#endif
}
struct Guard {
  Stats stats;
  Guard() {
    if (active)
      throw std::logic_error("Nested allocation guard");
    active = &stats;
  }
  ~Guard() { active = nullptr; }
  Stats finish() {
    active = nullptr;
    return stats;
  }
};
} // namespace allocation_probe
void *operator new(std::size_t bytes) { return allocation_probe::allocate(bytes); }
void *operator new[](std::size_t bytes) { return allocation_probe::allocate(bytes); }
void operator delete(void *memory) noexcept { std::free(memory); }
void operator delete[](void *memory) noexcept { std::free(memory); }
void operator delete(void *memory, std::size_t) noexcept { std::free(memory); }
void operator delete[](void *memory, std::size_t) noexcept { std::free(memory); }
void *operator new(std::size_t bytes, const std::nothrow_t &) noexcept {
  try { return ::operator new(bytes); } catch (...) { return nullptr; }
}
void *operator new[](std::size_t bytes, const std::nothrow_t &) noexcept {
  try { return ::operator new[](bytes); } catch (...) { return nullptr; }
}
void operator delete(void *memory, const std::nothrow_t &) noexcept { std::free(memory); }
void operator delete[](void *memory, const std::nothrow_t &) noexcept { std::free(memory); }
void *operator new(std::size_t bytes, std::align_val_t alignment) {
  return allocation_probe::aligned(bytes, static_cast<std::size_t>(alignment));
}
void *operator new[](std::size_t bytes, std::align_val_t alignment) {
  return allocation_probe::aligned(bytes, static_cast<std::size_t>(alignment));
}
void operator delete(void *memory, std::align_val_t) noexcept {
  allocation_probe::releaseAligned(memory);
}
void operator delete[](void *memory, std::align_val_t) noexcept {
  allocation_probe::releaseAligned(memory);
}
void operator delete(void *memory, std::size_t, std::align_val_t) noexcept {
  allocation_probe::releaseAligned(memory);
}
void operator delete[](void *memory, std::size_t, std::align_val_t) noexcept {
  allocation_probe::releaseAligned(memory);
}
void *operator new(std::size_t bytes, std::align_val_t alignment,
                   const std::nothrow_t &) noexcept {
  try { return ::operator new(bytes, alignment); } catch (...) { return nullptr; }
}
void *operator new[](std::size_t bytes, std::align_val_t alignment,
                     const std::nothrow_t &) noexcept {
  try { return ::operator new[](bytes, alignment); } catch (...) { return nullptr; }
}
void operator delete(void *memory, std::align_val_t,
                     const std::nothrow_t &) noexcept {
  allocation_probe::releaseAligned(memory);
}
void operator delete[](void *memory, std::align_val_t,
                       const std::nothrow_t &) noexcept {
  allocation_probe::releaseAligned(memory);
}

using namespace imagejxx;
namespace codec = imagejxx::detail::session_metadata;
using Json = codec::Json;
namespace {
void check(bool condition, const char *message) {
  if (!condition)
    throw std::runtime_error(message);
}
template <class F> void rejects(F function, const char *message) {
  bool rejected = false;
  try { function(); } catch (const std::invalid_argument &) { rejected = true; }
  check(rejected, message);
}
std::uint64_t bits(double value) {
  static_assert(sizeof(double) == sizeof(std::uint64_t), "64-bit double required");
  std::uint64_t result;
  std::memcpy(&result, &value, sizeof(result));
  return result;
}
// JSON equality intentionally distinguishes integer / float / bool and double
// sign bits. Signed/unsigned positive JSON storage tags are not a wire claim.
bool exact(const Json &a, const Json &b) {
  if (a.is_number() || b.is_number()) {
    if (a.is_number_float() != b.is_number_float() ||
        a.is_number_integer() != b.is_number_integer())
      return false;
    if (a.is_number_float())
      return bits(a.get<double>()) == bits(b.get<double>());
    return a == b;
  }
  if (a.type() != b.type() || a.size() != b.size())
    return false;
  if (a.is_object()) {
    for (auto it = a.begin(); it != a.end(); ++it)
      if (!b.contains(it.key()) || !exact(it.value(), b.at(it.key())))
        return false;
    return true;
  }
  if (a.is_array()) {
    for (std::size_t i = 0; i != a.size(); ++i)
      if (!exact(a[i], b[i]))
        return false;
    return true;
  }
  return a == b;
}
Json literalDefault(bool stack) {
  return {
    {"metadata", {
      {"title", "Untitled"}, {"properties", Json::object()},
      {"extensions", {{"is_stack", stack}, {"labels", false}}},
      {"record_layout", {
        {"root_fields", {"calibration", "overlays", "rois", "title"}},
        {"null_fields", Json::array()},
        {"calibration_fields", {"frame_interval", "pixel_depth", "pixel_height",
                                "pixel_width", "time_unit", "unit"}},
        {"roi_slice_fields", Json::array()}}}}},
    {"display_properties", {{"lut", "Grays"}}}};
}
Json project(const std::string &input, int planes = 1) {
  return Json::parse(codec::legacy_session_state_json(input, planes));
}
void converters() {
  check(std::holds_alternative<std::monostate>(codec::from_json(nullptr)), "null tag");
  check(std::holds_alternative<bool>(codec::from_json(false)), "bool tag");
  check(std::holds_alternative<std::int64_t>(codec::from_json(Json::parse("17"))),
        "Small positive JSON integer remains established signed-first");
  check(std::get<std::int64_t>(codec::from_json(Json::parse("-9223372036854775808"))) ==
            std::numeric_limits<std::int64_t>::min(), "Exact signed minimum");
  check(std::get<std::uint64_t>(codec::from_json(Json::parse("18446744073709551615"))) ==
            std::numeric_limits<std::uint64_t>::max(), "Exact unsigned maximum");
  check(std::holds_alternative<double>(codec::from_json(17.0)), "Double tag");
  check(bits(std::get<double>(codec::from_json(-0.0))) == UINT64_C(0x8000000000000000),
        "Double signed zero");
  check(std::holds_alternative<std::string>(codec::from_json("text")), "String tag");
  check(std::holds_alternative<std::vector<std::string>>(
            codec::from_json(Json::parse("[\"a\",\"b\"]"))), "Legacy string vector");
  check(std::holds_alternative<std::vector<std::string>>(
            codec::from_json(Json::array())), "Legacy empty-array string vector");
  const auto nested = Json::parse(R"({"empty":{},"array":[null,false,0,0.0,-0.0,{"u":18446744073709551615}],"text":["a","b"]})");
  const auto object = codec::object_from_json(nested);
  check(exact(codec::to_json(object), nested), "Nested scalar roundtrip exact type/bits");
  rejects([&] { codec::object_from_json(Json::array()); }, "Object parser rejects array");
  rejects([&] { codec::from_json(std::numeric_limits<double>::infinity()); },
          "Native nonfinite scalar rejected");
  MetadataValue emptyNode = std::shared_ptr<const MetadataTree>{};
  rejects([&] { codec::to_json(emptyNode); }, "Null metadata tree rejected");
}
void classifications() {
  for (const auto *key : {"pixel_width", "pixel_height", "pixel_depth", "unit",
                          "frame_interval", "time_unit", "origin", "direction", "time_origin"})
    check(codec::calibration_field(key), "Established calibration key missing");
  for (const auto *key : {"title", "calibration", "selection", "previous_selection", "rois",
                          "overlays", "slice_index", "view_state", "display_range", "lut",
                          "custom_lut", "native_properties", "spectrum_shape", "pixel_sample_policy"})
    check(codec::document_field(key), "Established document key missing");
  for (const auto *key : {"", "is_stack", "labels", "record_layout", "native_document_state",
                          "pixel_widths", "vendor", "spectrum", "shape"})
    check(!codec::document_field(key), "Opaque root accidentally reserved");
  for (const auto *key : {"", "intensity", "vendor", "pixel_widths", "title"})
    check(!codec::calibration_field(key), "Opaque calibration accidentally reserved");
}
void defaultsAndOpaque() {
  check(exact(project("{}"), literalDefault(false)), "Independent default one-plane literal");
  check(exact(project("{}", 2), literalDefault(true)), "Independent default stack literal");
  const auto result = project(R"({"title":null,"native_properties":{"integer":1,"float":1.0,"negative_zero":-0.0,"max":18446744073709551615},"selection":null,"rois":null,"overlays":null,"calibration":{"pixel_width":0.5,"vendor":{"raw":[true,null,-0.0]}},"record_layout":{"root_fields":false},"":"opaque"})");
  const auto &metadata = result.at("metadata");
  check(metadata.at("title") == "Untitled", "Null title fallback");
  check(exact(metadata.at("properties"), Json::parse(R"({"integer":1,"float":1.0,"negative_zero":-0.0,"max":18446744073709551615})")), "Full opaque properties");
  check(exact(metadata.at("extensions").at("calibration"), Json::parse(R"({"vendor":{"raw":[true,null,-0.0]}})")), "Calibration unknown fields retained");
  check(metadata.at("extensions").at("record_layout") == Json{{"root_fields", false}},
        "Input record_layout remains extension rather than DTO layout");
  check(metadata.at("extensions").at("") == "opaque", "Empty extension key retained");
  check(metadata.at("record_layout").at("null_fields") ==
            Json({"overlays", "rois", "selection", "title"}), "Ordered explicit null roots");
  check(metadata.at("record_layout").at("calibration_fields") == Json({"pixel_width"}),
        "Missing calibration fields are not invented in layout");
  const auto rois = project(R"({"rois":[{}, {"slice_index":null}, {"slice_index":1}],"slice_index":1})", 2);
  check(rois["metadata"]["record_layout"]["roi_slice_fields"] == Json({"1", "2"}),
        "Absent/null/present ROI slice presence is ordered string indices");
}
void sourceAware() {
  auto original = codec::initial_metadata(2, "Original");
  original["selection"] = nullptr;
  original["previous_selection"] = nullptr;
  original["calibration"]["origin"] = {0.0, 0.0, 0.0};
  DocumentMetadata prior;
  prior.title = "Original";
  prior.record_layout = {{"root_fields", std::vector<std::string>{"title"}},
                         {"null_fields", std::vector<std::string>{}},
                         {"calibration_fields", std::vector<std::string>{"unit"}},
                         {"roi_slice_fields", std::vector<std::string>{}}};
  prior.record_layout["opaque_future"] = metadata_object({{"retained_elsewhere", true}});
  auto inferred = codec::infer_metadata(original, &prior, &original);
  check(inferred.extensions.empty(), "Implicit is_stack/labels remain absent after edit");
  auto layout = codec::to_json(inferred.record_layout);
  check(layout["root_fields"] == Json({"title"}) && layout["null_fields"].empty(),
        "Prior absent roots stay inferred");
  check(layout["calibration_fields"] == Json({"unit"}), "Prior calibration field presence retained");
  check(layout.count("opaque_future") == 0 && layout.size() == 4,
        "Existing source-aware legacy inference rebuilds four groups; opaque marked-layout editing is a later gap");
  auto changed = original;
  changed["labels"] = true;
  changed["selection"] = Json::object();
  changed["calibration"]["pixel_width"] = 1.0;
  changed["calibration"]["origin"] = {0.0, -0.0, 0.0};
  inferred = codec::infer_metadata(changed, &prior, &original);
  layout = codec::to_json(inferred.record_layout);
  check(std::get<bool>(inferred.extensions.at("labels")), "Changed implicit extension becomes explicit");
  check(layout["root_fields"] == Json({"selection", "title"}), "Changed absent root enters layout");
  check(layout["calibration_fields"] == Json({"unit"}),
        "Established prior comparison uses numeric JSON equality, not float tag or signed zero");
  prior.extensions["is_stack"] = true;
  inferred = codec::infer_metadata(original, &prior, &original);
  check(inferred.extensions.count("is_stack") == 1, "Prior explicit default extension retained");
  prior.record_layout.clear();
  inferred = codec::infer_metadata(changed, &prior, &original);
  check(inferred.record_layout.empty(), "Native empty-layout owner remains empty-layout");
  rejects([&] { codec::infer_metadata(original, &prior); }, "Incomplete prior pair rejected");
  rejects([&] { codec::infer_metadata(original, nullptr, &original); }, "Incomplete prior wire pair rejected");
}
void displayPresence() {
  check(codec::display_properties(Json::object()).empty(), "Direct extraction does not add defaults");
  const auto fields = codec::display_properties(Json::parse(R"({"display_range":null,"lut":null,"custom_lut":null,"vendor":7})"));
  check(exact(codec::to_json(fields), Json::parse(R"({"display_range":null,"lut":null,"custom_lut":null})")),
        "All three supported display keys preserve explicit null presence");
  check(exact(project(R"({"lut":null})")["display_properties"], Json{{"lut", nullptr}}),
        "Explicit null overrides the legacy LUT default");
  check(exact(project(R"({"display_range":[-2,9.5],"lut":"Fire","custom_lut":null})")["display_properties"],
              Json::parse(R"({"display_range":[-2,9.5],"lut":"Fire","custom_lut":null})")),
        "Display keys preserve numeric type and value");
}
void privateParser() {
  for (const auto *text : {"[]", "null", "false", "1", "\"text\"", "{", "{} trailing",
                           "{\"vendor\":1,\"vendor\":2}",
                           "{\"native_properties\":{\"id\":1,\"id\":2}}",
                           "{\"x\":1,\"\\u0078\":2}",
                           "{\"vendor\":NaN}", "{\"vendor\":Infinity}",
                           "{\"vendor\":1e400}", "{\"vendor\":-9223372036854775809}",
                           "{\"vendor\":18446744073709551616}",
                           "{\"title\":true}", "{\"native_properties\":[]}",
                           "{\"native_properties\":{\"\":1}}", "{\"rois\":{}}"})
    rejects([&] { codec::legacy_session_state_json(text, 1); }, "Private malformed/domain JSON accepted");
  for (const auto *key : {"pixels", "dtype", "shape", "spectrum", "fft_shape", "spectrum_shape", "pixel_sample_policy"}) {
    const auto text = Json{{key, nullptr}}.dump();
    rejects([&] { codec::legacy_session_state_json(text, 1); }, "Root pixel transport accepted");
  }
  rejects([&] { codec::legacy_session_state_json("{}", 0); }, "Zero plane count accepted");
  rejects([&] { codec::legacy_session_state_json("{}", -1); }, "Negative plane count accepted");
  // Preserve n20's original130 arrays rejection, not a normalized fixture.
  const auto originalN20 = "{\"vendor\":" + std::string(130, '[') + "0" + std::string(130, ']') + "}";
  rejects([&] { codec::legacy_session_state_json(originalN20, 1); }, "Original n20 excessive value depth accepted");
  rejects([&] { codec::legacy_session_state_json("{\"vendor\":\"\\ud800\"}", 1); }, "Unpaired surrogate accepted");
  const std::string invalidUtf8 = std::string("{\"vendor\":\"") + char(0xc0) + char(0xaf) + "\"}";
  rejects([&] { codec::legacy_session_state_json(invalidUtf8, 1); }, "Malformed UTF8 accepted");
  const auto opaque = project(R"({"vendor":{"pixels":"opaque","dtype":"opaque","shape":[1,2]},"view_state":{"shape":[3],"pixels":"ignored"}})");
  check(opaque["metadata"]["extensions"]["vendor"]["pixels"] == "opaque", "Nested transport spelling is opaque metadata");
  check(opaque["metadata"]["extensions"].count("view_state") == 0, "View is excluded from scalar metadata projection");
}
// Private-only scalar display admission follows the original pure desktop
// number validator: null->0, Boolean/text reject, fractional finite values stay
// valid. Types and null/sign bits must be retained rather than normalized.
void scalarDisplayAdmission() {
  rejects([&] { codec::legacy_session_state_json(R"({"lut":false})", 1); },
          "n17 private display LUT type accepted");
  rejects([&] { codec::legacy_session_state_json(R"({"display_range":[2,1]})", 1); },
          "n18 private display range order accepted");
  for (const auto *text : {R"({"display_range":[]})", R"({"display_range":[0]})",
                           R"({"display_range":[0,1,2]})",
                           R"({"display_range":[null,null]})", R"({"display_range":[false,1]})",
                           R"({"display_range":[0,"1"]})", R"({"custom_lut":[]})"})
    rejects([&] { codec::legacy_session_state_json(text, 1); }, "Private scalar display domain accepted");
  for (const auto *text : {R"({"display_range":[0,0]})", R"({"display_range":[7,7]})",
                           R"({"lut":""})", R"({"lut":"unknown vendor name"})",
                           R"({"display_range":[null,1]})", R"({"display_range":[-1,null]})",
                           R"({"display_range":[-0.0,0.25],"lut":null,"custom_lut":null})"}) {
    const auto input = Json::parse(text);
    const auto output = project(text).at("display_properties");
    for (auto it = input.begin(); it != input.end(); ++it)
      check(output.contains(it.key()) && exact(output.at(it.key()), it.value()),
            "Private scalar display values must not be normalized");
  }
  Json colors = Json::array();
  for (int i = 0; i < 256; ++i)
    colors.push_back(Json::array({nullptr, -0.0, i == 255 ? 255.0 : 0.25}));
  check(exact(project(Json{{"custom_lut", colors}}.dump())["display_properties"]["custom_lut"], colors),
        "Null/fractional/signed-zero custom colors retain exact representation");
  for (const auto &bad : {Json(true), Json("1"), Json(-0.25), Json(255.25)}) {
    auto changed = colors;
    changed[7][1] = bad;
    const auto text = Json{{"custom_lut", changed}}.dump();
    rejects([&] { codec::legacy_session_state_json(text, 1); }, "Invalid custom color accepted");
  }
  auto badRows = colors;
  badRows.push_back(Json::array({0, 0, 0}));
  auto text = Json{{"custom_lut", badRows}}.dump();
  rejects([&] { codec::legacy_session_state_json(text, 1); }, "257-row custom LUT accepted");
  badRows = colors;
  badRows[3] = Json::array({0, 1});
  text = Json{{"custom_lut", badRows}}.dump();
  rejects([&] { codec::legacy_session_state_json(text, 1); }, "Two-entry custom LUT row accepted");
}
struct DepthCase { const char *context; int allowedArrays; };
constexpr std::array<DepthCase, 9> DepthCases{{
  {"root_extension", 128}, {"native_properties", 127}, {"calibration_extra", 127},
  {"selection_attribute", 128}, {"previous_selection_attribute", 128},
  {"roi_attribute", 128}, {"roi_selection_attribute", 128},
  {"overlay_object", 127}, {"opaque_context_lookalike", 124}}};
std::string depthInput(const std::string &context, int arrays) {
  const auto value = std::string(static_cast<std::size_t>(arrays), '[') + "0" +
                     std::string(static_cast<std::size_t>(arrays), ']');
  if (context == "root_extension")
    return "{\"vendor\":" + value + "}";
  if (context == "native_properties")
    return "{\"native_properties\":{\"vendor\":" + value + "}}";
  if (context == "calibration_extra")
    return "{\"calibration\":{\"pixel_width\":1,\"pixel_height\":1,\"pixel_depth\":1,\"unit\":\"px\",\"frame_interval\":1,\"time_unit\":\"s\",\"vendor\":" + value + "}}";
  if (context == "selection_attribute" || context == "previous_selection_attribute")
    return "{\"" + std::string(context == "selection_attribute" ? "selection" : "previous_selection") +
           "\":{\"kind\":\"point\",\"points\":[[0,0]],\"width\":1,\"vendor\":" + value + "}}";
  if (context == "roi_attribute")
    return "{\"rois\":[{\"name\":\"r\",\"selection\":{\"kind\":\"point\",\"points\":[[0,0]],\"width\":1},\"vendor\":" + value + "}]}";
  if (context == "roi_selection_attribute")
    return "{\"rois\":[{\"name\":\"r\",\"selection\":{\"kind\":\"point\",\"points\":[[0,0]],\"width\":1,\"vendor\":" + value + "}}]}";
  if (context == "overlay_object")
    return "{\"overlays\":[{\"kind\":\"text\",\"text\":\"opaque\",\"points\":[[0,0]],\"vendor\":" + value + "}]}";
  if (context == "opaque_context_lookalike")
    return "{\"vendor\":{\"rois\":[{\"selection\":{\"calibration\":" + value + "}}]}}";
  throw std::logic_error("Unknown independently frozen depth context");
}
void depthBoundaries() {
  for (const auto &fixture : DepthCases) {
    const auto text = depthInput(fixture.context, fixture.allowedArrays);
    const auto input = Json::parse(text);
    const auto result = project(text);
    check(result["metadata"]["title"] == "Untitled", "Depth fixture title/default changed");
    const auto context = std::string(fixture.context);
    if (context == "root_extension" || context == "opaque_context_lookalike")
      check(exact(result["metadata"]["extensions"]["vendor"], input["vendor"]), "Deep opaque extension changed");
    else if (context == "native_properties")
      check(exact(result["metadata"]["properties"]["vendor"], input["native_properties"]["vendor"]), "Deep native properties changed");
    else if (context == "calibration_extra")
      check(exact(result["metadata"]["extensions"]["calibration"]["vendor"], input["calibration"]["vendor"]), "Deep calibration extra changed");
    // Annotation values are not projection output: these checks certify only
    // compatibility of admitted value-depth paths, not geometry preservation.
    const auto tooDeep = depthInput(fixture.context, fixture.allowedArrays + 1);
    rejects([&] { codec::legacy_session_state_json(tooDeep, 1); }, "Source-faithful value depth overflow accepted");
    check(exact(project("{}"), literalDefault(false)), "Depth failure contaminated subsequent scalar projection");
  }
}
void allocationCounts() {
  // Warm library lazy initialization OUTSIDE the measurement. Then measure
  // the complete helper, including parser, inference and returned JSON string.
  const std::string input = "{}";
  codec::legacy_session_state_json(input, 2);
  allocation_probe::Stats baseline;
  for (const int count : {1, 2, 1048576, INT32_MAX}) {
    std::string output;
    allocation_probe::Stats observed;
    {
      allocation_probe::Guard guard;
      output = codec::legacy_session_state_json(input, count);
      observed = guard.finish();
    }
    check(observed.calls > 0 && observed.bytes > 0 && observed.denied == 0,
          "Allocation instrumentation must observe successful metadata allocations");
    check(observed.maximum <= allocation_probe::MaxOne && observed.bytes <= allocation_probe::MaxTotal,
          "Minimal scalar projection allocated pixel-scale memory");
    check(exact(Json::parse(output), literalDefault(count > 1)), "Large count changes only inferred is_stack");
    if (count == 2)
      baseline = observed;
    if (count > 2)
      check(observed.calls == baseline.calls && observed.bytes == baseline.bytes &&
                observed.maximum == baseline.maximum,
            "Scalar allocations must be exactly independent of plane count above one");
    std::cout << "allocation-count " << count << " calls=" << observed.calls
              << " bytes=" << observed.bytes << " maximum=" << observed.maximum
              << " denied=" << observed.denied << '\n';
  }
}
struct Group { const char *name; void (*test)(); };
constexpr std::array<Group, 9> Groups{{
  {"converters", converters}, {"classifications", classifications},
  {"defaults_and_opaque", defaultsAndOpaque}, {"source_aware", sourceAware},
  {"display_presence", displayPresence}, {"private_parser", privateParser},
  {"scalar_display_admission", scalarDisplayAdmission}, {"depth_boundaries", depthBoundaries},
  {"allocation_counts", allocationCounts}}};
} // namespace
int main(int argc, char **argv) {
  try {
    if (argc > 2)
      throw std::invalid_argument("Expected zero arguments or one exact group name");
    bool matched = false;
    for (const auto &group : Groups)
      if (argc == 1 || std::string(argv[1]) == group.name) {
        matched = true;
        group.test();
        std::cout << "passed session_metadata." << group.name << '\n';
      }
    check(matched, "Unknown native scalar test group");
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "session_metadata assertion failure: " << error.what() << '\n';
    return 1;
  }
}
