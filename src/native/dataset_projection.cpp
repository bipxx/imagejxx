#include <imagejxx/dataset_projection.hpp>
#include <stdexcept>

namespace imagejxx {
namespace {
std::size_t pixel_bytes(std::size_t count) {
  if (count > std::numeric_limits<std::size_t>::max() / sizeof(double))
    throw std::invalid_argument("Projection pixel storage exceeds addressable memory");
  return count * sizeof(double);
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
  auto output_calibration = calibration;
  if (request.first != 0)
    output_calibration.origin = calibration.voxel_to_world({0, 0, double(request.first)});
  if (request.group_size != 0) output_calibration.spacing[2] *= group;
  if (first_t != 0) output_calibration.time_origin = calibration.time(first_t);
  output_calibration.validate();
  return {output_shape, output_calibration,
          method == "maximum" || method == "minimum" ? type : PixelType::Float64,
          first_t, first_c, request.first, group, bytes, pixel_bytes(group)};
}

DatasetZProjectionLayout describe_dataset_z_projection(
    const Dataset &source, const DatasetZProjectionRequest &request) {
  return describe_dataset_z_projection(source.shape(), source.pixel_type(),
                                       source.calibration(), request);
}
} // namespace imagejxx
