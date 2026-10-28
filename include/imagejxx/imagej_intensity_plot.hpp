#pragma once
#include <imagejxx/imagej_intensity_calibration.hpp>
#include <imagejxx/execution.hpp>
namespace imagejxx {
struct ImageJIntensityFitPlot {
  std::string function, value_unit, objective;
  std::vector<double> measured, known;
  std::vector<float> curve_x, curve_y;
  double x_min=0, x_max=0, y_min=0, y_max=0;
};
// ImageJ CurveFitter.getPlot sampling: max(points, pairs), capped at1000;
// points==256 requests the8-bit calibration domain0..255. Coordinates and
// model values round to Float32 as upstream. Non-finite curve samples remain
// gaps; display limits use finite values and bounded data-range expansion.
// Requires bounded fitted-function provenance, not an arbitrary lookup table.
// Admission precedes input copies/callbacks. No provider reads or source edits.
ImageJIntensityFitPlot imagej_intensity_fit_plot(
    const ImageJIntensityCalibration&, std::size_t points=100,
    std::size_t max_working_bytes=4ULL*1024*1024,
    const ExecutionControl& control={});
struct ImageJIntensityCalibrationPlot {
  std::string mode, value_unit;
  std::vector<float> curve_x, curve_y;
  // Exact sampled IEEE words, including NaN payloads and signed zero.
  std::vector<std::uint32_t> curve_words;
  double x_min=0, x_max=0, y_min=0, y_max=1;
  bool has_finite_samples=false;
};
// Calibrator.showPlot table policy:256 Float32 positions over the complete
// integer raw range, truncated indices into the stored calibration table.
// No model evaluation/interpolation or full table materialization. Signed16
// uses its implicit table. Non-finite samples remain gaps; empty finite ranges
// use0..1 display limits. Does not read providers, fit data or edit metadata.
ImageJIntensityCalibrationPlot imagej_intensity_calibration_plot(
    const ImageJIntensityCalibration&,
    std::size_t max_working_bytes=4ULL*1024*1024,
    const ExecutionControl& control={});
}
