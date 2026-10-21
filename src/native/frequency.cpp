#include "imagejxx/image.hpp"
#include "kernel_control.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace imagejxx {
namespace {
bool power_of_two(int n) { return n > 0 && (n & (n - 1)) == 0; }
int next_power_of_two(int n) {
    int value = 1;
    while (value < n) {
        if (value > std::numeric_limits<int>::max() / 2) throw std::invalid_argument("FFT padded dimensions overflow");
        value *= 2;
    }
    return value;
}
void fft_line(std::vector<std::complex<double>>& values, bool inverse) {
    const std::size_t n = values.size();
    for (std::size_t i = 1, j = 0; i < n; ++i) {
        kernel_checkpoint_periodic();
        std::size_t bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) std::swap(values[i], values[j]);
    }
    for (std::size_t length = 2; length <= n; length <<= 1) {
        double angle = (inverse ? 2 : -2) * std::acos(-1.0) / static_cast<double>(length);
        std::complex<double> root(std::cos(angle), std::sin(angle));
        for (std::size_t start = 0; start < n; start += length) {
            std::complex<double> w(1, 0);
            for (std::size_t j = 0; j < length / 2; ++j) {
                kernel_checkpoint_periodic();
                auto a = values[start + j], b = values[start + j + length / 2] * w;
                values[start + j] = a + b; values[start + j + length / 2] = a - b; w *= root;
            }
        }
        if (length == n) break;
    }
    if (inverse) for (auto& value : values) value /= static_cast<double>(n);
}
void check_spectrum(const Spectrum& spectrum) {
    if (!power_of_two(spectrum.width) || !power_of_two(spectrum.height) ||
        spectrum.pixels.size() != static_cast<std::size_t>(spectrum.width) * spectrum.height)
        throw std::invalid_argument("FFT spectrum must have power-of-two dimensions and matching storage");
}
void transform(Spectrum& spectrum, bool inverse) {
    check_spectrum(spectrum);
    std::vector<std::complex<double>> line(static_cast<std::size_t>(spectrum.width));
    for (int y = 0; y < spectrum.height; ++y) {
        kernel_checkpoint();
        for (int x = 0; x < spectrum.width; ++x) line[x] = spectrum.pixels[static_cast<std::size_t>(y) * spectrum.width + x];
        fft_line(line, inverse);
        for (int x = 0; x < spectrum.width; ++x) spectrum.pixels[static_cast<std::size_t>(y) * spectrum.width + x] = line[x];
    }
    line.resize(static_cast<std::size_t>(spectrum.height));
    for (int x = 0; x < spectrum.width; ++x) {
        kernel_checkpoint();
        for (int y = 0; y < spectrum.height; ++y) line[y] = spectrum.pixels[static_cast<std::size_t>(y) * spectrum.width + x];
        fft_line(line, inverse);
        for (int y = 0; y < spectrum.height; ++y) spectrum.pixels[static_cast<std::size_t>(y) * spectrum.width + x] = line[y];
    }
}
} // namespace
Spectrum fft2(const Image& image, bool pad) {
    require_gray(image);
    if (!pad && (!power_of_two(image.width) || !power_of_two(image.height))) throw std::invalid_argument("pad=False requires power-of-two image dimensions");
    int w = pad ? next_power_of_two(image.width) : image.width, h = pad ? next_power_of_two(image.height) : image.height;
    Spectrum spectrum{w, h, std::vector<std::complex<double>>(static_cast<std::size_t>(w) * h)};
    for (int y = 0; y < image.height; ++y) for (int x = 0; x < image.width; ++x)
        spectrum.pixels[static_cast<std::size_t>(y) * w + x] = image.at(x, y);
    transform(spectrum, false);
    return spectrum;
}
Image ifft2(const Spectrum& input) {
    auto spectrum = input; transform(spectrum, true);
    Image out(spectrum.width, spectrum.height, 1, PixelType::Float64);
    for (std::size_t i = 0; i < out.pixels.size(); ++i) out.pixels[i] = spectrum.pixels[i].real();
    return out;
}
Image power_spectrum(const Spectrum& spectrum, bool log_scale, bool shift) {
    check_spectrum(spectrum); Image out(spectrum.width, spectrum.height, 1, PixelType::Float64);
    for (int y = 0; y < spectrum.height; ++y) for (int x = 0; x < spectrum.width; ++x) {
        int xx = shift ? (x + spectrum.width / 2) % spectrum.width : x;
        int yy = shift ? (y + spectrum.height / 2) % spectrum.height : y;
        double power = std::norm(spectrum.pixels[static_cast<std::size_t>(yy) * spectrum.width + xx]);
        out.at(x, y) = log_scale ? std::log1p(power) : power;
    }
    return out;
}
Image fft_bandpass(const Image& image, double low, double high) {
    require_finite(low, "Low cutoff"); require_finite(high, "High cutoff");
    if (low < 0 || high < 0 || low > std::sqrt(0.5) || high > std::sqrt(0.5) || (high > 0 && low >= high))
        throw std::invalid_argument("Cutoffs must be in [0, sqrt(0.5)], with low < high when high is enabled");
    auto spectrum = fft2(image);
    for (int y = 0; y < spectrum.height; ++y) for (int x = 0; x < spectrum.width; ++x) {
        double fx = std::min(x, spectrum.width - x) / static_cast<double>(spectrum.width);
        double fy = std::min(y, spectrum.height - y) / static_cast<double>(spectrum.height);
        double f2 = fx * fx + fy * fy;
        double gain = (low > 0 ? -std::expm1(-f2 / (2 * low * low)) : 1) * (high > 0 ? std::exp(-f2 / (2 * high * high)) : 1);
        spectrum.pixels[static_cast<std::size_t>(y) * spectrum.width + x] *= gain;
    }
    auto out = crop(ifft2(spectrum), 0, 0, image.width, image.height); out.type = image.type;
    return out;
}
} // namespace imagejxx
