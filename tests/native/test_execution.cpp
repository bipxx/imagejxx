#include <algorithm>
#include <imagejxx/application.hpp>
#include <iostream>

using namespace imagejxx;
namespace {
void check(bool condition, const char *message) {
  if (!condition)
    throw std::runtime_error(message);
}
template <class E = std::exception, class F> void rejects(F f) {
  bool rejected = false;
  try {
    f();
  } catch (const E &) {
    rejected = true;
  }
  check(rejected, "Invalid execution/publication accepted");
}
class Source final : public PlaneSource {
public:
  DatasetShape shape() const override { return {2, 2, 4, 4, 6, 1}; }
  PixelType pixel_type() const override { return PixelType::UInt16; }
  std::shared_ptr<const Image> read_plane(PlanePosition p) const override {
    ++reads;
    auto image = std::make_shared<Image>(6, 4, 1, PixelType::UInt16);
    std::fill(image->pixels.begin(), image->pixels.end(),
              1000 * p.t + 100 * p.c + 10 * p.z + 300);
    return image;
  }
  mutable int reads = 0;
};
TargetRequest ranges() {
  TargetRequest request;
  request.scope = TargetScope::Ranges;
  request.t = AxisRange{1, 2, 1};
  request.c = AxisRange{0, 2, 1};
  request.z = AxisRange{1, 4, 2};
  return request;
}
void testPreparationAndPublication() {
  auto source = std::make_shared<Source>();
  auto owner = std::make_shared<Document>(Dataset(source));
  Application app;
  app.adopt(owner);
  auto target = app.resolve_target(ranges(), 0, "add");
  auto prepared = app.prepare("add", target, {{"value", 7}});
  auto copy = prepared;
  check(source->reads == 4 && owner->history().undo_steps == 0 &&
            !owner->snapshot().dirty(),
        "Preparation reads only selected planes and does not publish");
  check(prepared.preview().data().dataset.plane({1, 1, 3})->at(0, 0) == 1437,
        "Prepared scientific positions contain expected results");
  auto view = owner->snapshot().display();
  view.crosshair = {2, 2, 2};
  owner->set_display(view);
  auto result = app.publish(prepared);
  check(result.document_id == owner->id && result.document == owner &&
            !result.created && owner->history().undo_steps == 1 &&
            !copy.active(),
        "One shared preparation publication and undo step");
  check(owner->snapshot().display().crosshair == view.crosshair,
        "Concurrent navigation survives publication");
  check(owner->snapshot().data().dataset.plane({0, 1, 2})->at(0, 0) == 420,
        "Untargeted lazy positions unchanged");
  rejects<std::invalid_argument>([&] { app.publish(copy); });
  owner->undo();
  check(owner->snapshot().data().dataset.plane({1, 1, 3})->at(0, 0) == 1430,
        "Undo restores original scientific data");
  auto stale = app.prepare("add", app.resolve_target(), {{"value", 1}});
  auto edit = owner->begin_edit();
  edit.set_title("Newer edit");
  owner->commit(edit);
  rejects<RevisionConflict>([&] { app.publish(stale); });
  check(stale.active(), "Rejected publication remains inspectable and active");
  auto closed = app.prepare("add", app.resolve_target(), {{"value", 1}});
  app.close(owner->id, true);
  rejects<DocumentUnavailable>([&] { app.publish(closed); });
  check(closed.preview().data().dataset.plane({0, 0, 2})->at(0, 0) == 321,
        "Retained preview survives close");
}
void testPreflightAndCancellation() {
  auto source = std::make_shared<Source>();
  Document owner{Dataset(source)};
  auto target = resolve_target(owner.snapshot(), ranges());
  rejects<std::invalid_argument>(
      [&] { prepare_operation("add", target, {{"unknown", 2}}); });
  rejects<std::invalid_argument>(
      [&] { prepare_operation("convolve", target); });
  rejects<std::invalid_argument>(
      [&] { prepare_operation("rotate90", target); });
  check(source->reads == 0,
        "Invalid parameters/geometry fail before source reads");
  auto token = std::make_shared<CancellationToken>();
  ExecutionControl control{token, [token](std::size_t done, std::size_t) {
                             if (done == 1)
                               token->cancel();
                           }};
  rejects<OperationCancelled>(
      [&] { prepare_operation("add", target, {{"value", 1}}, {}, control); });
  check(source->reads == 1 && owner.history().undo_steps == 0,
        "Cancellation discards prepared planes without reading remaining "
        "positions");
  source->reads = 0;
  rejects<OperationCancelled>(
      [&] { prepare_operation("add", target, {}, {}, {token, {}}); });
  check(source->reads == 0, "Pre-canceled preparation reads no pixels");
}
void testMasksAndPromotion() {
  auto source = std::make_shared<Source>();
  Document owner{Dataset(source)};
  auto edit = owner.begin_edit();
  edit.set_rois(
      {Roi{"one", Selection("rectangle", {{1, 1}, {3, 3}}), 1, 0, 1, {}}});
  owner.commit(edit);
  auto request = ranges();
  request.roi = RoiPolicy::PositionedRois;
  auto prepared =
      prepare_operation("convert", resolve_target(owner.snapshot(), request),
                        {{"dtype", std::string("uint8")}});
  check(source->reads == 1 &&
            prepared.preview().data().dataset.pixel_type() == PixelType::UInt16,
        "Empty ROI positions skipped; partial narrowing retains a common "
        "lossless type");
  auto plane = prepared.preview().data().dataset.plane({1, 0, 1});
  check(plane->at(1, 1) == 255 && plane->at(0, 0) == 1310,
        "Narrowing affects selected pixels only");
  check(prepared.outputs().size() == 4 && prepared.outputs()[1].skipped,
        "Skipped positions have explicit output status");
  request.roi = RoiPolicy::WholePlane;
  auto widened =
      prepare_operation("convert", resolve_target(owner.snapshot(), request),
                        {{"dtype", std::string("float64")}});
  check(widened.preview().data().dataset.pixel_type() == PixelType::Float64 &&
            source->reads == 5,
        "Untargeted promotion is lazy");
  check(widened.preview().data().dataset.plane({0, 0, 0})->at(0, 0) == 300,
        "Lazy promotion preserves unselected numeric values");
}
void testNewDocumentAndFailure() {
  Application app;
  auto owner = std::make_shared<Document>(
      Dataset::from_stack(Stack(3, Image(6, 4, 1, PixelType::UInt16))));
  app.adopt(owner);
  TargetRequest request;
  request.destination = OutputDestination::NewDocument;
  auto prepared =
      app.prepare("add", app.resolve_target(request), {{"value", 8}});
  auto created = app.publish(prepared, "Output");
  check(created.created && created.document &&
            created.document_id != owner->id &&
            created.document->snapshot().data().metadata.title == "Output" &&
            owner->snapshot().data().dataset.plane()->at(0, 0) == 0 &&
            created.document->history().undo_steps == 1,
        "New-document publication retains original owner and creates undoable "
        "output");
  rejects<std::invalid_argument>([&] { app.publish(prepared); });
  auto token = std::make_shared<CancellationToken>();
  auto canceled = app.prepare("add", app.resolve_target({}, owner->id), {}, {},
                              {token, {}});
  token->cancel();
  rejects<OperationCancelled>([&] { app.publish(canceled); });
  auto before = owner->snapshot();
  ExecutionControl fail{{}, [](std::size_t done, std::size_t) {
                          if (done == 1)
                            throw std::runtime_error(
                                "Progress consumer failure");
                        }};
  rejects<std::runtime_error>([&] {
    app.prepare("add", app.resolve_target({}, owner->id), {}, {}, fail);
  });
  check(owner->snapshot().shares_data(before),
        "Preparation callback failure preserves source");
  auto bulk = before.data().dataset.with_planes(
      {{{0, 0, 1}, Image(6, 4, 1, PixelType::UInt16)},
       {{0, 0, 2}, Image(6, 4, 1, PixelType::UInt16)}});
  check(bulk.plane({0, 0, 0}) == before.data().dataset.plane(),
        "Bulk edits share untouched eager planes");
  rejects<std::invalid_argument>([&] {
    before.data().dataset.with_planes(
        {{{0, 0, 1}, Image(6, 4, 1, PixelType::UInt16)},
         {{0, 0, 1}, Image(6, 4, 1, PixelType::UInt16)}});
  });
}
void testSkippedLabelOutputs() {
  auto source = std::make_shared<Source>();
  Document owner{Dataset(source)};
  auto request = ranges();
  request.roi = RoiPolicy::PositionedRois;
  const auto target = resolve_target(owner.snapshot(), request);
  for (const auto &operation : {"label", "binary_watershed"}) {
    auto prepared = prepare_operation(operation, target);
    check(std::all_of(prepared.outputs().begin(), prepared.outputs().end(),
                      [](const auto &output) {
                        return output.skipped && output.label_image &&
                               !output.label_count;
                      }),
          "Skipped label outputs preserve default absence of optional counts");
  }
  auto counted = prepare_operation("label", target, {{"return_num", true}});
  check(std::all_of(counted.outputs().begin(), counted.outputs().end(),
                    [](const auto &output) {
                      return output.skipped && output.label_count == 0;
                    }),
        "Requested skipped component counts are zero");
  check(source->reads == 0 && owner.history().undo_steps == 0,
        "Empty positioned ROIs do not read pixels or publish history");
}
} // namespace
int main() {
  try {
    testPreparationAndPublication();
    testPreflightAndCancellation();
    testMasksAndPromotion();
    testNewDocumentAndFailure();
    testSkippedLabelOutputs();
    std::cout << "Native dataset execution checks passed\n";
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
