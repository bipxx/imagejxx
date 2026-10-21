#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <imagejxx/io.hpp>
#include <iostream>
#include <limits>
using namespace imagejxx;
namespace {
void check(bool value, const char *message) {
  if (!value)
    throw std::runtime_error(message);
}
template <class Error = std::exception, class Function>
void refuses(Function function) {
  bool refused = false;
  try {
    function();
  } catch (const Error &) {
    refused = true;
  }
  check(refused, "Expected scientific TIFF refusal");
}
std::vector<unsigned char> bytes(const std::filesystem::path &path) {
  std::ifstream f(path, std::ios::binary);
  check(bool(f), "Open fixture bytes");
  return {std::istreambuf_iterator<char>(f), {}};
}
double expected(int t, int c, int z, int y, int x, int width) {
  return 10000 * t + 1000 * c + 100 * z + y * width + x;
}
void equations(const Dataset &data) {
  const auto shape = data.shape();
  for (int t = 0; t < shape.t; ++t)
    for (int c = 0; c < shape.c; ++c)
      for (int z = 0; z < shape.z; ++z) {
        auto image = data.plane({t, c, z});
        check(image->type == PixelType::UInt16, "UInt16 output type");
        for (int y = 0; y < shape.y; ++y)
          for (int x = 0; x < shape.x; ++x)
            check(image->at(x, y) == expected(t, c, z, y, x, shape.x),
                  "Independent TCZ pixel equation");
      }
}
Dataset make(DatasetShape shape = {2, 3, 2, 3, 5, 1},
             SpatialCalibration calibration = {}) {
  Stack planes;
  for (int t = 0; t < shape.t; ++t)
    for (int c = 0; c < shape.c; ++c)
      for (int z = 0; z < shape.z; ++z) {
        Image image(shape.x, shape.y, 1, PixelType::UInt16);
        for (int y = 0; y < shape.y; ++y)
          for (int x = 0; x < shape.x; ++x)
            image.at(x, y) = expected(t, c, z, y, x, shape.x);
        planes.push_back(std::move(image));
      }
  return Dataset::from_planes(shape, std::move(planes), calibration);
}
} // namespace
int main(int argc, char **argv) {
  try {
    check(argc == 3, "TIFF test paths required");
    namespace fs = std::filesystem;
    const auto fixtures = fs::absolute(fs::u8path(argv[2]));
    const auto dir =
        fs::absolute(fs::u8path(argv[1])) /
        std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count());
    check(!fs::exists(dir), "Fresh TIFF artifact path");
    fs::create_directories(dir);
    std::size_t valid = 0, bad = 0;
    for (const auto &entry : fs::directory_iterator(fixtures))
      if (entry.path().extension() == ".tif") {
        const auto name = entry.path().filename().u8string();
        const bool good = name == "little.tif" || name == "big.tif" ||
                          name == "defaults.tif" ||
                          name == "shared-values.tif" ||
                          name == "imagej-description.tif";
        std::vector<std::pair<std::size_t, std::size_t>> progress;
        ExecutionControl control;
        control.progress = [&](auto n, auto total) {
          progress.push_back({n, total});
        };
        if (good) {
          auto inspection =
              inspect_image_file(entry.path().u8string(), {}, control);
          check(progress ==
                    std::vector<std::pair<std::size_t, std::size_t>>{{0, 1},
                                                                     {1, 1}},
                "Inspection coarse progress");
          progress.clear();
          auto result =
              read_image_series(entry.path().u8string(), {}, {}, control);
          equations(result.dataset);
          check(progress ==
                    std::vector<std::pair<std::size_t, std::size_t>>{{0, 1},
                                                                     {1, 1}},
                "Read coarse progress");
          check(inspection.series.size() == 1 &&
                    inspection.series[0].shape.dimensions() ==
                        result.dataset.shape().dimensions(),
                "Inspection descriptor");
          if (name != "defaults.tif") {
            check(result.dataset.shape().dimensions() ==
                      std::array<int, 6>{2, 3, 2, 3, 5, 1},
                  "Explicit TCZYXS fixture");
            check(result.dataset.calibration().spacing ==
                      std::array<double, 3>{
                          0.5, name == "shared-values.tif" ? 0.5 : 1.25, 2.75},
                  "Fixture calibration");
            check(result.dataset.calibration().unit == "um" &&
                      result.dataset.calibration().time_unit == "ms" &&
                      result.dataset.calibration().frame_interval == 0.125,
                  "Fixture units/time");
          }
          ++valid;
        } else {
          refuses<std::invalid_argument>([&] {
            inspect_image_file(entry.path().u8string(), {}, control);
          });
          refuses<std::invalid_argument>([&] {
            read_image_series(entry.path().u8string(), {}, {}, control);
          });
          check(progress.empty(), "Malformed admission called user callback");
          ++bad;
        }
      }
    check(valid == 5 && bad == 34, "Independent fixture corpus");
    const auto fixture = (fixtures / "little.tif").u8string();
    for (int which = 0; which < 7; ++which) {
      ImageFileLimits limits;
      if (which == 0)
        limits.max_file_bytes = 8;
      else if (which == 1)
        limits.max_ifds = 1;
      else if (which == 2)
        limits.max_ifd_entries_per_page = 1;
      else if (which == 3)
        limits.max_metadata_bytes = 8;
      else if (which == 4)
        limits.max_total_samples = 5;
      else if (which == 5)
        limits.max_plane_samples = 5;
      else
        limits.max_working_bytes = 1;
      int calls = 0;
      ExecutionControl c;
      c.progress = [&](auto, auto) { ++calls; };
      c.checkpoint = [&] { ++calls; };
      c.stage_progress = [&](auto, auto, auto) { ++calls; };
      refuses<std::length_error>(
          [&] { read_image_series(fixture, {}, limits, c); });
      check(calls == 0, "Resource admission called user callbacks");
    }
    ImageFileLimits tiny_codec;
    tiny_codec.max_codec_bytes = 1;
    int codec_calls = 0;
    ExecutionControl no_callbacks;
    no_callbacks.progress = [&](auto, auto) { ++codec_calls; };
    refuses<std::length_error>(
        [&] { read_image_series(fixture, {}, tiny_codec, no_callbacks); });
    check(codec_calls == 0, "Codec allocation cap called progress");
    refuses<std::out_of_range>([&] { read_image_series("missing-file", {1}); });
    SpatialCalibration calibration;
    calibration.spacing = {0.3, 2.5, 2.75};
    calibration.frame_interval = 0.125;
    calibration.unit = "um";
    calibration.time_unit = "ms";
    auto source = make({2, 3, 2, 3, 5, 1}, calibration);
    DatasetReadObserver reads;
    source = source.observe_reads(reads);
    MetadataObject metadata{{"document_name", "Test"},
                            {"artist", "Native fixture"},
                            {"date_time", "2026:10:18 12:34:56"}};
    const auto estimate = estimate_image_write(source, metadata);
    check(reads.counts().plane_reads == 0 && reads.counts().region_reads == 0,
          "Estimate read provider pixels");
    check(estimate.planes == 12 && estimate.samples == 180 &&
              estimate.xy_relative_error[0] <= 1e-6,
          "Write estimate");
    const auto output = dir / "written.tif";
    write_image_series(output.u8string(), source, metadata);
    check(reads.counts().plane_reads == 12,
          "Writer must borrow one plane at a time");
    auto loaded = read_image_series(output.u8string());
    equations(loaded.dataset);
    check(fs::file_size(output) == estimate.encoded_upper_bound,
          "Frozen output size");
    check(loaded.dataset.calibration().spacing ==
              estimate.encoded_calibration.spacing,
          "Frozen rational calibration");
    const auto original = bytes(output);
    refuses<std::runtime_error>(
        [&] { write_image_series(output.u8string(), source, metadata); });
    check(bytes(output) == original, "New-file policy overwrote winner");
    write_image_series(output.u8string(), loaded.dataset, loaded.metadata,
                       {ImageFileFormat::ScientificTiffU16V1,
                        FileReplacePolicy::ReplaceExisting});
    const auto canonical = bytes(output);
    auto cancel = std::make_shared<CancellationToken>();
    ExecutionControl control;
    control.cancellation = cancel;
    control.progress = [&](auto n, auto) {
      if (n == 1)
        cancel->cancel();
    };
    refuses<OperationCancelled>([&] {
      write_image_series(output.u8string(), source, metadata,
                         {ImageFileFormat::ScientificTiffU16V1,
                          FileReplacePolicy::ReplaceExisting},
                         {}, control);
    });
    check(bytes(output) == canonical, "Completion cancel changed destination");
    for (const auto &entry : fs::directory_iterator(dir))
      check(entry.path().extension() == ".tif",
            "Owned streaming temporary leaked");
    for (double spacing : {1.0 / double(UINT32_MAX), 0.5, 1.25, 2.5,
                           0.123456789, double(UINT32_MAX)}) {
      auto cal = calibration;
      cal.spacing[0] = cal.spacing[1] = spacing;
      auto data = make({1, 1, 1, 2, 3, 1}, cal);
      auto e = estimate_image_write(data);
      check(e.xy_relative_error[0] <= 1e-6 && e.xy_resolution[0][0] &&
                e.xy_resolution[0][1],
            "Full rational interval");
      const auto path = dir / ("spacing-" + std::to_string(spacing) + ".tif");
      write_image_series(path.u8string(), data);
      auto roundtrip = read_image_series(path.u8string());
      check(roundtrip.dataset.calibration().spacing ==
                e.encoded_calibration.spacing,
            "Rational wire pair roundtrip");
    }
    const auto failures = dir / "failures";
    fs::create_directory(failures);
    struct CallbackFailure {};
    for (bool writing : {false, true}) {
      ExecutionControl throwing;
      throwing.progress = [](auto, auto) { throw CallbackFailure{}; };
      refuses<CallbackFailure>([&] {
        if (writing)
          write_image_series((failures / "throw.tif").u8string(), source, {},
                             {}, {}, throwing);
        else
          read_image_series(fixture, {}, {}, throwing);
      });
      throwing.progress = {};
      throwing.checkpoint = [] { throw CallbackFailure{}; };
      refuses<CallbackFailure>([&] {
        if (writing)
          write_image_series((failures / "checkpoint.tif").u8string(), source,
                             {}, {}, {}, throwing);
        else
          read_image_series(fixture, {}, {}, throwing);
      });
      auto canceled = std::make_shared<CancellationToken>();
      ExecutionControl row_cancel;
      row_cancel.cancellation = canceled;
      row_cancel.stage_progress = [&](auto, auto done, auto total) {
        check(total == 36, "Fixed total row progress");
        if (done == 2)
          canceled->cancel();
      };
      refuses<OperationCancelled>([&] {
        if (writing)
          write_image_series((failures / "row.tif").u8string(), source, {}, {},
                             {}, row_cancel);
        else
          read_image_series(fixture, {}, {}, row_cancel);
      });
    }
    check(fs::is_empty(failures), "Failure/cancellation leaked staged files");
    for (int budget = 0; budget < 7; ++budget) {
      ImageFileLimits limits;
      if (budget == 0)
        limits.max_output_file_bytes = 8;
      else if (budget == 1)
        limits.max_ifds = 1;
      else if (budget == 2)
        limits.max_ifd_entries_per_page = 1;
      else if (budget == 3)
        limits.max_metadata_bytes = 1;
      else if (budget == 4)
        limits.max_plane_samples = 1;
      else if (budget == 5)
        limits.max_total_samples = 1;
      else
        limits.max_working_bytes = 1;
      int callbacks = 0;
      ExecutionControl c;
      c.progress = [&](auto, auto) { ++callbacks; };
      c.checkpoint = [&] { ++callbacks; };
      const auto before = reads.counts().plane_reads;
      refuses<std::length_error>([&] {
        write_image_series((failures / "limit.tif").u8string(), source, {}, {},
                           limits, c);
      });
      check(callbacks == 0 && reads.counts().plane_reads == before &&
                fs::is_empty(failures),
            "Write resource admission had effects");
    }
    ExecutionControl pre;
    pre.cancellation = std::make_shared<CancellationToken>();
    pre.cancellation->cancel();
    refuses<OperationCancelled>(
        [&] { inspect_image_file("missing-file", {}, pre); });
    refuses<OperationCancelled>(
        [&] { read_image_series("missing-file", {}, {}, pre); });
    refuses<OperationCancelled>([&] {
      write_image_series((failures / "pre.tif").u8string(), source, {}, {}, {},
                         pre);
    });
    // Mutating caller options in progress must not change the admitted
    // operation.
    auto mutable_metadata = metadata;
    ImageFileLimits mutable_limits;
    ImageWriteRequest mutable_request;
    ExecutionControl freeze;
    freeze.progress = [&](auto n, auto) {
      if (n == 0) {
        mutable_metadata["artist"] = std::string("changed");
        mutable_limits.max_output_file_bytes = 1;
        mutable_request.format = static_cast<ImageFileFormat>(999);
        freeze.checkpoint = [] { throw CallbackFailure{}; };
      }
    };
    const auto frozen_output = failures / "frozen.tif";
    write_image_series(frozen_output.u8string(), source, mutable_metadata,
                       mutable_request, mutable_limits, freeze);
    auto frozen_metadata = read_image_series(frozen_output.u8string()).metadata;
    check(std::get<std::string>(frozen_metadata.at("artist")) ==
              "Native fixture",
          "Caller mutation changed frozen metadata");
    const auto race_output = failures / "race.tif";
    ExecutionControl race;
    race.progress = [&](auto n, auto) {
      if (n == 1) {
        std::ofstream f(race_output, std::ios::binary);
        f << "winner";
      }
    };
    refuses<std::runtime_error>([&] {
      write_image_series(race_output.u8string(), source, {}, {}, {}, race);
    });
    check(bytes(race_output) ==
              std::vector<unsigned char>{'w', 'i', 'n', 'n', 'e', 'r'},
          "FailIfExists publication replaced a race winner");
    const auto changed = failures / "changed.tif";
    fs::copy_file(fixtures / "little.tif", changed);
    ExecutionControl mutate;
    bool mutated = false;
    mutate.stage_progress = [&](auto, auto done, auto) {
      if (done == 1 && !mutated) {
        std::ofstream f(changed, std::ios::binary | std::ios::app);
        f << 'x';
        mutated = true;
      }
    };
    refuses<std::runtime_error>(
        [&] { read_image_series(changed.u8string(), {}, {}, mutate); });
    check(mutated, "Observable input change was not exercised");
    // Exercise several strips including a final partial strip and a full 64K
    // row.
    for (const auto shape : {DatasetShape{1, 1, 1, 130, 5, 1},
                             DatasetShape{1, 1, 1, 1, 65536, 1}}) {
      const auto path =
          failures / ("layout-" + std::to_string(shape.x) + ".tif");
      auto data = make(shape, calibration);
      write_image_series(path.u8string(), data);
      equations(read_image_series(path.u8string()).dataset);
    }
    for (const auto &entry : fs::directory_iterator(failures))
      check(entry.path().extension() == ".tif",
            "Failure-path streaming temporary leaked");
    std::cout << "PASS scientific TIFF native service: " << valid
              << " valid and " << bad << " malformed carriers\n";
    return 0;
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
