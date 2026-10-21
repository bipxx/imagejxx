#pragma once
#include <imagejxx/document.hpp>

namespace imagejxx {
using RoiColor = std::array<unsigned char, 4>;
inline constexpr const char *roi_style_attribute = "imagejxx.roi_style";
struct RoiStyle {
  bool present = false, has_stroke = false, has_fill = false;
  std::optional<RoiColor> stroke_color, fill_color;
  std::optional<double> stroke_width;
};
struct ResolvedRoiStyle {
  bool styled = false, cosmetic = false;
  std::optional<RoiColor> stroke_color, fill_color;
  double stroke_width = 1;
};
// ImageJ colors: named colors, #RRGGBB or #AARRGGBB. "none" is null;
// transparent colors retain their RGBA value. Legacy color uses #RRGGBBAA.
std::optional<RoiColor> parse_roi_color(const std::string &value);
std::string roi_color_string(const std::optional<RoiColor> &value);
bool selection_is_area(const Selection &selection);
RoiStyle roi_style(const Selection &selection);
// Recognized patch colors accept public strings, canonical RGBA arrays or null.
// Fill is set before stroke. Unknown fields and retained legacy values survive.
Selection with_roi_style(const Selection &selection,
                         const MetadataObject &patch);
ResolvedRoiStyle resolve_roi_style(const Selection &selection,
                                   RoiColor fallback = {255, 213, 75, 220});
double selection_processing_width(const Selection &selection);
Image roi_stroke_mask(const Selection &selection, int width, int height);
// Flatten onto a detached uint8 RGB display image. Each component is rounded
// with floor(background*(1-alpha)+foreground*alpha+0.5), fill before stroke.
Image render_roi_style(const Image &rgb, const Selection &selection,
                       RoiColor fallback = {255, 204, 0, 255});
} // namespace imagejxx
