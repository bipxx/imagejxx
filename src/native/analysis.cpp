#include "imagejxx/image.hpp"

#include <algorithm>
#include <cmath>
#include <numeric>
#include <stdexcept>

namespace imagejxx {
namespace {
std::size_t histogram_index(double v, const Histogram& h) {
    double position = (v - h.edges.front()) / (h.edges.back() - h.edges.front());
    return static_cast<std::size_t>(std::clamp(std::floor(position * h.counts.size()), 0.0, static_cast<double>(h.counts.size() - 1)));
}
void check_stack(const Stack& stack) {
    if (stack.empty() || stack.size() > static_cast<std::size_t>(std::numeric_limits<int>::max())) throw std::invalid_argument("Stack must contain at least one slice");
    for (const auto& image : stack) {
        require_same_shape(stack.front(), image);
        if (image.type != stack.front().type) throw std::invalid_argument("Stack dtypes must match");
    }
}
} // namespace

Histogram histogram(const Image& image, int bins, double lower, double upper) {
    require_gray(image);
    if (bins == 0) bins = image.type == PixelType::UInt16 ? 65536 : 256;
    if (bins < 1 || bins > 1048576) throw std::invalid_argument("Histogram bins must be in [1, 1048576]");
    if (std::isnan(lower) != std::isnan(upper)) throw std::invalid_argument("Supply both histogram range bounds or neither");
    if (std::isnan(lower)) {
        if (image.type == PixelType::UInt8 || image.type == PixelType::UInt16) { lower = -0.5; upper = type_max(image.type) + 0.5; }
        else {
            auto extrema = std::minmax_element(image.pixels.begin(), image.pixels.end());
            lower = *extrema.first; upper = *extrema.second;
            if (lower == upper) { lower -= 0.5; upper += 0.5; }
        }
    }
    require_finite(lower, "Histogram lower bound"); require_finite(upper, "Histogram upper bound");
    if (upper <= lower || !std::isfinite(upper - lower)) throw std::invalid_argument("Histogram range must have finite positive width");
    Histogram out{std::vector<std::uint64_t>(static_cast<std::size_t>(bins), 0), std::vector<double>(static_cast<std::size_t>(bins) + 1)};
    for (int b = 0; b <= bins; ++b) out.edges[b] = lower + (upper - lower) * (b / static_cast<double>(bins));
    for (double value : image.pixels) if (value >= lower && value <= upper) ++out.counts[histogram_index(value, out)];
    return out;
}
Statistics statistics(const Image& image, const Image* mask) {
    require_gray(image);
    if (mask) { require_gray(*mask); require_same_shape(image, *mask); }
    Statistics out;
    double m2 = 0;
    std::vector<double> values;
    for (std::size_t i = 0; i < image.pixels.size(); ++i) if (!mask || mask->pixels[i] != 0) {
        double value = image.pixels[i];
        if (out.count == 0) out.minimum = out.maximum = value;
        ++out.count; out.minimum = std::min(out.minimum, value); out.maximum = std::max(out.maximum, value);
        double delta = value - out.mean;
        out.mean += delta / out.count; m2 += delta * (value - out.mean); out.sum += value;
        values.push_back(value);
    }
    if (out.count == 0) throw std::invalid_argument("Selection contains no pixels");
    out.stddev = std::sqrt(std::max(0.0, m2 / out.count)); out.integrated_density = out.sum;
    auto middle = values.begin() + values.size() / 2;
    std::nth_element(values.begin(), middle, values.end()); out.median = *middle;
    if (values.size() % 2 == 0) out.median = out.median / 2 + *std::max_element(values.begin(), middle) / 2;
    return out;
}

double auto_threshold(const Image& image, const std::string& method, int bins) {
    require_gray(image);
    const std::vector<std::string> methods = {"otsu", "mean", "isodata", "li", "triangle", "yen", "max_entropy", "percentile", "intermodes", "minimum", "moments", "min_error"};
    if (std::find(methods.begin(), methods.end(), method) == methods.end()) throw std::invalid_argument("Unknown automatic threshold method");
    auto extrema = std::minmax_element(image.pixels.begin(), image.pixels.end());
    if (*extrema.first == *extrema.second) return *extrema.first;
    if (method == "mean") return std::accumulate(image.pixels.begin(), image.pixels.end(), 0.0) / image.pixels.size();
    auto h = histogram(image, bins == 0 ? 256 : bins);
    std::size_t n = h.counts.size();
    if (n < 2) throw std::invalid_argument("Automatic threshold needs at least two histogram bins");
    std::vector<double> p(n), cumulative(n), moment(n), second(n), entropy(n), squares(n), reverse_squares(n);
    double total = std::accumulate(h.counts.begin(), h.counts.end(), 0.0);
    for (std::size_t i = 0; i < n; ++i) {
        p[i] = h.counts[i] / total;
        cumulative[i] = p[i] + (i ? cumulative[i - 1] : 0);
        moment[i] = p[i] * i + (i ? moment[i - 1] : 0);
        second[i] = p[i] * i * i + (i ? second[i - 1] : 0);
        entropy[i] = (p[i] > 0 ? -p[i] * std::log(p[i]) : 0) + (i ? entropy[i - 1] : 0);
        squares[i] = p[i] * p[i] + (i ? squares[i - 1] : 0);
    }
    for (std::size_t j = n; j-- > 0;) reverse_squares[j] = p[j] * p[j] + (j + 1 < n ? reverse_squares[j + 1] : 0);
    std::size_t threshold_bin = 0;
    double mean = moment.back();
    if (method == "otsu" || method == "yen" || method == "max_entropy" || method == "min_error") {
        double best = method == "min_error" ? std::numeric_limits<double>::infinity() : -std::numeric_limits<double>::infinity();
        bool found = false;
        for (std::size_t i = 0; i + 1 < n; ++i) {
            double w0 = cumulative[i], w1 = 1 - w0;
            if (w0 <= 0 || w1 <= 1e-15) continue;
            double score = 0;
            if (method == "otsu") {
                double delta = moment[i] / w0 - (mean - moment[i]) / w1;
                score = w0 * w1 * delta * delta;
            } else if (method == "yen") score = -std::log(squares[i] * reverse_squares[i + 1]) + 2 * std::log(w0 * w1);
            else if (method == "max_entropy") score = entropy[i] / w0 + std::log(w0) + (entropy.back() - entropy[i]) / w1 + std::log(w1);
            else {
                double m0 = moment[i] / w0, m1 = (mean - moment[i]) / w1;
                double v0 = second[i] / w0 - m0 * m0, v1 = (second.back() - second[i]) / w1 - m1 * m1;
                if (v0 <= 0 || v1 <= 0) continue;
                score = 1 + w0 * std::log(v0) + w1 * std::log(v1) - 2 * (w0 * std::log(w0) + w1 * std::log(w1));
            }
            bool better = method == "min_error" ? score < best : score > best;
            if (better) { found = true; best = score; threshold_bin = i; }
        }
        if (!found && method == "min_error") return auto_threshold(image, "otsu", bins);
    } else if (method == "percentile") {
        threshold_bin = static_cast<std::size_t>(std::lower_bound(cumulative.begin(), cumulative.end(), 0.5) - cumulative.begin());
    } else if (method == "isodata" || method == "li") {
        double t = mean;
        for (int iteration = 0; iteration < 10000; ++iteration) {
            auto i = static_cast<std::size_t>(std::clamp(std::floor(t), 0.0, static_cast<double>(n - 2)));
            double w0 = cumulative[i], w1 = 1 - w0;
            if (w0 <= 0 || w1 <= 1e-15) break;
            double m0 = moment[i] / w0, m1 = (mean - moment[i]) / w1;
            double next = method == "isodata" ? (m0 + m1) / 2 : m0 <= 0 ? 0 : (m0 - m1) / (std::log(m0) - std::log(m1));
            if (std::abs(next - t) <= 0.5) { t = next; break; }
            t = next;
        }
        threshold_bin = static_cast<std::size_t>(std::clamp(std::floor(t), 0.0, static_cast<double>(n - 1)));
    } else if (method == "triangle") {
        auto peak = static_cast<std::size_t>(std::max_element(p.begin(), p.end()) - p.begin());
        std::size_t left = 0, right = n - 1;
        while (p[left] == 0) ++left;
        while (p[right] == 0) --right;
        std::size_t endpoint = peak - left >= right - peak ? left : right;
        double dx = static_cast<double>(peak) - endpoint, dy = p[peak] - p[endpoint], best = -1;
        for (std::size_t i = std::min(peak, endpoint); i <= std::max(peak, endpoint); ++i) {
            double distance = std::abs(dy * (static_cast<double>(i) - endpoint) - dx * (p[i] - p[endpoint]));
            if (distance > best) { best = distance; threshold_bin = i; }
        }
    } else if (method == "moments") {
        double m2 = second.back(), m3 = 0;
        for (std::size_t i = 0; i < n; ++i) m3 += p[i] * i * i * i;
        double cd = m2 - mean * mean;
        if (cd <= 0) return auto_threshold(image, "otsu", bins);
        double c0 = (-m2 * m2 + mean * m3) / cd, c1 = (-m3 + mean * m2) / cd;
        double discriminant = c1 * c1 - 4 * c0;
        if (discriminant <= 0) return auto_threshold(image, "otsu", bins);
        double z0 = (-c1 - std::sqrt(discriminant)) / 2, z1 = (-c1 + std::sqrt(discriminant)) / 2;
        double background = std::clamp((z1 - mean) / (z1 - z0), 0.0, 1.0);
        threshold_bin = static_cast<std::size_t>(std::lower_bound(cumulative.begin(), cumulative.end(), background) - cumulative.begin());
    } else {
        auto smooth = p;
        std::vector<std::size_t> peaks;
        bool converged = false;
        for (int iteration = 0; iteration < 10000; ++iteration) {
            peaks.clear();
            for (std::size_t i = 0; i < n;) {
                std::size_t end = i;
                while (end + 1 < n && smooth[end + 1] == smooth[i]) ++end;
                double left = i ? smooth[i - 1] : 0, right = end + 1 < n ? smooth[end + 1] : 0;
                if (smooth[i] > left && smooth[i] > right) peaks.push_back((i + end) / 2);
                i = end + 1;
            }
            if (peaks.size() == 2) { converged = true; break; }
            auto previous = smooth;
            for (std::size_t i = 0; i < n; ++i) smooth[i] = (previous[i ? i - 1 : i] + previous[i] + previous[i + 1 < n ? i + 1 : i]) / 3;
        }
        if (!converged) throw std::invalid_argument("Histogram did not become bimodal; use another threshold method");
        if (method == "intermodes") threshold_bin = (peaks[0] + peaks[1]) / 2;
        else threshold_bin = static_cast<std::size_t>(std::min_element(smooth.begin() + peaks[0], smooth.begin() + peaks[1] + 1) - smooth.begin());
    }
    threshold_bin = std::min(threshold_bin, n - 1);
    return (h.edges[threshold_bin] + h.edges[threshold_bin + 1]) / 2;
}
Image auto_mask(const Image& image, const std::string& method, bool bright, int bins) {
    double t = auto_threshold(image, method, bins);
    Image out(image.width, image.height, 1, PixelType::UInt8);
    for (std::size_t i = 0; i < out.pixels.size(); ++i) out.pixels[i] = (image.pixels[i] > t) == bright ? 255 : 0;
    return out;
}
Image equalize_histogram(const Image& image) {
    auto h = histogram(image);
    std::uint64_t count = 0, first = 0;
    std::vector<double> lookup(h.counts.size());
    for (std::size_t b = 0; b < h.counts.size(); ++b) {
        count += h.counts[b]; if (first == 0 && count > 0) first = count;
        lookup[b] = static_cast<double>(count - first);
    }
    if (count == first) return image;
    Image out = image;
    for (double& v : out.pixels) v = lookup[histogram_index(v, h)] * type_max(image.type) / (count - first);
    return out;
}
Image project(const Stack& stack, const std::string& method) {
    check_stack(stack);
    if (method != "maximum" && method != "minimum" && method != "mean" && method != "sum" && method != "median" && method != "stddev")
        throw std::invalid_argument("Unknown projection method");
    Image out = stack.front();
    if (method != "maximum" && method != "minimum") out.type = PixelType::Float64;
    std::vector<double> values(stack.size());
    for (std::size_t i = 0; i < out.pixels.size(); ++i) {
        for (std::size_t z = 0; z < stack.size(); ++z) values[z] = stack[z].pixels[i];
        double value = 0;
        if (method == "maximum") value = *std::max_element(values.begin(), values.end());
        else if (method == "minimum") value = *std::min_element(values.begin(), values.end());
        else if (method == "median") {
            auto mid = values.begin() + values.size() / 2;
            std::nth_element(values.begin(), mid, values.end()); value = *mid;
            if (values.size() % 2 == 0) value = value / 2 + *std::max_element(values.begin(), mid) / 2;
        } else {
            double sum = std::accumulate(values.begin(), values.end(), 0.0), mean = sum / values.size();
            if (method == "sum") value = sum;
            else if (method == "mean") value = mean;
            else { for (double v : values) value += (v - mean) * (v - mean); value = std::sqrt(value / values.size()); }
        }
        out.pixels[i] = value;
    }
    return out;
}
Stack gaussian_blur3d(const Stack& stack, double sx, double sy, double sz, Border border) {
    check_stack(stack); require_finite(sz, "Z sigma");
    if (sz < 0 || sz > 1024) throw std::invalid_argument("Z sigma must be in [0, 1024]");
    Stack xy;
    for (const auto& image : stack) xy.push_back(gaussian_blur(image, sx, sy, border));
    if (sz == 0) return xy;
    int radius = static_cast<int>(std::ceil(4 * sz)), depth = static_cast<int>(stack.size());
    std::vector<double> weights(static_cast<std::size_t>(2 * radius + 1));
    for (int z = -radius; z <= radius; ++z) weights[z + radius] = std::exp(-0.5 * (z / sz) * (z / sz));
    double sum = std::accumulate(weights.begin(), weights.end(), 0.0);
    for (double& w : weights) w /= sum;
    Stack out = xy;
    for (int z = 0; z < depth; ++z) for (std::size_t i = 0; i < xy.front().pixels.size(); ++i) {
        double value = 0;
        for (int dz = -radius; dz <= radius; ++dz) {
            int zz = z + dz;
            if (zz < 0 || zz >= depth) {
                if (border == Border::Constant) continue;
                if (border == Border::Nearest) zz = std::clamp(zz, 0, depth - 1);
                else {
                    auto period = border == Border::Wrap ? static_cast<std::int64_t>(depth) : 2LL * depth;
                    auto t = (static_cast<std::int64_t>(zz) % period + period) % period;
                    zz = static_cast<int>(t < depth ? t : period - 1 - t);
                }
            }
            value += weights[dz + radius] * xy[zz].pixels[i];
        }
        out[z].pixels[i] = value;
    }
    return out;
}
} // namespace imagejxx
