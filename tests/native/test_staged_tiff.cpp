#include "atomic_file.hpp"
#include "staged.hpp"
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif
using namespace imagejxx;
namespace {
void check(bool v, const char *m) {
  if (!v)
    throw std::runtime_error(m);
}
template <class E = std::exception, class F> void refuses(F f) {
  bool failed = false;
  try {
    f();
  } catch (const E &) {
    failed = true;
  }
  check(failed, "Expected staged TIFF refusal");
}
std::string bytes(const std::filesystem::path &p) {
  std::ifstream f(p, std::ios::binary);
  return {std::istreambuf_iterator<char>(f), {}};
}
} // namespace
int main(int argc, char **argv) {
  try {
    check(argc == 2, "Need staged test root");
    namespace fs = std::filesystem;
    auto root =
        fs::absolute(fs::u8path(argv[1])) /
        std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count());
    fs::create_directories(root);
    Image image(3, 2, 1, PixelType::UInt16);
    image.pixels = {0, 1, 255, 256, 32768, 65535};
    auto data = Dataset::from_planes({1, 1, 1, 2, 3, 1}, Stack{image});
    auto path = root / "output.tif";
    std::vector<std::size_t> progress;
    ExecutionControl control;
    control.progress = [&](auto n, auto total) {
      check(total == 1, "Coarse total");
      progress.push_back(n);
    };
    auto staged = io_detail::stage_image_series(path.u8string(), data, {}, {},
                                                {}, control);
    check(staged->active() && !fs::exists(path) &&
              progress == std::vector<std::size_t>{0},
          "Preparation published early");
    staged->publish(control);
    check(!staged->active() && progress == std::vector<std::size_t>{0, 1},
          "Staged completion");
    check(read_image_series(path.u8string()).dataset.plane({0, 0, 0})->pixels ==
              image.pixels,
          "Staged literal UInt16 pixels");
    refuses<std::logic_error>([&] { staged->publish(); });
    auto original = bytes(path);
    ImageWriteRequest replace;
    replace.replace = FileReplacePolicy::ReplaceExisting;
    {
      auto abandoned =
          io_detail::stage_image_series(path.u8string(), data, {}, replace);
      check(bytes(path) == original, "Staging changed destination");
    }
    {
      auto canceled =
          io_detail::stage_image_series(path.u8string(), data, {}, replace);
      ExecutionControl c;
      c.cancellation = std::make_shared<CancellationToken>();
      c.cancellation->cancel();
      refuses<OperationCancelled>([&] { canceled->publish(c); });
      check(!canceled->active(), "Canceled staging not consumed");
    }
    {
      auto throwing =
          io_detail::stage_image_series(path.u8string(), data, {}, replace);
      struct Marker {};
      ExecutionControl c;
      c.progress = [](auto, auto) { throw Marker{}; };
      refuses<Marker>([&] { throwing->publish(c); });
      check(!throwing->active(), "Throwing staging not consumed");
    }
    check(bytes(path) == original, "Abandon/cancel/callback changed output");
    auto race = root / "race.tif";
    {
      auto waiting = io_detail::stage_image_series(race.u8string(), data);
      {
        std::ofstream f(race);
        f << "winner";
      }
      refuses<std::runtime_error>([&] { waiting->publish(); });
      check(bytes(race) == "winner", "Staged race replaced winner");
    }
    {
      auto changed =
          io_detail::stage_image_series(path.u8string(), data, {}, replace);
      for (const auto &entry : fs::directory_iterator(root))
        if (entry.path().extension() != ".tif") {
#ifdef _WIN32
          // A cooperating mutator shares deletion with the retained owner.
          // CRT streams omit FILE_SHARE_DELETE and are refused by Windows.
          const auto h = CreateFileW(
              entry.path().c_str(), FILE_APPEND_DATA,
              FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
              OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
          check(h != INVALID_HANDLE_VALUE, "Could not open staged mutation");
          DWORD written = 0;
          const auto success = WriteFile(h, "x", 1, &written, nullptr);
          CloseHandle(h);
          check(success && written == 1, "Could not mutate staged temporary");
#else
          std::ofstream f(entry.path(), std::ios::binary | std::ios::app);
          check(bool(f), "Could not open staged temporary for mutation");
          f << 'x';
          f.close();
          check(bool(f), "Could not mutate staged temporary");
#endif
        }
      refuses<std::runtime_error>([&] { changed->publish(); });
    }
    check(bytes(path) == original, "Changed temporary replaced output");
#ifdef _WIN32
    // Rename the owned file away, then occupy its former pathname. Cleanup must
    // target the original file object, never the unrelated replacement.
    for (int mode = 0; mode != 3; ++mode) {
      const auto moved = root / "owned-moved.bin";
      fs::path temporary;
      auto waiting =
          io_detail::stage_image_series(path.u8string(), data, {}, replace);
      for (const auto &entry : fs::directory_iterator(root))
        if (entry.path().filename().u8string().find(".tmp.") !=
            std::string::npos)
          temporary = entry.path();
      check(!temporary.empty(), "Missing owned sibling");
      auto substitute = [&] {
        fs::rename(temporary, moved);
        std::ofstream f(temporary, std::ios::binary);
        f << "unrelated replacement";
      };
      ExecutionControl c;
      if (mode == 2) {
        c.progress = [&](auto, auto) { substitute(); };
        refuses<std::runtime_error>([&] { waiting->publish(c); });
      } else {
        substitute();
        if (mode == 1) {
          c.cancellation = std::make_shared<CancellationToken>();
          c.cancellation->cancel();
          refuses<std::runtime_error>([&] { waiting->publish(c); });
        }
      }
      waiting.reset();
      check(bytes(temporary) == "unrelated replacement",
            "Cleanup deleted substituted temporary path");
      check(!fs::exists(moved), "Cleanup leaked renamed owned file");
      check(bytes(path) == original, "Substitution changed destination");
      fs::remove(temporary); // Test owns this explicit replacement.
    }
    {
      detail::StagedAtomicStream stream(root / "handle-published.tif");
      stream.open({});
      const unsigned char literal[] = {0, 1, 255, 42};
      stream.append(literal, sizeof(literal), {});
      stream.close({});
      const auto moved = root / "stream-owned-moved.bin";
      fs::rename(stream.temporary_path(), moved);
      {
        std::ofstream f(stream.temporary_path());
        f << "publication decoy";
      }
      stream.publish(false);
      check(bytes(root / "handle-published.tif") ==
                std::string(reinterpret_cast<const char *>(literal),
                            sizeof(literal)),
            "Publication used substituted path instead of owned file");
      check(bytes(stream.temporary_path()) == "publication decoy",
            "Publication consumed unrelated replacement");
      check(!fs::exists(moved), "Publication left owned file behind");
      fs::remove(stream.temporary_path());
    }
#endif
    for (const auto &entry : fs::directory_iterator(root))
      check(entry.path().extension() == ".tif", "Staged temporary leaked");
    std::cout << "PASS caller-thread TIFF "
                 "staging/publication/cancellation/race/source guards\n";
    return 0;
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
