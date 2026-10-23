#pragma once
#include <imagejxx/selection_edit.hpp>

namespace imagejxx::detail {
std::size_t admit_selection_geometry(const Selection &, int, int,
                                     const SelectionEditLimits &,
                                     const ExecutionControl &);
Selection rotate_region(const Selection &, int, int, double,
                        std::optional<std::array<double, 2>>,
                        const SelectionEditLimits &, const ExecutionControl &);
} // namespace imagejxx::detail
