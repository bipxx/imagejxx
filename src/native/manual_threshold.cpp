#include <imagejxx/manual_threshold.hpp>

#include "kernel_control.hpp"
#include "manual_threshold_display_internal.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace imagejxx {
namespace {
constexpr std::size_t bins = 256;
constexpr std::size_t analysis_bytes =
    bins * sizeof(std::uint64_t) + (bins + 1) * sizeof(double);

void validate_type(PixelType type) {
  switch (type) {
  case PixelType::UInt8:
  case PixelType::UInt16:
  case PixelType::Float32:
  case PixelType::Float64:
    return;
  }
  throw std::invalid_argument("Unsupported manual threshold pixel type");
}
void validate_position(DatasetShape shape, PlanePosition position) {
  if (position.t < 0 || position.t >= shape.t || position.c < 0 ||
      position.c >= shape.c || position.z < 0 || position.z >= shape.z)
    throw std::out_of_range("Manual threshold T/C/Z is outside the dataset");
}
std::size_t multiply(std::size_t a, std::size_t b) {
  if (a > std::numeric_limits<std::size_t>::max() / b)
    throw std::invalid_argument("Manual threshold storage exceeds addressable size");
  return a * b;
}
void check_working(std::size_t bytes, const ManualThresholdLimits &limits) {
  if (bytes > limits.max_working_bytes)
    throw std::invalid_argument("Manual threshold operation exceeds working byte limit");
}
ManualThresholdEstimate estimate_image(const Image &image,
                                       const ManualThresholdLimits &limits) {
  const auto estimate = estimate_manual_threshold(
      {1, 1, 1, image.height, image.width, image.channels}, image.type, {}, limits);
  if (image.pixels.size() != estimate.pixel_count)
    throw std::invalid_argument("Manual threshold plane storage does not match its extent");
  return estimate;
}
void validate_value(double value, PixelType type) {
  require_finite(value, "Manual threshold source sample");
  if (type == PixelType::UInt8 || type == PixelType::UInt16) {
    if (value < 0 || value > type_max(type) || std::trunc(value) != value)
      throw std::invalid_argument("Manual threshold integer sample is not canonical");
  } else if (type == PixelType::Float32) {
    if (value < -double(std::numeric_limits<float>::max()) ||
        value > double(std::numeric_limits<float>::max()) ||
        double(static_cast<float>(value)) != value)
      throw std::invalid_argument("Manual threshold Float32 sample is not canonical");
  }
}
void validate_samples(const Image &source) {
  kernel_blocks("Manual threshold sample validation", source.pixels.size(),
                [&](std::size_t first, std::size_t last) {
                  for (auto i = first; i < last; ++i)
                    validate_value(source.pixels[i], source.type);
                });
}
bool matches(double value, ManualThresholdRange range) {
  return value >= range.lower && value <= range.upper;
}
// Convex interpolation avoids overflowing max-min when the interval crosses
// zero; a same-sign interval uses a difference that is representable.
double edge(double lower, double upper, double fraction) {
  if (lower <= 0 && upper >= 0)
    return lower * (1 - fraction) + upper * fraction;
  return lower + (upper - lower) * fraction;
}
double normalized(double value, double lower, double upper) {
  const double span = upper - lower;
  if (std::isfinite(span))
    return (value - lower) / span;
  const double scale = std::max(std::abs(lower), std::abs(upper));
  return (value / scale - lower / scale) / (upper / scale - lower / scale);
}
const MetadataObject &object(const MetadataValue &value) {
  const auto tree = std::get_if<std::shared_ptr<const MetadataTree>>(&value);
  if (!tree || !*tree || !std::holds_alternative<MetadataObject>((*tree)->value))
    throw std::invalid_argument("Manual threshold display property must be an object");
  return std::get<MetadataObject>((*tree)->value);
}
int integer(const MetadataValue &value) {
  if (const auto signed_value = std::get_if<std::int64_t>(&value)) {
    if (*signed_value >= 0 && *signed_value <= std::numeric_limits<int>::max())
      return static_cast<int>(*signed_value);
  } else if (const auto unsigned_value = std::get_if<std::uint64_t>(&value)) {
    if (*unsigned_value <= std::uint64_t(std::numeric_limits<int>::max()))
      return static_cast<int>(*unsigned_value);
  }
  throw std::invalid_argument("Manual threshold version and T/C/Z must be nonnegative integers");
}
double numeric(const MetadataValue &value) {
  double result;
  if (const auto number = std::get_if<double>(&value)) result = *number;
  else if (const auto number = std::get_if<std::int64_t>(&value)) result = double(*number);
  else if (const auto number = std::get_if<std::uint64_t>(&value)) result = double(*number);
  else throw std::invalid_argument("Manual threshold intensity bounds must be numbers");
  require_finite(result, "Manual threshold intensity bound");
  return result;
}
} // namespace

