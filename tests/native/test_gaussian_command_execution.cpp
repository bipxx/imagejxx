#include <imagejxx/application.hpp>
#include <imagejxx/gaussian_command.hpp>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <iostream>
#include <limits>
#include <mutex>
#include <stdexcept>
#include <string_view>
#include <utility>
#include <vector>

using namespace imagejxx;
using namespace std::chrono_literals;
namespace {
constexpr const char *operation = "gaussian_blur_imagej";

void check(bool condition, const char *message) {
  if (!condition)
    throw std::runtime_error(message);
}

template <class Exception = std::invalid_argument, class F>
void rejects(F work, const char *message) {
  bool rejected = false;
  try {
    work();
  } catch (const Exception &) {
    rejected = true;
  }
  check(rejected, message);
}

std::array<int, 3> key(PlanePosition position) {
  return {position.t, position.c, position.z};
}

bool sameImage(const Image &left, const Image &right) {
  return left.width == right.width && left.height == right.height &&
         left.channels == right.channels && left.type == right.type &&
         left.pixels.size() == right.pixels.size() &&
         std::memcmp(left.pixels.data(), right.pixels.data(),
                     left.pixels.size() * sizeof(double)) == 0;
}

// Owned deterministic input, independent of the command, target and executor.
// Float32 values are exactly representable eighths; integer channels remain
// asymmetric and distinct. These tests verify integration against the public
// numerical primitive; the separate captured matrix establishes its numerics.
Image analyticPlane(DatasetShape shape, PixelType type, PlanePosition position) {
  Image result(shape.x, shape.y, shape.s, type);
  for (int y = 0; y < shape.y; ++y)
    for (int x = 0; x < shape.x; ++x)
      for (int sample = 0; sample < shape.s; ++sample) {
        const int value = 37 * x + 61 * y + 13 * x * y +
                          97 * position.t + 503 * position.c +
                          997 * position.z + 173 * sample;
        result.at(x, y, sample) =
            type == PixelType::UInt8 ? value % 256
            : type == PixelType::Float32 ? (value % 65536) / 8.0 - .125
                                        : value % 65536;
      }
  return result;
}

struct Gate {
  std::mutex mutex;
  std::condition_variable changed;
  bool entered = false, released = false;
  void block(const JobContext *context = nullptr) {
    std::unique_lock<std::mutex> lock(mutex);
    entered = true;
    changed.notify_all();
    while (!released) {
      changed.wait_for(lock, 5ms);
      if (context)
        context->checkpoint();
    }
  }
  void await() {
    std::unique_lock<std::mutex> lock(mutex);
    check(changed.wait_for(lock, 5s, [&] { return entered; }),
          "Gaussian test worker did not reach its gate");
  }
  void release() {
    std::lock_guard<std::mutex> lock(mutex);
    released = true;
    changed.notify_all();
  }
};

// Declare after the Application so exception unwinding releases blocked work
// before application shutdown joins it. Worker/source captures retain Gate.
struct ReleaseGate {
  std::shared_ptr<Gate> gate;
  ~ReleaseGate() { gate->release(); }
};

class CountingSource final : public PlaneSource {
public:
  CountingSource(DatasetShape shape, PixelType type = PixelType::Float32,
                 std::shared_ptr<Gate> readGate = {})
      : shape_(shape), type_(type), readGate_(std::move(readGate)) {
    for (int t = 0; t < shape.t; ++t)
      for (int c = 0; c < shape.c; ++c)
        for (int z = 0; z < shape.z; ++z)
          originals_.push_back(std::make_shared<const Image>(
              analyticPlane(shape, type, {t, c, z})));
  }
  DatasetShape shape() const override { return shape_; }
  PixelType pixel_type() const override { return type_; }
  std::shared_ptr<const Image> read_plane(PlanePosition position) const override {
    {
      std::lock_guard<std::mutex> lock(mutex_);
      reads_.push_back(key(position));
    }
    if (readGate_)
      readGate_->block();
    return originals_.at(index(position));
  }
  std::vector<std::array<int, 3>> reads() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return reads_;
  }
  void checkOriginals() const {
    for (int t = 0; t < shape_.t; ++t)
      for (int c = 0; c < shape_.c; ++c)
        for (int z = 0; z < shape_.z; ++z)
          check(sameImage(*originals_.at(index({t, c, z})),
                          analyticPlane(shape_, type_, {t, c, z})),
                "Gaussian execution modified a retained source pixel buffer");
  }

private:
  std::size_t index(PlanePosition position) const {
    return (std::size_t(position.t) * shape_.c + position.c) * shape_.z +
           position.z;
  }
  DatasetShape shape_;
  PixelType type_;
  std::shared_ptr<Gate> readGate_;
  std::vector<std::shared_ptr<const Image>> originals_;
  mutable std::mutex mutex_;
  mutable std::vector<std::array<int, 3>> reads_;
};

