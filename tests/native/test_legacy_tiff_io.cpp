#include "window.hpp"
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
using namespace imagejxx;
using namespace imagejxx::desktop;
namespace fs = std::filesystem;
namespace {
void check(bool v, const char *m) {
  if (!v)
    throw std::runtime_error(m);
}
std::string bytes(const fs::path &p) {
  std::ifstream f(p, std::ios::binary);
  check(bool(f), "Open observed file");
  return {std::istreambuf_iterator<char>(f), {}};
}
template <class F> void refuses(F f) {
  bool failed = false;
  try {
    f();
  } catch (const std::exception &) {
    failed = true;
  }
  check(failed, "Expected legacy save refusal");
}
Stack frames(PixelType type, int channels, int count) {
  Stack out;
  for (int z = 0; z < count; ++z) {
    Image image(5, 3, channels, type);
    for (int y = 0; y < 3; ++y)
      for (int x = 0; x < 5; ++x)
        for (int s = 0; s < channels; ++s)
          image.at(x, y, s) =
              (type == PixelType::Float32 || type == PixelType::Float64)
                  ? -2.5 + z * 64 + y * 8 + x + s * .125
                  : z * 64 + y * 8 + x + s;
    out.push_back(std::move(image));
  }
  return out;
}
} // namespace
int main(int argc, char **argv) {
  try {
    check(argc == 3, "Need legacy output and scientific fixture paths");
    auto root =
        fs::absolute(fs::u8path(argv[1])) /
        std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count());
    fs::create_directories(root);
    auto target = root / "collision.tif";
    auto unrelated = target;
    unrelated += ".imagejxx.tmp";
    {
      std::ofstream f(unrelated);
      f << "unrelated owned file";
    }
    const auto source = frames(PixelType::UInt16, 1, 2);
    writeImages(target.u8string(), source);
    check(fs::exists(unrelated) && bytes(unrelated) == "unrelated owned file",
          "Legacy TIFF save destroyed unrelated fixed-suffix file");
    const auto original = bytes(target);
    auto bad = source;
    bad[1].pixels.pop_back();
    refuses([&] { writeImages(target.u8string(), bad); });
    check(bytes(target) == original &&
              bytes(unrelated) == "unrelated owned file",
          "Failed encoding changed target or unrelated file");
    auto occupied = root / "occupied.tif";
    fs::create_directory(occupied);
    {
      std::ofstream f(occupied / "keep");
      f << "keep";
    }
    refuses([&] { writeImages(occupied.u8string(), source); });
    check(bytes(occupied / "keep") == "keep",
          "Failed atomic publication changed directory");
    int cases = 0;
    for (auto type : {PixelType::UInt8, PixelType::UInt16, PixelType::Float32,
                      PixelType::Float64})
      for (int channels : {1, 3, 4})
        for (int count : {1, 2}) {
          auto data = frames(type, channels, count);
          const auto before = data;
          auto name = typeName(type) + "-s" + std::to_string(channels) + "-z" +
                      std::to_string(count) + ".tif";
          writeImages((root / name).u8string(), data);
          auto read = readImages((root / name).u8string());
          check(read.size() == data.size(), "Legacy TIFF plane count");
          for (std::size_t i = 0; i < data.size(); ++i)
            check(read[i].type == type && read[i].channels == channels &&
                      read[i].width == 5 && read[i].height == 3 &&
                      read[i].pixels == data[i].pixels &&
                      data[i].pixels == before[i].pixels,
                  "Legacy TIFF literal dtype/samples/source");
          ++cases;
        }
    Window legacy((root / "prefs.json").u8string());
  legacy.openLegacyFile(argv[2]);
    auto snapshot = legacy.nativeSnapshot();
    check(snapshot.data().dataset.shape().dimensions() ==
              std::array<int, 6>{1, 1, 12, 3, 5, 1},
          "Frozen ordinary-open flat-stack mapping");
    check(snapshot.data().dataset.calibration().spacing ==
                  std::array<double, 3>{1, 1, 1} &&
              snapshot.data().dataset.calibration().unit == "px",
          "Frozen legacy default calibration");
    for (int p = 0; p < 12; ++p) {
      auto image = snapshot.data().dataset.plane({0, 0, p});
      int t = p / 6, z = (p / 3) % 2, c = p % 3;
      for (int y = 0; y < 3; ++y)
        for (int x = 0; x < 5; ++x)
          check(image->at(x, y) == 10000 * t + 1000 * c + 100 * z + 5 * y + x,
                "Frozen legacy file-page ordering");
    }
    for (const auto &entry : fs::directory_iterator(root))
      if (entry.path() != unrelated)
        check(entry.path().filename().u8string().find(".tmp") ==
                  std::string::npos,
              "Legacy save leaked temporary");
    std::cout << "PASS legacy TIFF variants" << cases
              << "; fixed-suffix ownership; encode/publication failures; "
                 "ordinary-open compatibility\nARTIFACTS "
              << root.u8string() << '\n';
    return 0;
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
