#pragma once
#include <imagejxx/document.hpp>
namespace imagejxx {
inline constexpr const char* point_annotations_attribute="imagejxx.point_annotations";
struct PointAnnotation {
  unsigned counter=0;
  // Native zero-based scientific T/C/Z; null uses the captured parent view.
  std::optional<PlanePosition> position;
};
// Pure metadata operations; nonempty point/multipoint, exact one entry per
// vertex, counters 0..99, nonnegative positions. Version1 rejects unknown
// fields/versions. Other attributes stay intact; no image/provider access.
std::vector<PointAnnotation> point_annotations(const Selection&,std::size_t max_points=100000);
Selection with_point_annotations(const Selection&,const std::vector<PointAnnotation>&,
    std::size_t max_points=100000);
Selection without_point_annotations(const Selection&);
void validate_point_annotations(const std::vector<PointAnnotation>&,DatasetShape);
// Explicit ImageJ wire adapters: low8 counter, high24 one-based C-fastest
// stack index; zero means unbound. Refuse out-of-extent and signed-position
// overflow rather than silently reinterpreting a different source layout.
Selection with_imagej_point_counters(const Selection&,const std::vector<std::uint32_t>&,
    DatasetShape,std::size_t max_points=100000);
std::vector<std::uint32_t> imagej_point_counters(const Selection&,DatasetShape,
    std::size_t max_points=100000);
}
