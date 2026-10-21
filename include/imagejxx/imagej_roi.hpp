#pragma once
#include <imagejxx/document.hpp>

namespace imagejxx {
struct ImageJRoiLimits {
  std::size_t max_bytes = 16 * 1024 * 1024, max_vertices = 65536,
              max_text_bytes = 1024 * 1024;
  std::size_t max_mask_pixels = 16 * 1024 * 1024,
              max_path_work = 64 * 1024 * 1024;
  std::size_t max_image_pixels = 4 * 1024 * 1024;
};
struct ImageJArrow {
  std::array<double, 4> endpoints{0, 0, 1, 1};
  unsigned char style = 0, head_size = 10;
};
struct ImageJSpline {
  std::vector<std::array<double, 2>> control_points;
  unsigned char type =
      0; // polygon0, freeline4, polyline5, freehand7, traced8, angle9
  bool integer_coordinates = false;
};
// Binary ImageJ positions are 1-based/zero-wildcard. Roi positions remain
// zero-based/optional. The ambiguous legacy stack position is retained
// separately and is never silently interpreted as a scientific Z coordinate.
// Text annotation transport. Wire bounds precede platform-dependent font
// layout; this type has no implicit conversion to a processing selection.
struct ImageJTextRoi {
  std::array<double, 4> bounds{0, 0, 1, 1}; // stored x,y,width,height
  std::string text, font_name = "SansSerif", name, properties;
  int font_size = 14, legacy_position = 0;
  unsigned char font_style = 0, justification = 0, group = 0;
  bool draw_string_mode = false;
  double angle = 0, stroke_width = 0;
  std::uint32_t stroke_color = 0, fill_color = 0, overlay_label_color = 0;
  std::uint16_t options = 8192, overlay_font_size = 0;
  std::optional<int> t, c, z;
};
ImageJTextRoi decode_imagej_text_roi(const std::vector<unsigned char> &,
                                     const ImageJRoiLimits & = {});
std::vector<unsigned char> encode_imagej_text_roi(const ImageJTextRoi &,
                                                  const ImageJRoiLimits & = {});
// Embedded display pixels, canonical RGB8. Retained TIFF is checked against
// pixels on export; clear it explicitly after editing to generate a new TIFF.
struct ImageJImageRoi {
  Image pixels{1, 1, 3, PixelType::UInt8};
  std::vector<unsigned char> serialized_image;
  std::array<int, 2> origin{0, 0};
  unsigned char opacity = 255, group = 0;
  bool zero_transparent = false;
  std::string name, properties;
  int legacy_position = 0;
  double stroke_width = 0;
  std::uint32_t stroke_color = 0, fill_color = 0, overlay_label_color = 0;
  std::uint16_t options = 8192, overlay_font_size = 0;
  std::optional<int> t, c, z;
};
ImageJImageRoi decode_imagej_image_roi(const std::vector<unsigned char> &,
                                       const ImageJRoiLimits & = {});
std::vector<unsigned char>
encode_imagej_image_roi(const ImageJImageRoi &, const ImageJRoiLimits & = {});
struct ImageJRoi {
  Roi roi{"", Selection("point"), {}, {}, {}, {}};
  std::string properties;
  int legacy_position = 0;
  std::uint16_t options = 8192;
  unsigned char group = 0, point_type = 0, point_size = 0;
  std::uint32_t overlay_label_color = 0;
  std::uint16_t overlay_font_size = 0;
  std::vector<std::uint32_t> point_counters;
  std::optional<unsigned char> source_type;
  std::uint16_t source_subtype = 0;
  // ImageJ's programmatic RotatedRectRoi may use polygon wire type0. Its
  // decoder then uses the polygon payload and ignores these tagged parameters.
  // Retain them explicitly without pretending the result is an active box.
  std::optional<std::array<double, 5>> ignored_rotated_parameters;
  // Lossless wire path: move0/line1/quad2/cubic3/close4, followed by
  // 2/2/4/6/0 absolute coordinates. Even-odd fill. A decoded composite
  // has an explicit canvas mask; export checks it against this retained path.
  std::vector<double> composite_path;
  // x,y,width,height,integer corner diameter. Processing selection is an
  // explicit canvas mask, checked against these retained parameters on export.
  std::optional<std::array<double, 5>> rounded_rectangle;
  std::optional<ImageJArrow> arrow;
  std::optional<ImageJSpline> spline;
};
Selection imagej_shape_mask(const std::vector<double> &path, int width,
                            int height, const ImageJRoiLimits & = {});
// Continuous editable-box contour: four lines/four cubics, closed, fixed storage.
// Uses the frame's orthogonal axes, including rotation/reflection. Diameter is
// a nonnegative integer in image units; each arc is clamped to its box extent.
// Coordinates must remain within the ShapeRoi +/-1e9 domain. No raster/provider
// access or callbacks occur. Unlike the wire-compatible mask helper below,
// this does not quantize the frame to ImageJ's integer processing rectangle.
std::array<double, 44> rounded_rectangle_path(const SelectionFrame &,
                                             int corner_diameter);
Selection imagej_rounded_rectangle_mask(const std::array<double, 5> &,
                                        int width, int height,
                                        const ImageJRoiLimits & = {});
// Processing getMask semantics: double-headed changes the tail inset; the
// separate flipped head/outline drawing operations are not this mask contract.
Selection imagej_arrow_mask(const ImageJArrow &, double stroke_width, int width,
                            int height, bool double_headed = false,
                            const ImageJRoiLimits & = {});
Selection imagej_spline_selection(const ImageJSpline &,
                                  const ImageJRoiLimits & = {});
// Metadata/geometry only. No filesystem, image samples, document edits or Java.
// Supported versions217..228. Unmodeled geometry/options refuse explicitly.
// Float coordinates use the ImageJ wire's finite IEEE754 binary32 precision.
ImageJRoi decode_imagej_roi(const std::vector<unsigned char> &,
                            const ImageJRoiLimits & = {}, int mask_width = 0,
                            int mask_height = 0);
std::vector<unsigned char> encode_imagej_roi(const ImageJRoi &,
                                             const ImageJRoiLimits & = {});
struct ImageJRoiZipEntry {
  std::string name;
  std::vector<unsigned char> data;
};
struct ImageJRoiZipLimits {
  std::size_t max_archive_bytes = 64 * 1024 * 1024, max_entries = 4096,
              max_entry_bytes = 16 * 1024 * 1024,
              max_total_bytes = 64 * 1024 * 1024, max_name_bytes = 1024,
              max_working_bytes = 192 * 1024 * 1024;
};
// Complete raw member transport, not semantic ROI/plane acceptance. Members
// have flat portable .roi names and Iout headers; use the typed codecs after
// choosing canvas/plane mapping. No filesystem extraction or omitted members.
std::vector<ImageJRoiZipEntry>
decode_imagej_roi_zip(const std::vector<unsigned char> &,
                      const ImageJRoiZipLimits & = {});
std::vector<unsigned char>
encode_imagej_roi_zip(const std::vector<ImageJRoiZipEntry> &,
                      const ImageJRoiZipLimits & = {}, bool compress = true);
} // namespace imagejxx
