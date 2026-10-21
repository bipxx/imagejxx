#include <imagejxx/image.hpp>
#include <iostream>

int main() {
    imagejxx::Image image(64, 64, 1, imagejxx::PixelType::UInt8);
    for (int y = 20; y < 40; ++y) for (int x = 20; x < 40; ++x) image.at(x, y) = 200;
    auto blurred = imagejxx::gaussian_blur(image, 1, 1);
    auto mask = imagejxx::auto_mask(blurred, "otsu", true);
    auto labels = imagejxx::binary_watershed(mask);
    std::cout << "Native regions: " << labels.count << '\n';
    return labels.count == 1 ? 0 : 1;
}
