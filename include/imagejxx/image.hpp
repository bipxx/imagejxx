#pragma once

#include <array>
#include <complex>
#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <vector>

namespace imagejxx {

enum class PixelType { UInt8, UInt16, Float32, Float64 };
enum class Border { Nearest, Reflect, Wrap, Constant };
enum class Interpolation { Nearest, Linear, Cubic };

// Owned, interleaved HWC storage. Doubles retain precision between native steps.
// PixelType specifies the output format; Python conversion saturates integers.
struct Image {
    int width, height, channels;
    PixelType type;
    std::vector<double> pixels;
    Image(int width, int height, int channels = 1, PixelType type = PixelType::Float64);
    double& at(int x, int y, int c = 0);
    double at(int x, int y, int c = 0) const;
    double sample(int x, int y, int c, Border border, double constant = 0) const;
};
using Stack = std::vector<Image>;
struct Histogram { std::vector<std::uint64_t> counts; std::vector<double> edges; };
struct Statistics {
    std::uint64_t count = 0;
    double minimum = 0, maximum = 0, mean = 0, stddev = 0;
    double sum = 0, median = 0, integrated_density = 0;
};
struct Labels { int width, height, count; std::vector<std::int32_t> pixels; };
struct Particle {
    int label = 0, x = 0, y = 0, width = 0, height = 0;
    std::uint64_t pixels = 0;
    double area = 0, centroid_x = 0, centroid_y = 0, perimeter = 0;
    double mean = 0, minimum = 0, maximum = 0, integrated_density = 0;
    double eccentricity = 0, circularity = 0;
    bool touches_border = false;
};
struct Spectrum { int width, height; std::vector<std::complex<double>> pixels; };

double type_max(PixelType type);
void require_gray(const Image& image);
void require_same_shape(const Image& a, const Image& b);
void require_finite(double value, const char* name);
void require_radius(int radius);
Border parse_border(const std::string& value);
Interpolation parse_interpolation(const std::string& value);

Image point(const Image&, const std::string& operation, double value = 0);
Image convert(const Image&, PixelType type, bool scale = false);
Image linear(const Image&, double scale, double offset);
Image calculate(const Image&, const Image&, const std::string& operation);
Image normalize(const Image&, double low, double high, double saturation = 0);
Image apply_lut(const Image&, const std::vector<double>& lut);
Image threshold(const Image&, double lower, double upper);
Image in_range(const Image&, const std::vector<double>& lower, const std::vector<double>& upper);
Image fill(const Image&, double value, const Image* mask = nullptr);
Image add_noise(const Image&, const std::string& kind, double amount, std::uint64_t seed);

Image convolve(const Image&, const std::vector<double>& kernel, int kw, int kh,
               Border border = Border::Reflect, double constant = 0, bool normalize = false);
// ImageJ's eight directional 3x3 shadows. Scalar borders replicate pixels;
// RGB borders remain unchanged. Extent is x,y,width,height; finite sources only.
Image shadow_imagej(const Image&, const std::string& direction = "north",
                    std::optional<std::array<int, 4>> extent = std::nullopt);
Image gaussian_blur(const Image&, double sigma_x, double sigma_y, Border border = Border::Reflect);
Image rank_filter(const Image&, int radius, const std::string& operation,
                  bool disk = true, Border border = Border::Reflect, double constant = 0);
Image box_blur(const Image&, int radius, Border border = Border::Reflect);
Image sobel(const Image&, Border border = Border::Reflect);
Image unsharp_mask(const Image&, double sigma, double amount);
Image bilateral_filter(const Image&, int radius, double sigma_spatial, double sigma_range);
Image kuwahara_filter(const Image&, int radius);
Image morphology(const Image&, const std::string& operation, int radius = 1,
                 bool disk = true, Border border = Border::Reflect, double constant = 0);
Image clahe(const Image&, int tile_width = 64, int tile_height = 64, double clip_limit = 2);
Image local_threshold(const Image&, const std::string& method, int radius,
                      double k, double offset, double dynamic_range, double contrast_threshold);
Image subtract_background(const Image&, int radius, bool light_background = false);
Image rolling_ball_background(const Image&, double radius, bool light_background = false);
Image subtract_rolling_ball(const Image&, double radius, bool light_background = false);
Image remove_outliers(const Image&, int radius, double threshold, bool bright = true);
Image anisotropic_diffusion(const Image&, int iterations, double kappa, double step);

Image crop(const Image&, int x, int y, int width, int height);
Image paste(const Image& destination, const Image& source, int x, int y, const Image* mask = nullptr);
Image flip(const Image&, bool horizontal, bool vertical);
Image rotate90(const Image&, int turns = 1);
Image resize(const Image&, int width, int height, Interpolation mode = Interpolation::Linear);
Image affine_transform(const Image&, const std::array<double, 6>& inverse,
                       int width, int height, Interpolation mode = Interpolation::Linear,
                       Border border = Border::Constant, double constant = 0);
Image rotate(const Image&, double degrees, bool expand, Interpolation mode, double constant = 0);
Image bin(const Image&, int factor_x, int factor_y, const std::string& method);
Image resize_canvas(const Image&, int width, int height, int offset_x, int offset_y, double constant);
Image draw_line(const Image&, double x1, double y1, double x2, double y2, double value, double thickness);
Image rectangle_mask(int width, int height, int x, int y, int roi_width, int roi_height);
Image ellipse_mask(int width, int height, double cx, double cy, double rx, double ry);
Image polygon_mask(int width, int height, const std::vector<std::array<double, 2>>& vertices);
Image wand_mask(const Image&, int x, int y, double tolerance = 0, int connectivity = 4);
std::vector<double> line_profile(const Image&, double x1, double y1, double x2, double y2, int samples);

Image grayscale(const Image&);
Image rgb_to_hsv(const Image&);
Image hsv_to_rgb(const Image&, PixelType output_type = PixelType::UInt8);
Stack split_channels(const Image&);
Image merge_channels(const Stack&);
Image color_deconvolution(const Image&, const std::array<double, 9>& stains);

Histogram histogram(const Image&, int bins = 0,
                    double lower = std::numeric_limits<double>::quiet_NaN(),
                    double upper = std::numeric_limits<double>::quiet_NaN());
Statistics statistics(const Image&, const Image* mask = nullptr);
double auto_threshold(const Image&, const std::string& method = "otsu", int bins = 0);
Image auto_mask(const Image&, const std::string& method, bool bright, int bins = 0);
Image equalize_histogram(const Image&);
Image project(const Stack&, const std::string& method);
Stack gaussian_blur3d(const Stack&, double sigma_x, double sigma_y, double sigma_z, Border border);

Labels connected_components(const Image&, int connectivity = 8);
std::vector<Particle> region_properties(const Labels&, const Image* intensity = nullptr,
                                        double spacing_x = 1, double spacing_y = 1);
Image fill_holes(const Image&, int connectivity = 4);
Image skeletonize(const Image&);
Image binary_outline(const Image&);
Image remove_small_objects(const Image&, std::uint64_t min_size, int connectivity = 8);
Image clear_border(const Image&, int connectivity = 8);
Image distance_transform(const Image&, double spacing_x = 1, double spacing_y = 1);
Image local_maxima(const Image&, int radius = 1);
Labels watershed(const Image& height, const Labels& markers, const Image* mask = nullptr, int connectivity = 8);
Labels binary_watershed(const Image&, int maxima_radius = 1, int connectivity = 8);

Spectrum fft2(const Image&, bool pad = true);
Image ifft2(const Spectrum&);
Image power_spectrum(const Spectrum&, bool log_scale = true, bool shift = true);
Image fft_bandpass(const Image&, double low_cutoff, double high_cutoff);

Image read_pnm(const std::string& path);
void write_pnm(const Image&, const std::string& path);
} // namespace imagejxx

