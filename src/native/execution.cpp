#include "kernel_control.hpp"
#include "calibration_preparation_internal.hpp"
#include <imagejxx/imagej_intensity_execution.hpp>
#include "imagej_projection_document.hpp"
#include "floating_charconv.hpp"
#include "target_mask_resources.hpp"
#include "rank_display_internal.hpp"
#include "rgb_dataset_internal.hpp"
#include "color_stack_internal.hpp"
#include "indexed_dataset_internal.hpp"
#include "indexed_rgb_internal.hpp"
#include "indexed_type_internal.hpp"
#include "axis_region_remap.hpp"
#include "workflow_stage_metadata.hpp"
#include <imagejxx/manual_threshold.hpp>
#include <imagejxx/auto_threshold_stack.hpp>
#include <imagejxx/channel_display.hpp>
#include <imagejxx/z_subset_command.hpp>
#include <imagejxx/axis_spacing_command.hpp>
#include <imagejxx/slicer_resize_command.hpp>
#include "operation_internal.hpp"
#include "retained_resource_internal.hpp"
#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstring>
#include <imagejxx/application.hpp>
#include <imagejxx/gaussian_command.hpp>
#include <imagejxx/rank_command.hpp>
#include <limits>
#include <set>

namespace imagejxx {
namespace {
using Key = std::array<int, 3>;
Key key(PlanePosition p) { return {p.t, p.c, p.z}; }
PlanePosition position(Key p) { return {p[0], p[1], p[2]}; }
std::size_t index(DatasetShape shape, PlanePosition p) {
  return (std::size_t(p.t) * shape.c + p.c) * shape.z + p.z;
}
PixelType common_type(PixelType a, PixelType b) {
  // Every U8/U16 value is exactly representable in F32; F64 contains F32.
  return static_cast<PixelType>(
      std::max(static_cast<int>(a), static_cast<int>(b)));
}
void checkpoint(const ExecutionControl &control) {
  (void)control;
  kernel_checkpoint();
}
void check_canceled(const std::shared_ptr<CancellationToken> &token) {
  if (token && token->canceled())
    throw OperationCancelled();
}
std::size_t export_product(std::size_t a, std::size_t b) {
  if (b && a > std::numeric_limits<std::size_t>::max() / b)
    throw std::invalid_argument("Plane export working memory exceeds addressable storage");
  return a * b;
}
std::size_t export_sum(std::size_t a, std::size_t b) {
  if (a > std::numeric_limits<std::size_t>::max() - b)
    throw std::invalid_argument("Plane export working memory exceeds addressable storage");
  return a + b;
}
using ExportLayout = AxisExtractionLayout;
ExportLayout export_layout(DatasetShape shape, PixelType type,
                           const SpatialCalibration &original,
                           const AxisPlaneExportRequest &request) {
  shape.validate();
  std::array<int, 3> axes;
  std::string name;
  switch (request.plane) {
  case Plane::XY: axes = {0, 1, 2}; name = "XY"; break;
  case Plane::XZ: axes = {0, 2, 1}; name = "XZ"; break;
  case Plane::YZ: axes = {1, 2, 0}; name = "YZ"; break;
  default: throw std::invalid_argument("Invalid plane export orientation");
  }
  const std::array<int, 3> spatial{shape.x, shape.y, shape.z};
  if (request.coordinate < 0 || request.coordinate >= spatial[axes[2]] ||
      request.t < 0 || request.t >= shape.t || request.c < 0 || request.c >= shape.c)
    throw std::out_of_range("Plane export coordinate/T/C index is out of range");
  switch (type) {
  case PixelType::UInt8: case PixelType::UInt16:
  case PixelType::Float32: case PixelType::Float64: break;
  default: throw std::invalid_argument("Invalid plane export source pixel type");
  }
  original.validate();
  std::array<double, 3> fixed_voxel{};
  fixed_voxel[axes[2]] = request.coordinate;
  auto calibration = original;
  calibration.origin = original.voxel_to_world(fixed_voxel);
  calibration.time_origin = original.time(request.t);
  for (int column = 0; column < 3; ++column) {
    calibration.spacing[column] = original.spacing[axes[column]];
    for (int row = 0; row < 3; ++row)
      calibration.direction[row * 3 + column] = original.direction[row * 3 + axes[column]];
  }
  calibration.validate();
  const std::array<std::string, 3> labels{"X", "Y", "Z"};
  PlaneGeometry geometry{calibration.origin, {}, {},
                         labels[axes[0]], labels[axes[1]], original.unit};
  for (int row = 0; row < 3; ++row) {
    geometry.horizontal_step[row] = original.direction[row * 3 + axes[0]] * original.spacing[axes[0]];
    geometry.vertical_step[row] = original.direction[row * 3 + axes[1]] * original.spacing[axes[1]];
    require_finite(geometry.horizontal_step[row], "Export horizontal world step");
    require_finite(geometry.vertical_step[row], "Export vertical world step");
  }
  DatasetShape output{1, 1, 1, spatial[axes[1]], spatial[axes[0]], shape.s};
  output.validate();
  const auto samples = export_product(std::size_t(output.x),
      export_product(std::size_t(output.y), std::size_t(shape.s)));
  if (samples > std::vector<double>().max_size())
    throw std::invalid_argument("Plane export output exceeds addressable sample storage");
  const auto output_bytes = export_product(samples, sizeof(double));
  const auto fallback_bytes = export_product(std::size_t(shape.x),
      export_product(std::size_t(shape.y), export_product(std::size_t(shape.s), sizeof(double))));
  const auto strip_bytes = request.plane == Plane::XY ? 0 :
      export_product(std::size_t(output.x), export_product(std::size_t(shape.s), sizeof(double)));
  const auto working = export_sum(export_sum(output_bytes, strip_bytes), fallback_bytes);
  if (working > request.max_working_bytes)
    throw std::invalid_argument("Plane export working-memory estimate exceeds its configured cap");
  return {output, std::move(calibration), std::move(geometry), axes, std::move(name), working};
}
ExportLayout export_layout(const Dataset &source,
                           const AxisPlaneExportRequest &request) {
  return export_layout(source.shape(), source.pixel_type(), source.calibration(), request);
}
ExportLayout range_reslice_layout(DatasetShape source_shape, PixelType type,
                                  const SpatialCalibration &calibration,
                                  const AxisRangeResliceRequest &request) {
  if (request.step <= 0 || !request.max_working_bytes)
    throw std::invalid_argument("Range reslice requires a positive step and working-memory cap");
  if (request.first < 0 || request.last < request.first)
    throw std::out_of_range("Range reslice requires 0 <= first <= last");
  // Reuse the admitted singleton layout, full direction permutation and direct
  // first-plane basis. No pixel read occurs in this validation path.
  auto layout = export_layout(source_shape, type, calibration,
      {request.plane, request.first, request.t, request.c, request.max_working_bytes});
  const std::array<int, 3> spatial{source_shape.x, source_shape.y, source_shape.z};
  if (request.last >= spatial[layout.source_axes[2]])
    throw std::out_of_range("Range reslice last index is out of range");
  // Admitted nonnegative int endpoints make this subtraction and count safe.
  layout.shape.z = 1 + (request.last - request.first) / request.step;
  layout.shape.validate();
  if (std::size_t(layout.shape.z) > Stack().max_size())
    throw std::invalid_argument("Range reslice plane table exceeds addressable storage");
  const auto plane_samples = export_product(std::size_t(layout.shape.x),
      export_product(std::size_t(layout.shape.y), std::size_t(layout.shape.s)));
  const auto samples = export_product(std::size_t(layout.shape.z), plane_samples);
  if (samples > std::vector<double>().max_size())
    throw std::invalid_argument("Range reslice output exceeds addressable sample storage");
  const auto singleton_bytes = export_product(plane_samples, sizeof(double));
  layout.working_bytes = export_sum(export_product(samples, sizeof(double)),
                                    layout.working_bytes - singleton_bytes);
  if (layout.working_bytes > request.max_working_bytes)
    throw std::invalid_argument("Range reslice working-memory estimate exceeds its configured cap");
  layout.calibration.spacing[2] *= double(request.step);
  layout.calibration.validate();
  // A finite direction and spacing need not have a finite product. Admit all
  // three direct basis columns and the complete emitted volume before reads.
  for (int row = 0; row < 3; ++row)
    for (int column = 0; column < 3; ++column)
      require_finite(layout.calibration.direction[row * 3 + column] *
                         layout.calibration.spacing[column],
                     "Range reslice world basis");
  for (int z : {0, layout.shape.z - 1})
    for (int y : {0, layout.shape.y - 1})
      for (int x : {0, layout.shape.x - 1})
        (void)layout.calibration.voxel_to_world({double(x), double(y), double(z)});
  return layout;
}
ExportLayout range_reslice_layout(const Dataset &source,
                                  const AxisRangeResliceRequest &request) {
  return range_reslice_layout(source.shape(), source.pixel_type(), source.calibration(), request);
}
ExportLayout range_orientation_layout(
    DatasetShape source_shape, PixelType type,
    const SpatialCalibration &original,
    const AxisRangeOrientationRequest &request) {
  auto layout = range_reslice_layout(source_shape, type, original, request.range);
  const auto plane_samples = export_product(std::size_t(layout.shape.x),
      export_product(std::size_t(layout.shape.y), std::size_t(layout.shape.s)));
  // One extracted plane coexists with the already admitted complete output,
  // source XY fallback and orthogonal strip. No full intermediate stack.
  layout.working_bytes = export_sum(layout.working_bytes,
                                    export_product(plane_samples, sizeof(double)));
  if (layout.working_bytes > request.range.max_working_bytes)
    throw std::invalid_argument("Range orientation working-memory estimate exceeds its configured cap");
  const auto last_selected = int(std::int64_t(request.range.first) +
      std::int64_t(layout.shape.z - 1) * request.range.step);
  std::array<double, 3> first_voxel{};
  first_voxel[layout.source_axes[1]] = request.flip_vertical ? layout.shape.y - 1 : 0;
  first_voxel[layout.source_axes[2]] = request.reverse_order ? last_selected : request.range.first;
  layout.calibration.origin = original.voxel_to_world(first_voxel);
  for (int row = 0; row < 3; ++row) {
    if (request.flip_vertical)
      layout.calibration.direction[row * 3 + 1] *= -1;
    if (request.reverse_order)
      layout.calibration.direction[row * 3 + 2] *= -1;
  }
  if (request.transpose) {
    std::swap(layout.shape.x, layout.shape.y);
    std::swap(layout.calibration.spacing[0], layout.calibration.spacing[1]);
    for (int row = 0; row < 3; ++row)
      std::swap(layout.calibration.direction[row * 3],
                layout.calibration.direction[row * 3 + 1]);
  }
  layout.shape.validate();
  layout.calibration.validate();
  for (int row = 0; row < 3; ++row)
    for (int column = 0; column < 3; ++column)
      require_finite(layout.calibration.direction[row * 3 + column] *
                         layout.calibration.spacing[column],
                     "Range orientation world basis");
  for (int z : {0, layout.shape.z - 1})
    for (int y : {0, layout.shape.y - 1})
      for (int x : {0, layout.shape.x - 1})
        (void)layout.calibration.voxel_to_world({double(x), double(y), double(z)});
  layout.geometry.origin = layout.calibration.origin;
  const std::array<std::string, 3> labels{"X", "Y", "Z"};
  layout.geometry.horizontal_axis = labels[layout.source_axes[request.transpose ? 1 : 0]];
  layout.geometry.vertical_axis = labels[layout.source_axes[request.transpose ? 0 : 1]];
  for (int row = 0; row < 3; ++row) {
    layout.geometry.horizontal_step[row] = layout.calibration.direction[row * 3] *
                                           layout.calibration.spacing[0];
    layout.geometry.vertical_step[row] = layout.calibration.direction[row * 3 + 1] *
                                         layout.calibration.spacing[1];
  }
  return layout;
}
ExportLayout range_orientation_layout(
    const Dataset &source, const AxisRangeOrientationRequest &request) {
  return range_orientation_layout(source.shape(), source.pixel_type(),
                                   source.calibration(), request);
}
struct SpacingLayout {
  ExportLayout output;
  AxisRangeSpacingEstimate estimate;
  int source_vertical_count;
  std::size_t pixel_bytes, object_bytes;
};
SpacingLayout range_spacing_layout(DatasetShape shape, PixelType type,
                                   const SpatialCalibration &original,
                                   FloatingSamplePolicy policy,
                                   const AxisRangeSpacingRequest &request) {
  const auto &orientation = request.orientation;
  const auto &range = orientation.range;
  if (range.plane != Plane::XZ && range.plane != Plane::YZ)
    throw std::invalid_argument("Physical Z spacing requires an XZ or YZ plane");
  if (request.sampling != AxisRangeSampling::Nearest &&
      request.sampling != AxisRangeSampling::Linear)
    throw std::invalid_argument("Invalid physical range sampling mode");
  require_finite(request.physical_vertical_spacing, "Physical Z spacing");
  if (request.physical_vertical_spacing <= 0 || range.step <= 0 ||
      !range.max_working_bytes)
    throw std::invalid_argument("Physical range requires positive spacing, step and memory cap");
  if (range.first < 0 || range.last < range.first)
    throw std::out_of_range("Physical range requires 0 <= first <= last");
  if (policy != FloatingSamplePolicy::FiniteOnly &&
      (policy != FloatingSamplePolicy::PreserveQuietNaNFloat32 ||
       type != PixelType::Float32 || shape.s != 1))
    throw std::invalid_argument("Invalid physical-grid source sample policy");
  if (policy == FloatingSamplePolicy::PreserveQuietNaNFloat32 &&
      request.sampling == AxisRangeSampling::Linear)
    throw std::invalid_argument("Linear physical sampling does not admit the quiet-NaN source policy");
  // Validate the original singleton geometry without applying the old full
  // range cap, which could falsely reject a downsampled output.
  auto layout = export_layout(shape, type, original, {range.plane, range.first, range.t, range.c,
                                      std::numeric_limits<std::size_t>::max()});
  const std::array<int, 3> spatial{shape.x, shape.y, shape.z};
  if (range.last >= spatial[layout.source_axes[2]])
    throw std::out_of_range("Physical range last index is out of range");
  const auto length = std::hypot(layout.geometry.vertical_step[0],
                                 layout.geometry.vertical_step[1],
                                 layout.geometry.vertical_step[2]);
  require_finite(length, "Source Z basis length");
  if (length <= 0)
    throw std::invalid_argument("Source Z basis length must be positive");
  volatile double step_value = request.physical_vertical_spacing / length;
  const double delta = step_value;
  require_finite(delta, "Physical grid voxel step");
  if (delta <= 0)
    throw std::invalid_argument("Physical grid voxel step must be positive");
  volatile double quotient_value = double(shape.z - 1) / delta;
  const double quotient = quotient_value;
  require_finite(quotient, "Physical grid dimension quotient");
  const double intervals = std::floor(quotient);
  const auto maximum = std::numeric_limits<int>::max() - 8194;
  if (intervals < 0 || intervals > double(maximum - 1))
    throw std::invalid_argument("Physical grid vertical count exceeds supported coordinates");
  const int count = 1 + int(intervals);
  const int planes = 1 + (range.last - range.first) / range.step;
  const int last_selected = int(std::int64_t(range.first) +
      std::int64_t(planes - 1) * range.step);
  const int effective_first = orientation.reverse_order ? last_selected : range.first;
  const int effective_last = orientation.reverse_order ? range.first : last_selected;
  const int horizontal = spatial[layout.source_axes[0]];
  layout.shape = {1, 1, planes, count, horizontal, shape.s};
  std::array<double, 3> first_voxel{};
  first_voxel[2] = orientation.flip_vertical ? shape.z - 1 : 0;
  first_voxel[layout.source_axes[2]] = effective_first;
  layout.calibration.origin = original.voxel_to_world(first_voxel);
  volatile double vertical_spacing = original.spacing[2] * delta;
  layout.calibration.spacing[1] = vertical_spacing;
  volatile double fixed_spacing = original.spacing[layout.source_axes[2]] * double(range.step);
  layout.calibration.spacing[2] = fixed_spacing;
  for (int row = 0; row < 3; ++row) {
    if (orientation.flip_vertical)
      layout.calibration.direction[row * 3 + 1] *= -1;
    if (orientation.reverse_order)
      layout.calibration.direction[row * 3 + 2] *= -1;
  }
  if (orientation.transpose) {
    std::swap(layout.shape.x, layout.shape.y);
    std::swap(layout.calibration.spacing[0], layout.calibration.spacing[1]);
    for (int row = 0; row < 3; ++row)
      std::swap(layout.calibration.direction[row * 3], layout.calibration.direction[row * 3 + 1]);
  }
  layout.shape.validate();
  layout.calibration.validate();
  for (int row = 0; row < 3; ++row)
    for (int column = 0; column < 3; ++column)
      require_finite(layout.calibration.direction[row * 3 + column] *
                         layout.calibration.spacing[column], "Physical grid world basis");
  for (int z : {0, planes - 1})
    for (int y : {0, layout.shape.y - 1})
      for (int x : {0, layout.shape.x - 1})
        (void)layout.calibration.voxel_to_world({double(x), double(y), double(z)});
  const auto a_samples = export_product(std::size_t(horizontal), std::size_t(shape.s));
  const auto output_plane_samples = export_product(a_samples, std::size_t(count));
  const auto output_samples = export_product(output_plane_samples, std::size_t(planes));
  const auto input_samples = export_product(a_samples, std::size_t(shape.z));
  const auto fallback_samples = export_product(std::size_t(shape.x),
      export_product(std::size_t(shape.y), std::size_t(shape.s)));
  const auto vector_max = std::vector<double>().max_size();
  if (output_samples > vector_max || input_samples > vector_max ||
      fallback_samples > vector_max || a_samples > vector_max ||
      std::size_t(planes) > Stack().max_size() ||
      std::size_t(planes) > std::vector<std::shared_ptr<const Image>>().max_size())
    throw std::invalid_argument("Physical grid buffers exceed addressable storage");
  const auto samples = export_sum(export_sum(output_samples, input_samples),
                                  export_sum(fallback_samples, a_samples));
  const auto table_per_plane = export_sum(export_sum(export_product(2, sizeof(Image)),
      sizeof(std::shared_ptr<const Image>)), std::size_t(256));
  const auto tables = export_sum(export_product(std::size_t(planes), table_per_plane),
                                 export_sum(export_product(2, sizeof(Image)), std::size_t(256)));
  const auto pixel_bytes = export_product(samples, sizeof(double));
  layout.working_bytes = export_sum(pixel_bytes, tables);
  if (layout.working_bytes > range.max_working_bytes)
    throw std::invalid_argument("Physical grid working-memory estimate exceeds its configured cap");
  const std::array<std::string, 3> names{"X", "Y", "Z"};
  const std::array<std::string, 3> axes{
      names[layout.source_axes[orientation.transpose ? 1 : 0]],
      names[layout.source_axes[orientation.transpose ? 0 : 1]],
      names[layout.source_axes[2]]};
  layout.geometry.origin = layout.calibration.origin;
  layout.geometry.horizontal_axis = axes[0];
  layout.geometry.vertical_axis = axes[1];
  for (int row = 0; row < 3; ++row) {
    layout.geometry.horizontal_step[row] = layout.calibration.direction[row * 3] * layout.calibration.spacing[0];
    layout.geometry.vertical_step[row] = layout.calibration.direction[row * 3 + 1] * layout.calibration.spacing[1];
  }
  AxisRangeSpacingEstimate estimate{layout.shape.x, layout.shape.y, planes, shape.s,
      count, last_selected, effective_first, effective_last, length, delta, axes, layout.working_bytes};
  return {std::move(layout), std::move(estimate), shape.z, pixel_bytes, tables};
}
SpacingLayout range_spacing_layout(const Dataset &source,
                                   const AxisRangeSpacingRequest &request) {
  return range_spacing_layout(source.shape(), source.pixel_type(), source.calibration(),
                              source.sample_policy(), request);
}
double spacing_linear_value(double a, double b, double weight, PixelType type) {
  // Endpoints/equal values preserve stored doubles, including signed zero and
  // noncanonical finite F32/integer values. Quantize only a newly mixed value.
  if (weight == 0 || a == b)
    return a;
  volatile double difference = b - a;
  require_finite(difference, "Physical linear sample difference");
  volatile double weighted = weight * difference;
  require_finite(weighted, "Physical linear weighted sample");
  volatile double mixed = a + weighted;
  double result = mixed;
  require_finite(result, "Physical linear sample");
  switch (type) {
  case PixelType::UInt8:
  case PixelType::UInt16:
    return std::floor(std::clamp(result, 0.0, type == PixelType::UInt8 ? 255.0 : 65535.0) + 0.5);
  case PixelType::Float32: {
    if (result < -double(std::numeric_limits<float>::max()) ||
        result > double(std::numeric_limits<float>::max()))
      throw std::invalid_argument("Physical linear Float32 sample exceeds finite range");
    const float narrowed = static_cast<float>(result);
    require_finite(double(narrowed), "Physical linear Float32 sample");
    return double(narrowed);
  }
  case PixelType::Float64:
    return result;
  default:
    throw std::invalid_argument("Invalid physical linear output pixel type");
  }
}
std::string spacing_title_value(double value) {
  char buffer[64];
  const auto result = detail::floating_to_chars(buffer, buffer + sizeof(buffer), value);
  if (result.ec != std::errc{})
    throw std::invalid_argument("Physical spacing title conversion failed");
  return std::string(buffer, result.ptr);
}
// Each temporary is a separately rounded binary64 operation. Volatile stores
// prevent reassociation/FMA from changing the Java processor expression order.
static_assert(std::numeric_limits<double>::is_iec559 &&
                  std::numeric_limits<double>::digits == 53 && sizeof(double) == 8 &&
                  std::numeric_limits<float>::is_iec559 &&
                  std::numeric_limits<float>::digits == 24 && sizeof(float) == 4,
              "Slicer resize requires IEEE binary64/binary32 sample storage");
double slicer_add(double a, double b) { volatile double v = a + b; return v; }
double slicer_subtract(double a, double b) { volatile double v = a - b; return v; }
double slicer_multiply(double a, double b) { volatile double v = a * b; return v; }
double slicer_divide(double a, double b) { volatile double v = a / b; return v; }
struct SlicerCoordinate { double unbounded, sampled; };
SlicerCoordinate slicer_axis_coordinate(int source, int destination, int index) {
  const double source_center = slicer_divide(double(source), 2.0);
  double destination_center = slicer_divide(double(destination), 2.0);
  const double scale = slicer_divide(double(destination), double(source));
  if (destination != source)
    destination_center = slicer_add(destination_center, slicer_divide(scale, 4.0));
  const double unbounded = slicer_add(
      slicer_divide(slicer_subtract(double(index), destination_center), scale),
      source_center);
  double sampled = unbounded;
  if (sampled < 0.0)
    sampled = 0.0;
  if (sampled >= slicer_subtract(double(source), 1.0))
    sampled = slicer_subtract(double(source), 1.001);
  return {unbounded, sampled};
}
const char *slicer_branch_name(AxisRangeSlicerResizeBranch branch) {
  switch (branch) {
  case AxisRangeSlicerResizeBranch::NoResize: return "no_resize";
  case AxisRangeSlicerResizeBranch::EqualDimensionCrop: return "equal_dimension_crop";
  case AxisRangeSlicerResizeBranch::Bilinear: return "bilinear_resize";
  }
  throw std::invalid_argument("Invalid Slicer resize branch");
}
struct SlicerResizeLayout {
  ExportLayout output;
  AxisRangeSlicerResizeEstimate estimate;
  std::size_t pixel_bytes, object_bytes;
};
SlicerResizeLayout range_slicer_resize_layout(
    DatasetShape shape, PixelType type, const SpatialCalibration &original,
    FloatingSamplePolicy policy, const AxisRangeSlicerResizeRequest &request) {
  const auto &orientation = request.orientation;
  const auto &range = orientation.range;
  if (range.plane != Plane::XZ && range.plane != Plane::YZ)
    throw std::invalid_argument("Slicer resize requires an XZ or YZ plane");
  if (range.step <= 0 || !range.max_working_bytes)
    throw std::invalid_argument("Slicer resize requires a positive step and memory cap");
  if (range.first < 0 || range.last < range.first)
    throw std::out_of_range("Slicer resize requires 0 <= first <= last");
  shape.validate();
  if (shape.s != 1 || policy != FloatingSamplePolicy::FiniteOnly)
    throw std::invalid_argument("Slicer resize requires finite scalar source storage");
  if (type != PixelType::UInt8 && type != PixelType::UInt16 && type != PixelType::Float32)
    throw std::invalid_argument("Slicer resize requires UInt8, UInt16 or Float32 source storage");
  const int horizontal = range.plane == Plane::XZ ? shape.x : shape.y;
  if (horizontal < 2 || shape.z < 2)
    throw std::invalid_argument("Slicer resize requires in-plane and source Z counts of at least two");
  // Singleton metadata supplies the original signed/sheared basis and axes.
  // Its old range/output cap must not pre-reject a smaller resized operation.
  auto layout = export_layout(shape, type, original, {range.plane, range.first, range.t, range.c,
                                      std::numeric_limits<std::size_t>::max()});
  const std::array<int, 3> spatial{shape.x, shape.y, shape.z};
  if (range.last >= spatial[layout.source_axes[2]])
    throw std::out_of_range("Slicer resize last index is out of range");
  const double ratio = slicer_divide(original.spacing[2], original.spacing[0]);
  require_finite(ratio, "Slicer Z spacing ratio");
  if (ratio <= 0.0)
    throw std::invalid_argument("Slicer Z spacing ratio must be positive");
  const double count_value = slicer_multiply(double(shape.z), ratio);
  require_finite(count_value, "Slicer resized Z count");
  const double count_floor = std::floor(count_value);
  if (count_floor < 2.0 || count_floor > double(std::numeric_limits<int>::max() - 8194))
    throw std::invalid_argument("Slicer resized Z count exceeds supported coordinates");
  const int count = int(count_floor);
  const int planes = 1 + (range.last - range.first) / range.step;
  const int last_selected = int(std::int64_t(range.first) +
      std::int64_t(planes - 1) * range.step);
  const int effective_first = orientation.reverse_order ? last_selected : range.first;
  const int effective_last = orientation.reverse_order ? range.first : last_selected;
  const auto branch = ratio == 1.0 ? AxisRangeSlicerResizeBranch::NoResize :
      count == shape.z ? AxisRangeSlicerResizeBranch::EqualDimensionCrop :
                        AxisRangeSlicerResizeBranch::Bilinear;
  const double delta = branch == AxisRangeSlicerResizeBranch::Bilinear ?
      slicer_divide(1.0, slicer_divide(double(count), double(shape.z))) : 1.0;
  const double z0 = branch == AxisRangeSlicerResizeBranch::Bilinear ?
      slicer_axis_coordinate(shape.z, count, 0).unbounded : 0.0;
  require_finite(delta, "Slicer nominal processor vertical step");
  require_finite(z0, "Slicer nominal processor vertical origin");
  if (delta <= 0.0)
    throw std::invalid_argument("Slicer nominal processor vertical step must be positive");
  layout.shape = {1, 1, planes, count, horizontal, 1};
  std::array<double, 3> first_voxel{};
  first_voxel[2] = orientation.flip_vertical ? slicer_subtract(double(shape.z - 1), z0) : z0;
  first_voxel[layout.source_axes[2]] = double(effective_first);
  layout.calibration.origin = original.voxel_to_world(first_voxel);
  layout.calibration.spacing[1] = slicer_multiply(original.spacing[2], delta);
  layout.calibration.spacing[2] = slicer_multiply(original.spacing[layout.source_axes[2]], double(range.step));
  std::array<double, 3> scalar_spacing{
      original.spacing[layout.source_axes[0]],
      slicer_divide(original.spacing[2], ratio),
      slicer_multiply(original.spacing[layout.source_axes[2]], 1.0)};
  for (double value : scalar_spacing) {
    require_finite(value, "Slicer scalar spacing");
    if (value <= 0.0)
      throw std::invalid_argument("Slicer scalar spacing must be positive");
  }
  for (int row = 0; row < 3; ++row) {
    if (orientation.flip_vertical)
      layout.calibration.direction[row * 3 + 1] *= -1;
    if (orientation.reverse_order)
      layout.calibration.direction[row * 3 + 2] *= -1;
  }
  if (orientation.transpose) {
    std::swap(layout.shape.x, layout.shape.y);
    std::swap(layout.calibration.spacing[0], layout.calibration.spacing[1]);
    std::swap(scalar_spacing[0], scalar_spacing[1]);
    for (int row = 0; row < 3; ++row)
      std::swap(layout.calibration.direction[row * 3], layout.calibration.direction[row * 3 + 1]);
  }
  layout.shape.validate();
  layout.calibration.validate();
  for (int row = 0; row < 3; ++row)
    for (int column = 0; column < 3; ++column)
      require_finite(layout.calibration.direction[row * 3 + column] * layout.calibration.spacing[column],
                     "Slicer nominal world basis");
  for (int z : {0, planes - 1})
    for (int y : {0, layout.shape.y - 1})
      for (int x : {0, layout.shape.x - 1})
        (void)layout.calibration.voxel_to_world({double(x), double(y), double(z)});
  const auto output_plane_samples = export_product(std::size_t(horizontal), std::size_t(count));
  const auto output_samples = export_product(output_plane_samples, std::size_t(planes));
  const auto input_samples = export_product(std::size_t(horizontal), std::size_t(shape.z));
  const auto fallback_samples = export_product(std::size_t(shape.x), std::size_t(shape.y));
  const auto strip_samples = std::size_t(horizontal);
  const auto vector_max = std::vector<double>().max_size();
  if (output_samples > vector_max || input_samples > vector_max || fallback_samples > vector_max ||
      strip_samples > vector_max || std::size_t(planes) > Stack().max_size() ||
      std::size_t(planes) > std::vector<std::shared_ptr<const Image>>().max_size())
    throw std::invalid_argument("Slicer resize buffers exceed addressable storage");
  const auto all_samples = export_sum(export_sum(output_samples, input_samples),
                                      export_sum(fallback_samples, strip_samples));
  const auto pixel_bytes = export_product(all_samples, sizeof(double));
  const auto table_per_plane = export_sum(export_sum(export_product(2, sizeof(Image)),
      sizeof(std::shared_ptr<const Image>)), std::size_t(256));
  const auto object_bytes = export_sum(export_product(std::size_t(planes), table_per_plane),
      export_sum(export_product(2, sizeof(Image)), std::size_t(256)));
  layout.working_bytes = export_sum(pixel_bytes, object_bytes);
  if (layout.working_bytes > range.max_working_bytes)
    throw std::invalid_argument("Slicer resize working-memory estimate exceeds its configured cap");
  const std::array<std::string, 3> names{"X", "Y", "Z"};
  const std::array<std::string, 3> axes{
      names[layout.source_axes[orientation.transpose ? 1 : 0]],
      names[layout.source_axes[orientation.transpose ? 0 : 1]], names[layout.source_axes[2]]};
  layout.geometry.origin = layout.calibration.origin;
  layout.geometry.horizontal_axis = axes[0];
  layout.geometry.vertical_axis = axes[1];
  for (int row = 0; row < 3; ++row) {
    layout.geometry.horizontal_step[row] = layout.calibration.direction[row * 3] * layout.calibration.spacing[0];
    layout.geometry.vertical_step[row] = layout.calibration.direction[row * 3 + 1] * layout.calibration.spacing[1];
  }
  AxisRangeSlicerResizeEstimate estimate{layout.shape.x, layout.shape.y, planes, 1,
      shape.z, count, last_selected, effective_first, effective_last, ratio, z0, delta,
      branch, axes, scalar_spacing, layout.working_bytes};
  return {std::move(layout), std::move(estimate), pixel_bytes, object_bytes};
}
SlicerResizeLayout range_slicer_resize_layout(const Dataset &source, const AxisRangeSlicerResizeRequest &request) {
  return range_slicer_resize_layout(source.shape(),source.pixel_type(),source.calibration(),source.sample_policy(),request);
}
std::uint64_t slicer_stored_bits(double value) {
  std::uint64_t bits;
  static_assert(sizeof(bits) == sizeof(value), "Slicer requires binary64 working storage");
  std::memcpy(&bits, &value, sizeof(bits));
  return bits;
}
void slicer_validate_canonical(double value, PixelType type) {
  require_finite(value, "Slicer selected source sample");
  double canonical;
  if (type == PixelType::Float32) {
    if (value < -double(std::numeric_limits<float>::max()) || value > double(std::numeric_limits<float>::max()))
      throw std::invalid_argument("Slicer source Float32 sample exceeds finite range");
    const float narrowed = static_cast<float>(value);
    canonical = double(narrowed);
  } else {
    const double limit = type == PixelType::UInt8 ? 255.0 : 65535.0;
    if (value < 0.0 || value > limit || std::floor(value) != value)
      throw std::invalid_argument("Slicer source integer sample is not canonical");
    canonical = double(static_cast<unsigned int>(value));
  }
  if (slicer_stored_bits(value) != slicer_stored_bits(canonical))
    throw std::invalid_argument("Slicer selected source sample is not the exact widened declared type");
}
double slicer_quantize(double value, PixelType type) {
  require_finite(value, "Slicer bilinear sample");
  if (type == PixelType::Float32) {
    if (value < -double(std::numeric_limits<float>::max()) || value > double(std::numeric_limits<float>::max()))
      throw std::invalid_argument("Slicer bilinear Float32 sample exceeds finite range");
    const float narrowed = static_cast<float>(value);
    require_finite(double(narrowed), "Slicer bilinear Float32 sample");
    return double(narrowed);
  }
  const int mask = type == PixelType::UInt8 ? 255 : 65535;
  if (value < -0.5 || value >= double(mask) + 0.5)
    throw std::invalid_argument("Slicer bilinear unsigned sample escaped its processor range");
  return double(static_cast<int>(slicer_add(value, 0.5)) & mask);
}
double slicer_bilinear_sample(const Image &profile, bool flip, bool transpose,
                             double x, double y, PixelType type) {
  const auto sample = [&](int px, int py) {
    const int a = transpose ? py : px;
    const int row = transpose ? px : py;
    const int z = flip ? profile.height - 1 - row : row;
    return profile.pixels[std::size_t(z) * profile.width + a];
  };
  const int xbase = int(x), ybase = int(y);
  const double xf = slicer_subtract(x, double(xbase));
  const double yf = slicer_subtract(y, double(ybase));
  const double lower_left = sample(xbase, ybase);
  const double lower_right = sample(xbase + 1, ybase);
  const double upper_left = sample(xbase, ybase + 1);
  const double upper_right = sample(xbase + 1, ybase + 1);
  const double upper = slicer_add(upper_left,
      slicer_multiply(xf, slicer_subtract(upper_right, upper_left)));
  const double lower = slicer_add(lower_left,
      slicer_multiply(xf, slicer_subtract(lower_right, lower_left)));
  return slicer_quantize(slicer_add(lower, slicer_multiply(yf, slicer_subtract(upper, lower))), type);
}
class PromotedSource final : public PlaneSource, public detail::ResourceSource {
public:
  PromotedSource(Dataset source, PixelType type,
                 std::map<Key, std::shared_ptr<const Image>> planes)
      : source_(std::move(source)), type_(type), planes_(std::move(planes)) {}
  DatasetShape shape() const override { return source_.shape(); }
  PixelType pixel_type() const override { return type_; }
  const PlaneSource *resource_identity() const override { return this; }
  std::shared_ptr<const PlaneSource> detach_resources() const override {
    auto source = detail::ResourceAccess::detach(source_);
    std::shared_ptr<PromotedSource> copy;
    if (!detail::ResourceAccess::same_source(source, source_)) {
      copy = std::make_shared<PromotedSource>(*this);
      copy->source_ = std::move(source);
    }
    for (const auto &[key, image] : planes_) {
      auto detached = detail::detach_image(image);
      if (detached.owner_before(image) || image.owner_before(detached)) {
        if (!copy)
          copy = std::make_shared<PromotedSource>(*this);
        copy->planes_[key] = std::move(detached);
      }
    }
    return copy;
  }
  std::shared_ptr<const Image> read_plane(PlanePosition p) const override {
    auto found = planes_.find(key(p));
    if (found != planes_.end())
      return found->second;
    auto original = source_.plane(p);
    if (original->type == type_)
      return original;
    auto copy = std::make_shared<Image>(*original);
    copy->type = type_; // Lossless promotion; preserve working pixel values.
    return copy;
  }
  Image read_region(PlanePosition p, int x, int y, int width,
                    int height) const override {
    auto found = planes_.find(key(p));
    if (found != planes_.end())
      return crop(*found->second, x, y, width, height);
    auto result = source_.region(p, x, y, width, height);
    result.type = type_;
    return result;
  }

private:
  Dataset source_;
  PixelType type_;
  std::map<Key, std::shared_ptr<const Image>> planes_;
};
DocumentTransaction make_edit(const DocumentSnapshot &source, Dataset dataset,
                              bool pixels_changed, bool legacy,
                              bool label_image, bool spatial_shape_changed,
                              Change dependencies) {
  auto edit = source.begin_edit(dependencies);
  if (pixels_changed)
    edit.replace_dataset(std::move(dataset));
  if (legacy || pixels_changed)
    edit.set_spectrum(std::nullopt);
  if (legacy)
    edit.set_extension("labels", label_image);
  if (spatial_shape_changed) {
    edit.select(std::nullopt);
    edit.set_previous_selection(source.data().previous_selection);
    edit.set_rois({});
    edit.set_overlays({});
  }
  return edit;
}
} // namespace

AxisExtractionLayout describe_axis_plane_export(
    DatasetShape shape, PixelType type, const SpatialCalibration &calibration,
    const AxisPlaneExportRequest &request) {
  return export_layout(shape, type, calibration, request);
}
AxisExtractionLayout describe_axis_plane_export(
    const Dataset &source, const AxisPlaneExportRequest &request) {
  return export_layout(source, request);
}
AxisExtractionLayout describe_axis_range_reslice(
    DatasetShape shape, PixelType type, const SpatialCalibration &calibration,
    const AxisRangeResliceRequest &request) {
  return range_reslice_layout(shape, type, calibration, request);
}
AxisExtractionLayout describe_axis_range_reslice(
    const Dataset &source, const AxisRangeResliceRequest &request) {
  return range_reslice_layout(source, request);
}
AxisExtractionLayout describe_axis_range_orientation(
    DatasetShape shape, PixelType type, const SpatialCalibration &calibration,
    const AxisRangeOrientationRequest &request) {
  return range_orientation_layout(shape, type, calibration, request);
}
AxisExtractionLayout describe_axis_range_orientation(
    const Dataset &source, const AxisRangeOrientationRequest &request) {
  return range_orientation_layout(source, request);
}

struct PreparedOperation::State {
  ResolvedTarget target;
  std::string operation;
  OperationParameters parameters;
  DocumentTransaction edit;
  DocumentSnapshot preview;
  ExecutionOptions options;
  std::vector<PlaneExecutionOutput> outputs;
  std::shared_ptr<CancellationToken> cancellation;
  mutable std::mutex mutex;
  bool consumed = false;
  std::shared_ptr<Document> fresh_document;
  std::optional<DocumentSnapshot> fresh_baseline;
  // Axis outputs deliberately start clean with empty history. Opt-in rank
  // staging instead preserves the existing generic clone-and-commit result.
  bool staged_rank_document = false;
  std::vector<std::pair<std::shared_ptr<const DocumentData>, std::uint64_t>>
      fresh_undo, fresh_redo;
  std::uint64_t fresh_identity = 0, fresh_saved_identity = 0,
                fresh_next_identity = 0;
  State(ResolvedTarget target, std::string operation,
        OperationParameters parameters, DocumentTransaction edit,
        DisplayState display, ExecutionOptions options,
        std::vector<PlaneExecutionOutput> outputs,
        std::shared_ptr<CancellationToken> cancellation)
      : target(std::move(target)), operation(std::move(operation)),
        parameters(std::move(parameters)), edit(std::move(edit)),
        preview(this->edit.preview()), options(options),
        outputs(std::move(outputs)), cancellation(std::move(cancellation)) {
    // Use a transaction preview's retained identity; replace display through an
    // adapter snapshot only, without publishing any live view or pixel state.
    if (options.reset_manual_threshold)
      reset_manual_threshold_state(display);
    display.normalize(preview.data().dataset.shape());
    preview = preview.with_display(std::move(display));
  }
};
namespace {
void apply_captured_output_density(DocumentTransaction& edit,
    const ResolvedTarget& captured, const Dataset& output) {
  const auto context=captured.calibration_context();
  if(!context)return;
  auto density=context->intensity();
  const auto type=output.pixel_type();const auto samples=output.shape().s;
  if(samples!=1)density=ImageJIntensityCalibration::uncalibrated();
  else if(type!=captured.source().data().dataset.pixel_type() || samples!=captured.source().data().dataset.shape().s)
    density=ImageJIntensityCalibration::uncalibrated(type==PixelType::Float32 || type==PixelType::Float64?density.value_unit():"Gray Value");
  density.validate_for(type,samples);
  if(!density.has_density() && density.value_unit()=="Gray Value")edit.remove_property(imagej_intensity_property);
  else edit.set_property(imagej_intensity_property,density.record());
}
std::shared_ptr<Document>
make_operation_output(const ResolvedTarget &target,
                      const DocumentSnapshot &preview,
                      const std::string &operation,
                      const std::optional<std::string> &title,
                      bool paired_display = false) {
  auto created = std::make_shared<Document>(target.source());
  auto data = preview.data();
  data.metadata.title = title.value_or(
      target.source().data().metadata.title + " (" + operation + ")");
  auto edit = created->begin_edit();
  if(const auto context=target.calibration_context()) {
    edit.replace_dataset(data.dataset.with_calibration(context->spatial()));
    apply_captured_output_density(edit, target, data.dataset);
  } else edit.replace_dataset(data.dataset);
  edit.set_title(data.metadata.title);
  edit.select(data.selection);
  edit.set_previous_selection(data.previous_selection);
  edit.set_rois(data.rois);
  edit.set_overlays(data.overlays);
  edit.set_spectrum(data.spectrum);
  edit.set_record_layout(data.metadata.record_layout);
  for (const auto &[name, value] : data.metadata.extensions)
    edit.set_extension(name, value);
  if (paired_display)
    edit.set_axis_remap_display(preview.display());
  created->commit(edit, preview.display());
  return created;
}
} // namespace
ResolvedTarget detail::CalibrationPreparation::target(
    const ResolvedTarget& captured, const TargetRequest& request) {
  auto resolved=resolve_target(captured.source(),request);
  resolved.calibration_binding_=captured.calibration_binding_;
  return resolved;
}
PreparedOperation::PreparedOperation(std::shared_ptr<State> state)
    : state_(std::move(state)) {}

PreparedOperation prepare_rgb_dataset(const ResolvedRgbDatasetCommand &command,
                                      const ExecutionControl &control, std::size_t cap) {
  if(!cap || command.layout().working_bytes>cap)
    throw std::invalid_argument("RGB execution working-byte limit");
  ExecutionScope scope(control);kernel_checkpoint();
  auto result = detail::prepare_rgb_dataset_edit(command, control, cap);
  TargetRequest request;
  const auto shape = command.source().data().dataset.shape();
  request.scope = TargetScope::Ranges;
  request.t = AxisRange{0, shape.t, 1}; request.c = AxisRange{0, shape.c, 1}; request.z = AxisRange{0, shape.z, 1};
  request.roi = RoiPolicy::WholePlane;
  request.destination = command.request().destination;
  auto state = std::make_shared<PreparedOperation::State>(
      resolve_target(command.source(), request), "rgb_dataset", OperationParameters{},
      std::move(result.transaction), std::move(result.display), ExecutionOptions{},
      std::vector<PlaneExecutionOutput>{}, execution_cancellation());
  if (request.destination == OutputDestination::NewDocument) {
    state->fresh_document = std::make_shared<Document>(state->preview.data(), state->preview.display());
    state->fresh_baseline = state->fresh_document->snapshot();
  }
  kernel_checkpoint();return PreparedOperation(std::move(state));
}

PreparedOperation prepare_color_stack(const ResolvedColorStackCommand &command,
                                      const ExecutionControl &control,std::size_t cap) {
  if(command.layout().working_bytes>cap)throw std::invalid_argument("Color stack execution working-byte limit");
  const auto captured=control;ExecutionScope scope(captured);kernel_checkpoint();
  auto result=detail::prepare_color_stack_edit(command,{},cap);
  TargetRequest request;const auto shape=command.source().data().dataset.shape();
  request.scope=TargetScope::Ranges;request.t=AxisRange{0,shape.t,1};request.c=AxisRange{0,shape.c,1};request.z=AxisRange{0,shape.z,1};request.roi=RoiPolicy::WholePlane;request.destination=command.request().destination;
  auto state=std::make_shared<PreparedOperation::State>(resolve_target(command.source(),request),"color_stack",OperationParameters{},std::move(result.transaction),std::move(result.display),ExecutionOptions{},std::vector<PlaneExecutionOutput>{},execution_cancellation());
  if(request.destination==OutputDestination::NewDocument){state->fresh_document=std::make_shared<Document>(state->preview.data(),state->preview.display());state->fresh_baseline=state->fresh_document->snapshot();}
  kernel_checkpoint();return PreparedOperation(std::move(state));
}

PreparedOperation prepare_indexed_dataset(const ResolvedIndexedDatasetCommand &command,const ExecutionControl &control,std::size_t cap) {
  if(command.layout().working_bytes>cap)throw std::invalid_argument("Indexed execution working-byte limit");
  const auto captured=control;ExecutionScope scope(captured);kernel_checkpoint();auto result=detail::prepare_indexed_dataset_edit(command,{},cap);
  TargetRequest request;const auto shape=command.source().data().dataset.shape();request.scope=TargetScope::Ranges;request.t=AxisRange{0,shape.t,1};request.c=AxisRange{0,shape.c,1};request.z=AxisRange{0,shape.z,1};request.roi=RoiPolicy::WholePlane;request.destination=command.request().destination;
  auto state=std::make_shared<PreparedOperation::State>(resolve_target(command.source(),request),"indexed_dataset",OperationParameters{},std::move(result.transaction),std::move(result.display),ExecutionOptions{},std::vector<PlaneExecutionOutput>{},execution_cancellation());
  if(request.destination==OutputDestination::NewDocument){state->fresh_document=std::make_shared<Document>(state->preview.data(),state->preview.display());state->fresh_baseline=state->fresh_document->snapshot();}
  kernel_checkpoint();return PreparedOperation(std::move(state));
}

PreparedOperation prepare_indexed_rgb(const ResolvedIndexedRgbCommand &command,const ExecutionControl &control,std::size_t cap) {
  if(command.layout().working_bytes>cap)throw std::invalid_argument("Indexed RGB execution working-byte limit");
  const auto captured=control;ExecutionScope scope(captured);kernel_checkpoint();auto result=detail::prepare_indexed_rgb_edit(command,{},cap);
  TargetRequest request;const auto shape=command.source().data().dataset.shape();request.scope=TargetScope::Ranges;request.t=AxisRange{0,shape.t,1};request.c=AxisRange{0,shape.c,1};request.z=AxisRange{0,shape.z,1};request.roi=RoiPolicy::WholePlane;request.destination=command.request().destination;
  auto state=std::make_shared<PreparedOperation::State>(resolve_target(command.source(),request),"indexed_rgb",OperationParameters{},std::move(result.transaction),std::move(result.display),ExecutionOptions{},std::vector<PlaneExecutionOutput>{},execution_cancellation());
  if(request.destination==OutputDestination::NewDocument){state->fresh_document=std::make_shared<Document>(state->preview.data(),state->preview.display());state->fresh_baseline=state->fresh_document->snapshot();}
  kernel_checkpoint();return PreparedOperation(std::move(state));
}

PreparedOperation prepare_z_subset(const ResolvedZSubsetCommand &command,const ExecutionControl &control) {
  const auto frozen_command=command;
  const auto frozen_control=control;
  {const ExecutionControl quiet;ExecutionScope preflight(quiet);
   detail::admit_z_axis_annotations(frozen_command.source(),frozen_command.request());}
  ExecutionScope scope(frozen_control);kernel_checkpoint();
  auto remapped=prepare_z_axis_remap(frozen_command.source(),frozen_command.request(),frozen_control);
  TargetRequest request;request.scope=TargetScope::Ranges;
  const auto shape=frozen_command.source().data().dataset.shape();
  request.t=AxisRange{0,shape.t,1};request.c=AxisRange{0,shape.c,1};request.z=AxisRange{0,shape.z,1};
  request.destination=frozen_command.request().destination;
  auto target=resolve_target(frozen_command.source(),request);
  if(request.destination==OutputDestination::NewDocument)
    remapped.transaction.set_title(frozen_command.source().data().metadata.title+" Z subset");
  auto state=std::make_shared<PreparedOperation::State>(std::move(target),"z_subset",OperationParameters{},
      std::move(remapped.transaction),std::move(remapped.display),ExecutionOptions{},std::vector<PlaneExecutionOutput>{},execution_cancellation());
  if(request.destination==OutputDestination::NewDocument) {
    state->fresh_document=std::make_shared<Document>(state->preview.data(),state->preview.display());
    state->fresh_baseline=state->fresh_document->snapshot();
  }
  kernel_checkpoint();return PreparedOperation(std::move(state));
}
void PreparedOperation::retain_resources(std::shared_ptr<const void> guard) {
  if (!guard)
    return;
  std::lock_guard<std::mutex> lock(state_->mutex);
  state_->edit.retain_resources(std::move(guard));
  state_->preview = state_->edit.preview().with_display(state_->preview.display());
}
const ResolvedTarget &PreparedOperation::target() const {
  return state_->target;
}
const std::string &PreparedOperation::operation() const {
  return state_->operation;
}
const OperationParameters &PreparedOperation::parameters() const {
  return state_->parameters;
}
const std::vector<PlaneExecutionOutput> &PreparedOperation::outputs() const {
  return state_->outputs;
}
const DocumentSnapshot &PreparedOperation::preview() const {
  return state_->preview;
}
ExecutionOptions PreparedOperation::options() const { return state_->options; }
bool PreparedOperation::active() const {
  std::lock_guard<std::mutex> lock(state_->mutex);
  return !state_->consumed;
}
DocumentTransaction PreparedOperation::begin_edit() const {
  std::lock_guard<std::mutex> lock(state_->mutex);
  if(state_->options.phase==ExecutionPhase::Preview)throw std::invalid_argument("Preview proposals cannot be published");
  if (state_->consumed)
    throw std::invalid_argument("Prepared operation was already published");
  return state_->edit;
}
std::shared_ptr<Document> PreparedOperation::prepared_new_document() const {
  std::lock_guard<std::mutex> lock(state_->mutex);
  return state_->fresh_document;
}
std::shared_ptr<Document>
PreparedOperation::stage_new_document(std::optional<std::string> title) {
  std::lock_guard<std::mutex> lock(state_->mutex);
  if(state_->options.phase==ExecutionPhase::Preview)throw std::invalid_argument("Preview proposals cannot be published");
  if (state_->consumed)
    throw std::invalid_argument("Prepared operation was already published");
  if ((state_->operation != "rank_filter_imagej" && state_->operation != "remove_outliers_imagej" && state_->operation != "top_hat_imagej" && state_->operation != "remove_nans_imagej") ||
      state_->target.destination() != OutputDestination::NewDocument)
    throw std::invalid_argument(
        "Output staging requires a rank NewDocument target");
  check_canceled(state_->cancellation);
  if (state_->fresh_document) {
    if (title && *title != state_->fresh_baseline->data().metadata.title)
      throw std::invalid_argument("Staged rank output title was already frozen");
    return state_->fresh_document;
  }
  auto candidate = make_operation_output(state_->target, state_->preview,
                                          state_->operation, title,
                                          bool(state_->edit.axis_remap_display()));
  // A normal commit unwraps result resource aliases. Until adoption this owner
  // is still a READY export, so restore the guarded dataset without copying
  // pixels or changing its committed identity/history/revisions.
  auto guarded_data = std::make_shared<DocumentData>(*candidate->data_);
  guarded_data->dataset = state_->preview.data().dataset;
  candidate->data_ = std::move(guarded_data);
  auto baseline = candidate->snapshot();
  decltype(state_->fresh_undo) undo, redo;
  undo.reserve(candidate->undo_.size());
  redo.reserve(candidate->redo_.size());
  for (const auto &entry : candidate->undo_)
    undo.emplace_back(entry.data, entry.identity);
  for (const auto &entry : candidate->redo_)
    redo.emplace_back(entry.data, entry.identity);
  check_canceled(state_->cancellation);
  // Every potentially throwing construction precedes exposing the candidate.
  state_->fresh_baseline.emplace(std::move(baseline));
  state_->fresh_undo.swap(undo);
  state_->fresh_redo.swap(redo);
  state_->fresh_identity = candidate->identity_;
  state_->fresh_saved_identity = candidate->savedIdentity_;
  state_->fresh_next_identity = candidate->nextIdentity_;
  state_->staged_rank_document = true;
  state_->fresh_document = std::move(candidate);
  return state_->fresh_document;
}
PublishedOperation
PreparedOperation::publish(Application &application,
                           std::optional<std::string> title) {
  std::lock_guard<std::mutex> app_lock(application.mutex_);
  std::lock_guard<std::mutex> prepared_lock(state_->mutex);
  if(state_->options.phase==ExecutionPhase::Preview)throw std::invalid_argument("Preview proposals cannot be published");
  if (state_->consumed)
    throw std::invalid_argument("Prepared operation was already published");
  auto source = application.lookup_locked(target().document_id());
  auto current = source->snapshot();
  target().validate_source(current);
  application.validate_target_calibration_locked(target());
  if (!current.revisions().matches(state_->edit.source_revisions(),
                                   state_->edit.dependencies()))
    throw RevisionConflict();
  check_canceled(state_->cancellation);
  PublishedOperation result{source->id, source, false, state_->outputs};
  if (state_->fresh_document) {
    // Public Document writers do not take the Application mutex. Hold both
    // owners through revalidation and adoption so neither can change between
    // these checks and the publication point. Fresh owners have distinct IDs.
    std::scoped_lock document_locks(source->mutex_, state_->fresh_document->mutex_);
    current = source->snapshot_locked();
    target().validate_source(current);
    if (!current.revisions().matches(state_->edit.source_revisions(),
                                     state_->edit.dependencies()))
      throw RevisionConflict();
    const auto &baseline = *state_->fresh_baseline;
    if (title && *title != baseline.data().metadata.title)
      throw std::invalid_argument(state_->staged_rank_document
          ? "Staged rank output title was already frozen"
          : "Fresh plane export title was frozen during preparation");
    const auto candidate = state_->fresh_document->snapshot_locked();
    if (!candidate.same_document(baseline) ||
        !candidate.revisions().matches(baseline.revisions(), Change::Data) ||
        candidate.data_identity() != baseline.data_identity() ||
        !candidate.shares_data(baseline))
      throw RevisionConflict();
    std::shared_ptr<const DocumentData> detached_data;
    if (state_->staged_rank_document) {
      const auto &fresh = *state_->fresh_document;
      const auto retained_history = [](const auto &actual, const auto &expected) {
        // Public policy changes can discard oldest entries without changing
        // current data or identity counters. Permit only an exact retained
        // suffix: no added, replaced or reordered entries. Data revisions and
        // current/next/saved identities below still exclude edits/undo/redo.
        if (actual.size() > expected.size()) return false;
        const auto offset = expected.size() - actual.size();
        for (std::size_t i = 0; i < actual.size(); ++i)
          if (actual[i].data != expected[offset + i].first ||
              actual[i].identity != expected[offset + i].second) return false;
        return true;
      };
      if (fresh.identity_ != state_->fresh_identity ||
          fresh.savedIdentity_ != state_->fresh_saved_identity ||
          fresh.nextIdentity_ != state_->fresh_next_identity ||
          !retained_history(fresh.undo_, state_->fresh_undo) ||
          !retained_history(fresh.redo_, state_->fresh_redo))
        throw RevisionConflict();
      auto detached = *fresh.data_;
      detached.dataset = detail::ResourceAccess::detach(detached.dataset);
      fresh.validate_constraints(detached, fresh.display_);
      detached_data = std::make_shared<const DocumentData>(std::move(detached));
    } else if (!state_->fresh_document->undo_.empty() ||
               !state_->fresh_document->redo_.empty()) {
      throw RevisionConflict();
    }
    check_canceled(state_->cancellation);
    result.document = state_->fresh_document;
    result.document_id = application.adopt_locked(result.document, true);
    // Adoption can allocate/fail. Once it succeeds, only a noexcept ownership
    // swap remains before releasing either document lock. Retained prepared or
    // preview snapshots keep their guards; the adopted owner holds raw pixels.
    if (detached_data)
      state_->fresh_document->data_.swap(detached_data);
    result.created = true;
  } else if (target().destination() == OutputDestination::InPlace) {
    if (title)
      throw std::invalid_argument(
          "An output title requires a new-document target");
    source->commit_impl(state_->edit, preview().display(), true,
                         state_->options.reset_manual_threshold);
  } else {
    auto created = make_operation_output(target(), preview(), operation(), title,
                                        bool(state_->edit.axis_remap_display()));
    check_canceled(state_->cancellation);
    // Direct Document writers do not take the registry lock. Recheck the
    // original source under its lock immediately before derived adoption.
    std::lock_guard<std::mutex> source_lock(source->mutex_);
    current=source->snapshot_locked();target().validate_source(current);
    if(!current.revisions().matches(state_->edit.source_revisions(),state_->edit.dependencies()))throw RevisionConflict();
    result.document = created;
    result.document_id = application.adopt_locked(std::move(created), true);
    result.created = true;
  }
  state_->consumed = true;
  return result;
}

AxisPlaneExportResult export_axis_plane(
    const Dataset &source, const AxisPlaneExportRequest &request_input,
    const ExecutionControl &control) {
  const auto request = request_input;
  ExecutionScope scope(control);
  kernel_checkpoint();
  auto layout = export_layout(source, request);
  const auto reads = request.plane == Plane::XY ? std::size_t(1) : std::size_t(source.shape().z);
  execution_progress(0, reads);
  kernel_checkpoint();
  auto image = source.extract_plane(request.plane, request.coordinate, request.t, request.c);
  kernel_checkpoint();
  Stack planes;
  planes.reserve(1);
  planes.push_back(std::move(image));
  kernel_checkpoint();
  auto output = Dataset::from_planes(layout.shape, std::move(planes),
                                    layout.calibration, source.sample_policy());
  kernel_checkpoint();
  execution_progress(reads, reads);
  kernel_checkpoint();
  return {std::move(output), std::move(layout.geometry), request, layout.working_bytes};
}

AxisRangeResliceResult reslice_axis_range(
    const Dataset &source_input, const AxisRangeResliceRequest &request_input,
    const ExecutionControl &control) {
  // Capture values before any user callback, including a mutable caller's
  // Dataset handle; only the retained immutable source supplies the samples.
  const auto source = source_input;
  const auto request = request_input;
  std::size_t completed_reads = 0, total_reads = 0;
  auto range_control = control;
  const auto progress = range_control.progress;
  if (progress)
    range_control.progress = [&](std::size_t done, std::size_t) {
      progress(export_sum(completed_reads, done), total_reads);
    };
  ExecutionScope scope(range_control);
  kernel_checkpoint();
  auto layout = range_reslice_layout(source, request);
  const auto reads_per_plane = request.plane == Plane::XY
      ? std::size_t(1) : std::size_t(source.shape().z);
  total_reads = export_product(std::size_t(layout.shape.z), reads_per_plane);
  if (progress) progress(0, total_reads);
  kernel_checkpoint();
  Stack planes;
  planes.reserve(std::size_t(layout.shape.z));
  kernel_checkpoint();
  for (int k = 0; k < layout.shape.z; ++k) {
    kernel_checkpoint();
    // A positive interval bounds this value by last, even for INT_MAX step.
    const auto coordinate = int(std::int64_t(request.first) +
                                std::int64_t(k) * request.step);
    planes.push_back(source.extract_plane(request.plane, coordinate, request.t, request.c));
    kernel_checkpoint();
    completed_reads = export_sum(completed_reads, reads_per_plane);
    if (progress) progress(completed_reads, total_reads);
    kernel_checkpoint();
  }
  auto output = Dataset::from_planes(layout.shape, std::move(planes),
                                    layout.calibration, source.sample_policy());
  kernel_checkpoint();
  return {std::move(output), std::move(layout.geometry), request, layout.working_bytes};
}

AxisRangeOrientationResult reslice_oriented_axis_range(
    const Dataset &source_input, const AxisRangeOrientationRequest &request_input,
    const ExecutionControl &control) {
  const auto source = source_input;
  const auto request = request_input;
  std::size_t completed = 0, total = 0;
  auto orientation_control = control;
  const auto progress = control.progress;
  if (progress)
    orientation_control.progress = [&](std::size_t done, std::size_t) {
      progress(export_sum(completed, done), total);
    };
  ExecutionScope scope(orientation_control);
  kernel_checkpoint();
  auto layout = range_orientation_layout(source, request);
  const auto &range = request.range;
  const auto reads_per_plane = range.plane == Plane::XY
      ? std::size_t(1) : std::size_t(source.shape().z);
  // Reserve the last unit for immutable sample validation. Completion cannot
  // be reported while validation can still reject or observe cancellation.
  total = export_sum(export_product(std::size_t(layout.shape.z),
                                    export_sum(reads_per_plane, 1)), 1);
  if (progress) progress(0, total);
  kernel_checkpoint();
  Stack planes;
  planes.reserve(std::size_t(layout.shape.z));
  const auto last_selected = int(std::int64_t(range.first) +
      std::int64_t(layout.shape.z - 1) * range.step);
  for (int k = 0; k < layout.shape.z; ++k) {
    kernel_checkpoint();
    const auto coordinate = int(request.reverse_order
        ? std::int64_t(last_selected) - std::int64_t(k) * range.step
        : std::int64_t(range.first) + std::int64_t(k) * range.step);
    auto input = source.extract_plane(range.plane, coordinate, range.t, range.c);
    completed = export_sum(completed, reads_per_plane);
    if (progress) progress(completed, total);
    kernel_checkpoint();
    Image output(layout.shape.x, layout.shape.y, layout.shape.s, source.pixel_type());
    kernel_checkpoint();
    kernel_blocks("axis_range_orientation_copy", output.pixels.size(),
        [&](std::size_t first, std::size_t last) {
          if (!request.flip_vertical && !request.transpose) {
            std::copy(input.pixels.begin() + first, input.pixels.begin() + last,
                      output.pixels.begin() + first);
            return;
          }
          for (auto i = first; i < last; ++i) {
            const auto s = i % std::size_t(layout.shape.s);
            const auto pixel = i / std::size_t(layout.shape.s);
            const auto u = pixel % std::size_t(layout.shape.x);
            const auto v = pixel / std::size_t(layout.shape.x);
            const auto input_u = request.transpose ? v : u;
            auto input_v = request.transpose ? u : v;
            if (request.flip_vertical)
              input_v = std::size_t(input.height - 1) - input_v;
            output.pixels[i] = input.pixels[(input_v * std::size_t(input.width) +
                                             input_u) * std::size_t(layout.shape.s) + s];
          }
        });
    kernel_checkpoint();
    planes.push_back(std::move(output));
    completed = export_sum(completed, 1);
    if (progress) progress(completed, total);
    kernel_checkpoint();
  }
  auto output = Dataset::from_planes(layout.shape, std::move(planes),
                                    layout.calibration, source.sample_policy());
  kernel_checkpoint();
  if (progress) progress(total, total);
  kernel_checkpoint();
  return {std::move(output), std::move(layout.geometry), request, layout.working_bytes};
}

AxisRangeSpacingEstimate estimate_axis_range_spacing(
    const Dataset &source_input, const AxisRangeSpacingRequest &request_input) {
  const auto source = source_input;
  const auto request = request_input;
  return range_spacing_layout(source, request).estimate;
}
AxisSpacingLayout describe_axis_spacing(DatasetShape shape, PixelType type,
    const SpatialCalibration &calibration, FloatingSamplePolicy policy,
    const AxisRangeSpacingRequest &request) {
  auto described = range_spacing_layout(shape, type, calibration, policy, request);
  return {std::move(described.output), std::move(described.estimate)};
}

AxisRangeSpacingResult reslice_spaced_axis_range(
    const Dataset &source_input, const AxisRangeSpacingRequest &request_input,
    const ExecutionControl &control) {
  const auto source = source_input;
  const auto request = request_input;
  auto layout = range_spacing_layout(source, request);
  const auto &orientation = request.orientation;
  const auto &range = orientation.range;
  const auto pixel_type = source.pixel_type();
  const auto sample_policy = source.sample_policy();
  const auto progress = control.progress;
  auto spacing_control = control;
  // Orthogonal extraction has its own per-Z read progress. The new public
  // progress is in output planes plus one immutable-validation reservation.
  spacing_control.progress = {};
  if (progress && !spacing_control.cancellation && !spacing_control.checkpoint &&
      !spacing_control.stage_progress)
    spacing_control.checkpoint = [] {};
  ExecutionScope scope(spacing_control);
  kernel_checkpoint();
  const auto total = export_sum(std::size_t(layout.estimate.plane_count), 1);
  if (progress) progress(0, total);
  kernel_checkpoint();
  Stack planes;
  kernel_checkpoint();
  planes.reserve(std::size_t(layout.estimate.plane_count));
  kernel_checkpoint();
  for (int k = 0; k < layout.estimate.plane_count; ++k) {
    const auto coordinate = int(orientation.reverse_order
        ? std::int64_t(layout.estimate.last_selected) - std::int64_t(k) * range.step
        : std::int64_t(range.first) + std::int64_t(k) * range.step);
    kernel_checkpoint();
    auto input = source.extract_plane(range.plane, coordinate, range.t, range.c);
    kernel_checkpoint();
    Image output(layout.output.shape.x, layout.output.shape.y,
                 layout.output.shape.s, pixel_type);
    kernel_checkpoint();
    kernel_blocks("axis_range_spacing_sample", output.pixels.size(),
        [&](std::size_t first, std::size_t last) {
          for (auto i = first; i < last; ++i) {
            const auto sample = i % std::size_t(layout.output.shape.s);
            const auto pixel = i / std::size_t(layout.output.shape.s);
            const auto u = pixel % std::size_t(layout.output.shape.x);
            const auto v = pixel / std::size_t(layout.output.shape.x);
            const auto a = orientation.transpose ? v : u;
            const auto j = orientation.transpose ? u : v;
            volatile double coordinate_value = double(j) * layout.estimate.voxel_step;
            double z = std::clamp(double(coordinate_value), 0.0,
                                   double(layout.source_vertical_count - 1));
            if (orientation.flip_vertical) {
              volatile double flipped = double(layout.source_vertical_count - 1) - z;
              z = flipped;
            }
            if (request.sampling == AxisRangeSampling::Nearest) {
              const auto nearest = std::min(layout.source_vertical_count - 1,
                                             int(std::floor(z + 0.5)));
              output.pixels[i] = input.pixels[(std::size_t(nearest) * input.width + a) * input.channels + sample];
            } else {
              const auto low = int(std::floor(z));
              const auto high = std::min(low + 1, layout.source_vertical_count - 1);
              const auto base = (std::size_t(low) * input.width + a) * input.channels + sample;
              if (low == high) {
                output.pixels[i] = input.pixels[base];
              } else {
                const auto next = (std::size_t(high) * input.width + a) * input.channels + sample;
                output.pixels[i] = spacing_linear_value(input.pixels[base], input.pixels[next],
                                                        z - double(low), pixel_type);
              }
            }
          }
        });
    kernel_checkpoint();
    planes.push_back(std::move(output));
    kernel_checkpoint();
    if (progress) progress(std::size_t(k) + 1, total);
    kernel_checkpoint();
  }
  // Moves transfer each pixel vector; no full oriented/typed stack is made.
  auto output = Dataset::from_planes(layout.output.shape, std::move(planes),
                                    layout.output.calibration, sample_policy);
  kernel_checkpoint();
  if (progress) progress(total, total);
  kernel_checkpoint();
  return {std::move(output), std::move(layout.output.geometry), request,
          layout.estimate.working_bytes, layout.estimate.source_vertical_spacing,
          layout.estimate.voxel_step, layout.estimate.vertical_count};
}

PreparedOperation prepare_imagej_intensity_fit(const DocumentSnapshot& source_input,
    const ImageJIntensityFitRequest& request_input,const ExecutionControl& control) {
  const auto source=source_input;
  (void)estimate_imagej_intensity_fit(source,request_input);
  const auto request=request_input; // freeze caller inputs before callbacks
  ExecutionScope scope(control);kernel_checkpoint();execution_progress(0,1);kernel_checkpoint();
  const auto type=source.data().dataset.pixel_type();
  const bool polynomial=request.function=="straight_line" || request.function=="poly2" || request.function=="poly3" || request.function=="poly4";
  const auto calibration=polynomial?ImageJIntensityCalibration::fit_polynomial(type,request.function,request.measured,request.known,request.value_unit,request.zero_clip):
      ImageJIntensityCalibration::fit_nonlinear(type,request.function,request.measured,request.known,request.value_unit,request.zero_clip,request.initial_coefficients,request.max_iterations,request.relative_tolerance);
  kernel_checkpoint();TargetRequest target_request;target_request.roi=RoiPolicy::WholePlane;
  auto target=resolve_target(source,target_request);
  auto edit=source.begin_edit(Change::All);edit.set_property(imagej_intensity_property,calibration.record());
  auto state=std::make_shared<PreparedOperation::State>(std::move(target),"imagej_intensity_fit",OperationParameters{},std::move(edit),source.display(),ExecutionOptions{},std::vector<PlaneExecutionOutput>{},execution_cancellation());
  execution_progress(1,1);kernel_checkpoint();return PreparedOperation(std::move(state));
}

PreparedOperation prepare_imagej_projection(
    const DocumentSnapshot& source, const ImageJProjectionDocumentRequest& request,
    const ExecutionControl& control) {
  return detail::CalibrationPreparation::prepare_imagej_projection(resolve_target(source), request, control);
}

PreparedOperation detail::CalibrationPreparation::prepare_imagej_projection(
    const ResolvedTarget& captured, const ImageJProjectionDocumentRequest& request_input,
    const ExecutionControl& control) {
  if(request_input.projection.signed16_density || !request_input.projection.calibration_table.empty())
    throw std::invalid_argument("Prepared projection density comes from the source document");
  const auto source=captured.source();
  const auto request=request_input;
  ExecutionScope scope(control);
  kernel_checkpoint();
  (void)detail::estimate_calibrated_projection(source,captured.calibration_context(),request);
  auto output=detail::project_imagej_document(source,request,{},captured.calibration_context());
  const auto& layout=output.layout;
  TargetRequest ranges;
  ranges.scope=TargetScope::Ranges;
  ranges.roi=RoiPolicy::WholePlane;
  ranges.destination=OutputDestination::NewDocument;
  const auto source_shape=source.data().dataset.shape();
  ranges.t=AxisRange{layout.axis==ImageJProjectionAxis::Time?layout.source_first:layout.source_first_t,
      layout.axis==ImageJProjectionAxis::Time?request.projection.last+1:layout.source_first_t+layout.shape.t,1};
  ranges.c=AxisRange{layout.axis==ImageJProjectionAxis::Channels?layout.source_first:layout.source_first_c,
      layout.axis==ImageJProjectionAxis::Channels?request.projection.last+1:layout.source_first_c+layout.shape.c,1};
  ranges.z=AxisRange{layout.axis==ImageJProjectionAxis::Z?layout.source_first:0,
      layout.axis==ImageJProjectionAxis::Z?request.projection.last+1:source_shape.z,1};
  auto target=CalibrationPreparation::target(captured,ranges);
  auto edit=source.begin_edit(Change::All);
  edit.replace_dataset(output.data.dataset);
  edit.set_title(output.data.metadata.title);
  edit.select(std::nullopt);edit.set_previous_selection(std::nullopt);
  edit.set_rois({});edit.set_overlays(std::move(output.data.overlays));
  edit.set_spectrum(std::nullopt);edit.set_record_layout({});
  for(const auto& item:source.data().metadata.properties) edit.remove_property(item.first);
  for(const auto& item:source.data().metadata.extensions) edit.remove_extension(item.first);
  for(const auto& item:output.data.metadata.properties) edit.set_property(item.first,item.second);
  edit.set_axis_remap_display(output.display);
  auto fresh=std::make_shared<Document>(edit.preview().data(),output.display);
  auto baseline=fresh->snapshot();
  OperationParameters parameters{{"method",request.projection.method}};
  auto state=std::make_shared<PreparedOperation::State>(std::move(target),
      "imagej_projection",std::move(parameters),std::move(edit),output.display,
      ExecutionOptions{},std::vector<PlaneExecutionOutput>{},execution_cancellation());
  state->fresh_document=std::move(fresh);
  state->fresh_baseline=std::move(baseline);
  kernel_checkpoint();
  return PreparedOperation(std::move(state));
}

PreparedOperation prepare_axis_plane_export(
    const DocumentSnapshot& source, const AxisPlaneExportRequest& request,
    const ExecutionControl& control) {
  return detail::CalibrationPreparation::prepare_axis_plane_export(resolve_target(source), request, control);
}

PreparedOperation detail::CalibrationPreparation::prepare_axis_plane_export(
    const ResolvedTarget &captured, const AxisPlaneExportRequest &request_input,
    const ExecutionControl &control) {
  const auto source = captured.source();
  const auto dataset = source.data().dataset.with_calibration(captured.calibration());
  const auto request = request_input;
  ExecutionScope scope(control);
  kernel_checkpoint();
  // Resolve/admit metadata before a pixel read. Explicit ranges bind the
  // captured scientific T/C and either one XY Z or all contributing Z slices.
  auto layout = export_layout(dataset, request);
  TargetRequest target_request;
  target_request.scope = TargetScope::Ranges;
  target_request.roi = RoiPolicy::WholePlane;
  target_request.destination = OutputDestination::NewDocument;
  target_request.t = AxisRange{request.t, request.t + 1, 1};
  target_request.c = AxisRange{request.c, request.c + 1, 1};
  target_request.z = request.plane == Plane::XY
      ? AxisRange{request.coordinate, request.coordinate + 1, 1}
      : AxisRange{0, source.data().dataset.shape().z, 1};
  auto target = CalibrationPreparation::target(captured, target_request);
  auto exported = export_axis_plane(dataset, request, {});
  kernel_checkpoint();
  const auto title = source.data().metadata.title + " " + layout.plane_name + " " +
                     std::to_string(request.coordinate + 1);
  const std::array<std::string, 3> axis_names{"X", "Y", "Z"};
  auto provenance = metadata_object({
      {"schema_version", std::int64_t(1)}, {"source_document_id", source.id()},
      {"plane", layout.plane_name}, {"coordinate", std::int64_t(request.coordinate)},
      {"t", std::int64_t(request.t)}, {"c", std::int64_t(request.c)},
      {"horizontal_axis", axis_names[layout.source_axes[0]]},
      {"vertical_axis", axis_names[layout.source_axes[1]]},
      {"fixed_axis", axis_names[layout.source_axes[2]]},
      {"index_convention", std::string("zero_based_voxel_centers")},
  });
  auto edit = source.begin_edit(target.dependencies());
  edit.replace_dataset(exported.dataset);
  edit.set_title(title);
  edit.select(std::nullopt);
  edit.set_previous_selection(std::nullopt);
  edit.set_rois({});
  edit.set_overlays({});
  edit.set_spectrum(std::nullopt);
  edit.set_record_layout({});
  for (const auto &[name, value] : source.data().metadata.properties) {
    (void)value; edit.remove_property(name);
  }
  for (const auto &[name, value] : source.data().metadata.extensions) {
    (void)value; edit.remove_extension(name);
  }
  edit.set_extension("axis_plane_export", std::move(provenance));
  apply_captured_output_density(edit, captured, edit.preview().data().dataset);
  auto fresh = std::make_shared<Document>(edit.preview().data());
  auto fresh_snapshot = fresh->snapshot();
  OperationParameters parameters{
      {"plane", layout.plane_name}, {"coordinate", std::int64_t(request.coordinate)},
      {"t", std::int64_t(request.t)}, {"c", std::int64_t(request.c)},
      {"max_working_bytes", std::uint64_t(request.max_working_bytes)},
      {"working_bytes", std::uint64_t(exported.working_bytes)},
  };
  auto state = std::make_shared<PreparedOperation::State>(
      std::move(target), "axis_plane_export", std::move(parameters), std::move(edit),
      fresh_snapshot.display(), ExecutionOptions{}, std::vector<PlaneExecutionOutput>{},
      execution_cancellation());
  state->fresh_document = std::move(fresh);
  state->fresh_baseline = std::move(fresh_snapshot);
  kernel_checkpoint();
  return PreparedOperation(std::move(state));
}

AxisRangeSlicerResizeEstimate estimate_axis_range_slicer_resize(
    const Dataset &source_input, const AxisRangeSlicerResizeRequest &request_input) {
  const auto source = source_input;
  const auto request = request_input;
  return range_slicer_resize_layout(source, request).estimate;
}

SlicerResizeLayoutDescriptor describe_slicer_resize(DatasetShape shape, PixelType type,
    const SpatialCalibration &calibration, FloatingSamplePolicy policy,
    const AxisRangeSlicerResizeRequest &request) {
  auto layout=range_slicer_resize_layout(shape,type,calibration,policy,request);
  return {std::move(layout.output),std::move(layout.estimate)};
}

AxisRangeSlicerResizeResult reslice_slicer_axis_range(
    const Dataset &source_input, const AxisRangeSlicerResizeRequest &request_input,
    const ExecutionControl &control) {
  const auto source = source_input;
  const auto request = request_input;
  auto layout = range_slicer_resize_layout(source, request);
  const auto &orientation = request.orientation;
  const auto &range = orientation.range;
  const auto type = source.pixel_type();
  const auto progress = control.progress;
  auto resize_control = control;
  // Extraction's Z-read progress stays private to its stages. Public progress
  // counts emitted planes plus one final immutable-storage validation unit.
  resize_control.progress = {};
  if (progress && !resize_control.cancellation && !resize_control.checkpoint &&
      !resize_control.stage_progress)
    resize_control.checkpoint = [] {};
  ExecutionScope scope(resize_control);
  kernel_checkpoint();
  const auto total = export_sum(std::size_t(layout.estimate.plane_count), 1);
  if (progress) progress(0, total);
  kernel_checkpoint();
  Stack planes;
  planes.reserve(std::size_t(layout.estimate.plane_count));
  kernel_checkpoint();
  for (int k = 0; k < layout.estimate.plane_count; ++k) {
    const int fixed = int(orientation.reverse_order ?
        std::int64_t(layout.estimate.last_selected) - std::int64_t(k) * range.step :
        std::int64_t(range.first) + std::int64_t(k) * range.step);
    kernel_checkpoint();
    // Exactly one Q=A*Nz extraction. Flip and transpose are accessor mappings;
    // no second profile or coordinate vector is allocated.
    auto profile = source.extract_plane(range.plane, fixed, range.t, range.c);
    kernel_checkpoint();
    kernel_blocks("axis_range_slicer_resize_canonical", profile.pixels.size(),
        [&](std::size_t first, std::size_t last) {
          for (auto i = first; i < last; ++i)
            slicer_validate_canonical(profile.pixels[i], type);
        });
    kernel_checkpoint();
    Image output(layout.output.shape.x, layout.output.shape.y, 1, type);
    kernel_checkpoint();
    const int processor_width = orientation.transpose ? profile.height : profile.width;
    const int processor_height = orientation.transpose ? profile.width : profile.height;
    kernel_blocks("axis_range_slicer_resize_sample", output.pixels.size(),
        [&](std::size_t first, std::size_t last) {
          for (auto i = first; i < last; ++i) {
            const int x = int(i % std::size_t(output.width));
            const int y = int(i / std::size_t(output.width));
            if (layout.estimate.resize_branch != AxisRangeSlicerResizeBranch::Bilinear) {
              const int a = orientation.transpose ? y : x;
              const int row = orientation.transpose ? x : y;
              const int z = orientation.flip_vertical ? profile.height - 1 - row : row;
              output.pixels[i] = profile.pixels[std::size_t(z) * profile.width + a];
            } else {
              const double px = slicer_axis_coordinate(processor_width, output.width, x).sampled;
              const double py = slicer_axis_coordinate(processor_height, output.height, y).sampled;
              output.pixels[i] = slicer_bilinear_sample(profile, orientation.flip_vertical,
                                                      orientation.transpose, px, py, type);
            }
          }
        });
    kernel_checkpoint();
    planes.push_back(std::move(output));
    kernel_checkpoint();
    if (progress) progress(std::size_t(k) + 1, total);
    kernel_checkpoint();
  }
  auto output = Dataset::from_planes(layout.output.shape, std::move(planes),
                                    layout.output.calibration, FloatingSamplePolicy::FiniteOnly);
  kernel_checkpoint();
  if (progress) progress(total, total);
  kernel_checkpoint();
  return {std::move(output), std::move(layout.output.geometry), request,
      layout.estimate.working_bytes, layout.estimate.source_vertical_count,
      layout.estimate.vertical_count, layout.estimate.z_spacing_ratio,
      layout.estimate.processor_vertical_origin, layout.estimate.processor_vertical_step,
      layout.estimate.resize_branch, layout.estimate.scalar_spacing};
}

PreparedOperation prepare_axis_range_reslice(
    const DocumentSnapshot& source, const AxisRangeResliceRequest& request,
    const ExecutionControl& control) {
  return detail::CalibrationPreparation::prepare_axis_range_reslice(resolve_target(source), request, control);
}

PreparedOperation detail::CalibrationPreparation::prepare_axis_range_reslice(
    const ResolvedTarget &captured, const AxisRangeResliceRequest &request_input,
    const ExecutionControl &control) {
  const auto source = captured.source();
  const auto dataset = source.data().dataset.with_calibration(captured.calibration());
  const auto request = request_input;
  auto layout = range_reslice_layout(dataset, request);
  TargetRequest target_request;
  target_request.scope = TargetScope::Ranges;
  target_request.roi = RoiPolicy::WholePlane;
  target_request.destination = OutputDestination::NewDocument;
  target_request.t = AxisRange{request.t, request.t + 1, 1};
  target_request.c = AxisRange{request.c, request.c + 1, 1};
  target_request.z = request.plane == Plane::XY
      ? AxisRange{request.first, request.last + 1, request.step}
      : AxisRange{0, source.data().dataset.shape().z, 1};
  auto target = CalibrationPreparation::target(captured, target_request);
  // Keep one controlled extraction scope so local orthogonal read checkpoints
  // report offsets within the checked complete emitted-stack work total.
  auto resliced = reslice_axis_range(dataset, request, control);
  ExecutionScope scope(control);
  kernel_checkpoint();
  const auto title = source.data().metadata.title + " " + layout.plane_name + " " +
      std::to_string(request.first + 1) + "-" + std::to_string(request.last + 1) +
      " step " + std::to_string(request.step);
  const std::array<std::string, 3> axis_names{"X", "Y", "Z"};
  auto provenance = metadata_object({
      {"schema_version", std::int64_t(1)}, {"source_document_id", source.id()},
      {"plane", layout.plane_name}, {"first", std::int64_t(request.first)},
      {"last", std::int64_t(request.last)}, {"step", std::int64_t(request.step)},
      {"count", std::int64_t(layout.shape.z)},
      {"t", std::int64_t(request.t)}, {"c", std::int64_t(request.c)},
      {"horizontal_axis", axis_names[layout.source_axes[0]]},
      {"vertical_axis", axis_names[layout.source_axes[1]]},
      {"fixed_axis", axis_names[layout.source_axes[2]]},
      {"index_convention", std::string("zero_based_voxel_centers")},
  });
  auto edit = source.begin_edit(target.dependencies());
  edit.replace_dataset(resliced.dataset);
  edit.set_title(title);
  edit.select(std::nullopt);
  edit.set_previous_selection(std::nullopt);
  edit.set_rois({});
  edit.set_overlays({});
  edit.set_spectrum(std::nullopt);
  edit.set_record_layout({});
  for (const auto &[name, value] : source.data().metadata.properties) {
    (void)value; edit.remove_property(name);
  }
  for (const auto &[name, value] : source.data().metadata.extensions) {
    (void)value; edit.remove_extension(name);
  }
  edit.set_extension("axis_range_reslice", std::move(provenance));
  apply_captured_output_density(edit, captured, edit.preview().data().dataset);
  auto fresh = std::make_shared<Document>(edit.preview().data());
  auto fresh_snapshot = fresh->snapshot();
  OperationParameters parameters{
      {"plane", layout.plane_name}, {"first", std::int64_t(request.first)},
      {"last", std::int64_t(request.last)}, {"step", std::int64_t(request.step)},
      {"count", std::int64_t(layout.shape.z)},
      {"t", std::int64_t(request.t)}, {"c", std::int64_t(request.c)},
      {"max_working_bytes", std::uint64_t(request.max_working_bytes)},
      {"working_bytes", std::uint64_t(resliced.working_bytes)},
  };
  auto state = std::make_shared<PreparedOperation::State>(
      std::move(target), "axis_range_reslice", std::move(parameters), std::move(edit),
      fresh_snapshot.display(), ExecutionOptions{}, std::vector<PlaneExecutionOutput>{},
      execution_cancellation());
  state->fresh_document = std::move(fresh);
  state->fresh_baseline = std::move(fresh_snapshot);
  kernel_checkpoint();
  return PreparedOperation(std::move(state));
}

PreparedOperation prepare_axis_range_orientation(
    const DocumentSnapshot& source, const AxisRangeOrientationRequest& request,
    const ExecutionControl& control) {
  return detail::CalibrationPreparation::prepare_axis_range_orientation(resolve_target(source), request, control);
}

PreparedOperation detail::CalibrationPreparation::prepare_axis_range_orientation(
    const ResolvedTarget &captured,
    const AxisRangeOrientationRequest &request_input,
    const ExecutionControl &control) {
  const auto source = captured.source();
  const auto dataset = source.data().dataset.with_calibration(captured.calibration());
  const auto request = request_input;
  const auto &range = request.range;
  auto layout = range_orientation_layout(dataset, request);
  TargetRequest target_request;
  target_request.scope = TargetScope::Ranges;
  target_request.roi = RoiPolicy::WholePlane;
  target_request.destination = OutputDestination::NewDocument;
  target_request.t = AxisRange{range.t, range.t + 1, 1};
  target_request.c = AxisRange{range.c, range.c + 1, 1};
  target_request.z = range.plane == Plane::XY
      ? AxisRange{range.first, range.last + 1, range.step}
      : AxisRange{0, source.data().dataset.shape().z, 1};
  auto target = CalibrationPreparation::target(captured, target_request);
  auto oriented = reslice_oriented_axis_range(dataset, request, control);
  ExecutionScope scope(control);
  kernel_checkpoint();
  const auto last_selected = int(std::int64_t(range.first) +
      std::int64_t(layout.shape.z - 1) * range.step);
  const auto title = source.data().metadata.title + " " + layout.plane_name + " " +
      std::to_string(range.first + 1) + "-" + std::to_string(range.last + 1) +
      " step " + std::to_string(range.step) + " [R" +
      std::to_string(int(request.reverse_order)) + " F" +
      std::to_string(int(request.flip_vertical)) + " T" +
      std::to_string(int(request.transpose)) + "]";
  const std::array<std::string, 3> axis_names{"X", "Y", "Z"};
  auto provenance = metadata_object({
      {"schema_version", std::int64_t(1)}, {"source_document_id", source.id()},
      {"plane", layout.plane_name}, {"first", std::int64_t(range.first)},
      {"last", std::int64_t(range.last)}, {"step", std::int64_t(range.step)},
      {"last_selected", std::int64_t(last_selected)},
      {"count", std::int64_t(layout.shape.z)},
      {"t", std::int64_t(range.t)}, {"c", std::int64_t(range.c)},
      {"reverse_order", request.reverse_order},
      {"flip_vertical", request.flip_vertical}, {"transpose", request.transpose},
      {"horizontal_axis", axis_names[layout.source_axes[request.transpose ? 1 : 0]]},
      {"vertical_axis", axis_names[layout.source_axes[request.transpose ? 0 : 1]]},
      {"fixed_axis", axis_names[layout.source_axes[2]]},
      {"index_convention", std::string("zero_based_voxel_centers")},
  });
  auto edit = source.begin_edit(target.dependencies());
  edit.replace_dataset(oriented.dataset);
  edit.set_title(title);
  edit.select(std::nullopt);
  edit.set_previous_selection(std::nullopt);
  edit.set_rois({});
  edit.set_overlays({});
  edit.set_spectrum(std::nullopt);
  edit.set_record_layout({});
  for (const auto &[name, value] : source.data().metadata.properties) {
    (void)value; edit.remove_property(name);
  }
  for (const auto &[name, value] : source.data().metadata.extensions) {
    (void)value; edit.remove_extension(name);
  }
  edit.set_extension("axis_range_orientation", std::move(provenance));
  apply_captured_output_density(edit, captured, edit.preview().data().dataset);
  auto fresh = std::make_shared<Document>(edit.preview().data());
  auto fresh_snapshot = fresh->snapshot();
  OperationParameters parameters{
      {"plane", layout.plane_name}, {"first", std::int64_t(range.first)},
      {"last", std::int64_t(range.last)}, {"step", std::int64_t(range.step)},
      {"last_selected", std::int64_t(last_selected)},
      {"count", std::int64_t(layout.shape.z)},
      {"t", std::int64_t(range.t)}, {"c", std::int64_t(range.c)},
      {"reverse_order", request.reverse_order},
      {"flip_vertical", request.flip_vertical}, {"transpose", request.transpose},
      {"max_working_bytes", std::uint64_t(range.max_working_bytes)},
      {"working_bytes", std::uint64_t(oriented.working_bytes)},
  };
  auto state = std::make_shared<PreparedOperation::State>(
      std::move(target), "axis_range_orientation", std::move(parameters), std::move(edit),
      fresh_snapshot.display(), ExecutionOptions{}, std::vector<PlaneExecutionOutput>{},
      execution_cancellation());
  state->fresh_document = std::move(fresh);
  state->fresh_baseline = std::move(fresh_snapshot);
  kernel_checkpoint();
  return PreparedOperation(std::move(state));
}

PreparedOperation prepare_axis_range_spacing(
    const DocumentSnapshot& source, const AxisRangeSpacingRequest& request,
    const ExecutionControl& control) {
  return detail::CalibrationPreparation::prepare_axis_range_spacing(resolve_target(source), request, control);
}

PreparedOperation detail::CalibrationPreparation::prepare_axis_range_spacing(
    const ResolvedTarget &captured,
    const AxisRangeSpacingRequest &request_input,
    const ExecutionControl &control) {
  const auto source = captured.source();
  const auto dataset = source.data().dataset.with_calibration(captured.calibration());
  const auto request = request_input;
  const auto layout = range_spacing_layout(dataset, request);
  const auto &orientation = request.orientation;
  const auto &range = orientation.range;
  TargetRequest target_request;
  target_request.scope = TargetScope::Ranges;
  target_request.roi = RoiPolicy::WholePlane;
  target_request.destination = OutputDestination::NewDocument;
  target_request.t = AxisRange{range.t, range.t + 1, 1};
  target_request.c = AxisRange{range.c, range.c + 1, 1};
  target_request.z = AxisRange{0, source.data().dataset.shape().z, 1};
  auto target = CalibrationPreparation::target(captured, target_request);
  auto spaced = reslice_spaced_axis_range(dataset, request, control);
  ExecutionScope scope(control);
  kernel_checkpoint();
  const auto sampling = std::string(request.sampling == AxisRangeSampling::Nearest ? "nearest" : "linear");
  const auto title = source.data().metadata.title + " " + layout.output.plane_name + " " +
      std::to_string(range.first + 1) + "-" + std::to_string(range.last + 1) +
      " step " + std::to_string(range.step) + " Z spacing " +
      spacing_title_value(request.physical_vertical_spacing) + " " + dataset.calibration().unit +
      " " + sampling + " [R" + std::to_string(int(orientation.reverse_order)) +
      " F" + std::to_string(int(orientation.flip_vertical)) +
      " T" + std::to_string(int(orientation.transpose)) + "]";
  auto provenance = metadata_object({
      {"schema_version", std::int64_t(1)}, {"source_document_id", source.id()},
      {"plane", layout.output.plane_name}, {"first", std::int64_t(range.first)},
      {"last", std::int64_t(range.last)}, {"step", std::int64_t(range.step)},
      {"last_selected", std::int64_t(layout.estimate.last_selected)},
      {"count", std::int64_t(layout.estimate.plane_count)},
      {"t", std::int64_t(range.t)}, {"c", std::int64_t(range.c)},
      {"reverse_order", orientation.reverse_order}, {"flip_vertical", orientation.flip_vertical},
      {"transpose", orientation.transpose}, {"sampling", sampling},
      {"physical_vertical_spacing", request.physical_vertical_spacing},
      {"source_vertical_spacing", layout.estimate.source_vertical_spacing},
      {"voxel_step", layout.estimate.voxel_step},
      {"vertical_count", std::int64_t(layout.estimate.vertical_count)},
      {"grid_policy", std::string("source_endpoint_centers_physical_z")},
      {"border_policy", std::string("clamp_endpoint_roundoff")},
      {"quantization_policy", std::string("stored_double_copy_else_declared_type_linear")},
      {"horizontal_axis", layout.estimate.output_axes[0]},
      {"vertical_axis", layout.estimate.output_axes[1]},
      {"fixed_axis", layout.estimate.output_axes[2]},
      {"index_convention", std::string("zero_based_voxel_centers")},
  });
  auto edit = source.begin_edit(target.dependencies());
  edit.replace_dataset(spaced.dataset);
  edit.set_title(title);
  edit.select(std::nullopt);
  edit.set_previous_selection(std::nullopt);
  edit.set_rois({});
  edit.set_overlays({});
  edit.set_spectrum(std::nullopt);
  edit.set_record_layout({});
  for (const auto &[name, value] : source.data().metadata.properties) {
    (void)value; edit.remove_property(name);
  }
  for (const auto &[name, value] : source.data().metadata.extensions) {
    (void)value; edit.remove_extension(name);
  }
  edit.set_extension("axis_range_spacing", std::move(provenance));
  kernel_checkpoint();
  apply_captured_output_density(edit, captured, edit.preview().data().dataset);
  auto fresh = std::make_shared<Document>(edit.preview().data());
  auto fresh_snapshot = fresh->snapshot();
  OperationParameters parameters{
      {"plane", layout.output.plane_name}, {"first", std::int64_t(range.first)},
      {"last", std::int64_t(range.last)}, {"step", std::int64_t(range.step)},
      {"last_selected", std::int64_t(layout.estimate.last_selected)},
      {"count", std::int64_t(layout.estimate.plane_count)},
      {"effective_first", std::int64_t(layout.estimate.effective_first)},
      {"effective_last", std::int64_t(layout.estimate.effective_last)},
      {"t", std::int64_t(range.t)}, {"c", std::int64_t(range.c)},
      {"reverse_order", orientation.reverse_order}, {"flip_vertical", orientation.flip_vertical},
      {"transpose", orientation.transpose}, {"sampling", sampling},
      {"physical_vertical_spacing", request.physical_vertical_spacing},
      {"source_vertical_spacing", layout.estimate.source_vertical_spacing},
      {"voxel_step", layout.estimate.voxel_step},
      {"source_vertical_count", std::int64_t(layout.source_vertical_count)},
      {"vertical_count", std::int64_t(layout.estimate.vertical_count)},
      {"max_working_bytes", std::uint64_t(range.max_working_bytes)},
      {"working_bytes", std::uint64_t(spaced.working_bytes)},
      {"sizeof_image", std::uint64_t(sizeof(Image))},
      {"sizeof_shared_image", std::uint64_t(sizeof(std::shared_ptr<const Image>))},
      {"allocation_allowance_bytes", std::uint64_t(256)},
      {"image_object_bytes", std::uint64_t(sizeof(Image))},
      {"shared_const_image_pointer_bytes", std::uint64_t(sizeof(std::shared_ptr<const Image>))},
      {"per_plane_control_allowance_bytes", std::uint64_t(256)},
      {"pixel_bytes", std::uint64_t(layout.pixel_bytes)},
      {"object_bytes", std::uint64_t(layout.object_bytes)},
  };
  auto state = std::make_shared<PreparedOperation::State>(
      std::move(target), "axis_range_spacing", std::move(parameters), std::move(edit),
      fresh_snapshot.display(), ExecutionOptions{}, std::vector<PlaneExecutionOutput>{},
      execution_cancellation());
  state->fresh_document = std::move(fresh);
  state->fresh_baseline = std::move(fresh_snapshot);
  kernel_checkpoint();
  return PreparedOperation(std::move(state));
}

PreparedOperation prepare_axis_range_slicer_resize(
    const DocumentSnapshot& source, const AxisRangeSlicerResizeRequest& request,
    const ExecutionControl& control) {
  return detail::CalibrationPreparation::prepare_axis_range_slicer_resize(resolve_target(source), request, control);
}

PreparedOperation detail::CalibrationPreparation::prepare_axis_range_slicer_resize(
    const ResolvedTarget &captured,
    const AxisRangeSlicerResizeRequest &request_input,
    const ExecutionControl &control) {
  const auto source = captured.source();
  const auto dataset = source.data().dataset.with_calibration(captured.calibration());
  const auto request = request_input;
  const auto layout = range_slicer_resize_layout(dataset, request);
  const auto &orientation = request.orientation;
  const auto &range = orientation.range;
  const auto &estimate = layout.estimate;
  TargetRequest target_request;
  target_request.scope = TargetScope::Ranges;
  target_request.roi = RoiPolicy::WholePlane;
  target_request.destination = OutputDestination::NewDocument;
  target_request.t = AxisRange{range.t, range.t + 1, 1};
  target_request.c = AxisRange{range.c, range.c + 1, 1};
  target_request.z = AxisRange{0, source.data().dataset.shape().z, 1};
  auto target = CalibrationPreparation::target(captured, target_request);
  auto resized = reslice_slicer_axis_range(dataset, request, control);
  ExecutionScope scope(control);
  kernel_checkpoint();
  const std::string branch = slicer_branch_name(estimate.resize_branch);
  const std::string geometry_policy = "nominal_unclamped_processor_sampling_affine";
  const std::string scalar_policy = "imagej_slicer_positive_scalar_spacing_public_stride_copy";
  const std::string sampling = "imagej_bilinear_source_order";
  const std::string border = "both_axes_last_center_minus_0_001";
  const std::string quantization = "canonical_typed_bilinear_or_exact_crop";
  const auto title = source.data().metadata.title + " " + layout.output.plane_name + " " +
      std::to_string(range.first + 1) + "-" + std::to_string(range.last + 1) +
      " step " + std::to_string(range.step) + " ImageJ bilinear [R" +
      std::to_string(int(orientation.reverse_order)) + " F" +
      std::to_string(int(orientation.flip_vertical)) + " T" +
      std::to_string(int(orientation.transpose)) + "]";
  auto provenance = metadata_object({
      {"schema_version", std::int64_t(1)}, {"source_document_id", source.id()},
      {"plane", layout.output.plane_name}, {"first", std::int64_t(range.first)},
      {"last", std::int64_t(range.last)}, {"step", std::int64_t(range.step)},
      {"count", std::int64_t(estimate.plane_count)},
      {"last_selected", std::int64_t(estimate.last_selected)},
      {"t", std::int64_t(range.t)}, {"c", std::int64_t(range.c)},
      {"reverse_order", orientation.reverse_order}, {"flip_vertical", orientation.flip_vertical},
      {"transpose", orientation.transpose}, {"horizontal_axis", estimate.output_axes[0]},
      {"vertical_axis", estimate.output_axes[1]}, {"fixed_axis", estimate.output_axes[2]},
      {"index_convention", std::string("zero_based_voxel_centers")},
      {"source_vertical_count", std::int64_t(estimate.source_vertical_count)},
      {"vertical_count", std::int64_t(estimate.vertical_count)},
      {"z_spacing_ratio", estimate.z_spacing_ratio},
      {"processor_vertical_origin", estimate.processor_vertical_origin},
      {"processor_vertical_step", estimate.processor_vertical_step},
      {"resize_branch", branch},
      {"scalar_spacing", metadata_array({estimate.scalar_spacing[0], estimate.scalar_spacing[1], estimate.scalar_spacing[2]})},
      {"geometry_policy", geometry_policy}, {"scalar_spacing_policy", scalar_policy},
      {"sampling", sampling}, {"border_policy", border}, {"quantization_policy", quantization},
  });
  auto edit = source.begin_edit(target.dependencies());
  edit.replace_dataset(resized.dataset);
  edit.set_title(title);
  edit.select(std::nullopt);
  edit.set_previous_selection(std::nullopt);
  edit.set_rois({});
  edit.set_overlays({});
  edit.set_spectrum(std::nullopt);
  edit.set_record_layout({});
  for (const auto &[name, value] : source.data().metadata.properties) {
    (void)value; edit.remove_property(name);
  }
  for (const auto &[name, value] : source.data().metadata.extensions) {
    (void)value; edit.remove_extension(name);
  }
  edit.set_extension("axis_range_slicer_resize", std::move(provenance));
  kernel_checkpoint();
  apply_captured_output_density(edit, captured, edit.preview().data().dataset);
  auto fresh = std::make_shared<Document>(edit.preview().data());
  auto fresh_snapshot = fresh->snapshot();
  OperationParameters parameters{
      {"plane", layout.output.plane_name}, {"first", std::int64_t(range.first)},
      {"last", std::int64_t(range.last)}, {"step", std::int64_t(range.step)},
      {"t", std::int64_t(range.t)}, {"c", std::int64_t(range.c)},
      {"reverse_order", orientation.reverse_order}, {"flip_vertical", orientation.flip_vertical},
      {"transpose", orientation.transpose}, {"count", std::int64_t(estimate.plane_count)},
      {"last_selected", std::int64_t(estimate.last_selected)},
      {"effective_first", std::int64_t(estimate.effective_first)},
      {"effective_last", std::int64_t(estimate.effective_last)},
      {"source_vertical_count", std::int64_t(estimate.source_vertical_count)},
      {"vertical_count", std::int64_t(estimate.vertical_count)},
      {"z_spacing_ratio", estimate.z_spacing_ratio},
      {"processor_vertical_origin", estimate.processor_vertical_origin},
      {"processor_vertical_step", estimate.processor_vertical_step},
      {"resize_branch", branch},
      {"scalar_spacing", NumericArray{{3}, {estimate.scalar_spacing[0], estimate.scalar_spacing[1], estimate.scalar_spacing[2]}}},
      {"geometry_policy", geometry_policy}, {"scalar_spacing_policy", scalar_policy},
      {"sampling", sampling}, {"border_policy", border}, {"quantization_policy", quantization},
      {"max_working_bytes", std::uint64_t(range.max_working_bytes)},
      {"working_bytes", std::uint64_t(resized.working_bytes)},
      {"pixel_bytes", std::uint64_t(layout.pixel_bytes)}, {"object_bytes", std::uint64_t(layout.object_bytes)},
      {"sizeof_image", std::uint64_t(sizeof(Image))},
      {"sizeof_shared_image", std::uint64_t(sizeof(std::shared_ptr<const Image>))},
      {"allocation_allowance_bytes", std::uint64_t(256)},
      {"image_object_bytes", std::uint64_t(sizeof(Image))},
      {"shared_const_image_pointer_bytes", std::uint64_t(sizeof(std::shared_ptr<const Image>))},
      {"per_plane_control_allowance_bytes", std::uint64_t(256)},
  };
  auto state = std::make_shared<PreparedOperation::State>(
      std::move(target), "axis_range_slicer_resize", std::move(parameters), std::move(edit),
      fresh_snapshot.display(), ExecutionOptions{}, std::vector<PlaneExecutionOutput>{},
      execution_cancellation());
  state->fresh_document = std::move(fresh);
  state->fresh_baseline = std::move(fresh_snapshot);
  kernel_checkpoint();
  return PreparedOperation(std::move(state));
}

OperationParameters validate_execution_parameters(
    const std::string &operation, const ResolvedTarget &target,
    const OperationParameters &parameters, ExecutionOptions options) {
  target.validate_operation(operation);
  if (target.execution_mode() != ExecutionMode::Planes)
    throw std::invalid_argument(
        "No volume executor is registered for this operation");
  const auto &spec = operation_spec(operation);
  if (options.dataset != DatasetPolicy::PreserveUntargeted &&
      options.dataset != DatasetPolicy::LegacyDesktop)
    throw std::invalid_argument("Unknown dataset execution policy");
  if (options.geometry != GeometryPolicy::RequireTransform &&
      options.geometry != GeometryPolicy::LegacyDesktop)
    throw std::invalid_argument("Unknown geometry execution policy");
  if (options.geometry == GeometryPolicy::RequireTransform &&
      spec.geometry != GeometryEffect::Preserve &&
      spec.geometry != GeometryEffect::Samples)
    throw std::invalid_argument(
        "Operation requires an explicit geometry policy: " + operation);
  const auto &source = target.source();
  auto shape = source.data().dataset.shape();
  if(source.data().dataset.sample_policy()==FloatingSamplePolicy::PreserveIEEEFloat32 &&
     operation!="remove_nans_imagej")
    throw std::invalid_argument("IEEE Float32 source processing requires a qualified operation profile");
  if(options.phase!=ExecutionPhase::Apply && options.phase!=ExecutionPhase::Preview)
    throw std::invalid_argument("Unknown operation execution phase");
  if(operation=="top_hat_imagej" && options.phase==ExecutionPhase::Preview &&
      (target.position_count()!=1 || target.destination()!=OutputDestination::InPlace))
    throw std::invalid_argument("Top Hat preview requires one in-place plane");
  const bool legacy = options.dataset == DatasetPolicy::LegacyDesktop;
  auto eager = source.data().dataset.shared_planes();
  if ((operation == "rank_filter_imagej" || operation == "remove_outliers_imagej" || operation == "top_hat_imagej") &&
      source.data().dataset.sample_policy() != FloatingSamplePolicy::FiniteOnly)
    throw std::invalid_argument("ImageJ rank filters require finite-only sources");
  if (operation == "convert_image" &&
      (target.position_count() != shape.plane_count() ||
       target.roi_policy() == RoiPolicy::PositionedRois ||
       (target.roi_policy() == RoiPolicy::ActiveSelection &&
        source.data().selection)))
    throw std::invalid_argument("Type conversion requires every dataset "
                                "position and a whole-plane target");
  if (operation == "convert_image" && !legacy)
    // A future channel-display record cannot be silently discarded by a type
    // transition. Admit the recognized source before acquiring any pixels.
    (void)recognized_channel_display(source.display(), source.data().dataset);
  if (legacy && (!eager || shape.t != 1 || shape.c != 1))
    throw std::invalid_argument(
        "Legacy desktop execution requires an eager Z stack");
  auto normalized=validate_operation(
      operation,
      OperationDefaultContext{
          shape.x, shape.y, shape.s, source.data().dataset.pixel_type(), {}},
      parameters);
  if(operation=="auto_mask" && std::get<bool>(normalized.at("stack_histogram"))) {
    const auto bins=std::get<std::int64_t>(normalized.at("bins"));
    if(shape.s!=1 || source.data().dataset.sample_policy()!=FloatingSamplePolicy::FiniteOnly || (bins!=0 && (bins<2 || bins>65536)))
      throw std::invalid_argument("Pooled threshold requires finite scalar planes and 2..65536 bins");
  }
  if(operation=="convert_image"){const auto &type=std::get<std::string>(normalized.at("dtype"));(void)detail::resolve_indexed_type_plan(source,type=="uint8"?PixelType::UInt8:type=="uint16"?PixelType::UInt16:type=="float32"?PixelType::Float32:PixelType::Float64);}
  if(operation=="rank_filter_imagej" && std::get<std::string>(normalized.at("method"))=="variance")
    (void)detail::validate_rank_variance_display_source(source);
  if(operation=="top_hat_imagej")(void)detail::validate_top_hat_display_source(source,std::get<bool>(normalized.at("dont_subtract")));
  (void)detail::validate_indexed_filter_eligibility(source,operation);
  return normalized;
}
PreparedOperation prepare_operation(const std::string &requested_operation,
                                    const ResolvedTarget &requested_target,
                                    const OperationParameters &parameters,
                                    ExecutionOptions options,
                                    const ExecutionControl &requested_control) {
  // Freeze execution handles before the first user callback. Normalization
  // owns the parameter values before callbacks can mutate caller-owned intent.
  const auto operation = requested_operation;
  const auto target = requested_target;
  const auto control = requested_control;
  ExecutionScope scope(control);
  auto normalized = detail::without_execution_callbacks([&] {
    kernel_checkpoint();
    auto values = validate_execution_parameters(operation, target, parameters, options);
    // Admit the complete frozen target, including later positioned masks,
    // before any provider read or user callback. Direct calls retain intrinsic
    // limits even when no explicit additional-mask byte cap was configured.
    (void)detail::target_mask_workspace(target, operation, options.max_target_mask_bytes);
    return values;
  });
  checkpoint(control);
  const auto &source = target.source();
  auto shape = source.data().dataset.shape();
  const auto source_type = source.data().dataset.pixel_type();
  const bool legacy = options.dataset == DatasetPolicy::LegacyDesktop;
  auto eager = source.data().dataset.shared_planes();
  const bool label_image =
      operation == "label" || operation == "binary_watershed";
  std::optional<detail::IndexedTypePlan> indexed_type;
  if(operation=="convert_image"){const auto &type=std::get<std::string>(normalized.at("dtype"));indexed_type=detail::resolve_indexed_type_plan(source,type=="uint8"?PixelType::UInt8:type=="uint16"?PixelType::UInt16:type=="float32"?PixelType::Float32:PixelType::Float64);}
  const bool return_num =
      operation == "label" && std::get<bool>(normalized.at("return_num"));
  std::map<Key, std::shared_ptr<const Image>> planes;
  std::vector<PlaneExecutionOutput> outputs;
  const bool covers_all = target.position_count() == shape.plane_count();
  bool full_masks = true;
  std::optional<DatasetShape> output_shape;
  std::optional<PixelType> result_type;
  std::size_t completed = 0;
  execution_progress(0, target.position_count());
  std::optional<double> pooled_level;
  if(operation=="auto_mask" && std::get<bool>(normalized.at("stack_histogram")))
    pooled_level=pooled_auto_threshold(target,std::get<std::string>(normalized.at("method")),
      int(std::get<std::int64_t>(normalized.at("bins"))),std::get<bool>(normalized.at("ignore_black")),
      std::get<bool>(normalized.at("ignore_white")),std::numeric_limits<std::size_t>::max()).threshold;
  std::optional<Image> fixed_mask;
  std::optional<GaussianBlurCommandRegion> fixed_gaussian_region;
  std::optional<RankFilterCommandRegion> fixed_rank_region;
  const bool imagej_gaussian = operation == "gaussian_blur_imagej";
  const bool remove_nans = operation == "remove_nans_imagej";
  const bool outliers = operation == "remove_outliers_imagej";
  const bool top_hat = operation == "top_hat_imagej";
  const bool top_hat_subtract = top_hat && !std::get<bool>(normalized.at("dont_subtract"));
  const bool imagej_rank = operation == "rank_filter_imagej" || outliers || top_hat || remove_nans;
  const bool variance = operation == "rank_filter_imagej" && std::get<std::string>(normalized.at("method")) == "variance";
  const bool nonbyte_variance = variance && source_type != PixelType::UInt8;
  const PlanePosition active_plane=top_hat && options.phase==ExecutionPhase::Preview
      ? target.position(0) : PlanePosition{source.display().t, source.display().c, source.display().crosshair[2]};
  auto top_hat_view=source.display();
  if(top_hat && options.phase==ExecutionPhase::Preview){top_hat_view.t=active_plane.t;top_hat_view.c=active_plane.c;top_hat_view.crosshair[2]=active_plane.z;}
  const auto top_hat_source=source.with_display(std::move(top_hat_view));
  auto initial_variance_range = nonbyte_variance
      ? detail::validate_rank_variance_display_source(source)
      : std::optional<std::array<double, 2>>{};
  auto variance_channels = nonbyte_variance
      ? recognized_channel_display(source.display(), source.data().dataset)
      : std::optional<ChannelDisplayState>{};
  auto top_hat_initial_range = top_hat
      ? detail::validate_top_hat_display_source(top_hat_source,!top_hat_subtract)
      : std::optional<std::array<double,2>>{};
  const auto top_hat_lane = target.position_count()==1 && target.contains(active_plane)
      ? detail::RankProcessorLane::LiveProcessor : detail::RankProcessorLane::DetachedPlanes;
  std::optional<std::array<double,2>> top_hat_setter_range;
  std::optional<detail::RankTopHatFinalDisplay> top_hat_preview_effects;
  if (target.roi_policy() != RoiPolicy::PositionedRois) {
    if (imagej_gaussian)
      fixed_gaussian_region = gaussian_command_region(target, 0);
    else if (imagej_rank)
      fixed_rank_region = remove_nans ? remove_nans_command_region(target, 0) : rank_command_region(target, 0);
    else
      fixed_mask = target.mask(0);
  }
  for (std::size_t i = 0; i < target.position_count(); ++i) {
    checkpoint(control);
    auto p = target.position(i);
    auto own_gaussian_region = imagej_gaussian && target.roi_policy() == RoiPolicy::PositionedRois
        ? std::optional<GaussianBlurCommandRegion>(gaussian_command_region(target, i))
        : std::optional<GaussianBlurCommandRegion>{};
    const auto *gaussian_region = own_gaussian_region ? &*own_gaussian_region
        : (fixed_gaussian_region ? &*fixed_gaussian_region : nullptr);
    auto own_rank_region = imagej_rank && target.roi_policy() == RoiPolicy::PositionedRois
        ? std::optional<RankFilterCommandRegion>(remove_nans ? remove_nans_command_region(target, i) : rank_command_region(target, i))
        : std::optional<RankFilterCommandRegion>{};
    const auto *rank_region = own_rank_region ? &*own_rank_region
        : (fixed_rank_region ? &*fixed_rank_region : nullptr);
    auto own_mask = !imagej_gaussian && !imagej_rank && target.roi_policy() == RoiPolicy::PositionedRois
                        ? target.mask(i)
                        : std::optional<Image>{};
    const Image *mask =
        gaussian_region ? (gaussian_region->mask ? &*gaussian_region->mask : nullptr)
        : rank_region ? (rank_region->mask ? &*rank_region->mask : nullptr)
        : (own_mask ? &*own_mask : (fixed_mask ? &*fixed_mask : nullptr));
    const bool mask_full =
        !mask || std::all_of(mask->pixels.begin(), mask->pixels.end(),
                             [](double v) { return v != 0; });
    full_masks = full_masks && mask_full;
    if ((!legacy || nonbyte_variance || top_hat || remove_nans) && mask &&
        std::all_of(mask->pixels.begin(), mask->pixels.end(),
                    [](double v) { return v == 0; })) {
      outputs.push_back({p, return_num ? std::optional<int>(0) : std::nullopt,
                         label_image, true});
    } else {
      // Empty write masks stay read-free. Capture an implicit source range
      // before the first actual filter, independently of the write target.
      if (nonbyte_variance && !initial_variance_range)
        initial_variance_range = effective_channel_display_range(source.data().dataset,
            {}, active_plane.t, active_plane.c, active_plane.z, {}, {}, control).range;
      if(top_hat_subtract && top_hat_lane==detail::RankProcessorLane::LiveProcessor && !top_hat_initial_range)
        top_hat_initial_range=effective_channel_display_range(source.data().dataset,
            {},active_plane.t,active_plane.c,active_plane.z,{}, {},control).range;
      auto input = source.data().dataset.plane(p);
      auto result = [&]() {
        if(pooled_level) {
          Image output(input->width,input->height,1,PixelType::UInt8);
          const bool bright=std::get<bool>(normalized.at("bright"));
          KernelStage stage("Pooled threshold mask",input->pixels.size());
          for(std::size_t index=0;index<input->pixels.size();++index) {
            stage.periodic(index);require_finite(input->pixels[index],"Pooled threshold mask sample");
            output.pixels[index]=(input->pixels[index]>*pooled_level)==bright?255:0;
          }stage.finish();return OperationResult{std::move(output),std::nullopt,false};
        }
        if (operation == "shadow_imagej") {
          std::optional<std::array<int,4>> extent;
          if (mask && !mask_full) {
            if (mask->width != input->width || mask->height != input->height || mask->channels != 1)
              throw std::invalid_argument("Shadow write mask does not match the source");
            int left=input->width,top=input->height,right=0,bottom=0;
            for (std::size_t index=0;index<mask->pixels.size();++index) {
              if ((index & 1023)==0) checkpoint(control);
              if (mask->pixels[index]==0) continue;
              const int x=static_cast<int>(index % input->width),y=static_cast<int>(index / input->width);
              left=std::min(left,x);top=std::min(top,y);
              right=std::max(right,x+1);bottom=std::max(bottom,y+1);
            }
            extent=right>left && bottom>top ? std::array<int,4>{left,top,right-left,bottom-top}
                                           : std::array<int,4>{0,0,0,0};
          }
          return OperationResult{shadow_imagej(*input,std::get<std::string>(normalized.at("direction")),extent),
                                 std::nullopt,false};
        }
        if(indexed_type)return detail::execute_indexed_type(*input,*indexed_type,normalized);
        if(top_hat) {
          auto filtered=detail::top_hat_pixels_imagej(*input,
              std::get<double>(normalized.at("radius")),std::get<bool>(normalized.at("light_background")),
              !top_hat_subtract,std::get<bool>(normalized.at("inverted_lut")),
              rank_region?rank_region->extent:std::nullopt,
              top_hat_lane==detail::RankProcessorLane::LiveProcessor?top_hat_initial_range:std::nullopt);
          if(p.t==active_plane.t && p.c==active_plane.c && p.z==active_plane.z)
            top_hat_setter_range=filtered.subtraction_range;
          if(top_hat_subtract && options.phase==ExecutionPhase::Preview)
            top_hat_preview_effects=detail::top_hat_final_display_imagej(
                Dataset::from_stack(Stack{filtered.image}),{},false,
                detail::RankProcessorLane::LiveProcessor,filtered.subtraction_range,control);
          return OperationResult{std::move(filtered.image),std::nullopt,false};
        }
        if (remove_nans)
          return OperationResult{remove_nans_imagej(*input,
              std::get<double>(normalized.at("radius")),
              rank_region ? rank_region->extent : std::nullopt), std::nullopt, false};
        if (outliers)
          return OperationResult{remove_outliers_imagej(*input,
              std::get<double>(normalized.at("radius")), std::get<double>(normalized.at("threshold")),
              std::get<std::string>(normalized.at("which")), std::get<bool>(normalized.at("inverted_lut")),
              rank_region ? rank_region->extent : std::nullopt), std::nullopt, false};
        if (imagej_rank)
          return OperationResult{rank_filter_imagej(*input,
              parse_rank_filter_method(std::get<std::string>(normalized.at("method"))),
              std::get<double>(normalized.at("radius")),
              rank_region ? rank_region->extent : std::nullopt), std::nullopt, false};
        if (!imagej_gaussian)
          return execute_normalized_operation(operation, *input, normalized);
        const auto &sigma = std::get<NumericArray>(normalized.at("sigma")).values;
        return OperationResult{gaussian_blur_imagej(*input, sigma[1], sigma[0],
                                gaussian_region ? gaussian_region->extent : std::nullopt),
                               std::nullopt, false};
      }();
      checkpoint(control);
      if (result_type && *result_type != result.image.type)
        throw std::invalid_argument(
            "Operation output types differ across positions");
      result_type = result.image.type;
      auto produced = shape;
      produced.x = result.image.width;
      produced.y = result.image.height;
      produced.s = result.image.channels;
      if (output_shape && output_shape->dimensions() != produced.dimensions())
        throw std::invalid_argument(
            "Operation output shapes differ across positions");
      output_shape = produced;
      if (mask && (legacy || !mask_full)) {
        require_same_shape(*input, result.image);
        if (result.labelCount) {
          std::set<double> labels;
          for (std::size_t pixel = 0; pixel < mask->pixels.size(); ++pixel)
            if (mask->pixels[pixel] != 0 && result.image.pixels[pixel] > 0)
              labels.insert(result.image.pixels[pixel]);
          result.labelCount = int(labels.size());
        }
        if (legacy && !remove_nans)
          result.image =
              paste(canonical_image(convert(*input, result.image.type)),
                    result.image, 0, 0, mask);
        else {
          result.image.type = common_type(source_type, result.image.type);
          for (std::size_t pixel = 0; pixel < mask->pixels.size(); ++pixel)
            if (mask->pixels[pixel] == 0)
              for (int s = 0; s < input->channels; ++s)
                result.image.pixels[pixel * input->channels + s] =
                    input->pixels[pixel * input->channels + s];
        }
      }
      outputs.push_back({p, result.labelCount, result.labelImage, false});
      // Rank's finite Float32 profile preserves source-observed signed-zero
      // ties. A zero-sign-only change is a pixel edit, not an equal-result no-op.
      const bool changed_samples = imagej_rank
          ? result.image.pixels.size() != input->pixels.size() ||
                std::memcmp(result.image.pixels.data(), input->pixels.data(),
                            input->pixels.size() * sizeof(double)) != 0
          : result.image.pixels != input->pixels;
      if ((legacy && !remove_nans) || result.image.width != input->width ||
          result.image.height != input->height ||
          result.image.channels != input->channels ||
          result.image.type != input->type ||
          changed_samples)
        planes.emplace(key(p),
                       std::make_shared<const Image>(std::move(result.image)));
    }
    ++completed;
    execution_progress(completed, target.position_count());
    checkpoint(control);
  }
  checkpoint(control);
  auto dataset = source.data().dataset;
  bool spatial_changed = false;
  if (output_shape && (!remove_nans || !planes.empty())) {
    const auto produced = *output_shape;
    spatial_changed = produced.x != shape.x || produced.y != shape.y;
    const bool changed_extent = spatial_changed || produced.s != shape.s;
    if (changed_extent && (!covers_all || !full_masks))
      throw std::invalid_argument(
          "Shape/sample changes require every plane and whole-plane coverage");
    if (spatial_changed && options.geometry == GeometryPolicy::RequireTransform)
      throw std::invalid_argument(
          "Operation changed spatial extent without a geometry policy");
    PixelType type = *result_type;
    if (!legacy && (!covers_all || !full_masks))
      type = common_type(source_type, type);
    if (legacy && !target.contains({0, 0, source.display().crosshair[2]}))
      type = source_type;
    for (auto &[p, image] : planes) {
      (void)p;
      if (image->type != type) {
        auto copy = *image;
        if (legacy)
          copy = canonical_image(convert(copy, type));
        else
          copy.type = type;
        image = std::make_shared<const Image>(std::move(copy));
      }
    }
    if (legacy) {
      auto table = *eager;
      for (auto &[p, image] : planes)
        table.replace_shared(index(shape, position(p)), image);
      for (std::size_t i = 0; i < table.size(); ++i)
        if (table[i].type != type)
          table.replace(i, canonical_image(convert(table[i], type)));
      dataset = Dataset::from_shared_planes(produced, std::move(table),
                                            dataset.calibration());
    } else if (changed_extent ||
               (covers_all && full_masks && type != source_type)) {
      Stack stack;
      stack.reserve(shape.plane_count());
      for (int t = 0; t < shape.t; ++t)
        for (int c = 0; c < shape.c; ++c)
          for (int z = 0; z < shape.z; ++z)
            stack.push_back(*planes.at({t, c, z}));
      dataset = Dataset::from_planes(produced, std::move(stack),
                                     dataset.calibration());
    } else if (type != source_type) {
      dataset = Dataset(
          std::make_shared<PromotedSource>(dataset, type, std::move(planes)),
          dataset.calibration());
    } else {
      std::vector<std::pair<PlanePosition, Image>> edits;
      edits.reserve(planes.size());
      for (auto &[p, image] : planes)
        edits.emplace_back(position(p), *image);
      dataset = dataset.with_planes(std::move(edits));
    }
  }
  checkpoint(control);
  const bool pixels_changed = !dataset.shares_pixels(source.data().dataset);
  auto dependencies =
      legacy || spatial_changed ? Change::Data : target.dependencies();
  if(detail::validate_indexed_filter_eligibility(source,operation))dependencies=Change::All;
  if(variance || top_hat)dependencies=Change::All;
  const bool empty_variance = (nonbyte_variance || top_hat) && std::all_of(outputs.begin(), outputs.end(),
                                    [](const PlaneExecutionOutput &p){return p.skipped;});
  auto edit = make_edit(source, dataset, pixels_changed, legacy && !empty_variance && !remove_nans,
                        operation == "label" || operation == "binary_watershed",
                        spatial_changed, dependencies);
  auto display = source.display();
  if (legacy && !variance && !top_hat && !remove_nans)
    display.properties.erase("display_range");
  if (operation == "convert_image" && (pixels_changed||indexed_type) && (!legacy||source.data().metadata.extensions.count(indexed_palette_extension))) {
    // Numeric presentation belongs to the old sample domain. Preserve camera,
    // position and unrelated display extensions, but clear range/LUT/threshold
    // interpretation in the same guarded transaction as the converted pixels.
    // Pair the display with pixel history so Undo/Redo restores both domains.
    display = detail::pair_numeric_conversion_metadata(source, dataset.shape(), edit,dataset.pixel_type());
  }
  if(pixels_changed && operation!="convert_image")
    if(const auto paired=detail::retire_indexed_filter_palette(source,dataset,operation,edit))display=*paired;
  if (nonbyte_variance && std::any_of(outputs.begin(), outputs.end(),
                                     [](const PlaneExecutionOutput &p){return !p.skipped;})) {
    const auto stretched = detail::rank_variance_final_range_imagej(dataset, active_plane, control);
    const auto range = stretched.value_or(*initial_variance_range);
    if (variance_channels) {
      // Publish the final active processor range into canonical channel state.
      // Other channels, LUTs, visibility, mode and projection remain exact.
      // Collapsed cutoffs retain the original requested bounds, including
      // literal zero/equal ranges. Legacy flat bounds remain independent.
      if (stretched) {
        variance_channels->channels.at(active_plane.c).range = *stretched;
        display = with_channel_display(std::move(display), dataset, std::move(variance_channels));
      }
    } else {
      display.properties["display_range"] = metadata_array({range[0], range[1]});
    }
    // ImageJ's typed setMinAndMax resets the processor threshold, including
    // when the chosen bounds equal the old ones. Collapsed histogram cutoffs
    // skip that setter and retain the threshold. A threshold anchored to a
    // different plane belongs to that plane and is retained by this editor.
    if (stretched) {
      const auto threshold = manual_threshold_state(display, dataset.shape());
      if (threshold && manual_threshold_contains(*threshold,active_plane))
        reset_manual_threshold_state(display);
    }
    edit.set_axis_remap_display(display);
  }
  if(top_hat_subtract && std::any_of(outputs.begin(),outputs.end(),
      [](const PlaneExecutionOutput &p){return !p.skipped;})) {
    const auto effects=top_hat_preview_effects ? *top_hat_preview_effects :
        detail::top_hat_final_display_imagej(dataset,active_plane,false,
            top_hat_lane,top_hat_setter_range,control);
    if(effects.range) {
      if(auto channels=recognized_channel_display(display,dataset)) {
        channels->channels.at(active_plane.c).range=*effects.range;
        display=with_channel_display(std::move(display),dataset,std::move(channels));
      } else display.properties["display_range"]=metadata_array({(*effects.range)[0],(*effects.range)[1]});
    }
    if(effects.reset_threshold) {
      const auto threshold=manual_threshold_state(display,dataset.shape());
      if(threshold && manual_threshold_contains(*threshold,active_plane))reset_manual_threshold_state(display);
    }
    edit.set_axis_remap_display(display);
  }
  auto state = std::make_shared<PreparedOperation::State>(
      target, operation, std::move(normalized), std::move(edit),
      std::move(display), options, std::move(outputs),
      execution_cancellation());
  checkpoint(control);
  return PreparedOperation(std::move(state));
}
} // namespace imagejxx