struct SourceState {
  DocumentSnapshot snapshot;
  std::size_t undo, redo;
  explicit SourceState(const Document &owner)
      : snapshot(owner.snapshot()), undo(owner.undo_steps()),
        redo(owner.redo_steps()) {}
  void unchanged(const Document &owner) const {
    const auto current = owner.snapshot();
    check(current.shares_data(snapshot) &&
              current.revisions().matches(snapshot.revisions(), Change::All) &&
              current.dirty() == snapshot.dirty() &&
              owner.undo_steps() == undo && owner.redo_steps() == redo,
          "Gaussian preparation/failure modified source, display, dirty state or history");
  }
};

SpatialCalibration calibrated() {
  SpatialCalibration calibration;
  calibration.spacing = {.5, 2, 3};
  calibration.origin = {10, -5, 2};
  calibration.direction = {0, -1, 0, 1, 0, 0, 0, 0, 1};
  calibration.unit = "um";
  return calibration;
}

void checkCalibration(const SpatialCalibration &actual,
                      const SpatialCalibration &expected) {
  check(actual.spacing == expected.spacing && actual.origin == expected.origin &&
            actual.direction == expected.direction && actual.unit == expected.unit &&
            actual.frame_interval == expected.frame_interval &&
            actual.time_origin == expected.time_origin &&
            actual.time_unit == expected.time_unit,
        "Gaussian pixel operation changed spatial or time calibration");
}

void testDirectDispatchAndScaledExecution() {
  const DatasetShape shape{1, 1, 1, 17, 23, 1};
  for (const auto layout :
       {std::pair<PixelType, int>{PixelType::UInt8, 1},
        {PixelType::UInt16, 1}, {PixelType::Float32, 1},
        {PixelType::UInt8, 3}}) {
    auto typedShape = shape;
    typedShape.s = layout.second;
    const auto input = analyticPlane(typedShape, layout.first, {});
    const auto retained = input;
    const auto direct = execute_operation(operation, input);
    check(sameImage(direct.image, gaussian_blur_imagej(input, 2, 2)) &&
              sameImage(input, retained) && !direct.labelCount && !direct.labelImage,
          "New operation dispatch changed defaults, source or typed numerical result");
  }
  Application app;
  const auto input = analyticPlane(shape, PixelType::Float32, {});
  auto owner = std::make_shared<Document>(Dataset::from_stack({input}, calibrated()));
  app.adopt(owner);
  const SourceState before(*owner);
  const auto command = resolve_gaussian_command(before.snapshot, {}, {2, true});
  const auto prepared = app.prepare(operation, command.target(), command.parameters());
  const auto expected = gaussian_blur_imagej(input, 4, 1);
  const auto pixel = execute_operation(
      operation, input, {{"sigma", NumericArray{{2}, {1, 4}}}});
  check(sameImage(*prepared.preview().data().dataset.plane(), expected) &&
            sameImage(pixel.image, expected) && prepared.active(),
        "Scaled command and explicit pixel sigma did not produce identical output");
  checkCalibration(prepared.preview().data().dataset.calibration(), calibrated());
  before.unchanged(*owner);
}

