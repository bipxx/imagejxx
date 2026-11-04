#pragma once
#include <imagejxx/imagej_intensity_calibration.hpp>
#include <optional>
namespace imagejxx::detail {
// Known function-producer provenance uses Calibration.getCValue's double
// formula for medians. Custom/future table producers return no formula.
std::optional<double> imagej_function_calibrated_value(
    const ImageJIntensityCalibration&, double raw);
}
