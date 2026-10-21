#pragma once
#include <imagejxx/execution.hpp>
namespace imagejxx {
enum class ImageFileFormat { ScientificTiffU16V1, ScientificTiffFloat32V1, ScientificTiffU8V1, ScientificTiffRgb8V1, ScientificTiffU16DeflateV1, ScientificTiffFloat32DeflateV1, ScientificTiffU8DeflateV1, ScientificTiffRgb8DeflateV1, ScientificTiffU16PackBitsV1, ScientificTiffFloat32PackBitsV1, ScientificTiffU8PackBitsV1, ScientificTiffRgb8PackBitsV1, ScientificTiffU16LzwV1, ScientificTiffFloat32LzwV1, ScientificTiffU8LzwV1, ScientificTiffRgb8LzwV1, ScientificTiffU16DeflateHorizontalV1, ScientificTiffU8DeflateHorizontalV1, ScientificTiffRgb8DeflateHorizontalV1, ScientificTiffU16LzwHorizontalV1, ScientificTiffU8LzwHorizontalV1, ScientificTiffRgb8LzwHorizontalV1, ScientificTiffFloat32DeflateFloatingV1, ScientificTiffFloat32LzwFloatingV1, ScientificTiffU16TiledV1, ScientificTiffU8TiledV1, ScientificTiffRgb8TiledV1, ScientificTiffFloat32TiledV1, ScientificTiffU16DeflateTiledV1, ScientificTiffU8DeflateTiledV1, ScientificTiffRgb8DeflateTiledV1, ScientificTiffFloat32DeflateTiledV1, ScientificTiffU16PackBitsTiledV1, ScientificTiffU8PackBitsTiledV1, ScientificTiffRgb8PackBitsTiledV1, ScientificTiffFloat32PackBitsTiledV1, ScientificTiffU16LzwTiledV1, ScientificTiffU8LzwTiledV1, ScientificTiffRgb8LzwTiledV1, ScientificTiffFloat32LzwTiledV1, ScientificTiffU16DeflateHorizontalTiledV1, ScientificTiffU8DeflateHorizontalTiledV1, ScientificTiffRgb8DeflateHorizontalTiledV1, ScientificTiffU16LzwHorizontalTiledV1, ScientificTiffU8LzwHorizontalTiledV1, ScientificTiffRgb8LzwHorizontalTiledV1, ScientificTiffFloat32DeflateFloatingTiledV1, ScientificTiffFloat32LzwFloatingTiledV1 };
enum class TiffContainer { Classic, BigTiff };
enum class FileReplacePolicy { FailIfExists, ReplaceExisting };
struct ImageFileLimits {
  std::size_t max_file_bytes = 536870912, max_ifds = 4096,
              max_ifd_entries_per_page = 64, max_metadata_bytes = 1048576,
              max_total_samples = 33554432, max_plane_samples = 16777216,
              max_codec_bytes = 67108864, max_working_bytes = 536870912,
              max_output_file_bytes = 134217728;
};
struct ImageSeriesInfo {
  std::size_t index = 0;
  DatasetShape shape;
  PixelType pixel_type = PixelType::UInt16;
  FloatingSamplePolicy sample_policy = FloatingSamplePolicy::FiniteOnly;
  SpatialCalibration calibration;
  MetadataObject metadata;
};
struct ImageFileInspection {
  ImageFileFormat format = ImageFileFormat::ScientificTiffU16V1;
  std::size_t file_bytes = 0, metadata_bytes = 0;
  std::vector<ImageSeriesInfo> series;
  TiffContainer container = TiffContainer::Classic;
};
struct ImageReadRequest {
  std::size_t series_index = 0;
  FloatingSamplePolicy sample_policy = FloatingSamplePolicy::FiniteOnly;
};
struct ImageReadResult {
  Dataset dataset;
  MetadataObject metadata;
};
struct ImageWriteRequest {
  ImageFileFormat format = ImageFileFormat::ScientificTiffU16V1;
  FileReplacePolicy replace = FileReplacePolicy::FailIfExists;
  TiffContainer container = TiffContainer::Classic;
};
struct ImageWriteEstimate {
  std::size_t planes = 0, samples = 0, metadata_bytes = 0,
              encoded_upper_bound = 0, working_bytes = 0;
  SpatialCalibration encoded_calibration;
  // Stored TIFF X/Y resolution numerator/denominator, not spacing pairs.
  std::array<std::array<std::uint32_t, 2>, 2> xy_resolution{};
  std::array<double, 2> xy_absolute_error{}, xy_relative_error{};
  std::vector<std::string> canonicalized_fields;
};
ImageFileInspection inspect_image_file(const std::string &,
                                       const ImageFileLimits & = {},
                                       const ExecutionControl & = {});
ImageReadResult read_image_series(const std::string &,
                                  const ImageReadRequest & = {},
                                  const ImageFileLimits & = {},
                                  const ExecutionControl & = {});
ImageWriteEstimate estimate_image_write(const Dataset &,
                                        const MetadataObject & = {},
                                        const ImageWriteRequest & = {},
                                        const ImageFileLimits & = {});
void write_image_series(const std::string &, const Dataset &,
                        const MetadataObject & = {},
                        const ImageWriteRequest & = {},
                        const ImageFileLimits & = {},
                        const ExecutionControl & = {});
} // namespace imagejxx
