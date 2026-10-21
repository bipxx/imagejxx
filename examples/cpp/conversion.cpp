#include <imagejxx/conversion.hpp>
#include <imagejxx/settings.hpp>
#include <iostream>
int main() {
  using namespace imagejxx;
  Settings settings;
  settings.update({{"conversion_full_range_inversions", true},
                   {"conversion_unsigned16_range", std::string("12")}});
  Image image(2, 1, 1, PixelType::UInt16);
  image.pixels = {0, 4095};
  if (convert_image(image, PixelType::UInt8).pixels !=
          std::vector<double>({0, 255}) ||
      invert_uint16(image, settings.conversion_options()).pixels !=
          std::vector<double>({4095, 0}))
    return 1;
  std::cout
      << "Installed ImageJ-style conversion and configured inversion passed\n";
}
