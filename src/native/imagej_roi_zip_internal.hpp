#pragma once
#include <imagejxx/imagej_roi.hpp>
namespace imagejxx::detail {
std::string imagej_roi_zip_member_key(const std::string &,
                                      const ImageJRoiZipLimits &);
}
