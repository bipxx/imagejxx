#include <imagejxx/application.hpp>
#include <array>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string_view>
#include <utility>
#include <vector>

using namespace imagejxx;
namespace {
constexpr std::string_view validation_stage = "Immutable plane sample validation";
constexpr int width = 4097, height = 2, depth = 2;
constexpr std::size_t plane_samples = std::size_t(width) * height;
constexpr auto quiet_policy = FloatingSamplePolicy::PreserveQuietNaNFloat32;
constexpr std::array<std::uint32_t, 8> quiet_bits{
    0, 0x80000000u, 1, 0x007fffffu, 0x7fc12345u, 0xffc34567u,
    0x3f800000u, 0xbf800000u};

void check(bool condition, const char *message) {
  if (!condition) throw std::runtime_error(message);
}
template <class Error, class Function>
void rejects(Function function, const char *message) {
  bool rejected = false;
  try { function(); } catch (const Error &) { rejected = true; }
  check(rejected, message);
}
SpatialCalibration calibration() {
  SpatialCalibration result;
  result.spacing = {.25, 1.5, 3};
  result.origin = {-2, 4, 9};
  result.direction = {-1, .5, .25, 0, 1, .5, .5, 0, 1};
  result.unit = "um";
  result.time_unit = "min";
  result.frame_interval = 2.5;
  result.time_origin = -3;
  return result;
}
Dataset validation_input(PixelType type, FloatingSamplePolicy policy) {
  Stack planes;
  for (int z = 0; z < depth; ++z) {
    Image image(width, height, 1, type);
    for (std::size_t i = 0; i < image.pixels.size(); ++i) {
      const auto code = int((i + std::size_t(z) * 17) % 251);
      if (policy == quiet_policy)
        image.pixels[i] = float32_source_sample(
            quiet_bits[(std::size_t(z) * plane_samples + i) % quiet_bits.size()], policy);
      else if (type == PixelType::UInt16) image.pixels[i] = 40000 + code;
      else if (type == PixelType::Float32) image.pixels[i] = code - 125.25;
      else if (type == PixelType::Float64) image.pixels[i] = code - 125.125;
      else image.pixels[i] = code;
    }
    planes.push_back(std::move(image));
  }
  return Dataset::from_planes({1, 1, depth, height, width, 1}, std::move(planes),
                              calibration(), policy);
}
bool same_calibration(const SpatialCalibration &a, const SpatialCalibration &b) {
  return a.spacing == b.spacing && a.origin == b.origin && a.direction == b.direction &&
         a.unit == b.unit && a.time_unit == b.time_unit &&
         a.frame_interval == b.frame_interval && a.time_origin == b.time_origin;
}
bool same_display(const DisplayState &a, const DisplayState &b) {
  return a.t == b.t && a.c == b.c && a.orthogonal == b.orthogonal &&
         a.linkedZoom == b.linkedZoom && a.crosshair == b.crosshair &&
         a.zoom == b.zoom && a.planeZoom == b.planeZoom && a.pan == b.pan &&
         a.revision == b.revision && a.properties == b.properties;
}
bool same_history(const HistoryInfo &a, const HistoryInfo &b) {
  return a.undo_steps == b.undo_steps && a.redo_steps == b.redo_steps &&
         a.step_limit == b.step_limit && a.pixel_budget == b.pixel_budget &&
         a.pixels.logical_bytes == b.pixels.logical_bytes &&
         a.pixels.current_bytes == b.pixels.current_bytes &&
         a.pixels.retained_bytes == b.pixels.retained_bytes &&
         a.pixels.obsolete_bytes == b.pixels.obsolete_bytes &&
         a.pixels.retained_planes == b.pixels.retained_planes &&
         a.pixels.exact == b.pixels.exact;
}
struct SourceGuard {
  DocumentSnapshot snapshot;
  HistoryInfo history;
  SpatialCalibration physical;
  Stack copied_pixels;
  explicit SourceGuard(const Document &owner)
      : snapshot(owner.snapshot()), history(owner.history()),
        physical(snapshot.data().dataset.calibration()) {
    for (int z = 0; z < depth; ++z)
      copied_pixels.push_back(*snapshot.data().dataset.plane({0, 0, z}));
  }
  void unchanged(const Document &owner) const {
    const auto after = owner.snapshot();
    const auto a = after.revisions(), b = snapshot.revisions();
    check(after.same_document(snapshot) && after.shares_data(snapshot) &&
          after.data_identity() == snapshot.data_identity() && after.dirty() == snapshot.dirty() &&
          a.pixels == b.pixels && a.metadata == b.metadata &&
          a.selection == b.selection && a.display == b.display &&
          same_display(after.display(), snapshot.display()) &&
          same_calibration(after.data().dataset.calibration(), physical) &&
          same_history(owner.history(), history),
          "Validation changed source Data/calibration/display/revisions/dirty/history");
    for (int z = 0; z < depth; ++z) {
      const auto image = after.data().dataset.plane({0, 0, z});
      const auto &before = copied_pixels[std::size_t(z)];
      check(image->type == before.type && image->pixels.size() == before.pixels.size() &&
            std::memcmp(image->pixels.data(), before.pixels.data(),
                        before.pixels.size() * sizeof(double)) == 0,
            "Validation changed source sample bits");
    }
  }
};

void positive_validation_cancellation() {
  const std::array<std::pair<PixelType, FloatingSamplePolicy>, 5> policies{{
      {PixelType::UInt8, FloatingSamplePolicy::FiniteOnly},
      {PixelType::UInt16, FloatingSamplePolicy::FiniteOnly},
      {PixelType::Float32, FloatingSamplePolicy::FiniteOnly},
      {PixelType::Float64, FloatingSamplePolicy::FiniteOnly},
      {PixelType::Float32, quiet_policy},
  }};
  for (const auto &entry : policies) {
    auto source = std::make_shared<Document>(validation_input(entry.first, entry.second),
                                            "Validation source");
    Application app;
    app.adopt(source);
    const SourceGuard preserved(*source);
    const auto ids = app.document_ids();
    const auto active = app.active_document_id();
    auto token = std::make_shared<CancellationToken>();
    ExecutionControl control;
    control.cancellation = token;
    std::size_t completed_copies = 0, positive_work = 0;
    control.stage_progress = [&](std::string_view stage, std::size_t done, std::size_t total) {
      if (stage == "plane-extract.copy" && done == total) ++completed_copies;
      if (stage != validation_stage) return;
      check(total == plane_samples && completed_copies == depth,
            "Named validation did not follow complete output extraction");
      if (done > 0) {
        check(done == 1024, "First positive validation callback exceeds 1024 samples");
        positive_work = done;
        token->cancel();
      }
    };
    std::optional<PreparedOperation> candidate;
    bool publication_reached = false;
    rejects<OperationCancelled>([&] {
      candidate.emplace(app.prepare_axis_range_reslice({Plane::XY, 0, 1, 1}, source->id, control));
      publication_reached = true;
      app.publish(*candidate);
    }, "Cancellation during positive final validation was ignored");
    check(positive_work == 1024 && token->canceled() && !candidate && !publication_reached &&
          app.document_ids() == ids && app.active_document_id() == active,
          "Canceled final validation returned a candidate or reached publication");
    preserved.unchanged(*source);
  }
  std::cout << "G029.range.validation.V01_positive_work_cancellation_atomic\n";
}

void successful_validation_polling_and_bits() {
  auto source = std::make_shared<Document>(validation_input(PixelType::Float32, quiet_policy),
                                          "Quiet source");
  Application app;
  app.adopt(source);
  const SourceGuard preserved(*source);
  ExecutionControl control;
  std::size_t previous = 0, runs = 0, completed = 0;
  control.stage_progress = [&](std::string_view stage, std::size_t done, std::size_t total) {
    if (stage != validation_stage) return;
    check(total == plane_samples, "Validation stage reports an incorrect local sample total");
    if (done == 0) {
      check(runs == completed, "Validation began another plane before finishing the prior plane");
      ++runs;
      previous = 0;
    } else {
      check(done > previous && done - previous <= 1024 && done <= total,
            "Positive validation callbacks have an unbounded sample gap");
      previous = done;
      if (done == total) ++completed;
    }
  };
  auto prepared = app.prepare_axis_range_reslice({Plane::XY, 0, 1, 1}, source->id, control);
  auto candidate = prepared.prepared_new_document();
  check(runs == depth && completed == depth && candidate && !app.contains(candidate->id),
        "Successful validation did not finish both planes before exposing the unregistered owner");
  const auto result = app.publish(prepared);
  check(result.created && result.document == candidate && result.document_id == candidate->id &&
        app.document_ids() == std::vector<std::uint64_t>{source->id, candidate->id} &&
        !prepared.active() && candidate->undo_steps() == 0 && candidate->redo_steps() == 0 &&
        !candidate->snapshot().dirty(), "Successful validation changed fresh publication/history rules");
  const auto output_snapshot = candidate->snapshot();
  const auto &output = output_snapshot.data().dataset;
  check(output.pixel_type() == PixelType::Float32 && output.sample_policy() == quiet_policy &&
        same_calibration(output.calibration(), calibration()),
        "Polling changed sample policy/type/geometry");
  for (int z = 0; z < depth; ++z) {
    const auto image = output.plane({0, 0, z});
    for (std::size_t i = 0; i < plane_samples; ++i)
      check(float32_source_sample_bits(image->pixels[i], quiet_policy) ==
            quiet_bits[(std::size_t(z) * plane_samples + i) % quiet_bits.size()],
            "Validation changed quiet-NaN payload/subnormal/signed-zero bits");
  }
  preserved.unchanged(*source);
  std::cout << "G029.range.validation.V02_polling_policy_bits_publication\n";
}

class TallSource final : public PlaneSource {
public:
  const DatasetShape sizes{2, 3, 1009, 3, 5, 4};
  mutable std::size_t plane_reads = 0;
  mutable std::vector<std::array<int, 7>> regions;
  DatasetShape shape() const override { return sizes; }
  PixelType pixel_type() const override { return PixelType::Float64; }
  std::shared_ptr<const Image> read_plane(PlanePosition) const override {
    ++plane_reads;
    throw std::runtime_error("Tall source must use selected regional strips");
  }
  static double literal(PlanePosition p, int x, int y, int sample) {
    return 1000000 * p.t + 100000 * p.c + 100 * p.z + 10 * y + x + sample * .125;
  }
  Image read_region(PlanePosition p, int x, int y, int w, int h) const override {
    regions.push_back({p.t, p.c, p.z, x, y, w, h});
    Image image(w, h, sizes.s, pixel_type());
    for (int v = 0; v < h; ++v)
      for (int u = 0; u < w; ++u)
        for (int s = 0; s < sizes.s; ++s)
          image.at(u, v, s) = literal(p, x + u, y + v, s);
    return image;
  }
};
void tall_z_cap_and_read_totals() {
  for (const auto plane : {Plane::XZ, Plane::YZ}) {
    auto counter = std::make_shared<TallSource>();
    const Dataset source(counter, calibration());
    const int horizontal = plane == Plane::XZ ? counter->sizes.x : counter->sizes.y;
    const std::size_t bytes_per_plane = std::size_t(horizontal) * counter->sizes.z *
                                         counter->sizes.s * sizeof(double);
    const std::size_t fallback = std::size_t(counter->sizes.x) * counter->sizes.y *
                                  counter->sizes.s * sizeof(double);
    const std::size_t strip = std::size_t(horizontal) * counter->sizes.s * sizeof(double);
    const std::size_t cap = 2 * bytes_per_plane + fallback + strip;
    check(bytes_per_plane > 100 * fallback, "Tall-Z case does not expose the plane/fallback size difference");
    AxisRangeResliceRequest request{plane, plane == Plane::XZ ? 0 : 1,
                                  plane == Plane::XZ ? 2 : 4, 2, 1, 2, cap - 1};
    rejects<std::invalid_argument>([&] { reslice_axis_range(source, request); },
                                  "Cap minus one admitted the complete tall-Z output");
    check(counter->plane_reads == 0 && counter->regions.empty(),
          "Rejected tall-Z memory cap read source pixels");
    request.max_working_bytes = cap;
    ExecutionControl control;
    std::size_t previous = 0, callbacks = 0;
    const auto expected_reads = std::size_t(2) * counter->sizes.z;
    control.progress = [&](std::size_t done, std::size_t total) {
      check(total == expected_reads && done >= previous && done <= total,
            "Tall-Z progress did not use monotonic complete source-read totals");
      previous = done;
      ++callbacks;
    };
    const auto result = reslice_axis_range(source, request, control);
    check(result.working_bytes == cap && callbacks > 0 && previous == expected_reads &&
          counter->plane_reads == 0 && counter->regions.size() == expected_reads &&
          result.dataset.shape().dimensions() == std::array<int, 6>{1, 1, 2, 1009, horizontal, 4},
          "Exact cap failed or tall-Z extraction read unselected planes");
    for (int k = 0; k < 2; ++k) {
      const int coordinate = request.first + k * request.step;
      const auto image = result.dataset.plane({0, 0, k});
      for (int z = 0; z < counter->sizes.z; ++z) {
        const std::array<int, 7> expected = plane == Plane::XZ
            ? std::array<int, 7>{1, 2, z, 0, coordinate, 5, 1}
            : std::array<int, 7>{1, 2, z, coordinate, 0, 1, 3};
        check(counter->regions[std::size_t(k) * counter->sizes.z + z] == expected,
              "Tall-Z selected strip extent/order/T/C differs");
        for (int u = 0; u < horizontal; ++u)
          for (int s = 0; s < counter->sizes.s; ++s)
            check(image->at(u, z, s) == TallSource::literal({1, 2, z},
                  plane == Plane::XZ ? u : coordinate,
                  plane == Plane::XZ ? coordinate : u, s),
                  "Tall-Z output pixel/sample order differs from independent literal input");
      }
    }
  }
  std::cout << "G029.range.validation.V03_tall_z_cap_selected_read_totals\n";
}
} // namespace
int main() {
  try {
    positive_validation_cancellation();
    successful_validation_polling_and_bits();
    tall_z_cap_and_read_totals();
    return 0;
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
