#pragma once
#include <imagejxx/selection_edit.hpp>

namespace imagejxx {
// Stored only when the ordinary native kind loses ImageJ polygon semantics,
// or when explicitly retaining degenerate area/open geometry. Legacy wires refuse it.
inline constexpr const char* polygon_type_attribute = "imagejxx.polygon_type";
// Canonical true-only marker. Absence means the native Float32 constructor.
// Explicit polygon_type carries the existing V5/creation-profile fence.
inline constexpr const char* polygon_integer_coordinates_attribute = "imagejxx.polygon_integer_coordinates";
enum class SelectionPolygonType { Polygon, Freehand, Traced, Polyline, Freeline };
std::optional<SelectionPolygonType> selection_polygon_type(const Selection&);
Selection with_selection_polygon_type(const Selection&,SelectionPolygonType);
bool selection_polygon_integer_coordinates(const Selection&);
Selection with_selection_polygon_coordinates(const Selection&,bool integer_coordinates);
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
