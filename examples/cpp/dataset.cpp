#include <imagejxx/dataset.hpp>
#include <iostream>

int main() {
  using namespace imagejxx;
  Stack planes;
  for (int z = 0; z < 3; ++z) {
    Image image(5, 4, 1, PixelType::UInt16);
    for (int y = 0; y < image.height; ++y)
      for (int x = 0; x < image.width; ++x)
        image.at(x, y) = 100 * z + 10 * y + x;
    planes.push_back(std::move(image));
  }
  SharedPlanes storage(std::move(planes));
  SpatialCalibration calibration;
  calibration.spacing = {.5, 2, 3};
  calibration.origin = {10, 20, 30};
  calibration.unit = "um";
  auto dataset =
      Dataset::from_shared_planes({1, 1, 3, 4, 5, 1}, storage, calibration);
  auto xz = dataset.extract_plane(Plane::XZ, 2);
  auto geometry = dataset.plane_geometry(Plane::XZ, 2);
  auto edited =
      dataset.with_plane({0, 0, 1}, fill(Image(5, 4, 1, PixelType::UInt16), 7));
  std::cout << "XZ: " << xz.width << 'x' << xz.height
            << ", sample: " << xz.at(3, 1)
            << ", physical origin: " << geometry.origin[0] << ','
            << geometry.origin[1] << ',' << geometry.origin[2] << ' '
            << geometry.unit << '\n';
  return xz.at(3, 1) == 123 && geometry.origin[1] == 24 &&
                 edited.plane({0, 0, 1})->at(3, 2) == 7 &&
                 dataset.plane({0, 0, 1})->at(3, 2) == 123 &&
                 edited.plane() == storage.shared(0)
             ? 0
             : 1;
}
