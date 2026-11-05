#pragma once
#include <imagejxx/point_presentation.hpp>
#include <imagejxx/execution.hpp>

namespace imagejxx {
// Decimal label coverage prepared by the owning font resource, at the exact
// device font_size requested by PointPresentation. No font IO or bundled asset.
struct PointRasterGlyph {
  int width=0, height=0, offset_x=0, offset_y=0; // Relative to baseline.
  double advance=0;
  std::vector<unsigned char> coverage; // Row-major alpha bytes, width*height.
};
struct PointRasterFont {
  double font_size=0;
  std::array<PointRasterGlyph,10> digits;
};
struct PointRasterLimits {
  PointPresentationLimits presentation;
  std::size_t max_working_bytes=256*1048576;
  std::size_t max_pixel_samples=128*1048576;
};
// Detached uint8 RGB display result, never a processing mask. Shapes use a
// deterministic 4x4 box coverage grid; decimal glyphs use supplied coverage.
// Primitive order (including duplicates) is preserved. Each blend rounds with
// floor(background*(1-alpha)+foreground*alpha+.5). Clip applies to every sample.
// A matching font is required only for emitted text. Parent-row eligibility is
// the caller's responsibility. No provider/font reads or metadata mutation.
// Budget covers borrowed native source/font, detached output, layout/staging;
// excludes allocator overhead, Python array transport and process RSS.
Image render_point_style(const Image& rgb,const Selection&,
    const PointPresentationOptions& = {},const PointRasterFont* font=nullptr,
    const PointRasterLimits& = {},const ExecutionControl& = {});
// Explicit generic display context. Tagged points resolve ROI stroke/fallback
// once and use the point raster (options.color is replaced by that resolution).
// Untagged geometry retains legacy render_roi_style semantics: point-only
// options/font/limits are ignored; no bounded legacy-mask claim is made.
// Both routes honor inherited/control cancellation and return detached pixels.
// No font IO. Existing three-argument calls remain unchanged.
Image render_roi_style(const Image& rgb,const Selection&,RoiColor fallback,
    const PointPresentationOptions& point_options,const PointRasterFont* font=nullptr,
    const PointRasterLimits& point_limits={},const ExecutionControl& = {});
}
