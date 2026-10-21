#pragma once
#include <imagejxx/imagej_roi.hpp>
#include <imagejxx/planar_annotations.hpp>

namespace imagejxx {
enum class ImageJZPositionMapping { ScientificZ, FixedAxis };
enum class ImageJLegacyPositionMapping { Reject, FixedAxis, HyperstackCZT };
struct ImageJRoiPlaneMapping {
  DatasetShape extent;
  Plane plane = Plane::XY;
  std::optional<int> coordinate;
  ImageJZPositionMapping z_position = ImageJZPositionMapping::ScientificZ;
  ImageJLegacyPositionMapping legacy_position =
      ImageJLegacyPositionMapping::Reject;
};
// Pure descriptor/geometry mapping. Source record and original mapping stay
// immutable, including opaque byte-codec metadata. No document/dataset sample
// access; codec admission can rasterize owned annotation mask geometry.
class MappedImageJRoi {
public:
  const ImageJRoi &source_record() const { return record_; }
  const ImageJRoiPlaneMapping &source_mapping() const { return mapping_; }
  const DatasetShape &target_extent() const { return target_; }
  const PlanarAnnotation &annotation() const { return annotation_; }

private:
  ImageJRoi record_;
  ImageJRoiPlaneMapping mapping_;
  DatasetShape target_;
  PlanarAnnotation annotation_;
  MappedImageJRoi(ImageJRoi record, ImageJRoiPlaneMapping mapping,
                  DatasetShape target, PlanarAnnotation annotation)
      : record_(std::move(record)), mapping_(std::move(mapping)),
        target_(target), annotation_(std::move(annotation)) {}
  friend MappedImageJRoi map_imagej_roi(const ImageJRoi &,
                                        const ImageJRoiPlaneMapping &,
                                        const ImageJRoiLimits &);
  friend MappedImageJRoi rebind_imagej_roi_mapping(const MappedImageJRoi &,
                                                   DatasetShape,
                                                   const PlanarAnnotation &,
                                                   const ImageJRoiLimits &);
};
void validate_imagej_roi_mapping(const MappedImageJRoi &,
                                 const ImageJRoiLimits & = {});
// Self-describing ASCII property envelope for an unchanged source binding.
// Standard geometry retains ImageJ wire precision; this is not a native-session
// serializer. Rebound rows require an explicit new standard projection and are
// refused here. Decode validates the original extent and standard metadata
// before returning a mapped record. No fallback for absent/unknown envelopes.
std::vector<unsigned char>
encode_imagej_roi_envelope(const MappedImageJRoi &,
                           const ImageJRoiLimits & = {});
MappedImageJRoi decode_imagej_roi_envelope(const std::vector<unsigned char> &,
                                           const ImageJRoiLimits & = {});
// Version2 carries the current native row (precision and opaque attributes),
// target extent, original wire provenance and an explicit standard projection.
// Projection must encode the row's geometry/style/name/T/C/fixed coordinate;
// standard Z names the fixed axis, including XZ/YZ. Legacy position must be0.
// Wire-only sidecars/options/counters/properties are explicitly caller chosen.
std::vector<unsigned char>
encode_imagej_roi_native_envelope(const MappedImageJRoi &,
                                  const ImageJRoi &standard_projection,
                                  const ImageJRoiLimits & = {});
MappedImageJRoi
decode_imagej_roi_native_envelope(const std::vector<unsigned char> &,
                                  const ImageJRoiLimits & = {});
MappedImageJRoi map_imagej_roi(const ImageJRoi &, const ImageJRoiPlaneMapping &,
                               const ImageJRoiLimits & = {});
MappedImageJRoi decode_imagej_roi_mapped(const std::vector<unsigned char> &,
                                         const ImageJRoiPlaneMapping &,
                                         const ImageJRoiLimits & = {});
// Complete explicit replacement. Validate original source and current binding
// before target/replacement; never infer/crop/reinterpret coordinates on
// rebind.
MappedImageJRoi rebind_imagej_roi_mapping(const MappedImageJRoi &, DatasetShape,
                                          const PlanarAnnotation &,
                                          const ImageJRoiLimits & = {});
} // namespace imagejxx
