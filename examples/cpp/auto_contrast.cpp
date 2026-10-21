// Installed SDK consumer: public imagejxx::core only; no GUI/Python/JSON headers.
#include <imagejxx/application.hpp>
#include <imagejxx/session.hpp>

#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
using namespace imagejxx;
std::size_t checks = 0, cases = 0;
const char *active = "setup";
void require(bool condition, const char *message) {
  if (!condition) throw std::runtime_error(message);
  ++checks;
}
template<class Error = std::invalid_argument, class F> void rejects(F action) {
  bool caught = false;
  try { action(); } catch (const Error &) { caught = true; }
  require(caught, "Expected SDK refusal was absent");
}
template<class F> void run(const char *name, F action) {
  active = name;
  action();
  ++cases;
  std::cout << name << " checked\n";
}
std::uint64_t bits(double value) {
  std::uint64_t result;
  std::memcpy(&result, &value, sizeof(result));
  return result;
}
double quiet_sample() {
  // Exact widened IEEE754 Float32 quiet NaN 0x7fc12345.
  const std::uint64_t raw = 0x7ff82468a0000000ULL;
  double result;
  std::memcpy(&result, &raw, sizeof(result));
  return result;
}
SpatialCalibration calibration() {
  SpatialCalibration result;
  result.spacing = {.5, 1.25, 2.5}; result.origin = {11, -7, 19};
  result.direction = {-1, .25, .5, 0, 1, .5, .5, 0, 1};
  result.unit = "um"; result.time_unit = "s";
  result.frame_interval = .75; result.time_origin = 2.25;
  return result;
}
Image plane(int t, int c, int z, PixelType type, int offset = 0,
            bool quiet = false) {
  Image result(6, 4, 1, type);
  for (int y = 0; y != 4; ++y) for (int x = 0; x != 6; ++x)
    result.at(x, y) = offset + 40*t + 30*c + 5*z + 10*y + x;
  if (quiet && t == 1 && c == 1 && z == 1) {
    result.at(2, 1) = quiet_sample(); result.at(4, 3) = -0.0;
  }
  return result;
}
Dataset dataset(PixelType type, int offset = 0, bool quiet = false) {
  Stack planes;
  for (int t = 0; t != 2; ++t) for (int c = 0; c != 3; ++c)
    for (int z = 0; z != 2; ++z)
      planes.push_back(plane(t, c, z, type, offset, quiet));
  return Dataset::from_planes({2, 3, 2, 4, 6, 1}, std::move(planes), calibration(),
      quiet ? FloatingSamplePolicy::PreserveQuietNaNFloat32
            : FloatingSamplePolicy::FiniteOnly);
}
Selection area() {
  Selection result("rectangle", {{1, 1}, {6, 4}});
  result.attributes["sdk"] = std::string("retained active ROI");
  return result;
}
ChannelDisplayState initial_state() {
  ChannelDisplayState result;
  result.version = ChannelDisplayVersion::V2;
  result.projection = ChannelDisplayProjection::Min;
  const std::array<const char *, 3> names{"Red", "Green", "Blue"};
  for (int c = 0; c != 3; ++c) {
    ChannelDisplayChannel channel;
    channel.visible = c != 1;
    channel.lut.name = names[c];
    result.channels.push_back(channel);
  }
  return result;
}
std::shared_ptr<Document> owner(Dataset source) {
  DocumentData data{std::move(source)};
  data.metadata.title = "Auto SDK";
  data.metadata.properties["sdk"] = std::string("raw domain");
  data.metadata.extensions["sdk"] = metadata_array({true, std::int64_t(7)});
  data.selection = area();
  data.previous_selection = Selection("ellipse", {{0, 0}, {3, 2}});
  data.rois.push_back({"positioned", Selection("rectangle", {{0, 0}, {2, 2}}),
                      1, 1, 1, {{"sdk", std::string("saved")}}});
  data.overlays.push_back({{"sdk", std::string("opaque retained overlay")}});
  DisplayState display;
  display.t = 1; display.c = 1; display.crosshair = {2, 1, 1};
  display.zoom = 2.5; display.orthogonal = true; display.linkedZoom = false;
  display.planeZoom = {1.5, 2, 3}; display.pan = {{{1, 2}, {3, 4}, {5, 6}}};
  display.properties["sdk"] = std::string("retained presentation");
  display = with_channel_display(display, data.dataset, initial_state());
  return std::make_shared<Document>(std::move(data), std::move(display));
}
ChannelDisplayState state(const DocumentSnapshot &source) {
  const auto result = recognized_channel_display(source.display(), source.data().dataset);
  require(result.has_value(), "SDK channel state missing");
  return *result;
}
void same_calibration(const SpatialCalibration &a, const SpatialCalibration &b) {
  require(a.spacing == b.spacing && a.origin == b.origin && a.direction == b.direction &&
          a.unit == b.unit && a.time_unit == b.time_unit &&
          a.frame_interval == b.frame_interval && a.time_origin == b.time_origin,
          "Auto changed calibration");
}
void same_data(const DocumentSnapshot &before, const DocumentSnapshot &after) {
  require(before.same_document(after) && before.shares_data(after) &&
          before.data_identity() == after.data_identity() && before.dirty() == after.dirty(),
          "Auto changed source owner/data/save identity");
  const auto a = before.revisions(), b = after.revisions();
  require(a.pixels == b.pixels && a.metadata == b.metadata && a.selection == b.selection,
          "Auto changed data revisions");
  bool same_overlays = before.data().overlays.size() == after.data().overlays.size();
  for (std::size_t i = 0; same_overlays && i != before.data().overlays.size(); ++i)
    same_overlays = metadata_equal(before.data().overlays[i], after.data().overlays[i]);
  require(before.data().selection == after.data().selection &&
          before.data().previous_selection == after.data().previous_selection &&
          before.data().rois == after.data().rois &&
          before.data().metadata == after.data().metadata && same_overlays,
          "Auto changed annotations/metadata");
  same_calibration(before.data().dataset.calibration(), after.data().dataset.calibration());
}
void atomic_unchanged(const DocumentSnapshot &before, const DocumentSnapshot &after) {
  same_data(before, after);
  require(before.revisions().matches(after.revisions(), Change::All) &&
          before.display().revision == after.display().revision &&
          metadata_equal(before.display().properties, after.display().properties) &&
          before.display().t == after.display().t && before.display().c == after.display().c &&
          before.display().crosshair == after.display().crosshair,
          "Refused Auto changed presentation");
}
void literal(const ResolvedAutoContrastCommand &command, std::array<int, 2> indices,
             std::array<double, 2> resolved, std::array<double, 2> applied) {
  require(command.position().t == 1 && command.position().c == 1 && command.position().z == 1,
          "Auto did not capture nonzero TCZ");
  require(command.indices() == std::optional<std::array<int, 2>>(indices) &&
          command.resolved_range() == resolved && command.applied_range() == applied,
          "Auto independent literal indices/ranges differ");
  require(command.cycle().previous == 0 && command.cycle().used == 5000 &&
          command.cycle().next == 5000 && command.outcome() == AutoContrastOutcome::HistogramBounds,
          "Auto first cycle/outcome differs");
  require(command.selection_provenance() == std::optional<Selection>(area()) &&
          command.histogram().visited_count == 15 && command.histogram().finite_count == 15 &&
          command.histogram().minimum == 86 && command.histogram().maximum == 110 &&
          command.histogram().plane_minimum == 75 && command.histogram().plane_maximum == 110 &&
          command.histogram().plane_finite_count == 24,
          "Auto ROI and whole-plane observations conflated");
}
void ready(AutoContrastJob &job) {
  if (!job.wait_for(std::chrono::seconds(5))) {
    job.cancel(); throw std::runtime_error("Auto SDK job exceeded 5 seconds");
  }
  (void)job.resolved(); // Preserve a provider/worker exception rather than relabeling it.
  require(job.info().state == JobState::Ready && job.ready(), "Auto SDK result not READY");
}
class CountingSource final : public PlaneSource {
public:
  mutable std::atomic<int> reads{0}, regions{0};
  std::atomic<bool> forbid{false};
  CountingSource() {
    for (int t = 0; t != 2; ++t) for (int c = 0; c != 3; ++c)
      for (int z = 0; z != 2; ++z)
        planes_.push_back(std::make_shared<const Image>(plane(t, c, z, PixelType::UInt16)));
  }
  DatasetShape shape() const override { return {2, 3, 2, 4, 6, 1}; }
  PixelType pixel_type() const override { return PixelType::UInt16; }
  std::shared_ptr<const Image> read_plane(PlanePosition p) const override {
    ++reads;
    if (forbid.load()) throw std::runtime_error("Replay unexpectedly read source pixels");
    if (p.t < 0 || p.t >= 2 || p.c < 0 || p.c >= 3 || p.z < 0 || p.z >= 2)
      throw std::out_of_range("SDK provider position");
    return planes_.at(static_cast<std::size_t>((p.t * 3 + p.c) * 2 + p.z));
  }
  Image read_region(PlanePosition, int, int, int, int) const override {
    ++regions; throw std::runtime_error("Auto unexpectedly requested a source region");
  }
private:
  std::vector<std::shared_ptr<const Image>> planes_;
};

