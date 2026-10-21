#include "imagej_roi_position.hpp"
#include <stdexcept>
namespace imagejxx::detail {
namespace {
void check(bool v, const char *message) {
  if (!v)
    throw std::invalid_argument(message);
}
void merge(std::optional<int> &into, int value, const char *message) {
  check(!into || *into == value, message);
  into = value;
}
} // namespace
ImageJRoiPosition
resolve_imagej_roi_position(std::optional<int> time, std::optional<int> channel,
                            std::optional<int> z, int legacy_position,
                            const ImageJRoiPlaneMapping &mapping) {
  mapping.extent.validate();
  const auto geometry =
      planar_annotation_geometry(mapping.extent, mapping.plane);
  const auto &shape = mapping.extent;
  const std::array<int, 3> dimensions{shape.x, shape.y, shape.z};
  check(mapping.z_position == ImageJZPositionMapping::ScientificZ ||
            mapping.z_position == ImageJZPositionMapping::FixedAxis,
        "Unknown ImageJ Z-position mapping");
  check(mapping.legacy_position == ImageJLegacyPositionMapping::Reject ||
            mapping.legacy_position == ImageJLegacyPositionMapping::FixedAxis ||
            mapping.legacy_position ==
                ImageJLegacyPositionMapping::HyperstackCZT,
        "Unknown ImageJ legacy-position mapping");
  auto t = time, c = channel;
  std::optional<int> fixed;
  check(!t || (*t >= 0 && *t < shape.t),
        "ImageJ ROI time outside declared source extent");
  check(!c || (*c >= 0 && *c < shape.c),
        "ImageJ ROI channel outside declared source extent");
  if (z) {
    const int size = mapping.z_position == ImageJZPositionMapping::ScientificZ
                         ? shape.z
                         : dimensions[geometry.fixed_axis];
    check(*z >= 0 && *z < size,
          "ImageJ ROI position outside declared source extent");
    check(mapping.plane == Plane::XY ||
              mapping.z_position == ImageJZPositionMapping::FixedAxis,
          "Scientific Z cannot fix an XZ/YZ plane; choose explicit fixed-axis "
          "interpretation");
    fixed = z;
  }
  check(legacy_position >= 0, "Negative ImageJ legacy stack position");
  if (legacy_position > 0) {
    check(mapping.legacy_position != ImageJLegacyPositionMapping::Reject,
          "Ambiguous ImageJ legacy stack position requires explicit "
          "interpretation");
    if (mapping.legacy_position == ImageJLegacyPositionMapping::FixedAxis) {
      const auto value = legacy_position - 1;
      check(value < dimensions[geometry.fixed_axis],
            "ImageJ legacy fixed coordinate outside declared source extent");
      merge(fixed, value,
            "ImageJ legacy and standard fixed coordinates conflict");
    } else {
      check(mapping.plane == Plane::XY,
            "ImageJ hyperstack index requires an XY mapping");
      auto index = std::size_t(legacy_position - 1);
      check(index < shape.plane_count(),
            "ImageJ hyperstack index outside declared source extent");
      const int channel = int(index % std::size_t(shape.c));
      index /= std::size_t(shape.c);
      const int z = int(index % std::size_t(shape.z)),
                time = int(index / std::size_t(shape.z));
      merge(c, channel, "ImageJ legacy and standard channels conflict");
      merge(t, time, "ImageJ legacy and standard times conflict");
      merge(fixed, z, "ImageJ legacy and standard Z positions conflict");
    }
  }
  if (mapping.coordinate) {
    check(*mapping.coordinate >= 0 &&
              *mapping.coordinate < dimensions[geometry.fixed_axis],
          "Explicit ImageJ fixed coordinate outside source extent");
    merge(fixed, *mapping.coordinate,
          "Explicit ImageJ fixed coordinate conflicts with file position");
  }
  check(bool(fixed),
        "Unpositioned ImageJ ROI requires an explicit fixed coordinate");
  return {*fixed, t, c};
}
} // namespace imagejxx::detail
