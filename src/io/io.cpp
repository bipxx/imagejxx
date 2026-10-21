#include "../native/atomic_file.hpp"
#include "../native/kernel_control.hpp"
#include "internal.hpp"
#include "staged.hpp"
#include <algorithm>
#include <cstring>
namespace imagejxx {
namespace {
bool same_calibration(const SpatialCalibration &a,
                      const SpatialCalibration &b) {
  return a.spacing == b.spacing && a.origin == b.origin &&
         a.direction == b.direction && a.unit == b.unit &&
         a.time_unit == b.time_unit && a.frame_interval == b.frame_interval &&
         a.time_origin == b.time_origin;
}
} // namespace
ImageFileInspection inspect_image_file(const std::string &path_arg,
                                       const ImageFileLimits &limits_arg,
                                       const ExecutionControl &control_arg) {
  using namespace io_detail;
  const auto limits = limits_arg;
  const auto control = control_arg;
  valid_limits(limits);
  token(control);
  const auto path = native_path(path_arg, limits.max_metadata_bytes);
  const auto path_bytes = path.u8string().size();
  Source source(path, limits.max_file_bytes, control);
  auto layout = inspect(source, path_bytes, limits, control);
  ImageFileInspection result;
  result.format = image_profile(layout.info.pixel_type,layout.info.shape.s,layout.compression,layout.predictor,layout.tiled).format;
  result.container = layout.bigtiff ? TiffContainer::BigTiff : TiffContainer::Classic;
  result.file_bytes = layout.file_bytes;
  result.metadata_bytes = layout.metadata_bytes;
  result.series.push_back(std::move(layout.info));
  progress(control, 0);
  source.unchanged();
  progress(control, 1);
  source.unchanged();
  token(control);
  return result;
}
ImageReadResult read_image_series(const std::string &path_arg,
                                  const ImageReadRequest &request_arg,
                                  const ImageFileLimits &limits_arg,
                                  const ExecutionControl &control_arg) {
  using namespace io_detail;
  const auto limits = limits_arg;
  const auto control = control_arg;
  const auto request = request_arg;
  valid_limits(limits);
  token(control);
  require(request.sample_policy == FloatingSamplePolicy::FiniteOnly ||
          request.sample_policy == FloatingSamplePolicy::PreserveQuietNaNFloat32 ||
          request.sample_policy == FloatingSamplePolicy::PreserveIEEEFloat32,
          "Invalid TIFF floating sample policy");
  if (request.series_index != 0)
    throw std::out_of_range(
        "Scientific TIFF profile contains only series index zero");
  const auto path = native_path(path_arg, limits.max_metadata_bytes);
  const auto path_bytes = path.u8string().size();
  Source source(path, limits.max_file_bytes, control);
  auto layout = inspect(source, path_bytes, limits, control);
  require(layout.info.pixel_type == PixelType::Float32 ||
          request.sample_policy == FloatingSamplePolicy::FiniteOnly,
          "Nonfinite TIFF sample policies require Float32");
  layout.info.sample_policy = request.sample_policy;
  cap(read_working(layout, limits), limits.max_working_bytes,
      "TIFF read working-byte limit");
  auto metadata = layout.info.metadata;
  auto dataset = decode(source, layout, limits, control);
  ImageReadResult result{std::move(dataset), std::move(metadata)};
  source.unchanged();
  checkpoint(control);
  progress(control, 1);
  source.unchanged();
  token(control);
  return result;
}
ImageWriteEstimate estimate_image_write(const Dataset &source,
                                        const MetadataObject &metadata,
                                        const ImageWriteRequest &request,
                                        const ImageFileLimits &limits) {
  return io_detail::plan_write(source, metadata, request, limits).estimate;
}
void write_image_series(const std::string &path_arg, const Dataset &source_arg,
                        const MetadataObject &metadata_arg,
                        const ImageWriteRequest &request_arg,
                        const ImageFileLimits &limits_arg,
                        const ExecutionControl &control_arg) {
  const auto control = control_arg;
  auto staged = io_detail::stage_image_series(
      path_arg, source_arg, metadata_arg, request_arg, limits_arg, control);
  staged->publish(control);
}
} // namespace imagejxx
namespace imagejxx::io_detail {
struct StagedImageWrite::State {
  std::unique_ptr<detail::StagedAtomicStream> staged;
  std::unique_ptr<Source> written;
  bool replace = false;
};
StagedImageWrite::StagedImageWrite(std::unique_ptr<State> state)
    : state_(std::move(state)) {}
StagedImageWrite::~StagedImageWrite() = default;
bool StagedImageWrite::active() const noexcept { return bool(state_); }
void StagedImageWrite::publish(const ExecutionControl &control_arg) {
  const auto control = control_arg;
  if (!state_)
    throw std::logic_error("Scientific TIFF staging already consumed");
  auto state = std::move(state_);
  ExecutionScope scope(control);
  state->written->unchanged();
  checkpoint(control);
  progress(control, 1);
  state->written->unchanged();
  token(control);
  state->staged->publish(state->replace);
}
std::unique_ptr<StagedImageWrite> stage_image_series(
    const std::string &path_arg, const Dataset &source_arg,
    const MetadataObject &metadata_arg, const ImageWriteRequest &request_arg,
    const ImageFileLimits &limits_arg, const ExecutionControl &control_arg) {
  using namespace io_detail;
  const auto limits = limits_arg;
  const auto request = request_arg;
  const auto control = control_arg;
  valid_limits(limits);
  token(control);
  const auto path = native_path(path_arg, limits.max_metadata_bytes);
  auto plan =
      plan_write(source_arg, metadata_arg, request, limits, control);
  const Dataset source = source_arg;
  // Resolve the existing parent before callbacks; never create directories.
  const auto destination =
      std::filesystem::canonical(path.parent_path()) / path.filename();
  std::error_code error;
  const auto status = std::filesystem::symlink_status(destination, error);
  if (error && error != std::errc::no_such_file_or_directory)
    throw std::runtime_error("Could not inspect TIFF destination");
  if (status.type() != std::filesystem::file_type::not_found) {
    if (request.replace == FileReplacePolicy::FailIfExists)
      throw std::runtime_error("TIFF destination already exists");
    if (status.type() == std::filesystem::file_type::directory)
      throw std::runtime_error("TIFF destination is a directory");
  }
  auto state = std::make_unique<StagedImageWrite::State>();
  state->staged = std::make_unique<detail::StagedAtomicStream>(destination);
  state->replace = request.replace == FileReplacePolicy::ReplaceExisting;
  auto &staged = *state->staged;
  validate_path_length(staged.temporary_path());
  const auto path_charge =
      add(mul(4, destination.u8string().size()),
          mul(4, staged.temporary_path().u8string().size()));
  cap(add(plan.estimate.metadata_bytes, path_charge), limits.max_metadata_bytes,
      "TIFF destination path metadata limit");
  cap(add(plan.estimate.working_bytes, mul(4, path_charge)),
      limits.max_working_bytes, "TIFF destination path working-byte limit");
  token(control);
  progress(control, 0);
  ExecutionScope scope(control);
  staged.open(control);
  encode_rows(staged, source, plan, control);
  staged.close(control);
  auto verification_limits = limits;
  verification_limits.max_file_bytes = limits.max_output_file_bytes;
  const auto verify_bytes = staged.temporary_path().u8string().size();
  state->written = std::make_unique<Source>(
      staged.temporary_path(), verification_limits.max_file_bytes, control);
  auto &written = *state->written;
  const auto actual =
      inspect(written, verify_bytes, verification_limits, control);
  require(actual.file_bytes == plan.encoded_bytes && actual.file_bytes <= plan.estimate.encoded_upper_bound &&
              actual.compression == plan.compression && actual.predictor == plan.predictor && actual.tiled == plan.tiled && actual.bigtiff == plan.bigtiff &&
              actual.info.shape.dimensions() == plan.shape.dimensions() &&
              actual.info.pixel_type == plan.pixel_type &&
              same_calibration(actual.info.calibration,
                               plan.estimate.encoded_calibration) &&
              actual.resolution == plan.estimate.xy_resolution,
          "Written TIFF descriptors disagree with frozen plan");
  std::array<unsigned char, 65536> buffer;
  for (std::size_t offset = 0; offset < plan.prefix.size();) {
    const auto n = std::min(buffer.size(), plan.prefix.size() - offset);
    written.read(offset, buffer.data(), n, control);
    require(std::memcmp(buffer.data(), plan.prefix.data() + offset, n) == 0,
            "Written TIFF metadata differs from frozen bytes");
    offset += n;
  }
  written.unchanged();
  token(control);
  return std::unique_ptr<StagedImageWrite>(
      new StagedImageWrite(std::move(state)));
}
} // namespace imagejxx::io_detail
