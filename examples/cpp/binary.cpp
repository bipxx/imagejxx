#include <imagejxx/binary.hpp>
#include <imagejxx/settings.hpp>
#include <iostream>

int main() {
  using namespace imagejxx;
  Settings settings;
  settings.update(
      {{"binary_iterations", std::int64_t(2)}, {"binary_pad_edges", true}});
  const auto image = fill(Image(3, 3, 1, PixelType::UInt8), 255);
  const auto options = settings.binary_options();
  const auto result = binary_morphology(image, "erode", options);
  if (result.pixels != image.pixels || options.iterations != 2)
    return 1;
  std::cout << "Installed binary options preserve the padded foreground\n";
}
