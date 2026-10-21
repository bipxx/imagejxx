#include <atomic>
#include <imagejxx/document.hpp>
#include <iostream>
#include <limits>
#include <thread>

using namespace imagejxx;
namespace {
void check(bool condition, const char *message) {
  if (!condition)
    throw std::runtime_error(message);
}
template <class E = std::exception, class F> void rejects(F function) {
  bool rejected = false;
  try {
    function();
  } catch (const E &) {
    rejected = true;
  }
  check(rejected, "Invalid document edit was accepted");
}
class LazySource : public PlaneSource {
public:
  mutable std::atomic<int> reads{0};
  DatasetShape shape() const override { return {2, 2, 3, 4, 5, 1}; }
  PixelType pixel_type() const override { return PixelType::Float64; }
  std::shared_ptr<const Image> read_plane(PlanePosition p) const override {
    ++reads;
    return std::make_shared<const Image>(
        fill(Image(5, 4), 100 * p.t + 10 * p.c + p.z));
  }
};
Dataset fixture() {
  Stack planes;
  for (int i = 0; i < 12; ++i)
    planes.push_back(fill(Image(5, 4), i));
  return Dataset::from_planes({2, 2, 3, 4, 5, 1}, std::move(planes));
}
void guarded_display_publication() {
  Document document(fixture());
  auto edit = document.begin_edit();
  edit.set_title("Retained redo");
  document.commit(edit);
  document.undo();
  auto source = document.snapshot();
  auto display = source.display();
  display.zoom = 2;
  document.set_display_if_current(source, display);
  auto after = document.snapshot();
  check(after.revisions().display == source.revisions().display + 1 &&
            after.revisions().matches(source.revisions(), Change::Data) &&
            after.display().revision == source.display().revision + 1 &&
            after.shares_data(source) &&
            after.data_identity() == source.data_identity() &&
            after.dirty() == source.dirty() && document.undo_steps() == 0 &&
            document.redo_steps() == 1,
        "Guarded display publication preserves source and populated redo");
  rejects<RevisionConflict>(
      [&] { document.set_display_if_current(source, display); });
  document.set_display_if_current(after, after.display());
  check(document.snapshot().revisions().display == after.revisions().display + 1,
        "A successful equal display publication advances exactly once");
  Document foreign(fixture());
  rejects<std::invalid_argument>([&] {
    document.set_display_if_current(foreign.snapshot(), display);
  });
  auto current = document.snapshot();
  auto invalid = current.display();
  invalid.zoom = -1;
  rejects<std::invalid_argument>(
      [&] { document.set_display_if_current(current, invalid); });
  check(document.snapshot().revisions().matches(current.revisions(), Change::All),
        "Invalid guarded display leaves every revision unchanged");
  auto lease = document.constrain(
      [](const DocumentData &, const DisplayState &view) {
        if (view.zoom == 5)
          throw std::invalid_argument("Rejected guarded display");
      });
  invalid = current.display();
  invalid.zoom = 5;
  rejects<std::invalid_argument>(
      [&] { document.set_display_if_current(current, invalid); });
  check(document.snapshot().revisions().matches(current.revisions(), Change::All),
        "Guarded constraints fail before publication");
  lease.reset();
  auto reject_stale = [&](const DocumentSnapshot &old) {
    const auto before = document.snapshot();
    rejects<RevisionConflict>(
        [&] { document.set_display_if_current(old, old.display()); });
    check(document.snapshot().revisions().matches(before.revisions(), Change::All),
          "Stale guarded publication has no effects");
  };
  current = document.snapshot();
  document.select(Selection("rectangle", {{0, 0}, {2, 2}}));
  reject_stale(current);
  current = document.snapshot();
  auto metadata = current.begin_edit();
  metadata.set_title("New title");
  document.commit(metadata);
  reject_stale(current);
  current = document.snapshot();
  auto pixels = current.begin_edit();
  pixels.replace_plane({}, fill(Image(5, 4), 17));
  document.commit(pixels);
  reject_stale(current);
  current = document.snapshot();
  std::atomic<int> winners{0}, conflicts{0}, errors{0};
  auto publish = [&] {
    try {
      document.set_display_if_current(current, current.display());
      ++winners;
    } catch (const RevisionConflict &) {
      ++conflicts;
    } catch (...) {
      ++errors;
    }
  };
  std::thread first(publish), second(publish);
  first.join();
  second.join();
  check(winners == 1 && conflicts == 1 && errors == 0 &&
            document.snapshot().revisions().display == current.revisions().display + 1,
        "Only one concurrent publisher can consume the same captured revisions");
}
} // namespace
int main() {
  try {
    guarded_display_publication();
    Document doc(fixture(), "Original");
    auto source = doc.snapshot();
    check(!source.dirty() && doc.undo_steps() == 0, "New document is clean");
    auto edit = source.begin_edit();
    edit.replace_plane({1, 1, 2}, fill(Image(5, 4), 99));
    edit.set_title("Edited");
    edit.set_property("author", std::string("Test"));
    auto calibration = source.data().dataset.calibration();
    calibration.spacing = {0.5, 2, 3};
    edit.set_calibration(calibration);
    auto preview = edit.preview();
    check(preview.data().dataset.plane({1, 1, 2})->at(0, 0) == 99 &&
              source.data().dataset.plane({1, 1, 2})->at(0, 0) == 11 &&
              doc.snapshot().data().metadata.title == "Original",
          "Preview is detached");
    doc.commit(edit);
    auto committed = doc.snapshot();
    check(!edit.active() && committed.dirty() && doc.undo_steps() == 1,
          "An atomic edit creates one history step");
    check(committed.revisions().pixels == 1 &&
              committed.revisions().metadata == 1 &&
              committed.revisions().selection == 0,
          "Categorized revisions");
    check(committed.data().dataset.plane({0, 0, 0}) ==
                  source.data().dataset.plane({0, 0, 0}) &&
              committed.data().dataset.plane({1, 1, 2}) ==
                  preview.data().dataset.plane({1, 1, 2}),
          "Commit shares unchanged and prepared plane buffers");
    check(committed.data().dataset.calibration().spacing == calibration.spacing,
          "Pixel and calibration changes commit together");
    auto display = committed.display();
    display.t = 1;
    display.c = 1;
    display.crosshair = {4, 3, 2};
    display.zoom = 2;
    doc.set_display(display);
    doc.undo();
    check(!doc.snapshot().dirty() &&
              doc.snapshot().display().crosshair == display.crosshair &&
              doc.snapshot().display().zoom == 2,
          "Undo preserves display and save identity");
    doc.redo();
    doc.mark_saved();
    check(!doc.snapshot().dirty(), "Save marks the data identity");
    rejects([&] { doc.commit(edit); });
    auto noop = doc.begin_edit();
    doc.commit(noop);
    check(doc.undo_steps() == 1 && !doc.snapshot().dirty(),
          "No-op is not an edit");

    auto aborted = doc.begin_edit();
    aborted.set_title("discarded");
    aborted.rollback();
    rejects([&] { doc.commit(aborted); });
    check(doc.snapshot().data().metadata.title == "Edited",
          "Rollback preserves source");
    Document other(fixture());
    auto foreign = doc.begin_edit();
    rejects([&] { other.commit(foreign); });
    check(foreign.active(), "Wrong owner leaves transaction retryable");
    auto collidingIdentity =
        std::make_shared<const DocumentIdentity>(DocumentIdentity{source.id()});
    DocumentSnapshot colliding(
        collidingIdentity, source.revisions(),
        std::make_shared<const DocumentData>(source.data()), source.display());
    auto collidingEdit = colliding.begin_edit();
    rejects([&] { doc.commit(collidingEdit); });
    check(collidingEdit.active(),
          "Equal numeric IDs cannot authorize a foreign owner token");
    auto stale = doc.begin_edit();
    stale.set_title("stale");
    doc.select(Selection("rectangle", {{1, 1}, {4, 3}}));
    rejects<RevisionConflict>([&] { doc.commit(stale); });
    check(stale.active() && !doc.snapshot().dirty() && doc.undo_steps() == 1,
          "Selection conflict does not dirty pixels");
    auto viewIndependent = doc.begin_edit();
    viewIndependent.set_property("display-independent", true);
    doc.set_display(display);
    doc.commit(viewIndependent);

    Document merged(fixture());
    auto first = merged.begin_edit(Change::Metadata);
    auto second = merged.begin_edit(Change::Metadata);
    first.replace_plane({0, 0, 1}, fill(Image(5, 4), 101));
    second.replace_plane({1, 1, 1}, fill(Image(5, 4), 202));
    merged.commit(first);
    merged.commit(second);
    check(merged.snapshot().data().dataset.plane({0, 0, 1})->at(0, 0) == 101 &&
              merged.snapshot().data().dataset.plane({1, 1, 1})->at(0, 0) ==
                  202,
          "Narrow dependencies merge touched planes without lost updates");
    auto propertiesA = merged.begin_edit(Change::Pixels);
    auto propertiesB = merged.begin_edit(Change::Pixels);
    propertiesA.set_property("a", std::int64_t(1));
    propertiesB.set_property("b", false);
    merged.commit(propertiesA);
    merged.commit(propertiesB);
    check(merged.snapshot().data().metadata.properties.size() == 2,
          "Metadata patches preserve untouched properties");
    auto invalid = merged.begin_edit();
    rejects([&] { invalid.set_property("", true); });
    rejects([&] {
      invalid.set_property("nan", std::numeric_limits<double>::quiet_NaN());
    });
    rejects([&] {
      invalid.set_rois({{"bad", Selection("point", {{1, 1}}), 2, {}, {}}});
    });
    rejects([&] { invalid.replace_plane({0, 0, 0}, Image(7, 4)); });
    check(merged.snapshot().data().metadata.properties.size() == 2,
          "Failed preparation cannot mutate the owner");
    invalid.set_rois({{"positioned", Selection("point", {{1, 1}}), 1, 1, 2}});
    merged.commit(invalid);
    check(merged.snapshot().data().rois[0].z == 2,
          "Positioned scientific ROIs");

    auto sourceLazy = std::make_shared<LazySource>();
    Document lazy{Dataset(sourceLazy)};
    auto lazySnapshot = lazy.snapshot();
    auto lazyEdit = lazy.begin_edit();
    lazyEdit.set_title("metadata only");
    lazy.commit(lazyEdit);
    check(
        sourceLazy->reads == 0 && !lazySnapshot.data().dataset.shared_planes(),
        "Inspection and metadata editing preserve lazy storage without reads");
    auto lazyPixels = lazy.begin_edit();
    lazyPixels.replace_plane({0, 0, 0}, fill(Image(5, 4), 7));
    lazy.commit(lazyPixels);
    check(sourceLazy->reads == 0 &&
              lazy.snapshot().data().dataset.plane({0, 0, 0})->at(0, 0) == 7,
          "Lazy plane replacement does not materialize the source");

    Document records{Dataset(sourceLazy)};
    MetadataObject style{{"color", std::string("red")},
                         {"id", std::numeric_limits<std::uint64_t>::max()}};
    auto nested = metadata_object(
        {{"values",
          metadata_array({std::monostate{}, false, metadata_object(style)})}});
    auto recordEdit = records.begin_edit();
    recordEdit.set_property("nested", nested);
    recordEdit.set_extension("vendor", nested);
    Selection styled("point", {{1, 1}});
    styled.attributes = style;
    recordEdit.select(styled);
    recordEdit.set_rois({{"styled", styled, 1, 1, 2, style}});
    recordEdit.set_overlays({style});
    FourierState spectrum(fft2(Image(5, 4)), std::array<int, 2>{4, 5});
    recordEdit.set_spectrum(spectrum);
    auto preparedRecord = recordEdit.preview();
    records.commit(recordEdit);
    auto retainedRecord = records.snapshot();
    check(sourceLazy->reads == 0 &&
              metadata_equal(
                  retainedRecord.data().metadata.properties.at("nested"),
                  nested) &&
              retainedRecord.data().spectrum->shares_storage(spectrum) &&
              metadata_equal(retainedRecord.data().rois[0].attributes, style) &&
              metadata_equal(retainedRecord.data().overlays[0], style),
          "Native records and spectra retain lazy data without reads");
    auto sameRecord = records.begin_edit();
    sameRecord.set_property(
        "nested", metadata_object(
                      {{"values", metadata_array({std::monostate{}, false,
                                                  metadata_object(style)})}}));
    records.commit(sameRecord);
    check(records.undo_steps() == 1,
          "Equivalent newly allocated metadata trees are not edits");
    auto invalidRecord = records.begin_edit();
    rejects([&] {
      invalidRecord.set_extension(
          "bad", metadata_object(
                     {{"value", std::numeric_limits<double>::infinity()}}));
    });
    rejects([&] {
      invalidRecord.set_property("null container",
                                 std::shared_ptr<const MetadataTree>{});
    });
    auto recordDisplay = retainedRecord.display();
    recordDisplay.properties = {{"lut", std::string("Fire")}};
    records.set_display(recordDisplay);
    auto changeRecordPixels = records.begin_edit();
    changeRecordPixels.replace_plane({0, 0, 0}, fill(Image(5, 4), 10));
    records.commit(changeRecordPixels);
    check(!records.snapshot().data().spectrum,
          "Pixel replacement invalidates the derived Fourier state");
    records.undo();
    check(records.snapshot().data().spectrum->shares_storage(spectrum) &&
              metadata_equal(records.snapshot().display().properties,
                             recordDisplay.properties) &&
              preparedRecord.data().spectrum->shares_storage(spectrum),
          "Undo and retained previews share Fourier buffers and preserve "
          "display");
    auto nextSelection = records.begin_edit();
    nextSelection.select(Selection("point", {{2, 2}}));
    records.commit(nextSelection);
    check(records.snapshot().data().previous_selection == styled,
          "Native selections retain previous geometry and styles");

    auto mask =
        Selection("rectangle", {{-1e100, -1e100}, {1e100, 1e100}}).mask(5, 4);
    check(mask.at(4, 3) == 255 &&
              Selection::from_mask(mask).mask(5, 4).pixels == mask.pixels,
          "Clipped geometry and packed masks agree");
    auto retained = [&] {
      Document temporary(fixture());
      return temporary.snapshot();
    }();
    check(retained.data().dataset.plane({0, 0, 0})->at(0, 0) == 0,
          "Snapshots retain pixels after owner destruction");
    auto retainedEdit = retained.begin_edit();
    rejects([&] { other.commit(retainedEdit); });

    std::atomic<bool> readerFailed{false};
    std::thread reader([&] {
      for (int i = 0; i < 100; ++i) {
        auto snapshot = merged.snapshot();
        if (snapshot.data().dataset.plane({0, 0, 1})->at(0, 0) != 101)
          readerFailed = true;
      }
    });
    for (int i = 0; i < 30; ++i) {
      auto tx = merged.begin_edit();
      tx.set_property("iteration", std::int64_t(i));
      merged.commit(tx);
    }
    reader.join();
    check(!readerFailed && merged.undo_steps() == 8,
          "Concurrent immutable reads and bounded history");
    merged.set_history_limit(2);
    check(merged.undo_steps() == 2, "Lower history limit prunes immediately");
    std::cout << "Native document contracts passed\n";
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
