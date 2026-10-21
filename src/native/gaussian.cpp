#include <imagejxx/gaussian.hpp>
#include "kernel_control.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <vector>

namespace imagejxx {
namespace {

constexpr double pi = 3.141592653589793238462643383279502884;

std::size_t checked_product(std::size_t a, std::size_t b) {
  if (b != 0 && a > std::numeric_limits<std::size_t>::max() / b)
    throw std::invalid_argument("Gaussian storage size overflows");
  return a * b;
}

std::size_t float_count(std::int64_t count) {
  if (count < 0 || static_cast<std::uint64_t>(count) >
                       std::vector<float>().max_size())
    throw std::invalid_argument("Gaussian scratch size is not representable");
  checked_product(static_cast<std::size_t>(count), sizeof(float));
  return static_cast<std::size_t>(count);
}

int safe_halo(double sigma) {
  if (!std::isfinite(sigma) || sigma < 0)
    throw std::invalid_argument("Gaussian sigma must be finite and nonnegative");
  const double halo = std::ceil(5.0 * sigma);
  if (!std::isfinite(halo) ||
      halo > static_cast<double>(std::numeric_limits<int>::max()))
    throw std::invalid_argument("Gaussian sigma exceeds safe coordinate range");
  return static_cast<int>(halo);
}

std::size_t validate_source(const Image &source) {
  kernel_checkpoint();
  if (source.width <= 0 || source.height <= 0 || source.channels <= 0)
    throw std::invalid_argument("Gaussian source dimensions must be positive");
  const bool byte = source.type == PixelType::UInt8;
  if (!((byte && (source.channels == 1 || source.channels == 3)) ||
        ((source.type == PixelType::UInt16 ||
          source.type == PixelType::Float32) && source.channels == 1)))
    throw std::invalid_argument(
        "ImageJ Gaussian supports UInt8 gray/RGB, UInt16 gray and Float32 gray");
  auto count = checked_product(static_cast<std::size_t>(source.width),
                               static_cast<std::size_t>(source.height));
  count = checked_product(count, static_cast<std::size_t>(source.channels));
  if (count > std::vector<double>().max_size() || count != source.pixels.size())
    throw std::invalid_argument("Gaussian source storage does not match shape");
  const bool integer = source.type != PixelType::Float32;
  const double maximum = byte ? 255.0 : 65535.0;
  kernel_blocks("ImageJ Gaussian validation", count,
                [&](std::size_t first, std::size_t last) {
                  for (auto i = first; i < last; ++i) {
                    const auto value = source.pixels[i];
                    if (!std::isfinite(value) ||
                        (!integer && !std::isfinite(static_cast<float>(value))))
                      throw std::invalid_argument(
                          "Gaussian source samples must be finite Float32 values");
                    if (integer && (value < 0 || value > maximum ||
                                    value != std::trunc(value)))
                      throw std::invalid_argument(
                          "Gaussian integer samples must be in their tagged range");
                  }
                });
  return count;
}

GaussianBlurExtent validate_extent(const Image &source,
                                  std::optional<GaussianBlurExtent> extent) {
  const auto area = extent.value_or(
      GaussianBlurExtent{0, 0, source.width, source.height});
  const auto right = static_cast<std::int64_t>(area.x) + area.width;
  const auto bottom = static_cast<std::int64_t>(area.y) + area.height;
  if (area.x < 0 || area.y < 0 || area.width < 0 || area.height < 0 ||
      right > source.width || bottom > source.height)
    throw std::invalid_argument("Gaussian extent is outside the source image");
  return area;
}

struct Coefficients {
  std::vector<float> value;
  std::vector<float> edge_mass;
};

Coefficients coefficients(double sigma, double accuracy,
                          std::int64_t maximum_length) {
  const auto cap = std::max<std::int64_t>(50, maximum_length);
  const double requested =
      std::ceil(sigma * std::sqrt(-2.0 * std::log(accuracy))) + 1.0;
  if (!std::isfinite(requested) || requested < 1)
    throw std::invalid_argument("Gaussian coefficient count is not representable");
  const auto count = requested >= static_cast<double>(cap)
                         ? cap
                         : static_cast<std::int64_t>(requested);
  const auto size = float_count(count);
  KernelStage stage("ImageJ Gaussian weights", size);
  Coefficients result{std::vector<float>(size), std::vector<float>(size)};
  for (std::size_t i = 0; i < size; ++i) {
    const double distance = static_cast<double>(i);
    result.value[i] = static_cast<float>(
        std::exp(-0.5 * distance * distance / sigma / sigma));
    stage.periodic(i + 1);
  }

  if (count < cap && count > 3) {
    double slope = std::numeric_limits<double>::max();
    std::int64_t last = count;
    for (auto candidate = count - 1; candidate >= count / 2; --candidate) {
      last = candidate;
      const double next =
          std::sqrt(static_cast<double>(result.value[static_cast<std::size_t>(candidate)])) /
          static_cast<double>(count - candidate);
      if (next >= slope)
        break;
      slope = next;
      stage.poll();
    }
    for (auto i = last + 2; i < count; ++i) {
      const double distance = static_cast<double>(count - i);
      result.value[static_cast<std::size_t>(i)] =
          static_cast<float>(distance * distance * slope * slope);
      stage.poll();
    }
  }

  double norm = sigma * std::sqrt(2.0 * pi);
  if (count < cap) {
    norm = result.value[0];
    for (std::size_t i = 1; i < size; ++i) {
      const float doubled = 2.0f * result.value[i];
      norm += doubled;
      stage.poll();
    }
  }
  double residual = 0.5 + 0.5 * result.value[0] / norm;
  for (std::size_t i = 0; i < size; ++i) {
    const double normalized = result.value[i] / norm;
    result.value[i] = static_cast<float>(normalized);
    residual -= normalized;
    result.edge_mass[i] = static_cast<float>(residual);
    stage.poll();
  }
  stage.finish();
  return result;
}

struct DirectionPlan {
  bool horizontal = false;
  int length = 0;
  int line_begin = 0;
  int line_end = 0;
  int write_begin = 0;
  int write_end = 0;
  std::int64_t rate = 1;
  std::int64_t origin = 0;
  std::size_t reduced_length = 0;
  bool resample = false;
  Coefficients blur;
  std::vector<float> reduction;
  std::vector<float> reconstruction;
};

DirectionPlan direction_plan(const Image &source, GaussianBlurExtent area,
                             double sigma, double accuracy, bool horizontal,
                             int halo) {
  DirectionPlan plan;
  plan.horizontal = horizontal;
  plan.length = horizontal ? source.width : source.height;
  plan.line_begin = horizontal
                        ? static_cast<int>(std::max<std::int64_t>(0, static_cast<std::int64_t>(area.y) - halo))
                        : area.x;
  plan.line_end = horizontal
                      ? static_cast<int>(std::min<std::int64_t>(source.height,
                            static_cast<std::int64_t>(area.y) + area.height + halo))
                      : area.x + area.width;
  plan.write_begin = horizontal ? area.x : area.y;
  plan.write_end = plan.write_begin + (horizontal ? area.width : area.height);
  if (sigma == 0)
    return plan;
  plan.resample = sigma > 8.5;
  if (plan.resample)
    plan.rate = static_cast<std::int64_t>(
        std::min(std::floor(sigma / 4.0), static_cast<double>(plan.length)));
  const auto maximum_length = plan.resample
                                  ? (plan.length + plan.rate - 1) / plan.rate + 6
                                  : plan.length;
  const double rate = static_cast<double>(plan.rate);
  const double adjusted_sigma =
      plan.resample
          ? std::sqrt(sigma * sigma / (rate * rate) - 1.0 / 3.0 - 1.0 / 4.0)
          : sigma;
  if (!std::isfinite(adjusted_sigma) || adjusted_sigma <= 0)
    throw std::invalid_argument("Gaussian adjusted variance is not representable");
  plan.blur = coefficients(adjusted_sigma, accuracy, maximum_length);
  if (!plan.resample)
    return plan;

  const auto radius = static_cast<std::int64_t>(plan.blur.value.size()) * plan.rate;
  const auto first = std::max<std::int64_t>(0, plan.write_begin - radius);
  const auto last = std::min<std::int64_t>(plan.length, plan.write_end + radius);
  plan.origin = first - 3 * plan.rate;
  plan.reduced_length = float_count((last - first + plan.rate - 1) / plan.rate + 6);
  const auto reduction_count = float_count(3 * plan.rate);
  const auto reconstruction_count = float_count(4 * plan.rate);
  kernel_checkpoint();
  plan.reduction.resize(reduction_count);
  plan.reconstruction.resize(reconstruction_count);
  kernel_blocks("ImageJ Gaussian reduction weights", reduction_count,
                [&](std::size_t first_index, std::size_t last_index) {
                  const auto center = 3 * plan.rate / 2;
                  for (auto i = first_index; i < last_index; ++i) {
                    const auto distance = std::abs(static_cast<std::int64_t>(i) - center);
                    const double x = static_cast<double>(distance) / rate;
                    const double weight = distance <= plan.rate / 2
                                              ? (0.75 - x * x) / rate
                                              : (0.125 + 0.5 * (x - 1.0) * (x - 2.0)) / rate;
                    plan.reduction[i] = static_cast<float>(weight);
                  }
                });
  kernel_blocks("ImageJ Gaussian reconstruction weights", reconstruction_count,
                [&](std::size_t first_index, std::size_t last_index) {
                  const auto center = 2 * plan.rate;
                  for (auto i = first_index; i < last_index; ++i) {
                    const auto distance = std::abs(static_cast<std::int64_t>(i) - center);
                    const double x = static_cast<double>(distance) / rate;
                    double weight = 0;
                    if (distance < plan.rate)
                      weight = 2.0 / 3.0 - x * x * (1.0 - 0.5 * x);
                    else if (distance < 2 * plan.rate) {
                      const double tail = 2.0 - x;
                      weight = tail * tail * tail / 6.0;
                    }
                    plan.reconstruction[i] = static_cast<float>(weight);
                  }
                });
  return plan;
}

float convolved_sample(const std::vector<float> &line, std::size_t position,
                       const Coefficients &weights) {
  const auto length = static_cast<std::int64_t>(line.size());
  const auto target = static_cast<std::int64_t>(position);
  const auto radius = static_cast<std::int64_t>(weights.value.size());
  float value = line[position] * weights.value[0];
  if (target < radius)
    value += weights.edge_mass[position] * line.front();
  const bool right_mass = target < radius ? target + radius > length
                                         : target + radius >= length;
  if (right_mass)
    value += weights.edge_mass[static_cast<std::size_t>(length - target - 1)] * line.back();
  const bool boundary = target < radius || target >= length - radius;
  for (std::int64_t distance = 1; distance < radius; ++distance) {
    float pair = 0;
    if (boundary) {
      if (target >= distance)
        pair += line[static_cast<std::size_t>(target - distance)];
      if (target + distance < length)
        pair += line[static_cast<std::size_t>(target + distance)];
    } else {
      pair = line[static_cast<std::size_t>(target - distance)] +
             line[static_cast<std::size_t>(target + distance)];
    }
    value += weights.value[static_cast<std::size_t>(distance)] * pair;
    kernel_checkpoint_periodic();
  }
  return value;
}

void reduce_line(const std::vector<float> &line, std::vector<float> &reduced,
                 const DirectionPlan &plan) {
  kernel_blocks("ImageJ Gaussian downscale", reduced.size(),
                [&](std::size_t first, std::size_t last) {
                  for (auto i = first; i < last; ++i) {
                    const auto start = plan.origin - 3 * plan.rate / 2 +
                                       static_cast<std::int64_t>(i) * plan.rate;
                    float total = 0;
                    for (int block = 0; block < 3; ++block) {
                      float partial = 0;
                      const auto block_offset = static_cast<std::int64_t>(block) * plan.rate;
                      for (std::int64_t j = 0; j < plan.rate; ++j) {
                        const auto sample = std::clamp<std::int64_t>(
                            start + block_offset + j, 0, plan.length - 1);
                        partial += line[static_cast<std::size_t>(sample)] *
                                   plan.reduction[static_cast<std::size_t>(block_offset + j)];
                        kernel_checkpoint_periodic();
                      }
                      if (block == 0)
                        total = partial;
                      else
                        total += partial;
                    }
                    reduced[i] = total;
                  }
                });
}

float reconstructed_sample(const std::vector<float> &blurred,
                           const DirectionPlan &plan, int position) {
  const auto offset = static_cast<std::int64_t>(position) - plan.origin + plan.rate - 1;
  const auto upper = offset / plan.rate;
  const auto phase = plan.rate - 1 - offset % plan.rate;
  float value = 0;
  for (int tap = 0; tap < 4; ++tap) {
    const auto sample = upper - 2 + tap;
    if (sample < 0 || static_cast<std::uint64_t>(sample) >= blurred.size())
      throw std::invalid_argument("Gaussian reconstruction coordinate overflows");
    const float term = blurred[static_cast<std::size_t>(sample)] *
                       plan.reconstruction[static_cast<std::size_t>(phase + tap * plan.rate)];
    if (tap == 0)
      value = term;
    else
      value += term;
  }
  return value;
}

void apply_direction(std::vector<float> &plane, int width,
                     const DirectionPlan &plan) {
  if (plan.blur.value.empty())
    return;
  KernelStage lines(plan.horizontal ? "ImageJ Gaussian X lines"
                                    : "ImageJ Gaussian Y lines",
                    static_cast<std::size_t>(plan.line_end - plan.line_begin));
  std::vector<float> line(float_count(plan.length));
  kernel_checkpoint();
  std::vector<float> reduced(plan.resample ? plan.reduced_length : 0);
  std::vector<float> blurred(plan.resample ? plan.reduced_length : 0);
  const std::size_t stride = plan.horizontal ? 1 : static_cast<std::size_t>(width);
  for (int row = plan.line_begin; row < plan.line_end; ++row) {
    lines.checkpoint(static_cast<std::size_t>(row - plan.line_begin));
    const auto base = plan.horizontal ? static_cast<std::size_t>(row) * width
                                      : static_cast<std::size_t>(row);
    kernel_blocks("ImageJ Gaussian line copy", line.size(),
                  [&](std::size_t first, std::size_t last) {
                    for (auto i = first; i < last; ++i)
                      line[i] = plane[base + i * stride];
                  });
    if (plan.resample) {
      reduce_line(line, reduced, plan);
      kernel_blocks("ImageJ Gaussian convolve", reduced.size() - 2,
                    [&](std::size_t first, std::size_t last) {
                      for (auto i = first; i < last; ++i)
                        blurred[i + 1] = convolved_sample(reduced, i + 1, plan.blur);
                    });
    }
    const auto count = static_cast<std::size_t>(plan.write_end - plan.write_begin);
    kernel_blocks(plan.resample ? "ImageJ Gaussian upscale" : "ImageJ Gaussian convolve",
                  count, [&](std::size_t first, std::size_t last) {
                    for (auto i = first; i < last; ++i) {
                      const auto position = plan.write_begin + static_cast<int>(i);
                      plane[base + static_cast<std::size_t>(position) * stride] =
                          plan.resample ? reconstructed_sample(blurred, plan, position)
                                        : convolved_sample(line, static_cast<std::size_t>(position), plan.blur);
                    }
                  });
  }
  lines.finish();
}

} // namespace

Image gaussian_blur_imagej(const Image &source, double sigma_x, double sigma_y,
                          std::optional<GaussianBlurExtent> extent) {
  kernel_checkpoint();
  (void)safe_halo(sigma_x);
  const int halo_y = safe_halo(sigma_y);
  const auto samples = validate_source(source);
  const auto area = validate_extent(source, extent);
  kernel_checkpoint();
  Image output(source.width, source.height, source.channels, source.type);
  kernel_blocks("ImageJ Gaussian source copy", samples,
                [&](std::size_t first, std::size_t last) {
                  std::copy(source.pixels.begin() + first,
                            source.pixels.begin() + last,
                            output.pixels.begin() + first);
                });
  if (area.width == 0 || area.height == 0)
    return output;

  const double accuracy = source.type == PixelType::UInt8 ? 0.002 : 0.0002;
  const auto horizontal = direction_plan(source, area, sigma_x, accuracy, true, halo_y);
  const auto vertical = direction_plan(source, area, sigma_y, accuracy, false, 0);
  const auto plane_size = checked_product(static_cast<std::size_t>(source.width),
                                          static_cast<std::size_t>(source.height));
  if (plane_size > std::vector<float>().max_size())
    throw std::invalid_argument("Gaussian plane scratch is not representable");
  checked_product(plane_size, sizeof(float));
  kernel_checkpoint();
  std::vector<float> plane(plane_size);
  const bool integer = source.type != PixelType::Float32;
  const float maximum = source.type == PixelType::UInt8 ? 255.0f : 65535.0f;
  const auto area_size = checked_product(static_cast<std::size_t>(area.width),
                                         static_cast<std::size_t>(area.height));
  for (int channel = 0; channel < source.channels; ++channel) {
    kernel_blocks("ImageJ Gaussian channel", plane_size,
                  [&](std::size_t first, std::size_t last) {
                    for (auto i = first; i < last; ++i)
                      plane[i] = static_cast<float>(source.pixels[
                          i * source.channels + static_cast<std::size_t>(channel)]);
                  });
    apply_direction(plane, source.width, horizontal);
    apply_direction(plane, source.width, vertical);
    kernel_blocks("ImageJ Gaussian writeback", area_size,
                  [&](std::size_t first, std::size_t last) {
                    for (auto i = first; i < last; ++i) {
                      const auto x = static_cast<std::size_t>(area.x) + i % area.width;
                      const auto y = static_cast<std::size_t>(area.y) + i / area.width;
                      const auto location = y * source.width + x;
                      float value = plane[location];
                      if (!std::isfinite(value))
                        throw std::invalid_argument("Gaussian output is not finite Float32");
                      double converted = value;
                      if (integer) {
                        value += 0.5f;
                        value = std::clamp(value, 0.0f, maximum);
                        converted = static_cast<double>(static_cast<std::uint32_t>(value));
                      }
                      output.pixels[location * source.channels +
                                    static_cast<std::size_t>(channel)] = converted;
                    }
                  });
  }
  kernel_checkpoint();
  return output;
}

} // namespace imagejxx
