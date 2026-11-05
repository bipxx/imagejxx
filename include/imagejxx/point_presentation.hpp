#pragma once
#include <imagejxx/imagej_roi.hpp>
#include <imagejxx/roi_style.hpp>

namespace imagejxx {
inline constexpr const char* point_style_attribute = "imagejxx.point_style";
enum class PointMarker { Hybrid, Cross, Dot, Circle };
struct PointStyle {
  PointMarker marker = PointMarker::Hybrid;
  unsigned size = 1; // ImageJ size index, NOT Selection::width/raster diameter.
  bool labels = true, show_all = false;
  RoiColor cross_color{255,255,255,255};
  std::map<unsigned,RoiColor> counter_colors;
};
std::optional<PointStyle> point_style(const Selection&);
Selection with_point_style(const Selection&,const PointStyle&);
Selection without_point_style(const Selection&);
// Codes 0..9 match ImageJ; 10..99 use a deterministic native palette, since
// ImageJ generates random colors per process. Persist overrides for exact color.
RoiColor point_counter_color(unsigned counter);
// Explicit semantic adapters. Raw codec transport still accepts arbitrary bytes.
PointStyle imagej_point_style(const ImageJRoi&);
// Only point_type/point_size/SHOW_LABELS are representable. Other style fields
// stay in native metadata; all unrelated wire options/fields are preserved.
ImageJRoi with_imagej_point_style(ImageJRoi,const PointStyle&);

struct PointPresentationOptions {
  std::array<double,2> origin{0,0}, scale{1,1};
  double marker_scale = 1; // Device/DPI scale; independent of anchor zoom.
  RoiColor color{255,255,0,255};
  std::optional<PlanePosition> position;
  Plane plane = Plane::XY;
  bool labels = true, show_all = false, round_centers = true;
  // Emit each primitive relative to its glyph center. Useful for immutable
  // UI templates: anchors can pan/zoom while marker geometry stays in device units.
  bool relative_primitives = false;
  // Optional device-space left,top,right,bottom. Parent-row eligibility remains
  // the caller's responsibility. XZ/YZ per-point visibility compares T/C only.
  std::optional<std::array<double,4>> clip;
};
struct PointPresentationLimits {
  std::size_t max_points=100000, max_primitives=500000,
      max_label_chars=1000000, max_working_bytes=256*1048576;
};
enum class PointPrimitiveKind { Line, FillRectangle, FillEllipse, StrokeEllipse, Text };
struct PointPrimitive {
  PointPrimitiveKind kind=PointPrimitiveKind::Line;
  // Line: endpoints; shapes: left,top,right,bottom; text: baseline x,y,0,0.
  std::array<double,4> coordinates{};
  RoiColor color{};
  double stroke_width=1, font_size=0;
  std::string text;
};
struct PointGlyph {
  std::size_t index=0, first_primitive=0, primitive_count=0;
  unsigned counter=0;
  std::array<double,2> center{};
  std::optional<PlanePosition> position;
  bool visible(std::optional<PlanePosition> current,Plane plane=Plane::XY,
               bool show_all=false) const;
};
struct PointPresentation {
  std::vector<PointGlyph> glyphs;
  std::vector<PointPrimitive> primitives;
  std::size_t working_bytes=0;
};
// Pure bounded layout, no pixels/provider access, mutation or geometry clone.
// Missing style uses PointStyle defaults only for this explicit API. The budget
// covers native staging/output, not allocator overhead, Python copies or RSS.
// Geometry/stroke/label baseline descriptors do not assert font/AA pixel parity.
PointPresentation point_presentation(const Selection&,
    const PointPresentationOptions& = {}, const PointPresentationLimits& = {});
}
