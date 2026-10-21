#include <chrono>
#include <filesystem>
#include <fstream>
#include <imagejxx/image.hpp>
#include <iostream>
#include <limits>
#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#endif

using namespace imagejxx;
namespace fs = std::filesystem;
namespace {
void check(bool value, const char *message) {
  if (!value)
    throw std::runtime_error(message);
}
std::string bytes(const fs::path &path) {
  std::ifstream file(path, std::ios::binary);
  check(bool(file), "Cannot inspect PNM output");
  return {std::istreambuf_iterator<char>(file), {}};
}
void put(const fs::path &path, const std::string &data) {
  std::ofstream file(path, std::ios::binary);
  file << data;
  check(bool(file), "Cannot create PNM control");
}
template <class F> void refuses(F call) {
  bool rejected = false;
  try {
    call();
  } catch (const std::exception &) {
    rejected = true;
  }
  check(rejected, "Expected PNM failure");
}
void no_stage(const fs::path &root) {
  for (const auto &p : fs::directory_iterator(root))
    check(p.path().filename().u8string().find(".tmp.") == std::string::npos,
          "PNM failure leaked an owned stage");
}
} // namespace
int main(int argc, char **argv) {
  try {
    check(argc == 2, "Need PNM scratch root");
    const auto root =
        fs::absolute(fs::u8path(argv[1])) /
        std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count());
    fs::create_directories(root);
    const auto path = root / "preserved.pgm";
    Image late(513, 129, 1, PixelType::UInt16);
    late.pixels.back() = std::numeric_limits<double>::quiet_NaN();
    put(path, "original target");
    refuses([&] { write_pnm(late, path.u8string()); });
    check(bytes(path) == "original target",
          "PNM encode failure destroyed existing target");
    const auto absent = root / "absent.pgm";
    refuses([&] { write_pnm(late, absent.u8string()); });
    check(!fs::exists(absent),
          "PNM encode failure published a partial new file");
    no_stage(root);
    for (auto nonfinite : {std::numeric_limits<double>::infinity(),
                           -std::numeric_limits<double>::infinity()}) {
      late.pixels.back() = nonfinite;
      refuses([&] { write_pnm(late, path.u8string()); });
      check(bytes(path) == "original target",
            "PNM infinity failure changed target");
    }
    for (int invalid : {0, 1, 2, 3}) {
      Image bad(2, 2, 1, PixelType::UInt8);
      if (invalid == 0)
        bad.width = 0;
      if (invalid == 1)
        bad.height = -1;
      if (invalid == 2)
        bad.pixels.pop_back();
      if (invalid == 3)
        bad.pixels.push_back(1);
      refuses([&] { write_pnm(bad, path.u8string()); });
      check(bytes(path) == "original target",
            "Malformed PNM image changed target");
    }
    int variants = 0;
    for (auto type : {PixelType::UInt8, PixelType::UInt16})
      for (int c : {1, 3}) {
        const auto maximum = type == PixelType::UInt8 ? 255 : 65535;
        Image image(257, 129, c, type);
        for (std::size_t i = 0; i < image.pixels.size(); ++i)
          image.pixels[i] = double((i * 257) % std::size_t(maximum + 1));
        image.pixels[0] = 10;
        image.pixels[1] = 13;
        image.pixels[2] = 32;
        image.pixels[3] = 35;
        image.pixels[4] = -.5;
        image.pixels[5] = .5;
        image.pixels[6] = maximum + 1.;
        auto output =
            root / (std::to_string(maximum) + "-" + std::to_string(c) + ".pnm");
        auto sentinel = output;
        sentinel += ".imagejxx.tmp";
        put(sentinel, "unrelated fixed suffix");
        write_pnm(image, output.u8string());
        std::string expected = (c == 1 ? "P5\n" : "P6\n") +
                               std::string("257 129\n") +
                               std::to_string(maximum) + "\n";
        for (double v : image.pixels) {
          auto value = static_cast<unsigned>(v < 0         ? 0
                                             : v > maximum ? maximum
                                                           : v + .5);
          if (maximum > 255)
            expected.push_back(static_cast<char>(value >> 8));
          expected.push_back(static_cast<char>(value & 255));
        }
        check(bytes(output) == expected,
              "PNM bytes/rounding/clamping/endian/order mismatch");
        check(bytes(sentinel) == "unrelated fixed suffix",
              "PNM writer changed unrelated suffix");
        ++variants;
      }
    const auto occupied = root / "occupied.pgm";
    fs::create_directory(occupied);
    put(occupied / "keep", "keep");
    Image valid(2, 2, 1, PixelType::UInt8);
    refuses([&] { write_pnm(valid, occupied.u8string()); });
    check(bytes(occupied / "keep") == "keep",
          "PNM publication changed occupied directory");
    refuses(
        [&] { write_pnm(valid, (root / "missing" / "out.pgm").u8string()); });
    check(!fs::exists(root / "missing"),
          "PNM save created an unrequested parent");
    auto nul = path.u8string() + std::string("\0tail", 5);
    refuses([&] { write_pnm(valid, nul); });
    check(bytes(path) == "original target",
          "PNM NUL path changed prefix target");
#ifdef _WIN32
    HANDLE held =
        CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                    OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    check(held != INVALID_HANDLE_VALUE, "Cannot hold PNM destination");
    bool rejected = false;
    try {
      write_pnm(valid, path.u8string());
    } catch (const std::exception &) {
      rejected = true;
    }
    CloseHandle(held);
    check(rejected && bytes(path) == "original target",
          "Locked PNM destination changed");
#endif
    no_stage(root);
    std::cout << "PASS PNM literal variants " << variants
              << "; late encode failure; malformed images; occupied/locked "
                 "destinations; no stage leaks\nARTIFACTS "
              << root.u8string() << '\n';
    return 0;
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
