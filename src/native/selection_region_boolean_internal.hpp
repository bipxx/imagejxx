#pragma once
#include <imagejxx/selection_region.hpp>

namespace imagejxx::detail {
// Borrowed pairs for admitted command execution. Callers retain both operands
// across execution callbacks; these helpers neither publish nor report progress.
SelectionRegionBooleanEstimate estimate_region_pair(
    int width, int height, const Selection &, const Selection &,
    SelectionBooleanOperation, const SelectionEditLimits &);
Selection combine_region_pair(int width, int height, const Selection &,
                              const Selection &, SelectionBooleanOperation,
                              const SelectionEditLimits &);
} // namespace imagejxx::detail
