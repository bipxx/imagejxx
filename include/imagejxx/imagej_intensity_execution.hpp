#pragma once
#include <imagejxx/execution.hpp>
#include <imagejxx/imagej_intensity_calibration.hpp>
namespace imagejxx {
struct ImageJIntensityFitRequest {
  std::string function = "straight_line";
  std::vector<double> measured, known;
  std::string value_unit = "Gray Value";
  bool zero_clip = false;
  std::vector<double> initial_coefficients;
  std::size_t max_iterations = 256;
  double relative_tolerance = 1e-10;
  std::size_t max_working_bytes = 512ULL*1024*1024;
};
// Descriptor-only combined metadata/input/solver/table/publication admission.
// No provider reads/callbacks. Provider-private caches retain separate budgets.
std::size_t estimate_imagej_intensity_fit(const DocumentSnapshot&, const ImageJIntensityFitRequest&);
// Freeze input before callbacks; worker builds an isolated metadata edit.
// Existing Application::publish guards membership, All revisions and retained
// cancellation. Same source/pixels/history untouched until atomic publication.
PreparedOperation prepare_imagej_intensity_fit(const DocumentSnapshot&,
    const ImageJIntensityFitRequest&, const ExecutionControl& control = {});
}
