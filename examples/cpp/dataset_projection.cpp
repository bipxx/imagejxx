#include <imagejxx/dataset_projection_command.hpp>
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
  imagejxx::Stack planes;
  for (int t = 0; t < 3; ++t) for (int c = 0; c < 2; ++c) for (int z = 0; z < 3; ++z) {
    imagejxx::Image plane(1, 1, 1, imagejxx::PixelType::UInt16);
    plane.pixels[0] = 100*t + 10*c + z;
    planes.push_back(std::move(plane));
  }
  auto source = imagejxx::Dataset::from_planes({3, 2, 3, 1, 1, 1}, std::move(planes), calibration);
  imagejxx::DatasetZProjectionRequest whole;
  whole.last = 2;
  auto result = imagejxx::project_dataset_z(source, whole);
  if (result.shape().dimensions() != std::array<int, 6>{3, 2, 1, 1, 1, 1} ||
      result.plane({2, 1, 0})->pixels[0] != 211 || source.shape().z != 3)
    throw std::runtime_error("Installed finite projection changed scientific axes or source");
  imagejxx::Document document(source);
  const auto command = imagejxx::resolve_dataset_z_projection_command(document.snapshot(), whole);
  imagejxx::validate_dataset_z_projection_command_record(command.record());
  const auto replay = imagejxx::dataset_z_projection_command_from_record(document.snapshot(), command.record());
  const auto executed = imagejxx::execute_dataset_z_projection(replay);
  if (executed.plane({2, 1, 0})->pixels[0] != 211 || document.undo_steps() != 0)
    throw std::runtime_error("Installed projection command replay changed values or source history");
}