void testAreaPhaseAndIrregularRestoration() {
  const DatasetShape shape{1, 1, 1, 101, 173, 1};
  const auto input = analyticPlane(shape, PixelType::Float32, {});
  const GaussianBlurExtent extent{97, 43, 21, 19};
  const auto rectangleExpected = gaussian_blur_imagej(input, 12, 12, extent);
  const auto whole = gaussian_blur_imagej(input, 12, 12);
  bool differentPhase = false;
  for (int y = 43; y < 62; ++y)
    for (int x = 97; x < 118; ++x)
      differentPhase = differentPhase || rectangleExpected.at(x, y) != whole.at(x, y);
  check(differentPhase,
        "Large off-center fixture does not distinguish ROI phase from whole-plane blur");
  Application app;
  auto owner = std::make_shared<Document>(Dataset::from_stack({input}, calibrated()));
  app.adopt(owner);
  owner->select(Selection("rectangle", {{97, 43}, {118, 62}}));
  SourceState before(*owner);
  auto command = resolve_gaussian_command(before.snapshot, {}, {12, false});
  auto rectangle = app.prepare(operation, command.target(), command.parameters());
  check(sameImage(*rectangle.preview().data().dataset.plane(), rectangleExpected),
        "Executor blurred the whole plane then masked instead of using original ROI bounds");
  before.unchanged(*owner);

  owner->select(Selection("polygon", {{97, 43}, {118, 43}, {118, 53},
                                      {107, 53}, {107, 62}, {97, 62}}));
  before = SourceState(*owner);
  command = resolve_gaussian_command(before.snapshot, {}, {12, false});
  auto irregular = app.prepare(operation, command.target(), command.parameters());
  auto expected = input;
  bool changedInside = false;
  for (int y = 0; y < shape.y; ++y)
    for (int x = 0; x < shape.x; ++x) {
      const bool selected = x >= 97 && x < 118 && y >= 43 && y < 62 &&
                            (y < 53 || x < 107);
      if (selected) {
        expected.at(x, y) = rectangleExpected.at(x, y);
        changedInside = changedInside || expected.at(x, y) != input.at(x, y);
      }
    }
  check(changedInside && sameImage(*irregular.preview().data().dataset.plane(), expected),
        "Irregular Gaussian did not use geometry phase and preserve exact mask exterior");
  check(irregular.preview().data().selection == before.snapshot.data().selection,
        "Gaussian preparation changed the selected geometry");
  checkCalibration(irregular.preview().data().dataset.calibration(), calibrated());
  before.unchanged(*owner);
  check(sameImage(input, analyticPlane(shape, PixelType::Float32, {})),
        "ROI integration mutated its owned analytic input");
}

void testNonAreaWholePlaneExecution() {
  const DatasetShape shape{1, 1, 1, 17, 23, 1};
  const auto input = analyticPlane(shape, PixelType::UInt16, {});
  const auto expected = gaussian_blur_imagej(input, 2, 2);
  Application app;
  auto owner = std::make_shared<Document>(Dataset::from_stack({input}));
  app.adopt(owner);
  for (const auto selection :
       {Selection("line", {{1, 1}, {8, 9}}, 3), Selection("point", {{3, 4}})}) {
    owner->select(selection);
    const SourceState before(*owner);
    const auto command = resolve_gaussian_command(before.snapshot);
    const auto prepared = app.prepare(operation, command.target(), command.parameters());
    check(sameImage(*prepared.preview().data().dataset.plane(), expected),
          "Line/point Gaussian command incorrectly used a stroke write mask");
    before.unchanged(*owner);
  }
}

