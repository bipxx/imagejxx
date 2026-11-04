#pragma once
#include <imagejxx/calibrated_analysis.hpp>

namespace imagejxx {
struct ImageJLineMeasurement {
  ImageJScalarStatistics statistics;
  double length = 0, angle_degrees = 0;
  std::array<double,3> midpoint_world{};
};
// ImageJ Analyzer thin straight-line intensity summaries: original automatic
// profile sampling, calibrated before interpolation, then Float32 conversion
// before statistics. This is separate from the editor's configured path API.
// Finite UInt8/UInt16/Float32 input and finite Float32-representable calibrated
// samples; endpoints inside the image. No threshold/redirect/wide-line modes.
// Length and midpoint use the native spatial affine (XY plane at voxel Z=0).
// Heading is ImageJ's image-axis angle atan2(-dy*sy,dx*sx), in [-180,180].
// Non-identity direction/origin are native affine extensions, not an ImageJ
// world-coordinate parity claim. ImageJ calibration-origin/invert-Y conventions
// need an explicit adapter; this function uses SpatialCalibration's contract.
// Working bound covers profile, decoded lookup, median/histogram scratch and
// returned statistics, excluding borrowed source, Python copies and RSS.
ImageJLineMeasurement imagej_calibrated_line_measurement(const Image&,
    const SpatialCalibration&, const ImageJIntensityCalibration&,
    double x1,double y1,double x2,double y2,bool interpolate=true,
    std::size_t max_working_bytes=128ULL*1024*1024);
}
