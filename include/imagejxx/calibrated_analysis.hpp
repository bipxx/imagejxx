#pragma once
#include <imagejxx/dataset.hpp>
#include <imagejxx/imagej_intensity_calibration.hpp>

namespace imagejxx {
// ImageJ1 scalar statistics, distinct from the existing raw/population statistics.
// Histograms retain raw processor bins; density affects statistics/sample values.
struct ImageJScalarStatistics {
  std::uint64_t pixel_count = 0;
  double area = 0, mean = 0, raw_mean = 0, stddev = 0;
  double minimum = 0, maximum = 0, median = 0, mode_value = 0;
  int mode_index = 0;
  double integrated_density = 0;
  std::vector<std::uint64_t> histogram, histogram16;
};
// Scalar UInt8/UInt16 and finite, canonical widened Float32. Mask is full-size
// scalar finite nonzero membership. No display threshold/range restriction.
// Integer upper-rank median, sample stddev and dtype-specific non-finite lookup
// behavior follow ImageJ1.54r. Known version1 function provenance evaluates the
// median's double formula; custom/future table provenance uses its table value.
// raw_mean follows ImageJ.umean, including UInt16's calibrated-mean behavior.
// Float32 median averages its two middle samples.
// Source/mask are borrowed for this synchronous call; no source is retained.
// Declared bound covers histogram/table/median scratch and returned vectors,
// not caller-owned inputs, allocator overhead, Python conversion copies or RSS.
ImageJScalarStatistics imagej_calibrated_statistics(const Image&,
    const SpatialCalibration&, const ImageJIntensityCalibration&,
    const Image* mask = nullptr,
    std::size_t max_working_bytes = 128ULL * 1024 * 1024);
// ImageProcessor.getLine/ProfilePlot scalar, single-pixel-width straight line.
// Default samples = round(Euclidean pixel length)+1, including a zero-length
// line. Density lookup precedes interpolation. No bicubic/wide/polyline modes.
// Coordinates must be finite and within the source image. This explicit domain
// avoids claiming ImageJ's separate outside-image border behavior.
std::vector<double> imagej_calibrated_line_profile(const Image&,
    const ImageJIntensityCalibration&, double x1, double y1, double x2, double y2,
    bool interpolate = true,
    std::size_t max_working_bytes = 128ULL * 1024 * 1024);
}
