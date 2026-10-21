#include <imagejxx/manual_threshold.hpp>

#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>
#include <numeric>
#include <stdexcept>
#include <string_view>

using namespace imagejxx;
namespace {
void check(bool condition, const char *message) {
  if (!condition) throw std::runtime_error(message);
}
template <class Error = std::invalid_argument, class Function>
void rejects(Function function, const char *message) {
  bool rejected = false;
  try { function(); } catch (const Error &) { rejected = true; }
  check(rejected, message);
}
Image plane(PixelType type, std::vector<double> values, int width = 3) {
  Image result(width, static_cast<int>(values.size()) / width, 1, type);
  result.pixels = std::move(values);
  return result;
}
void histogram_invariants(const ManualThresholdSummary &summary) {
  const auto &histogram = summary.histogram;
  check(histogram.counts.size() == 256 && histogram.edges.size() == 257,
        "Threshold histogram is not bounded to 256 bins");
  check(std::accumulate(histogram.counts.begin(), histogram.counts.end(),
                        std::uint64_t(0)) == summary.sample_count,
        "Threshold histogram lost or duplicated source samples");
  check(std::all_of(histogram.edges.begin(), histogram.edges.end(),
                    [](double value) { return std::isfinite(value); }) &&
            std::is_sorted(histogram.edges.begin(), histogram.edges.end()),
        "Threshold histogram edges overflowed or went backwards");
}
void typed_inclusive_masks() {
  for (const auto type : {PixelType::UInt8, PixelType::UInt16,
                          PixelType::Float32, PixelType::Float64}) {
    const auto source = plane(type, {0, 1, 2, 3, 4, 5});
    const auto saved = source.pixels;
    const auto summary = analyze_manual_threshold(source, {1, 4});
    histogram_invariants(summary);
    check(summary.minimum == 0 && summary.maximum == 5 &&
              summary.sample_count == 6 && summary.matched_count == 4,
          "Threshold raw extrema or inclusive match count changed");
    const auto output = manual_threshold_mask(source, {1, 4});
    check(output.type == PixelType::UInt8 && output.width == 3 &&
              output.height == 2 && output.channels == 1 &&
              output.pixels == std::vector<double>({0, 255, 255, 255, 255, 0}),
          "Threshold mask lost inclusive endpoints or binary output shape/type");
    check(source.pixels == saved, "Threshold operation changed source pixels");
    if (type == PixelType::UInt8) {
      check(summary.histogram.edges.front() == 0 &&
                summary.histogram.edges.back() == 256 &&
                summary.histogram.counts[0] == 1 &&
                summary.histogram.counts[5] == 1,
            "UInt8 threshold histogram does not use raw intensity bins");
    } else if (type == PixelType::UInt16) {
      check(summary.histogram.edges.front() == 0 &&
                summary.histogram.edges.back() == 65536 &&
                summary.histogram.counts.front() == 6,
            "UInt16 threshold histogram did not use 256 bounded raw bins");
    }
  }
  const auto u16 = analyze_manual_threshold(
      plane(PixelType::UInt16, {0, 255, 256, 32768, 65280, 65535}), {255, 65280});
  check(u16.histogram.counts[0] == 2 && u16.histogram.counts[1] == 1 &&
            u16.histogram.counts[128] == 1 && u16.histogram.counts[255] == 2 &&
            u16.matched_count == 4,
        "UInt16 histogram boundaries or endpoint threshold changed");
}
void floating_ranges() {
  for (const auto type : {PixelType::Float32, PixelType::Float64}) {
    auto source = plane(type, {-8, -4, -2, -1, -0.5, 0});
    const auto summary = analyze_manual_threshold(source, {-4, -0.5});
    histogram_invariants(summary);
    check(summary.minimum == -8 && summary.maximum == 0 &&
              summary.matched_count == 4 && summary.histogram.counts.front() == 1 &&
              summary.histogram.counts.back() == 1,
          "Negative floating threshold histogram was display-scaled or clipped");
    source.pixels.assign(6, -3.5);
    const auto constant = analyze_manual_threshold(source, {-3.5, -3.5});
    histogram_invariants(constant);
    check(constant.minimum == -3.5 && constant.maximum == -3.5 &&
              constant.matched_count == 6 && constant.histogram.counts[0] == 6 &&
              constant.histogram.edges.front() == -3.5 &&
              constant.histogram.edges.back() == -3.5,
          "Constant floating histogram did not keep explicit degenerate range");
  }
  const auto maximum = std::numeric_limits<double>::max();
  const auto extreme = analyze_manual_threshold(
      plane(PixelType::Float64, {-maximum, -maximum / 2, 0, maximum / 2,
                                 std::nextafter(maximum, 0.0), maximum}),
      {-maximum / 2, maximum / 2});
  histogram_invariants(extreme);
  check(extreme.minimum == -maximum && extreme.maximum == maximum &&
            extreme.matched_count == 3 && extreme.histogram.counts.front() == 1 &&
            extreme.histogram.counts.back() == 2,
        "Extreme finite Float64 interval overflowed the histogram normalization");
  for (double value : {-maximum, maximum}) {
    const auto constant = analyze_manual_threshold(
        plane(PixelType::Float64, {value, value, value}), {value, value});
    histogram_invariants(constant);
    check(constant.histogram.counts[0] == 3 && constant.minimum == value,
          "Extreme finite constant cannot be analyzed");
  }
  const auto tiny = std::numeric_limits<double>::denorm_min();
  histogram_invariants(analyze_manual_threshold(
      plane(PixelType::Float64, {0, tiny, tiny * 2}), {0, tiny * 2}));
  histogram_invariants(analyze_manual_threshold(
      plane(PixelType::Float64, {std::nextafter(maximum, 0.0), maximum, maximum}),
      {maximum, maximum}));
}
void area_masks() {
  const auto ring = plane(PixelType::UInt8, {5, 5, 5, 5, 0, 5, 5, 5, 5});
  const auto selected = manual_threshold_selection(ring, {5, 5});
  check(selected.kind == "mask" && selected.mask_width() == 3 &&
            selected.mask_height() == 3 &&
            selected.packed_mask() == std::vector<unsigned char>({0xf7, 0x80}),
        "Threshold area selection filled a hole or changed packed bit order");
  check(selected.mask(3, 3).pixels ==
            std::vector<double>({255, 255, 255, 255, 0, 255, 255, 255, 255}),
        "Threshold area selection raster membership changed");
  const auto empty = manual_threshold_selection(ring, {6, 7});
  check(empty.kind == "mask" && empty.packed_mask() ==
            std::vector<unsigned char>({0, 0}),
        "Empty threshold selection became absent or full canvas");
  const auto full = manual_threshold_selection(ring, {0, 5});
  check(full.packed_mask() == std::vector<unsigned char>({0xff, 0x80}),
        "Threshold full mask left unused packed bits enabled");
}
void display_overlay() {
  const auto source = plane(PixelType::Float64, {-10, 5, 20});
  const auto saved = source.pixels;
  Image rgb(3, 1, 3, PixelType::UInt8);
  rgb.pixels = {10, 11, 12, 30, 31, 32, 50, 51, 52};
  const auto unblended = rgb.pixels;
  ManualThresholdLimits limits;
  limits.max_working_bytes = 1;
  blend_manual_threshold_overlay(source, rgb, {5, 5}, limits);
  check(rgb.pixels == std::vector<double>({10, 11, 12, 255, 15, 16, 50, 51, 52}) &&
            source.pixels == saved,
        "Threshold overlay failed inclusive raw-intensity red blend or changed source");
  rgb.pixels = unblended;
  rgb.pixels.back() = 256;
  const auto invalid = rgb.pixels;
  rejects([&] { blend_manual_threshold_overlay(source, rgb, {-10, 20}); },
          "Threshold overlay admitted an invalid RGB sample");
  check(rgb.pixels == invalid, "Threshold overlay mutated output before validating all samples");
  Image wrong(2, 1, 3, PixelType::UInt8);
  rejects([&] { blend_manual_threshold_overlay(source, wrong, {5, 5}); },
          "Threshold overlay admitted mismatched output extent");
}
void admission_and_limits() {
  const DatasetShape shape{4, 3, 5, 2, 3, 1};
  const auto estimate = estimate_manual_threshold(shape, PixelType::Float64,
                                                  {3, 2, 4});
  check(estimate.pixel_count == 6 && estimate.analysis_bytes == 4104 &&
            estimate.mask_bytes == 48 && estimate.selection_bytes == 1,
        "Metadata estimate counted an entire volume instead of one scalar plane");
  rejects<std::out_of_range>([&] {
    estimate_manual_threshold(shape, PixelType::UInt8, {4, 0, 0});
  }, "Threshold estimate admitted out-of-range time");
  rejects<std::out_of_range>([&] {
    estimate_manual_threshold(shape, PixelType::UInt8, {0, -1, 0});
  }, "Threshold estimate admitted negative scientific channel");
  rejects<std::out_of_range>([&] {
    estimate_manual_threshold(shape, PixelType::UInt8, {0, 0, 5});
  }, "Threshold estimate admitted out-of-range slice");
  rejects([&] { estimate_manual_threshold({1, 1, 1, 2, 3, 3}, PixelType::UInt8); },
          "Threshold estimate admitted RGB data");
  rejects([&] { estimate_manual_threshold({1, 1, 1, 0, 3, 1}, PixelType::UInt8); },
          "Threshold estimate admitted zero-sized extent");
  rejects([&] { estimate_manual_threshold(shape, static_cast<PixelType>(99)); },
          "Threshold estimate admitted unknown pixel type");
  auto source = plane(PixelType::UInt8, {0, 1, 2, 3, 4, 5});
  ManualThresholdLimits limits;
  limits.max_pixels = 5;
  rejects([&] { analyze_manual_threshold(source, {0, 5}, limits); },
          "Threshold analysis exceeded declared pixel bound");
  limits.max_pixels = 6;
  limits.max_working_bytes = 47;
  rejects([&] { manual_threshold_mask(source, {0, 5}, limits); },
          "Threshold mask exceeded double-backed output byte bound");
  limits.max_working_bytes = 48;
  check(manual_threshold_mask(source, {0, 5}, limits).pixels.size() == 6,
        "Exact threshold mask byte bound was rejected");
  limits.max_working_bytes = 1;
  check(manual_threshold_selection(source, {0, 5}, limits).packed_mask().size() == 1,
        "Packed selection incorrectly reserved a double-backed mask");
  limits.max_working_bytes = 4103;
  rejects([&] { analyze_manual_threshold(source, {0, 5}, limits); },
          "Threshold analysis exceeded fixed histogram byte bound");
  limits.max_working_bytes = 4104;
  histogram_invariants(analyze_manual_threshold(source, {0, 5}, limits));
  source.pixels.pop_back();
  rejects([&] { analyze_manual_threshold(source, {0, 5}); },
          "Threshold analysis admitted inconsistent backing storage");
  const auto infinity = std::numeric_limits<double>::infinity();
  const auto nan = std::numeric_limits<double>::quiet_NaN();
  for (const auto range : {ManualThresholdRange{5, 4}, {nan, 5},
                           {0, infinity}, {-infinity, 0}}) {
    rejects([&] { range.validate(); }, "Threshold range admitted invalid bounds");
  }
  for (const auto type : {PixelType::UInt8, PixelType::UInt16,
                          PixelType::Float32, PixelType::Float64}) {
    for (double value : {nan, infinity, -infinity}) {
      const auto invalid = plane(type, {0, value, 2});
      rejects([&] { analyze_manual_threshold(invalid, {0, 2}); },
              "Threshold analysis admitted a nonfinite source sample");
      rejects([&] { manual_threshold_mask(invalid, {0, 2}); },
              "Threshold mask admitted a nonfinite source sample");
      rejects([&] { manual_threshold_selection(invalid, {0, 2}); },
              "Threshold selection admitted a nonfinite source sample");
    }
  }
  for (const auto invalid : {
          plane(PixelType::UInt8, {-1, 0, 1}),
          plane(PixelType::UInt8, {0, 255, 256}),
          plane(PixelType::UInt16, {0, 2.5, 65535}),
          plane(PixelType::UInt16, {0, 65535, 65536}),
          plane(PixelType::Float32, {0, 0.1, 1})})
    rejects([&] { analyze_manual_threshold(invalid, {0, 65536}); },
            "Threshold analysis admitted a noncanonical typed sample");
}
void cancellation() {
  Image source(4096, 2, 1, PixelType::UInt8);
  source.pixels.assign(source.pixels.size(), 17);
  const auto saved = source.pixels;
  for (const std::string_view name : {"Manual threshold analysis",
                                      "Manual threshold histogram",
                                      "Manual threshold sample validation",
                                      "Threshold pixels",
                                      "Manual threshold selection"}) {
    ExecutionControl control;
    control.cancellation = std::make_shared<CancellationToken>();
    std::size_t observed = 0;
    control.stage_progress = [&](std::string_view stage, std::size_t completed,
                                  std::size_t total) {
      if (stage == name && completed >= 1024) {
        check(completed < total, "Cancellation did not enter a bounded loop");
        observed = completed;
        control.cancellation->cancel();
      }
    };
    rejects<OperationCancelled>([&] {
      if (name == "Manual threshold analysis" || name == "Manual threshold histogram")
        (void)analyze_manual_threshold(source, {17, 17}, {}, control);
      else if (name == "Threshold pixels")
        (void)manual_threshold_mask(source, {17, 17}, {}, control);
      else
        (void)manual_threshold_selection(source, {17, 17}, {}, control);
    }, "Threshold processing did not honor inside-kernel cancellation");
    check(observed == 1024 && source.pixels == saved,
          "Threshold canceled processing changed source or skipped poll interval");
  }
  ExecutionControl canceled;
  canceled.cancellation = std::make_shared<CancellationToken>();
  canceled.cancellation->cancel();
  rejects<OperationCancelled>([&] {
    manual_threshold_mask(source, {0, 255}, {}, canceled);
  }, "Pre-canceled threshold operation allocated output or ran to completion");
}
void display_state() {
  DatasetShape shape{2, 3, 4, 5, 6, 1};
  DisplayState display;
  display.properties["unrelated"] = std::string("retained");
  check(!manual_threshold_state(display, shape), "Absent threshold state was enabled");
  set_manual_threshold_state(display, shape, {{-2.5, 7.25}, {1, 2, 3}});
  const auto state = manual_threshold_state(display, shape);
  check(state && state->range.lower == -2.5 && state->range.upper == 7.25 &&
            state->position.t == 1 && state->position.c == 2 && state->position.z == 3,
        "Threshold typed display state did not round-trip");
  const auto saved = display.properties;
  rejects<std::out_of_range>([&] {
    set_manual_threshold_state(display, shape, {{0, 1}, {2, 0, 0}});
  }, "Threshold display setter admitted out-of-range position");
  check(metadata_equal(saved, display.properties),
        "Rejected threshold display setter partially changed properties");
  auto base = MetadataObject{{"version", std::int64_t(1)}, {"lower", -1.0},
      {"upper", 2.0}, {"t", std::int64_t(0)}, {"c", std::int64_t(0)},
      {"z", std::int64_t(0)}};
  auto invalid = [&](MetadataObject record) {
    display.properties[manual_threshold_property] = metadata_object(std::move(record));
    rejects([&] { manual_threshold_state(display, shape); },
            "Threshold display parser admitted malformed schema");
  };
  auto record = base; record.erase("z"); invalid(record);
  record = base; record["extra"] = true; invalid(record);
  record = base; record["version"] = std::int64_t(2); invalid(record);
  record = base; record["version"] = true; invalid(record);
  record = base; record["c"] = 0.0; invalid(record);
  record = base; record["lower"] = std::string("-1"); invalid(record);
  record = base; record["lower"] = true; invalid(record);
  record = base; record["upper"] = -2.0; invalid(record);
  record = base; record["upper"] = std::numeric_limits<double>::infinity();
  // Immutable metadata admission rejects infinity before a parser can see it.
  // Keep that rejection inside its assertion rather than in test setup.
  rejects([&] { (void)metadata_object(record); },
          "Threshold metadata admitted a nonfinite intensity bound");
  rejects([&] {
    set_manual_threshold_state(display, shape,
        {{0, std::numeric_limits<double>::infinity()}, {0, 0, 0}});
  }, "Threshold display setter admitted a nonfinite intensity bound");
  display.properties[manual_threshold_property] = true;
  rejects([&] { manual_threshold_state(display, shape); },
          "Threshold display parser admitted boolean property");
  display.properties[manual_threshold_property] = metadata_object(base);
  auto color = shape; color.s = 3;
  rejects([&] { manual_threshold_state(display, color); },
          "Threshold display state was admitted on RGB data");
  reset_manual_threshold_state(display);
  check(!manual_threshold_state(display, shape) &&
            std::get<std::string>(display.properties.at("unrelated")) == "retained",
        "Threshold reset removed unrelated display properties");
}
} // namespace

int main() {
  try {
    typed_inclusive_masks();
    floating_ranges();
    area_masks();
    display_overlay();
    admission_and_limits();
    cancellation();
    display_state();
    std::cout << "Manual threshold native tests passed\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