void ManualThresholdRange::validate() const {
  require_finite(lower, "Manual threshold lower bound");
  require_finite(upper, "Manual threshold upper bound");
  if (lower > upper)
    throw std::invalid_argument("Manual threshold upper bound must be at least lower bound");
}
ManualThresholdEstimate estimate_manual_threshold(
    DatasetShape shape, PixelType type, PlanePosition position,
    const ManualThresholdLimits &limits) {
  shape.validate();
  validate_type(type);
  if (shape.s != 1)
    throw std::invalid_argument("Manual threshold requires a scalar plane");
  validate_position(shape, position);
  if (limits.max_pixels == 0 || limits.max_working_bytes == 0)
    throw std::invalid_argument("Manual threshold limits must be positive");
  const auto count = multiply(std::size_t(shape.x), std::size_t(shape.y));
  if (count > limits.max_pixels)
    throw std::invalid_argument("Manual threshold plane exceeds pixel limit");
  return {count, analysis_bytes, multiply(count, sizeof(double)),
          count / 8 + (count % 8 != 0)};
}
ManualThresholdSummary analyze_manual_threshold(
    const Image &source, ManualThresholdRange range,
    const ManualThresholdLimits &limits, const ExecutionControl &control) {
  range.validate();
  const auto estimate = estimate_image(source, limits);
  check_working(estimate.analysis_bytes, limits);
  ExecutionScope scope(control);
  kernel_checkpoint();
  ManualThresholdSummary result;
  result.minimum = result.maximum = source.pixels.front();
  kernel_blocks("Manual threshold analysis", estimate.pixel_count,
                [&](std::size_t first, std::size_t last) {
                  for (auto i = first; i < last; ++i) {
                    const double value = source.pixels[i];
                    validate_value(value, source.type);
                    result.minimum = std::min(result.minimum, value);
                    result.maximum = std::max(result.maximum, value);
                    result.matched_count += matches(value, range);
                  }
                });
  result.sample_count = estimate.pixel_count;
  kernel_checkpoint();
  result.histogram.counts.assign(bins, 0);
  result.histogram.edges.resize(bins + 1);
  double lower = result.minimum, upper = result.maximum;
  if (source.type == PixelType::UInt8) { lower = 0; upper = 256; }
  else if (source.type == PixelType::UInt16) { lower = 0; upper = 65536; }
  for (std::size_t i = 0; i <= bins; ++i)
    result.histogram.edges[i] = edge(lower, upper, double(i) / bins);
  result.histogram.edges.front() = lower;
  result.histogram.edges.back() = upper;
  kernel_blocks("Manual threshold histogram", estimate.pixel_count,
                [&](std::size_t first, std::size_t last) {
                  for (auto i = first; i < last; ++i) {
                    const double fraction = lower == upper ? 0 :
                        normalized(source.pixels[i], lower, upper);
                    const auto bin = std::size_t(std::clamp(
                        std::floor(fraction * bins), 0.0, double(bins - 1)));
                    ++result.histogram.counts[bin];
                  }
                });
  kernel_checkpoint();
  return result;
}
Image manual_threshold_mask(const Image &source, ManualThresholdRange range,
                            const ManualThresholdLimits &limits,
                            const ExecutionControl &control) {
  range.validate();
  const auto estimate = estimate_image(source, limits);
  check_working(estimate.mask_bytes, limits);
  ExecutionScope scope(control);
  kernel_checkpoint();
  validate_samples(source);
  kernel_checkpoint();
  auto result = threshold(source, range.lower, range.upper);
  kernel_checkpoint();
  return result;
}
Selection manual_threshold_selection(const Image &source,
                                      ManualThresholdRange range,
                                      const ManualThresholdLimits &limits,
                                      const ExecutionControl &control) {
  range.validate();
  const auto estimate = estimate_image(source, limits);
  check_working(estimate.selection_bytes, limits);
  ExecutionScope scope(control);
  kernel_checkpoint();
  validate_samples(source);
  kernel_checkpoint();
  std::vector<unsigned char> bytes(estimate.selection_bytes, 0);
  kernel_blocks("Manual threshold selection", estimate.pixel_count,
                [&](std::size_t first, std::size_t last) {
                  for (auto i = first; i < last; ++i)
                    if (matches(source.pixels[i], range))
                      bytes[i / 8] |= static_cast<unsigned char>(1u << (7 - i % 8));
                });
  kernel_checkpoint();
  return Selection::from_packed_mask(source.width, source.height, std::move(bytes));
}
void blend_manual_threshold_overlay(const Image &source, Image &rgb,
                                     ManualThresholdRange range,
                                     const ManualThresholdLimits &limits,
                                     const ExecutionControl &control) {
  detail::blend_manual_threshold_source_overlay(source,rgb,range,
      FloatingSamplePolicy::FiniteOnly,limits,control);
}
void detail::blend_manual_threshold_source_overlay(const Image &source, Image &rgb,
                                     ManualThresholdRange range,
                                     FloatingSamplePolicy policy,
                                     const ManualThresholdLimits &limits,
                                     const ExecutionControl &control) {
  range.validate();
  const auto estimate = estimate_image(source, limits);
  if (rgb.width != source.width || rgb.height != source.height ||
      rgb.channels != 3 || rgb.type != PixelType::UInt8 ||
      rgb.pixels.size() != multiply(estimate.pixel_count, 3))
    throw std::invalid_argument("Manual threshold overlay requires same-extent UInt8 RGB output");
  ExecutionScope scope(control);
  kernel_checkpoint();
  if (policy == FloatingSamplePolicy::FiniteOnly) validate_samples(source);
  else if ((policy == FloatingSamplePolicy::PreserveQuietNaNFloat32 ||
            policy == FloatingSamplePolicy::PreserveIEEEFloat32) &&
           source.type == PixelType::Float32 && source.channels == 1) {
    kernel_blocks("Manual threshold display source validation",source.pixels.size(),
        [&](std::size_t first,std::size_t last) {
          for(auto i=first;i<last;++i)
            (void)float32_source_sample_bits(source.pixels[i],policy);
        });
  } else throw std::invalid_argument("Unsupported manual threshold display source policy");
  kernel_blocks("Manual threshold display validation", rgb.pixels.size(),
                [&](std::size_t first, std::size_t last) {
                  for (auto i = first; i < last; ++i)
                    validate_value(rgb.pixels[i], PixelType::UInt8);
                });
  kernel_checkpoint();
  kernel_blocks("Manual threshold overlay", estimate.pixel_count,
                [&](std::size_t first, std::size_t last) {
                  for (auto i = first; i < last; ++i) {
                    if (policy != FloatingSamplePolicy::FiniteOnly) {
                      const auto word=float32_source_sample_bits(source.pixels[i],policy);
                      if ((word & 0x7f800000u)==0x7f800000u) continue;
                    }
                    if (!matches(source.pixels[i], range)) continue;
                    rgb.pixels[i * 3] = 255;
                    rgb.pixels[i * 3 + 1] = std::floor(rgb.pixels[i * 3 + 1] / 2);
                    rgb.pixels[i * 3 + 2] = std::floor(rgb.pixels[i * 3 + 2] / 2);
                  }
                });
  kernel_checkpoint();
}
std::optional<ManualThresholdState>
manual_threshold_state(const DisplayState &display, DatasetShape shape) {
  shape.validate();
  const auto found = display.properties.find(manual_threshold_property);
  if (found == display.properties.end()) return std::nullopt;
  const auto &record = object(found->second);
  for (const auto *key : {"version", "lower", "upper", "t", "c", "z"})
    if (record.find(key) == record.end())
      throw std::invalid_argument("Manual threshold display property has missing fields");
  if (record.size() != 6 || integer(record.at("version")) != 1)
    throw std::invalid_argument("Unsupported manual threshold display schema");
  ManualThresholdState state{{numeric(record.at("lower")), numeric(record.at("upper"))},
                             {integer(record.at("t")), integer(record.at("c")),
                              integer(record.at("z"))}};
  state.range.validate();
  if (shape.s != 1)
    throw std::invalid_argument("Manual threshold display requires a scalar dataset");
  validate_position(shape, state.position);
  return state;
}
void set_manual_threshold_state(DisplayState &display, DatasetShape shape,
                                const ManualThresholdState &state) {
  shape.validate();
  state.range.validate();
  if (shape.s != 1)
    throw std::invalid_argument("Manual threshold display requires a scalar dataset");
  validate_position(shape, state.position);
  display.properties[manual_threshold_property] = metadata_object({
      {"version", std::int64_t(1)}, {"lower", state.range.lower},
      {"upper", state.range.upper}, {"t", std::int64_t(state.position.t)},
      {"c", std::int64_t(state.position.c)}, {"z", std::int64_t(state.position.z)}});
}
void reset_manual_threshold_state(DisplayState &display) {
  display.properties.erase(manual_threshold_property);
}

} // namespace imagejxx
