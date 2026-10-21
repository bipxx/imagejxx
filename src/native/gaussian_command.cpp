#include "kernel_control.hpp"
#include "target_record_internal.hpp"
#include <imagejxx/gaussian_command.hpp>

#include <algorithm>
#include <cmath>
#include <initializer_list>
#include <limits>
#include <set>
#include <stdexcept>
#include <utility>

namespace imagejxx {
namespace {

constexpr const char *operation_id = "gaussian_blur_imagej";

void validate_source_metadata(const DocumentSnapshot &source) {
  kernel_checkpoint();
  const auto shape = source.data().dataset.shape();
  const auto type = source.data().dataset.pixel_type();
  if (!((type == PixelType::UInt8 && (shape.s == 1 || shape.s == 3)) ||
        ((type == PixelType::UInt16 || type == PixelType::Float32) &&
         shape.s == 1)))
    throw std::invalid_argument("ImageJ Gaussian supports UInt8 gray/RGB, "
                                "UInt16 gray and Float32 gray");
}

std::array<double, 2> effective_sigmas(GaussianBlurCommandOptions options,
                                       const SpatialCalibration &calibration) {
  validate_gaussian_command_options(options);
  const bool scaled =
      options.scaled_units && gaussian_scaled_units_available(calibration);
  std::array<double, 2> result{options.sigma, options.sigma};
  if (scaled) {
    result[0] /= calibration.spacing[0];
    result[1] /= calibration.spacing[1];
  }
  for (double sigma : result) {
    const double halo = std::ceil(5.0 * sigma);
    if (!std::isfinite(sigma) || sigma < 0 || !std::isfinite(halo) ||
        halo > static_cast<double>(std::numeric_limits<int>::max()))
      throw std::invalid_argument(
          "Gaussian effective sigma exceeds safe coordinate range");
  }
  return result;
}

template <std::size_t N>
MetadataValue number_array(const std::array<double, N> &values) {
  MetadataArray result;
  result.reserve(N);
  for (double value : values)
    result.emplace_back(value);
  return metadata_array(std::move(result));
}

MetadataObject calibration_record(const SpatialCalibration &calibration) {
  return {{"spacing", number_array(calibration.spacing)},
          {"origin", number_array(calibration.origin)},
          {"direction", number_array(calibration.direction)},
          {"unit", calibration.unit},
          {"frame_interval", calibration.frame_interval},
          {"time_origin", calibration.time_origin},
          {"time_unit", calibration.time_unit}};
}

void exact_fields(const MetadataObject &object,
                  std::initializer_list<const char *> names) {
  std::set<std::string> expected;
  for (const char *name : names) {
    expected.emplace(name);
    if (!object.count(name))
      throw std::invalid_argument(
          std::string("Missing Gaussian record field: ") + name);
  }
  for (const auto &[name, value] : object) {
    (void)value;
    if (!expected.count(name))
      throw std::invalid_argument("Unknown Gaussian record field: " + name);
  }
}

const MetadataObject &object_value(const MetadataValue &value) {
  if (const auto node =
          std::get_if<std::shared_ptr<const MetadataTree>>(&value);
      node && *node)
    if (const auto object = std::get_if<MetadataObject>(&(*node)->value))
      return *object;
  throw std::invalid_argument("Gaussian record requires an object");
}

const MetadataArray &array_value(const MetadataValue &value) {
  if (const auto node =
          std::get_if<std::shared_ptr<const MetadataTree>>(&value);
      node && *node)
    if (const auto array = std::get_if<MetadataArray>(&(*node)->value))
      return *array;
  throw std::invalid_argument("Gaussian record requires a numeric array");
}

double number_value(const MetadataValue &value) {
  double result;
  if (const auto real = std::get_if<double>(&value))
    result = *real;
  else if (const auto integer = std::get_if<std::int64_t>(&value)) {
    result = static_cast<double>(*integer);
    if (result >= std::ldexp(1.0, 63) || result < -std::ldexp(1.0, 63) ||
        static_cast<std::int64_t>(result) != *integer)
      throw std::invalid_argument(
          "Gaussian record integer loses numeric precision");
  } else if (const auto integer = std::get_if<std::uint64_t>(&value)) {
    result = static_cast<double>(*integer);
    if (result >= std::ldexp(1.0, 64) ||
        static_cast<std::uint64_t>(result) != *integer)
      throw std::invalid_argument(
          "Gaussian record integer loses numeric precision");
  } else
    throw std::invalid_argument("Gaussian record requires a number");
  if (!std::isfinite(result))
    throw std::invalid_argument("Gaussian record numbers must be finite");
  return result;
}

bool boolean_value(const MetadataValue &value) {
  if (const auto boolean = std::get_if<bool>(&value))
    return *boolean;
  throw std::invalid_argument("Gaussian record requires a Boolean");
}

const std::string &text_value(const MetadataValue &value) {
  if (const auto text = std::get_if<std::string>(&value))
    return *text;
  throw std::invalid_argument("Gaussian record requires text");
}

template <std::size_t N>
std::array<double, N> read_numbers(const MetadataValue &value) {
  const auto &array = array_value(value);
  if (array.size() != N)
    throw std::invalid_argument("Gaussian record array has the wrong length");
  std::array<double, N> result;
  for (std::size_t i = 0; i < N; ++i)
    result[i] = number_value(array[i]);
  return result;
}

SpatialCalibration read_calibration(const MetadataObject &record) {
  exact_fields(record, {"spacing", "origin", "direction", "unit",
                        "frame_interval", "time_origin", "time_unit"});
  SpatialCalibration calibration;
  calibration.spacing = read_numbers<3>(record.at("spacing"));
  calibration.origin = read_numbers<3>(record.at("origin"));
  calibration.direction = read_numbers<9>(record.at("direction"));
  calibration.unit = text_value(record.at("unit"));
  calibration.frame_interval = number_value(record.at("frame_interval"));
  calibration.time_origin = number_value(record.at("time_origin"));
  calibration.time_unit = text_value(record.at("time_unit"));
  calibration.validate();
  return calibration;
}

bool same_calibration(const SpatialCalibration &a,
                      const SpatialCalibration &b) {
  return a.spacing == b.spacing && a.origin == b.origin &&
         a.direction == b.direction && a.unit == b.unit &&
         a.frame_interval == b.frame_interval &&
         a.time_origin == b.time_origin && a.time_unit == b.time_unit;
}

struct GaussianRecord {
  GaussianBlurCommandOptions options;
  SpatialCalibration calibration;
  std::array<double, 2> effective_sigma;
  detail::TargetRecord target;
};

GaussianRecord decode_gaussian_record(const MetadataObject &record) {
  validate_metadata(record);
  exact_fields(record, {"schema_version", "operation", "options", "calibration",
                        "effective_sigma_xy", "target"});
  const auto &version = record.at("schema_version");
  const auto signed_version = std::get_if<std::int64_t>(&version);
  const auto unsigned_version = std::get_if<std::uint64_t>(&version);
  if (!((signed_version && *signed_version == 1) ||
        (unsigned_version && *unsigned_version == 1)))
    throw std::invalid_argument("Unsupported Gaussian command record version");
  if (text_value(record.at("operation")) != operation_id)
    throw std::invalid_argument(
        "Gaussian command record names another operation");
  const auto &option_record = object_value(record.at("options"));
  exact_fields(option_record, {"sigma", "scaled_units"});
  GaussianBlurCommandOptions options{
      number_value(option_record.at("sigma")),
      boolean_value(option_record.at("scaled_units"))};
  auto calibration = read_calibration(object_value(record.at("calibration")));
  const auto expected = effective_sigmas(options, calibration);
  const auto recorded = read_numbers<2>(record.at("effective_sigma_xy"));
  if (recorded != expected)
    throw std::invalid_argument(
        "Gaussian command effective sigma contradicts its intent");
  auto target = detail::decode_target_record(object_value(record.at("target")));
  if (target.request.mode != ExecutionMode::Planes)
    throw std::invalid_argument("Gaussian command requires plane execution");
  if (target.source_shape.s != 1 && target.source_shape.s != 3)
    throw std::invalid_argument(
        "Gaussian command record requires gray or RGB samples");
  return {options, std::move(calibration), expected, std::move(target)};
}

bool non_area(const Selection &selection) {
  return selection.kind == "line" || selection.kind == "polyline" ||
         selection.kind == "angle" || selection.kind == "point" ||
         selection.kind == "multipoint";
}

GaussianBlurExtent clipped_bounds(std::array<double, 4> bounds, int width,
                                  int height) {
  for (double value : bounds)
    if (!std::isfinite(value))
      throw std::invalid_argument("Gaussian ROI bounds must be finite");
  const double left = std::clamp(std::floor(bounds[0]), 0.0, double(width));
  const double top = std::clamp(std::floor(bounds[1]), 0.0, double(height));
  const double right = std::clamp(std::ceil(bounds[2]), 0.0, double(width));
  const double bottom = std::clamp(std::ceil(bounds[3]), 0.0, double(height));
  if (right < left || bottom < top)
    throw std::invalid_argument("Gaussian ROI bounds are reversed");
  // Clipping to positive int-sized metadata precedes every integer conversion.
  const int x = static_cast<int>(left), y = static_cast<int>(top);
  return {x, y, static_cast<int>(right) - x, static_cast<int>(bottom) - y};
}

Image area_mask(const Selection &selection, int width, int height) {
  kernel_checkpoint();
  selection.validate(width, height);
  if (selection.kind != "mask")
    return selection.mask(width, height);
  Image result(width, height, 1, PixelType::UInt8);
  const auto &packed = selection.packed_mask();
  kernel_blocks("Gaussian command mask", result.pixels.size(),
                [&](std::size_t first, std::size_t last) {
                  for (auto i = first; i < last; ++i)
                    result.pixels[i] =
                        (packed[i / 8] & (1u << (7 - i % 8))) ? 255.0 : 0.0;
                });
  return result;
}

GaussianBlurExtent area_bounds(const Selection &selection, const Image &mask) {
  const auto width = mask.width, height = mask.height;
  if (selection.is_rounded_box() || selection.kind == "mask") {
    int left = width, top = height, right = 0, bottom = 0;
    kernel_blocks("Gaussian command mask bounds", mask.pixels.size(),
                  [&](std::size_t first, std::size_t last) {
                    for (auto i = first; i < last; ++i)
                      if (mask.pixels[i] != 0) {
                        const int x = static_cast<int>(i % width);
                        const int y = static_cast<int>(i / width);
                        left = std::min(left, x);
                        top = std::min(top, y);
                        right = std::max(right, x + 1);
                        bottom = std::max(bottom, y + 1);
                      }
                  });
    if (right == 0 || bottom == 0)
      return {0, 0, 0, 0};
    return {left, top, right - left, bottom - top};
  }
  if (selection.is_rounded_box() || selection.kind == "rotated_rectangle" ||
      selection.kind == "rotated_ellipse")
    return clipped_bounds(
        selection.box_frame()->bounds(selection.kind == "rotated_ellipse"),
        width, height);
  if (selection.points.empty())
    throw std::invalid_argument("Gaussian area ROI has no geometry");
  std::array<double, 4> bounds{
      selection.points.front()[0], selection.points.front()[1],
      selection.points.front()[0], selection.points.front()[1]};
  for (const auto &point : selection.points) {
    bounds[0] = std::min(bounds[0], point[0]);
    bounds[1] = std::min(bounds[1], point[1]);
    bounds[2] = std::max(bounds[2], point[0]);
    bounds[3] = std::max(bounds[3], point[1]);
    kernel_checkpoint_periodic();
  }
  return clipped_bounds(bounds, width, height);
}

void union_extent(std::optional<GaussianBlurExtent> &combined,
                  GaussianBlurExtent next) {
  if (next.width == 0 || next.height == 0)
    return;
  if (!combined) {
    combined = next;
    return;
  }
  const int left = std::min(combined->x, next.x);
  const int top = std::min(combined->y, next.y);
  const int right =
      std::max(combined->x + combined->width, next.x + next.width);
  const int bottom =
      std::max(combined->y + combined->height, next.y + next.height);
  combined = GaussianBlurExtent{left, top, right - left, bottom - top};
}

} // namespace

bool gaussian_scaled_units_available(const SpatialCalibration &calibration) {
  calibration.validate();
  return calibration.unit != "px" && calibration.unit != "pixel" &&
         calibration.unit != "pixels";
}

ResolvedGaussianBlurCommand::ResolvedGaussianBlurCommand(
    GaussianBlurCommandOptions options, SpatialCalibration calibration,
    ResolvedTarget target, double sigma_x, double sigma_y)
    : options_(options), calibration_(std::move(calibration)),
      target_(std::move(target)), sigma_x_(sigma_x), sigma_y_(sigma_y) {}

bool ResolvedGaussianBlurCommand::scaled_units_available() const {
  return gaussian_scaled_units_available(calibration_);
}

OperationParameters ResolvedGaussianBlurCommand::parameters() const {
  return {{"sigma", NumericArray{{2}, {sigma_y_, sigma_x_}}}};
}

MetadataObject ResolvedGaussianBlurCommand::record() const {
  return {
      {"schema_version", 1},
      {"operation", std::string(operation_id)},
      {"options", metadata_object({{"sigma", options_.sigma},
                                   {"scaled_units", options_.scaled_units}})},
      {"calibration", metadata_object(calibration_record(calibration_))},
      {"effective_sigma_xy", metadata_array({sigma_x_, sigma_y_})},
      {"target", metadata_object(target_.record())}};
}

void validate_gaussian_command_options(
    const GaussianBlurCommandOptions &options) {
  if (!std::isfinite(options.sigma) || options.sigma < 0)
    throw std::invalid_argument(
        "Gaussian sigma must be finite and nonnegative");
}

ResolvedGaussianBlurCommand
resolve_gaussian_command(const DocumentSnapshot &source,
                         const TargetRequest &request,
                         GaussianBlurCommandOptions options) {
  validate_source_metadata(source);
  if (request.mode != ExecutionMode::Planes)
    throw std::invalid_argument("Gaussian command requires plane execution");
  const auto calibration = source.data().dataset.calibration();
  calibration.validate();
  const auto sigma = effective_sigmas(options, calibration);
  auto target = resolve_target(source, request, operation_id);
  return {options, calibration, std::move(target), sigma[0], sigma[1]};
}

void validate_gaussian_command_record(const MetadataObject &record) {
  (void)decode_gaussian_record(record);
}

ResolvedGaussianBlurCommand
gaussian_command_from_record(const DocumentSnapshot &source,
                             const MetadataObject &record) {
  validate_source_metadata(source);
  auto decoded = decode_gaussian_record(record);
  if (!same_calibration(decoded.calibration,
                        source.data().dataset.calibration()))
    throw std::invalid_argument(
        "Gaussian command calibration differs from its source");
  auto target =
      detail::strict_target_from_record(source, decoded.target, operation_id);
  return {decoded.options, std::move(decoded.calibration), std::move(target),
          decoded.effective_sigma[0], decoded.effective_sigma[1]};
}

ResolvedGaussianBlurCommand
rebind_gaussian_command(const DocumentSnapshot &destination,
                        const MetadataObject &record,
                        const TargetRequest &request) {
  // Validate every original claim before intentionally replacing only the
  // target via the caller's request. Old shape/calibration cannot be borrowed
  // from the destination to make a malformed record appear valid.
  const auto decoded = decode_gaussian_record(record);
  return resolve_gaussian_command(destination, request, decoded.options);
}

GaussianBlurCommandRegion gaussian_command_region(const ResolvedTarget &target,
                                                  std::size_t index) {
  validate_source_metadata(target.source());
  if (target.execution_mode() != ExecutionMode::Planes)
    throw std::invalid_argument("Gaussian command requires plane execution");
  const auto position = target.position(index);
  const auto &data = target.source().data();
  const auto shape = data.dataset.shape();
  if (target.roi_policy() == RoiPolicy::WholePlane ||
      (target.roi_policy() == RoiPolicy::ActiveSelection && !data.selection))
    return {};
  if (target.roi_policy() == RoiPolicy::ActiveSelection) {
    const auto &selection = *data.selection;
    if (non_area(selection))
      return {};
    auto mask = area_mask(selection, shape.x, shape.y);
    const auto extent = area_bounds(selection, mask);
    return {extent, std::move(mask)};
  }

  // A matching non-area ROI is whole-plane command intent, independent of any
  // other matching area entries and their order in the ROI manager.
  auto matches = [&](const Roi &roi) {
    return (!roi.t || *roi.t == position.t) &&
           (!roi.c || *roi.c == position.c) && (!roi.z || *roi.z == position.z);
  };
  for (const auto &roi : data.rois) {
    kernel_checkpoint_periodic();
    if (matches(roi) && non_area(roi.selection))
      return {};
  }
  kernel_checkpoint();
  Image combined(shape.x, shape.y, 1, PixelType::UInt8);
  std::optional<GaussianBlurExtent> extent;
  for (const auto &roi : data.rois) {
    kernel_checkpoint();
    if (!matches(roi))
      continue;
    auto mask = area_mask(roi.selection, shape.x, shape.y);
    union_extent(extent, area_bounds(roi.selection, mask));
    kernel_blocks("Gaussian command ROI union", combined.pixels.size(),
                  [&](std::size_t first, std::size_t last) {
                    for (auto i = first; i < last; ++i)
                      if (mask.pixels[i] != 0)
                        combined.pixels[i] = 255;
                  });
  }
  return {extent.value_or(GaussianBlurExtent{0, 0, 0, 0}), std::move(combined)};
}

} // namespace imagejxx
