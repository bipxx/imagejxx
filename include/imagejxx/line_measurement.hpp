#pragma once
#include <imagejxx/calibrated_analysis.hpp>

namespace imagejxx {
struct Selection;
// PolygonRoi.getLength for ordinary polygon/freehand/polyline subtypes, including
// FREELINE smoothing, automatic traced polygons and explicit TRACED_ROI.
// Constructor precision is explicit: integer coordinates must be integral Int32;
// Float32-relative coordinates are the default. A float TRACED_ROI returns NaN,
// as upstream does when its integer coordinate array is absent. No source reads.
// Working bound is 8192 + 16*vertex_count bytes, excluding borrowed input and RSS.
// The spatial affine transforms segment vectors; traced correction uses axis
// lengths. Origin is irrelevant. 1..65536 vertices (SelectionEditLimits default);
// other ROI kinds are refused.
double imagej_calibrated_polygon_length(const Selection&,const SpatialCalibration&,
    bool integer_coordinates=false,std::size_t max_working_bytes=128ULL*1024*1024);

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
struct ImageJPolylineMeasurement {
  ImageJScalarStatistics statistics;
  double length=0;
  std::array<int,4> bounds{};
};
// Original irregular ProfilePlot sampling, distinct from configured editor
// paths. Float32-relative PolygonRoi vertices, 0.01-pixel coordinate recurrence,
// original sampling trigger, then Float32 conversion for Analyzer statistics.
// Thin in-image paths, 2..4096 vertices; work is admitted before source scans.
ImageJPolylineMeasurement imagej_calibrated_polyline_measurement(const Image&,
    const SpatialCalibration&,const ImageJIntensityCalibration&,
    const std::vector<std::array<double,2>>&,bool interpolate=true,
    std::size_t max_working_bytes=128ULL*1024*1024,
    std::size_t max_work_steps=32ULL*1024*1024);
std::vector<double> imagej_calibrated_irregular_profile(const Image&,
    const ImageJIntensityCalibration&,const std::vector<std::array<double,2>>&,
    bool interpolate=true,std::size_t max_working_bytes=128ULL*1024*1024,
    std::size_t max_work_steps=32ULL*1024*1024);
// Open-line integer drawing bounds (not an area mask). Vertices of PolygonRoi
// are normalized separately by the polyline measurement above.
std::array<int,4> imagej_line_bounds(const std::array<double,4>&);
// Recompute ImageJ's calibrated image-axis interior angle from Float32 points,
// as its geometry refresh does; not the stale programmatic constructor cache.
double imagej_calibrated_angle(const std::array<std::array<double,2>,3>&,
    const SpatialCalibration&,bool reflex=false);
}