void scalar_u8() {
  const auto document = owner(dataset(PixelType::UInt8)); const auto before = document->snapshot();
  const auto command = resolve_auto_contrast_command(before);
  literal(command, {86, 110}, {86, 110}, {86, 110});
  require(command.channels() == std::vector<int>{1}, "Active U8 Auto retargeted channels");
  atomic_unchanged(before, document->snapshot());
}
void scalar_u16() {
  const auto document = owner(dataset(PixelType::UInt16)); const auto before = document->snapshot();
  AutoContrastRequest request; request.scope = AutoContrastScope::CompositeAll;
  const auto command = resolve_auto_contrast_command(before, request);
  // 25/256-wide raw U16 bins: last sample110 lies in bin245, getter rounds109.92578125 to110.
  literal(command, {0, 245}, {86, 109.92578125}, {86, 110});
  require(command.histogram().origin == 86 && command.histogram().bin_size == .09765625 &&
          command.channels() == std::vector<int>({0, 1, 2}), "U16 binning/all-C scope differs");
  const auto proposed = before.with_display(command_display_state(command, before));
  const auto actual = state(proposed), original = state(before);
  require(actual.mode == original.mode && actual.version == ChannelDisplayVersion::V2 &&
          actual.projection == ChannelDisplayProjection::Min, "Auto changed display mode/version/projection");
  for (int c = 0; c != 3; ++c)
    require(actual.channels[c].range == std::array<double, 2>{86, 110} &&
            actual.channels[c].visible == original.channels[c].visible &&
            actual.channels[c].lut.name == original.channels[c].lut.name,
            "Auto all-C propagation changed LUT/visibility");
  atomic_unchanged(before, document->snapshot());
}
void scalar_f32() {
  const auto document = owner(dataset(PixelType::Float32));
  const auto command = resolve_auto_contrast_command(document->snapshot());
  literal(command, {0, 255}, {86, 109.90625}, {86, 109.90625});
  require(command.histogram().origin == 86 && command.histogram().bin_size == .09375,
          "F32 endpoint arithmetic differs from adopted raw bins");
}
void fallback() {
  const auto document = owner(dataset(PixelType::UInt16));
  document->select(Selection("rectangle", {{2, 1}, {4, 3}}));
  const auto before = document->snapshot();
  const auto observed = resolve_auto_contrast_command(before);
  require(observed.outcome() == AutoContrastOutcome::ResetFallback && !observed.indices() &&
          !observed.candidate_range() && observed.resolved_range() == std::array<double, 2>{75, 110} &&
          observed.histogram().minimum == 87 && observed.histogram().maximum == 98 &&
          observed.histogram().finite_count == 4 && observed.cycle().used == 5000 &&
          observed.cycle().next == 0, "Tiny ROI fallback did not reuse whole-plane extrema");
  AutoContrastRequest request; request.unsigned16_bits = 12;
  const auto configured = resolve_auto_contrast_command(before, request);
  require(configured.resolved_range() == std::array<double, 2>{0, 4095} &&
          configured.applied_range() == std::array<double, 2>{0, 4095} &&
          configured.channels() == std::vector<int>({0, 1, 2}),
          "Configured U16 fallback must update all C even for ActiveChannel");
  atomic_unchanged(before, document->snapshot());
  // One eligible bin must expand to area extrema, rather than taking ResetFallback.
  Image rare(6, 4, 1, PixelType::UInt8);
  for (auto &value : rare.pixels) value = 50;
  rare.at(1, 1) = 80; rare.at(2, 1) = 80;
  const auto equal = owner(dataset(PixelType::UInt8).with_plane({1, 1, 1}, std::move(rare)));
  AutoContrastRequest whole; whole.roi = AutoContrastRoiPolicy::WholePlane;
  const auto extrema = resolve_auto_contrast_command(equal->snapshot(), whole);
  require(extrema.outcome() == AutoContrastOutcome::AreaExtrema &&
          extrema.indices() == std::optional<std::array<int, 2>>(std::array<int, 2>{80, 80}) &&
          extrema.candidate_range() == std::optional<std::array<double, 2>>(std::array<double, 2>{80, 80}) &&
          extrema.resolved_range() == std::array<double, 2>{50, 80} && extrema.cycle().next == 5000,
          "Equal eligible candidate must retain cycle and expand to area extrema");
}
void quiet_and_empty() {
  const auto document = owner(dataset(PixelType::Float32, 0, true));
  const auto before = document->snapshot();
  const auto command = resolve_auto_contrast_command(before);
  require(command.histogram().nan_count == 1 && command.histogram().finite_count == 14 &&
          command.histogram().plane_nan_count == 1 && command.histogram().plane_finite_count == 23 &&
          command.indices() == std::optional<std::array<int, 2>>(std::array<int, 2>{0, 255}) &&
          command.resolved_range() == std::array<double, 2>{0, 109.5703125},
          "Quiet NaN exclusion/signed-zero Auto literal differs");
  const auto raw = document->snapshot().data().dataset.plane({1, 1, 1});
  require(bits(raw->at(2, 1)) == 0x7ff82468a0000000ULL &&
          bits(raw->at(4, 3)) == 0x8000000000000000ULL, "Auto changed source NaN/signed-zero bits");
  document->select(Selection::from_packed_mask(6, 4, {0, 0, 0}));
  const auto empty = document->snapshot();
  rejects([&] { (void)resolve_auto_contrast_command(empty); });
  atomic_unchanged(empty, document->snapshot());
}
void provider_and_replay() {
  const auto provider = std::make_shared<CountingSource>();
  const auto document = owner(Dataset(provider, calibration()));
  Application app; app.adopt(document);
  const auto command = resolve_auto_contrast_command(document->snapshot());
  require(provider->reads == 1 && provider->regions == 0, "Auto must acquire exactly one TCZ plane");
  provider->forbid = true;
  auto display = document->snapshot().display();
  display.t = 0; display.c = 2; display.crosshair = {5, 3, 0}; display.zoom = 4;
  document->set_display(display); document->select(Selection("rectangle", {{0, 0}, {2, 2}}));
  const auto replay = auto_contrast_command_from_record(document->snapshot(), command.record());
  require(!replay.requires_captured_state() && metadata_equal(replay.record(), command.record()),
          "Replay changed historical record or required historical display/selection");
  const auto applied = app.replay_auto_contrast(command.record());
  const auto after = document->snapshot();
  require(provider->reads == 1 && provider->regions == 0 && app.auto_contrast_divisor() == 0 &&
          after.display().t == 0 && after.display().c == 2 && after.display().crosshair == display.crosshair &&
          after.display().zoom == 4 && metadata_equal(applied.record(), command.record()) &&
          state(after).channels[1].range == std::array<double, 2>{86, 109.92578125},
          "Strict replay read/recomputed pixels, advanced cycle or retargeted navigation");
}
void explicit_rebind() {
  const auto original = owner(dataset(PixelType::UInt16));
  const auto command = resolve_auto_contrast_command(original->snapshot());
  const auto destination = owner(dataset(PixelType::Float32, 20));
  auto display = destination->snapshot().display();
  display.t = 0; display.c = 2; display.crosshair[2] = 0; destination->set_display(display);
  AutoContrastRequest request; request.previous_divisor = 2500;
  request.roi = AutoContrastRoiPolicy::WholePlane; request.scope = AutoContrastScope::CompositeAll;
  const auto rebound = rebind_auto_contrast_command(destination->snapshot(), command.record(), request);
  require(rebound.source().id() == destination->id && rebound.position().t == 0 &&
          rebound.position().c == 2 && rebound.position().z == 0 &&
          rebound.request().roi == AutoContrastRoiPolicy::WholePlane &&
          rebound.cycle().previous == 2500 && rebound.cycle().used == 1250 &&
          rebound.histogram().finite_count == 24 &&
          rebound.resolved_range() == std::array<double, 2>{80, 114.86328125} &&
          rebound.channels() == std::vector<int>({0, 1, 2}),
          "Explicit rebind borrowed original target/request/range");
  auto malformed = command.record(); malformed["version"] = true;
  const auto before = destination->snapshot();
  rejects([&] { validate_auto_contrast_command_record(malformed); });
  rejects([&] { (void)auto_contrast_command_from_record(before, malformed); });
  rejects([&] { (void)rebind_auto_contrast_command(before, malformed, request); });
  rejects([&] { (void)auto_contrast_command_from_record(before, command.record()); });
  atomic_unchanged(before, destination->snapshot());
  Application app; app.adopt(destination);
  request.previous_divisor = 0; // The controller, not the old record, owns the live cycle.
  auto job = app.submit_rebound_auto_contrast(command.record(), request); ready(job);
  const auto published = app.publish_auto_contrast(job);
  require(published.source().id() == destination->id && published.cycle().previous == 0 &&
          app.auto_contrast_divisor() == 5000, "Application explicit rebound lane differs");
}
void application_cycles() {
  const auto document = owner(dataset(PixelType::UInt16));
  auto edit = document->begin_edit(); edit.set_property("temporary", true); document->commit(edit);
  document->undo(); document->mark_saved();
  Application app; app.adopt(document);
  const auto before = document->snapshot();
  const auto undo = document->undo_steps(), redo = document->redo_steps();
  auto first = app.submit_auto_contrast(); ready(first);
  atomic_unchanged(before, document->snapshot());
  require(app.auto_contrast_divisor() == 0 && redo == 1, "Submission advanced cycle or lost redo setup");
  const auto a = app.publish_auto_contrast(first);
  const auto once = document->snapshot();
  require(first.info().state == JobState::Completed && a.cycle().previous == 0 &&
          app.auto_contrast_divisor() == 5000 && once.revisions().display == before.revisions().display + 1,
          "Typed Auto first publication/cycle differs");
  auto second = app.submit_auto_contrast(); ready(second);
  const auto b = app.publish_auto_contrast(second);
  const auto twice = document->snapshot();
  require(b.cycle().previous == 5000 && b.cycle().used == 2500 && b.cycle().next == 2500 &&
          app.auto_contrast_divisor() == 2500 && twice.revisions().display == once.revisions().display + 1 &&
          document->undo_steps() == undo && document->redo_steps() == redo,
          "Equal repeated Auto must publish Display once without pixel undo/redo");
  same_data(before, twice);
  require(twice.display().zoom == before.display().zoom && twice.display().pan == before.display().pan &&
          twice.display().planeZoom == before.display().planeZoom &&
          twice.display().crosshair == before.display().crosshair &&
          std::get<std::string>(twice.display().properties.at("sdk")) == "retained presentation",
          "Auto changed camera/unrelated display properties");
}
void stale_and_activation() {
  const auto document = owner(dataset(PixelType::UInt16));
  Application app; app.adopt(document);
  auto stale = app.submit_auto_contrast(); ready(stale);
  document->select(Selection("rectangle", {{0, 0}, {3, 3}}));
  const auto changed = document->snapshot();
  rejects<RevisionConflict>([&] { (void)app.publish_auto_contrast(stale); });
  atomic_unchanged(changed, document->snapshot());
  require(app.auto_contrast_divisor() == 0, "Stale selection advanced Auto cycle");
  auto away = app.submit_auto_contrast(); ready(away);
  const auto other = owner(dataset(PixelType::UInt8)); app.adopt(other, false);
  app.activate(other->id); app.activate(document->id);
  const auto returned = document->snapshot();
  rejects<RevisionConflict>([&] { (void)app.publish_auto_contrast(away); });
  atomic_unchanged(returned, document->snapshot());
  require(app.active_document_id() == document->id && app.auto_contrast_divisor() == 0,
          "Owner away/back must invalidate captured activation even with same final ID");
}
void cancellation_and_reset() {
  const auto document = owner(dataset(PixelType::UInt16));
  Application app; app.adopt(document);
  const auto before = document->snapshot();
  auto canceled = app.submit_auto_contrast(); ready(canceled);
  std::optional<ResolvedAutoContrastCommand> alias(canceled.resolved());
  require(app.job_scheduler()->usage().retained_bytes > 0, "READY Auto has no retained reservation");
  require(canceled.cancel(), "READY Auto cancellation failed");
  rejects<OperationCancelled>([&] { (void)canceled.resolved(); });
  rejects<OperationCancelled>([&] { (void)app.publish_auto_contrast(canceled); });
  require(app.job_scheduler()->usage().reserved_bytes > 0 && alias->resolved_range() ==
          std::array<double, 2>{86, 109.92578125}, "Escaped Auto alias lost reservation/result");
  alias.reset();
  require(app.job_scheduler()->usage().reserved_bytes == 0, "Canceled Auto reservation leaked after alias release");
  atomic_unchanged(before, document->snapshot());
  auto reset = app.submit_auto_contrast(); ready(reset);
  app.reset_auto_contrast_cycle();
  rejects<RevisionConflict>([&] { (void)app.publish_auto_contrast(reset); });
  atomic_unchanged(before, document->snapshot());
  require(app.auto_contrast_divisor() == 0, "Explicit cycle reset published pending Auto");
}
void preflight_and_control() {
  const auto provider = std::make_shared<CountingSource>();
  const auto document = owner(Dataset(provider, calibration()));
  const auto before = document->snapshot();
  int callbacks = 0;
  ExecutionControl control; control.checkpoint = [&] { ++callbacks; };
  AutoContrastLimits cap; cap.max_working_bytes = 1;
  rejects([&] { (void)resolve_auto_contrast_command(before, {}, cap, control); });
  control.cancellation = std::make_shared<CancellationToken>(); control.cancellation->cancel();
  rejects<OperationCancelled>([&] { (void)resolve_auto_contrast_command(before, {}, {}, control); });
  require(provider->reads == 0 && provider->regions == 0 && callbacks == 0,
          "Cap/pre-cancel reached provider or callbacks");
  control = {}; control.checkpoint = [&] { ++callbacks; throw std::runtime_error("SDK callback failure"); };
  rejects<std::runtime_error>([&] { (void)resolve_auto_contrast_command(before, {}, {}, control); });
  require(callbacks == 1 && provider->reads == 0, "Initial callback exception reached provider");
  AutoContrastRequest caller_request; DocumentSnapshot caller_source = before;
  ExecutionControl captured;
  captured.checkpoint = [&] {
    caller_request.previous_divisor = 5000; caller_request.roi = AutoContrastRoiPolicy::WholePlane;
    auto view = caller_source.display(); view.t = 0; view.c = 0; view.crosshair[2] = 0;
    caller_source = caller_source.with_display(view);
  };
  const auto immutable = resolve_auto_contrast_command(caller_source, caller_request, {}, captured);
  require(immutable.request().previous_divisor == 0 && immutable.request().roi == AutoContrastRoiPolicy::ActiveSelection &&
          immutable.position().t == 1 && immutable.position().c == 1 && immutable.position().z == 1 &&
          immutable.histogram().finite_count == 15 && provider->reads == 1,
          "Reentrant callback retargeted captured Auto request/source");
  atomic_unchanged(before, document->snapshot());
}
void native_session() {
  const auto document = owner(dataset(PixelType::UInt16));
  Application app; app.adopt(document);
  auto job = app.submit_auto_contrast(); ready(job);
  const auto command = app.publish_auto_contrast(job);
  const auto captured = document->snapshot();
  SessionCapture capture{{captured}, {metadata_object({{"sdk", std::int64_t(7)}})},
                         {metadata_object(command.record())}, {{"sdk", std::string("Auto provenance")}}};
  SessionLimits limits; limits.max_documents = 1; limits.max_planes = 12; limits.max_samples = 288;
  limits.max_json_bytes = 2*1024*1024; limits.max_encoded_bytes = 2*1024*1024;
  limits.max_working_bytes = 16*1024*1024;
  const auto bytes = encode_session(capture, limits);
  const std::string magic = "IMAGEJXX3\n";
  require(bytes.size() >= magic.size() &&
          std::string(bytes.begin(), bytes.begin()+magic.size()) == magic,
          "SDK did not emit nativeV3 framing");
  const auto decoded = decode_session(bytes, limits);
  require(decoded.source_version == 3 && decoded.documents.size() == 1 &&
          metadata_equal(decoded.workflow, capture.workflow) && metadata_equal(decoded.results, capture.results) &&
          metadata_equal(decoded.extensions, capture.extensions), "NativeV3 lost resolved Auto/workflow/results/extensions");
  const auto loaded = decoded.documents[0]->snapshot();
  require(loaded.id() != captured.id() && !loaded.dirty() && decoded.documents[0]->undo_steps() == 0 &&
          decoded.documents[0]->redo_steps() == 0 && loaded.data().selection == captured.data().selection &&
          loaded.data().previous_selection == captured.data().previous_selection &&
          loaded.data().rois == captured.data().rois && loaded.data().metadata == captured.data().metadata &&
          metadata_equal(loaded.display().properties, captured.display().properties),
          "NativeV3 owner/annotations/display persistence differs");
  same_calibration(loaded.data().dataset.calibration(), calibration());
  for (int t = 0; t != 2; ++t) for (int c = 0; c != 3; ++c) for (int z = 0; z != 2; ++z) {
    const auto actual = loaded.data().dataset.plane({t, c, z});
    for (int y = 0; y != 4; ++y) for (int x = 0; x != 6; ++x)
      require(actual->at(x, y) == 40*t + 30*c + 5*z + 10*y + x, "NativeV3 changed literal source samples");
  }
  Application reopened; reopened.adopt(decoded.documents[0]);
  require(reopened.auto_contrast_divisor() == 0 && app.auto_contrast_divisor() == 5000,
          "NativeV3 serialized transient Auto cycle");
  rejects([&] { (void)reopened.replay_auto_contrast(command.record()); });
  AutoContrastRequest complete; complete.scope = AutoContrastScope::CompositeAll;
  auto rebound = reopened.submit_rebound_auto_contrast(command.record(), complete); ready(rebound);
  const auto replacement = reopened.publish_auto_contrast(rebound);
  require(replacement.source().id() == loaded.id() && replacement.cycle().previous == 0 &&
          replacement.applied_range() == std::array<double, 2>{86, 110} &&
          reopened.auto_contrast_divisor() == 5000, "Reopened explicit rebind did not compute a fresh owner effect");
}
void atomic_display_reset() {
  const auto provider = std::make_shared<CountingSource>();
  const auto document = owner(Dataset(provider, calibration()));
  auto edit = document->begin_edit(); edit.set_property("reset redo witness", true);
  document->commit(edit); document->undo(); document->mark_saved();
  Application app; app.adopt(document);
  const auto undo = document->undo_steps(), redo = document->redo_steps();
  require(redo == 1 && !document->snapshot().dirty(), "Reset SDK fixture lacks clean saved/redo state");
  const auto display_only = [](const std::shared_ptr<Document> &target,
                               const DocumentSnapshot &before, const DisplayState &proposed) {
    const auto after = target->snapshot();
    same_data(before, after);
    require(after.revisions().display == before.revisions().display + 1 &&
            after.display().revision == before.display().revision + 1 &&
            after.display().t == proposed.t && after.display().c == proposed.c &&
            after.display().crosshair == proposed.crosshair && after.display().zoom == proposed.zoom &&
            after.display().planeZoom == proposed.planeZoom && after.display().pan == proposed.pan &&
            after.display().orthogonal == proposed.orthogonal && after.display().linkedZoom == proposed.linkedZoom &&
            metadata_equal(after.display().properties, proposed.properties),
            "Atomic SDK Reset must publish only Display+1 and preserve the complete proposed presentation");
  };
  auto first = app.submit_auto_contrast(); ready(first);
  const auto record = app.publish_auto_contrast(first).record();
  require(app.auto_contrast_divisor() == 5000 && provider->reads == 1, "Reset SDK failed to establish first live cycle");

  // Numeric Set and strict replay are not an explicit ResetRange consumer.
  provider->forbid = true;
  auto ordinary = document->snapshot().display(); ordinary.zoom = 4;
  app.set_display(ordinary);
  require(app.auto_contrast_divisor() == 5000, "Ordinary Set incorrectly resets the authoritative Auto cycle");
  (void)app.replay_auto_contrast(record);
  require(app.auto_contrast_divisor() == 5000 && provider->reads == 1 && provider->regions == 0 &&
          document->snapshot().display().zoom == 4,
          "Strict replay reset cycle, reacquired pixels or restored historical camera state");

  provider->forbid = false;
  auto old_ready = app.submit_auto_contrast(); ready(old_ready);
  require(old_ready.resolved().cycle().previous == 5000, "Reset SDK pending request lost authoritative previous value");
  const auto equal_before = document->snapshot(); const auto equal_reads = provider->reads.load();
  provider->forbid = true;
  app.set_display_and_reset_auto_contrast(equal_before, equal_before.display());
  display_only(document, equal_before, equal_before.display());
  require(app.auto_contrast_divisor() == 0 && provider->reads == equal_reads && provider->regions == 0 &&
          old_ready.info().state == JobState::Ready, "Equal active Reset failed atomic cycle/no-read semantics");
  const auto equal_after = document->snapshot();
  rejects<RevisionConflict>([&] { (void)app.publish_auto_contrast(old_ready); });
  atomic_unchanged(equal_after, document->snapshot());
  require(old_ready.info().state == JobState::Failed && app.auto_contrast_divisor() == 0,
          "Pre-reset READY command restored its old live cycle");

  provider->forbid = false;
  auto restarted = app.submit_auto_contrast(); ready(restarted);
  require(restarted.resolved().cycle().previous == 0, "Equal Reset did not restart the next native Auto request");
  (void)app.publish_auto_contrast(restarted);
  const auto changed_before = document->snapshot(); auto changed = changed_before.display();
  auto channel_state = state(changed_before); channel_state.channels[1].range = {3, 19};
  changed = with_channel_display(std::move(changed), changed_before.data().dataset, std::move(channel_state));
  const auto changed_reads = provider->reads.load(); provider->forbid = true;
  app.set_display_and_reset_auto_contrast(changed_before, changed);
  display_only(document, changed_before, changed);
  require(app.auto_contrast_divisor() == 0 && provider->reads == changed_reads,
          "Changed active Reset did not clear cycle without reading pixels");

  provider->forbid = false;
  auto active_first = app.submit_auto_contrast(); ready(active_first);
  (void)app.publish_auto_contrast(active_first);
  const auto inactive = owner(dataset(PixelType::UInt8)); app.adopt(inactive, false);
  const auto active_before = document->snapshot(), inactive_before = inactive->snapshot();
  auto active_pending = app.submit_auto_contrast(); ready(active_pending);
  auto inactive_display = inactive_before.display(); inactive_display.zoom = 7;
  const auto inactive_reads = provider->reads.load(); provider->forbid = true;
  app.set_display_and_reset_auto_contrast(inactive_before, inactive_display);
  display_only(inactive, inactive_before, inactive_display);
  atomic_unchanged(active_before, document->snapshot());
  require(app.auto_contrast_divisor() == 5000 && active_pending.info().state == JobState::Ready &&
          provider->reads == inactive_reads && inactive->undo_steps() == 0 && inactive->redo_steps() == 0,
          "Inactive Reset altered active owner/cycle/work or its own history");
  const auto active_result = app.publish_auto_contrast(active_pending);
  require(active_result.cycle().previous == 5000 && active_result.cycle().next == 2500 &&
          app.auto_contrast_divisor() == 2500 && provider->reads == inactive_reads,
          "Inactive Reset invalidated active pending work or changed its authoritative cycle");

  provider->forbid = false;
  auto invalid_guard_pending = app.submit_auto_contrast(); ready(invalid_guard_pending);
  const auto invalid_before = document->snapshot(); auto invalid = invalid_before.display(); invalid.zoom = -1;
  const auto invalid_reads = provider->reads.load(); provider->forbid = true;
  rejects([&] { app.set_display_and_reset_auto_contrast(invalid_before, invalid); });
  atomic_unchanged(invalid_before, document->snapshot());
  require(app.auto_contrast_divisor() == 2500 && invalid_guard_pending.info().state == JobState::Ready &&
          provider->reads == invalid_reads, "Invalid Reset published cycle/epoch or consumed source pixels");
  const auto after_invalid = app.publish_auto_contrast(invalid_guard_pending);
  require(after_invalid.cycle().previous == 2500 && app.auto_contrast_divisor() == 1250 && provider->reads == invalid_reads,
          "Invalid Reset mutated the epoch of otherwise-valid READY work");

  const auto stale_expected = document->snapshot(); auto newer = stale_expected.display(); newer.zoom += .25;
  app.set_display(std::move(newer));
  provider->forbid = false;
  auto current_pending = app.submit_auto_contrast(); ready(current_pending);
  const auto current = document->snapshot(); const auto stale_reads = provider->reads.load(); provider->forbid = true;
  rejects<RevisionConflict>([&] { app.set_display_and_reset_auto_contrast(stale_expected, stale_expected.display()); });
  atomic_unchanged(current, document->snapshot());
  require(app.auto_contrast_divisor() == 1250 && current_pending.info().state == JobState::Ready,
          "Stale Reset changed live cycle or current READY work");
  const auto after_stale = app.publish_auto_contrast(current_pending);
  require(after_stale.cycle().previous == 1250 && app.auto_contrast_divisor() == 625 &&
          provider->reads == stale_reads && provider->regions == 0,
          "Stale Reset mutated the epoch or performed hidden pixel work");
  require(document->undo_steps() == undo && document->redo_steps() == redo && !document->snapshot().dirty(),
          "Atomic Reset or its refusals changed saved/dirty identity, undo or redo");
}
} // namespace

int main() {
  try {
    run("S01.u8-captured-roi", scalar_u8);
    run("S02.u16-composite-getter", scalar_u16);
    run("S03.f32-raw-endpoints", scalar_f32);
    run("S04.tiny-roi-u16-fallback", fallback);
    run("S05.quiet-nan-empty", quiet_and_empty);
    run("S06.provider-zero-read-replay", provider_and_replay);
    run("S07.complete-rebind-malformed", explicit_rebind);
    run("S08.application-cycle-history", application_cycles);
    run("S09.stale-owner-activation", stale_and_activation);
    run("S10.cancel-reset-alias", cancellation_and_reset);
    run("S11.preflight-callback-capture", preflight_and_control);
    run("S12.native-v3-provenance", native_session);
    run("S13.atomic-display-reset-controller", atomic_display_reset);
    require(cases == 13, "SDK case membership incomplete");
    std::cout << "PASS auto_contrast SDK: " << cases << " cases, " << checks << " checks\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "auto_contrast SDK " << active << ": " << error.what() << '\n';
    return 1;
  }
}
