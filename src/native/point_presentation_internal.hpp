#pragma once
#include <imagejxx/point_presentation.hpp>
#include <functional>
namespace imagejxx::detail {
// Same layout implementation, with absolute working-byte admission before
// annotation parsing and output allocation. Empty admission preserves public API.
PointPresentation point_presentation_admitted(const Selection&,
    const PointPresentationOptions&,const PointPresentationLimits&,
    const std::function<void(std::size_t)>&);
}
