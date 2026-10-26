#include <imagejxx/dataset_projection.hpp>
#include <stdexcept>

// Installed SDK consumer: describe a grouped scientific projection without
// opening or allocating any pixels. Processing/publication are separate steps.
int main() {
  imagejxx::DatasetZProjectionRequest request;
  request.first = 2;
  request.last = 7;
  request.group_size = 3;
  imagejxx::SpatialCalibration calibration;
  calibration.spacing = {.5, 2, 3};
  calibration.origin = {10, -4, 8};
  calibration.time_origin = -3;
  calibration.frame_interval = 2.5;
  const auto layout = imagejxx::describe_dataset_z_projection(
      {3, 2, 9, 5, 7, 3}, imagejxx::PixelType::UInt16, calibration, request);
  const auto contributor = layout.contributor({2, 1, 1}, 2);
  if (layout.shape.dimensions() != std::array<int, 6>{3, 2, 2, 5, 7, 3} ||
      layout.pixel_type != imagejxx::PixelType::Float64 ||
      layout.calibration.origin != std::array<double, 3>{10, -4, 14} ||
      layout.calibration.spacing != std::array<double, 3>{.5, 2, 9} ||
      contributor.t != 2 || contributor.c != 1 || contributor.z != 7)
    throw std::runtime_error("Installed projection descriptor differs from its contract");
}
