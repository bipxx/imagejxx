#pragma once
#include <imagejxx/imagej_roi.hpp>

namespace imagejxx {
// Immutable absolute M/L/Q/C/Z commands in the ImageJ ShapeRoi domain.
// Curves, subpaths, implicit closures and even-odd fill remain continuous;
// rasterization is an explicit operation and never replaces these commands.
class ImageJCompositePath {
public:
  explicit ImageJCompositePath(const std::vector<double> &commands,
                               const ImageJRoiLimits &limits = {});
  const std::vector<double> &commands() const { return *commands_; }
  std::size_t segment_count() const { return segments_; }
  // min-x, min-y, max-x, max-y, including isolated move points. Curve extrema
  // are evaluated analytically rather than using the control-point hull.
  std::array<double, 4> bounds() const { return bounds_; }
  // Continuous even-odd fill, using monotone curve intervals and bounded root
  // isolation. Boundary classification follows half-open ray crossings.
  bool contains(std::array<double, 2> point) const;
  // Row-major [a,b,tx,c,d,ty], finite and invertible. No float32 quantization.
  ImageJCompositePath transformed(const std::array<double, 6> &matrix,
                                  const ImageJRoiLimits &limits = {}) const;
  Selection mask(int width, int height, const ImageJRoiLimits &limits = {}) const;

private:
  std::shared_ptr<const std::vector<double>> commands_;
  std::size_t segments_ = 0;
  std::array<double, 4> bounds_{};
};

// Validates the retained path against the source processing mask, transforms
// every control point, then explicitly rasterizes onto the requested canvas.
// Source geometry and metadata are never modified. Off-canvas contours survive
// in composite_path. This is an interchange-record operation, not a document
// edit/history command. Wire export still quantizes coordinates to binary32.
ImageJRoi transform_imagej_composite_roi(const ImageJRoi &source,
                                         const std::array<double, 6> &matrix, int width,
                                         int height,
                                         const ImageJRoiLimits &limits = {});
} // namespace imagejxx
