#include "../../src/native/kernel_control.hpp"
#include <cstring>
#include <imagejxx/image.hpp>
#include <iostream>
#include <tuple>

using namespace imagejxx;
namespace {
void check(bool condition, const char *message) {
  if (!condition)
    throw std::runtime_error(message);
}
void testRankSetupCancellation() {
  Image input(2, 2, 1, PixelType::UInt16);
  ExecutionControl control;
  control.cancellation = std::make_shared<CancellationToken>();
  bool setup_entered = false, canceled = false;
  std::size_t last_completed = 0, total_work = 0;
  control.stage_progress = [&](std::string_view name, std::size_t completed,
                               std::size_t total) {
    if (name != "Rank footprint")
      return;
    setup_entered = true;
    last_completed = completed;
    total_work = total;
    if (completed >= 2048)
      control.cancellation->cancel();
  };
  try {
    ExecutionScope scope(control);
    (void)rank_filter(input, 1024, "median", true);
  } catch (const OperationCancelled &) {
    canceled = true;
  }
  check(setup_entered && canceled && last_completed == 2048 &&
            last_completed < total_work,
        "Rank cancellation did not stop inside footprint construction");
  check(input.at(0, 0) == 0, "Canceled setup changed its source");
}
Image fixture(int samples = 1, PixelType type = PixelType::UInt16) {
  Image image(128, 128, samples, type);
  for (std::size_t i = 0; i < image.pixels.size(); ++i)
    image.pixels[i] = double((i * 7919 + 17) % 65536);
  return image;
}
void canceledInStage(std::string_view name, const std::function<void()> &work) {
  ExecutionControl control;
  control.cancellation = std::make_shared<CancellationToken>();
  std::size_t completed_work = 0, total_work = 0;
  control.stage_progress = [&](std::string_view stage, std::size_t completed,
                               std::size_t total) {
    check(completed <= total, "Stage progress exceeds its declared work");
    if (stage != name)
      return;
    completed_work = completed;
    total_work = total;
    if (completed >= 1024)
      control.cancellation->cancel();
  };
  bool canceled = false;
  try {
    ExecutionScope scope(control);
    work();
  } catch (const OperationCancelled &) {
    canceled = true;
  }
  if (!canceled || completed_work != 1024 || completed_work >= total_work)
    throw std::runtime_error(std::string(name) +
                             " did not cancel after entering its work loop");
}
void testDirectAndSetupCancellation() {
  const auto image = fixture();
  const auto source = image.pixels;
  const std::vector<double> lut(65536, 17);
  canceledInStage("Rolling-ball footprint", [&] {
    (void)rolling_ball_background(Image(2, 2, 1, PixelType::UInt16), 1024);
  });
  canceledInStage("Gaussian kernel", [&] {
    (void)gaussian_blur(Image(2, 2, 1, PixelType::UInt16), 1024, 1024);
  });
  canceledInStage("CLAHE maps", [&] { (void)clahe(image, 2, 2); });
  canceledInStage("CLAHE pixels", [&] { (void)clahe(image, 16, 16); });
  canceledInStage("Point pixels", [&] { (void)point(image, "add", 3); });
  canceledInStage("Calculator pixels",
                  [&] { (void)calculate(image, image, "add"); });
  canceledInStage("Conversion pixels",
                  [&] { (void)convert(image, PixelType::UInt8, true); });
  canceledInStage("Linear pixels", [&] { (void)linear(image, 1.1, 3); });
  canceledInStage("Normalization pixels",
                  [&] { (void)normalize(image, 0, 255, 3); });
  canceledInStage("LUT validation", [&] { (void)apply_lut(image, lut); });
  canceledInStage("LUT pixels", [&] { (void)apply_lut(image, lut); });
  canceledInStage("Threshold pixels",
                  [&] { (void)threshold(image, 100, 30000); });
  canceledInStage("Range pixels",
                  [&] { (void)in_range(image, {100}, {30000}); });
  canceledInStage("Fill pixels", [&] { (void)fill(image, 17); });
  canceledInStage("Noise pixels",
                  [&] { (void)add_noise(image, "gaussian", 3, 42); });
  canceledInStage("Image validation",
                  [&] { (void)execute_operation("add", image); });
  canceledInStage("Canonical pixels",
                  [&] { (void)execute_operation("add", image); });
  NumericArray values{{128, 128}, image.pixels};
  canceledInStage("Numeric arguments", [&] { values.validate(); });
  check(source == image.pixels && values.values == source,
        "Canceled work changed an input buffer");
}
void testLibraryWorkCancellation() {
  const auto input = fixture();
  const std::vector<std::function<void()>> work{
      [&] {
        auto values = input.pixels;
        kernel_sort(values.begin(), values.end());
      },
      [&] {
        auto values = input.pixels;
        kernel_nth_element(values.begin(), values.begin() + values.size() / 2,
                           values.end());
      },
      [&] {
        (void)kernel_minmax_element(input.pixels.begin(), input.pixels.end());
      },
      [&] {
        (void)kernel_min_element(input.pixels.begin(), input.pixels.end());
      },
      [&] {
        (void)kernel_max_element(input.pixels.begin(), input.pixels.end());
      },
      [&] {
        (void)kernel_accumulate(input.pixels.begin(), input.pixels.end(), 0.);
      },
      [&] {
        (void)kernel_accumulate(
            input.pixels.begin(), input.pixels.end(), 0.,
            [](double sum, double v) { return sum + v * v; });
      }};
  for (const auto &operation : work) {
    ExecutionControl control;
    control.cancellation = std::make_shared<CancellationToken>();
    unsigned checkpoints = 0;
    control.checkpoint = [&] {
      if (++checkpoints == 5)
        control.cancellation->cancel();
    };
    bool canceled = false;
    try {
      ExecutionScope scope(control);
      operation();
    } catch (const OperationCancelled &) {
      canceled = true;
    }
    check(
        canceled && checkpoints == 5,
        "Library work did not poll from inside its comparison/reduction loop");
  }
}
void sameImage(const Image &a, const Image &b) {
  check(a.width == b.width && a.height == b.height &&
            a.channels == b.channels && a.type == b.type &&
            a.pixels.size() == b.pixels.size() &&
            std::memcmp(a.pixels.data(), b.pixels.data(),
                        a.pixels.size() * sizeof(double)) == 0,
        "Controlled execution changed raw numerical results or type");
}
void testSegmentationCancellation() {
  const auto image = fixture();
  Image foreground(128, 128, 1, PixelType::UInt8);
  std::fill(foreground.pixels.begin(), foreground.pixels.end(), 255);
  Image background(128, 128, 1, PixelType::UInt8);
  Image wide(4096, 2, 1, PixelType::UInt8);
  Image tall(2, 4096, 1, PixelType::UInt8);
  Image ridge(4096, 1, 1, PixelType::UInt8);
  std::fill(ridge.pixels.begin(), ridge.pixels.end(), 255);
  Image broad(2048, 8, 1, PixelType::UInt8);
  std::fill(broad.pixels.begin(), broad.pixels.end(), 255);
  Image islands(256, 256, 1, PixelType::UInt8);
  for (int y = 0; y < islands.height; ++y)
    for (int x = 0; x < islands.width; ++x)
      islands.at(x, y) = (x + y) % 2 ? 255 : 0;
  Labels labels{128, 128, 1, std::vector<std::int32_t>(16384, 1)};
  Labels markers{128, 128, 1, std::vector<std::int32_t>(16384, 0)};
  markers.pixels[0] = 1;
  Labels many = labels;
  many.count = static_cast<int>(many.pixels.size());
  for (std::size_t i = 0; i < many.pixels.size(); ++i)
    many.pixels[i] = static_cast<std::int32_t>(i + 1);
  const auto original = foreground.pixels;
  const auto original_markers = markers.pixels;
  canceledInStage("Wand validation",
                  [&] { (void)wand_mask(image, 0, 0, 65535); });
  canceledInStage("Wand traversal", [&] { (void)wand_mask(foreground, 0, 0); });
  canceledInStage("Label validation", [&] { (void)region_properties(labels); });
  canceledInStage("Component scan",
                  [&] { (void)connected_components(background); });
  canceledInStage("Component traversal",
                  [&] { (void)connected_components(foreground); });
  canceledInStage("Region accumulation",
                  [&] { (void)region_properties(labels); });
  canceledInStage("Region measurements",
                  [&] { (void)region_properties(many); });
  canceledInStage("Hole border seeds", [&] { (void)fill_holes(wide); });
  canceledInStage("Hole traversal", [&] { (void)fill_holes(background); });
  canceledInStage("Hole output", [&] { (void)fill_holes(foreground); });
  canceledInStage("Skeleton input", [&] { (void)skeletonize(foreground); });
  canceledInStage("Skeleton thinning", [&] { (void)skeletonize(foreground); });
  canceledInStage("Skeleton output", [&] { (void)skeletonize(background); });
  canceledInStage("Skeleton removal", [&] { (void)skeletonize(broad); });
  canceledInStage("Outline input", [&] { (void)binary_outline(foreground); });
  canceledInStage("Object sizes",
                  [&] { (void)remove_small_objects(foreground, 2); });
  canceledInStage("Object filtering",
                  [&] { (void)remove_small_objects(foreground, 2); });
  canceledInStage("Border object filtering",
                  [&] { (void)clear_border(foreground); });
  canceledInStage("Border object selection",
                  [&] { (void)clear_border(islands, 4); });
  canceledInStage("Distance rows", [&] { (void)distance_transform(tall); });
  canceledInStage("Distance columns", [&] { (void)distance_transform(wide); });
  canceledInStage("Distance sites", [&] { (void)distance_transform(wide); });
  canceledInStage("Distance evaluation",
                  [&] { (void)distance_transform(wide); });
  canceledInStage("Distance empty output",
                  [&] { (void)distance_transform(broad); });
  canceledInStage("Distance row input",
                  [&] { (void)distance_transform(wide); });
  canceledInStage("Distance row output",
                  [&] { (void)distance_transform(wide); });
  canceledInStage("Distance column input",
                  [&] { (void)distance_transform(tall); });
  canceledInStage("Distance column output",
                  [&] { (void)distance_transform(tall); });
  canceledInStage("Maxima output", [&] { (void)local_maxima(image, 1); });
  canceledInStage("Watershed seeds", [&] { (void)watershed(image, markers); });
  canceledInStage("Watershed traversal",
                  [&] { (void)watershed(foreground, markers); });
  canceledInStage("Binary mask scan",
                  [&] { (void)binary_watershed(background); });
  canceledInStage("Watershed plateau scan",
                  [&] { (void)binary_watershed(foreground); });
  canceledInStage("Watershed plateau traversal",
                  [&] { (void)binary_watershed(ridge); });
  canceledInStage("Watershed fallback scan",
                  [&] { (void)binary_watershed(foreground); });
  canceledInStage("Watershed plateau removal",
                  [&] { (void)binary_watershed(broad); });
  canceledInStage("Watershed fallback seeds",
                  [&] { (void)binary_watershed(islands, 1, 4); });
  check(foreground.pixels == original && markers.pixels == original_markers,
        "Canceled segmentation modified its source or markers");
}
void testSegmentationControlledNumerics() {
  for (const auto &input : {fixture(), fixture(1, PixelType::Float64)}) {
    const auto mask = threshold(input, 25000, 60000);
    const std::vector<std::function<Image()>> work{
        [&] { return wand_mask(input, 3, 7, 5000, 4); },
        [&] { return wand_mask(input, 3, 7, 65535, 8); },
        [&] { return fill_holes(mask, 4); },
        [&] { return fill_holes(mask, 8); },
        [&] { return skeletonize(mask); },
        [&] { return binary_outline(mask); },
        [&] { return remove_small_objects(mask, 5, 4); },
        [&] { return clear_border(mask, 8); },
        [&] { return distance_transform(mask, .75, 1.5); },
        [&] { return local_maxima(input, 2); }};
    ExecutionControl control;
    unsigned events = 0;
    control.stage_progress = [&](std::string_view name, std::size_t done,
                                 std::size_t total) {
      check(!name.empty() && done <= total,
            "Invalid segmentation phase progress");
      ++events;
    };
    for (const auto &operation : work) {
      const auto raw = operation();
      events = 0;
      ExecutionScope scope(control);
      sameImage(raw, operation());
      check(events > 0, "Controlled segmentation did not report phases");
    }
    const auto sameLabels = [](const Labels &a, const Labels &b) {
      check(a.width == b.width && a.height == b.height && a.count == b.count &&
                a.pixels == b.pixels,
            "Controlled segmentation changed label pixels/counts");
    };
    Labels markers{input.width, input.height, 2,
                   std::vector<std::int32_t>(input.pixels.size(), 0)};
    markers.pixels.front() = 1;
    markers.pixels.back() = 2;
    for (int connectivity : {4, 8}) {
      const auto labels = connected_components(mask, connectivity);
      const auto flooded = watershed(input, markers, nullptr, connectivity);
      const auto split = binary_watershed(mask, 1, connectivity);
      const auto properties = region_properties(labels, &input, .75, 1.5);
      ExecutionScope scope(control);
      sameLabels(labels, connected_components(mask, connectivity));
      sameLabels(flooded, watershed(input, markers, nullptr, connectivity));
      sameLabels(split, binary_watershed(mask, 1, connectivity));
      const auto measured = region_properties(labels, &input, .75, 1.5);
      check(properties.size() == measured.size(),
            "Controlled region count changed");
      for (std::size_t i = 0; i < properties.size(); ++i) {
        const auto &a = properties[i];
        const auto &b = measured[i];
        check(std::tie(a.label, a.x, a.y, a.width, a.height, a.pixels, a.area,
                       a.centroid_x, a.centroid_y, a.perimeter, a.mean,
                       a.minimum, a.maximum, a.integrated_density,
                       a.eccentricity, a.circularity, a.touches_border) ==
                  std::tie(b.label, b.x, b.y, b.width, b.height, b.pixels,
                           b.area, b.centroid_x, b.centroid_y, b.perimeter,
                           b.mean, b.minimum, b.maximum, b.integrated_density,
                           b.eccentricity, b.circularity, b.touches_border),
              "Controlled region measurements changed");
      }
    }
  }
}
void testControlledNumerics() {
  for (const auto &input : {fixture(), fixture(3, PixelType::Float64)}) {
    const std::vector<std::function<Image()>> work{
        [&] { return point(input, "add", 3); },
        [&] { return calculate(input, input, "multiply"); },
        [&] { return convert(input, PixelType::UInt8, true); },
        [&] { return linear(input, 1.1, 3); },
        [&] { return normalize(input, 0, 255, 3); },
        [&] { return fill(input, 17); },
        [&] { return add_noise(input, "gaussian", 3, 42); },
        [&] { return gaussian_blur(input, 1.5, 1.5); },
        [&] { return box_blur(input, 2); },
        [&] { return rank_filter(input, 2, "median"); },
        [&] { return rank_filter(input, 2, "mean"); },
        [&] { return rank_filter(input, 2, "variance"); },
        [&] { return rank_filter(input, 2, "minimum"); },
        [&] { return rank_filter(input, 2, "maximum"); },
        [&] { return convolve(input, std::vector<double>(49, 1. / 49), 7, 7); },
        [&] { return sobel(input); },
        [&] { return unsharp_mask(input, 1.5, .6); },
        [&] { return bilateral_filter(input, 2, 1.5, 3); },
        [&] { return kuwahara_filter(input, 2); },
        [&] { return rolling_ball_background(input, 3); },
        [&] { return remove_outliers(input, 2, 30); },
        [&] { return anisotropic_diffusion(input, 2, 10, .2); }};
    for (const auto &operation : work) {
      const auto raw = operation();
      ExecutionControl control;
      unsigned events = 0;
      control.stage_progress = [&](std::string_view name, std::size_t completed,
                                   std::size_t total) {
        check(!name.empty() && completed <= total, "Invalid phase event");
        ++events;
      };
      ExecutionScope scope(control);
      sameImage(raw, operation());
      check(events > 0, "Stage-only execution control was ignored");
    }
    if (input.channels != 1)
      continue;
    const std::vector<std::function<Image()>> grayWork{
        [&] { return apply_lut(input, std::vector<double>(65536, 17)); },
        [&] { return threshold(input, 100, 30000); },
        [&] { return in_range(input, {100}, {30000}); },
        [&] { return clahe(input, 16, 16); },
        [&] { return local_threshold(input, "median", 2, .2, 0, 128, 15); }};
    for (const auto &operation : grayWork) {
      const auto raw = operation();
      ExecutionControl control;
      control.cancellation = std::make_shared<CancellationToken>();
      ExecutionScope scope(control);
      sameImage(raw, operation());
    }
  }
}
void testNestedCallbacksAndFailure() {
  ExecutionControl outer, inner;
  outer.cancellation = std::make_shared<CancellationToken>();
  unsigned outerEvents = 0, innerEvents = 0;
  outer.stage_progress = [&](std::string_view, std::size_t n, std::size_t) {
    ++outerEvents;
    if (n == 1024)
      outer.cancellation->cancel();
  };
  inner.stage_progress = [&](std::string_view, std::size_t, std::size_t) {
    ++innerEvents;
  };
  bool canceled = false;
  try {
    ExecutionScope first(outer);
    ExecutionScope second(inner);
    (void)point(fixture(), "add", 3);
  } catch (const OperationCancelled &) {
    canceled = true;
  }
  check(canceled && outerEvents == 2 && innerEvents == 2,
        "Nested phase controls lost a callback or cancellation");
  check(current_execution_scope == nullptr,
        "Canceled scope remained installed");
  ExecutionControl failing;
  failing.stage_progress = [](std::string_view, std::size_t, std::size_t) {
    throw std::runtime_error("phase callback failure");
  };
  bool failed = false;
  try {
    ExecutionScope scope(failing);
    (void)point(fixture(), "add", 3);
  } catch (const std::runtime_error &error) {
    failed = std::string(error.what()) == "phase callback failure";
  }
  check(failed && current_execution_scope == nullptr,
        "Callback failure did not restore execution scope");
}
} // namespace
int main() {
  try {
    testRankSetupCancellation();
    testDirectAndSetupCancellation();
    testLibraryWorkCancellation();
    testSegmentationCancellation();
    testSegmentationControlledNumerics();
    testControlledNumerics();
    testNestedCallbacksAndFailure();
    std::cout << "Kernel phase/cancellation checks passed\n";
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
