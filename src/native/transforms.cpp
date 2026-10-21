#include "imagejxx/image.hpp"
#include "kernel_control.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace imagejxx {
namespace {
double cubic_weight(double x) {
    x = std::abs(x);
    if (x <= 1) return (1.5 * x - 2.5) * x * x + 1;
    if (x < 2) return ((-0.5 * x + 2.5) * x - 4) * x + 2;
    return 0;
}
double interpolate(const Image& i, double x, double y, int c, Interpolation mode, Border border, double constant) {
    if (!std::isfinite(x) || !std::isfinite(y)) throw std::invalid_argument("Transform produced non-finite coordinates");
    if (border == Border::Constant && (x < -2 || y < -2 || x > i.width + 1.0 || y > i.height + 1.0)) return constant;
    if (border == Border::Nearest) { x = std::clamp(x, 0.0, i.width - 1.0); y = std::clamp(y, 0.0, i.height - 1.0); }
    if (border == Border::Wrap || border == Border::Reflect) {
        double factor = border == Border::Wrap ? 1 : 2;
        x = std::fmod(x, factor * i.width); y = std::fmod(y, factor * i.height);
    }
    if (x < std::numeric_limits<int>::min() + 4.0 || x > std::numeric_limits<int>::max() - 4.0 ||
        y < std::numeric_limits<int>::min() + 4.0 || y > std::numeric_limits<int>::max() - 4.0)
        throw std::invalid_argument("Transform coordinates exceed supported dimensions");
    if (mode == Interpolation::Nearest) return i.sample(static_cast<int>(std::floor(x + 0.5)), static_cast<int>(std::floor(y + 0.5)), c, border, constant);
    int xx = static_cast<int>(std::floor(x)), yy = static_cast<int>(std::floor(y));
    double fx = x - xx, fy = y - yy;
    if (mode == Interpolation::Linear)
        return (1 - fy) * ((1 - fx) * i.sample(xx, yy, c, border, constant) + fx * i.sample(xx + 1, yy, c, border, constant)) +
               fy * ((1 - fx) * i.sample(xx, yy + 1, c, border, constant) + fx * i.sample(xx + 1, yy + 1, c, border, constant));
    double value = 0;
    for (int dy = -1; dy <= 2; ++dy) for (int dx = -1; dx <= 2; ++dx)
        value += cubic_weight(dx - fx) * cubic_weight(dy - fy) * i.sample(xx + dx, yy + dy, c, border, constant);
    return value;
}
void require_rgb(const Image& image) {
    if (image.channels != 3 && image.channels != 4) throw std::invalid_argument("Operation requires RGB or RGBA");
}
} // namespace

