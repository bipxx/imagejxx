#include <imagejxx/channel_display.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <cstdint>
#include <functional>
#include <iostream>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

using namespace imagejxx;
namespace {
std::size_t checks = 0;
std::string context;
void check(bool value, const char *message) {
  ++checks;
  if (!value) throw std::runtime_error(context + ": " + message);
}
template<class Error = std::invalid_argument, class Function>
void rejects(Function function, const char *message) {
  bool refused = false;
  try { function(); } catch (const Error &) { refused = true; }
  check(refused, message);
}
MetadataObject object(const MetadataValue &value) {
  return std::get<MetadataObject>(std::get<std::shared_ptr<const MetadataTree>>(value)->value);
}
MetadataArray array(const MetadataValue &value) {
  return std::get<MetadataArray>(std::get<std::shared_ptr<const MetadataTree>>(value)->value);
}
MetadataObject record(int count = 3, const std::string &mode = "composite") {
  MetadataArray channels;
  const std::array<std::string, 8> names{"Red", "Green", "Blue", "White", "Cyan", "Magenta", "Yellow", "White"};
  for (int c = 0; c < count; ++c)
    channels.push_back(metadata_object({{"visible", true}, {"range", metadata_array({0.0, 255.0})},
      {"lut", metadata_object({{"name", names[std::min(c, 7)]}})}}));
  return {{"schema", std::string("imagejxx.channel-display")}, {"version", std::int64_t(1)},
          {"mode", mode}, {"channels", metadata_array(std::move(channels))}};
}
MetadataObject replace_channel(MetadataObject value, int c, const std::string &field,
                               MetadataValue replacement) {
  auto channels = array(value.at("channels"));
  auto channel = object(channels.at(c));
  channel[field] = std::move(replacement);
  channels[c] = metadata_object(std::move(channel));
  value["channels"] = metadata_array(std::move(channels));
  return value;
}
ChannelDisplayState state(int count = 3, ChannelDisplayMode mode = ChannelDisplayMode::Composite) {
  ChannelDisplayState result;
  result.mode = mode;
  const std::array<std::string, 8> names{"Red", "Green", "Blue", "White", "Cyan", "Magenta", "Yellow", "White"};
  for (int c = 0; c < count; ++c) {
    ChannelDisplayChannel channel;
    channel.lut.name = names[std::min(c, 7)];
    result.channels.push_back(channel);
  }
  return result;
}
Dataset row(PixelType type, std::vector<double> values,
            FloatingSamplePolicy policy = FloatingSamplePolicy::FiniteOnly) {
  const auto width = static_cast<int>(values.size());
  Image image(width, 1, 1, type);
  image.pixels = std::move(values);
  Stack planes;
  planes.push_back(std::move(image));
  return Dataset::from_planes({1, 1, 1, 1, width, 1}, std::move(planes), {}, policy);
}
std::uint64_t double_bits(double value) {
  std::uint64_t bits;
  std::memcpy(&bits, &value, sizeof bits);
  return bits;
}
double f32(std::uint32_t bits) {
  float value;
  std::memcpy(&value, &bits, sizeof bits);
  return static_cast<double>(value);
}
std::vector<std::uint64_t> fingerprint(const Dataset &dataset) {
  std::vector<std::uint64_t> result;
  const auto shape = dataset.shape();
  for (int t = 0; t < shape.t; ++t)
    for (int c = 0; c < shape.c; ++c)
      for (int z = 0; z < shape.z; ++z) {
        const auto plane = dataset.plane({t, c, z});
        for (double value : plane->pixels)
          result.push_back(double_bits(value));
      }
  return result;
}
void gray(const Image &image, const std::vector<int> &expected) {
  check(image.width == static_cast<int>(expected.size()) && image.height == 1 &&
        image.channels == 3 && image.type == PixelType::UInt8, "Mapped row lost RGB/U8 output geometry");
  for (std::size_t x = 0; x < expected.size(); ++x)
    for (int component = 0; component < 3; ++component)
      check(image.at(static_cast<int>(x), 0, component) == expected[x], "Mapped processor byte differs from literal reference");
}
void rgb(const Image &image, const std::vector<std::array<int, 3>> &expected) {
  check(image.width == static_cast<int>(expected.size()) && image.height == 1 &&
        image.channels == 3 && image.type == PixelType::UInt8, "Composed row lost RGB/U8 output geometry");
  for (std::size_t x = 0; x < expected.size(); ++x)
    for (int component = 0; component < 3; ++component)
      check(image.at(static_cast<int>(x), 0, component) == expected[x][component], "Composed RGB differs from independent literal");
}
class UnreadSource final : public PlaneSource {
public:
  explicit UnreadSource(DatasetShape shape = {2, 3, 4, 5, 7, 1},
                        PixelType type = PixelType::UInt8) : sizes(shape), type(type) {}
  DatasetShape shape() const override { return sizes; }
  PixelType pixel_type() const override { return type; }
  std::shared_ptr<const Image> read_plane(PlanePosition) const override {
    ++reads;
    throw std::runtime_error("Metadata-only path read source pixels");
  }
  Image read_region(PlanePosition, int, int, int, int) const override {
    ++regions;
    throw std::runtime_error("Metadata-only path read source region");
  }
  DatasetShape sizes;
  PixelType type;
  mutable int reads = 0, regions = 0;
};
class FormulaSource final : public PlaneSource {
public:
  explicit FormulaSource(PixelType type = PixelType::Float32) : type(type) {}
  DatasetShape shape() const override { return {2, 3, 4, 5, 7, 1}; }
  PixelType pixel_type() const override { return type; }
  std::shared_ptr<const Image> read_plane(PlanePosition position) const override {
    positions.push_back(position);
    auto image = std::make_shared<Image>(7, 5, 1, type);
    for (int y = 0; y < 5; ++y)
      for (int x = 0; x < 7; ++x)
        image->at(x, y) = 70 * position.t + 50 * position.c + 8 * position.z + 2 * y + x;
    return image;
  }
  PixelType type;
  mutable std::vector<PlanePosition> positions;
};

void state_cases() {
  context = "strict state";
  const DatasetShape shape{2, 3, 4, 5, 7, 1};
  const auto finite = FloatingSamplePolicy::FiniteOnly;
  const auto original = record();
  const auto parsed = parse_channel_display(original, shape, PixelType::UInt8, finite);
  check(parsed.mode == ChannelDisplayMode::Composite && parsed.channels.size() == 3, "Canonical state did not parse");
  check(metadata_equal(encode_channel_display(parsed, shape, PixelType::UInt8, finite), original), "Canonical state round-trip changed fields or scalar types");
  for (const auto &mode : std::array<std::string, 3>{"color", "grayscale", "composite"}) {
    auto candidate = record(3, mode);
    check(metadata_equal(encode_channel_display(parse_channel_display(candidate, shape, PixelType::UInt16, finite),
          shape, PixelType::UInt16, finite), candidate), "Mode round-trip changed canonical state");
  }
  for (const auto &key : {"schema", "version", "mode", "channels"}) {
    auto value = original; value.erase(key);
    rejects([&] { parse_channel_display(value, shape, PixelType::UInt8, finite); }, "Missing required top-level field admitted");
  }
  std::vector<MetadataObject> malformed;
  auto add = [&](const char *key, MetadataValue value) { auto r = original; r[key] = std::move(value); malformed.push_back(std::move(r)); };
  add("schema", std::string("unrelated")); add("schema", true);
  add("version", true); add("version", 1.0); add("version", 1.0000000000000002);
  add("version", std::int64_t(0)); add("version", std::uint64_t(37));
  add("version", std::numeric_limits<std::uint64_t>::max());
  add("mode", std::string("SUM")); add("mode", std::string("max")); add("mode", std::string("Color")); add("mode", true);
  add("channels", metadata_array({})); add("channels", std::string("channels")); add("extra", 1);
  auto short_array = array(original.at("channels")); short_array.pop_back(); add("channels", metadata_array(short_array));
  auto long_array = array(original.at("channels")); long_array.push_back(long_array[0]); add("channels", metadata_array(long_array));
  for (const auto &bad : malformed)
    rejects([&] { parse_channel_display(bad, shape, PixelType::UInt8, finite); }, "Malformed state header/count admitted");
  auto unsigned_version = original; unsigned_version["version"] = std::uint64_t(1);
  check(parse_channel_display(unsigned_version, shape, PixelType::UInt8, finite).channels.size() == 3, "Exact unsigned version rejected");
  for (const auto &key : {"visible", "range", "lut"}) {
    auto channels = array(original.at("channels")); auto channel = object(channels[1]); channel.erase(key);
    channels[1] = metadata_object(channel); auto bad = original; bad["channels"] = metadata_array(channels);
    rejects([&] { parse_channel_display(bad, shape, PixelType::UInt8, finite); }, "Missing channel field admitted");
  }
  const std::vector<std::pair<std::string, MetadataValue>> bad_fields{
    {"visible", 1}, {"visible", std::string("true")}, {"range", true},
    {"range", metadata_array({0})}, {"range", metadata_array({0, 1, 2})},
    {"range", metadata_array({false, 255})}, {"range", metadata_array({0, true})},
    {"range", metadata_array({7, 3})}, {"range", metadata_array({std::string("0"), 255})},
    {"range", metadata_array({std::int64_t(9007199254740993LL), std::int64_t(9007199254740994LL)})},
    {"range", metadata_array({0, std::numeric_limits<std::uint64_t>::max()})},
    {"lut", metadata_object({{"name", std::string("Fire")}})},
    {"lut", metadata_object({{"name", std::string("red")}})},
    {"lut", metadata_object({{"name", true}})}, {"lut", metadata_object({})},
    {"lut", metadata_object({{"name", std::string("Red")}, {"extra", 1}})}, {"extra", 1}};
  for (const auto &field : bad_fields) {
    auto bad = replace_channel(original, 1, field.first, field.second);
    rejects([&] { parse_channel_display(bad, shape, PixelType::UInt8, finite); }, "Malformed channel field admitted");
  }
  rejects([&] { parse_channel_display(replace_channel(original, 0, "range", metadata_array({0, std::numeric_limits<double>::infinity()})),
                                      shape, PixelType::UInt8, finite); }, "Nonfinite nested range admitted");
  auto fractional = replace_channel(original, 1, "range", metadata_array({.5, 10.5}));
  check(parse_channel_display(fractional, shape, PixelType::UInt8, finite).channels[1].range == std::array<double, 2>{.5, 10.5}, "Requested fractional endpoints were destroyed by serialization");
  for (double value : {0.0, 7.0}) {
    auto equal = replace_channel(original, 1, "range", metadata_array({value, value}));
    check(parse_channel_display(equal, shape, PixelType::Float32, finite).channels[1].range == std::array<double, 2>{value, value}, "Literal equal interval rejected");
  }
  MetadataArray table;
  for (int i = 0; i < 256; ++i) table.push_back(metadata_array({i, (3 * i) % 256, 255 - i}));
  auto custom = replace_channel(original, 1, "lut", metadata_object({{"table", metadata_array(table)}}));
  check(metadata_equal(encode_channel_display(parse_channel_display(custom, shape, PixelType::UInt8, finite), shape, PixelType::UInt8, finite), custom), "Custom table lost exact RGB bytes");
  for (auto byte : std::vector<MetadataValue>{true, -1, 256, 1.0, 1.25, std::numeric_limits<std::uint64_t>::max()}) {
    auto bad_table = table; bad_table[42] = metadata_array({0, byte, 0});
    auto bad = replace_channel(original, 1, "lut", metadata_object({{"table", metadata_array(bad_table)}}));
    rejects([&] { parse_channel_display(bad, shape, PixelType::UInt8, finite); }, "Invalid/lossy table byte admitted");
  }
  for (int size : {255, 257}) {
    auto bad_table = table; if (size == 255) bad_table.pop_back(); else bad_table.push_back(table[0]);
    auto bad = replace_channel(original, 1, "lut", metadata_object({{"table", metadata_array(bad_table)}}));
    rejects([&] { parse_channel_display(bad, shape, PixelType::UInt8, finite); }, "Wrong table length admitted");
  }
  for (const auto &bad_row : std::vector<MetadataValue>{metadata_array({0,1}), metadata_array({0,1,2,3}), true, std::string("RGB")}) {
    auto bad_table = table; bad_table[42] = bad_row;
    auto bad = replace_channel(original, 1, "lut", metadata_object({{"table", metadata_array(bad_table)}}));
    rejects([&] { parse_channel_display(bad, shape, PixelType::UInt8, finite); }, "Malformed RGB table row admitted");
  }
  auto mixed = replace_channel(original, 1, "lut", metadata_object({{"name", std::string("Red")}, {"table", metadata_array(table)}}));
  rejects([&] { parse_channel_display(mixed, shape, PixelType::UInt8, finite); }, "Mixed named/custom LUT admitted");
  auto typed = state(); typed.mode = static_cast<ChannelDisplayMode>(42);
  rejects([&] { validate_channel_display(typed, shape, PixelType::UInt8, finite); }, "Invalid typed mode admitted");
  typed = state(); typed.channels[0].lut.table = ChannelDisplayTable{};
  rejects([&] { validate_channel_display(typed, shape, PixelType::UInt8, finite); }, "Typed mixed LUT admitted");
  typed = state(); typed.channels[0].range[0] = std::numeric_limits<double>::quiet_NaN();
  rejects([&] { validate_channel_display(typed, shape, PixelType::UInt8, finite); }, "Typed nonfinite endpoint admitted");
  rejects([&] { validate_channel_display(state(), shape, PixelType::Float64, finite); }, "Unsupported Float64 channel state admitted");
  for (int samples : {3, 4}) {
    auto s = shape; s.s = samples;
    rejects([&] { validate_channel_display(state(), s, PixelType::UInt8, finite); }, "Interleaved RGB/RGBA confused with scientific channels");
  }
  rejects([&] { validate_channel_display(state(), shape, PixelType::UInt8, FloatingSamplePolicy::PreserveQuietNaNFloat32); }, "NaN policy admitted on integer type");
  rejects([&] { validate_channel_display(state(), shape, PixelType::UInt8, static_cast<FloatingSamplePolicy>(42)); }, "Invalid sample policy admitted");
  rejects([&] { validate_channel_display(state(), shape, static_cast<PixelType>(42), finite); }, "Invalid pixel-type enum admitted");
  Dataset dataset = row(PixelType::UInt8, {0, 255});
  DisplayState display;
  display.properties = {{"display_range", metadata_array({3, 9})}, {"lut", std::string("Blue")}, {"unrelated", metadata_object({{"nested", true}})}};
  const auto legacy = display.properties;
  check(!recognized_channel_display(display, dataset), "Absent state did not preserve legacy interpretation");
  auto one = state(1, ChannelDisplayMode::Color);
  auto enabled = with_channel_display(display, dataset, one);
  for (const auto &entry : legacy) check(metadata_equal(enabled.properties.at(entry.first), entry.second), "Enabling state changed a legacy/unrelated property");
  check(recognized_channel_display(enabled, dataset)->mode == ChannelDisplayMode::Color, "Recognized state lookup lost mode");
  check(metadata_equal(with_channel_display(enabled, dataset, std::nullopt).properties, legacy), "Removal did not restore retained legacy fields exactly");
  for (const auto &header : std::vector<std::pair<std::string, MetadataValue>>{{"schema", std::string("opaque.application")}, {"version", std::uint64_t(37)}, {"version", true}, {"version", 1.0}}) {
    auto opaque = record(1); opaque[header.first] = header.second; opaque["extra"] = metadata_array({1, std::string("opaque")});
    display.properties["channel_display"] = metadata_object(opaque);
    const auto before = display.properties;
    validate_known_channel_display(display, dataset.shape()); validate_known_channel_display(display, dataset);
    check(metadata_equal(display.properties, before), "Opaque state validation changed metadata");
    rejects([&] { recognized_channel_display(display, dataset); }, "Unsupported header silently selected legacy interpretation");
    check(with_channel_display(display, dataset, std::nullopt).properties.count("channel_display") == 0, "Removal interpreted an opaque state");
  }
  display.properties["channel_display"] = metadata_object(record(2));
  rejects([&] { validate_known_channel_display(display, dataset.shape()); }, "Known malformed channel count escaped generic validation");
  for (const auto &bad_property : std::vector<MetadataValue>{true, std::string("opaque"), metadata_array({1,2})}) {
    display.properties["channel_display"] = bad_property;
    rejects([&] { recognized_channel_display(display, dataset); }, "Malformed present property selected legacy interpretation");
  }
  auto large_source = std::make_shared<UnreadSource>(DatasetShape{1, 9, 1, 1, 4, 1});
  Dataset large(large_source);
  rejects([&] { parse_channel_display(record(9), large.shape(), PixelType::UInt8, finite); }, "Stored Composite above8 admitted");
  check(parse_requested_channel_display(record(9), large.shape(), PixelType::UInt8, finite).mode == ChannelDisplayMode::Color, "Requested JSON above8 did not normalize to Color");
  check(normalize_channel_display(state(9), large.shape(), PixelType::UInt8, finite).mode == ChannelDisplayMode::Color, "Explicit above8 request did not normalize to Color");
  auto requested_bad = replace_channel(record(9), 2, "range", metadata_array({true,255}));
  rejects([&] { parse_requested_channel_display(requested_bad,large.shape(),PixelType::UInt8,finite); }, "Above8 normalization bypassed strict channel validation");
  auto large_display = with_channel_display(DisplayState{},large,state(9));
  check(recognized_channel_display(large_display,large)->mode == ChannelDisplayMode::Color, "Explicit setter failed above8 Color normalization");
  check(large_source->reads == 0 && large_source->regions == 0, "State validation/normalization read pixels");
}

// Literal bytes below come from the immutable 63-case ImageJ1.54r observation
// (entry SHA256 5360efa8a9f605d845aec107d6d9e1ee940e9732d0bdafdbd07a4c213d1bb471).
// Equal [0,0], noncanonical native samples, all-hidden determinism and finite
// all-NaN fallback are explicitly owned policies, separately labelled below.
void mapping_cases() {
  struct Mapping { PixelType type; std::vector<double> input; std::vector<std::array<double, 2>> ranges; std::vector<std::vector<int>> expected; };
  const std::vector<double> u8{0,1,2,3,4,5,6,7,8,9,10,11,126,127,128,129,254,255};
  const std::vector<double> u16{0,1,2,5,6,7,8,9,10,11,126,127,128,129,255,256,257,65535};
  const float half = static_cast<float>(.5 / 255.0);
  const std::vector<double> floats{-1, -0.0, f32(1), half, std::nextafter(half, -std::numeric_limits<float>::infinity()),
    std::nextafter(half, std::numeric_limits<float>::infinity()), .49999997f, .5f, .50000006f, 1,2,7,8,9,10,254.5f,255,f32(0x7fc12345)};
  const std::vector<std::array<double, 2>> ranges{{0,255},{0,10},{0,1},{.5,10.5},{7,7}};
  const std::vector<Mapping> rows{
    {PixelType::UInt8,u8,ranges,{
      {0,1,2,3,4,5,6,7,8,9,10,11,126,127,128,129,254,255},
      {0,25,51,76,102,128,153,179,204,230,255,255,255,255,255,255,255,255},
      {0,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255},
      {0,0,25,51,76,102,128,153,179,204,230,255,255,255,255,255,255,255},
      {0,0,0,0,0,0,0,0,255,255,255,255,255,255,255,255,255,255}}},
    {PixelType::UInt16,u16,ranges,{
      {0,1,2,5,6,7,8,9,10,11,126,127,128,129,255,255,255,255},
      {0,23,47,116,140,163,186,209,233,255,255,255,255,255,255,255,255,255},
      {0,128,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255},
      {0,0,23,93,116,140,163,186,209,233,255,255,255,255,255,255,255,255},
      {0,0,0,0,0,0,255,255,255,255,255,255,255,255,255,255,255,255}}},
    {PixelType::Float32,floats,ranges,{
      {0,0,0,0,0,0,0,1,1,1,2,7,8,9,10,255,255,0},
      {0,0,0,0,0,0,13,13,13,26,51,179,204,230,255,255,255,0},
      {0,0,0,1,0,1,127,128,128,255,255,255,255,255,255,255,255,0},
      {0,0,0,0,0,0,0,0,0,13,38,166,191,217,242,255,255,0},
      {0,0,0,0,0,0,0,0,0,0,0,0,255,255,255,255,255,0}}}};
  for (std::size_t type = 0; type < rows.size(); ++type) {
    const auto &test = rows[type];
    auto source = row(test.type, test.input, test.type == PixelType::Float32 ? FloatingSamplePolicy::PreserveQuietNaNFloat32 : FloatingSamplePolicy::FiniteOnly);
    const auto before = fingerprint(source);
    for (std::size_t i = 0; i < test.ranges.size(); ++i) {
      context = "pinned processor type" + std::to_string(type) + " range" + std::to_string(i);
      auto channels = state(1, ChannelDisplayMode::Grayscale); channels.channels[0].range = test.ranges[i];
      gray(compose_channel_display(source, channels, Plane::XY, 0, 0, 0), test.expected[i]);
    }
    check(fingerprint(source) == before, "Rendering changed original sample bits");
  }
  // The nine additional range-edge observations are sealed separately under
  // build/p06c-001/range-edges-001: range-edges-001.json SHA256
  // 907c30df12529291e295427380b3d18e5e91e964f55791cdd3816cc987fc5c23
  // and range-edges-002.json SHA256
  // e8f34f29a4d79b9530f706df2fb054f4c40df395e65a3cdcb2a3f57c756cc8f2.
  // Both independent public rendering calls produced each literal byte vector.
  struct RangeEdge {
    const char *name;
    PixelType type;
    std::vector<double> input;
    std::array<double,2> requested_range;
    std::vector<int> expected;
  };
  const std::vector<double> integers{0,1,2,3,4,5,6,7,8,9,10,11,12};
  const std::vector<RangeEdge> edges{
    {"uint8 .2..10.8",PixelType::UInt8,integers,{.2,10.8},
      {0,24,48,72,96,120,144,169,193,217,241,255,255}},
    {"uint8 .6..10.4",PixelType::UInt8,integers,{.6,10.4},
      {0,0,26,52,78,104,130,156,182,208,235,255,255}},
    {"uint8 .49...51",PixelType::UInt8,integers,{.49,.51},
      {0,255,255,255,255,255,255,255,255,255,255,255,255}},
    {"float32 .1...9 canonical boundary neighbors",PixelType::Float32,
      {f32(0x00000000),f32(0x3dcccccc),f32(0x3dcccccd),f32(0x3dccccce),
       f32(0x3e4ccccd),f32(0x3e99999a),f32(0x3f000000),f32(0x3f333333),
       f32(0x3f666665),f32(0x3f666666),f32(0x3f666667),f32(0x3f800000)},
      {.1,.9},{0,0,0,0,32,64,128,191,255,255,255,255}},
    {"float32 requested endpoints collapse near one",PixelType::Float32,
      {f32(0x3f7fffff),f32(0x3f800000),f32(0x3f800001),f32(0x3f800002),f32(0x00000000),f32(0x40000000)},
      {1.00000001,1.00000005},{0,0,255,255,0,255}},
    {"float32 explicit finite extrema",PixelType::Float32,
      {f32(0xff7fffff),f32(0x00000000),f32(0x7f7fffff)},
      {-double(std::numeric_limits<float>::max()),double(std::numeric_limits<float>::max())},{0,128,255}},
    {"uint16 .2..10.8 control",PixelType::UInt16,integers,{.2,10.8},
      {0,21,43,64,85,107,128,149,171,192,213,235,255}},
    {"uint16 .6..10.4 control",PixelType::UInt16,integers,{.6,10.4},
      {0,0,26,51,77,102,128,154,179,205,230,255,255}},
    {"float32 .1...9 discriminating byte threshold neighbors",PixelType::Float32,
      {f32(0x3ddcdcdd),f32(0x3dfcfcfd),f32(0x3e1b6835),f32(0x3e553ba2),
       f32(0x3e654bb2),f32(0x3e755bc2),f32(0x3e82b5e9),f32(0x3e9c6936),
       f32(0x3ea4713e),f32(0x3eac7946),f32(0x3eb4814e),f32(0x3ebc8956)},
      {.1,.9},{2,7,17,35,40,45,50,66,71,76,81,86}}
  };
  check(edges.size() == 9, "Targeted range-edge fixture membership changed");
  for (const auto &test : edges) {
    context = std::string("pinned range-edge ") + test.name;
    const auto source = row(test.type,test.input);
    const auto before = fingerprint(source);
    auto channels = state(1,ChannelDisplayMode::Grayscale);
    channels.channels[0].range = test.requested_range;
    const auto encoded = encode_channel_display(channels,source.shape(),test.type,source.sample_policy());
    const auto restored = parse_channel_display(encoded,source.shape(),test.type,source.sample_policy());
    check(restored.channels[0].range == test.requested_range,
          "State persistence rounded requested endpoints to processor mapping endpoints");
    gray(compose_channel_display(source,restored,Plane::XY,0,0,0),test.expected);
    check(fingerprint(source) == before, "Range-edge mapping changed canonical source sample bits");
    check(restored.channels[0].range == test.requested_range && channels.channels[0].range == test.requested_range,
          "Mapping changed exact requested state endpoints");
  }
  context = "literal equal0 owned policy";
  for (auto type : {PixelType::UInt8, PixelType::UInt16, PixelType::Float32}) {
    auto source = row(type, {0, 1, 2, 7}); auto channels = state(1, ChannelDisplayMode::Grayscale); channels.channels[0].range = {0, 0};
    gray(compose_channel_display(source, channels, Plane::XY, 0, 0, 0), {0,255,255,255});
  }
  context = "noncanonical native finite arithmetic owned extension";
  const std::array<PixelType, 3> types{PixelType::UInt8, PixelType::UInt16, PixelType::Float32};
  const std::array<std::vector<int>, 3> fractional{{{32,64,96,160,192,224},{21,43,64,107,128,149},{32,64,96,159,191,223}}};
  for (int i = 0; i < 3; ++i) {
    auto source = row(types[i], {.25,.5,.75,1.25,1.5,1.75}); auto channels = state(1, ChannelDisplayMode::Grayscale); channels.channels[0].range = {0,2};
    auto before = fingerprint(source);
    gray(compose_channel_display(source, channels, Plane::XY, 0, 0, 0), fractional[i]);
    check(before == fingerprint(source), "Noncanonical display cast or rounded source storage");
  }
  context = "wide Float32-tagged finite provider intermediates owned extension";
  auto wide_metadata = std::make_shared<UnreadSource>(DatasetShape{1,1,1,1,5,1},PixelType::Float32);
  Dataset wide_source(wide_metadata);
  auto wide_plane = std::make_shared<Image>(5,1,1,PixelType::Float32);
  const double float_max = std::numeric_limits<float>::max();
  wide_plane->pixels = {-1e100,-float_max,0,float_max,1e100};
  std::vector<std::uint64_t> wide_before;
  for (double value : wide_plane->pixels) wide_before.push_back(double_bits(value));
  auto wide_state = state(1,ChannelDisplayMode::Grayscale); wide_state.channels[0].range = {-float_max,float_max};
  int wide_calls = 0;
  gray(compose_channel_display(wide_source,wide_state,Plane::XY,0,0,0,[&](Plane,int,int,int)->std::shared_ptr<const Image> { ++wide_calls; return wide_plane; }),{0,0,128,255,255});
  check(wide_calls == 1 && wide_metadata->reads == 0 && wide_metadata->regions == 0, "Wide provider mapping bypassed the selected source");
  for (std::size_t i = 0; i < wide_before.size(); ++i) check(double_bits(wide_plane->pixels[i]) == wide_before[i], "Wide provider mapping cast/clamped/mutated raw source storage");
  context = "pinned palettes";
  const std::array<std::string, 8> names{"Grays","Red","Green","Blue","White","Cyan","Magenta","Yellow"};
  const std::array<std::array<int, 3>, 8> ends{{{255,255,255},{255,0,0},{0,255,0},{0,0,255},{255,255,255},{0,255,255},{255,0,255},{255,255,0}}};
  for (std::size_t i = 0; i < names.size(); ++i) {
    auto table = channel_display_lut(names[i]);
    for (int index = 0; index < 256; ++index)
      for (int component = 0; component < 3; ++component)
        check(table[index][component] == (ends[i][component] ? index : 0), "Named palette differs from pinned full256 table");
  }
  rejects([&] { channel_display_lut("unknown"); }, "Unknown named palette admitted");
  for (auto type : types) {
    context = "pinned SUM and modes";
    Stack planes;
    for (auto values : std::vector<std::vector<double>>{{10,20,30,40},{100,200,150,120},{0,255,64,128},{21,33,39,51}}) {
      Image image(4,1,1,type); image.pixels = values; planes.push_back(std::move(image));
    }
    auto source = Dataset::from_planes({1,4,1,1,4,1}, std::move(planes)); auto channels = state(4);
    const auto before = fingerprint(source);
    rgb(compose_channel_display(source, channels, Plane::XY, 0, 0, 1), {{31,121,21},{53,233,255},{69,189,103},{91,171,179}});
    channels.mode = ChannelDisplayMode::Color; channels.channels[1].visible = false;
    rgb(compose_channel_display(source, channels, Plane::XY, 0, 0, 1), {{0,100,0},{0,200,0},{0,150,0},{0,120,0}});
    channels.mode = ChannelDisplayMode::Grayscale;
    gray(compose_channel_display(source, channels, Plane::XY, 0, 0, 1), {100,200,150,120});
    channels.mode = ChannelDisplayMode::Composite;
    for (auto &channel : channels.channels) channel.visible = false;
    rgb(compose_channel_display(source, channels, Plane::XY, 0, 0, 1), {{0,0,0},{0,0,0},{0,0,0},{0,0,0}});
    channels.channels[1].visible = true;
    rgb(compose_channel_display(source, channels, Plane::XY, 0, 0, 1), {{0,100,0},{0,200,0},{0,150,0},{0,120,0}});
    for (auto &channel : channels.channels) channel.visible = false;
    rgb(compose_channel_display(source, channels, Plane::XY, 0, 0, 1), {{0,0,0},{0,0,0},{0,0,0},{0,0,0}});
    check(fingerprint(source) == before, "Visibility/mode changes modified source pixels");
  }
  context = "pinned constructor ranges inherited from C0";
  const std::vector<std::vector<std::array<int,3>>> default_rgb{
    {{10,100,0},{20,200,255},{30,150,64},{40,120,128}},
    {{0,255,0},{83,255,255},{165,255,255},{248,255,255}},
    {{0,255,0},{85,255,255},{170,255,255},{255,255,255}}};
  for (int i = 0; i < 3; ++i) {
    Stack planes;
    for (const auto &values : std::vector<std::vector<double>>{{10,20,30,40},{100,200,150,120},{0,255,64,128}}) {
      Image image(4,1,1,types[i]); image.pixels = values; planes.push_back(std::move(image));
    }
    auto source3 = Dataset::from_planes({1,3,1,1,4,1},std::move(planes));
    const auto defaults = default_channel_display(source3,0,0);
    rgb(compose_channel_display(source3,defaults,Plane::XY,0,0,0),default_rgb[i]);
  }
  context = "pinned above8 default normalization and palette";
  Stack nine_planes;
  for (int c = 0; c < 9; ++c) {
    Image image(4,1,1,PixelType::UInt8);
    image.pixels = c == 8 ? std::vector<double>{56,88,104,136} : std::vector<double>{10,20,30,40};
    nine_planes.push_back(std::move(image));
  }
  auto nine = Dataset::from_planes({1,9,1,1,4,1},std::move(nine_planes));
  auto nine_default = default_channel_display(nine,0,0);
  check(nine_default.mode == ChannelDisplayMode::Color && nine_default.channels.size() == 9, "Default above8 lost channels or failed Color normalization");
  const std::array<std::string,9> default_names{"Red","Green","Blue","White","Cyan","Magenta","Yellow","White","White"};
  for (int c = 0; c < 9; ++c) check(nine_default.channels[c].lut.name == default_names[c] && nine_default.channels[c].visible &&
    nine_default.channels[c].range == std::array<double,2>{0,255}, "Default palette/range/visibility order differs from pinned capture");
  gray(compose_channel_display(nine,nine_default,Plane::XY,0,0,8),{56,88,104,136});
  context = "pinned singleton modes";
  auto one = row(PixelType::UInt8, {10,20,30,40}); auto singleton = state(1);
  gray(compose_channel_display(one, singleton, Plane::XY, 0, 0, 0), {10,20,30,40});
  singleton.mode = ChannelDisplayMode::Color;
  rgb(compose_channel_display(one, singleton, Plane::XY, 0, 0, 0), {{10,0,0},{20,0,0},{30,0,0},{40,0,0}});
  context = "custom nonblack zero including quiet NaN";
  auto source = row(PixelType::Float32, {f32(0x7fc12345),0,1,255}, FloatingSamplePolicy::PreserveQuietNaNFloat32);
  auto custom = state(1, ChannelDisplayMode::Color); custom.channels[0].lut.name.clear();
  ChannelDisplayTable table{};
  for (int i = 0; i < 256; ++i) table[i] = {static_cast<std::uint8_t>(i), static_cast<std::uint8_t>((3*i)%256), static_cast<std::uint8_t>(255-i)};
  custom.channels[0].lut.table = table;
  auto before = fingerprint(source);
  rgb(compose_channel_display(source, custom, Plane::XY, 0, 0, 0), {{0,0,255},{0,0,255},{1,3,254},{255,253,0}});
  check(before == fingerprint(source), "Custom mapping changed quiet-NaN bits");
  Stack nan_sum_planes;
  Image nan_channel(4,1,1,PixelType::Float32); nan_channel.pixels = {f32(0x7fc12345),f32(0xffc34567),f32(0x7fc00000),f32(0x7fc12345)};
  Image finite_channel(4,1,1,PixelType::Float32); finite_channel.pixels = {100,200,150,120};
  nan_sum_planes.push_back(std::move(nan_channel)); nan_sum_planes.push_back(std::move(finite_channel));
  auto nan_sum = Dataset::from_planes({1,2,1,1,4,1},std::move(nan_sum_planes),{},FloatingSamplePolicy::PreserveQuietNaNFloat32);
  auto nan_sum_state = state(2); nan_sum_state.channels[0].lut = custom.channels[0].lut;
  const auto sum_before = fingerprint(nan_sum);
  rgb(compose_channel_display(nan_sum,nan_sum_state,Plane::XY,0,0,1),{{0,100,255},{0,200,255},{0,150,255},{0,120,255}});
  check(fingerprint(nan_sum) == sum_before, "SUM changed NaN payload/sign or other source bits");
}

void ranges_cases() {
  context = "live XY reset order and defaults";
  auto source = std::make_shared<FormulaSource>(); Dataset dataset(source);
  auto channels = state(); channels.channels[0].range = {-5, 6}; channels.channels[1].range = {8, 9}; channels.channels[2].range = {30, 31};
  channels.channels[1].lut.name = "Yellow"; channels.channels[2].visible = false;
  auto reset = reset_channel_display_ranges(dataset, channels, 1, 2, {2,0});
  check(reset.second.size() == 2 && reset.second[0].range == std::array<double,2>{186,200} &&
        reset.second[1].range == std::array<double,2>{86,100}, "Auto did not use requested C order and live captured XY");
  check(reset.first.channels[0].range == std::array<double,2>{86,100} && reset.first.channels[1].range == std::array<double,2>{8,9} &&
        reset.first.channels[2].range == std::array<double,2>{186,200}, "Partial reset changed an unrequested range or misplaced a requested range");
  check(channels.channels[0].range == std::array<double,2>{-5,6}, "Range reset mutated input state");
  check(reset.first.mode == channels.mode && reset.first.channels[1].lut.name == "Yellow" && !reset.first.channels[2].visible, "Auto changed mode, LUT or visibility");
  check(source->positions.size() == 2 && source->positions[0].t == 1 && source->positions[0].c == 2 && source->positions[0].z == 2 &&
        source->positions[1].t == 1 && source->positions[1].c == 0 && source->positions[1].z == 2, "Auto read another T/Z, skipped a C or materialized a volume");
  source->positions.clear();
  auto defaults = default_channel_display(dataset, 1, 2);
  check(source->positions.size() == 1 && source->positions[0].c == 0, "Defaults did not inherit just first-channel live range");
  for (int c = 0; c < 3; ++c) check(defaults.channels[c].visible && defaults.channels[c].range == std::array<double,2>{86,100}, "Default C range inheritance/visibility differs");
  source->positions.clear();
  auto all = reset_channel_display_ranges(dataset, channels, 0, 1);
  check(all.second.size() == 3 && source->positions.size() == 3, "Empty reset list did not resolve every C once");
  for (int c = 0; c < 3; ++c) check(all.second[c].range == std::array<double,2>{double(8+50*c),double(22+50*c)}, "All-C Auto range differs from analytic live XY");
  context = "constant, empty finite, extrema ranges";
  for (auto type : {PixelType::UInt8,PixelType::UInt16,PixelType::Float32}) {
    auto constant = row(type, {7,7,7});
    auto result = reset_channel_display_ranges(constant, state(1), 0, 0);
    const std::array<double,2> expected = type == PixelType::UInt8 ? std::array<double,2>{0,255} : std::array<double,2>{7,7};
    check(result.second.size() == 1 && result.second[0].range == expected && !result.second[0].empty_finite, "Constant Auto range/finite flag differs from contract");
    gray(compose_channel_display(constant, result.first, Plane::XY, 0, 0, 0), type == PixelType::UInt8 ? std::vector<int>{7,7,7} : std::vector<int>{0,0,0});
  }
  auto empty = row(PixelType::Float32, {f32(0x7fc00000),f32(0x7fc12345),f32(0xffc34567),f32(0x7fc00000)}, FloatingSamplePolicy::PreserveQuietNaNFloat32);
  const auto empty_before = fingerprint(empty);
  auto empty_reset = reset_channel_display_ranges(empty, state(1), 0, 0);
  check(empty_reset.second[0].range == std::array<double,2>{0,1} && empty_reset.second[0].empty_finite && empty_reset.first.channels[0].range == std::array<double,2>{0,1}, "All-NaN Auto did not resolve finite owned fallback and empty flag");
  gray(compose_channel_display(empty, empty_reset.first, Plane::XY, 0, 0, 0), {0,0,0,0});
  check(fingerprint(empty) == empty_before, "All-NaN range/render changed payload/sign bits");
  auto mixed = row(PixelType::Float32, {f32(0x7fc12345),7,f32(0xffc34567),3}, FloatingSamplePolicy::PreserveQuietNaNFloat32);
  auto mixed_reset = reset_channel_display_ranges(mixed, state(1), 0, 0);
  check(mixed_reset.second[0].range == std::array<double,2>{3,7} && !mixed_reset.second[0].empty_finite, "Mixed NaN range included NaNs or lost finite observation");
  gray(compose_channel_display(mixed, mixed_reset.first, Plane::XY, 0, 0, 0), {0,255,0,0});
  const double largest = std::numeric_limits<float>::max();
  auto extremes = row(PixelType::Float32, {-largest,0,largest});
  auto extremes_reset = reset_channel_display_ranges(extremes, state(1), 0, 0);
  check(extremes_reset.second[0].range == std::array<double,2>{-largest,largest}, "Finite Float32 extrema range overflowed");
  gray(compose_channel_display(extremes, extremes_reset.first, Plane::XY, 0, 0, 0), {0,128,255});
  context = "effective range precedence";
  auto unread = std::make_shared<UnreadSource>(); Dataset metadata_only(unread);
  auto known = state(); known.channels[2].range = {11,19};
  check(effective_channel_display_range(metadata_only, known, 1,2,3, std::array<double,2>{4,5}).range == std::array<double,2>{11,19}, "Recognized C range lost precedence to legacy flat range");
  check(effective_channel_display_range(metadata_only, std::nullopt, 1,2,3, std::array<double,2>{4,5}).range == std::array<double,2>{4,5}, "Legacy explicit range was ignored");
  check(unread->reads == 0 && unread->regions == 0, "Explicit effective range read source pixels");
  auto legacy64 = row(PixelType::Float64, {-100,0,200});
  check(effective_channel_display_range(legacy64,std::nullopt,0,0,0,std::array<double,2>{-2,9}).range == std::array<double,2>{-2,9}, "Legacy Float64 explicit range was refused or changed");
  for (int samples : {3,4}) {
    auto legacy_source = std::make_shared<UnreadSource>(DatasetShape{1,1,1,5,7,samples},PixelType::Float64); Dataset legacy_rgb(legacy_source);
    check(effective_channel_display_range(legacy_rgb,std::nullopt,0,0,0,std::array<double,2>{-2,9}).range == std::array<double,2>{-2,9}, "Legacy RGB/RGBA explicit range was refused or changed");
    check(legacy_source->reads == 0 && legacy_source->regions == 0, "Legacy RGB/RGBA explicit range read pixels");
  }
  check(effective_channel_display_range(dataset, std::nullopt, 1,1,3).range == std::array<double,2>{144,158}, "Legacy absent range did not resolve current live XY");
  context = "asymmetric streamed XYZ and per-C preview selector";
  auto live_source = std::make_shared<FormulaSource>(PixelType::UInt8); Dataset live(live_source);
  for (int crosshair_z : {2,3,2})
    for (Plane plane : {Plane::XY,Plane::XZ,Plane::YZ}) {
      const int coordinate = plane == Plane::XY ? crosshair_z : plane == Plane::XZ ? 3 : 4;
      std::vector<int> calls; std::weak_ptr<const Image> previous;
      ChannelDisplayPlaneProvider provider = [&](Plane requested, int fixed, int t, int c) {
        check(previous.expired(), "Compositor retained previous channel plane while requesting next C");
        check(requested == plane && fixed == coordinate && t == 1, "Provider lost requested plane/fixed axis/T");
        calls.push_back(c);
        auto image = std::make_shared<Image>(live.extract_plane(requested, fixed, t, c));
        if (c == 1 && crosshair_z == 2) {
          // Retained target C1/T1/Z2: XY is staged; orthogonal views retain
          // live values outside the captured source-Z strip.
          for (int y = 0; y < image->height; ++y)
            if (requested == Plane::XY || y == 2)
              for (int x = 0; x < image->width; ++x) image->at(x,y) += 7;
        }
        previous = image; return image;
      };
      auto image = compose_channel_display(live, state(), plane, coordinate, 1, 0, provider);
      check(calls == std::vector<int>{0,1,2} && previous.expired(), "Composite did not stream exactly contributing C0/C1/C2 and release last plane");
      const int width = plane == Plane::YZ ? 5 : 7, height = plane == Plane::XY ? 5 : 4;
      check(image.width == width && image.height == height, "XYZ geometry is transposed or fixed on wrong axis");
      for (int y = 0; y < height; ++y)
        for (int x = 0; x < width; ++x) {
          const int sx = plane == Plane::YZ ? coordinate : x;
          const int sy = plane == Plane::XZ ? coordinate : plane == Plane::YZ ? x : y;
          const int sz = plane == Plane::XY ? coordinate : y;
          for (int c = 0; c < 3; ++c) {
            int expected = 70 + 50*c + 8*sz + 2*sy + sx;
            if (c == 1 && crosshair_z == 2 && sz == 2) expected += 7;
            check(image.at(x,y,c) == expected, "Per-C live/staged XYZ sample differs from independent source coordinates");
          }
        }
    }
  for (Plane plane : {Plane::XY,Plane::XZ,Plane::YZ}) {
    live_source->positions.clear();
    const int coordinate = plane == Plane::XY ? 2 : plane == Plane::XZ ? 3 : 4;
    auto image = compose_channel_display(live,state(),plane,coordinate,1,0);
    check(live_source->positions.size() == (plane == Plane::XY ? 3u : 12u), "Default provider read another TCZ volume or repeated a source plane");
    for (int c = 0; c < 3; ++c)
      for (int z = plane == Plane::XY ? 2 : 0; z < (plane == Plane::XY ? 3 : 4); ++z)
        check(std::count_if(live_source->positions.begin(),live_source->positions.end(),[&](PlanePosition p) { return p.t == 1 && p.c == c && p.z == z; }) == 1,
              "Default provider omitted/repeated a contributing source TCZ plane");
    const int sx = plane == Plane::YZ ? coordinate : image.width-1;
    const int sy = plane == Plane::XZ ? coordinate : plane == Plane::YZ ? image.width-1 : image.height-1;
    const int sz = plane == Plane::XY ? coordinate : image.height-1;
    for (int c = 0; c < 3; ++c) check(image.at(image.width-1,image.height-1,c) == 70+50*c+8*sz+2*sy+sx,
          "Default XYZ provider changed analytic source coordinates");
  }
  context = "Color/Grayscale provider selection and all-hidden zero reads";
  int provider_calls = 0;
  auto provider = [&](Plane plane,int coordinate,int t,int c) {
    ++provider_calls; check(c == 2, "Single-channel mode requested a non-active C");
    return std::make_shared<const Image>(live.extract_plane(plane,coordinate,t,c));
  };
  for (auto mode : {ChannelDisplayMode::Color,ChannelDisplayMode::Grayscale}) {
    auto channels2 = state(3, mode); channels2.channels[2].visible = false;
    compose_channel_display(live, channels2, Plane::XY, 2,1,2,provider);
  }
  check(provider_calls == 2, "Single-channel modes read more than one contributing C");
  auto hidden = state(); for (auto &channel : hidden.channels) channel.visible = false;
  auto black = compose_channel_display(metadata_only, hidden, Plane::XY,2,1,0,[&](Plane,int,int,int)->std::shared_ptr<const Image> { ++provider_calls; throw std::runtime_error("Hidden channel was requested"); });
  check(provider_calls == 2 && std::all_of(black.pixels.begin(),black.pixels.end(),[](double v) { return v == 0; }), "All-hidden composition read source/provider or reused stale accumulator");
}

void limits_cases() {
  context = "metadata-only estimates and refusal";
  auto source = std::make_shared<UnreadSource>(); Dataset dataset(source);
  auto channels = state();
  int provider_reads = 0, callbacks = 0;
  ChannelDisplayPlaneProvider provider = [&](Plane,int,int,int)->std::shared_ptr<const Image> { ++provider_reads; throw std::runtime_error("Refused metadata path called provider"); };
  ExecutionControl control; control.checkpoint = [&] { ++callbacks; };
  control.progress = [&](std::size_t,std::size_t) { ++callbacks; };
  control.stage_progress = [&](std::string_view,std::size_t,std::size_t) { ++callbacks; };
  for (auto plane : {Plane::XY,Plane::XZ,Plane::YZ}) {
    auto estimate = estimate_channel_display(dataset, channels, plane,2,1,0);
    const int width = plane == Plane::YZ ? 5 : 7, height = plane == Plane::XY ? 5 : 4;
    const auto pixels = std::size_t(width)*height;
    check(estimate.width == width && estimate.height == height && estimate.pixels == pixels &&
          estimate.working_bytes == (4*pixels + (plane == Plane::XY ? 0 : width))*sizeof(double) + 6272 &&
          estimate.contributing_channels == std::vector<int>{0,1,2}, "Metadata-only working estimate differs from scalar/RGB/strip bound");
    ChannelDisplayLimits exact{pixels,estimate.working_bytes};
    check(estimate_channel_display(dataset,channels,plane,2,1,0,exact).working_bytes == estimate.working_bytes, "Exact metadata caps refused");
    for (auto cap : std::array<ChannelDisplayLimits,4>{{{0,estimate.working_bytes},{pixels-1,estimate.working_bytes},{pixels,0},{pixels,estimate.working_bytes-1}}}) {
      rejects([&] { compose_channel_display(dataset,channels,plane,2,1,0,provider,cap,control); }, "Zero/tight metadata cap admitted");
    }
  }
  for (auto mode : {ChannelDisplayMode::Color,ChannelDisplayMode::Grayscale}) {
    auto single = channels; single.mode = mode; single.channels[2].visible = false;
    check(estimate_channel_display(dataset,single,Plane::XY,2,1,2).contributing_channels == std::vector<int>{2}, "Single-mode metadata selection depended on visibility");
  }
  auto hidden = channels; for (auto &channel : hidden.channels) channel.visible = false;
  check(estimate_channel_display(dataset,hidden,Plane::XY,2,1,0).contributing_channels.empty(), "All-hidden preflight retained a contributing C");
  for (auto bad : std::vector<std::array<int,4>>{{-1,0,0,0},{4,0,0,0},{0,-1,0,0},{0,2,0,0},{0,0,-1,0},{0,0,3,0}}) {
    rejects<std::out_of_range>([&] { compose_channel_display(dataset,channels,Plane::XY,bad[0],bad[1],bad[2],provider,{},control); }, "Invalid coordinate/T/active C admitted");
  }
  rejects([&] { compose_channel_display(dataset,channels,static_cast<Plane>(42),0,0,0,provider,{},control); }, "Invalid plane enum admitted");
  for (auto list : std::vector<std::vector<int>>{{1,1},{0,2,0}})
    rejects([&] { reset_channel_display_ranges(dataset,channels,1,2,list,{},control); }, "Duplicate Auto channel list admitted");
  for (auto list : std::vector<std::vector<int>>{{-1},{3}})
    rejects<std::out_of_range>([&] { reset_channel_display_ranges(dataset,channels,1,2,list,{},control); }, "Out-of-range Auto channel admitted");
  auto invalid_state = channels; invalid_state.mode = static_cast<ChannelDisplayMode>(42);
  rejects([&] { compose_channel_display(dataset,invalid_state,Plane::XY,2,1,0,provider,{},control); }, "Invalid typed state reached provider");
  rejects([&] { default_channel_display(dataset,1,2,static_cast<ChannelDisplayMode>(42),{},control); }, "Invalid requested default mode admitted");
  rejects<std::out_of_range>([&] { reset_channel_display_ranges(dataset,channels,2,2,{}, {},control); }, "Auto invalid T admitted");
  rejects<std::out_of_range>([&] { reset_channel_display_ranges(dataset,channels,1,4,{}, {},control); }, "Auto invalid Z admitted");
  for (auto cap : std::array<ChannelDisplayLimits,2>{{{0,1024*1024},{35,0}}}) {
    rejects([&] { default_channel_display(dataset,1,2,ChannelDisplayMode::Composite,cap,control); }, "Default zero cap admitted");
    rejects([&] { reset_channel_display_ranges(dataset,channels,1,2,{},cap,control); }, "Auto zero cap admitted");
  }
  const auto default_bytes = 35*sizeof(double) + 3*(sizeof(ChannelDisplayChannel)+8);
  rejects([&] { default_channel_display(dataset,1,2,ChannelDisplayMode::Composite,{35,default_bytes-1},control); }, "Default C-dependent allocation preflight admitted a tight cap");
  const auto reset_bytes = default_bytes + 2*(sizeof(ChannelDisplayRange)+2*sizeof(int));
  rejects([&] { reset_channel_display_ranges(dataset,channels,1,2,{2,0},{35,reset_bytes-1},control); }, "Reset C-dependent result/index allocation preflight admitted a tight cap");
  check(source->reads == 0 && source->regions == 0 && provider_reads == 0 && callbacks == 0, "Metadata admission failure performed a source/provider read or user callback");
  const int large_side = sizeof(std::size_t) >= 8 ? 1000000000 : 20000;
  auto huge_source = std::make_shared<UnreadSource>(DatasetShape{1,1,1,large_side,large_side,1}); Dataset huge(huge_source);
  const ChannelDisplayLimits unbounded{std::numeric_limits<std::size_t>::max(),std::numeric_limits<std::size_t>::max()};
  rejects<std::exception>([&] { estimate_channel_display(huge,state(1),Plane::XY,0,0,0,unbounded); }, "Working byte arithmetic overflow admitted");
  check(huge_source->reads == 0 && huge_source->regions == 0, "Overflow preflight read huge source");
  const int large_axis = sizeof(std::size_t) >= 8 ? 1000000000 : 10000;
  auto large_axes_source = std::make_shared<UnreadSource>(DatasetShape{large_axis,3,large_axis,1,1,1}); Dataset large_axes(large_axes_source);
  check(estimate_channel_display(large_axes,state(),Plane::XY,large_axis-1,large_axis-1,2).pixels == 1, "Metadata preflight materialized/scaled by unrequested TCZ volume");
  check(large_axes_source->reads == 0 && large_axes_source->regions == 0, "Large-axis metadata request read pixels");
  auto huge_c_source = std::make_shared<UnreadSource>(DatasetShape{1,1000000000,1,1,1,1}); Dataset huge_c(huge_c_source);
  rejects([&] { default_channel_display(huge_c,0,0,ChannelDisplayMode::Composite,{},control); }, "Extreme-C defaults attempted an unbounded result allocation");
  check(huge_c_source->reads == 0 && huge_c_source->regions == 0 && callbacks == 0, "Extreme-C default preflight read pixels or called user code");
  context = "cancellation and callback delegation";
  auto formula = std::make_shared<FormulaSource>(PixelType::UInt8); Dataset live(formula);
  auto token = std::make_shared<CancellationToken>(); token->cancel();
  ExecutionControl canceled; canceled.cancellation = token;
  int calls = 0;
  auto reading = [&](Plane plane,int coordinate,int t,int c) {
    ++calls; return std::make_shared<const Image>(live.extract_plane(plane,coordinate,t,c));
  };
  rejects<OperationCancelled>([&] { compose_channel_display(live,channels,Plane::XY,2,1,0,reading,{},canceled); }, "Pre-canceled composition ignored cancellation");
  check(calls == 0 && formula->positions.empty(), "Pre-canceled composition read a channel");
  rejects<OperationCancelled>([&] { reset_channel_display_ranges(live,channels,1,2,{}, {},canceled); }, "Pre-canceled Auto ignored cancellation");
  check(formula->positions.empty(), "Pre-canceled Auto read a channel");
  token = std::make_shared<CancellationToken>(); canceled.cancellation = token;
  auto cancel_provider = [&](Plane plane,int coordinate,int t,int c) {
    ++calls; auto image = std::make_shared<const Image>(live.extract_plane(plane,coordinate,t,c)); token->cancel(); return image;
  };
  rejects<OperationCancelled>([&] { compose_channel_display(live,channels,Plane::XY,2,1,0,cancel_provider,{},canceled); }, "Mid-stream cancellation returned composed output");
  check(calls == 1, "Mid-stream cancellation read subsequent C planes");
  struct CallbackError : std::runtime_error { CallbackError() : std::runtime_error("expected callback abort") {} };
  ExecutionControl aborting; aborting.checkpoint = [] { throw CallbackError(); };
  const auto read_before = formula->positions.size();
  rejects<CallbackError>([&] { compose_channel_display(live,channels,Plane::XY,2,1,0,reading,{},aborting); }, "Checkpoint exception was swallowed or translated");
  check(formula->positions.size() == read_before, "Checkpoint failure read source before delegate");
  ExecutionControl progress_aborting;
  progress_aborting.progress = [](std::size_t,std::size_t) { throw CallbackError(); };
  rejects<CallbackError>([&] { reset_channel_display_ranges(live,channels,1,2,{}, {},progress_aborting); }, "Range progress exception was swallowed or ignored");
  check(channels.channels[0].range == std::array<double,2>{0,255} && channels.channels[1].range == std::array<double,2>{0,255}, "Aborted Auto changed caller state before atomic return");
  context = "provider validation/source immutability";
  auto owned = row(PixelType::UInt8,{0,7,255}); auto before = fingerprint(owned); auto one = state(1,ChannelDisplayMode::Color);
  rejects([&] { compose_channel_display(owned,one,Plane::XY,0,0,0,[](Plane,int,int,int) { return std::shared_ptr<const Image>{}; }); }, "Null provider output admitted");
  for (int bad = 0; bad < 5; ++bad) {
    auto malformed = [bad](Plane,int,int,int)->std::shared_ptr<const Image> {
      auto image = std::make_shared<Image>(bad == 0 ? 4 : 3,1,bad == 1 ? 3 : 1,bad == 2 ? PixelType::Float32 : PixelType::UInt8);
      if (bad == 3) image->pixels[0] = std::numeric_limits<double>::infinity();
      if (bad == 4) image->pixels[0] = std::numeric_limits<double>::quiet_NaN();
      return image;
    };
    rejects([&] { compose_channel_display(owned,one,Plane::XY,0,0,0,malformed); }, "Malformed provider shape/type/samples admitted");
  }
  check(fingerprint(owned) == before, "Rejected provider changed source raw bits");
  auto quiet = row(PixelType::Float32,{f32(0x7fc12345),-0.0,7,f32(0xffc34567)},FloatingSamplePolicy::PreserveQuietNaNFloat32);
  const auto quiet_before = fingerprint(quiet);
  auto quiet_token = std::make_shared<CancellationToken>(); quiet_token->cancel();
  ExecutionControl quiet_control; quiet_control.cancellation = quiet_token;
  rejects<OperationCancelled>([&] { reset_channel_display_ranges(quiet,state(1),0,0,{}, {},quiet_control); }, "Pre-canceled NaN Auto returned a result");
  rejects<OperationCancelled>([&] { compose_channel_display(quiet,state(1),Plane::XY,0,0,0,{}, {},quiet_control); }, "Pre-canceled NaN composition returned a result");
  check(fingerprint(quiet) == quiet_before, "Cancellation changed original NaN payload/sign/negative-zero bits");
}
} // namespace

int main(int argc, char **argv) {
  try {
    if (argc > 2) throw std::invalid_argument("Usage: imagejxx_channel_display_tests [state|mapping|ranges|limits|all]");
    const std::string group = argc == 2 ? argv[1] : "all";
    if (group != "all" && group != "state" && group != "mapping" && group != "ranges" && group != "limits")
      throw std::invalid_argument("Unknown channel-display assertion group");
    if (group == "all" || group == "state") state_cases();
    if (group == "all" || group == "mapping") mapping_cases();
    if (group == "all" || group == "ranges") ranges_cases();
    if (group == "all" || group == "limits") limits_cases();
    std::cout << "Channel display " << group << " PASS (" << checks << " checks)\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "Channel display FAIL after " << checks << " checks: " << error.what() << '\n';
    return 1;
  }
}
