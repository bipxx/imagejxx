#include "imagej_roi_position.hpp"
#include "kernel_control.hpp"
#include <imagejxx/imagej_roi_mapping.hpp>
#include <imagejxx/imagej_roi_projection.hpp>
#include <stdexcept>
namespace imagejxx {
namespace {
void check(bool v, const char *message) {
  if (!v)
    throw std::invalid_argument(message);
}
PlanarAnnotation resolve(const ImageJRoi &record,
                         const ImageJRoiPlaneMapping &mapping,
                         const ImageJRoiLimits &limits) {
  kernel_checkpoint();
  check(record.properties.size() <= limits.max_text_bytes,
        "ImageJ ROI properties text limit");
  for (std::size_t start = 0; start < record.properties.size();) {
    auto end = record.properties.find('\n', start);
    if (end == std::string::npos)
      end = record.properties.size();
    const auto key = record.properties.find_first_not_of(" \t\r", start);
    check(key >= end ||
              record.properties.compare(key, sizeof("imagejxx.roi.") - 1,
                                        "imagejxx.roi.") != 0,
          "Native ROI envelope requires an explicit envelope decoder");
    start = end < record.properties.size() ? end + 1 : end;
  }
  const auto position = detail::resolve_imagej_roi_position(
      record.roi.t, record.roi.c, record.roi.z, record.legacy_position,
      mapping);
  const auto geometry =
      planar_annotation_geometry(mapping.extent, mapping.plane);
  // Geometry is checked on the declared plane canvas, not a fabricated XY view.
  record.roi.selection.validate(geometry.width, geometry.height);
  // Reuse the complete typed byte-codec admission rules before returning any
  // mapped value. Unsupported extensions/options remain explicit errors.
  (void)encode_imagej_roi(record, limits);
  PlanarAnnotation row{record.roi.name,      record.roi.selection,
                       mapping.plane,        position.coordinate,
                       position.t,           position.c,
                       record.roi.attributes};
  row.validate(mapping.extent);
  kernel_checkpoint();
  return row;
}
} // namespace
MappedImageJRoi map_imagej_roi(const ImageJRoi &record,
                               const ImageJRoiPlaneMapping &mapping,
                               const ImageJRoiLimits &limits) {
  auto row = resolve(record, mapping, limits);
  return MappedImageJRoi(record, mapping, mapping.extent, std::move(row));
}
MappedImageJRoi
decode_imagej_roi_mapped(const std::vector<unsigned char> &bytes,
                         const ImageJRoiPlaneMapping &mapping,
                         const ImageJRoiLimits &limits) {
  const auto geometry =
      planar_annotation_geometry(mapping.extent, mapping.plane);
  return map_imagej_roi(
      decode_imagej_roi(bytes, limits, geometry.width, geometry.height),
      mapping, limits);
}
MappedImageJRoi decode_imagej_composite_roi_mapped(
    const std::vector<unsigned char> &bytes,
    const ImageJRoiPlaneMapping &mapping, const ImageJRoiLimits &limits) {
  return map_imagej_roi(decode_imagej_composite_roi(bytes, limits), mapping, limits);
}
MappedImageJRoi rebind_imagej_roi_mapping(const MappedImageJRoi &original,
                                          DatasetShape target,
                                          const PlanarAnnotation &replacement,
                                          const ImageJRoiLimits &limits) {
  validate_imagej_roi_mapping(original, limits);
  target.validate();
  replacement.validate(target);
  kernel_checkpoint();
  return MappedImageJRoi(original.record_, original.mapping_, target,
                         replacement);
}
void validate_imagej_roi_mapping(const MappedImageJRoi &original,
                                 const ImageJRoiLimits &limits) {
  (void)resolve(original.source_record(), original.source_mapping(), limits);
  original.annotation().validate(original.target_extent());
}
MappedImageJRoi editable_imagej_roi_mapping(const MappedImageJRoi &mapped,
                                            const ImageJRoiLimits &limits) {
  validate_imagej_roi_mapping(mapped,limits);
  auto row = mapped.annotation();
  if (row.selection.is_rounded_box()) return mapped;
  const auto &source = mapped.source_record();
  check(bool(source.rounded_rectangle),"Editable rounded mapping requires a retained rounded primitive");
  check(row.selection.kind == "mask" && row.selection == source.roi.selection,
        "Current geometry differs from retained rounded primitive");
  const auto &box = *source.rounded_rectangle;
  const double x=double(float(box[0])),y=double(float(box[1])),
               w=double(float(box[2])),h=double(float(box[3]));
  Selection candidate("rounded_rectangle",{{x,y},{x+w,y+h}},row.selection.width);
  candidate.attributes=row.selection.attributes;
  check(!candidate.attributes.count("corner_diameter"),"Rounded promotion has conflicting geometry metadata");
  candidate.attributes["corner_diameter"]=std::int64_t(box[4]);
  const auto geometry=planar_annotation_geometry(mapped.target_extent(),row.plane);
  candidate.validate(geometry.width,geometry.height);
  const auto path=candidate.rounded_path(true);
  const auto mask=imagej_shape_mask(std::vector<double>(path.begin(),path.end()),geometry.width,geometry.height,limits);
  check(mask.packed_mask()==row.selection.packed_mask(),"Rounded promotion changes processing membership");
  row.selection=std::move(candidate);
  return rebind_imagej_roi_mapping(mapped,mapped.target_extent(),row,limits);
}
} // namespace imagejxx
