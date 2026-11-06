#pragma once
#include <imagejxx/selection_edit.hpp>

namespace imagejxx {
// Stored only when the ordinary native kind loses ImageJ polygon semantics,
// or when explicitly retaining degenerate area/open geometry. Legacy wires refuse it.
inline constexpr const char* polygon_type_attribute = "imagejxx.polygon_type";
enum class SelectionPolygonType { Polygon, Freehand, Traced, Polyline, Freeline };
std::optional<SelectionPolygonType> selection_polygon_type(const Selection&);
Selection with_selection_polygon_type(const Selection&,SelectionPolygonType);
struct SelectionInterpolationOptions {
  double interval = 1; // pixels; a negative value toggles adjust_to_match
  bool smooth = false;
  bool adjust_to_match = false;
  double magnification = 1; // frozen canvas context; detached callers default to 1x
};
std::size_t estimate_interpolated_selection(const Selection&,int width,int height,
    const SelectionInterpolationOptions& = {},const SelectionEditLimits& = {});
Selection interpolate_selection(const Selection&,int width,int height,
    const SelectionInterpolationOptions& = {},const SelectionEditLimits& = {},
    const ExecutionControl& = {});
}