Image crop(const Image& i, int x, int y, int width, int height) {
    if (x < 0 || y < 0 || x >= i.width || y >= i.height || width <= 0 || height <= 0 || width > i.width - x || height > i.height - y)
        throw std::invalid_argument("Crop rectangle must be nonempty and inside the image");
    if (i.channels <= 0 || std::size_t(i.width) > std::numeric_limits<std::size_t>::max() / std::size_t(i.channels))
        throw std::invalid_argument("Crop input dimensions overflow sample storage");
    const auto source_row_samples = std::size_t(i.width) * i.channels;
    if (std::size_t(i.height) > std::numeric_limits<std::size_t>::max() / source_row_samples ||
        i.pixels.size() != source_row_samples * i.height)
        throw std::invalid_argument("Crop input storage does not match its dimensions");
    kernel_checkpoint();
    Image out(width, height, i.channels, i.type);
    kernel_checkpoint();
    const auto row_samples = std::size_t(width) * i.channels;
    kernel_blocks("crop.copy", out.pixels.size(),
                  [&](std::size_t first, std::size_t last) {
        while (first < last) {
            const auto row = first / row_samples, offset = first % row_samples;
            const auto count = std::min(last - first, row_samples - offset);
            const auto source_offset = (row + std::size_t(y)) * source_row_samples +
                                       std::size_t(x) * i.channels + offset;
            std::copy_n(i.pixels.begin() + source_offset, count,
                        out.pixels.begin() + first);
            first += count;
        }
    });
    kernel_checkpoint();
    return out;
}
Image paste(const Image& destination, const Image& source, int x, int y, const Image* mask) {
    if (source.channels != destination.channels) throw std::invalid_argument("Paste channel counts must match");
    if (mask) {
        require_gray(*mask);
        if (mask->width != source.width || mask->height != source.height) throw std::invalid_argument("Paste mask must match the source shape");
    }
    Image out = destination;
    auto x0 = std::max<std::int64_t>(0, x), y0 = std::max<std::int64_t>(0, y);
    auto x1 = std::min<std::int64_t>(destination.width, static_cast<std::int64_t>(x) + source.width);
    auto y1 = std::min<std::int64_t>(destination.height, static_cast<std::int64_t>(y) + source.height);
    for (auto yy = y0; yy < y1; ++yy) for (auto xx = x0; xx < x1; ++xx) {
        int sx = static_cast<int>(xx - x), sy = static_cast<int>(yy - y);
        if (!mask || mask->at(sx, sy) != 0)
            for (int c = 0; c < source.channels; ++c) out.at(static_cast<int>(xx), static_cast<int>(yy), c) = source.at(sx, sy, c);
    }
    return out;
}
Image flip(const Image& i, bool horizontal, bool vertical) {
    Image out = i;
    for (int y = 0; y < i.height; ++y) for (int x = 0; x < i.width; ++x) for (int c = 0; c < i.channels; ++c)
        out.at(x, y, c) = i.at(horizontal ? i.width - 1 - x : x, vertical ? i.height - 1 - y : y, c);
    return out;
}
Image rotate90(const Image& i, int turns) {
    int k = (turns % 4 + 4) % 4;
    if (k == 0) return i;
    Image out(k % 2 ? i.height : i.width, k % 2 ? i.width : i.height, i.channels, i.type);
    for (int y = 0; y < i.height; ++y) for (int x = 0; x < i.width; ++x) for (int c = 0; c < i.channels; ++c) {
        if (k == 1) out.at(y, i.width - 1 - x, c) = i.at(x, y, c);
        else if (k == 2) out.at(i.width - 1 - x, i.height - 1 - y, c) = i.at(x, y, c);
        else out.at(i.height - 1 - y, x, c) = i.at(x, y, c);
    }
    return out;
}
Image resize(const Image& i, int width, int height, Interpolation mode) {
    Image out(width, height, i.channels, i.type);
    for (int y = 0; y < height; ++y) for (int x = 0; x < width; ++x) for (int c = 0; c < i.channels; ++c)
        out.at(x, y, c) = interpolate(i, (x + 0.5) * i.width / width - 0.5, (y + 0.5) * i.height / height - 0.5, c, mode, Border::Nearest, 0);
    return out;
}
Image affine_transform(const Image& i, const std::array<double, 6>& inverse, int width, int height, Interpolation mode, Border border, double constant) {
    for (double value : inverse) require_finite(value, "Matrix entry");
    require_finite(constant, "Border value");
    Image out(width, height, i.channels, i.type);
    for (int y = 0; y < height; ++y) for (int x = 0; x < width; ++x) for (int c = 0; c < i.channels; ++c)
        out.at(x, y, c) = interpolate(i, inverse[0] * x + inverse[1] * y + inverse[2], inverse[3] * x + inverse[4] * y + inverse[5], c, mode, border, constant);
    return out;
}
Image rotate(const Image& i, double degrees, bool expand, Interpolation mode, double constant) {
    require_finite(degrees, "Angle");
    double theta = std::remainder(degrees, 360) * std::acos(-1.0) / 180;
    double cs = std::cos(theta), sn = std::sin(theta);
    double wd = expand ? std::ceil(std::abs(i.width * cs) + std::abs(i.height * sn) - 1e-10) : i.width;
    double hd = expand ? std::ceil(std::abs(i.height * cs) + std::abs(i.width * sn) - 1e-10) : i.height;
    if (wd > std::numeric_limits<int>::max() || hd > std::numeric_limits<int>::max()) throw std::invalid_argument("Rotated dimensions overflow");
    int w = std::max(1, static_cast<int>(wd)), h = std::max(1, static_cast<int>(hd));
    double cx = (i.width - 1) / 2.0, cy = (i.height - 1) / 2.0, ox = (w - 1) / 2.0, oy = (h - 1) / 2.0;
    return affine_transform(i, {cs, -sn, cx - cs * ox + sn * oy, sn, cs, cy - sn * ox - cs * oy}, w, h, mode, Border::Constant, constant);
}
Image bin(const Image& i, int fx, int fy, const std::string& method) {
    if (fx <= 0 || fy <= 0) throw std::invalid_argument("Binning factors must be positive");
    if (method != "mean" && method != "sum" && method != "minimum" && method != "maximum") throw std::invalid_argument("Unknown binning method");
    Image out((i.width - 1) / fx + 1, (i.height - 1) / fy + 1, i.channels, method == "sum" ? PixelType::Float64 : i.type);
    for (int y = 0; y < out.height; ++y) for (int x = 0; x < out.width; ++x) for (int c = 0; c < i.channels; ++c) {
        int x0 = x * fx, y0 = y * fy;
        int x1 = static_cast<int>(std::min<std::int64_t>(i.width, static_cast<std::int64_t>(x0) + fx));
        int y1 = static_cast<int>(std::min<std::int64_t>(i.height, static_cast<std::int64_t>(y0) + fy));
        double v = method == "minimum" ? std::numeric_limits<double>::infinity() : method == "maximum" ? -std::numeric_limits<double>::infinity() : 0;
        for (int yy = y0; yy < y1; ++yy) for (int xx = x0; xx < x1; ++xx) {
            if (method == "minimum") v = std::min(v, i.at(xx, yy, c));
            else if (method == "maximum") v = std::max(v, i.at(xx, yy, c));
            else v += i.at(xx, yy, c);
        }
        if (method == "mean") v /= static_cast<double>(x1 - x0) * (y1 - y0);
        out.at(x, y, c) = v;
    }
    return out;
}
Image resize_canvas(const Image& i, int width, int height, int x, int y, double constant) {
    require_finite(constant, "Canvas value");
    Image out(width, height, i.channels, i.type);
    std::fill(out.pixels.begin(), out.pixels.end(), constant);
    return paste(out, i, x, y);
}
Image draw_line(const Image& i, double x1, double y1, double x2, double y2, double value, double thickness) {
    for (double v : {x1, y1, x2, y2, value, thickness}) require_finite(v, "Line parameter");
    if (thickness <= 0) throw std::invalid_argument("Line thickness must be positive");
    double dx = x2 - x1, dy = y2 - y1, denominator = dx * dx + dy * dy;
    if (!std::isfinite(denominator)) throw std::invalid_argument("Line coordinates are too large");
    Image out = i;
    for (int y = 0; y < i.height; ++y) for (int x = 0; x < i.width; ++x) {
        double t = denominator == 0 ? 0 : std::clamp(((x - x1) * dx + (y - y1) * dy) / denominator, 0.0, 1.0);
        if (std::hypot(x - x1 - t * dx, y - y1 - t * dy) <= thickness / 2)
            for (int c = 0; c < i.channels; ++c) out.at(x, y, c) = value;
    }
    return out;
}
Image rectangle_mask(int width, int height, int x, int y, int rw, int rh) {
    Image out(width, height, 1, PixelType::UInt8);
    if (x < 0 || y < 0 || x >= width || y >= height || rw <= 0 || rh <= 0 || rw > width - x || rh > height - y)
        throw std::invalid_argument("Rectangle must be nonempty and inside the image");
    for (int yy = y; yy < y + rh; ++yy) for (int xx = x; xx < x + rw; ++xx) out.at(xx, yy) = 255;
    return out;
}
Image ellipse_mask(int width, int height, double cx, double cy, double rx, double ry) {
    for (double v : {cx, cy, rx, ry}) require_finite(v, "Ellipse parameter");
    if (rx <= 0 || ry <= 0) throw std::invalid_argument("Ellipse radii must be positive");
    Image out(width, height, 1, PixelType::UInt8);
    for (int y = 0; y < height; ++y) for (int x = 0; x < width; ++x) {
        double dx = (x + 0.5 - cx) / rx, dy = (y + 0.5 - cy) / ry;
        if (dx * dx + dy * dy <= 1) out.at(x, y) = 255;
    }
    return out;
}
Image polygon_mask(int width, int height, const std::vector<std::array<double, 2>>& vertices) {
    if (vertices.size() < 3) throw std::invalid_argument("Polygon needs at least three vertices");
    for (const auto& vertex : vertices) for (double v : vertex) require_finite(v, "Vertex");
    Image out(width, height, 1, PixelType::UInt8);
    for (int y = 0; y < height; ++y) for (int x = 0; x < width; ++x) {
        bool inside = false; double px = x + 0.5, py = y + 0.5;
        for (std::size_t a = 0, b = vertices.size() - 1; a < vertices.size(); b = a++) {
            auto v = vertices[a], u = vertices[b];
            if ((v[1] > py) != (u[1] > py) && px < (u[0] - v[0]) * (py - v[1]) / (u[1] - v[1]) + v[0]) inside = !inside;
        }
        out.at(x, y) = inside ? 255 : 0;
    }
    return out;
}
std::vector<double> line_profile(const Image& i, double x1, double y1, double x2, double y2, int samples) {
    require_gray(i);
    for (double v : {x1, y1, x2, y2}) require_finite(v, "Profile coordinate");
    double length = std::hypot(x2 - x1, y2 - y1);
    if (samples == 0) {
        if (!std::isfinite(length) || length >= 10000000) throw std::invalid_argument("Profile is too long");
        samples = static_cast<int>(std::ceil(length)) + 1;
    }
    if (samples < 1 || samples > 10000000) throw std::invalid_argument("Profile samples must be in [1, 10000000]");
    std::vector<double> out(static_cast<std::size_t>(samples));
    for (int n = 0; n < samples; ++n) {
        double t = samples == 1 ? 0 : n / (samples - 1.0);
        out[n] = interpolate(i, (1 - t) * x1 + t * x2, (1 - t) * y1 + t * y2, 0, Interpolation::Linear, Border::Nearest, 0);
    }
    return out;
}

