#include <imagejxx/application.hpp>
#include <array>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <future>
#include <iostream>
#include <limits>
#include <mutex>
#include <stdexcept>

using namespace imagejxx;
namespace {
void check(bool value, const char *message) {
  if (!value) throw std::runtime_error(message);
}
template <class Error, class Function>
void rejects(Function function, const char *message) {
  bool rejected = false;
  try { function(); } catch (const Error &) { rejected = true; }
  check(rejected, message);
}
SpatialCalibration calibration() {
  SpatialCalibration c;
  c.spacing = {.25, 1.5, 3};
  c.origin = {-2, 4, 9};
  c.direction = {-1, .5, .25, 0, 1, .5, .5, 0, 1};
  c.unit = "um";
  c.time_unit = "min";
  c.frame_interval = 2.5;
  c.time_origin = -3;
  return c;
}
double value(int t, int c, int z, int y, int x, int s, PixelType type) {
  const auto number = 64 * t + 32 * c + 8 * z + 2 * y + x + 24 * s;
  if (type == PixelType::UInt16) return 4096 + number;
  if (type == PixelType::Float32) return -32.5 + number;
  if (type == PixelType::Float64) return -32.375 + number;
  return number;
}
Dataset formula(PixelType type = PixelType::Float64, int samples = 1) {
  Stack images;
  for (int t = 0; t < 2; ++t)
    for (int c = 0; c < 2; ++c)
      for (int z = 0; z < 3; ++z) {
        Image image(5, 4, samples, type);
        for (int y = 0; y < 4; ++y)
          for (int x = 0; x < 5; ++x)
            for (int s = 0; s < samples; ++s)
              image.at(x, y, s) = value(t, c, z, y, x, s, type);
        images.push_back(std::move(image));
      }
  return Dataset::from_planes({2, 2, 3, 4, 5, samples}, std::move(images), calibration());
}
std::array<int, 3> source_voxel(Plane p, int coordinate, int u, int v) {
  if (p == Plane::XY) return {u, v, coordinate};
  if (p == Plane::XZ) return {u, coordinate, v};
  return {coordinate, u, v};
}
void axes_types_samples_calibration() {
  for (const auto type : {PixelType::UInt8, PixelType::UInt16, PixelType::Float32, PixelType::Float64})
    for (int samples : {1, 3, 4})
      for (const auto p : {Plane::XY, Plane::XZ, Plane::YZ}) {
        auto source = formula(type, samples);
        const AxisPlaneExportRequest request{p, 2, 1, 1};
        const auto output = export_axis_plane(source, request);
        const int width = p == Plane::YZ ? 4 : 5;
        const int height = p == Plane::XY ? 4 : 3;
        check(output.dataset.shape().dimensions() == std::array<int, 6>{1, 1, 1, height, width, samples},
              "Output must be one ordinary XY plane with unchanged S");
        check(output.dataset.pixel_type() == type && output.dataset.sample_policy() == source.sample_policy(),
              "Type and floating sample policy must be preserved");
        check(!output.dataset.shares_pixels(source), "Export must own a detached pixel plane");
        const auto &original = source.calibration();
        const auto &actual = output.dataset.calibration();
        const std::array<int, 3> axes = p == Plane::XY ? std::array<int, 3>{0, 1, 2}
            : p == Plane::XZ ? std::array<int, 3>{0, 2, 1} : std::array<int, 3>{1, 2, 0};
        const auto zero = source_voxel(p, 2, 0, 0);
        check(actual.origin == original.voxel_to_world({double(zero[0]), double(zero[1]), double(zero[2])}),
              "Export origin must be the source fixed-coordinate voxel center");
        check(actual.time_origin == -.5 && actual.frame_interval == 2.5 && actual.time_unit == "min" && actual.unit == "um",
              "Selected time origin and spatial/time units must remain calibrated");
        for (int column = 0; column < 3; ++column) {
          check(actual.spacing[column] == original.spacing[axes[column]], "Spatial spacing permutation differs");
          for (int row = 0; row < 3; ++row)
            check(actual.direction[row * 3 + column] == original.direction[row * 3 + axes[column]],
                  "Full sheared/reflected direction columns must be preserved");
        }
        const auto image = output.dataset.plane();
        for (int v = 0; v < height; ++v)
          for (int u = 0; u < width; ++u) {
            const auto voxel = source_voxel(p, 2, u, v);
            for (int s = 0; s < samples; ++s)
              check(image->at(u, v, s) == value(1, 1, voxel[2], voxel[1], voxel[0], s, type),
                    "Axis plane pixel/sample correspondence differs from the asymmetric formula");
            check(actual.voxel_to_world({double(u), double(v), 0}) ==
                  original.voxel_to_world({double(voxel[0]), double(voxel[1]), double(voxel[2])}),
                  "Output world affine differs from corresponding source voxel centers");
          }
        const auto output_bytes = std::size_t(width) * height * samples * sizeof(double);
        const auto strip_bytes = p == Plane::XY ? 0 : std::size_t(width) * samples * sizeof(double);
        check(output.working_bytes == output_bytes + strip_bytes + std::size_t(5 * 4 * samples) * sizeof(double),
              "Working-memory estimate must use actual double storage and one fallback XY plane");
        check(output.source_geometry.origin == actual.origin && output.request.coordinate == 2 &&
              output.request.t == 1 && output.request.c == 1,
              "Typed result must retain the exact source request and physical plane origin");
      }
  // A large origin must not cancel the small basis vectors in metadata.
  auto c = calibration(); c.origin = {1e100, -1e100, 1e100};
  const auto output = export_axis_plane(formula().with_calibration(c), {Plane::XZ, 2, 1, 1});
  check(output.source_geometry.horizontal_step == std::array<double, 3>{-.25, 0, .125} &&
        output.source_geometry.vertical_step == std::array<double, 3>{.75, 1.5, 3},
        "Basis vectors must be computed directly instead of subtracting enormous world origins");
  std::cout << "P05f.native.E01_axes_types_samples_calibration\n";
}
void quiet_nan_bits() {
  constexpr auto policy = FloatingSamplePolicy::PreserveQuietNaNFloat32;
  const std::array<std::uint32_t, 6> bits{0, 0x80000000u, 1, 0x7fc12345u, 0xffc34567u, 0x3f800000u};
  Stack images;
  for (int z = 0; z < 3; ++z) {
    Image image(5, 4, 1, PixelType::Float32);
    for (int i = 0; i < 20; ++i) image.pixels[i] = float32_source_sample(bits[(z * 20 + i) % bits.size()], policy);
    images.push_back(std::move(image));
  }
  const auto source = Dataset::from_planes({1, 1, 3, 4, 5, 1}, std::move(images), calibration(), policy);
  for (auto p : {Plane::XY, Plane::XZ, Plane::YZ}) {
    const auto output = export_axis_plane(source, {p, 2, 0, 0});
    check(output.dataset.sample_policy() == policy, "Quiet-NaN source policy was silently downgraded");
    const auto image = output.dataset.plane();
    for (int v = 0; v < image->height; ++v)
      for (int u = 0; u < image->width; ++u) {
        const auto xyz = source_voxel(p, 2, u, v);
        const auto expected = bits[(xyz[2] * 20 + xyz[1] * 5 + xyz[0]) % bits.size()];
        check(float32_source_sample_bits(image->at(u, v), policy) == expected,
              "Float32 quiet-NaN payload/subnormal/signed-zero bits changed during exact export");
      }
  }
  std::cout << "P05f.native.E02_policy_float_bits\n";
}
class CountingSource final : public PlaneSource {
public:
  DatasetShape sizes{2, 2, 3, 4, 5, 1};
  mutable std::size_t plane_reads = 0, region_reads = 0, region_samples = 0;
  bool regional_only = false;
  DatasetShape shape() const override { return sizes; }
  PixelType pixel_type() const override { return PixelType::Float64; }
  std::shared_ptr<const Image> read_plane(PlanePosition p) const override {
    ++plane_reads;
    if (regional_only) throw std::runtime_error("Full plane read violates the regional-only test source");
    auto image = std::make_shared<Image>(sizes.x, sizes.y, sizes.s, pixel_type());
    for (int y = 0; y < sizes.y; ++y)
      for (int x = 0; x < sizes.x; ++x)
        for (int s = 0; s < sizes.s; ++s) image->at(x, y, s) = value(p.t, p.c, p.z, y, x, s, pixel_type());
    return image;
  }
  Image read_region(PlanePosition p, int x, int y, int width, int height) const override {
    ++region_reads; region_samples += std::size_t(width) * height * sizes.s;
    Image image(width, height, sizes.s, pixel_type());
    for (int j = 0; j < height; ++j)
      for (int i = 0; i < width; ++i)
        for (int s = 0; s < sizes.s; ++s) image.at(i, j, s) = value(p.t, p.c, p.z, y + j, x + i, s, pixel_type());
    return image;
  }
};
void validation_and_regions() {
  auto source = std::make_shared<CountingSource>();
  Dataset dataset(source, calibration());
  for (auto p : {Plane::XY, Plane::XZ, Plane::YZ}) {
    const int limit = p == Plane::XY ? 3 : p == Plane::XZ ? 4 : 5;
    rejects<std::out_of_range>([&] { export_axis_plane(dataset, {p, -1, 0, 0}); }, "Negative coordinate was admitted");
    rejects<std::out_of_range>([&] { export_axis_plane(dataset, {p, limit, 0, 0}); }, "Past-end coordinate was admitted");
  }
  rejects<std::invalid_argument>([&] { export_axis_plane(dataset, {static_cast<Plane>(99), 0, 0, 0}); }, "Invalid plane enum was admitted");
  for (const auto request : {AxisPlaneExportRequest{Plane::XY, 0, -1, 0},
       AxisPlaneExportRequest{Plane::XY, 0, 2, 0}, AxisPlaneExportRequest{Plane::XY, 0, 0, -1},
       AxisPlaneExportRequest{Plane::XY, 0, 0, 2}, AxisPlaneExportRequest{Plane::XY, 0, 0, 0, 0}})
    rejects<std::exception>([&] { export_axis_plane(dataset, request); }, "Invalid scientific index/cap was admitted");
  rejects<std::invalid_argument>([&] { export_axis_plane(dataset, {Plane::XZ, 2, 1, 1, 319}); }, "Too-small working cap was admitted");
  check(!source->plane_reads && !source->region_reads, "Metadata validation/cap rejection read source pixels");
  auto huge = std::make_shared<CountingSource>();
  huge->sizes = {1, 1, 1, 1000000000, 1000000000, 4};
  rejects<std::invalid_argument>([&] {
    export_axis_plane(Dataset(huge), {Plane::XZ, 0, 0, 0, std::numeric_limits<std::size_t>::max()});
  }, "Working-byte multiplication overflow was admitted");
  check(!huge->plane_reads && !huge->region_reads, "Overflow admission requested a giant source allocation");
  auto overflowing = calibration(); overflowing.origin[2] = 1e308; overflowing.spacing[2] = 1e308;
  rejects<std::invalid_argument>([&] {
    export_axis_plane(dataset.with_calibration(overflowing), {Plane::XY, 2, 0, 0});
  }, "Nonfinite chosen world origin was admitted");
  check(!source->plane_reads && !source->region_reads, "World-coordinate rejection read source pixels");
  auto overflowing_time = calibration();
  overflowing_time.time_origin = 1e308; overflowing_time.frame_interval = 1e308;
  rejects<std::invalid_argument>([&] {
    export_axis_plane(dataset.with_calibration(overflowing_time), {Plane::XY, 0, 1, 0});
  }, "Nonfinite selected time origin was admitted");
  check(!source->plane_reads && !source->region_reads, "Time-coordinate rejection read source pixels");
  Image malformed_crop(3, 2, 3); malformed_crop.pixels.pop_back();
  rejects<std::invalid_argument>([&] { crop(malformed_crop, 1, 0, 2, 2); },
                                 "Crop accepted sample storage inconsistent with its dimensions");
  std::cout << "P05f.native.E03_validation_zero_reads_limits\n";
  source->regional_only = true;
  const auto xz = export_axis_plane(dataset, {Plane::XZ, 2, 1, 1, 320});
  check(source->plane_reads == 0 && source->region_reads == 3 && source->region_samples == 15,
        "XZ must request one bounded horizontal strip per contributing Z");
  check(xz.dataset.plane()->at(3, 2) == value(1, 1, 2, 2, 3, 0, PixelType::Float64), "Regional XZ values differ");
  const auto yz = export_axis_plane(dataset, {Plane::YZ, 3, 1, 1, 288});
  check(source->plane_reads == 0 && source->region_reads == 6 && source->region_samples == 27,
        "YZ must request one bounded vertical strip per contributing Z");
  check(yz.dataset.plane()->at(2, 2) == value(1, 1, 2, 2, 3, 0, PixelType::Float64), "Regional YZ values differ");
  std::cout << "P05f.native.E04_lazy_region_requests\n";
}
void request_callback_capture() {
  const auto dataset = formula();
  AxisPlaneExportRequest request{Plane::XY, 2, 1, 1, 4096};
  bool called = false;
  ExecutionControl control;
  control.checkpoint = [&] {
    called = true;
    request = {Plane::YZ, 0, 0, 0, 0};
  };
  const auto output = export_axis_plane(dataset, request, control);
  check(called && output.request.plane == Plane::XY && output.request.coordinate == 2 &&
        output.request.t == 1 && output.request.c == 1 && output.request.max_working_bytes == 4096 &&
        output.dataset.shape().dimensions() == std::array<int, 6>{1, 1, 1, 4, 5, 1} &&
        output.dataset.plane()->at(3, 2) == value(1, 1, 2, 2, 3, 0, PixelType::Float64),
        "A checkpoint callback retargeted the admitted pure export request");
  Application app;
  auto owner = std::make_shared<Document>(dataset, "Captured request"); app.adopt(owner);
  const auto source = owner->snapshot();
  request = {Plane::XZ, 2, 1, 1, 4096}; called = false;
  auto prepared = prepare_axis_plane_export(source, request, control);
  const auto candidate = prepared.prepared_new_document();
  const auto &parameters = prepared.parameters();
  const auto tree = std::get<std::shared_ptr<const MetadataTree>>(
      candidate->snapshot().data().metadata.extensions.at("axis_plane_export"));
  const auto &provenance = std::get<MetadataObject>(tree->value);
  check(called && std::get<std::string>(parameters.at("plane")) == "XZ" &&
        std::get<std::int64_t>(parameters.at("coordinate")) == 2 &&
        std::get<std::int64_t>(parameters.at("t")) == 1 && std::get<std::int64_t>(parameters.at("c")) == 1 &&
        std::get<std::uint64_t>(parameters.at("max_working_bytes")) == 4096 &&
        std::get<std::string>(provenance.at("plane")) == "XZ" &&
        std::get<std::int64_t>(provenance.at("coordinate")) == 2 &&
        std::get<std::int64_t>(provenance.at("t")) == 1 && std::get<std::int64_t>(provenance.at("c")) == 1 &&
        prepared.target().position_count() == 3,
        "A checkpoint callback changed captured target parameters or typed provenance");
  const auto published = app.publish(prepared);
  const auto actual = published.document->snapshot();
  check(actual.data().metadata.title == "Captured request XZ 3" &&
        actual.data().dataset.shape().dimensions() == std::array<int, 6>{1, 1, 1, 3, 5, 1} &&
        actual.data().dataset.plane()->at(3, 2) == value(1, 1, 2, 2, 3, 0, PixelType::Float64) &&
        actual.data().dataset.calibration().origin == dataset.calibration().voxel_to_world({0, 2, 0}) &&
        owner->snapshot().shares_data(source),
        "A callback mutation changed prepared pixels/calibration/title or the original source");
}
void cancellation() {
  Application app;
  auto source = std::make_shared<CountingSource>(); source->sizes = {1, 1, 3, 3, 5000, 1};
  auto owner = std::make_shared<Document>(Dataset(source)); app.adopt(owner);
  const auto before = owner->snapshot();
  auto token = std::make_shared<CancellationToken>(); token->cancel();
  rejects<OperationCancelled>([&] { app.prepare_axis_plane_export({Plane::XY, 0}, owner->id, {token}); },
                             "Pre-canceled export was admitted");
  check(!source->plane_reads && !source->region_reads, "Pre-cancellation read pixels");
  for (auto plane : {Plane::XY, Plane::XZ, Plane::YZ}) {
    Application local_app;
    auto local_source = std::make_shared<CountingSource>();
    local_source->sizes = plane == Plane::YZ ? DatasetShape{1, 1, 3, 5000, 3, 1}
                                           : DatasetShape{1, 1, 3, 3, 5000, 1};
    auto local_owner = std::make_shared<Document>(Dataset(local_source));
    local_app.adopt(local_owner);
    const auto local_before = local_owner->snapshot();
    token = std::make_shared<CancellationToken>(); bool positive = false;
    ExecutionControl control; control.cancellation = token;
    control.stage_progress = [&](std::string_view stage, std::size_t done, std::size_t) {
      if (stage == "plane-extract.copy" && done >= 1024) { positive = true; token->cancel(); }
    };
    rejects<OperationCancelled>([&] { local_app.prepare_axis_plane_export({plane, 1}, local_owner->id, control); },
                               "Positive-copy cancellation did not abort preparation");
    check(positive && local_owner->snapshot().shares_data(local_before) && local_app.document_ids().size() == 1 &&
          local_owner->history().undo_steps == 0,
          "Canceled export changed source/history or adopted a partial output");
  }
  // Default PlaneSource::read_region uses crop; direct controlled crops also
  // require bounded cancellation checkpoints without a new sampling algorithm.
  auto token2 = std::make_shared<CancellationToken>();
  ExecutionControl control; control.cancellation = token2; bool positive_crop = false;
  control.stage_progress = [&](std::string_view stage, std::size_t done, std::size_t) {
    if (stage == "crop.copy" && done >= 1024) { positive_crop = true; token2->cancel(); }
  };
  // Crop is exercised inside the existing extraction fallback separately from
  // the regional-only provider above.
  class Fallback final : public PlaneSource {
  public:
    DatasetShape shape() const override { return {1, 1, 2, 3, 5000, 1}; }
    PixelType pixel_type() const override { return PixelType::Float64; }
    std::shared_ptr<const Image> read_plane(PlanePosition) const override { return std::make_shared<Image>(5000, 3); }
  };
  rejects<OperationCancelled>([&] { export_axis_plane(Dataset(std::make_shared<Fallback>()), {Plane::XZ, 1}, control); },
                             "Default region crop ignored positive-work cancellation");
  check(positive_crop, "Crop cancellation was not observed after positive copied samples");
  check(owner->snapshot().shares_data(before), "Canceled extraction changed the initial pre-cancellation source");
  request_callback_capture();
  std::cout << "P05f.native.E05_cancellation_atomicity\n";
}
std::shared_ptr<Document> annotated_owner() {
  DocumentData data{formula(), {"Source", {{"kept property", std::int64_t(7)}}}, {}, {}};
  data.metadata.extensions["opaque"] = std::string("retained source extension");
  data.metadata.record_layout = {{"presence", true}};
  data.selection = Selection("rectangle", {{1, 1}, {4, 3}});
  data.previous_selection = Selection("line", {{0, 0}, {2, 2}});
  data.rois = {{"saved", Selection("rectangle", {{1, 1}, {3, 3}}), 1, 1, 1}};
  data.overlays = {{{"kept overlay", std::string("original")}}};
  data.spectrum = FourierState(Spectrum{5, 4, std::vector<std::complex<double>>(20)}, std::array<int, 2>{4, 5});
  auto owner = std::make_shared<Document>(std::move(data));
  auto edit = owner->begin_edit(); edit.set_property("history entry", true); owner->commit(edit); owner->undo();
  return owner;
}
void publication() {
  Application app; auto owner = annotated_owner(); app.adopt(owner);
  const auto original = owner->snapshot(); const auto history = owner->history();
  auto prepared = app.prepare_axis_plane_export({Plane::XZ, 2, 1, 1}, owner->id);
  check(prepared.target().source().same_document(original) &&
        prepared.target().dependencies() == (Change::Pixels | Change::Metadata) &&
        prepared.target().position_count() == 3,
        "Plane preparation must bind captured owner/T/C/all contributing Z and omit selection dependencies");
  auto candidate = prepared.prepared_new_document();
  check(candidate && candidate->id != owner->id && !app.contains(candidate->id),
        "Candidate must be the exact new unregistered native owner");
  const auto output = candidate->snapshot(); const auto &data = output.data();
  check(!output.dirty() && candidate->history().undo_steps == 0 && candidate->history().redo_steps == 0,
        "Fresh candidate must retain ordinary-constructor dirty/history semantics");
  check(data.metadata.title == "Source XZ 3" && data.metadata.properties.empty() &&
        data.metadata.record_layout.empty() && data.metadata.extensions.size() == 1 &&
        data.metadata.extensions.count("axis_plane_export") == 1 && !data.selection &&
        !data.previous_selection && data.rois.empty() && data.overlays.empty() && !data.spectrum,
        "Fresh output must clear source annotations/properties/spectrum and retain dedicated typed provenance only");
  auto display = output.display(); display.zoom = 2; display.planeZoom = {2, 2, 2};
  candidate->set_display(display); candidate->set_history_policy(3, 4096);
  const auto unrelated = app.create(Dataset::from_stack(Stack{Image(2, 2)}), "Other");
  const auto other = app.snapshot(unrelated);
  const auto published = app.publish(prepared, "Source XZ 3");
  check(published.created && published.document == candidate && published.document_id == candidate->id &&
        app.active_document_id() == candidate->id && !prepared.active(),
        "Publication must adopt the staged exact owner once and activate the new tab");
  check(published.document->snapshot().display().zoom == 2 && published.document->history().undo_steps == 0,
        "Valid staged display/history-policy configuration must survive publication");
  check(owner->snapshot().shares_data(original) && owner->snapshot().revisions().matches(original.revisions(), Change::All) &&
        owner->snapshot().dirty() == original.dirty() && owner->history().undo_steps == history.undo_steps &&
        owner->history().redo_steps == history.redo_steps && app.snapshot(unrelated).shares_data(other),
        "Fresh publication changed the source/another active owner or source history");
  rejects<std::invalid_argument>([&] { app.publish(prepared); }, "Plane export was published twice");
  std::cout << "P05f.native.E06_publication_fresh_candidate_owner\n";
}
void concurrent_publication_rejection(bool change_source) {
  using namespace std::chrono_literals;
  Application app; auto owner = annotated_owner(); app.adopt(owner);
  auto prepared = app.prepare_axis_plane_export({Plane::XY, 1, 1, 1}, owner->id);
  auto candidate = prepared.prepared_new_document();
  auto display = candidate->snapshot().display(); display.zoom = 2; display.planeZoom = {2, 2, 2};
  auto candidate_edit = candidate->begin_edit(); candidate_edit.set_property("racing candidate", true);
  auto source_edit = owner->begin_edit(); source_edit.set_property("racing source", true);
  struct Gate {
    std::mutex mutex;
    std::condition_variable changed;
    bool armed = false, entered = false, released = false;
  } gate;
  // The validator is a public, retained lease and calls neither document nor
  // controller. The writer holds the candidate mutex while this gate waits.
  const auto lease = candidate->constrain([&](const DocumentData &, const DisplayState &) {
    std::unique_lock<std::mutex> lock(gate.mutex);
    if (!gate.armed) return;
    gate.entered = true; gate.changed.notify_all();
    if (!gate.changed.wait_for(lock, 2s, [&] { return gate.released; }))
      throw std::runtime_error("Candidate publication gate exceeded its bounded wait");
  });
  { std::lock_guard<std::mutex> lock(gate.mutex); gate.armed = true; }
  auto writer = std::async(std::launch::async, [&] {
    if (change_source) candidate->set_display(display);
    else candidate->commit(candidate_edit);
  });
  bool entered;
  {
    std::unique_lock<std::mutex> lock(gate.mutex);
    entered = gate.changed.wait_for(lock, 1s, [&] { return gate.entered; });
  }
  if (!entered) {
    { std::lock_guard<std::mutex> lock(gate.mutex); gate.released = true; }
    gate.changed.notify_all(); writer.get();
    check(false, "Concurrent writer never acquired the candidate guard");
  }
  std::promise<void> publishing_started;
  auto started = publishing_started.get_future();
  auto publishing = std::async(std::launch::async, [&] {
    publishing_started.set_value();
    return app.publish(prepared);
  });
  const bool started_in_time = started.wait_for(1s) == std::future_status::ready;
  const bool waited_on_candidate = publishing.wait_for(50ms) == std::future_status::timeout;
  bool source_changed_before_release = true;
  std::future<void> source_writer;
  if (change_source) {
    // Publication is queued behind a valid candidate display writer. A direct
    // source commit bypasses the Application lock and must invalidate the
    // source again when both document guards have finally been acquired.
    source_writer = std::async(std::launch::async, [&] { owner->commit(source_edit); });
    source_changed_before_release = source_writer.wait_for(1s) == std::future_status::ready;
  }
  { std::lock_guard<std::mutex> lock(gate.mutex); gate.released = true; }
  gate.changed.notify_all();
  writer.get();
  if (source_writer.valid()) source_writer.get();
  check(started_in_time && waited_on_candidate && source_changed_before_release,
        "Concurrent stale-publication case did not establish its bounded writer schedule");
  check(publishing.wait_for(1s) == std::future_status::ready,
        "Publication failed to complete after the candidate writer released its guard");
  rejects<RevisionConflict>([&] { publishing.get(); },
                            "Concurrent source/candidate Data changes were adopted after stale publication checks");
  check(prepared.active() && !app.contains(candidate->id) && app.document_ids().size() == 1 &&
        app.active_document_id() == owner->id &&
        (change_source ? candidate->snapshot().display().zoom == 2 :
                        candidate->snapshot().data().metadata.properties.count("racing candidate") == 1),
        "Concurrent rejection consumed/adopted output or discarded the legitimate writer change");
}
void publication_rejections() {
  for (int scenario = 0; scenario < 9; ++scenario) {
    Application app; auto owner = annotated_owner(); app.adopt(owner);
    auto token = std::make_shared<CancellationToken>();
    auto prepared = app.prepare_axis_plane_export({Plane::XY, 1, 1, 1}, owner->id, {token});
    auto candidate = prepared.prepared_new_document();
    if (scenario == 0) {
      auto edit = owner->begin_edit(); edit.set_property("new metadata", true); owner->commit(edit);
    } else if (scenario == 1) {
      auto edit = owner->begin_edit(); edit.replace_plane({1, 1, 1}, Image(5, 4)); owner->commit(edit);
    } else if (scenario == 2) {
      app.close(owner->id, true);
    } else if (scenario == 3) {
      token->cancel();
    } else if (scenario == 4) {
      auto edit = candidate->begin_edit(); edit.set_title("Changed output"); candidate->commit(edit);
    } else if (scenario == 5) {
      auto edit = candidate->begin_edit(); edit.select(Selection("point", {{1, 1}})); candidate->commit(edit);
    } else if (scenario == 6) {
      auto edit = candidate->begin_edit(); edit.set_property("output change", true);
      edit.replace_plane({}, Image(5, 4)); candidate->commit(edit);
      candidate->undo(); candidate->set_history_policy(1, 1);
      check(!candidate->history().undo_steps && !candidate->history().redo_steps,
            "Valid tiny pixel budget failed to prune obsolete changed-candidate history");
    } else if (scenario == 7) {
      auto edit = candidate->begin_edit(); edit.replace_plane({}, Image(5, 4)); candidate->commit(edit);
    } else {
      candidate->checkpoint();
    }
    const auto before = owner->snapshot(); const auto ids = app.document_ids();
    if (scenario == 2) rejects<DocumentUnavailable>([&] { app.publish(prepared); }, "Closed source output was adopted");
    else if (scenario == 3) rejects<OperationCancelled>([&] { app.publish(prepared); }, "Canceled prepared output was adopted");
    else rejects<RevisionConflict>([&] { app.publish(prepared); }, "Stale source/modified output candidate was adopted");
    check(prepared.active() && app.document_ids() == ids && !app.contains(candidate->id) && owner->snapshot().shares_data(before),
          "Failed publication consumed preparation, adopted output or changed source");
  }
  Application app; auto owner = annotated_owner(); app.adopt(owner);
  auto prepared = app.prepare_axis_plane_export({Plane::XY, 1, 1, 1}, owner->id);
  Application foreign; foreign.create(Dataset::from_stack(Stack{Image(2, 2)}));
  rejects<DocumentUnavailable>([&] { foreign.publish(prepared); }, "Another application's owner was implicitly rebound");
  rejects<std::invalid_argument>([&] { app.publish(prepared, "Different title"); }, "A differing title modified the staged output");
  check(prepared.active() && !app.contains(prepared.prepared_new_document()->id), "Title rejection adopted/consumed output");
  // Pure display/selection changes do not alter frozen coordinates or affect
  // the output, which deliberately clears source selections.
  auto display = owner->snapshot().display(); display.t = 0; display.c = 0; display.crosshair = {4, 3, 2};
  owner->set_display(display); owner->select(Selection("point", {{2, 2}}));
  const auto before = owner->snapshot();
  auto published = app.publish(prepared);
  check(published.document->snapshot().data().dataset.plane()->at(3, 2) == value(1, 1, 1, 2, 3, 0, PixelType::Float64) &&
        owner->snapshot().shares_data(before), "Display/selection change retargeted the captured exact plane");
  for (int repeat = 0; repeat < 4; ++repeat) concurrent_publication_rejection(true);
  concurrent_publication_rejection(false);
  std::cout << "P05f.native.E07_publication_rejections\n";
}
void retained_generic_publication() {
  Application app;
  auto owner = std::make_shared<Document>(Dataset::from_stack(Stack{Image(3, 2, 1, PixelType::UInt16)}));
  app.adopt(owner);
  TargetRequest request; request.destination = OutputDestination::NewDocument;
  auto prepared = app.prepare("add", app.resolve_target(request), {{"value", 8}});
  check(!prepared.prepared_new_document(), "Generic operations unexpectedly acquired an axis-export candidate");
  const auto output = app.publish(prepared, "Generic custom title");
  check(output.document->snapshot().data().metadata.title == "Generic custom title" && output.document->history().undo_steps == 1 &&
        output.document->snapshot().data().dataset.plane()->at(0, 0) == 8 && owner->snapshot().data().dataset.plane()->at(0, 0) == 0,
        "Existing generic new-document title/history1/source-preservation behavior changed");
  std::cout << "P05f.native.E08_generic_new_document_unchanged\n";
}
}
int main() {
  try {
    axes_types_samples_calibration(); quiet_nan_bits(); validation_and_regions();
    cancellation(); publication(); publication_rejections(); retained_generic_publication();
    return 0;
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n'; return 1;
  }
}
