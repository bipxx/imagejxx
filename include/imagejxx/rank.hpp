#pragma once

#include <imagejxx/image.hpp>
#include <optional>
#include <string>
#include <vector>

namespace imagejxx {

enum class RankFilterMethod { Minimum, Maximum, Median, Mean, Variance };

// Exact, lowercase public method names. Unknown strings and enum values throw.
RankFilterMethod parse_rank_filter_method(const std::string &method);
std::string rank_filter_method_name(RankFilterMethod method);

// Half-open write rectangle in original image coordinates. The surrounding
// source image remains available to the filter, including outside this extent.
struct RankFilterExtent {
  int x;
  int y;
  int width;
  int height;
};

struct RankFilterKernel {
  // One inclusive horizontal half-width for each row, ordered from
  // dy=-half_widths.size()/2 through dy=+half_widths.size()/2.
  std::vector<int> half_widths;
  std::size_t point_count;
};

// ImageJ 1.54r's circular fractional-radius footprint. Radius must be finite
// and in [0,1000]. In this profile radius zero is a five-point cross.
RankFilterKernel rank_filter_kernel_imagej(double radius);

// Additive ImageJ 1.54r finite scalar profile. Supports canonical UInt8/UInt16
// grayscale and finite, exactly represented Float32 grayscale samples. Integer
// tags require integral values in range and positive zero; Float32 preserves
// either zero sign. Other types and channel counts are rejected.
//
// Uses nearest image-edge extension and original-source halo reads. A missing
// extent selects the whole image; an empty valid extent preserves all samples.
// Radius is finite and in [0,1000], restricted to [0,100] for Median. The
// original rank_filter API and its numerical behavior remain independent.
// Variance retains the input type, saturating UInt8/UInt16 to their native
// range. Nonfinite Float32 results throw before returning an output. This pixel
// API does not apply the plugin final display stretch; editor commands pair
// that policy with their pixel transaction and validate display-state admission.
Image rank_filter_imagej(
    const Image &source, RankFilterMethod method = RankFilterMethod::Median,
    double radius = 2.0,
    std::optional<RankFilterExtent> extent = std::nullopt);

// ImageJ Despeckle preset: Median with the fixed radius-one nine-point
// footprint. Same finite scalar types, original-source halo and extent rules.
Image despeckle_imagej(
    const Image &source,
    std::optional<RankFilterExtent> extent = std::nullopt);

// ImageJ Remove Outliers finite scalar pixel profile. Bright/Dark are display
// polarities: an inverted LUT reverses their numeric direction. The threshold
// is nonnegative, finite and representable as finite Float32; comparisons use
// its Float32 rounding and strict inequality, matching the plugin. Radius is
// finite in [0,100]. Extent and halo rules match rank_filter_imagej.
Image remove_outliers_imagej(
    const Image &source, double radius = 2.0, double threshold = 50.0,
    const std::string &which = "bright", bool inverted_lut = false,
    std::optional<RankFilterExtent> extent = std::nullopt);

// ImageJ Remove NaNs Float32 grayscale pixel profile. Accepts canonical finite
// samples and every binary32 NaN/infinity, preserving untouched payloads/zeros.
// Only NaN centers are replaced; all-NaN neighborhoods produce canonical NaN.
// Uses original-source halos, nearest-edge extension, and ImageJ's NaN-aware
// median/row-guess selection. Radius is finite in [0,100]. Infinity centers
// remain unchanged. Caller restores irregular masks.
Image remove_nans_imagej(
    const Image &source, double radius = 2.0,
    std::optional<RankFilterExtent> extent = std::nullopt);

// ImageJ Top Hat finite scalar pixel profile. Light background XOR inverted LUT
// selects closing (maximum then minimum); otherwise opening (minimum then
// maximum). dont_subtract returns that background. Subtraction is Float32;
// closing adds 255/65535 for integer types and zero for Float32. The first
// pass expands the write extent for the second-pass halo. Samples outside the
// final extent remain exact source values; irregular masks are applied after
// both passes by the caller. Radius is finite in [0,1000]. This pixel API does
// not apply the plugin final display stretch or threshold changes.
Image top_hat_imagej(
    const Image &source, double radius = 2.0, bool light_background = false,
    bool dont_subtract = false, bool inverted_lut = false,
    std::optional<RankFilterExtent> extent = std::nullopt);

} // namespace imagejxx