void checkTargetExecution(TargetRequest request,
                          const std::vector<PlanePosition> &expectedPositions,
                          bool legacyAllZ = false) {
  const DatasetShape shape{2, 2, 3, 13, 19, 1};
  auto source = std::make_shared<CountingSource>(shape);
  Application app;
  auto owner = std::make_shared<Document>(Dataset(source, calibrated()));
  app.adopt(owner);
  auto display = owner->snapshot().display();
  display.t = display.c = 1;
  display.crosshair[2] = 2;
  owner->set_display(display);
  const auto command = resolve_gaussian_command(owner->snapshot(), request, {2, true});
  const auto target = legacyAllZ
      ? resolve_legacy_target(owner->snapshot(), true, {}, operation)
      : command.target();
  display.t = display.c = 0;
  display.crosshair[2] = 0;
  owner->set_display(display);
  const SourceState before(*owner);
  const auto prepared = app.prepare(operation, target, command.parameters());
  std::vector<std::array<int, 3>> expectedReads;
  for (const auto position : expectedPositions)
    expectedReads.push_back(key(position));
  check(source->reads() == expectedReads &&
            prepared.outputs().size() == expectedPositions.size(),
        "Gaussian preparation read unrelated planes or ignored captured target order");
  for (std::size_t index = 0; index < expectedPositions.size(); ++index) {
    const auto position = expectedPositions[index];
    const auto expected = gaussian_blur_imagej(
        analyticPlane(shape, PixelType::Float32, position), 4, 1);
    check(key(prepared.outputs()[index].position) == key(position) &&
              !prepared.outputs()[index].skipped &&
              sameImage(*prepared.preview().data().dataset.plane(position), expected),
          "Gaussian scientific target output differs from its captured plane and sigma");
  }
  check(source->reads() == expectedReads,
        "Prepared changed planes fell through to unrelated lazy source reads");
  before.unchanged(*owner);
  source->checkOriginals();
}

void testScientificTargetExecution() {
  checkTargetExecution({}, {{1, 1, 2}});
  TargetRequest request;
  request.scope = TargetScope::Positions;
  request.positions = {{1, 1, 2}, {0, 0, 1}, {1, 0, 0}};
  checkTargetExecution(request, request.positions);
  request = {};
  request.scope = TargetScope::Ranges;
  request.t = AxisRange{0, 2, 1};
  request.c = AxisRange{1, 2, 1};
  request.z = AxisRange{0, 3, 2};
  checkTargetExecution(request, {{0, 1, 0}, {0, 1, 2}, {1, 1, 0}, {1, 1, 2}});
  request.t = request.c = request.z = AxisRange{};
  std::vector<PlanePosition> all;
  for (int t = 0; t < 2; ++t)
    for (int c = 0; c < 2; ++c)
      for (int z = 0; z < 3; ++z)
        all.push_back({t, c, z});
  checkTargetExecution(request, all);
  request = {};
  request.scope = TargetScope::AllZ;
  checkTargetExecution(request, {{1, 1, 0}, {1, 1, 1}, {1, 1, 2}});
  checkTargetExecution(request, {{1, 1, 0}, {1, 1, 1}, {1, 1, 2}}, true);
}

void testPositionedUnionAndSkippedReads() {
  const DatasetShape shape{2, 2, 3, 17, 23, 1};
  auto source = std::make_shared<CountingSource>(shape);
  Application app;
  auto owner = std::make_shared<Document>(Dataset(source));
  app.adopt(owner);
  auto edit = owner->begin_edit();
  edit.set_rois({
      Roi{"first", Selection("rectangle", {{1, 2}, {4, 5}}), 1, 1, 2, {}},
      Roi{"second", Selection("rectangle", {{8, 6}, {11, 9}}), 1, 1, 2, {}},
      Roi{"line", Selection("line", {{2, 3}, {7, 9}}), 1, 0, 0, {}}});
  owner->commit(edit);
  const SourceState before(*owner);
  TargetRequest request;
  request.scope = TargetScope::Positions;
  request.positions = {{1, 1, 2}, {0, 0, 1}, {1, 0, 0}};
  request.roi = RoiPolicy::PositionedRois;
  const auto command = resolve_gaussian_command(before.snapshot, request, {12, false});
  const auto prepared = app.prepare(operation, command.target(), command.parameters());
  const std::vector<std::array<int, 3>> expectedReads{{1, 1, 2}, {1, 0, 0}};
  check(source->reads() == expectedReads && prepared.outputs().size() == 3 &&
            !prepared.outputs()[0].skipped && prepared.outputs()[1].skipped &&
            !prepared.outputs()[2].skipped,
        "Positioned Gaussian read an unmatched plane or skipped a matching non-area ROI");
  auto expected = analyticPlane(shape, PixelType::Float32, {1, 1, 2});
  const auto rectangle = gaussian_blur_imagej(expected, 12, 12,
                                             GaussianBlurExtent{1, 2, 10, 7});
  for (int y = 0; y < shape.y; ++y)
    for (int x = 0; x < shape.x; ++x)
      if ((x >= 1 && x < 4 && y >= 2 && y < 5) ||
          (x >= 8 && x < 11 && y >= 6 && y < 9))
        expected.at(x, y) = rectangle.at(x, y);
  check(sameImage(*prepared.preview().data().dataset.plane({1, 1, 2}), expected) &&
            sameImage(*prepared.preview().data().dataset.plane({1, 0, 0}),
                      gaussian_blur_imagej(
                          analyticPlane(shape, PixelType::Float32, {1, 0, 0}), 12, 12)) &&
            source->reads() == expectedReads,
        "Positioned union used per-ROI phases or failed exact exterior restoration");
  before.unchanged(*owner);
  source->checkOriginals();
}

