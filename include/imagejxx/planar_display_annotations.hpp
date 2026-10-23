#pragma once
#include <imagejxx/planar_annotations.hpp>

namespace imagejxx {
inline constexpr const char *planar_display_annotations_extension =
    "imagejxx.planar_display_annotations";
// Display payloads are independent of processing selections. Bounds use plane
// image-cell boundaries; off-canvas content is retained for clipped rendering.
struct TextAnnotationPayload {
  std::array<double, 4> bounds{0, 0, 1, 1};
  std::string text, font_name = "SansSerif";
  double font_size = 14, angle = 0;
  unsigned char font_style = 0, justification = 0;
  bool draw_string_mode = false;
  std::uint32_t color = 0xff000000;
  std::optional<std::uint32_t> background;
  void validate() const;
  bool operator==(const TextAnnotationPayload &) const;
};
struct ImageAnnotationPayload {
  std::array<double, 4> bounds{0, 0, 1, 1};
  int width = 1, height = 1, channels = 4;
  std::vector<unsigned char> pixels{0, 0, 0, 255};
  unsigned char opacity = 255;
  bool zero_transparent = false;
  void validate() const;
  bool operator==(const ImageAnnotationPayload &) const;
};
using DisplayAnnotationPayload =
    std::variant<TextAnnotationPayload, ImageAnnotationPayload>;
struct PlanarDisplayAnnotation {
  std::string name;
  DisplayAnnotationPayload payload;
  Plane plane = Plane::XY;
  int coordinate = 0;
  std::optional<int> t, c;
  MetadataObject attributes;
  // Payload-local boundary coordinates to plane-local boundaries, row-major
  // 2x3 affine transform. Payload text/style and source image bytes stay exact.
  // Coefficients must be finite, within +/-1e9, and have nonzero determinant.
  std::array<double, 6> transform{1, 0, 0, 0, 1, 0};
  bool has_transform() const;
  void validate_transform() const;
  std::array<double, 2> map_point(std::array<double, 2>) const;
  std::array<double, 2> unmap_point(std::array<double, 2>) const;
  void validate(DatasetShape) const;
  bool operator==(const PlanarDisplayAnnotation &) const;
};
// V1 is closed to affine fields; V2 requires at least one nonidentity transform
// and carries complete transforms on every row. All six dimensions are bound.
// At most4096 rows,1MiB text/row,
// 1Mi image pixels/row (RGB8/RGBA8),16MiB logical encoded metadata,1M nodes.
// No font discovery/layout, rasterization, filesystem or source pixel reads.
MetadataValue
encode_planar_display_annotations(DatasetShape,
                                  const std::vector<PlanarDisplayAnnotation> &);
std::vector<PlanarDisplayAnnotation>
decode_planar_display_annotations(DatasetShape, const MetadataValue &);
std::vector<PlanarDisplayAnnotation>
planar_display_annotations(const DocumentData &);
void validate_planar_display_annotations(const DocumentData &);
bool planar_display_annotation_visible(const PlanarDisplayAnnotation &, Plane,
                                       int coordinate, int t, int c);
DocumentTransaction
prepare_store_planar_display_annotation(const DocumentSnapshot &,
                                        const PlanarDisplayAnnotation &);
DocumentTransaction
prepare_update_planar_display_annotation(const DocumentSnapshot &, std::size_t,
                                         const PlanarDisplayAnnotation &);
DocumentTransaction
prepare_delete_planar_display_annotation(const DocumentSnapshot &, std::size_t);
} // namespace imagejxx
