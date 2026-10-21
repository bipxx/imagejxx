#pragma once
#include <imagejxx/imagej_roi_mapping.hpp>
namespace imagejxx::detail {
struct ImageJRoiPosition {
  int coordinate;
  std::optional<int> t, c;
};
ImageJRoiPosition resolve_imagej_roi_position(std::optional<int> t,
                                              std::optional<int> c,
                                              std::optional<int> z,
                                              int legacy_position,
                                              const ImageJRoiPlaneMapping &);
} // namespace imagejxx::detail