void testPreviewPublicationUndoRedo() {
  const DatasetShape shape{1, 1, 1, 17, 23, 1};
  const auto input = analyticPlane(shape, PixelType::UInt16, {});
  const auto expected = gaussian_blur_imagej(input, 2, 2);
  check(!sameImage(expected, input), "Publication fixture is an identity blur");
  Application app;
  auto owner = std::make_shared<Document>(Dataset::from_stack({input}, calibrated()));
  app.adopt(owner);
  const SourceState before(*owner);
  const auto command = resolve_gaussian_command(before.snapshot);
  auto prepared = app.prepare(operation, command.target(), command.parameters());
  auto retained = prepared;
  check(sameImage(*prepared.preview().data().dataset.plane(), expected),
        "Prepared preview has the wrong Gaussian pixels");
  before.unchanged(*owner);
  auto display = owner->snapshot().display();
  display.crosshair = {7, 5, 0};
  display.pan[0] = {2, 3};
  app.set_display(display);
  const auto published = app.publish(prepared);
  check(!published.created && published.document == owner &&
            owner->undo_steps() == 1 && !prepared.active() && !retained.active() &&
            owner->snapshot().dirty() &&
            sameImage(*owner->snapshot().data().dataset.plane(), expected) &&
            owner->snapshot().display().crosshair == display.crosshair &&
            owner->snapshot().display().pan == display.pan,
        "Gaussian Apply did not publish one atomic undoable edit preserving navigation");
  const SourceState applied(*owner);
  rejects([&] { app.publish(retained); }, "A Gaussian preparation published twice");
  applied.unchanged(*owner);
  app.undo();
  check(sameImage(*owner->snapshot().data().dataset.plane(), input) &&
            owner->undo_steps() == 0 && owner->redo_steps() == 1 &&
            owner->snapshot().display().crosshair == display.crosshair,
        "Gaussian undo lost original pixels or current navigation");
  app.redo();
  check(sameImage(*owner->snapshot().data().dataset.plane(), expected) &&
            owner->undo_steps() == 1 && owner->redo_steps() == 0,
        "Gaussian redo did not restore the exact published result");
  check(sameImage(*retained.preview().data().dataset.plane(), expected),
        "Retained Gaussian preview changed after publication and undo/redo");
  checkCalibration(owner->snapshot().data().dataset.calibration(), calibrated());
}

void testZeroSigmaNativeNoopHistory() {
  const DatasetShape shape{1, 1, 1, 17, 23, 1};
  const auto input = analyticPlane(shape, PixelType::UInt16, {});
  Application app;
  auto owner = std::make_shared<Document>(Dataset::from_stack({input}));
  app.adopt(owner);
  const SourceState before(*owner);
  const auto command = resolve_gaussian_command(before.snapshot, {}, {0, false});
  auto prepared = app.prepare(operation, command.target(), command.parameters());
  check(sameImage(*prepared.preview().data().dataset.plane(), input),
        "Zero sigma changed representable native UInt16 samples");
  app.publish(prepared);
  check(!prepared.active(), "Zero-sigma publication did not consume its preparation");
  before.unchanged(*owner);
}

