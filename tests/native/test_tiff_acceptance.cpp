#include "atomic_file.hpp"
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <imagejxx/document.hpp>
#include <imagejxx/io.hpp>
#include <iostream>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif
using namespace imagejxx;
namespace fs = std::filesystem;
namespace {
void check(bool value, const char *message) {
  if (!value)
    throw std::runtime_error(message);
}
template <class E = std::exception, class F> std::string refuses(F f) {
  try {
    f();
  } catch (const E &e) {
    return e.what();
  }
  throw std::runtime_error("Expected acceptance fault");
}
std::string bytes(const fs::path &p) {
  std::ifstream f(p, std::ios::binary);
  check(bool(f), "Open observed bytes");
  return {std::istreambuf_iterator<char>(f), {}};
}
void noTemporary(const fs::path &p) {
  for (const auto &entry : fs::directory_iterator(p))
    check(entry.path().filename().u8string().find(".tmp.") == std::string::npos,
          "Fault leaked temporary");
}
#ifdef _WIN32
struct LockedFile {
  HANDLE handle = INVALID_HANDLE_VALUE;
  OVERLAPPED overlap{};
  void lock(const fs::path &p) {
    handle = CreateFileW(p.c_str(), GENERIC_READ,
                         FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                         nullptr, OPEN_EXISTING, 0, nullptr);
    check(handle != INVALID_HANDLE_VALUE, "Open real fault handle");
    check(LockFileEx(handle,
                     LOCKFILE_EXCLUSIVE_LOCK | LOCKFILE_FAIL_IMMEDIATELY, 0,
                     MAXDWORD, 0, &overlap),
          "Lock real output bytes");
  }
  void close() {
    if (handle != INVALID_HANDLE_VALUE) {
      UnlockFileEx(handle, 0, MAXDWORD, 0, &overlap);
      CloseHandle(handle);
      handle = INVALID_HANDLE_VALUE;
    }
  }
  ~LockedFile() { close(); }
};
#endif
} // namespace
int main(int argc, char **argv) {
  try {
    check(argc == 3, "Acceptance artifact/fixture paths required");
    const auto root =
        fs::absolute(fs::u8path(argv[1])) /
        std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count());
    fs::create_directories(root);
    const auto fixtures = fs::absolute(fs::u8path(argv[2]));
    const std::vector<double> literal{0, 1, 255, 256, 32768, 65535};
    for (auto name : {"literal-ii.tif", "literal-mm.tif"}) {
      const auto result = read_image_series((fixtures / name).u8string());
      check(result.dataset.shape().dimensions() ==
                std::array<int, 6>{1, 1, 1, 2, 3, 1},
            "TU01 literal shape");
      check(result.dataset.plane({0, 0, 0})->pixels == literal,
            "TU01 exact endian samples");
    }
    auto data =
        read_image_series((fixtures / "czt-exact.tif").u8string()).dataset;
    check(data.shape().dimensions() == std::array<int, 6>{2, 3, 2, 2, 3, 1},
          "TU02 exact dimensions");
    const std::array<int, 12> pages{0, 3, 1, 4, 2, 5, 6, 9, 7, 10, 8, 11};
    int native = 0;
    for (int t = 0; t < 2; ++t)
      for (int c = 0; c < 3; ++c)
        for (int z = 0; z < 2; ++z) {
          check(pages[native++] == ((t * 2) + z) * 3 + c,
                "TU02 independent native/file table");
          auto image = data.plane({t, c, z});
          for (int y = 0; y < 2; ++y)
            for (int x = 0; x < 3; ++x)
              check(image->at(x, y) ==
                        10000 * t + 1000 * c + 100 * z + 10 * y + x,
                    "TU02 exact voxel equation");
        }
    for (auto name : {"rows-1.tif", "rows-2.tif", "rows-9.tif"}) {
      auto result = read_image_series((fixtures / name).u8string());
      for (int y = 0; y < 3; ++y)
        for (int x = 0; x < 5; ++x)
          check(result.dataset.plane({0, 0, 0})->at(x, y) == 10 * y + x,
                "TU03 independent strips");
    }
    const auto &cal = data.calibration();
    check(cal.spacing == std::array<double, 3>{.5, 1.25, 2.5} &&
              cal.unit == "um" && cal.frame_interval == 2.5 &&
              cal.time_unit == "ms",
          "TU04 exact calibration");
    check(cal.voxel_to_world({2, 1, 1}) == std::array<double, 3>{1, 1.25, 2.5},
          "TU04 literal world point");
    const auto rationalSource = Dataset::from_planes(
        {1, 1, 1, 2, 3, 1}, Stack{*data.plane({0, 0, 0})}, cal);
    for (int which = 0; which < 3; ++which) {
      auto calibration = cal;
      if (which == 0)
        calibration.spacing = {.108, std::acos(-1.0) / 10, 1.23456789};
      if (which == 1)
        calibration.spacing = {1.0 / double(UINT32_MAX), double(UINT32_MAX),
                               2.5};
      auto source = Dataset::from_planes(
          rationalSource.shape(), Stack{*rationalSource.plane({0, 0, 0})},
          calibration);
      write_image_series(
          (root / ("rational-" + std::to_string(which) + ".tif")).u8string(),
          source);
    }
    // Collision uses the exact planned name in the same production primitive.
    fs::path collision;
    {
      detail::StagedAtomicStream staged(root / "collision-destination.tif");
      collision = staged.temporary_path();
      {
        std::ofstream f(collision);
        f << "unrelated collision winner";
      }
      refuses<fs::filesystem_error>([&] { staged.open({}); });
    }
    check(bytes(collision) == "unrelated collision winner" &&
              !fs::exists(root / "collision-destination.tif"),
          "TU07 collision ownership");
    fs::rename(collision, root / "collision-winner.bin");
    // Truncate after descriptor/codec admission, before the first sample read.
    const auto input = root / "short-read.tif";
    fs::copy_file(fixtures / "czt-exact.tif", input);
    ExecutionControl readControl;
    std::vector<std::size_t> progress;
    readControl.progress = [&](auto done, auto) {
      progress.push_back(done);
      if (!done)
        fs::resize_file(input, 0);
    };
    const auto error = refuses<std::runtime_error>(
        [&] { read_image_series(input.u8string(), {}, {}, readControl); });
    check(error.find("Short image file read") != std::string::npos &&
              progress == std::vector<std::size_t>{0},
          "TU07 actual short-read failure");
    DatasetReadObserver observer;
    Document owner(data.observe_reads(observer), "Fault source");
    const auto before = owner.snapshot();
    const auto target = root / "preserved-target.tif";
    {
      std::ofstream f(target);
      f << "prior target bytes";
    }
    const auto original = bytes(target);
    ImageWriteRequest replace;
    replace.replace = FileReplacePolicy::ReplaceExisting;
    ExecutionControl cancel;
    cancel.cancellation = std::make_shared<CancellationToken>();
    cancel.stage_progress = [&](auto, auto done, auto) {
      if (done == 2)
        cancel.cancellation->cancel();
    };
    refuses<OperationCancelled>([&] {
      write_image_series(target.u8string(), before.data().dataset, {}, replace,
                         {}, cancel);
    });
    check(observer.counts().plane_reads == 1 && bytes(target) == original,
          "TU07 between-plane cancel");
#ifdef _WIN32
    const auto priorReads = observer.counts().plane_reads;
    LockedFile lock;
    ExecutionControl disk;
    disk.checkpoint = [&] {
      if (lock.handle != INVALID_HANDLE_VALUE)
        return;
      for (const auto &entry : fs::directory_iterator(root))
        if (entry.path().filename().u8string().find(".tmp.") !=
            std::string::npos) {
          lock.lock(entry.path());
          break;
        }
    };
    const auto writeError = refuses<std::runtime_error>([&] {
      write_image_series(target.u8string(), before.data().dataset, {}, replace,
                         {}, disk);
    });
    check(lock.handle != INVALID_HANDLE_VALUE &&
              writeError == "Streaming file write failed",
          "TU07 real OS write failure exercised");
    lock.close();
    check(observer.counts().plane_reads == priorReads &&
              bytes(target) == original,
          "TU07 failed header write touched source/target");
#endif
    check(owner.snapshot().shares_data(before) &&
              owner.snapshot().revisions().matches(before.revisions(),
                                                   Change::All) &&
              owner.snapshot().dirty() == before.dirty() &&
              owner.undo_steps() == 0,
          "TU07 source document state changed");
    noTemporary(root);
    std::cout << "PASS TU01 endian literals; TU02 exact CZT/table; TU03 "
                 "strips; TU04 raw-output candidates; TU07 "
                 "collision/short-read/between-plane cancel";
#ifdef _WIN32
    std::cout << "; actual Windows write failure";
#endif
    std::cout << "\nARTIFACTS " << root.u8string() << '\n';
    return 0;
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
