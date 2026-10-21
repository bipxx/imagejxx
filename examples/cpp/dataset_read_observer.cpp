#include <imagejxx/dataset.hpp>
#include <imagejxx/document.hpp>
#include <imagejxx/shortcuts.hpp>
#include <iostream>
#include <stdexcept>

namespace {
void require(bool condition, const char *message) {
  if (!condition)
    throw std::runtime_error(message);
}
}

int main() {
  using namespace imagejxx;
  try {
    Stack planes;
    for (int z = 0; z < 2; ++z) {
      Image image(4, 3, 1, PixelType::UInt8);
      for (int y = 0; y < 3; ++y)
        for (int x = 0; x < 4; ++x)
          image.at(x, y) = 100 * z + 10 * y + x;
      planes.push_back(std::move(image));
    }
    SpatialCalibration calibration;
    calibration.spacing = {.5, 2, 3};
    calibration.origin = {10, 20, 30};
    auto dataset = Dataset::from_planes({1, 1, 2, 3, 4, 1},
                                        std::move(planes), calibration);
    DatasetReadObserver first, second;
    auto copy = first;
    auto observed = dataset.observe_reads(first).observe_reads(second)
                           .observe_reads(copy);
    require(first.counts().plane_reads == 0 && first.counts().region_reads == 0,
            "Observation must not read pixels");
    const bool shares = observed.shares_pixels(dataset);
    const bool eager = observed.shared_planes().has_value();
    require(shares && eager, "Observation must preserve storage and identity");
    require(observed.calibration().origin == calibration.origin &&
                observed.calibration().spacing == calibration.spacing,
            "Observation must preserve calibration");
    require(observed.plane({0, 0, 1})->at(3, 2) == 123, "Exact plane samples");
    require(observed.region({}, 1, 1, 2, 1).at(1, 0) == 12, "Exact region samples");
    Image replacement(4, 3, 1, PixelType::UInt8);
    for (auto &value : replacement.pixels)
      value = 7;
    auto edited = observed.with_plane({0, 0, 1}, std::move(replacement));
    require(first.counts().plane_reads == 1 && first.counts().region_reads == 1,
            "Edits must not read the source");
    require(edited.shared_planes()->shared(0) == dataset.shared_planes()->shared(0),
            "Unchanged plane storage must stay shared");
    require(edited.plane({0, 0, 1})->at(3, 2) == 7 &&
                dataset.plane({0, 0, 1})->at(3, 2) == 123,
            "Editing must preserve the original dataset");
    const auto reads = first.counts(), other = second.counts();
    require(reads.plane_reads == 2 && reads.region_reads == 1 && !reads.overflowed &&
                other.plane_reads == 2 && other.region_reads == 1,
            "Independent observers count once per provider entry");
    Document owner(observed);
    const auto before = owner.snapshot().revisions();
    const auto counts_before = first.counts();
    auto profile = compile_shortcuts({"analysis.measure=Primary+K"});
    auto cleared = clear_shortcut(profile, "analysis.measure");
    require(!cleared.profile.chord("analysis.measure", ShortcutPlatform::Windows),
            "Native shortcut result");
    const auto counts_after = first.counts();
    const auto after = owner.snapshot().revisions();
    require(counts_before.plane_reads == counts_after.plane_reads &&
                counts_before.region_reads == counts_after.region_reads &&
                before.matches(after, Change::All),
            "Shortcut configuration must preserve document and provider calls");
    std::cout << std::boolalpha << "{\"plane_reads\":" << reads.plane_reads
              << ",\"region_reads\":" << reads.region_reads
              << ",\"overflowed\":" << reads.overflowed
              << ",\"storage_shared\":" << shares
              << ",\"eager_preserved\":" << eager
              << ",\"shortcut_plane_delta\":"
              << counts_after.plane_reads - counts_before.plane_reads
              << ",\"shortcut_region_delta\":"
              << counts_after.region_reads - counts_before.region_reads << "}\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