void testRecordedCommandAndExplicitNewDocumentRebind() {
  const DatasetShape shape{1, 1, 1, 17, 23, 1};
  const auto input = analyticPlane(shape, PixelType::Float32, {});
  Application app;
  auto owner = std::make_shared<Document>(Dataset::from_stack({input}, calibrated()));
  app.adopt(owner);
  TargetRequest request;
  request.destination = OutputDestination::NewDocument;
  const auto command = resolve_gaussian_command(owner->snapshot(), request, {2, true});
  const auto record = command.record();
  const auto replay = gaussian_command_from_record(owner->snapshot(), record);
  check(metadata_equal(replay.record(), record),
        "Gaussian command replay did not retain source-bound raw and effective intent");
  const SourceState before(*owner);
  auto prepared = app.prepare(operation, replay.target(), replay.parameters());
  const auto published = app.publish(prepared, "Gaussian output");
  const auto expected = gaussian_blur_imagej(input, 4, 1);
  check(published.created && published.document && published.document != owner &&
            published.document_id != owner->id && published.document->undo_steps() == 1 &&
            published.document->snapshot().data().metadata.title == "Gaussian output" &&
            sameImage(*published.document->snapshot().data().dataset.plane(), expected),
        "Recorded new-document command did not create an isolated scientific result");
  before.unchanged(*owner);
  checkCalibration(published.document->snapshot().data().dataset.calibration(), calibrated());
  const SourceState outputBefore(*published.document);
  rejects([&] { gaussian_command_from_record(outputBefore.snapshot, record); },
          "Gaussian replay silently rebound an original source record to new output");
  const auto rebound = resolve_gaussian_command(outputBefore.snapshot, {}, replay.options());
  auto next = app.prepare(operation, rebound.target(), rebound.parameters());
  check(sameImage(*next.preview().data().dataset.plane(),
                  gaussian_blur_imagej(expected, 4, 1)),
        "Explicit new-source command rebind retained old pixels or old target identity");
  outputBefore.unchanged(*published.document);
}

void testStaleCanceledAndClosedFailureAtomicity() {
  const DatasetShape shape{1, 1, 1, 17, 23, 1};
  const auto input = analyticPlane(shape, PixelType::UInt16, {});
  Application app;
  auto owner = std::make_shared<Document>(Dataset::from_stack({input}));
  app.adopt(owner);
  auto command = resolve_gaussian_command(owner->snapshot());
  auto stale = app.prepare(operation, command.target(), command.parameters());
  auto newer = input;
  newer.at(1, 1) += 777;
  auto edit = owner->begin_edit();
  edit.replace_plane({}, newer);
  app.commit(edit);
  SourceState before(*owner);
  rejects<RevisionConflict>([&] { app.publish(stale); },
                           "Stale Gaussian preparation overwrote a newer edit");
  check(stale.active(), "Failed stale Gaussian publication consumed the preview");
  before.unchanged(*owner);
  check(sameImage(*owner->snapshot().data().dataset.plane(), newer),
        "Stale failure changed current pixels");

  command = resolve_gaussian_command(owner->snapshot());
  auto token = std::make_shared<CancellationToken>();
  auto canceled = app.prepare(operation, command.target(), command.parameters(), {}, {token, {}});
  before = SourceState(*owner);
  token->cancel();
  rejects<OperationCancelled>([&] { app.publish(canceled); },
                              "Canceled Gaussian preparation published pixels");
  before.unchanged(*owner);
  auto closed = app.prepare(operation, command.target(), command.parameters());
  const auto retainedExpected = gaussian_blur_imagej(newer, 2, 2);
  before = SourceState(*owner);
  app.close(owner->id, true);
  rejects<DocumentUnavailable>([&] { app.publish(closed); },
                               "Closed-source Gaussian preparation published pixels");
  rejects<DocumentUnavailable>(
      [&] { app.prepare(operation, command.target(), command.parameters()); },
      "Closed-source Gaussian command prepared through Application");
  before.unchanged(*owner);
  check(sameImage(*closed.preview().data().dataset.plane(), retainedExpected),
        "Closed-source Gaussian preview lost its retained result");
}

