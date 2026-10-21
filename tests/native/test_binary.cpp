#include "../../src/native/kernel_control.hpp"
#include <imagejxx/binary.hpp>
#include <imagejxx/operations.hpp>
#include <imagejxx/settings.hpp>
#include <iostream>
#include <stdexcept>

using namespace imagejxx;
namespace {
void check(bool condition, const char *message) {
  if (!condition)
    throw std::runtime_error(message);
}
template <class F> void rejects(F work) {
  bool rejected = false;
  try {
    work();
  } catch (const std::invalid_argument &) {
    rejected = true;
  }
  check(rejected, "Invalid binary input was accepted");
}
void testPixelOptions() {
  const Image filled = fill(Image(5, 5, 1, PixelType::UInt8), 255);
  const auto eroded = binary_morphology(filled, "erode");
  check(eroded.at(0, 0) == 0 && eroded.at(1, 1) == 255,
        "Default erosion must count the background exterior");
  auto padded = binary_morphology(filled, "erode", {100, 1, true, true});
  check(padded.pixels == filled.pixels,
        "Padding must preserve an all-foreground image");
  check(binary_morphology(filled, "erode", {2, 1}).at(1, 1) == 0 &&
            binary_morphology(filled, "erode", {2, 1}).at(2, 2) == 255,
        "Iterations must use separate source and destination buffers");
  auto center = Image(3, 3, 1, PixelType::UInt8);
  center.at(1, 1) = 255;
  check(
      binary_morphology(center, "dilate").pixels ==
              std::vector<double>(9, 255) &&
          binary_morphology(center, "dilate", {1, 2}).pixels == center.pixels,
      "Neighbor count must exclude the center and count all eight directions");
  for (int count = 1; count <= 8; ++count) {
    auto pattern = fill(Image(5, 5, 1, PixelType::UInt8), 255);
    for (int k = 0; k < count; ++k) {
      const int dx[] = {-1, 0, 1, -1, 1, -1, 0, 1};
      const int dy[] = {-1, -1, -1, 0, 0, 1, 1, 1};
      pattern.at(2 + dx[k], 2 + dy[k]) = 0;
    }
    check(binary_morphology(pattern, "erode", {1, count}).at(2, 2) == 0,
          "Erosion neighbor threshold is inclusive");
    if (count < 8)
      check(binary_morphology(pattern, "erode", {1, count + 1}).at(2, 2) == 255,
            "Erosion changed a pixel below the neighbor threshold");
    for (auto &p : pattern.pixels)
      p = 255 - p;
    check(binary_morphology(pattern, "erode", {1, count, false}).at(2, 2) ==
              255,
          "White background must select zero-valued foreground");
  }
  const auto opened = binary_morphology(filled, "open", {2, 1});
  const auto staged = binary_morphology(
      binary_morphology(filled, "erode", {2, 1}), "dilate", {2, 1});
  check(opened.pixels == staged.pixels,
        "Opening must complete N erosions before N dilations");
  for (bool pad : {false, true}) {
    Image single(1, 1, 1, PixelType::UInt8);
    single.at(0, 0) = 255;
    check(binary_morphology(single, "erode", {1, 1, true, pad}).at(0, 0) ==
              (pad ? 255 : 0),
          "Single-pixel edge behavior is incorrect");
  }
  check(filled.pixels == std::vector<double>(25, 255),
        "Binary operations changed their input");
}
void testValidationAndSettings() {
  Image image(2, 2, 1, PixelType::UInt8);
  for (int iterations : {0, 101})
    rejects([&] { binary_morphology(image, "erode", {iterations, 1}); });
  for (int neighbors : {0, 9})
    rejects([&] { binary_morphology(image, "erode", {1, neighbors}); });
  rejects([&] { binary_morphology(image, "unknown"); });
  rejects([&] { binary_morphology(Image(2, 2), "erode"); });
  rejects(
      [&] { binary_morphology(Image(2, 2, 3, PixelType::UInt8), "erode"); });
  image.at(0, 0) = 1;
  rejects([&] { binary_morphology(image, "erode"); });
  image.pixels.pop_back();
  rejects([&] { binary_morphology(image, "erode"); });
  Settings settings;
  settings.update({{"binary_iterations", std::int64_t(3)},
                   {"binary_neighbors", std::int64_t(5)},
                   {"binary_black_background", false},
                   {"binary_pad_edges", true}});
  auto options = settings.binary_options();
  check(options.iterations == 3 && options.neighbors == 5 &&
            !options.black_background && options.pad_edges,
        "Native settings did not produce the effective binary options");
  rejects([&] { settings.set("binary_neighbors", std::int64_t(9)); });
  check(settings.binary_options().neighbors == 5,
        "Rejected preferences changed their consumer");
  for (const auto *id :
       {"binary_erode", "binary_dilate", "binary_open", "binary_close"}) {
    check(operation_spec(id).planeOnly &&
              !operation_spec(id).cancellationInsideKernel,
          "Binary catalog must retain conservative certification flags");
    rejects([&] {
      validate_operation_record(id, {{"neighbors", std::int64_t(9)}});
    });
    rejects([&] {
      validate_operation_record(id, {{"iterations", std::int64_t(0)}});
    });
  }
}
void testCancellation() {
  const auto image = fill(Image(128, 128, 1, PixelType::UInt8), 255);
  for (const auto *phase :
       {"Binary input validation", "Binary erosion", "Binary dilation"}) {
    ExecutionControl control;
    control.cancellation = std::make_shared<CancellationToken>();
    std::size_t done = 0;
    control.stage_progress = [&](std::string_view name, std::size_t completed,
                                 std::size_t total) {
      check(completed <= total, "Invalid binary stage progress");
      if (name == phase && completed == 1024) {
        done = completed;
        control.cancellation->cancel();
      }
    };
    bool canceled = false;
    try {
      ExecutionScope scope(control);
      (void)binary_morphology(image, "close", {3, 1});
    } catch (const OperationCancelled &) {
      canceled = true;
    }
    check(canceled && done == 1024,
          "Binary work did not cancel within its pixel phase");
    check(image.pixels == std::vector<double>(128 * 128, 255),
          "Canceled binary work changed its input");
  }
}
} // namespace
int main() {
  try {
    testPixelOptions();
    testValidationAndSettings();
    testCancellation();
    std::cout << "Binary morphology options, settings and phase cancellation "
                 "passed\n";
    return 0;
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
