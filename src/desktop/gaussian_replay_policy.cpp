#include "gaussian_replay_policy.hpp"

#include <imagejxx/gaussian_command.hpp>
#include <imagejxx/rank_command.hpp>
#include <array>
#include <stdexcept>
#include <string>

namespace imagejxx::desktop {
namespace {

bool gaussian(const Json &step) {
  return step.is_object() && step.contains("schema_version") &&
         get(step, "operation") == "gaussian_blur_imagej";
}

// The native record validator has already checked exact fields, numeric types,
// finiteness, dimensions and effective sigmas. This is the command's calibration
// schema, which differs from the desktop document serialization adapter.
SpatialCalibration recordedCalibration(const Json &step) {
  const auto &value = step.at("calibration");
  SpatialCalibration calibration;
  calibration.spacing = value.at("spacing").get<std::array<double, 3>>();
  calibration.origin = value.at("origin").get<std::array<double, 3>>();
  calibration.direction = value.at("direction").get<std::array<double, 9>>();
  calibration.unit = value.at("unit").get<std::string>();
  calibration.frame_interval = value.at("frame_interval").get<double>();
  calibration.time_origin = value.at("time_origin").get<double>();
  calibration.time_unit = value.at("time_unit").get<std::string>();
  return calibration;
}

bool sameCalibration(const SpatialCalibration &recorded,
                     const SpatialCalibration &destination) {
  return recorded.spacing == destination.spacing &&
         recorded.origin == destination.origin &&
         recorded.direction == destination.direction &&
         recorded.unit == destination.unit &&
         recorded.frame_interval == destination.frame_interval &&
         recorded.time_origin == destination.time_origin &&
         recorded.time_unit == destination.time_unit;
}

} // namespace

void validateGaussianReplayCalibration(const Json &steps,
                                       const DocumentSnapshot &destination,
                                       bool useDestinationCalibration) {
  if (!steps.is_array())
    throw std::invalid_argument("Gaussian workflow steps must be an array");

  // An early calibration mismatch must not conceal a malformed later original,
  // and explicit consent must never bypass complete typed-record validation.
  for (const auto &step : steps) {
    if (!step.is_object() || !step.contains("schema_version"))
      continue;
    if (gaussian(step))
      validate_gaussian_command_record(metadataObjectFromJson(step));
    else if ((get(step, "operation") == "rank_filter_imagej" || (get(step, "operation") == "remove_outliers_imagej" || get(step, "operation") == "top_hat_imagej" || get(step, "operation") == "remove_nans_imagej")))
      validate_rank_command_record(metadataObjectFromJson(step));
    else
      throw std::invalid_argument("Unsupported typed workflow operation");
  }

  if (useDestinationCalibration)
    return;

  const auto &calibration = destination.data().dataset.calibration();
  for (std::size_t index = 0; index < steps.size(); ++index) {
    const auto &step = steps[index];
    if (gaussian(step) &&
        !sameCalibration(recordedCalibration(step), calibration))
      throw std::invalid_argument(
          "Gaussian workflow step " + std::to_string(index + 1) +
          " recorded calibration differs from this image; choose Use this "
          "image's calibration to rebind explicitly");
  }
}

} // namespace imagejxx::desktop