void testExecutionValidationBeforeLazyReads() {
  const DatasetShape shape{1, 1, 1, 17, 23, 1};
  auto source = std::make_shared<CountingSource>(shape);
  Document owner{Dataset(source)};
  const SourceState before(owner);
  const auto target = resolve_target(before.snapshot);
  for (double sigma : {-1.0, std::numeric_limits<double>::infinity(),
                       std::numeric_limits<double>::quiet_NaN(),
                       std::numeric_limits<double>::max()})
    rejects([&] { prepare_operation(operation, target, {{"sigma", sigma}}); },
            "Invalid Gaussian execution sigma passed metadata preflight");
  rejects([&] { prepare_operation(operation, target, {{"border", std::string("reflect")}}); },
          "ImageJ-profile execution accepted an unsupported border option");
  rejects([&] { prepare_operation(operation, target, {{"sigma", NumericArray{{3}, {1, 2, 3}}}}); },
          "Malformed Gaussian sigma pair passed metadata preflight");
  check(source->reads().empty(), "Invalid Gaussian execution parameters read source planes");
  before.unchanged(owner);
  source->checkOriginals();
  for (const auto layout :
       {std::pair<PixelType, int>{PixelType::UInt8, 4},
        {PixelType::UInt16, 3}, {PixelType::Float32, 3},
        {PixelType::Float64, 1}}) {
    auto badShape = shape;
    badShape.s = layout.second;
    auto unsupported = std::make_shared<CountingSource>(badShape, layout.first);
    Document bad{Dataset(unsupported)};
    const SourceState badBefore(bad);
    rejects([&] { prepare_operation(operation, resolve_target(badBefore.snapshot)); },
            "Unsupported Gaussian execution type reached the pixel kernel");
    check(unsupported->reads().empty(), "Unsupported Gaussian execution type read pixels");
    badBefore.unchanged(bad);
    unsupported->checkOriginals();
  }
}

void testCancellationDuringActualKernel() {
  const DatasetShape shape{1, 1, 1, 101, 173, 1};
  auto source = std::make_shared<CountingSource>(shape);
  Document owner{Dataset(source)};
  const SourceState before(owner);
  const auto command = resolve_gaussian_command(before.snapshot, {}, {12, false});
  auto token = std::make_shared<CancellationToken>();
  bool wroteIntermediateOutput = false;
  ExecutionControl control;
  control.cancellation = token;
  control.stage_progress = [&](std::string_view name, std::size_t completed, std::size_t) {
    if (name == "ImageJ Gaussian writeback" && completed > 0) {
      wroteIntermediateOutput = true;
      token->cancel();
    }
  };
  rejects<OperationCancelled>(
      [&] { prepare_operation(operation, command.target(), command.parameters(), {}, control); },
      "Cancellation during Gaussian kernel writeback returned a publishable result");
  check(wroteIntermediateOutput && source->reads() ==
            std::vector<std::array<int, 3>>{{0, 0, 0}},
        "Kernel cancellation was not observed after actual plane work");
  before.unchanged(owner);
  source->checkOriginals();
  const auto readCount = source->reads().size();
  rejects<OperationCancelled>(
      [&] { prepare_operation(operation, command.target(), command.parameters(), {}, {token, {}}); },
      "Pre-canceled Gaussian preparation ran work");
  check(source->reads().size() == readCount,
        "Pre-canceled Gaussian preparation read another plane");
}