Image grayscale(const Image& image) {
    if (image.channels == 1) return image;
    require_rgb(image);
    Image out(image.width, image.height, 1, image.type);
    for (int y = 0; y < image.height; ++y) for (int x = 0; x < image.width; ++x)
        out.at(x, y) = 0.299 * image.at(x, y, 0) + 0.587 * image.at(x, y, 1) + 0.114 * image.at(x, y, 2);
    return out;
}
Image rgb_to_hsv(const Image& image) {
    require_rgb(image);
    Image out(image.width, image.height, 3, PixelType::Float64);
    double scale = type_max(image.type);
    for (int y = 0; y < image.height; ++y) for (int x = 0; x < image.width; ++x) {
        double r = image.at(x, y, 0) / scale, g = image.at(x, y, 1) / scale, b = image.at(x, y, 2) / scale;
        if (std::min({r, g, b}) < 0 || std::max({r, g, b}) > 1) throw std::invalid_argument("RGB pixels must be within the dtype's color range (float: [0, 1])");
        double maximum = std::max({r, g, b}), minimum = std::min({r, g, b}), delta = maximum - minimum, h = 0;
        if (delta != 0) {
            if (maximum == r) h = std::fmod((g - b) / delta, 6);
            else if (maximum == g) h = (b - r) / delta + 2;
            else h = (r - g) / delta + 4;
            h /= 6; if (h < 0) h += 1;
        }
        out.at(x, y, 0) = h; out.at(x, y, 1) = maximum == 0 ? 0 : delta / maximum; out.at(x, y, 2) = maximum;
    }
    return out;
}
Image hsv_to_rgb(const Image& image, PixelType output_type) {
    if (image.channels != 3) throw std::invalid_argument("HSV needs exactly three channels");
    Image out(image.width, image.height, 3, output_type);
    for (int y = 0; y < image.height; ++y) for (int x = 0; x < image.width; ++x) {
        double h = image.at(x, y, 0), s = image.at(x, y, 1), v = image.at(x, y, 2);
        if (std::min({h, s, v}) < 0 || std::max({h, s, v}) > 1) throw std::invalid_argument("HSV values must be in [0, 1]");
        double hue = std::fmod(h, 1.0) * 6, fraction = hue - std::floor(hue);
        int sector = static_cast<int>(std::floor(hue));
        double p = v * (1 - s), q = v * (1 - s * fraction), t = v * (1 - s * (1 - fraction));
        std::array<double, 3> rgb;
        switch (sector) {
            case 0: rgb = {v, t, p}; break; case 1: rgb = {q, v, p}; break;
            case 2: rgb = {p, v, t}; break; case 3: rgb = {p, q, v}; break;
            case 4: rgb = {t, p, v}; break; default: rgb = {v, p, q}; break;
        }
        for (int c = 0; c < 3; ++c) out.at(x, y, c) = rgb[c] * type_max(output_type);
    }
    return out;
}
Stack split_channels(const Image& image) {
    Stack channels;
    for (int c = 0; c < image.channels; ++c) {
        Image plane(image.width, image.height, 1, image.type);
        for (int y = 0; y < image.height; ++y) for (int x = 0; x < image.width; ++x) plane.at(x, y) = image.at(x, y, c);
        channels.push_back(std::move(plane));
    }
    return channels;
}
Image merge_channels(const Stack& channels) {
    if (channels.empty() || channels.size() > 4) throw std::invalid_argument("Merge needs one to four grayscale channels");
    const auto& first = channels.front();
    for (const auto& image : channels) {
        require_gray(image); require_same_shape(first, image);
        if (image.type != first.type) throw std::invalid_argument("Merged channel dtypes must match");
    }
    Image out(first.width, first.height, static_cast<int>(channels.size()), first.type);
    for (int y = 0; y < out.height; ++y) for (int x = 0; x < out.width; ++x) for (int c = 0; c < out.channels; ++c)
        out.at(x, y, c) = channels[c].at(x, y);
    return out;
}
Image color_deconvolution(const Image& image, const std::array<double, 9>& stains) {
    require_rgb(image);
    std::array<double, 9> a{};
    for (int stain = 0; stain < 3; ++stain) {
        double length = 0;
        for (int c = 0; c < 3; ++c) {
            double v = stains[stain * 3 + c]; require_finite(v, "Stain vector");
            if (v < 0) throw std::invalid_argument("Stain absorbances must be nonnegative");
            length = std::hypot(length, v);
        }
        if (length == 0) throw std::invalid_argument("Stain vectors must be nonzero");
        for (int c = 0; c < 3; ++c) a[c * 3 + stain] = stains[stain * 3 + c] / length;
    }
    double det = a[0] * (a[4] * a[8] - a[5] * a[7]) - a[1] * (a[3] * a[8] - a[5] * a[6]) + a[2] * (a[3] * a[7] - a[4] * a[6]);
    if (std::abs(det) < 1e-12) throw std::invalid_argument("Stain matrix is singular or ill-conditioned");
    std::array<double, 9> inverse = {a[4]*a[8]-a[5]*a[7], a[2]*a[7]-a[1]*a[8], a[1]*a[5]-a[2]*a[4],
        a[5]*a[6]-a[3]*a[8], a[0]*a[8]-a[2]*a[6], a[2]*a[3]-a[0]*a[5],
        a[3]*a[7]-a[4]*a[6], a[1]*a[6]-a[0]*a[7], a[0]*a[4]-a[1]*a[3]};
    for (double& value : inverse) value /= det;
    double scale = type_max(image.type), epsilon = scale / 255;
    Image out(image.width, image.height, 3, PixelType::Float64);
    for (int y = 0; y < image.height; ++y) for (int x = 0; x < image.width; ++x) {
        std::array<double, 3> od{};
        for (int c = 0; c < 3; ++c) {
            double value = image.at(x, y, c);
            if (value < 0 || value > scale) throw std::invalid_argument("Color deconvolution pixels must be within the dtype's color range");
            od[c] = -std::log((value + epsilon) / (scale + epsilon));
        }
        for (int s = 0; s < 3; ++s) for (int c = 0; c < 3; ++c) out.at(x, y, s) += inverse[s * 3 + c] * od[c];
    }
    return out;
}
} // namespace imagejxx
