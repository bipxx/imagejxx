#include <imagejxx/channel_display.hpp>
#include <imagejxx/operations.hpp>
#include "kernel_control.hpp"

#include <algorithm>
#include <cmath>
#include <initializer_list>
#include <limits>
#include <stdexcept>
#include <utility>

namespace imagejxx {
namespace {
constexpr const char *schema = "imagejxx.channel-display";
constexpr const char *property = "channel_display";

const MetadataObject *maybe_object(const MetadataValue &value) {
  const auto node = std::get_if<std::shared_ptr<const MetadataTree>>(&value);
  return node && *node ? std::get_if<MetadataObject>(&(*node)->value) : nullptr;
}
const MetadataObject &object_value(const MetadataValue &value) {
  if (const auto object = maybe_object(value))
    return *object;
  throw std::invalid_argument("Channel display requires an object");
}
const MetadataArray &array_value(const MetadataValue &value) {
  const auto node = std::get_if<std::shared_ptr<const MetadataTree>>(&value);
  if (node && *node)
    if (const auto array = std::get_if<MetadataArray>(&(*node)->value))
      return *array;
  throw std::invalid_argument("Channel display requires an array");
}
const std::string &text_value(const MetadataValue &value) {
  if (const auto text = std::get_if<std::string>(&value))
    return *text;
  throw std::invalid_argument("Channel display requires text");
}
void exact_fields(const MetadataObject &object,
                  std::initializer_list<const char *> fields) {
  if (object.size() != fields.size())
    throw std::invalid_argument("Channel display has unexpected fields");
  for (const char *field : fields)
    if (!object.count(field))
      throw std::invalid_argument(std::string("Missing channel display field: ") + field);
}
std::optional<ChannelDisplayVersion> known_version(const MetadataValue &value) {
  if (const auto integer = std::get_if<std::int64_t>(&value)) {
    if (*integer == 1) return ChannelDisplayVersion::V1;
    if (*integer == 2) return ChannelDisplayVersion::V2;
    if (*integer == 3) return ChannelDisplayVersion::V3;
  } else if (const auto integer = std::get_if<std::uint64_t>(&value)) {
    if (*integer == 1) return ChannelDisplayVersion::V1;
    if (*integer == 2) return ChannelDisplayVersion::V2;
    if (*integer == 3) return ChannelDisplayVersion::V3;
  }
  return std::nullopt;
}
double number_value(const MetadataValue &value) {
  double result;
  if (const auto real = std::get_if<double>(&value))
    result = *real;
  else if (const auto integer = std::get_if<std::int64_t>(&value)) {
    result = static_cast<double>(*integer);
    if (result >= std::ldexp(1.0, 63) || result < -std::ldexp(1.0, 63) ||
        static_cast<std::int64_t>(result) != *integer)
      throw std::invalid_argument("Channel display range loses integer precision");
  } else if (const auto integer = std::get_if<std::uint64_t>(&value)) {
    result = static_cast<double>(*integer);
    if (result >= std::ldexp(1.0, 64) ||
        static_cast<std::uint64_t>(result) != *integer)
      throw std::invalid_argument("Channel display range loses integer precision");
  } else
    throw std::invalid_argument("Channel display range requires numbers");
  if (!std::isfinite(result))
    throw std::invalid_argument("Channel display range must be finite");
  return result;
}
std::uint8_t byte_value(const MetadataValue &value) {
  std::uint64_t result;
  if (const auto integer = std::get_if<std::uint64_t>(&value))
    result = *integer;
  else if (const auto integer = std::get_if<std::int64_t>(&value);
           integer && *integer >= 0)
    result = static_cast<std::uint64_t>(*integer);
  else
    throw std::invalid_argument("Channel display LUT requires exact integer bytes");
  if (result > 255)
    throw std::invalid_argument("Channel display LUT byte is out of range");
  return static_cast<std::uint8_t>(result);
}
void validate_range(const std::array<double, 2> &range) {
  if (!std::isfinite(range[0]) || !std::isfinite(range[1]) || range[0] > range[1])
    throw std::invalid_argument("Channel display range requires finite low <= high");
}
void validate_ieee_range(const std::array<double, 2> &range) {
  constexpr auto policy = FloatingSamplePolicy::PreserveIEEEFloat32;
  const auto low = float32_source_sample_bits(range[0], policy);
  const auto high = float32_source_sample_bits(range[1], policy);
  const bool low_nan = (low & 0x7fffffffu) > 0x7f800000u;
  const bool high_nan = (high & 0x7fffffffu) > 0x7f800000u;
  if (low_nan || high_nan) {
    if (low != 0x7fc00000u || high != 0x7fc00000u)
      throw std::invalid_argument("IEEE channel range requires a canonical NaN pair");
  } else if (range[0] > range[1])
    throw std::invalid_argument("IEEE channel range requires low <= high");
}
std::uint32_t word_value(const MetadataValue &value) {
  if (const auto word = std::get_if<std::uint64_t>(&value); word && *word <= 0xffffffffu)
    return static_cast<std::uint32_t>(*word);
  if (const auto word = std::get_if<std::int64_t>(&value); word && *word >= 0 && *word <= 0xffffffffLL)
    return static_cast<std::uint32_t>(*word);
  throw std::invalid_argument("IEEE channel endpoint requires an exact uint32 word");
}
MetadataValue encoded_range(const std::array<double, 2> &range) {
  if (std::isfinite(range[0]) && std::isfinite(range[1]))
    return metadata_array({range[0], range[1]});
  return metadata_object({{"float32_bits", metadata_array({
      std::uint64_t(float32_source_sample_bits(range[0], FloatingSamplePolicy::PreserveIEEEFloat32)),
      std::uint64_t(float32_source_sample_bits(range[1], FloatingSamplePolicy::PreserveIEEEFloat32))})}});
}
bool named_lut(const std::string &name) {
  return name == "Grays" || name == "Red" || name == "Green" ||
         name == "Blue" || name == "White" || name == "Cyan" ||
         name == "Magenta" || name == "Yellow";
}
void validate_mode(ChannelDisplayMode mode) {
  if (mode != ChannelDisplayMode::Color && mode != ChannelDisplayMode::Grayscale &&
      mode != ChannelDisplayMode::Composite)
    throw std::invalid_argument("Invalid channel display mode");
}
void validate_encoding(const ChannelDisplayState &state) {
  if (state.version != ChannelDisplayVersion::V1 &&
      state.version != ChannelDisplayVersion::V2 &&
      state.version != ChannelDisplayVersion::V3)
    throw std::invalid_argument("Invalid channel display version");
  if (state.projection != ChannelDisplayProjection::Sum &&
      state.projection != ChannelDisplayProjection::Max &&
      state.projection != ChannelDisplayProjection::Min)
    throw std::invalid_argument("Invalid channel display projection");
  if (state.version == ChannelDisplayVersion::V1 &&
      state.projection != ChannelDisplayProjection::Sum)
    throw std::invalid_argument("Channel display V1 requires Sum projection");
}
void validate_native_source(DatasetShape shape, PixelType type,
                            FloatingSamplePolicy policy) {
  shape.validate();
  if (type != PixelType::UInt8 && type != PixelType::UInt16 &&
      type != PixelType::Float32 && type != PixelType::Float64)
    throw std::invalid_argument("Invalid native channel display source type");
  if (policy != FloatingSamplePolicy::FiniteOnly &&
      policy != FloatingSamplePolicy::PreserveQuietNaNFloat32 &&
      policy != FloatingSamplePolicy::PreserveIEEEFloat32)
    throw std::invalid_argument("Invalid channel display sample policy");
  if (policy != FloatingSamplePolicy::FiniteOnly &&
      (type != PixelType::Float32 || shape.s != 1))
    throw std::invalid_argument("Quiet-NaN channel display requires scalar Float32");
}
void validate_source(DatasetShape shape, PixelType type,
                     FloatingSamplePolicy policy) {
  validate_native_source(shape, type, policy);
  if (shape.s != 1 || type == PixelType::Float64)
    throw std::invalid_argument("Channel display requires scalar U8/U16/F32");
}
void validate_shape_state(const ChannelDisplayState &state, DatasetShape shape,
                          bool requested = false) {
  shape.validate();
  validate_mode(state.mode);
  validate_encoding(state);
  if (shape.s != 1 || state.channels.size() != static_cast<std::size_t>(shape.c))
    throw std::invalid_argument("Channel display count or sample axis mismatch");
  if (!requested && state.mode == ChannelDisplayMode::Composite && shape.c > 8)
    throw std::invalid_argument("Canonical composite display supports at most eight channels");
  for (const auto &channel : state.channels) {
    if (state.version == ChannelDisplayVersion::V3 &&
        (!std::isfinite(channel.range[0]) || !std::isfinite(channel.range[1])))
      validate_ieee_range(channel.range);
    else validate_range(channel.range);
    if (channel.lut.table ? !channel.lut.name.empty() : !named_lut(channel.lut.name))
      throw std::invalid_argument("Channel display requires one supported named or custom LUT");
  }
}
ChannelDisplayState read_state(const MetadataObject &object, DatasetShape shape,
                              bool requested) {
  shape.validate();
  const auto schema_item = object.find("schema"), version_item = object.find("version");
  // Preserve V1 structural diagnostics for records without a complete header.
  if (schema_item == object.end() || version_item == object.end())
    exact_fields(object, {"schema", "version", "mode", "channels"});
  if (schema_item == object.end() || version_item == object.end() ||
      text_value(schema_item->second) != schema)
    throw std::invalid_argument("Unsupported channel display schema or version");
  const auto version = known_version(version_item->second);
  if (!version)
    throw std::invalid_argument("Unsupported channel display schema or version");
  if (*version == ChannelDisplayVersion::V1)
    exact_fields(object, {"schema", "version", "mode", "channels"});
  else
    exact_fields(object, {"schema", "version", "mode", "projection", "channels"});
  ChannelDisplayState state;
  state.version = *version;
  if (state.version != ChannelDisplayVersion::V1) {
    const auto &projection = text_value(object.at("projection"));
    if (projection == "sum")
      state.projection = ChannelDisplayProjection::Sum;
    else if (projection == "max")
      state.projection = ChannelDisplayProjection::Max;
    else if (projection == "min")
      state.projection = ChannelDisplayProjection::Min;
    else
      throw std::invalid_argument("Unknown channel display projection");
  }
  const auto &mode = text_value(object.at("mode"));
  if (mode == "color")
    state.mode = ChannelDisplayMode::Color;
  else if (mode == "grayscale")
    state.mode = ChannelDisplayMode::Grayscale;
  else if (mode == "composite")
    state.mode = ChannelDisplayMode::Composite;
  else
    throw std::invalid_argument("Unknown channel display mode");
  const auto &channels = array_value(object.at("channels"));
  if (channels.size() != static_cast<std::size_t>(shape.c))
    throw std::invalid_argument("Channel display channel count mismatch");
  state.channels.reserve(channels.size());
  for (const auto &value : channels) {
    const auto &record = object_value(value);
    exact_fields(record, {"visible", "range", "lut"});
    ChannelDisplayChannel channel;
    const auto visible = std::get_if<bool>(&record.at("visible"));
    if (!visible)
      throw std::invalid_argument("Channel display visibility requires a Boolean");
    channel.visible = *visible;
    if (state.version == ChannelDisplayVersion::V3 && maybe_object(record.at("range"))) {
      const auto &range = object_value(record.at("range"));
      exact_fields(range, {"float32_bits"});
      const auto &words = array_value(range.at("float32_bits"));
      if (words.size() != 2)
        throw std::invalid_argument("IEEE channel range requires two Float32 words");
      channel.range = {float32_source_sample(word_value(words[0]), FloatingSamplePolicy::PreserveIEEEFloat32),
                       float32_source_sample(word_value(words[1]), FloatingSamplePolicy::PreserveIEEEFloat32)};
      // Only nonfinite automatic bounds use the raw form. Validate through
      // integer fields before any comparisons, including hostile SNaN words.
      validate_ieee_range(channel.range);
      if (std::isfinite(channel.range[0]) && std::isfinite(channel.range[1]))
        throw std::invalid_argument("Finite channel bounds require the numeric range form");
    } else {
      const auto &range = array_value(record.at("range"));
      if (range.size() != 2)
        throw std::invalid_argument("Channel display range requires two numbers");
      channel.range = {number_value(range[0]), number_value(range[1])};
    }
    const auto &lut = object_value(record.at("lut"));
    if (lut.count("name")) {
      exact_fields(lut, {"name"});
      channel.lut.name = text_value(lut.at("name"));
    } else {
      exact_fields(lut, {"table"});
      const auto &rows = array_value(lut.at("table"));
      if (rows.size() != 256)
        throw std::invalid_argument("Channel display LUT requires 256 RGB rows");
      channel.lut.name.clear();
      channel.lut.table.emplace();
      for (std::size_t i = 0; i < rows.size(); ++i) {
        const auto &rgb = array_value(rows[i]);
        if (rgb.size() != 3)
          throw std::invalid_argument("Channel display LUT requires RGB triples");
        for (std::size_t component = 0; component < 3; ++component)
          (*channel.lut.table)[i][component] = byte_value(rgb[component]);
      }
    }
    state.channels.push_back(std::move(channel));
  }
  if (requested && state.mode == ChannelDisplayMode::Composite && shape.c > 8)
    state.mode = ChannelDisplayMode::Color;
  validate_shape_state(state, shape);
  return state;
}
const MetadataObject *known_record(const DisplayState &display) {
  const auto item = display.properties.find(property);
  if (item == display.properties.end())
    return nullptr;
  const auto object = maybe_object(item->second);
  if (!object)
    return nullptr;
  const auto schema_item = object->find("schema"), version_item = object->find("version");
  if (schema_item == object->end() || version_item == object->end())
    return nullptr;
  const auto text = std::get_if<std::string>(&schema_item->second);
  return text && *text == schema && known_version(version_item->second) ? object : nullptr;
}
std::string mode_name(ChannelDisplayMode mode) {
  switch (mode) {
  case ChannelDisplayMode::Color: return "color";
  case ChannelDisplayMode::Grayscale: return "grayscale";
  case ChannelDisplayMode::Composite: return "composite";
  }
  throw std::invalid_argument("Invalid channel display mode");
}
std::string projection_name(ChannelDisplayProjection projection) {
  switch (projection) {
  case ChannelDisplayProjection::Sum: return "sum";
  case ChannelDisplayProjection::Max: return "max";
  case ChannelDisplayProjection::Min: return "min";
  }
  throw std::invalid_argument("Invalid channel display projection");
}
std::size_t checked_product(std::size_t left, std::size_t right) {
  if (right && left > std::numeric_limits<std::size_t>::max() / right)
    throw std::invalid_argument("Channel display allocation size overflows");
  return left * right;
}
std::size_t checked_sum(std::size_t left, std::size_t right) {
  if (left > std::numeric_limits<std::size_t>::max() - right)
    throw std::invalid_argument("Channel display allocation size overflows");
  return left + right;
}
void validate_limits(ChannelDisplayLimits limits, std::size_t pixels,
                     std::size_t bytes) {
  if (!limits.max_pixels || !limits.max_working_bytes ||
      pixels > limits.max_pixels || bytes > limits.max_working_bytes)
    throw std::invalid_argument("Channel display exceeds its pixel or working-memory limit");
}
void validate_position(DatasetShape shape, int t, int c, int z) {
  if (t < 0 || t >= shape.t || c < 0 || c >= shape.c || z < 0 || z >= shape.z)
    throw std::out_of_range("Channel display T/C/Z position is out of range");
}
void validate_live_request(DatasetShape shape, PixelType type,
                           FloatingSamplePolicy policy, int t, int c, int z,
                           ChannelDisplayLimits limits) {
  validate_source(shape, type, policy);
  validate_position(shape, t, c, z);
  if (shape.x > std::numeric_limits<int>::max() - 8194 ||
      shape.y > std::numeric_limits<int>::max() - 8194)
    throw std::invalid_argument("Channel display XY dimensions exceed Image storage limits");
  const auto pixels = checked_product(static_cast<std::size_t>(shape.x), shape.y);
  if (pixels > std::vector<double>().max_size())
    throw std::invalid_argument("Channel display XY storage is not addressable");
  // One borrowed scalar XY plane; reserve its equivalent conservatively even
  // when the U8 reset needs no read or the source already owns the storage.
  validate_limits(limits, pixels, checked_product(pixels, sizeof(double)));
}
std::size_t live_pixels(DatasetShape shape) {
  return checked_product(static_cast<std::size_t>(shape.x), shape.y);
}
std::size_t state_bytes(DatasetShape shape) {
  // Optional LUT tables are inline in ChannelDisplayChannel. Eight extra
  // bytes per channel conservatively cover the longest supported name plus
  // terminator without relying on a library's small-string optimization.
  return checked_product(static_cast<std::size_t>(shape.c),
                         checked_sum(sizeof(ChannelDisplayChannel), 8));
}
void validate_image(const Image &image, int width, int height, PixelType type) {
  if (image.width != width || image.height != height || image.channels != 1 ||
      image.type != type || image.pixels.size() !=
          checked_product(static_cast<std::size_t>(width), height))
    throw std::invalid_argument("Channel display provider plane shape/type mismatch");
}
// Classify preserved samples through the reviewed bit helper before any FP
// comparison. Each policy retains its admission rules; IEEE NaNs map to zero
// while infinities participate in computed bounds and finite-range clipping.
bool finite_sample(double sample, FloatingSamplePolicy policy) {
  if (policy != FloatingSamplePolicy::FiniteOnly) {
    const auto bits = float32_source_sample_bits(sample, policy);
    return (bits & 0x7fffffffu) <= 0x7f800000u;
  }
  if (!std::isfinite(sample))
    throw std::invalid_argument("Invalid finite channel display source sample");
  return true;
}
ChannelDisplayRange live_range(const Dataset &source, DatasetShape shape,
                              PixelType type, FloatingSamplePolicy policy,
                              int t, int c, int z) {
  kernel_checkpoint();
  if (type == PixelType::UInt8)
    return {{0, 255}, false};
  auto image = source.plane({t, c, z});
  kernel_checkpoint();
  validate_image(*image, shape.x, shape.y, type);
  ChannelDisplayRange result;
  result.empty_finite = true;
  bool found = false;
  if (policy == FloatingSamplePolicy::PreserveIEEEFloat32) {
    const double nan = float32_source_sample(0x7fc00000u, policy);
    result.range = {nan, nan};
  }
  kernel_blocks("channel-display.reset-range", image->pixels.size(),
                [&](std::size_t first, std::size_t last) {
    for (auto i = first; i < last; ++i) {
      const double sample = image->pixels[i];
      if (!finite_sample(sample, policy))
        continue;
      if (std::isfinite(sample)) result.empty_finite = false;
      if (!found) {
        result.range = {sample, sample};
        found = true;
      } else {
        result.range[0] = std::min(result.range[0], sample);
        result.range[1] = std::max(result.range[1], sample);
      }
    }
  });
  kernel_checkpoint();
  return result;
}
const std::array<double, 2> &stored_range(const ChannelDisplayState &state, int c) {
  return state.channels[static_cast<std::size_t>(c)].range;
}
struct Mapping {
  PixelType type;
  double low, high, denominator, scale, requested_low, requested_high;
  bool equal_step;
  explicit Mapping(PixelType source_type, const std::array<double, 2> &range)
      : type(source_type),
        low(source_type == PixelType::Float32 ? range[0] : std::floor(range[0] + .5)),
        high(source_type == PixelType::Float32 ? range[1] : std::floor(range[1] + .5)),
        denominator(0), scale(0), requested_low(range[0]), requested_high(range[1]),
        equal_step(false) {
    if (type == PixelType::Float32 &&
        range[0] >= -double(std::numeric_limits<float>::max()) &&
        range[1] <= double(std::numeric_limits<float>::max())) {
      low = static_cast<double>(static_cast<float>(range[0]));
      high = static_cast<double>(static_cast<float>(range[1]));
    }
    denominator = type == PixelType::UInt8 ? range[1] - range[0] : high - low;
    if (type == PixelType::UInt16)
      denominator += 1;
    equal_step = type == PixelType::UInt8 ? range[0] == range[1]
                                        : type == PixelType::Float32 && low == high;
    if (!equal_step)
      scale = (type == PixelType::Float32 ? 255.0 : 256.0) / denominator;
  }
  unsigned index(double sample, FloatingSamplePolicy policy) const {
    if (!finite_sample(sample, policy))
      return 0;
    if (policy == FloatingSamplePolicy::PreserveIEEEFloat32 &&
        (!std::isfinite(low) || !std::isfinite(high))) return 0;
    if (sample <= low)
      return 0;
    if (equal_step)
      return 255;
    if (type == PixelType::Float32 && sample >= high)
      return 255;
    double mapped;
    if (std::isfinite(denominator)) {
      // Preserve processor evaluation order for ordinary typed samples.
      const double difference = sample - low;
      if (type == PixelType::UInt8) {
        const double product = 256.0 * difference;
        mapped = std::isfinite(product) ? product / denominator
                                        : (difference / denominator) * 256.0;
      } else if (std::isfinite(scale))
        mapped = difference * scale;
      else
        mapped = (difference / denominator) * 255.0;
    } else {
      // Finite metadata can span more than DBL_MAX. Half-scaled differences
      // avoid overflow without changing ordinary processor arithmetic.
      const double numerator = sample * .5 - low * .5;
      const double divisor = type == PixelType::UInt8
                                 ? requested_high * .5 - requested_low * .5
                                 : high * .5 - low * .5;
      mapped = (numerator / divisor) * (type == PixelType::Float32 ? 255.0 : 256.0);
    }
    if (type != PixelType::UInt8)
      mapped += .5;
    // Check/clamp while still floating point, before any potentially unsafe
    // integer conversion. Positive overflow is a saturated high intensity.
    if (!(mapped > 0))
      return 0;
    if (mapped >= 255)
      return 255;
    return static_cast<unsigned>(mapped);
  }
};
} // namespace

void validate_channel_display(const ChannelDisplayState &state, DatasetShape shape,
                              PixelType type, FloatingSamplePolicy policy) {
  validate_source(shape, type, policy);
  if ((state.version == ChannelDisplayVersion::V3) !=
      (policy == FloatingSamplePolicy::PreserveIEEEFloat32))
    throw std::invalid_argument("IEEE Float32 channel display requires explicit V3 state");
  validate_shape_state(state, shape);
}
ChannelDisplayState parse_channel_display(const MetadataObject &object,
                                          DatasetShape shape, PixelType type,
                                          FloatingSamplePolicy policy) {
  validate_source(shape, type, policy);
  auto state = read_state(object, shape, false);
  validate_channel_display(state, shape, type, policy);
  return state;
}
ChannelDisplayState parse_requested_channel_display(
    const MetadataObject &object, DatasetShape shape, PixelType type,
    FloatingSamplePolicy policy) {
  validate_source(shape, type, policy);
  auto state = read_state(object, shape, true);
  validate_channel_display(state, shape, type, policy);
  return state;
}
MetadataObject encode_channel_display(const ChannelDisplayState &state,
                                     DatasetShape shape, PixelType type,
                                     FloatingSamplePolicy policy) {
  validate_channel_display(state, shape, type, policy);
  MetadataArray channels;
  channels.reserve(state.channels.size());
  for (const auto &channel : state.channels) {
    MetadataObject lut;
    if (channel.lut.table) {
      MetadataArray rows;
      rows.reserve(256);
      for (const auto &rgb : *channel.lut.table)
        rows.emplace_back(metadata_array({std::int64_t(rgb[0]),
                                          std::int64_t(rgb[1]),
                                          std::int64_t(rgb[2])}));
      lut.emplace("table", metadata_array(std::move(rows)));
    } else
      lut.emplace("name", channel.lut.name);
    channels.emplace_back(metadata_object({
        {"visible", channel.visible},
        {"range", encoded_range(channel.range)},
        {"lut", metadata_object(std::move(lut))}}));
  }
  MetadataObject result{
      {"schema", std::string(schema)},
      {"version", std::int64_t(static_cast<std::uint8_t>(state.version))},
      {"mode", mode_name(state.mode)},
      {"channels", metadata_array(std::move(channels))}};
  if (state.version != ChannelDisplayVersion::V1)
    result.emplace("projection", projection_name(state.projection));
  return result;
}
void validate_known_channel_display(const DisplayState &display, DatasetShape shape) {
  if (const auto record = known_record(display))
    (void)read_state(*record, shape, false);
}
void validate_known_channel_display(const DisplayState &display, const Dataset &source) {
  if (const auto record = known_record(display))
    (void)parse_channel_display(*record, source.shape(), source.pixel_type(),
                                source.sample_policy());
}
std::optional<ChannelDisplayState>
recognized_channel_display(const DisplayState &display, const Dataset &source) {
  const auto item = display.properties.find(property);
  if (item == display.properties.end())
    return std::nullopt;
  return parse_channel_display(object_value(item->second), source.shape(),
                               source.pixel_type(), source.sample_policy());
}
ChannelDisplayState normalize_channel_display(ChannelDisplayState state,
                                              DatasetShape shape, PixelType type,
                                              FloatingSamplePolicy policy) {
  if (state.mode == ChannelDisplayMode::Composite && shape.c > 8)
    state.mode = ChannelDisplayMode::Color;
  validate_channel_display(state, shape, type, policy);
  return state;
}
DisplayState with_channel_display(DisplayState display, const Dataset &source,
                                 std::optional<ChannelDisplayState> state) {
  const auto shape = source.shape();
  if (state) {
    *state = normalize_channel_display(std::move(*state), shape,
                                      source.pixel_type(), source.sample_policy());
    display.properties[property] = metadata_object(encode_channel_display(
        *state, shape, source.pixel_type(), source.sample_policy()));
  } else
    display.properties.erase(property);
  display.validate(shape);
  return display;
}
ChannelDisplayTable channel_display_lut(const std::string &name) {
  if (!named_lut(name))
    throw std::invalid_argument("Unsupported channel display LUT name");
  const bool red = name == "Grays" || name == "White" || name == "Red" ||
                   name == "Magenta" || name == "Yellow";
  const bool green = name == "Grays" || name == "White" || name == "Green" ||
                     name == "Cyan" || name == "Yellow";
  const bool blue = name == "Grays" || name == "White" || name == "Blue" ||
                    name == "Cyan" || name == "Magenta";
  ChannelDisplayTable table{};
  for (std::size_t i = 0; i < table.size(); ++i)
    table[i] = {static_cast<std::uint8_t>(red ? i : 0),
                static_cast<std::uint8_t>(green ? i : 0),
                static_cast<std::uint8_t>(blue ? i : 0)};
  return table;
}
ChannelDisplayState default_channel_display(const Dataset &input, int t, int z,
                                           ChannelDisplayMode requested_mode,
                                           ChannelDisplayLimits limits,
                                           const ExecutionControl &control) {
  const Dataset source = input;
  const auto shape = source.shape();
  const auto type = source.pixel_type();
  const auto policy = source.sample_policy();
  validate_live_request(shape, type, policy, t, 0, z, limits);
  validate_mode(requested_mode);
  validate_limits(limits, live_pixels(shape),
                  checked_sum(checked_product(live_pixels(shape), sizeof(double)),
                              state_bytes(shape)));
  ChannelDisplayState state;
  state.mode = requested_mode;
  if (policy == FloatingSamplePolicy::PreserveIEEEFloat32) state.version = ChannelDisplayVersion::V3;
  state.channels.resize(static_cast<std::size_t>(shape.c));
  state = normalize_channel_display(std::move(state), shape, type, policy);
  const ExecutionControl owned_control = control;
  ExecutionScope scope(owned_control);
  kernel_checkpoint();
  const auto range = live_range(source, shape, type, policy, t, 0, z);
  static constexpr const char *palette[] = {
      "Red", "Green", "Blue", "White", "Cyan", "Magenta", "Yellow", "White"};
  kernel_blocks("channel-display.defaults", state.channels.size(),
                [&](std::size_t first, std::size_t last) {
    for (auto c = first; c < last; ++c) {
      state.channels[c].range = range.range;
      state.channels[c].lut.name = palette[std::min<std::size_t>(c, 7)];
    }
  });
  kernel_checkpoint();
  return state;
}
std::pair<ChannelDisplayState, std::vector<ChannelDisplayRange>>
reset_channel_display_ranges(const Dataset &input,
                             const ChannelDisplayState &input_state,
                             int t, int z, const std::vector<int> &input_channels,
                             ChannelDisplayLimits limits,
                             const ExecutionControl &control) {
  const Dataset source = input;
  const auto shape = source.shape();
  const auto type = source.pixel_type();
  const auto policy = source.sample_policy();
  validate_source(shape, type, policy);
  validate_shape_state(input_state, shape, true);
  validate_live_request(shape, type, policy, t, 0, z, limits);
  const auto count = input_channels.empty() ? static_cast<std::size_t>(shape.c)
                                             : input_channels.size();
  if (count > static_cast<std::size_t>(shape.c))
    throw std::invalid_argument("Duplicate or excessive channel display reset channels");
  for (int c : input_channels)
    validate_position(shape, t, c, z);
  auto bytes = checked_sum(checked_product(live_pixels(shape), sizeof(double)),
                           state_bytes(shape));
  bytes = checked_sum(bytes, checked_product(count, sizeof(ChannelDisplayRange)));
  bytes = checked_sum(bytes, checked_product(count, 2 * sizeof(int)));
  validate_limits(limits, live_pixels(shape), bytes);
  std::vector<int> channels = input_channels;
  if (channels.empty()) {
    channels.reserve(static_cast<std::size_t>(shape.c));
    for (int c = 0; c < shape.c; ++c)
      channels.push_back(c);
  }
  {
    auto sorted = channels;
    std::sort(sorted.begin(), sorted.end());
    if (std::adjacent_find(sorted.begin(), sorted.end()) != sorted.end())
      throw std::invalid_argument("Duplicate channel display reset channel");
  }
  auto state = normalize_channel_display(input_state, shape, type, policy);
  std::vector<ChannelDisplayRange> observations;
  observations.reserve(channels.size());
  const ExecutionControl owned_control = control;
  ExecutionScope scope(owned_control);
  kernel_checkpoint();
  execution_progress(0, channels.size());
  for (int c : channels) {
    const auto range = live_range(source, shape, type, policy, t, c, z);
    state.channels[static_cast<std::size_t>(c)].range = range.range;
    observations.push_back(range);
    execution_progress(observations.size(), channels.size());
    kernel_checkpoint();
  }
  return {std::move(state), std::move(observations)};
}
ChannelDisplayRange effective_channel_display_range(
    const Dataset &input, const std::optional<ChannelDisplayState> &input_state,
    int t, int c, int z, std::optional<std::array<double, 2>> legacy_range,
    ChannelDisplayLimits limits, const ExecutionControl &control) {
  const Dataset source = input;
  const auto shape = source.shape();
  const auto type = source.pixel_type();
  const auto policy = source.sample_policy();
  validate_native_source(shape, type, policy);
  validate_position(shape, t, c, z);
  if (input_state) {
    validate_channel_display(*input_state, shape, type, policy);
    return {stored_range(*input_state, c), false};
  }
  if (legacy_range) {
    validate_range(*legacy_range);
    return {*legacy_range, false};
  }
  validate_live_request(shape, type, policy, t, c, z, limits);
  const ExecutionControl owned_control = control;
  ExecutionScope scope(owned_control);
  kernel_checkpoint();
  return live_range(source, shape, type, policy, t, c, z);
}
ChannelDisplayEstimate estimate_channel_display(
    const Dataset &source, const ChannelDisplayState &state, Plane plane,
    int coordinate, int t, int active_c, ChannelDisplayLimits limits) {
  const auto shape = source.shape();
  validate_channel_display(state, shape, source.pixel_type(), source.sample_policy());
  validate_position(shape, t, active_c, 0);
  ChannelDisplayEstimate estimate;
  int fixed_size;
  switch (plane) {
  case Plane::XY:
    estimate.width = shape.x; estimate.height = shape.y; fixed_size = shape.z;
    break;
  case Plane::XZ:
    estimate.width = shape.x; estimate.height = shape.z; fixed_size = shape.y;
    break;
  case Plane::YZ:
    estimate.width = shape.y; estimate.height = shape.z; fixed_size = shape.x;
    break;
  default:
    throw std::invalid_argument("Invalid channel display plane");
  }
  if (coordinate < 0 || coordinate >= fixed_size)
    throw std::out_of_range("Channel display fixed coordinate is out of range");
  if (estimate.width > std::numeric_limits<int>::max() - 8194 ||
      estimate.height > std::numeric_limits<int>::max() - 8194)
    throw std::invalid_argument("Channel display dimensions exceed Image storage limits");
  estimate.pixels = checked_product(static_cast<std::size_t>(estimate.width),
                                    estimate.height);
  if (checked_product(estimate.pixels, 3) > std::vector<double>().max_size())
    throw std::invalid_argument("Channel display RGB storage is not addressable");
  auto samples = checked_product(estimate.pixels, 4);
  if (plane != Plane::XY)
    samples = checked_sum(samples, static_cast<std::size_t>(estimate.width));
  estimate.working_bytes = checked_sum(
      checked_product(samples, sizeof(double)),
      sizeof(std::array<ChannelDisplayTable, 8>) +
          sizeof(std::array<std::array<double, 2>, 8>));
  validate_limits(limits, estimate.pixels, estimate.working_bytes);
  if (state.mode == ChannelDisplayMode::Composite) {
    for (int c = 0; c < shape.c; ++c)
      if (state.channels[static_cast<std::size_t>(c)].visible)
        estimate.contributing_channels.push_back(c);
  } else
    estimate.contributing_channels.push_back(active_c);
  return estimate;
}
Image compose_channel_display(const Dataset &input,
                             const ChannelDisplayState &input_state, Plane plane,
                             int coordinate, int t, int active_c,
                             ChannelDisplayPlaneProvider provider,
                             ChannelDisplayLimits limits,
                             const ExecutionControl &control) {
  // Own every caller-controlled object used after the first callback. Immutable
  // source storage and provider results remain retained for their whole use.
  const Dataset source = input;
  const auto estimate = estimate_channel_display(source, input_state, plane, coordinate,
                                                t, active_c, limits);
  const auto shape = source.shape();
  const auto type = source.pixel_type();
  const auto policy = source.sample_policy();
  const auto mode = input_state.mode;
  const auto projection = mode == ChannelDisplayMode::Composite
                              ? input_state.projection
                              : ChannelDisplayProjection::Sum;
  // At most eight channels contribute. Pin only their fixed-size numerical
  // state before callbacks, rather than copying a potentially huge C table.
  std::array<ChannelDisplayTable, 8> tables{};
  std::array<std::array<double, 2>, 8> ranges{};
  for (std::size_t i = 0; i < estimate.contributing_channels.size(); ++i) {
    const int c = estimate.contributing_channels[i];
    const auto &channel = input_state.channels[static_cast<std::size_t>(c)];
    tables[i] = mode == ChannelDisplayMode::Grayscale ||
                        (mode == ChannelDisplayMode::Composite && shape.c == 1)
                    ? channel_display_lut("Grays")
                    : channel.lut.table ? *channel.lut.table
                                        : channel_display_lut(channel.lut.name);
    ranges[i] = stored_range(input_state, c);
  }
  const ExecutionControl owned_control = control;
  ExecutionScope scope(owned_control);
  kernel_checkpoint();
  Image output(estimate.width, estimate.height, 3, PixelType::UInt8);
  kernel_checkpoint();
  execution_progress(0, estimate.contributing_channels.size());
  std::size_t completed = 0;
  for (int c : estimate.contributing_channels) {
    kernel_checkpoint();
    std::shared_ptr<const Image> image;
    if (provider)
      image = provider(plane, coordinate, t, c);
    else if (plane == Plane::XY)
      image = source.plane({t, c, coordinate});
    else
      image = std::make_shared<const Image>(source.extract_plane(plane, coordinate, t, c));
    kernel_checkpoint();
    if (!image)
      throw std::invalid_argument("Channel display provider returned a null plane");
    validate_image(*image, estimate.width, estimate.height, type);
    const auto &lut = tables[completed];
    const Mapping mapping(type, ranges[completed]);
    kernel_blocks("channel-display.map", estimate.pixels,
                  [&](std::size_t first, std::size_t last) {
      for (auto i = first; i < last; ++i) {
        const auto &rgb = lut[mapping.index(image->pixels[i], policy)];
        for (std::size_t component = 0; component < 3; ++component) {
          auto &sample = output.pixels[i * 3 + component];
          switch (projection) {
          case ChannelDisplayProjection::Sum:
            sample = std::min(255.0, sample + rgb[component]);
            break;
          case ChannelDisplayProjection::Max:
            sample = std::max(sample, static_cast<double>(rgb[component]));
            break;
          case ChannelDisplayProjection::Min:
            sample = completed == 0
                         ? static_cast<double>(rgb[component])
                         : std::min(sample, static_cast<double>(rgb[component]));
            break;
          }
        }
      }
    });
    image.reset();
    execution_progress(++completed, estimate.contributing_channels.size());
    kernel_checkpoint();
  }
  kernel_checkpoint();
  return output;
}

} // namespace imagejxx
