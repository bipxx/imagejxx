#include <imagejxx/rank.hpp>
#include <imagejxx/operations.hpp>
#include "kernel_control.hpp"
#include "rank_display_internal.hpp"
#include "dataset_histogram_internal.hpp"
#include "axis_command_record.hpp"
#include <imagejxx/manual_threshold.hpp>
#include <imagejxx/channel_display.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <stdexcept>

namespace imagejxx {
namespace {

std::size_t checked_product(std::size_t first, std::size_t second) {
  if (second != 0 && first > std::numeric_limits<std::size_t>::max() / second)
    throw std::invalid_argument("Rank filter storage size overflows");
  return first * second;
}

void validate_radius(double radius) {
  if (!std::isfinite(radius) || radius < 0 || radius > 1000)
    throw std::invalid_argument("ImageJ rank radius must be finite and in [0,1000]");
}

void validate_method(RankFilterMethod method, double radius) {
  switch (method) {
  case RankFilterMethod::Minimum:
  case RankFilterMethod::Maximum:
  case RankFilterMethod::Mean:
  case RankFilterMethod::Variance:
    break;
  case RankFilterMethod::Median:
    if (radius > 100)
      throw std::invalid_argument("ImageJ median radius must be in [0,100]");
    break;
  default:
    throw std::invalid_argument("Unknown ImageJ rank filter method");
  }
}

std::size_t validate_source(const Image &source) {
  kernel_checkpoint();
  if (source.width <= 0 || source.height <= 0 || source.channels != 1)
    throw std::invalid_argument("ImageJ rank filters require positive grayscale dimensions");
  const bool integer = source.type == PixelType::UInt8 ||
                       source.type == PixelType::UInt16;
  if (!integer && source.type != PixelType::Float32)
    throw std::invalid_argument("ImageJ rank filters support UInt8, UInt16 and Float32 grayscale");
  const auto count = checked_product(static_cast<std::size_t>(source.width),
                                     static_cast<std::size_t>(source.height));
  if (count > std::vector<double>().max_size() || count != source.pixels.size())
    throw std::invalid_argument("Rank filter source storage does not match shape");
  checked_product(count, sizeof(double));
  const double maximum = source.type == PixelType::UInt8 ? 255.0 : 65535.0;
  const double float_maximum = std::numeric_limits<float>::max();
  kernel_blocks("ImageJ rank validation", count,
                [&](std::size_t first, std::size_t last) {
                  for (auto index = first; index < last; ++index) {
                    const double value = source.pixels[index];
                    if (!std::isfinite(value))
                      throw std::invalid_argument("Rank filter source samples must be finite");
                    if (integer) {
                      if (value < 0 || value > maximum || value != std::trunc(value) ||
                          (value == 0 && std::signbit(value)))
                        throw std::invalid_argument("Rank filter integer samples must be canonical values in their tagged range");
                    } else if (value < -float_maximum || value > float_maximum ||
                               static_cast<double>(static_cast<float>(value)) != value) {
                      throw std::invalid_argument("Rank filter Float32 samples must be exactly represented Float32 values");
                    }
                  }
                });
  return count;
}

RankFilterExtent validate_extent(const Image &source,
                                std::optional<RankFilterExtent> extent) {
  const auto area = extent.value_or(
      RankFilterExtent{0, 0, source.width, source.height});
  const auto right = static_cast<std::int64_t>(area.x) + area.width;
  const auto bottom = static_cast<std::int64_t>(area.y) + area.height;
  if (area.x < 0 || area.y < 0 || area.width < 0 || area.height < 0 ||
      right > source.width || bottom > source.height)
    throw std::invalid_argument("Rank filter extent is outside the source image");
  return area;
}

// All neighborhood coordinates are widened before halo arithmetic, even for
// manually modified Image dimensions near the signed-int limit.
float sample(const Image &source, std::int64_t x, std::int64_t y) {
  x = std::clamp<std::int64_t>(x, 0, source.width - 1);
  y = std::clamp<std::int64_t>(y, 0, source.height - 1);
  return static_cast<float>(source.pixels[static_cast<std::size_t>(y) *
                                             source.width +
                                         static_cast<std::size_t>(x)]);
}

template <class Stage, class Function>
void visit_neighborhood(const Image &source, const RankFilterKernel &kernel,
                        int x, int y, const Stage &stage, Function function,
                        bool omit_rightmost = false) {
  const auto vertical_radius = static_cast<std::int64_t>(kernel.half_widths.size() / 2);
  for (std::size_t row = 0; row < kernel.half_widths.size(); ++row) {
    const auto half = static_cast<std::int64_t>(kernel.half_widths[row]);
    const auto row_y = static_cast<std::int64_t>(y) +
                       static_cast<std::int64_t>(row) - vertical_radius;
    const auto end = half - static_cast<int>(omit_rightmost);
    for (auto dx = -half; dx <= end; ++dx) {
      function(sample(source, static_cast<std::int64_t>(x) + dx, row_y));
      stage.poll();
    }
  }
}

template <class Stage>
float side_extreme(const Image &source, const RankFilterKernel &kernel,
                   int x, int y, float sign, bool incoming,
                   const Stage &stage) {
  float value = -std::numeric_limits<float>::max();
  const auto vertical_radius = static_cast<std::int64_t>(kernel.half_widths.size() / 2);
  for (std::size_t row = 0; row < kernel.half_widths.size(); ++row) {
    const auto half = static_cast<std::int64_t>(kernel.half_widths[row]);
    const auto offset_x = incoming ? half : -half - 1;
    const float candidate = sample(source, static_cast<std::int64_t>(x) + offset_x,
                                   static_cast<std::int64_t>(y) +
                                       static_cast<std::int64_t>(row) - vertical_radius) * sign;
    if (value < candidate)
      value = candidate;
    stage.poll();
  }
  return value;
}

template <class Stage>
float extreme(const Image &source, const RankFilterKernel &kernel,
              int x, int y, float sign, bool full, float previous,
              const Stage &stage) {
  if (full) {
    float value = -std::numeric_limits<float>::max();
    visit_neighborhood(source, kernel, x, y, stage, [&](float sample_value) {
      const float candidate = sample_value * sign;
      if (value < candidate)
        value = candidate;
    });
    return value;
  }
  const float incoming = side_extreme(source, kernel, x, y, sign, true, stage);
  if (incoming >= previous)
    return incoming;
  const float outgoing = side_extreme(source, kernel, x, y, sign, false, stage);
  if (outgoing < previous)
    return previous;
  float value = incoming;
  visit_neighborhood(source, kernel, x, y, stage, [&](float sample_value) {
    const float candidate = sample_value * sign;
    if (value < candidate)
      value = candidate;
  }, true);
  return value;
}

template <class Stage>
double mean_total(const Image &source, const RankFilterKernel &kernel,
                  int x, int y, bool full, double previous,
                  const Stage &stage) {
  if (full) {
    double total = 0.0;
    visit_neighborhood(source, kernel, x, y, stage,
                       [&](float value) { total += value; });
    return total;
  }
  double delta = 0.0;
  const auto vertical_radius = static_cast<std::int64_t>(kernel.half_widths.size() / 2);
  for (std::size_t row = 0; row < kernel.half_widths.size(); ++row) {
    const auto half = static_cast<std::int64_t>(kernel.half_widths[row]);
    const auto row_y = static_cast<std::int64_t>(y) +
                       static_cast<std::int64_t>(row) - vertical_radius;
    delta -= sample(source, static_cast<std::int64_t>(x) - half - 1, row_y);
    stage.poll();
    delta += sample(source, static_cast<std::int64_t>(x) + half, row_y);
    stage.poll();
  }
  return previous + delta;
}

template <class Stage>
void variance_totals(const Image &source,const RankFilterKernel &kernel,
                     int x,int y,bool full,double &sum,double &squared,
                     const Stage &stage) {
  if(full){
    sum=0.;squared=0.;
    visit_neighborhood(source,kernel,x,y,stage,[&](float sample_value){
      const double value=sample_value;sum+=value;squared+=value*value;
    });
    return;
  }
  double delta=0.,delta_squared=0.;
  const auto vertical_radius=static_cast<std::int64_t>(kernel.half_widths.size()/2);
  for(std::size_t row=0;row<kernel.half_widths.size();++row){
    const auto half=static_cast<std::int64_t>(kernel.half_widths[row]);
    const auto row_y=static_cast<std::int64_t>(y)+static_cast<std::int64_t>(row)-vertical_radius;
    const double outgoing=sample(source,static_cast<std::int64_t>(x)-half-1,row_y);
    delta-=outgoing;delta_squared-=outgoing*outgoing;stage.poll();
    const double incoming=sample(source,static_cast<std::int64_t>(x)+half,row_y);
    delta+=incoming;delta_squared+=incoming*incoming;stage.poll();
  }
  sum+=delta;squared+=delta_squared;
}

// Numeric equals may carry different zero signs. The partition's exact visit
// and swap order preserves the observed Hoare-selection tie behavior.
template <class Stage>
float select_ordered(std::vector<float> &values, std::size_t count,
                     std::size_t selected, const Stage &stage) {
  std::ptrdiff_t left = 0;
  std::ptrdiff_t right = static_cast<std::ptrdiff_t>(count) - 1;
  const auto target = static_cast<std::ptrdiff_t>(selected);
  float pivot = values[selected];
  while (left < right) {
    auto low = left;
    auto high = right;
    do {
      while (values[static_cast<std::size_t>(low)] < pivot) {
        ++low;
        stage.poll();
      }
      while (pivot < values[static_cast<std::size_t>(high)]) {
        --high;
        stage.poll();
      }
      std::swap(values[static_cast<std::size_t>(high)],
                values[static_cast<std::size_t>(low)]);
      ++low;
      --high;
      stage.poll();
    } while (high >= target && low <= target);
    if (high < target)
      left = low;
    if (target < low)
      right = high;
    pivot = values[selected];
  }
  return pivot;
}

template <class Stage>
float median(const Image &source, const RankFilterKernel &kernel,
             int x, int y, float guess, std::vector<float> &above,
             std::vector<float> &below, const Stage &stage) {
  std::size_t above_count = 0;
  std::size_t below_count = 0;
  visit_neighborhood(source, kernel, x, y, stage, [&](float value) {
    if (value > guess)
      above[above_count++] = value;
    else if (value < guess)
      below[below_count++] = value;
  });
  const auto half = kernel.point_count / 2;
  if (above_count > half)
    return select_ordered(above, above_count, above_count - half - 1, stage);
  if (below_count > half)
    return select_ordered(below, below_count, half, stage);
  return guess;
}

std::int32_t java_integer(float value) {
  if (std::isnan(value))
    return 0;
  if (value >= 2147483648.0f)
    return std::numeric_limits<std::int32_t>::max();
  if (value <= -2147483648.0f)
    return std::numeric_limits<std::int32_t>::min();
  return static_cast<std::int32_t>(value);
}

double output_sample(float value, PixelType type) {
  if (type == PixelType::Float32)
    return static_cast<double>(value);
  const float rounded = value + 0.5f;
  const auto integer = static_cast<std::uint32_t>(java_integer(rounded));
  return static_cast<double>(integer & (type == PixelType::UInt8 ? 255u : 65535u));
}

} // namespace

RankFilterMethod parse_rank_filter_method(const std::string &method) {
  if (method == "minimum")
    return RankFilterMethod::Minimum;
  if (method == "maximum")
    return RankFilterMethod::Maximum;
  if (method == "median")
    return RankFilterMethod::Median;
  if (method == "mean")
    return RankFilterMethod::Mean;
  if (method == "variance")
    return RankFilterMethod::Variance;
  throw std::invalid_argument("Unknown ImageJ rank filter method");
}

std::string rank_filter_method_name(RankFilterMethod method) {
  switch (method) {
  case RankFilterMethod::Minimum:
    return "minimum";
  case RankFilterMethod::Maximum:
    return "maximum";
  case RankFilterMethod::Median:
    return "median";
  case RankFilterMethod::Mean:
    return "mean";
  case RankFilterMethod::Variance:
    return "variance";
  default:
    throw std::invalid_argument("Unknown ImageJ rank filter method");
  }
}

RankFilterKernel rank_filter_kernel_imagej(double radius) {
  kernel_checkpoint();
  validate_radius(radius);
  if (radius >= 1.5 && radius < 1.75)
    radius = 1.75;
  else if (radius >= 2.5 && radius < 2.85)
    radius = 2.85;
  const auto squared = static_cast<int>(std::floor(radius * radius)) + 1;
  const auto extent = static_cast<int>(std::floor(std::sqrt(squared + 1e-10)));
  const auto rows = static_cast<std::size_t>(2 * extent + 1);
  RankFilterKernel kernel{std::vector<int>(rows), 0};
  KernelStage stage("ImageJ rank footprint", rows);
  for (std::size_t row = 0; row < rows; ++row) {
    const auto dy = static_cast<int>(row) - extent;
    const auto half = static_cast<int>(
        std::floor(std::sqrt(static_cast<double>(squared - dy * dy) + 1e-10)));
    kernel.half_widths[row] = half;
    kernel.point_count += static_cast<std::size_t>(2 * half + 1);
    stage.periodic(row + 1);
  }
  stage.finish();
  return kernel;
}

Image rank_filter_imagej(const Image &source, RankFilterMethod method,
                        double radius, std::optional<RankFilterExtent> extent) {
  kernel_checkpoint();
  validate_radius(radius);
  validate_method(method, radius);
  const auto samples = validate_source(source);
  const auto area = validate_extent(source, extent);
  kernel_checkpoint();
  Image output(source.width, source.height, 1, source.type);
  kernel_blocks("ImageJ rank source copy", samples,
                [&](std::size_t first, std::size_t last) {
                  std::copy(source.pixels.begin() + first,
                            source.pixels.begin() + last,
                            output.pixels.begin() + first);
                });
  if (area.width == 0 || area.height == 0)
    return output;

  const auto kernel = rank_filter_kernel_imagej(radius);
  std::vector<float> above;
  std::vector<float> below;
  if (method == RankFilterMethod::Median) {
    if (kernel.point_count > std::vector<float>().max_size())
      throw std::invalid_argument("Rank filter median scratch size is not representable");
    checked_product(checked_product(kernel.point_count, 2), sizeof(float));
    kernel_checkpoint();
    above.resize(kernel.point_count);
    kernel_checkpoint();
    below.resize(kernel.point_count);
  }
  const auto area_size = checked_product(static_cast<std::size_t>(area.width),
                                          static_cast<std::size_t>(area.height));
  const bool always_full = kernel.half_widths.size() / 2 < 2;
  const float sign = method == RankFilterMethod::Minimum ? -1.0f : 1.0f;
  with_kernel_stage("ImageJ rank filtering", area_size, [&](const auto &stage) {
    std::size_t completed = 0;
    for (int y = area.y; y < area.y + area.height; ++y) {
      stage.checkpoint(completed);
      double total = 0.0;
      double squared = 0.0;
      float carried_extreme = -std::numeric_limits<float>::max();
      float guess = sample(source, area.x, y);
      for (int x = area.x; x < area.x + area.width; ++x) {
        const bool full = x == area.x || always_full;
        float value;
        switch (method) {
        case RankFilterMethod::Minimum:
        case RankFilterMethod::Maximum:
          carried_extreme = extreme(source, kernel, x, y, sign, full,
                                     carried_extreme, stage);
          value = carried_extreme * sign;
          break;
        case RankFilterMethod::Mean:
          total = mean_total(source, kernel, x, y, full, total, stage);
          value = static_cast<float>(total / static_cast<double>(kernel.point_count));
          break;
        case RankFilterMethod::Variance:
          variance_totals(source,kernel,x,y,full,total,squared,stage);
          value=static_cast<float>((squared-total*total/static_cast<double>(kernel.point_count))/static_cast<double>(kernel.point_count));
          // ImageJ clamps integer variance before rounding; FloatProcessor has
          // no upper clamp. Keep this API's finite Float32 output contract.
          if (source.type != PixelType::Float32)
            value = std::min(value, source.type == PixelType::UInt8 ? 255.f : 65535.f);
          if (value < 0.f) value = 0.f;
          if (!std::isfinite(value))
            throw std::invalid_argument("ImageJ variance exceeds finite Float32 output range");
          break;
        case RankFilterMethod::Median:
          if (source.type == PixelType::Float32)
            guess = sample(source, x, y);
          guess = median(source, kernel, x, y, guess, above, below, stage);
          value = guess;
          break;
        default:
          throw std::logic_error("Validated rank filter method changed");
        }
        output.pixels[static_cast<std::size_t>(y) * source.width + x] =
            output_sample(value, source.type);
        stage.periodic(++completed);
      }
    }
    stage.finish();
  });
  kernel_checkpoint();
  return output;
}


Image despeckle_imagej(const Image &source,
                      std::optional<RankFilterExtent> extent) {
  return rank_filter_imagej(source, RankFilterMethod::Median, 1.0, extent);
}

Image remove_outliers_imagej(const Image &source, double radius, double threshold,
                            const std::string &which, bool inverted_lut,
                            std::optional<RankFilterExtent> extent) {
  kernel_checkpoint();
  validate_radius(radius);
  validate_method(RankFilterMethod::Median, radius);
  if (!std::isfinite(threshold) || threshold < 0 ||
      threshold > static_cast<double>(std::numeric_limits<float>::max()))
    throw std::invalid_argument("Outlier threshold must be nonnegative and finite Float32");
  if (which != "bright" && which != "dark")
    throw std::invalid_argument("Outlier polarity must be bright or dark");
  const auto samples = validate_source(source);
  const auto area = validate_extent(source, extent);
  Image output(source.width, source.height, 1, source.type);
  kernel_blocks("ImageJ outlier source copy", samples,
                [&](std::size_t first, std::size_t last) {
                  std::copy(source.pixels.begin() + first,
                            source.pixels.begin() + last,
                            output.pixels.begin() + first);
                });
  if (area.width == 0 || area.height == 0)
    return output;
  const auto kernel = rank_filter_kernel_imagej(radius);
  if (kernel.point_count > std::vector<float>().max_size())
    throw std::invalid_argument("Outlier median scratch size is not representable");
  checked_product(checked_product(kernel.point_count, 2), sizeof(float));
  kernel_checkpoint();
  std::vector<float> above(kernel.point_count);
  kernel_checkpoint();
  std::vector<float> below(kernel.point_count);
  const auto area_size = checked_product(static_cast<std::size_t>(area.width),
                                          static_cast<std::size_t>(area.height));
  const float sign = inverted_lut == (which == "dark") ? -1.f : 1.f;
  const float limit = static_cast<float>(threshold);
  const bool always_full = kernel.half_widths.size() / 2 < 2;
  with_kernel_stage("ImageJ outlier filtering", area_size, [&](const auto &stage) {
    std::size_t completed = 0;
    for (int y = area.y; y < area.y + area.height; ++y) {
      stage.checkpoint(completed);
      float carried_extreme = -std::numeric_limits<float>::max();
      float guess = sample(source, area.x, y);
      for (int x = area.x; x < area.x + area.width; ++x) {
        carried_extreme = extreme(source, kernel, x, y, sign,
            x == area.x || always_full, carried_extreme, stage);
        float value = sample(source, x, y);
        const float cutoff = value * sign + limit;
        // Avoid median work unless an extreme can exceed the threshold.
        // Keep the row's median guess, including across unchanged pixels.
        if (cutoff < carried_extreme) {
          guess = median(source, kernel, x, y, guess, above, below, stage);
          if (cutoff < guess * sign)
            value = guess;
        }
        output.pixels[static_cast<std::size_t>(y) * source.width + x] =
            output_sample(value, source.type);
        stage.periodic(++completed);
      }
    }
    stage.finish();
  });
  kernel_checkpoint();
  return output;
}

Image remove_nans_imagej(const Image &source, double radius,
                         std::optional<RankFilterExtent> extent) {
  kernel_checkpoint();
  validate_radius(radius);
  validate_method(RankFilterMethod::Median, radius);
  if (source.width <= 0 || source.height <= 0 || source.channels != 1 ||
      source.type != PixelType::Float32)
    throw std::invalid_argument("Remove NaNs requires positive Float32 grayscale dimensions");
  const auto count = checked_product(static_cast<std::size_t>(source.width),
                                     static_cast<std::size_t>(source.height));
  if (count != source.pixels.size())
    throw std::invalid_argument("Remove NaNs source storage does not match shape");
  constexpr auto policy = FloatingSamplePolicy::PreserveIEEEFloat32;
  kernel_blocks("ImageJ Remove NaNs validation", count,
      [&](std::size_t first, std::size_t last) {
        for (auto i = first; i < last; ++i)
          (void)float32_source_sample_bits(source.pixels[i], policy);
      });
  const auto area = validate_extent(source, extent);
  Image output(source.width, source.height, 1, source.type);
  kernel_blocks("ImageJ Remove NaNs source copy", count,
      [&](std::size_t first, std::size_t last) {
        std::copy(source.pixels.begin() + first, source.pixels.begin() + last,
                  output.pixels.begin() + first);
      });
  if (!area.width || !area.height) return output;
  const auto kernel = rank_filter_kernel_imagej(radius);
  checked_product(checked_product(kernel.point_count, 2), sizeof(float));
  kernel_checkpoint();
  std::vector<float> above(kernel.point_count), below(kernel.point_count);
  const auto area_size = checked_product(static_cast<std::size_t>(area.width),
                                         static_cast<std::size_t>(area.height));
  with_kernel_stage("ImageJ Remove NaNs filtering", area_size, [&](const auto &stage) {
    std::size_t completed = 0;
    for (int y = area.y; y < area.y + area.height; ++y) {
      stage.checkpoint(completed);
      float guess = sample(source, area.x, y);
      if (std::isnan(guess)) guess = 0.f;
      for (int x = area.x; x < area.x + area.width; ++x) {
        const float value = sample(source, x, y);
        if (!std::isnan(value)) {
          guess = value;
        } else {
          std::size_t valid = kernel.point_count, n_above = 0, n_below = 0;
          visit_neighborhood(source, kernel, x, y, stage, [&](float v) {
            if (std::isnan(v)) --valid;
            else if (v > guess) above[n_above++] = v;
            else if (v < guess) below[n_below++] = v;
          });
          const auto half = valid / 2;
          const float result = !valid ? std::numeric_limits<float>::quiet_NaN() :
              n_above > half ? select_ordered(above, n_above, n_above-half-1, stage) :
              n_below > half ? select_ordered(below, n_below, half, stage) : guess;
          // Keep the last non-NaN source guess, rather than the replacement.
          output.pixels[static_cast<std::size_t>(y)*source.width+x] =
              std::isnan(result) ? float32_source_sample(0x7fc00000u, policy) :
                                   static_cast<double>(result);
        }
        stage.periodic(++completed);
      }
    }
    stage.finish();
  });
  kernel_checkpoint();
  return output;
}

Image top_hat_imagej(const Image &source, double radius, bool light_background,
                    bool dont_subtract, bool inverted_lut,
                    std::optional<RankFilterExtent> extent) {
  return detail::top_hat_pixels_imagej(source, radius, light_background,
      dont_subtract, inverted_lut, extent).image;
}

detail::RankTopHatPixelResult detail::top_hat_pixels_imagej(
    const Image &source, double radius, bool light_background, bool dont_subtract,
    bool inverted_lut, std::optional<RankFilterExtent> extent,
    std::optional<std::array<double, 2>> initial_range) {
  if (initial_range && (!std::isfinite((*initial_range)[0]) ||
      !std::isfinite((*initial_range)[1]) || (*initial_range)[0] > (*initial_range)[1]))
    throw std::invalid_argument("Top Hat processor bounds must be finite and ordered");
  validate_radius(radius);
  const auto samples = validate_source(source);
  const auto area = validate_extent(source, extent);
  // The first pass must produce the entire second-pass halo. Masks never
  // restrict these reads. Widen arithmetic before growing near int limits.
  const auto kernel = rank_filter_kernel_imagej(radius);
  const auto ry = static_cast<std::int64_t>(kernel.half_widths.size() / 2);
  const auto rx = static_cast<std::int64_t>(
      *std::max_element(kernel.half_widths.begin(), kernel.half_widths.end()));
  const auto left = std::max<std::int64_t>(0, std::int64_t(area.x) - rx);
  const auto top = std::max<std::int64_t>(0, std::int64_t(area.y) - ry);
  const auto right = std::min<std::int64_t>(source.width,
      std::int64_t(area.x) + area.width + rx);
  const auto bottom = std::min<std::int64_t>(source.height,
      std::int64_t(area.y) + area.height + ry);
  const RankFilterExtent expanded{int(left), int(top), int(right-left), int(bottom-top)};
  const bool closing = light_background != inverted_lut;
  auto first = rank_filter_imagej(source,
      closing ? RankFilterMethod::Maximum : RankFilterMethod::Minimum,
      radius, area.width && area.height ? expanded : area);
  auto output = rank_filter_imagej(first,
      closing ? RankFilterMethod::Minimum : RankFilterMethod::Maximum,
      radius, area);
  const float offset = !closing || source.type == PixelType::Float32 ? 0.f :
      source.type == PixelType::UInt8 ? 255.f : 65535.f;
  const bool automatic_range = initial_range && !dont_subtract &&
      (*initial_range)[0] == 0.0 && (*initial_range)[1] == 0.0;
  std::array<double, 2> background_range{std::numeric_limits<double>::max(), -std::numeric_limits<double>::max()};
  auto result_range = background_range;
  kernel_blocks("ImageJ top hat subtraction and boundary restore", samples,
      [&](std::size_t begin, std::size_t end) {
        for (auto i = begin; i < end; ++i) {
          const auto x = i % static_cast<std::size_t>(source.width);
          const auto y = i / static_cast<std::size_t>(source.width);
          const bool outside = x < std::size_t(area.x) || x >= std::size_t(area.x + area.width) ||
              y < std::size_t(area.y) || y >= std::size_t(area.y + area.height);
          if (automatic_range) {
            const double value = outside ? source.pixels[i] : output.pixels[i];
            background_range[0] = std::min(background_range[0], value);
            background_range[1] = std::max(background_range[1], value);
          }
          if (x < std::size_t(area.x) || x >= std::size_t(area.x + area.width) ||
              y < std::size_t(area.y) || y >= std::size_t(area.y + area.height)) {
            output.pixels[i] = source.pixels[i];
          } else if (!dont_subtract) {
            // Two separate Float32 operations match Java's subtraction and
            // addition, including negative Float32 closing residuals.
            const float difference = static_cast<float>(source.pixels[i]) -
                                     static_cast<float>(output.pixels[i]);
            const float value = difference + offset;
            if (!std::isfinite(value))
              throw std::invalid_argument("ImageJ Top Hat exceeds finite Float32 output range");
            output.pixels[i] = output_sample(value, source.type);
          }
          if (automatic_range) {
            result_range[0] = std::min(result_range[0], output.pixels[i]);
            result_range[1] = std::max(result_range[1], output.pixels[i]);
          }
        }
      });
  kernel_checkpoint();
  auto setter_range = dont_subtract ? std::optional<std::array<double, 2>>{} : initial_range;
  if (automatic_range) {
    // Byte/Short toFloat recalculates (0,0) on the background. A Short
    // setPixels with that same zero range recalculates on the residual.
    // Float toFloat aliases its processor, so its reset uses residuals.
    if (source.type == PixelType::UInt8) setter_range = background_range;
    else if (source.type == PixelType::UInt16 &&
             (background_range[0] != 0.0 || background_range[1] != 0.0))
      setter_range = background_range;
    else setter_range = result_range;
  }
  return {std::move(output), setter_range};
}

namespace detail {
namespace {
DatasetHistogramRequest variance_histogram_request(const Dataset &source,
                                                 PlanePosition position) {
  if (source.pixel_type() != PixelType::UInt16 && source.pixel_type() != PixelType::Float32)
    throw std::invalid_argument("Nonbyte variance final range requires UInt16 or Float32");
  if (source.sample_policy() != FloatingSamplePolicy::FiniteOnly)
    throw std::invalid_argument("Variance final range requires finite-only samples");
  DatasetHistogramRequest request;
  request.position = position;
  if (source.pixel_type() == PixelType::UInt16) {
    // Integral samples occupy the centers of unit-width bins, so every
    // integer's bin is its exact unsigned value, as in ShortStatistics.
    request.binning = HistogramBinning::UniformRange;
    request.bins = 65536;
    request.range = std::array<double, 2>{-0.5, 65535.5};
  }
  return request;
}
} // namespace

std::optional<std::array<double, 2>> validate_top_hat_display_source(
    const DocumentSnapshot &source, bool dont_subtract) {
  const auto &dataset = source.data().dataset;
  const auto channels = recognized_channel_display(source.display(), dataset);
  (void)manual_threshold_state(source.display(),dataset.shape());
  validate_top_hat_final_display_source(dataset,
      {source.display().t, source.display().c, source.display().crosshair[2]},
      dont_subtract,RankProcessorLane::LiveProcessor);
  if (dataset.pixel_type() != PixelType::UInt8 && !dont_subtract)
    return validate_rank_variance_display_source(source);
  // Background-only keeps presentation; validate its explicit bounds without
  // admitting a histogram that execution never runs.
  if (dataset.pixel_type() != PixelType::UInt8 && dont_subtract) {
    if (channels) return channels->channels.at(source.display().c).range;
    const auto found=source.display().properties.find("display_range");
    if(found==source.display().properties.end() || std::holds_alternative<std::monostate>(found->second))return std::nullopt;
    const auto &v=axis_command_record::array_value(found->second);
    if(v.size()!=2)throw std::invalid_argument("Top Hat display range requires two endpoints");
    const std::array<double,2> range{axis_command_record::number_value(v[0]),axis_command_record::number_value(v[1])};
    if(!std::isfinite(range[0]) || !std::isfinite(range[1]) || range[0]>range[1])throw std::invalid_argument("Top Hat display range requires finite ordered endpoints");
    return range;
  }
  if (channels) return channels->channels.at(source.display().c).range;
  const auto found=source.display().properties.find("display_range");
  if(found==source.display().properties.end() || std::holds_alternative<std::monostate>(found->second))return std::nullopt;
  const auto &v=axis_command_record::array_value(found->second);
  if(v.size()!=2)throw std::invalid_argument("Top Hat display range requires two endpoints");
  std::array<double,2> range{axis_command_record::number_value(v[0]),axis_command_record::number_value(v[1])};
  if(!std::isfinite(range[0]) || !std::isfinite(range[1]) || range[0]>range[1])
    throw std::invalid_argument("Top Hat display range requires finite ordered endpoints");
  for(auto &x:range){x=std::floor(x+0.5);if(x < double(std::numeric_limits<std::int32_t>::min()) || x > double(std::numeric_limits<std::int32_t>::max()))throw std::invalid_argument("Top Hat byte display bounds exceed processor range");}
  return range;
}

void validate_top_hat_final_display_source(
    const Dataset &dataset, PlanePosition position, bool dont_subtract,
    RankProcessorLane lane) {
  if (lane != RankProcessorLane::LiveProcessor && lane != RankProcessorLane::DetachedPlanes)
    throw std::invalid_argument("Unknown Top Hat processor lane");
  const auto shape = dataset.shape();
  const auto type = dataset.pixel_type();
  if (shape.s != 1 || (type != PixelType::UInt8 && type != PixelType::UInt16 && type != PixelType::Float32) ||
      dataset.sample_policy() != FloatingSamplePolicy::FiniteOnly)
    throw std::invalid_argument("Top Hat final display requires finite scalar UInt8/UInt16/Float32");
  if (position.t < 0 || position.t >= shape.t || position.c < 0 || position.c >= shape.c ||
      position.z < 0 || position.z >= shape.z)
    throw std::invalid_argument("Top Hat final display position is outside the dataset");
  if (!dont_subtract && type != PixelType::UInt8)
    (void)estimate_rank_variance_final_range(dataset, position);
}

RankTopHatFinalDisplay top_hat_final_display_imagej(
    const Dataset &dataset, PlanePosition position, bool dont_subtract,
    RankProcessorLane lane, std::optional<std::array<double, 2>> live_subtraction_range,
    const ExecutionControl &control) {
  if (live_subtraction_range && (lane != RankProcessorLane::LiveProcessor || dont_subtract ||
      !std::isfinite((*live_subtraction_range)[0]) || !std::isfinite((*live_subtraction_range)[1]) ||
      (*live_subtraction_range)[0] > (*live_subtraction_range)[1]))
    throw std::invalid_argument("Top Hat live subtraction bounds require a live subtraction and finite ordered values");
  validate_top_hat_final_display_source(dataset, position, dont_subtract, lane);
  ExecutionScope scope(control);
  kernel_checkpoint();
  if (dont_subtract) return {};
  const auto range = dataset.pixel_type() == PixelType::UInt8
      ? std::optional<std::array<double, 2>>{}
      : rank_variance_final_range_imagej(dataset, position, control);
  kernel_checkpoint();
  return {range ? range : live_subtraction_range,
      lane == RankProcessorLane::LiveProcessor || range.has_value()};
}

std::optional<std::array<double, 2>> validate_rank_variance_display_source(
    const DocumentSnapshot &source) {
  if (source.data().dataset.pixel_type() == PixelType::UInt8) return std::nullopt;
  const auto channels = recognized_channel_display(source.display(), source.data().dataset);
  // Thresholds do not restrict rank pixels or final histogram samples. Keep
  // schema validation metadata-only; final processing owns threshold reset.
  (void)manual_threshold_state(source.display(), source.data().dataset.shape());
  (void)estimate_rank_variance_final_range(source.data().dataset,
      {source.display().t, source.display().c, source.display().crosshair[2]});
  std::array<double, 2> range;
  if (channels) {
    range = channels->channels.at(source.display().c).range;
  } else {
    const auto found = source.display().properties.find("display_range");
    if (found == source.display().properties.end() ||
        std::holds_alternative<std::monostate>(found->second)) return std::nullopt;
    const auto &values = axis_command_record::array_value(found->second);
    if (values.size() != 2) throw std::invalid_argument("Variance display range requires two endpoints");
    range = {axis_command_record::number_value(values[0]),
             axis_command_record::number_value(values[1])};
  }
  if (!std::isfinite(range[0]) || !std::isfinite(range[1]) || range[0] > range[1])
    throw std::invalid_argument("Variance display range requires finite ordered endpoints");
  // Both processor setters interpret requested (0,0) as automatic reset,
  // before narrowing/rounding. Nonzero requests that narrow to zero stay fixed.
  // Canonical channel ranges use literal [0,0], unlike the legacy setter.
  if (!channels && range[0] == 0.0 && range[1] == 0.0) return std::nullopt;
  for (double &endpoint : range) {
    if (source.data().dataset.pixel_type() == PixelType::UInt16) {
      endpoint = std::floor(endpoint + 0.5);
      if (endpoint < double(std::numeric_limits<std::int32_t>::min()) ||
          endpoint > double(std::numeric_limits<std::int32_t>::max()))
        throw std::invalid_argument("Variance UInt16 display bounds exceed the qualified processor range");
    } else {
      if (std::abs(endpoint) > double(std::numeric_limits<float>::max()))
        throw std::invalid_argument("Variance display bounds exceed finite Float32");
      endpoint = double(static_cast<float>(endpoint));
    }
  }
  return range;
}

DatasetHistogramEstimate estimate_rank_variance_final_range(
    const Dataset &source, PlanePosition position) {
  return estimate_dataset_histogram(source, variance_histogram_request(source, position));
}

std::optional<std::array<double, 2>> rank_variance_final_range_imagej(
    const Dataset &source, PlanePosition position, const ExecutionControl &control) {
  const Dataset captured = source;
  const ExecutionControl owned_control = control;
  const auto request = variance_histogram_request(captured, position);
  (void)estimate_dataset_histogram(captured, request);
  ExecutionScope scope(owned_control);
  kernel_checkpoint();
  const auto histogram = accumulate_dataset_histogram_stages(captured, request, {}, owned_control);
  const auto &counts = histogram.histogram.counts;
  const auto threshold = static_cast<std::uint64_t>(double(histogram.included_count) * 0.5 / 200.0);
  std::size_t low = 0, high = counts.size() - 1;
  KernelStage tails("ImageJ variance display tails", 2 * counts.size());
  std::size_t visited = 0;
  std::uint64_t count = counts[low];
  while (count <= threshold && low + 1 < counts.size()) {
    count += counts[++low];tails.periodic(++visited);
  }
  count = counts[high];
  while (count <= threshold && high > 0) {
    count += counts[--high];tails.periodic(++visited);
  }
  tails.finish();
  if (high <= low) return std::nullopt;
  if (captured.pixel_type() == PixelType::UInt16)
    return std::array<double, 2>{double(low), double(high)};
  // FloatProcessor.setMinAndMax narrows the computed bin edges to Float32.
  // The histogram's extrema/bin mapping remains double, as in FloatStatistics.
  const auto edge = [&](std::size_t bin) {
    return double(static_cast<float>(histogram.histogram_origin + double(bin) * *histogram.bin_size));
  };
  return std::array<double, 2>{edge(low), edge(high)};
}
} // namespace detail

} // namespace imagejxx
