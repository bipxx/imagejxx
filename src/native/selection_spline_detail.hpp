#pragma once
#include <imagejxx/document.hpp>
namespace imagejxx { struct SplineFitOptions;struct SelectionEditLimits;struct ExecutionControl; }
namespace imagejxx::detail {
// Borrowed structural check for geometry admission/rendering. Does not copy
// controls or solve a spline; numerical validation belongs to creation/load/edit.
std::size_t spline_control_count(const Selection&);
// Minimal geometry/type extraction for bounded metric scratch; no opaque metadata copy.
Selection spline_length_controls(const Selection&,bool* relative=nullptr);
// Refit actual editor controls without command-level freehand preprocessing.
Selection refit_spline_controls(const Selection&,int,int,const SplineFitOptions&,
    const SelectionEditLimits&,const ExecutionControl&);
}
