#pragma once
#include <imagejxx/color_components.hpp>
namespace imagejxx::detail {
// Dataset adapters own plane progress. Retain inherited stage/cancellation
// callbacks while suppressing this sub-kernel's standalone completion unit.
Stack split_color_components_controlled(const Image &,ColorComponentSpace,
    const ExecutionControl &,std::size_t,bool report_progress);
Image merge_color_components_controlled(const Stack &,ColorComponentSpace,bool,
    const ExecutionControl &,std::size_t,bool report_progress);
}