void testQueuedCancellationAndParameterBudget() {
  const DatasetShape shape{1, 1, 1, 101, 173, 1};
  Application app;
  auto source = std::make_shared<CountingSource>(shape);
  auto owner = std::make_shared<Document>(Dataset(source));
  app.adopt(owner);
  const SourceState before(*owner);
  auto scheduler = app.job_scheduler();
  scheduler->set_limits({1, 0, 8});
  auto gate = std::make_shared<Gate>();
  ReleaseGate release{gate};
  auto blocker = scheduler->submit([gate](const JobContext &context) { gate->block(&context); });
  gate->await();
  const auto zero = resolve_gaussian_command(before.snapshot, {}, {0, false});
  const auto large = resolve_gaussian_command(before.snapshot, {}, {12, false});
  auto smallJob = app.submit(operation, zero.target(), zero.parameters());
  auto largeJob = app.submit(operation, large.target(), large.parameters());
  const auto smallBytes = smallJob.info().resource_bytes;
  const auto largeBytes = largeJob.info().resource_bytes;
  check(smallJob.info().state == JobState::Queued &&
            largeJob.info().state == JobState::Queued && largeBytes > smallBytes,
        "Gaussian admission did not report parameter-dependent working reservations");
  check(smallJob.cancel() && largeJob.cancel() &&
            smallJob.info().state == JobState::Canceled &&
            largeJob.info().state == JobState::Canceled && source->reads().empty(),
        "Queued Gaussian cancellation ran or retained publishable work");
  rejects<OperationCancelled>([&] { app.publish(largeJob); },
                              "Canceled queued Gaussian job published");
  before.unchanged(*owner);
  gate->release();
  check(blocker.wait_for(5s), "Gaussian reservation gate did not finish");
  blocker.complete([] {});
  scheduler->set_limits({1, smallBytes, 8});
  const auto admittedBefore = scheduler->jobs().size();
  rejects([&] { app.submit(operation, large.target(), large.parameters(), {}, {"underdeclared", {}, 0, 1}); },
          "Underdeclared large-sigma job bypassed the parameter-dependent budget");
  check(source->reads().empty() && scheduler->jobs().size() == admittedBefore &&
            scheduler->usage().reserved_bytes == 0,
        "Rejected Gaussian budget request read pixels or occupied scheduler capacity");
  before.unchanged(*owner);
  auto accepted = app.submit(operation, zero.target(), zero.parameters());
  check(accepted.wait_for(5s) && accepted.info().state == JobState::Ready &&
            source->reads() == std::vector<std::array<int, 3>>{{0, 0, 0}},
        "Admissible zero-sigma request did not execute after budget rejection");
  app.publish(accepted);
  check(accepted.info().state == JobState::Completed,
        "Admissible Gaussian job did not complete publication");
  before.unchanged(*owner);
  source->checkOriginals();
}

void testRunningJobCancellation() {
  const DatasetShape shape{1, 1, 1, 101, 173, 1};
  Application app;
  auto gate = std::make_shared<Gate>();
  ReleaseGate release{gate};
  auto source = std::make_shared<CountingSource>(shape, PixelType::Float32, gate);
  auto owner = std::make_shared<Document>(Dataset(source));
  app.adopt(owner);
  const SourceState before(*owner);
  const auto command = resolve_gaussian_command(before.snapshot, {}, {12, false});
  auto job = app.submit(operation, command.target(), command.parameters());
  gate->await();
  check(job.info().state == JobState::Running && job.cancel(),
        "Running Gaussian job was not cancelable during its source read");
  gate->release();
  check(job.wait_for(5s) && job.info().state == JobState::Canceled &&
            source->reads() == std::vector<std::array<int, 3>>{{0, 0, 0}},
        "Running Gaussian cancellation did not reach a safe kernel checkpoint");
  rejects<OperationCancelled>([&] { app.publish(job); },
                              "Canceled running Gaussian job published pixels");
  before.unchanged(*owner);
  source->checkOriginals();
}
} // namespace

int main() {
  try {
    testDirectDispatchAndScaledExecution();
    testAreaPhaseAndIrregularRestoration();
    testNonAreaWholePlaneExecution();
    testScientificTargetExecution();
    testPositionedUnionAndSkippedReads();
    testPreviewPublicationUndoRedo();
    testZeroSigmaNativeNoopHistory();
    testRecordedCommandAndExplicitNewDocumentRebind();
    testStaleCanceledAndClosedFailureAtomicity();
    testExecutionValidationBeforeLazyReads();
    testCancellationDuringActualKernel();
    testQueuedCancellationAndParameterBudget();
    testRunningJobCancellation();
    std::cout << "Native Gaussian command execution checks passed\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
