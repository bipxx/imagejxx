#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <imagejxx/io.hpp>
#include <iostream>
#include <limits>
#include <nlohmann/json.hpp>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif
using namespace imagejxx;
using Json = nlohmann::json;
namespace fs = std::filesystem;
namespace {
void check(bool v, const char *m) {
  if (!v)
    throw std::runtime_error(m);
}
Json manifest(const fs::path &p) {
  std::ifstream f(p);
  check(bool(f), "Open independent manifest");
  Json j;
  f >> j;
  return j;
}
template <class E, class F> void refuses(F f) {
  bool failed = false;
  try {
    f();
  } catch (const E &) {
    failed = true;
  }
  check(failed, "Expected typed metadata refusal");
}
std::string bytes(const fs::path &p) {
  std::ifstream f(p, std::ios::binary);
  check(bool(f), "Read observed bytes");
  return {std::istreambuf_iterator<char>(f), {}};
}
#ifdef _WIN32
struct PixelLock {
  HANDLE handle = INVALID_HANDLE_VALUE;
  OVERLAPPED range{};
  DWORD length;
  PixelLock(const fs::path &path, std::size_t offset, std::size_t count)
      : length(DWORD(count)) {
    check(offset <= MAXDWORD && count && count <= MAXDWORD,
          "Small independent pixel range");
    range.Offset = DWORD(offset);
    handle = CreateFileW(path.c_str(), GENERIC_READ,
                         FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                         nullptr, OPEN_EXISTING, 0, nullptr);
    check(handle != INVALID_HANDLE_VALUE, "Open pixel-lock handle");
    if (!LockFileEx(handle, LOCKFILE_EXCLUSIVE_LOCK | LOCKFILE_FAIL_IMMEDIATELY,
                    0, length, 0, &range)) {
      CloseHandle(handle);
      handle = INVALID_HANDLE_VALUE;
      throw std::runtime_error("Acquire pixel byte lock");
    }
    // Independent negative control: a second ordinary handle cannot read even
    // one byte in the declared pixel interval. This is not an exception-text
    // oracle.
    const auto probe =
        CreateFileW(path.c_str(), GENERIC_READ,
                    FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                    nullptr, OPEN_EXISTING, 0, nullptr);
    check(probe != INVALID_HANDLE_VALUE, "Open raw pixel probe");
    LARGE_INTEGER position{};
    position.QuadPart = offset;
    const auto seek = SetFilePointerEx(probe, position, nullptr, FILE_BEGIN);
    unsigned char byte = 0;
    DWORD got = 0;
    const auto read = ReadFile(probe, &byte, 1, &got, nullptr);
    const auto error = GetLastError();
    CloseHandle(probe);
    check(seek && !read && got == 0 && error == ERROR_LOCK_VIOLATION,
          "Raw pixel read was not independently blocked");
  }
  ~PixelLock() {
    if (handle != INVALID_HANDLE_VALUE) {
      UnlockFileEx(handle, 0, length, 0, &range);
      CloseHandle(handle);
    }
  }
};
#endif
void metadataRefuses(const fs::path &path, std::size_t offset,
                     std::size_t count, bool resource = false) {
#ifdef _WIN32
  PixelLock lock(path, offset, count);
#endif
  std::size_t callbacks = 0;
  ExecutionControl c;
  c.progress = [&](auto, auto) { ++callbacks; };
  c.checkpoint = [&] { ++callbacks; };
  c.stage_progress = [&](auto, auto, auto) { ++callbacks; };
  if (resource) {
    refuses<std::length_error>(
        [&] { inspect_image_file(path.u8string(), {}, c); });
    refuses<std::length_error>(
        [&] { read_image_series(path.u8string(), {}, {}, c); });
  } else {
    refuses<std::invalid_argument>(
        [&] { inspect_image_file(path.u8string(), {}, c); });
    refuses<std::invalid_argument>(
        [&] { read_image_series(path.u8string(), {}, {}, c); });
  }
  check(!callbacks, "Metadata refusal called user hooks");
}
} // namespace
int main(int argc, char **argv) {
  try {
    check(argc == 6,
          "Metadata test needs output/new/legacy/refusal/single-axis paths");
    const auto root =
        fs::absolute(fs::u8path(argv[1])) /
        std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count());
    check(!fs::exists(root), "Fresh metadata artifacts required");
    fs::create_directories(root);
    const auto fixtures = fs::absolute(fs::u8path(argv[2])),
               legacy = fs::absolute(fs::u8path(argv[3]));
    auto cases = manifest(fixtures / "SOURCE.json");
    std::size_t valid = 0, bad = 0;
    for (const auto &item : cases.at("files")) {
      const auto path = fixtures / item.at("file").get<std::string>();
      if (!item.at("valid").get<bool>()) {
        metadataRefuses(path, item.at("pixel_interval")[0],
                        item.at("pixel_interval")[1]);
        ++bad;
        continue;
      }
      auto info = inspect_image_file(path.u8string());
      auto result = read_image_series(path.u8string());
      check(result.dataset.shape().dimensions() ==
                item.at("shape").get<std::array<int, 6>>(),
            "Literal defaulted shape");
      const auto &cal = result.dataset.calibration();
      check(cal.spacing == item.at("spacing").get<std::array<double, 3>>() &&
                cal.unit == item.at("unit") &&
                cal.frame_interval == item.at("frame_interval") &&
                cal.time_unit == item.at("time_unit"),
            "Literal defaulted calibration");
      check(std::get<std::vector<std::string>>(
                result.metadata.at("defaulted_fields")) ==
                item.at("defaulted_fields").get<std::vector<std::string>>(),
            "Exact ordered default ledger");
      check(std::get<std::string>(result.metadata.at("source_description")) ==
                    item.at("description").get<std::string>() &&
                result.metadata == info.series[0].metadata,
            "Exact source provenance");
      const auto shape = result.dataset.shape();
      for (int t = 0; t < shape.t; ++t)
        for (int c = 0; c < shape.c; ++c)
          for (int z = 0; z < shape.z; ++z) {
            auto image = result.dataset.plane({t, c, z});
            for (int y = 0; y < shape.y; ++y)
              for (int x = 0; x < shape.x; ++x)
                check(image->at(x, y) ==
                          10000 * t + 1000 * c + 100 * z + 10 * y + x,
                      "Independent metadata carrier samples");
          }
      DatasetReadObserver observer;
      auto data = result.dataset.observe_reads(observer);
      auto estimate = estimate_image_write(data, result.metadata);
      std::vector<std::string> expected{
          "defaulted_fields", "source_description", "source_imagej_version"};
      if (result.metadata.count("source_software"))
        expected.push_back("source_software");
      check(estimate.canonicalized_fields == expected &&
                observer.counts().plane_reads == 0,
            "Exact canonicalization ledger without sample reads");
      write_image_series((root / path.filename()).u8string(), data,
                         result.metadata);
      check(observer.counts().plane_reads ==
                std::size_t(shape.t * shape.c * shape.z),
            "One plane at a time for metadata export");
      ++valid;
    }
    check(valid == 6 && bad == 17, "Independent metadata corpus counts");
    std::size_t legacyBad = 0;
    const auto legacyCases = manifest(legacy / "SOURCE.json");
    for (const auto &item : legacyCases.at("files"))
      if (!item.at("valid").get<bool>()) {
        std::size_t count = 2;
        for (auto n : item.at("shape"))
          count *= n.get<std::size_t>();
        metadataRefuses(legacy / item.at("file").get<std::string>(), 8, count);
        ++legacyBad;
      }
    check(legacyBad == 34, "Original malformed carrier count");
    const auto extra = fs::absolute(fs::u8path(argv[4]));
    const auto unsupported = manifest(extra / "SOURCE.json");
    std::size_t guardedRefusals = 0;
    for (const auto &item : unsupported.at("files")) {
      const auto path = extra / item.at("file").get<std::string>();
      check(!item.at("valid").get<bool>() &&
                item.at("pixel_intervals").size() == 1,
            "Explicit unsupported carrier and pixel interval");
      const auto &interval = item.at("pixel_intervals")[0];
      if(item.at("file")=="bigtiff-complete.tif") {
#ifdef _WIN32
        { PixelLock lock(path,interval[0],interval[1]);check(inspect_image_file(path.u8string()).container==TiffContainer::BigTiff,"BigTIFF inspection decoded pixels"); }
#endif
        const auto admitted=read_image_series(path.u8string());const auto plane=admitted.dataset.plane();
        check(plane->width==5 && plane->height==3 && plane->channels==1,"Promoted BigTIFF descriptor");
        for(int y=0;y<3;++y)for(int x=0;x<5;++x)check(plane->pixels[y*5+x]==double(y*10+x),"Promoted BigTIFF sample");
        ++guardedRefusals;continue;
      }
      if (item.at("file") == "float-complete.tif") {
#ifdef _WIN32
        { PixelLock lock(path, interval[0], interval[1]);
          check(inspect_image_file(path.u8string()).series[0].pixel_type == PixelType::Float32,
                "Float32 promotion decoded pixels during inspection"); }
#endif
        const auto admitted = read_image_series(path.u8string());
        check(admitted.dataset.pixel_type() == PixelType::Float32, "Promoted Float32 descriptor");
        auto plane = admitted.dataset.plane();
        for (std::size_t i=0; i<plane->pixels.size(); ++i) check(plane->pixels[i] == double(i), "Promoted Float32 sample");
        ++guardedRefusals;
        continue;
      }
      if(item.at("file")=="tiled-complete.tif") {
#ifdef _WIN32
        { PixelLock lock(path,interval[0],interval[1]);check(inspect_image_file(path.u8string()).format==ImageFileFormat::ScientificTiffU16TiledV1,"Tiled inspection decoded pixels"); }
#endif
        const auto admitted=read_image_series(path.u8string());const auto plane=admitted.dataset.plane();
        check(plane->width==16 && plane->height==16 && plane->channels==1,"Promoted tiled descriptor");
        for(int y=0;y<16;++y)for(int x=0;x<16;++x)check(plane->pixels[y*16+x]==double(y*10+x),"Promoted tiled sample");
        ++guardedRefusals;continue;
      }
      if(item.at("file")=="packbits-complete.tif") {
#ifdef _WIN32
        { PixelLock lock(path,interval[0],interval[1]);check(inspect_image_file(path.u8string()).format==ImageFileFormat::ScientificTiffU16PackBitsV1,"PackBits inspection decoded pixels"); }
#endif
        auto admitted=read_image_series(path.u8string());auto plane=admitted.dataset.plane();
        for(int y=0;y<3;++y)for(int x=0;x<5;++x)check(plane->pixels[y*5+x]==double(y*10+x),"Promoted PackBits sample");
        ++guardedRefusals;continue;
      }
      metadataRefuses(path, interval[0], interval[1],
                      item.at("error_category") == "resource");
      ++guardedRefusals;
    }
    check(guardedRefusals == 30, "Additional guarded malformed carrier count");
    const auto singleAxisPath = fs::absolute(fs::u8path(argv[5]));
    const auto singleAxisCases = manifest(singleAxisPath / "SOURCE.json");
    std::size_t singleAccepted = 0, singleRefused = 0;
    for (const auto &item : singleAxisCases.at("files")) {
      const auto path = singleAxisPath / item.at("file").get<std::string>();
      if (!item.at("valid").get<bool>()) {
        metadataRefuses(path, item.at("pixel_interval")[0],
                        item.at("pixel_interval")[1]);
        ++singleRefused;
        continue;
      }
      auto result = read_image_series(path.u8string());
      check(result.dataset.shape().dimensions() ==
                item.at("shape").get<std::array<int, 6>>(),
            "Explicit single-axis shape");
      check(std::get<std::vector<std::string>>(
                result.metadata.at("defaulted_fields")) ==
                item.at("defaulted_fields").get<std::vector<std::string>>(),
            "Single-axis exact default ledger");
      const auto shape = result.dataset.shape();
      for (int t = 0; t < shape.t; ++t)
        for (int c = 0; c < shape.c; ++c)
          for (int z = 0; z < shape.z; ++z) {
            auto plane = result.dataset.plane({t, c, z});
            for (int y = 0; y < 2; ++y)
              for (int x = 0; x < 3; ++x)
                check(plane->at(x, y) ==
                          10000 * t + 1000 * c + 100 * z + 10 * y + x,
                      "Single-axis literal samples");
          }
      ++singleAccepted;
    }
    check(singleAccepted == 2 && singleRefused == 4,
          "Narrow single-axis admission boundary");
#ifdef _WIN32
    {
      PixelLock lock(fixtures / "singleton-ledger.tif", 8, 12);
      refuses<std::runtime_error>([&] {
        read_image_series((fixtures / "singleton-ledger.tif").u8string());
      });
    }
#endif
    // A valid Dataset can still have positive XY spacing outside TIFF's UInt32
    // rational interval. Refuse at planning, before hooks/provider
    // reads/staging.
    Image image(3, 2, 1, PixelType::UInt16);
    image.pixels = {0, 1, 255, 256, 32768, 65535};
    const auto target = root / "preserved.bin";
    {
      std::ofstream f(target);
      f << "preserved";
    }
    ImageWriteRequest replace;
    replace.replace = FileReplacePolicy::ReplaceExisting;
    std::size_t bounds = 0;
    for (double spacing :
         {std::nextafter(1.0 / double(UINT32_MAX), 0.0),
          std::nextafter(double(UINT32_MAX),
                         std::numeric_limits<double>::infinity()),
          std::numeric_limits<double>::denorm_min(),
          std::numeric_limits<double>::max()})
      for (int axis : {0, 1}) {
        SpatialCalibration cal;
        cal.spacing[axis] = spacing;
        auto data = Dataset::from_planes({1, 1, 1, 2, 3, 1}, Stack{image}, cal);
        DatasetReadObserver observer;
        data = data.observe_reads(observer);
        std::size_t callbacks = 0;
        ExecutionControl c;
        c.progress = [&](auto, auto) { ++callbacks; };
        c.checkpoint = [&] { ++callbacks; };
        c.stage_progress = [&](auto, auto, auto) { ++callbacks; };
        refuses<std::invalid_argument>([&] { estimate_image_write(data); });
        refuses<std::invalid_argument>([&] {
          write_image_series(target.u8string(), data, {}, replace, {}, c);
        });
        check(!callbacks && observer.counts().plane_reads == 0 &&
                  bytes(target) == "preserved",
              "Invalid rational plan had effects");
        ++bounds;
      }
    SpatialCalibration aliases;
    aliases.unit = "pixel";
    aliases.time_unit = "sec";
    DatasetReadObserver aliasReads;
    const auto aliased =
        Dataset::from_planes({1, 1, 1, 2, 3, 1}, Stack{image}, aliases)
            .observe_reads(aliasReads);
    check(estimate_image_write(aliased).canonicalized_fields ==
                  std::vector<std::string>{"unit", "time_unit"} &&
              aliasReads.counts().plane_reads == 0,
          "Exact direct unit-alias canonicalization ledger");
    write_image_series((root / "unit-alias.tif").u8string(), aliased);
    const std::array<std::size_t ImageFileLimits::*, 9> limitFields{
        &ImageFileLimits::max_file_bytes,
        &ImageFileLimits::max_ifds,
        &ImageFileLimits::max_ifd_entries_per_page,
        &ImageFileLimits::max_metadata_bytes,
        &ImageFileLimits::max_total_samples,
        &ImageFileLimits::max_plane_samples,
        &ImageFileLimits::max_codec_bytes,
        &ImageFileLimits::max_working_bytes,
        &ImageFileLimits::max_output_file_bytes};
    for (auto member : limitFields) {
      ImageFileLimits limits;
      limits.*member = 0;
      const auto prior = aliasReads.counts().plane_reads;
      std::size_t calls = 0;
      ExecutionControl c;
      c.progress = [&](auto, auto) { ++calls; };
      c.checkpoint = [&] { ++calls; };
      c.stage_progress = [&](auto, auto, auto) { ++calls; };
      refuses<std::invalid_argument>(
          [&] { inspect_image_file("missing-file", limits, c); });
      refuses<std::invalid_argument>(
          [&] { read_image_series("missing-file", {}, limits, c); });
      refuses<std::invalid_argument>([&] {
        write_image_series(target.u8string(), aliased, {}, replace, limits, c);
      });
      check(!calls && aliasReads.counts().plane_reads == prior &&
                bytes(target) == "preserved",
            "Zero budget had effects");
    }
    for (const auto &entry : fs::directory_iterator(root))
      check(entry.path().filename().u8string().find(".tmp.") ==
                std::string::npos,
            "Invalid calibration leaked staging");
    std::cout << "PASS metadata defaults6; invalid calibration17; original "
                 "malformed34; outside rational bounds"
              << bounds
              << "; direct unit-alias ledger; zero budgets9; additional "
                 "guarded carriers (including promoted Float32)"
              << guardedRefusals;
#ifdef _WIN32
    std::cout << "; Windows pixel-byte locks with independent raw-read and "
                 "valid-file controls";
#endif
    std::cout << "\nARTIFACTS " << root.u8string() << '\n';
    return 0;
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
