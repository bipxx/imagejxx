#pragma once
#include <imagejxx/imagej_roi.hpp>
namespace imagejxx {
Image decode_imagej_embedded_tiff(const std::vector<unsigned char> &,
                                  const ImageJRoiLimits &);
std::vector<unsigned char> encode_imagej_embedded_tiff(const Image &,
                                                       const ImageJRoiLimits &);
} // namespace imagejxx
