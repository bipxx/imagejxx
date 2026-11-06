#pragma once
#include <imagejxx/selection_interpolation.hpp>
namespace imagejxx::detail {
// Borrow the state; no geometry/opaque-metadata copy during wire admission.
std::optional<SelectionPolygonType> polygon_type(const std::string&,const MetadataObject&);
}
