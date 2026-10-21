#pragma once
#include <imagejxx/imagej_roi.hpp>
namespace imagejxx::detail {
// Bounded preview only; callers must subsequently run the complete typed codec.
std::string preview_imagej_roi_properties(const std::vector<unsigned char> &,
                                          const ImageJRoiLimits &);
} // namespace imagejxx::detail
