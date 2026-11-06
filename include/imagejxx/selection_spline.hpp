#pragma once
#include <imagejxx/selection_edit.hpp>

namespace imagejxx {
inline constexpr const char* spline_fit_attribute = "imagejxx.spline_fit";
enum class SplineFitMode { Fit, Straighten };
struct SplineFitOptions {
  SplineFitMode mode = SplineFitMode::Fit;
  std::size_t evaluation_points = 0; // zero: frozen ImageJ length/zoom default
  double magnification = 1;
};
struct SelectionSplineFit {
  Selection controls;
  SplineFitOptions options;
  std::size_t sampling_points = 0; // frozen evaluations before straightening
};
// Processing geometry remains an ordinary polygon or open polyline. Typed
// state retains actual editable controls/options separately from its samples.
std::optional<SelectionSplineFit> selection_spline_fit(const Selection&);
void validate_selection_spline_fit(const Selection&);
std::size_t estimate_spline_selection(const Selection&,int width,int height,
    const SplineFitOptions& = {},const SelectionEditLimits& = {});
Selection fit_spline_selection(const Selection&,int width,int height,
    const SplineFitOptions& = {},const SelectionEditLimits& = {},const ExecutionControl& = {});
Selection remove_spline_fit(const Selection&,int width,int height,
    const SelectionEditLimits& = {},const ExecutionControl& = {});
}
