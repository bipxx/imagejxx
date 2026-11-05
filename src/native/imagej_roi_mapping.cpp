#include "imagej_roi_position.hpp"
#include "kernel_control.hpp"
#include <imagejxx/imagej_roi_mapping.hpp>
#include <imagejxx/imagej_roi_projection.hpp>
#include <imagejxx/point_annotations.hpp>
#include <imagejxx/point_presentation.hpp>
#include <imagejxx/roi_style.hpp>
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
MappedImageJRoi annotated_imagej_point_mapping(const MappedImageJRoi &mapped,
                                               const ImageJRoiLimits &limits) {
  validate_imagej_roi_mapping(mapped,limits);
  const auto &source=mapped.source_record().roi.selection;
  const auto& current=mapped.annotation();
  check(mapped.source_mapping().plane==Plane::XY && current.plane==Plane::XY,
        "Scientific point counters require an explicit XY mapping");
  check((source.kind=="point" || source.kind=="multipoint") &&
        (current.selection.kind=="point" || current.selection.kind=="multipoint"),
        "Point annotation promotion requires point geometry");
  check(current.selection.points.size()<=limits.max_vertices,"Point annotation vertex limit");
  if(current.selection.attributes.count(point_annotations_attribute)) {
    validate_point_annotations(point_annotations(current.selection,limits.max_vertices),mapped.target_extent());
    return mapped; // Never overwrite already edited native annotations.
  }
  check(current.selection.kind==source.kind && current.selection.points==source.points,
        "Changed point geometry requires explicitly aligned annotations");
  auto row=current;
  row.selection=with_imagej_point_counters(row.selection,mapped.source_record().point_counters,
                                         mapped.source_mapping().extent,limits.max_vertices);
  validate_point_annotations(point_annotations(row.selection,limits.max_vertices),mapped.target_extent());
  return rebind_imagej_roi_mapping(mapped,mapped.target_extent(),row,limits);
}
ImageJRoi imagej_roi_annotation_projection(const PlanarAnnotation &row,DatasetShape extent,
                                           ImageJRoi out,const ImageJRoiLimits &limits) {
  row.validate(extent);
  check(row.selection.points.size()<=limits.max_vertices,"ROI projection vertex limit");
  if(row.selection.attributes.count(point_annotations_attribute)) {
    check(row.plane==Plane::XY,"Scientific point counters require an explicit XY projection");
    out.point_counters=imagej_point_counters(row.selection,extent,limits.max_vertices);
  }
  auto selection=row.selection;
  for(auto it=selection.attributes.begin();it!=selection.attributes.end();)
    if(it->first!=roi_style_attribute && !(selection.is_rounded_box() && it->first=="corner_diameter"))
      it=selection.attributes.erase(it);
    else ++it;
  // Retain a caller-chosen rounded wire primitive when it still describes the
  // native row; the canonical projection/envelope validators check equality.
  if(selection.is_rounded_box() && out.roi.selection.kind=="mask" &&
      (out.rounded_rectangle || !out.composite_path.empty())) {
    auto retained=out.roi.selection;retained.width=selection.width;
    retained.attributes=selection.attributes;retained.attributes.erase("corner_diameter");
    selection=std::move(retained);
  }
  out.roi={row.name,std::move(selection),row.t,row.c,row.coordinate,{}};
  out.legacy_position=0;
  if(const auto style=point_style(row.selection))out=with_imagej_point_style(std::move(out),*style);
  const auto geometry=planar_annotation_geometry(extent,row.plane);
  return imagej_roi_standard_projection(std::move(out),geometry.width,geometry.height,limits);
}
} // namespace imagejxx
