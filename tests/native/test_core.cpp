#include "imagejxx/image.hpp"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <stdexcept>

using namespace imagejxx;
void check(bool condition, const char* message) { if (!condition) throw std::runtime_error(message); }
int main() {
    try {
        Image image(3, 2, 1, PixelType::UInt8); image.pixels = {0, 1, 2, 3, 4, 5};
        auto rotated = rotate90(image);
        check(rotated.width == 2 && rotated.height == 3 && rotated.pixels == std::vector<double>({2, 5, 1, 4, 0, 3}), "C++ rotation");
        check(statistics(image).mean == 2.5 && statistics(image).median == 2.5, "C++ statistics");
        check(point(image, "invert").pixels.front() == 255, "C++ pixel arithmetic");
        check(crop(image, 1, 0, 2, 2).pixels == std::vector<double>({1, 2, 4, 5}), "C++ crop");
        Image impulse(5, 5); impulse.at(2, 2) = 1;
        check(rank_filter(impulse, 1, "median").at(2, 2) == 0, "C++ median");
        check(std::abs(statistics(gaussian_blur(impulse, 1, 1)).sum - 1) < 1e-12, "C++ Gaussian mass");
        auto labels = connected_components(impulse);
        check(labels.count == 1 && region_properties(labels).front().pixels == 1, "C++ particles");
        check(distance_transform(impulse).at(2, 2) == 1, "C++ distance transform");
        check(skeletonize(impulse).at(2, 2) == 255, "C++ skeleton");
        check(binary_watershed(impulse).count == 1, "C++ automatic watershed");
        Image constant(7, 9); std::fill(constant.pixels.begin(), constant.pixels.end(), 20);
        check(std::abs(statistics(rolling_ball_background(constant, 2)).mean - 20) < 1e-12, "C++ spherical background");
        auto spectrum = fft2(image);
        auto restored = crop(ifft2(spectrum), 0, 0, image.width, image.height);
        for (std::size_t i = 0; i < image.pixels.size(); ++i) check(std::abs(restored.pixels[i] - image.pixels[i]) < 1e-12, "C++ FFT round trip");
        bool rejected = false;
        try { crop(image, 2, 0, 2, 2); } catch (const std::invalid_argument&) { rejected = true; }
        check(rejected, "C++ invalid ROI rejection");
        Stack projection;
        for (double v : {199., 2., 6., 188.}) {
            Image plane(2, 1, 1, PixelType::UInt16);
            std::fill(plane.pixels.begin(), plane.pixels.end(), v);
            projection.push_back(std::move(plane));
        }
        for (const auto &method : {"maximum", "minimum", "mean", "sum", "median", "stddev"}) {
            const double expected = std::string(method) == "maximum" ? 6. :
                std::string(method) == "minimum" || std::string(method) == "stddev" ? 2. :
                std::string(method) == "sum" ? 8. : 4.;
            const auto result = project(projection, method, 1, 2);
            check(result.pixels == std::vector<double>({expected, expected}), "C++ inclusive Z range");
            check(project(projection, method).pixels == project(projection, method, 0, 3).pixels,
                  "C++ original whole-stack overload");
        }
        check(projection.front().pixels[0] == 199. && projection.back().pixels[0] == 188.,
              "C++ projection source unchanged");
        for (const auto bounds : {std::pair<std::size_t, std::size_t>{2, 1}, {0, 4}}) {
            bool refused = false;
            try { (void)project(projection, "mean", bounds.first, bounds.second); }
            catch (const std::invalid_argument &) { refused = true; }
            check(refused, "C++ invalid projection bounds");
        }
        std::cout << "Native API checks passed\n";
        return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
