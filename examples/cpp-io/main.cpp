#include <imagejxx/io.hpp>

#include <array>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {
std::size_t checks = 0;

void require(bool condition, const char* message) {
  ++checks;
  if (!condition)
    throw std::runtime_error(message);
}

void exact_dataset(const imagejxx::Dataset& dataset) {
  using namespace imagejxx;
  require(dataset.shape().dimensions() == std::array<int, 6>{1, 1, 1, 1, 2, 1},
          "The installed reader changed literal TCZYXS dimensions");
  require(dataset.pixel_type() == PixelType::UInt16,
          "The installed reader changed the UInt16 type");
  require(dataset.sample_policy() == FloatingSamplePolicy::FiniteOnly,
          "The installed reader changed finite sample policy");
  const auto plane = dataset.plane({0, 0, 0});
  require(bool(plane), "The installed reader returned no plane");
  require(plane->width == 2 && plane->height == 1 && plane->channels == 1,
          "The installed reader changed the literal plane extent");
  require(plane->type == PixelType::UInt16 && plane->pixels.size() == 2,
          "The installed reader changed plane storage");
  require(plane->pixels[0] == 0.0 && plane->pixels[1] == 1.0,
          "The installed reader changed literal samples [0,1]");
}

void exact_inspection(const imagejxx::ImageFileInspection& inspected) {
  using namespace imagejxx;
  require(inspected.format == ImageFileFormat::ScientificTiffU16V1,
          "The installed IO selected a different scientific TIFF profile");
  require(inspected.container == TiffContainer::Classic,
          "The installed IO changed the classic TIFF container");
  require(inspected.series.size() == 1 && inspected.series[0].index == 0,
          "The installed IO changed the literal series count/index");
  require(inspected.series[0].shape.dimensions() == std::array<int, 6>{1, 1, 1, 1, 2, 1},
          "The installed inspector changed literal TCZYXS dimensions");
  require(inspected.series[0].pixel_type == PixelType::UInt16,
          "The installed inspector changed the UInt16 type");
}
} // namespace

int main(int argc, char** argv) {
  try {
    using namespace imagejxx;
    require(argc == 3, "Usage: imagejxx_scientific_sdk ORIGINAL_TIFF FRESH_OUTPUT_TIFF");
    const auto source_path = std::filesystem::u8path(argv[1]);
    const auto output_path = std::filesystem::u8path(argv[2]);
    require(source_path.is_absolute() && output_path.is_absolute(),
            "The SDK witness requires explicit absolute input/output paths");
    require(std::filesystem::is_regular_file(std::filesystem::symlink_status(source_path)),
            "The original TIFF must be a regular file");
    require(std::filesystem::file_size(source_path) == 116,
            "The original TIFF byte count changed");
    require(std::filesystem::symlink_status(output_path).type() == std::filesystem::file_type::not_found,
            "The round-trip output is not fresh");
    require(std::filesystem::is_directory(output_path.parent_path()),
            "The caller must create the owned output directory");

    ImageFileLimits limits;
    limits.max_file_bytes = limits.max_output_file_bytes = 1048576;
    limits.max_metadata_bytes = 65536;
    limits.max_total_samples = limits.max_plane_samples = 8;
    limits.max_codec_bytes = 1048576;
    limits.max_working_bytes = 4194304;
    const auto inspected = inspect_image_file(source_path.u8string(), limits);
    exact_inspection(inspected);
    require(inspected.file_bytes == 116, "The inspector changed the carrier byte count");
    auto input = read_image_series(source_path.u8string(), {}, limits);
    exact_dataset(input.dataset);
    const auto estimate = estimate_image_write(input.dataset, input.metadata, {}, limits);
    require(estimate.planes == 1 && estimate.samples == 2,
            "The installed writer estimated a different literal workload");
    require(estimate.encoded_upper_bound > 0 && estimate.encoded_upper_bound <= limits.max_output_file_bytes,
            "The installed writer exceeded the bounded output contract");
    write_image_series(output_path.u8string(), input.dataset, input.metadata, {}, limits);
    require(std::filesystem::is_regular_file(std::filesystem::symlink_status(output_path)),
            "The installed writer did not publish a regular output");
    const auto written = inspect_image_file(output_path.u8string(), limits);
    exact_inspection(written);
    require(written.file_bytes <= estimate.encoded_upper_bound,
            "The installed writer exceeded its admitted estimate");
    auto restored = read_image_series(output_path.u8string(), {}, limits);
    exact_dataset(restored.dataset);
    exact_dataset(input.dataset);
    require(std::filesystem::file_size(source_path) == 116,
            "Writing the owned output changed the original carrier");
    std::cout << "Installed imagejxx::io: " << checks
              << " checks passed; UInt16 [1,1,1,1,2,1], literal [0,1], round-trip verified\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "Installed imagejxx::io failed after " << checks << " checks: " << error.what() << '\n';
    return 1;
  }
}
