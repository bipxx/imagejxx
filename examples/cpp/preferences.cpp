#include <imagejxx/preferences.hpp>
#include <array>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <stdexcept>

namespace {
namespace fs = std::filesystem;
struct OwnedDirectory {
  fs::path temporaryRoot;
  fs::path path;
  bool owned = false;
  OwnedDirectory() {
    temporaryRoot = fs::canonical(fs::temp_directory_path());
    if (!temporaryRoot.is_absolute())
      throw std::runtime_error("SDK temporary root must resolve to an absolute path");
    for (unsigned int attempt = 0; attempt < 128; ++attempt) {
      path = (temporaryRoot /
             ("imagejxx-preferences-sdk-" +
              std::to_string(std::chrono::steady_clock::now()
                                 .time_since_epoch()
                                 .count()) +
              "-" + std::to_string(attempt))).lexically_normal();
      if (!path.is_absolute() || path.parent_path() != temporaryRoot)
        throw std::runtime_error("SDK directory must stay inside its temporary root");
      if (fs::create_directory(path)) {
        owned = true; // Only successful exclusive creation grants cleanup ownership.
        return;
      }
    }
    throw std::runtime_error("Cannot create an exclusive SDK test directory");
  }
  ~OwnedDirectory() {
    if (!owned)
      return;
    std::error_code error;
    const auto status = fs::symlink_status(path, error);
    if (error || !fs::is_directory(status) || fs::is_symlink(status))
      return;
    const auto resolved = fs::canonical(path, error);
    if (error || !resolved.is_absolute() || resolved != path ||
        resolved.parent_path() != temporaryRoot ||
        resolved.filename().u8string().rfind("imagejxx-preferences-sdk-", 0) != 0)
      return;
    fs::remove_all(resolved, error);
  }
};
// Exact pinned ByteProcessor results: binary-imagej154r.json cases 7/103/199,
// a 9x7 foreground plane, neighbors=1, black background, unpadded erosion.
const std::array<std::array<unsigned char, 63>, 3> expected{{
    {{0, 0, 0, 0, 0, 0, 0, 0, 0,
      0, 255, 255, 255, 255, 255, 255, 255, 0,
      0, 255, 255, 255, 255, 255, 255, 255, 0,
      0, 255, 255, 255, 255, 255, 255, 255, 0,
      0, 255, 255, 255, 255, 255, 255, 255, 0,
      0, 255, 255, 255, 255, 255, 255, 255, 0,
      0, 0, 0, 0, 0, 0, 0, 0, 0}},
    {{0, 0, 0, 0, 0, 0, 0, 0, 0,
      0, 0, 0, 0, 0, 0, 0, 0, 0,
      0, 0, 255, 255, 255, 255, 255, 0, 0,
      0, 0, 255, 255, 255, 255, 255, 0, 0,
      0, 0, 255, 255, 255, 255, 255, 0, 0,
      0, 0, 0, 0, 0, 0, 0, 0, 0,
      0, 0, 0, 0, 0, 0, 0, 0, 0}},
    {{0, 0, 0, 0, 0, 0, 0, 0, 0,
      0, 0, 0, 0, 0, 0, 0, 0, 0,
      0, 0, 0, 0, 0, 0, 0, 0, 0,
      0, 0, 0, 255, 255, 255, 0, 0, 0,
      0, 0, 0, 0, 0, 0, 0, 0, 0,
      0, 0, 0, 0, 0, 0, 0, 0, 0,
      0, 0, 0, 0, 0, 0, 0, 0, 0}},
}};
void checkResult(const imagejxx::Image &image, int iterations) {
  if (image.pixels.size() != expected.front().size())
    throw std::runtime_error("Saved preference changed output dimensions");
  for (std::size_t index = 0; index < image.pixels.size(); ++index)
    if (image.pixels[index] != expected.at(iterations - 1)[index])
      throw std::runtime_error("Saved/reopened preference changed exact pixels");
}
} // namespace

int main() {
  try {
    using namespace imagejxx;
    OwnedDirectory work;
    const auto path = (work.path / "preferences.json").u8string();
    const auto input = fill(Image(9, 7, 1, PixelType::UInt8), 255);
    PreferencesDocument current;
    checkResult(binary_morphology(input, "erode", current.settings.binary_options()), 1);
    current.settings.set("binary_iterations", std::int64_t(2));
    if (!current.save(path))
      return 1;
    const auto reopened = PreferencesDocument::load(path, false);
    const auto frozen = reopened.settings.binary_options();
    if (frozen.iterations != 2)
      return 1;
    checkResult(binary_morphology(input, "erode", frozen), 2);
    current.settings.set("binary_iterations", std::int64_t(3));
    checkResult(binary_morphology(input, "erode", current.settings.binary_options()), 3);
    checkResult(binary_morphology(input, "erode", frozen), 2);
    if (!current.save(path) ||
        PreferencesDocument::load(path, false).settings.integer("binary_iterations") != 3)
      return 1;
    std::cout << "Installed headless preferences save/reopen selects exact pinned "
                 "35/15/3-pixel erosion results and preserves detached options\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
