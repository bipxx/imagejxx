#pragma once
#include <imagejxx/selection_edit.hpp>

namespace imagejxx {
struct SelectionInterpolationOptions {
  double interval = 1; // pixels; a negative value toggles adjust_to_match
  bool smooth = false;
  bool adjust_to_match = false;
};
std::size_t estimate_interpolated_selection(const Selection&,int width,int height,
    const SelectionInterpolationOptions& = {},const SelectionEditLimits& = {});
Selection interpolate_selection(const Selection&,int width,int height,
    const SelectionInterpolationOptions& = {},const SelectionEditLimits& = {},
    const ExecutionControl& = {});
}
