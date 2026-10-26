#include <imagejxx/dataset_projection.hpp>
#include <imagejxx/dataset_projection_execution.hpp>
#include "kernel_control.hpp"
#include "projection_internal.hpp"
#include <cmath>
#include <stdexcept>

namespace imagejxx {
namespace {
std::size_t pixel_bytes(std::size_t count) {
  if (count > std::numeric_limits<std::size_t>::max() / sizeof(double))
    throw std::invalid_argument("Projection pixel storage exceeds addressable memory");
  return count * sizeof(double);
}
std::size_t product(std::size_t count, std::size_t bytes) {
  if (bytes && count > std::numeric_limits<std::size_t>::max() / bytes)
    throw std::invalid_argument("Projection working storage exceeds addressable memory");
  return count * bytes;
}
std::size_t sum(std::size_t a, std::size_t b) {
  if (a > std::numeric_limits<std::size_t>::max() - b)
    throw std::invalid_argument("Projection working storage exceeds addressable memory");
  return a + b;
}
} // namespace

PlanePosition DatasetZProjectionLayout::contributor(PlanePosition output,
                                                   int within_group) const {
  if (output.t < 0 || output.t >= shape.t || output.c < 0 || output.c >= shape.c ||
      output.z < 0 || output.z >= shape.z || within_group < 0 ||
      within_group >= contributors_per_group)
    throw std::out_of_range("Projection output or contributor index is out of range");
  const auto t = std::int64_t(source_first_t) + output.t;
  const auto c = std::int64_t(source_first_c) + output.c;
  const auto z = std::int64_t(source_first_z) +
                 std::int64_t(output.z) * contributors_per_group + within_group;
  const auto limit = std::numeric_limits<int>::max();
  if (t < 0 || t > limit || c < 0 || c > limit || z < 0 || z > limit)
    throw std::out_of_range("Projection contributor exceeds supported coordinates");
  return {static_cast<int>(t), static_cast<int>(c), static_cast<int>(z)};
}

DatasetZProjectionLayout describe_dataset_z_projection(
    DatasetShape shape, PixelType type, const SpatialCalibration &calibration,
    const DatasetZProjectionRequest &request) {
  shape.validate();
  calibration.validate();
  switch (type) {
  case PixelType::UInt8: case PixelType::UInt16:
  case PixelType::Float32: case PixelType::Float64: break;
  default: throw std::invalid_argument("Invalid projection source pixel type");
  }
  if (request.first < 0 || request.first > request.last || request.last >= shape.z ||
      request.active_t < 0 || request.active_t >= shape.t ||
      request.active_c < 0 || request.active_c >= shape.c)
    throw std::out_of_range("Projection source Z/T/C index is out of range");
  const auto &method = request.method;
  if (method != "maximum" && method != "minimum" && method != "mean" &&
      method != "sum" && method != "median" && method != "stddev")
    throw std::invalid_argument("Unknown projection method");
  const int count = request.last - request.first + 1;
  const int group = request.group_size == 0 ? count : request.group_size;
  if (group <= 0 || group > count || count % group)
    throw std::invalid_argument("Projection group size must divide the selected Z count exactly");
  auto output_shape = shape;
  output_shape.t = request.all_time_frames ? shape.t : 1;
  output_shape.c = request.all_channels ? shape.c : 1;
  output_shape.z = count / group;
  const int first_t = request.all_time_frames ? 0 : request.active_t;
  const int first_c = request.all_channels ? 0 : request.active_c;
  const auto bytes = pixel_bytes(output_shape.pixel_count());
  if (bytes > request.max_output_pixel_bytes)
    throw std::invalid_argument("Projection output exceeds its pixel storage limit");
  // During final ownership conversion both the moved-from Image table and the
  // immutable plane owners may coexist. Contributors retain one group only.
  const auto plane_pixels = product(product(std::size_t(shape.x), shape.y), shape.s);
  const auto output_owners = product(output_shape.plane_count(),
      2 * sizeof(Image) + 3 * sizeof(std::shared_ptr<const Image>));
  const auto contributors = product(group, sum(pixel_bytes(plane_pixels),
      sizeof(Image) + 2 * sizeof(std::shared_ptr<const Image>)));
  const auto scratch_bytes = pixel_bytes(group);
  const auto working = sum(sum(bytes, output_owners), sum(contributors, scratch_bytes));
  if (working > request.max_working_bytes)
    throw std::invalid_argument("Projection exceeds its native working storage limit");
  auto output_calibration = calibration;
  if (request.first != 0)
    output_calibration.origin = calibration.voxel_to_world({0, 0, double(request.first)});
  if (request.group_size != 0) output_calibration.spacing[2] *= group;
  if (first_t != 0) output_calibration.time_origin = calibration.time(first_t);
  output_calibration.validate();
  return {output_shape, output_calibration,
          method == "maximum" || method == "minimum" ? type : PixelType::Float64,
          first_t, first_c, request.first, group, bytes, scratch_bytes, working};
}

DatasetZProjectionLayout describe_dataset_z_projection(
    const Dataset &source, const DatasetZProjectionRequest &request) {
  return describe_dataset_z_projection(source.shape(), source.pixel_type(),
                                       source.calibration(), request);
}

Dataset project_dataset_z(const Dataset &source,
                          const DatasetZProjectionRequest &request,
                          const ExecutionControl &control) {
  ExecutionScope scope(control);
  detail::without_execution_callbacks([] { kernel_checkpoint(); });
  const auto source_shape = source.shape();
  const auto source_type = source.pixel_type();
  const auto layout = describe_dataset_z_projection(source_shape, source_type,
                                                    source.calibration(), request);
  if (source.sample_policy() != FloatingSamplePolicy::FiniteOnly)
    throw std::invalid_argument("Native projection requires the finite sample profile");
  const auto method = request.method;
  const bool validate_samples = !source.has_eager_planes();
  KernelStage stage("Scientific Z projection", layout.shape.plane_count());
  Stack output;
  output.reserve(layout.shape.plane_count());
  std::vector<std::shared_ptr<const Image>> contributors;
  contributors.reserve(layout.contributors_per_group);
  std::vector<double> scratch(layout.contributors_per_group);
  for (int t = 0; t < layout.shape.t; ++t)
    for (int c = 0; c < layout.shape.c; ++c)
      for (int z = 0; z < layout.shape.z; ++z) {
        stage.checkpoint(output.size());
        execution_progress(output.size(), layout.shape.plane_count());
        kernel_checkpoint();
        contributors.clear();
        for (int k = 0; k < layout.contributors_per_group; ++k) {
          kernel_checkpoint();
          auto plane = source.plane(layout.contributor({t, c, z}, k));
          if (plane->width != source_shape.x || plane->height != source_shape.y ||
              plane->channels != source_shape.s || plane->type != source_type)
            throw std::invalid_argument("Projection provider differs from captured source metadata");
          if (validate_samples) {
            KernelStage validation("Projection source samples", plane->pixels.size());
            for (std::size_t i = 0; i < plane->pixels.size(); ++i) {
              validation.periodic(i);
              require_finite(plane->pixels[i], "Projection input");
              if (plane->type == PixelType::Float32 &&
                  std::abs(plane->pixels[i]) > double(std::numeric_limits<float>::max()))
                throw std::invalid_argument("Projection Float32 input exceeds its finite range");
            }
            validation.finish();
          }
          contributors.push_back(std::move(plane));
        }
        output.push_back(detail::reduce_projection_group(contributors, method, scratch));
      }
  contributors.clear();
  stage.finish();
  auto result = Dataset::from_planes(layout.shape, std::move(output), layout.calibration);
  execution_progress(layout.shape.plane_count(), layout.shape.plane_count());
  kernel_checkpoint();
  return result;
}
} // namespace imagejxx
