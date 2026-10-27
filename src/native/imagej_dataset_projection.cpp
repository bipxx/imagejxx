#include <imagejxx/imagej_rgb_projection.hpp>
#include "imagej_projection_numeric.hpp"
#include "kernel_control.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <stdexcept>

namespace imagejxx {
namespace {
using T = detail::ImageJProjectionType;
using M = detail::ImageJProjectionMethod;
std::size_t multiply(std::size_t a, std::size_t b) {
  if (b && a > std::numeric_limits<std::size_t>::max() / b)
    throw std::invalid_argument("ImageJ projection storage overflow");
  return a * b;
}
std::size_t add(std::size_t a, std::size_t b) {
  if (a > std::numeric_limits<std::size_t>::max() - b)
    throw std::invalid_argument("ImageJ projection storage overflow");
  return a + b;
}
T scalar_type(PixelType type) {
  switch (type) {
  case PixelType::UInt8: return T::UInt8;
  case PixelType::UInt16: return T::UInt16;
  case PixelType::Float32: return T::Float32;
  default: throw std::invalid_argument("ImageJ projection requires UInt8/UInt16/Float32");
  }
}
M scalar_method(const std::string& method) {
  if (method == "mean") return M::Mean;
  if (method == "maximum") return M::Maximum;
  if (method == "minimum") return M::Minimum;
  if (method == "sum") return M::Sum;
  if (method == "stddev") return M::StdDev;
  if (method == "median") return M::Median;
  throw std::invalid_argument("Unknown ImageJ projection method");
}
std::uint32_t source_word(double sample, PixelType type,
                          FloatingSamplePolicy policy) {
  if (type != PixelType::Float32) {
    const auto maximum = type == PixelType::UInt8 ? 255.0 : 65535.0;
    if (!std::isfinite(sample) || sample < 0 || sample > maximum ||
        std::trunc(sample) != sample)
      throw std::invalid_argument("ImageJ projection needs exact unsigned source codes");
    return static_cast<std::uint32_t>(sample);
  }
  if (policy != FloatingSamplePolicy::FiniteOnly)
    return float32_source_sample_bits(sample, policy);
  if (!std::isfinite(sample) ||
      std::abs(sample) > double(std::numeric_limits<float>::max()))
    throw std::invalid_argument("Invalid finite ImageJ Float32 source sample");
  const float v = static_cast<float>(sample);
  std::uint32_t bits;
  std::memcpy(&bits, &v, sizeof bits);
  return bits;
}
double output_sample(detail::ImageJProjectionSample sample) {
  return sample.type == T::Float32
      ? float32_source_sample(sample.bits, FloatingSamplePolicy::PreserveIEEEFloat32)
      : double(sample.bits);
}
struct RGBProjectionRanges {
  std::array<float, 3> minimum, maximum;
  std::array<double, 3> scale;
};
unsigned char rgb_byte(double rounded) {
  // Java floating-to-integer conversion maps NaN to zero and saturates before
  // the final byte clamp. Avoid undefined C++ casts for nonfinite values.
  if (std::isnan(rounded) || rounded <= 0) return 0;
  if (rounded >= 255) return 255;
  return static_cast<unsigned char>(rounded);
}
void merge_rgb_projection(Image& image, M method, bool clip) {
  if (method != M::Sum && method != M::StdDev) return;
  if (method == M::Sum && clip) {
    kernel_blocks("ImageJ RGB sum clipping", image.pixels.size(),
        [&](std::size_t first, std::size_t last) {
          for (auto i = first; i < last; ++i) {
            const float rounded = static_cast<float>(image.pixels[i]) + .5f;
            image.pixels[i] = rgb_byte(rounded);
          }
        });
    return;
  }
  // ImageJ first scales the Float32 channel reductions by a shared maximum,
  // then RGBStackMerge converts each channel through its own min/max range.
  // Keep both rounding stages: algebraically cancelling them changes results.
  float maximum = 0;
  kernel_blocks("ImageJ RGB projection maximum", image.pixels.size(),
      [&](std::size_t first, std::size_t last) {
        for (auto i = first; i < last; ++i)
          if (image.pixels[i] > maximum) maximum = static_cast<float>(image.pixels[i]);
      });
  const float global_scale = maximum == 0 ? std::numeric_limits<float>::infinity()
      : static_cast<float>(255.0 / double(maximum));
  RGBProjectionRanges ranges;
  ranges.minimum.fill(std::numeric_limits<float>::quiet_NaN());
  ranges.maximum = ranges.minimum;
  kernel_blocks("ImageJ RGB projection scaling", image.pixels.size(),
      [&](std::size_t first, std::size_t last) {
        for (auto i = first; i < last; ++i) {
          const float scaled = static_cast<float>(image.pixels[i]) * global_scale;
          image.pixels[i] = scaled;
          const auto channel = i % 3;
          if (!std::isnan(scaled)) {
            if (std::isnan(ranges.minimum[channel]))
              ranges.minimum[channel] = ranges.maximum[channel] = scaled;
            else if (scaled < ranges.minimum[channel]) ranges.minimum[channel] = scaled;
            else if (scaled > ranges.maximum[channel]) ranges.maximum[channel] = scaled;
          }
        }
      });
  for (std::size_t c = 0; c < 3; ++c)
    ranges.scale[c] = 255.0 / (double(ranges.maximum[c]) - double(ranges.minimum[c]));
  kernel_blocks("ImageJ RGB channel conversion", image.pixels.size(),
      [&](std::size_t first, std::size_t last) {
        for (auto i = first; i < last; ++i) {
          const auto channel = i % 3;
          const double value = std::max(0.0, image.pixels[i] - double(ranges.minimum[channel]));
          image.pixels[i] = rgb_byte(value * ranges.scale[channel] + .5);
        }
      });
}
} // namespace

PlanePosition ImageJDatasetProjectionLayout::contributor(
    PlanePosition output, int k) const {
  if (output.t < 0 || output.t >= shape.t || output.c < 0 || output.c >= shape.c ||
      output.z < 0 || output.z >= shape.z || k < 0 || k >= contributors_per_group ||
      (axis != ImageJProjectionAxis::Z && axis != ImageJProjectionAxis::Time &&
       axis != ImageJProjectionAxis::Channels))
    throw std::out_of_range("Invalid ImageJ projection contributor");
  const auto index = std::int64_t(source_first) +
      std::int64_t(axis == ImageJProjectionAxis::Z ? output.z :
                   axis == ImageJProjectionAxis::Time ? output.t : output.c) *
          contributors_per_group + k;
  const auto t = axis == ImageJProjectionAxis::Time ? index
      : std::int64_t(source_first_t) + output.t;
  const auto c = axis == ImageJProjectionAxis::Channels ? index
      : std::int64_t(source_first_c) + output.c;
  const auto z = axis == ImageJProjectionAxis::Z ? index : std::int64_t(output.z);
  const auto limit = std::numeric_limits<int>::max();
  if (t < 0 || t > limit || c < 0 || c > limit || z < 0 || z > limit)
    throw std::out_of_range("ImageJ projection contributor overflow");
  return {int(t), int(c), int(z)};
}

namespace {
ImageJDatasetProjectionLayout describe_projection(
    DatasetShape shape, PixelType type, const SpatialCalibration& calibration,
    const ImageJDatasetProjectionRequest& request, bool rgb) {
  shape.validate();
  calibration.validate();
  (void)scalar_type(type);
  const auto method = scalar_method(request.method);
  if (rgb && (shape.s != 3 || type != PixelType::UInt8))
    throw std::invalid_argument("ImageJ RGB profile requires UInt8 S3 source");
  if (!rgb && shape.s != 1)
    throw std::invalid_argument("ImageJ scalar profile requires S1; RGB merge is separate");
  if (request.signed16_density && type != PixelType::UInt16)
    throw std::invalid_argument("Signed16 density requires UInt16 source");
  if (!request.calibration_table.empty() &&
      (rgb || (type != PixelType::UInt8 && type != PixelType::UInt16) ||
       request.calibration_table.size() != (type == PixelType::UInt8 ? 256U : 65536U)))
    throw std::invalid_argument("ImageJ density table requires exactly256/65536 scalar integer entries");
  if (request.active_t < 0 || request.active_t >= shape.t ||
      request.active_c < 0 || request.active_c >= shape.c)
    throw std::out_of_range("ImageJ projection active T/C is out of range");
  auto axis = request.axis;
  if (axis == ImageJProjectionAxis::ImageJDefault)
    axis = shape.t == 1 && shape.z == 1 && shape.c > 1 ? ImageJProjectionAxis::Channels
        : shape.z == 1 ? ImageJProjectionAxis::Time : ImageJProjectionAxis::Z;
  if (axis != ImageJProjectionAxis::Z && axis != ImageJProjectionAxis::Time &&
      axis != ImageJProjectionAxis::Channels)
    throw std::invalid_argument("Invalid ImageJ projection axis");
  if (rgb && shape.c != 1 &&
      !(axis == ImageJProjectionAxis::Channels && shape.t == 1 && shape.z == 1))
    throw std::invalid_argument("RGB stacks not supported");
  const int axis_count = axis == ImageJProjectionAxis::Z ? shape.z :
      axis == ImageJProjectionAxis::Time ? shape.t : shape.c;
  if (request.first < 0 || request.first > request.last || request.last >= axis_count)
    throw std::out_of_range("ImageJ projection bounds are out of range");
  const int count = request.last - request.first + 1;
  const int group = request.group_size == 0 ? count : request.group_size;
  if (group <= 0 || group > count || count % group)
    throw std::invalid_argument("ImageJ projection requires complete contributor groups");
  if (request.tile_width <= 0 || request.tile_height <= 0)
    throw std::invalid_argument("ImageJ projection tile dimensions must be positive");
  ImageJDatasetProjectionLayout result;
  result.shape = shape;
  result.axis = axis;
  result.source_first = request.first;
  if (axis == ImageJProjectionAxis::Channels) result.shape.c = count / group;
  else {
    result.source_first_c = request.all_channels ? 0 : request.active_c;
    result.shape.c = request.all_channels ? shape.c : 1;
  }
  if (axis != ImageJProjectionAxis::Time) {
    result.shape.t = request.all_time_frames ? shape.t : 1;
    result.source_first_t = request.all_time_frames ? 0 : request.active_t;
  } else result.shape.t = count / group;
  if (axis == ImageJProjectionAxis::Z) result.shape.z = count / group;
  result.contributors_per_group = group;
  result.tile_width = std::min(shape.x, request.tile_width);
  result.tile_height = std::min(shape.y, request.tile_height);
  result.calibration = calibration;
  if (request.group_size != 0) {
    result.calibration.spacing[2] *= group;
    if (group != 1) {
      const auto zero_indices = calibration.world_to_voxel({0, 0, 0});
      result.calibration.origin = calibration.voxel_to_world(
          {0, 0, (1.0 - group) * zero_indices[2]});
    }
  }
  result.calibration.validate();
  const bool float_output = type == PixelType::Float32 || method == M::Sum ||
      method == M::StdDev || (type == PixelType::UInt16 && method == M::Median);
  result.pixel_type = !rgb && float_output ? PixelType::Float32 : type;
  result.signed16_density = request.signed16_density && result.pixel_type == PixelType::UInt16;
  result.preserve_density_calibration =
      (request.signed16_density || !request.calibration_table.empty()) &&
      (result.pixel_type == PixelType::UInt8 || result.pixel_type == PixelType::UInt16);
  result.output_pixel_bytes = multiply(result.shape.pixel_count(), sizeof(double));
  if (result.output_pixel_bytes > request.max_output_pixel_bytes)
    throw std::invalid_argument("ImageJ projection exceeds output pixel allowance");
  const auto tile = multiply(multiply(std::size_t(result.tile_width), result.tile_height), shape.s);
  result.reduction_scratch_bytes = method == M::Median
      ? multiply(multiply(add(tile, 1), group), sizeof(std::uint32_t))
      : multiply(tile, sizeof(detail::ImageJProjectionAccumulator));
  if (rgb && (method == M::Sum || method == M::StdDev))
    result.reduction_scratch_bytes = add(result.reduction_scratch_bytes, sizeof(RGBProjectionRanges));
  result.reduction_scratch_bytes = add(result.reduction_scratch_bytes,
      multiply(request.calibration_table.size(), sizeof(std::uint32_t)));
  const auto tables = multiply(result.shape.plane_count(),
      2 * sizeof(Image) + 3 * sizeof(std::shared_ptr<const Image>));
  // Conservative default region adapter: one full decoded plane plus its crop.
  // Provider-private caches and allocator/control-block overhead are excluded.
  const auto fallback = multiply(multiply(multiply(std::size_t(shape.x), shape.y), shape.s), sizeof(double));
  result.working_bytes = add(add(result.output_pixel_bytes, tables),
      add(result.reduction_scratch_bytes, add(fallback, multiply(tile, sizeof(double)))));
  if (result.working_bytes > request.max_working_bytes)
    throw std::invalid_argument("ImageJ projection exceeds working allowance");
  return result;
}
} // namespace
ImageJDatasetProjectionLayout describe_imagej_dataset_projection(
    DatasetShape shape, PixelType type, const SpatialCalibration& calibration,
    const ImageJDatasetProjectionRequest& request) {
  return describe_projection(shape, type, calibration, request, false);
}
ImageJDatasetProjectionLayout describe_imagej_rgb_projection(
    DatasetShape shape, PixelType type, const SpatialCalibration& calibration,
    const ImageJRGBProjectionRequest& request) {
  return describe_projection(shape, type, calibration, request, true);
}
ImageJDatasetProjectionLayout describe_imagej_rgb_projection(
    const Dataset& source, const ImageJRGBProjectionRequest& request) {
  return describe_imagej_rgb_projection(source.shape(), source.pixel_type(),
                                       source.calibration(), request);
}
ImageJDatasetProjectionLayout describe_imagej_dataset_projection(
    const Dataset& source, const ImageJDatasetProjectionRequest& request) {
  return describe_imagej_dataset_projection(source.shape(), source.pixel_type(),
                                            source.calibration(), request);
}

namespace {
ImageJDatasetProjectionResult project_dataset_profile(
    const Dataset& input, const ImageJDatasetProjectionRequest& request,
    const ExecutionControl& control, bool rgb, bool clip) {
  ExecutionScope scope(control);
  detail::without_execution_callbacks([] { kernel_checkpoint(); });
  const Dataset source = input;
  const auto shape = source.shape();
  const auto type = source.pixel_type();
  const auto layout = describe_projection(shape, type, source.calibration(), request, rgb);
  // Density tables are captured only after their size and the complete working
  // allowance have been admitted. No callback can retarget this owned copy.
  const auto captured = request;
  const auto method = scalar_method(captured.method);
  const auto numeric_type = scalar_type(type);
  const bool calibrated_median = method == M::Median && !captured.calibration_table.empty();
  const auto policy = source.sample_policy();
  const auto output_policy = layout.pixel_type == PixelType::Float32
      ? FloatingSamplePolicy::PreserveIEEEFloat32 : FloatingSamplePolicy::FiniteOnly;
  const auto metadata_guard = [&] {
    if (source.shape().dimensions() != shape.dimensions() || source.pixel_type() != type)
      throw std::invalid_argument("ImageJ projection provider metadata changed");
  };
  KernelStage stage("ImageJ dataset projection", layout.shape.plane_count());
  Stack output;
  output.reserve(layout.shape.plane_count());
  {
    const auto tile = multiply(multiply(std::size_t(layout.tile_width), layout.tile_height), shape.s);
    const auto group = std::size_t(layout.contributors_per_group);
    std::vector<detail::ImageJProjectionAccumulator> accumulators;
    std::vector<std::uint32_t> rays, scratch;
    if (method == M::Median) {
      rays.resize(multiply(tile, group));
      scratch.resize(group);
    } else accumulators.reserve(tile);
    for (int t = 0; t < layout.shape.t; ++t)
      for (int c = 0; c < layout.shape.c; ++c)
        for (int z = 0; z < layout.shape.z; ++z) {
          stage.checkpoint(output.size());
          execution_progress(output.size(), layout.shape.plane_count());
          kernel_checkpoint();
          metadata_guard();
          Image plane(shape.x, shape.y, shape.s, layout.pixel_type);
          for (int y = 0; y < shape.y;) {
            const int height = std::min(layout.tile_height, shape.y - y);
            for (int x = 0; x < shape.x;) {
              kernel_checkpoint();
              const int width = std::min(layout.tile_width, shape.x - x);
              const auto samples = multiply(multiply(std::size_t(width), height), shape.s);
              accumulators.clear();
              if (method != M::Median)
                for (std::size_t i = 0; i < samples; ++i) {
                  if ((i & 1023) == 0) kernel_checkpoint();
                  accumulators.emplace_back(numeric_type, method, captured.signed16_density);
                }
              for (int k = 0; k < layout.contributors_per_group; ++k) {
                kernel_checkpoint();
                metadata_guard();
                const auto region = source.region(layout.contributor({t, c, z}, k),
                                                   x, y, width, height);
                metadata_guard();
                if (region.width != width || region.height != height || region.channels != shape.s ||
                    region.type != type || region.pixels.size() != samples)
                  throw std::invalid_argument("ImageJ projection provider region changed");
                for (std::size_t i = 0; i < samples; ++i) {
                  if ((i & 1023) == 0) kernel_checkpoint();
                  const auto bits = source_word(region.pixels[i], type, policy);
                  if (method == M::Median)
                    rays[i * group + k] = calibrated_median ? captured.calibration_table[bits] : bits;
                  else accumulators[i].push(bits);
                }
              }
              // Write contiguous row spans without per-component coordinate
              // division, including partial tiles and interleaved RGB samples.
              const auto row_samples = multiply(std::size_t(width), shape.s);
              for (int row = 0; row < height; ++row) {
                const auto input_offset = std::size_t(row) * row_samples;
                const auto output_offset = ((std::size_t(y) + row) * shape.x + x) * shape.s;
                for (std::size_t column = 0; column < row_samples; ++column) {
                  const auto i = input_offset + column;
                  if ((i & 1023) == 0) kernel_checkpoint();
                  auto sample = method == M::Median
                      ? detail::project_imagej_scalar_ray(calibrated_median ? T::Float32 : numeric_type, method,
                          rays.data() + i * group, group, calibrated_median ? false : captured.signed16_density,
                          scratch.data(), scratch.size(), kernel_checkpoint)
                      : accumulators[i].finish();
                  if (calibrated_median && type == PixelType::UInt8) {
                    float value;
                    std::memcpy(&value, &sample.bits, sizeof value);
                    sample = {T::UInt8, rgb_byte(value + .5f)};
                  }
                  plane.pixels[output_offset + column] = output_sample(sample);
                }
              }
              x += width;
            }
            y += height;
          }
          if (rgb) merge_rgb_projection(plane, method, clip);
          output.push_back(std::move(plane));
        }
  } // Retire reduction scratch before immutable output ownership conversion.
  stage.finish();
  kernel_checkpoint();
  metadata_guard();
  auto dataset = Dataset::from_planes(layout.shape, std::move(output),
                                      layout.calibration, output_policy);
  execution_progress(layout.shape.plane_count(), layout.shape.plane_count());
  kernel_checkpoint();
  metadata_guard();
  return {std::move(dataset), layout.signed16_density, layout.preserve_density_calibration};
}
} // namespace
ImageJDatasetProjectionResult project_imagej_dataset(
    const Dataset& source, const ImageJDatasetProjectionRequest& request,
    const ExecutionControl& control) {
  return project_dataset_profile(source, request, control, false, false);
}
ImageJDatasetProjectionResult project_imagej_rgb_dataset(
    const Dataset& source, const ImageJRGBProjectionRequest& request,
    const ExecutionControl& control) {
  return project_dataset_profile(source, request, control, true, request.clip_when_summing);
}
} // namespace imagejxx
