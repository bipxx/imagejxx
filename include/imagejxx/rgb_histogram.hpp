#pragma once
#include <imagejxx/target_histogram.hpp>
#include <imagejxx/auto_contrast.hpp>
namespace imagejxx {
enum class RgbHistogramComponent { Intensity, Red, Green, Blue };
struct RgbHistogramOptions {
  RgbHistogramComponent component=RgbHistogramComponent::Intensity;
  std::array<double,3> weights{1./3,1./3,1./3};
};
struct RgbHistogramResult:TargetHistogramResult {
  RgbHistogramOptions rgb_options;
};
// Raw 256-bin UInt8 RGB histograms; independent scientific T/C/Z and area ROI
// policies. Intensity uses ColorProcessor's ordered weighted sum plus 0.5,
// narrowed to an integer. Weights are finite, never normalized/clipped; a
// ROI member index outside 0..255 refuses atomically, as upstream does.
// Whole-plane extrema are a native extension over Java-narrowed intensities,
// including excluded pixels whose projected values can fall outside byte bins.
// Primary components ignore weights. Consumer policy decides which ROI to use.
// Admission has no callbacks/reads. Processing freezes intent before callbacks,
// validates all acquired RGB samples, and reads each selected plane once.
TargetHistogramEstimate estimate_rgb_histogram(const ResolvedTarget &,
    const RgbHistogramOptions & = {},const DatasetHistogramLimits & = {});
RgbHistogramResult accumulate_rgb_histogram(const ResolvedTarget &,
    const RgbHistogramOptions & = {},const DatasetHistogramLimits & = {},
    const ExecutionControl & = {});
struct RgbAutoContrastResult {
  std::array<double,2> range{0,255};
  AutoContrastCycle cycle;
  AutoContrastOutcome outcome=AutoContrastOutcome::ResetFallback;
  std::optional<std::array<int,2>> indices;
};
// Pure ImageJ byte-histogram decision. Does not read sources or publish pixels,
// preview, cycle or records. RGB Auto's consumer must histogram the immutable
// baseline intensity (not a selected primary), then map the whole plane.
// Empty/dominant-only populations reset to 0..255 and reset the next cycle.
RgbAutoContrastResult rgb_auto_contrast_from_histogram(const TargetHistogramResult &,
    int previous_divisor=0);
}
