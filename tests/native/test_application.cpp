#include <atomic>
#include <imagejxx/application.hpp>
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
  check(rejected, "Invalid application action was accepted");
}
Dataset fixture() { return Dataset::from_stack({Image(5, 4), Image(5, 4)}); }
void guarded_display_membership() {
  Application application;
  auto owner = std::make_shared<Document>(fixture());
  const auto id = application.adopt(owner);
  auto captured = application.snapshot(id);
  const auto other = application.create(fixture());
  auto display = captured.display();
  display.zoom = 3;
  application.set_display_if_current(captured, display);
  check(application.active_document_id() == other &&
            application.snapshot(id).display().zoom == 3 &&
            application.snapshot(other).display().zoom == 0,
        "Guarded display resolves its captured owner without retargeting activation");
  captured = application.snapshot(id);
  owner->select(Selection("rectangle", {{0, 0}, {2, 2}}));
  rejects<RevisionConflict>(
      [&] { application.set_display_if_current(captured, display); });
  check(application.snapshot(id).display().zoom == 3,
        "External native writers invalidate guarded application publication");
  captured = application.snapshot(id);
  application.close(id);
  rejects<DocumentUnavailable>(
      [&] { application.set_display_if_current(captured, display); });
  check(application.active_document_id() == other &&
            application.snapshot(other).display().zoom == 0,
        "A closed owner cannot publish into another active document");
  Document foreign(fixture());
  rejects<DocumentUnavailable>([&] {
    application.set_display_if_current(foreign.snapshot(), foreign.snapshot().display());
  });
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
} // namespace

void testPublicationConstraints() {
  Document document(fixture(), "Initial");
  auto source = document.snapshot();
  auto lease = document.constrain(
      [](const DocumentData &data, const DisplayState &display) {
        if (data.metadata.title == "Forbidden" || display.zoom > 4)
          throw std::invalid_argument("Frontend constraint");
      });
  auto rejected = source.begin_edit();
  rejected.set_title("Forbidden");
  rejects<std::invalid_argument>([&] { document.commit(rejected); });
  check(rejected.active() && document.snapshot().shares_data(source) &&
            document.undo_steps() == 0,
        "Rejected constraints leave publication and history unchanged");
  auto display = source.display();
  display.zoom = 5;
  rejects<std::invalid_argument>([&] { document.set_display(display); });
  check(document.snapshot().revisions().display == source.revisions().display,
        "Rejected display constraints do not advance revisions");
  lease.reset();
  document.commit(rejected);
  check(!rejected.active(),
        "Expired frontend constraints release the native owner");
  auto undoConstraint =
      document.constrain([](const DocumentData &data, const DisplayState &) {
        if (data.metadata.title == "Initial")
          throw std::invalid_argument("Old title");
      });
  rejects<std::invalid_argument>([&] { document.undo(); });
  check(document.undo_steps() == 1 && document.redo_steps() == 0 &&
            document.snapshot().data().metadata.title == "Forbidden",
        "Undo validates the candidate before altering history");
  undoConstraint.reset();
  document.undo();
  check(!document.snapshot().dirty(),
        "Undo can reach the original save identity");
  auto retained = document.snapshot();
  document.checkpoint();
  check(
      document.snapshot().dirty() && document.undo_steps() == 1 &&
          document.snapshot().shares_data(retained),
      "Initial-publication checkpoints share data and record a save identity");
  document.undo();
  auto records = document.begin_edit();
  records.set_record_layout({{"fields", std::vector<std::string>{"origin"}}});
  records.set_previous_selection(Selection::from_mask(Image(2, 2)));
  document.commit(records);
  auto recorded = document.snapshot();
  check(std::get<std::vector<std::string>>(
            recorded.data().metadata.record_layout.at("fields")) ==
                std::vector<std::string>{"origin"} &&
            recorded.data().previous_selection->mask_width() == 2,
        "Codec layout and previous geometry retain their native records");
  document.undo();
  check(document.snapshot().data().metadata.record_layout.empty() &&
            !document.snapshot().data().previous_selection &&
            recorded.data().previous_selection->mask_width() == 2,
        "Record edits undo atomically and retained snapshots remain immutable");
}

int main() {
  try {
    guarded_display_membership();
    testPublicationConstraints();
    static_assert(std::variant_size_v<MetadataValue> == 8);
    static_assert(
        std::is_nothrow_constructible_v<MetadataValue, std::uint32_t>);
    static_assert(std::is_same_v<std::variant_alternative_t<2, MetadataValue>,
                                 std::int64_t>);
    MetadataValue small = std::uint32_t(4);
    check(std::get<std::int64_t>(small) == 4 &&
              std::get<std::int64_t>(MetadataValue(std::uint16_t(4))) == 4 &&
              std::get<std::int64_t>(MetadataValue(std::uint8_t(4))) == 4,
          "Small unsigned metadata retains its legacy int64 alternative");
    small = static_cast<unsigned long>(4);
    check(metadata_equal(small, std::int64_t(4)),
          "Unsigned long metadata works on 32-bit and 64-bit long platforms");
    check(metadata_equal(std::vector<std::string>{"tag"},
                         metadata_array({std::string("tag")})) &&
              metadata_equal(metadata_array({std::string("tag")}),
                             std::vector<std::string>{"tag"}) &&
              metadata_equal(metadata_array({}), std::vector<std::string>{}) &&
              !metadata_equal(metadata_array({std::int64_t(4)}),
                              std::vector<std::string>{"4"}),
          "Decoded string lists and recursive string arrays compare by value");
    small = std::string("text");
    check(
        std::visit([](const auto &) { return true; }, small) &&
            std::get_if<std::string>(&small) &&
            std::holds_alternative<std::string>(small),
        "Standard variant construction, assignment, access and visits survive");
    check(metadata_equal(std::int64_t(4), std::uint64_t(4)) &&
              metadata_equal(std::uint64_t(4), std::int64_t(4)) &&
              !metadata_equal(std::int64_t(-1),
                              std::numeric_limits<std::uint64_t>::max()) &&
              !metadata_equal(std::numeric_limits<std::uint64_t>::max(),
                              std::int64_t(-1)) &&
              !metadata_equal(true, std::int64_t(1)),
          "Integer value equality handles signedness without wraparound or "
          "bool coercion");
    Application app;
    check(app.active_document_id() == 0 && app.document_ids().empty() &&
              !app.contains(0),
          "Empty application has no active document");
    rejects<DocumentUnavailable>([&] { app.snapshot(); });
    rejects<DocumentUnavailable>([&] { app.activate(42); });
    rejects([&] { app.adopt(nullptr); });
    auto doc = std::make_shared<Document>(fixture(), "Original");
    auto id = app.adopt(doc);
    check(id != 0 && app.active_document_id() == id &&
              app.snapshot().shares_data(doc->snapshot()),
          "Application adopts the same authoritative document");
    rejects([&] { app.adopt(doc); });
    auto source = app.snapshot();
    auto edit = doc->begin_edit();
    edit.replace_plane({0, 0, 1}, fill(Image(5, 4), 42));
    edit.set_property("small-unsigned", std::uint32_t(4));
    edit.set_property("unsigned", std::uint64_t(4));
    edit.set_property("tags", metadata_array({std::string("tag")}));
    auto view = source.display();
    view.zoom = 2;
    view.crosshair = {2, 1, 1};
    app.commit(edit, view);
    check(doc->snapshot().data().dataset.plane({0, 0, 1})->at(0, 0) == 42 &&
              doc->undo_steps() == 1 && app.snapshot().display().zoom == 2 &&
              app.snapshot().revisions().display == 1,
          "Application publishes pixels and display to the adopted owner");
    rejects([&] { app.close(id); });
    check(app.contains(id), "Unsaved close preserves membership");
    app.mark_saved(source);
    check(app.snapshot().dirty(),
          "Saving an older snapshot keeps newer edits dirty");
    auto saved = app.snapshot();
    app.mark_saved(saved);
    check(!doc->snapshot().dirty(), "Snapshot save targets the adopted owner");
    auto sameInteger = app.begin_edit();
    sameInteger.set_property("unsigned", std::int64_t(4));
    sameInteger.set_property("tags", std::vector<std::string>{"tag"});
    app.commit(sameInteger);
    check(app.snapshot().shares_data(saved) && !app.snapshot().dirty(),
          "Decoded equal unsigned metadata creates no edit");
    app.undo(id);
    check(app.snapshot().dirty() && app.snapshot().display().zoom == 2,
          "Undo retains display and the saved identity");
    app.redo(id);
    check(!app.snapshot().dirty(), "Redo reaches the saved data identity");

    auto stale = app.begin_edit(id);
    stale.set_title("stale");
    app.select(Selection("rectangle", {{1, 1}, {4, 3}}), id);
    rejects<RevisionConflict>([&] { app.commit(stale); });
    check(stale.active(), "Stale edit remains inspectable");
    auto foreignIdentity =
        std::make_shared<const DocumentIdentity>(DocumentIdentity{id});
    DocumentSnapshot foreign(
        foreignIdentity, app.snapshot().revisions(),
        std::make_shared<const DocumentData>(source.data()), source.display());
    auto foreignEdit = foreign.begin_edit();
    rejects([&] { app.commit(foreignEdit); });
    check(foreignEdit.active(),
          "Colliding IDs cannot authorize a foreign edit");
    auto preview = app.begin_edit(id);
    rejects([&] { app.mark_saved(preview.preview()); });

    auto a = app.create(fixture(), "Second");
    auto b = app.create(fixture(), "Third");
    app.activate(b);
    app.close(a);
    check(app.active_document_id() == b,
          "Closing an earlier tab retains active identity");
    app.close(b);
    check(app.active_document_id() == id,
          "Closing active tab selects a remaining neighbor");
    auto retained = app.snapshot(id);
    auto closedEdit = app.begin_edit(id);
    app.close(id);
    rejects<DocumentUnavailable>([&] { app.snapshot(id); });
    rejects<DocumentUnavailable>([&] { app.commit(closedEdit); });
    rejects<DocumentUnavailable>([&] { app.adopt(doc); });
    check(closedEdit.active() && !app.contains(id) &&
              retained.data().dataset.plane({0, 0, 1})->at(0, 0) == 42 &&
              doc->snapshot().shares_data(retained),
          "Close rejects live access while snapshots and external owners "
          "survive");

    auto lazy = std::make_shared<LazySource>();
    Application lazyApp;
    auto lazyId = lazyApp.create(Dataset(lazy));
    auto lazyEdit = lazyApp.begin_edit(lazyId);
    lazyEdit.set_property("instrument", std::string("test"));
    lazyApp.commit(lazyEdit);
    lazyApp.set_history_policy(8, 160, lazyId);
    auto lazySnapshot = lazyApp.snapshot(lazyId);
    check(lazy->reads == 0 && !lazyApp.history(lazyId).pixels.exact &&
              lazyApp.history(lazyId).pixels.logical_bytes == 1920,
          "Lazy metadata and history inspection never read pixels");
    check(lazySnapshot.data().dataset.plane({1, 1, 2})->at(0, 0) == 112 &&
              lazy->reads == 1,
          "One targeted lazy plane read does not materialize a volume");
    lazyApp.close(lazyId, true);

    Application budget;
    auto budgetId = budget.create(fixture());
    budget.set_history_policy(8, 160);
    auto before = budget.snapshot();
    for (int i = 1; i <= 3; ++i) {
      auto tx = budget.begin_edit();
      tx.replace_plane({0, 0, 1}, fill(Image(5, 4), i));
      budget.commit(tx);
    }
    auto history = budget.history();
    check(
        history.undo_steps == 1 && history.pixels.exact &&
            history.pixels.logical_bytes == 320 &&
            history.pixels.current_bytes == 320 &&
            history.pixels.retained_bytes == 480 &&
            history.pixels.obsolete_bytes == 160 &&
            history.pixels.retained_planes == 3,
        "Native budget counts shared planes once and prunes obsolete buffers");
    budget.undo();
    budget.redo();
    rejects([&] { budget.set_history_policy(0, 1); });
    check(budget.history().pixel_budget == 160 &&
              budget.history().undo_steps == 1,
          "Invalid history settings preserve the policy and history");
    budget.set_history_policy(8, 159);
    check(budget.history().undo_steps == 0 &&
              budget.history().pixels.obsolete_bytes == 0 &&
              before.data().dataset.plane({0, 0, 1})->at(0, 0) == 0,
          "Pruning excludes external readers and keeps their snapshots valid");
    auto atomic = budget.begin_edit();
    atomic.set_title("must not commit");
    auto invalid = budget.snapshot().display();
    invalid.zoom = std::numeric_limits<double>::quiet_NaN();
    auto unchanged = budget.snapshot();
    rejects([&] { budget.commit(atomic, invalid); });
    check(atomic.active() && budget.snapshot().shares_data(unchanged) &&
              budget.history().undo_steps == 0,
          "Invalid combined display/data publication is atomic");
    auto pending = budget.begin_edit();
    pending.set_title("Background");
    auto oldDisplay = budget.snapshot().display();
    auto currentDisplay = oldDisplay;
    currentDisplay.zoom = 7;
    budget.set_display(currentDisplay);
    budget.commit(pending, oldDisplay, true);
    check(budget.snapshot().display().zoom == 7,
          "Background publication preserves concurrent navigation");
    auto displayOnly = budget.begin_edit();
    currentDisplay.zoom = 8;
    auto steps = budget.history().undo_steps;
    auto dataIdentity = budget.snapshot().data_identity();
    budget.commit(displayOnly, currentDisplay);
    check(budget.history().undo_steps == steps &&
              budget.snapshot().data_identity() == dataIdentity,
          "Display-only transaction does not create data history");

    auto rich = budget.begin_edit();
    rich.set_extension("unknown",
                       metadata_array({std::uint64_t(1) << 63, true}));
    rich.set_rois({Roi{"styled",
                       Selection("point", {{2, 1}}),
                       {},
                       {},
                       1,
                       {{"group", std::int64_t(2)}}}});
    rich.set_overlays(
        {{{"kind", std::string("text")}, {"text", std::string("retained")}}});
    rich.set_spectrum(
        FourierState(fft2(Image(5, 4)), std::array<int, 2>{4, 5}));
    budget.commit(rich);
    auto richSource = budget.snapshot();
    auto duplicateId = budget.duplicate(budgetId);
    auto duplicate = budget.snapshot(duplicateId);
    check(
        !duplicate.same_document(richSource) &&
            duplicate.data().dataset.shares_pixels(richSource.data().dataset) &&
            duplicate.data().metadata == richSource.data().metadata &&
            duplicate.data().rois == richSource.data().rois &&
            metadata_equal(duplicate.data().overlays[0],
                           richSource.data().overlays[0]) &&
            duplicate.data().spectrum->shares_storage(
                *richSource.data().spectrum) &&
            duplicate.display().zoom == richSource.display().zoom,
        "Import and duplication retain complete records with a new owner");

    Application concurrent;
    auto concurrentId = concurrent.create(fixture());
    std::atomic<bool> failed{false};
    std::vector<std::thread> workers;
    for (int worker = 0; worker < 4; ++worker)
      workers.emplace_back([&, worker] {
        try {
          for (int i = 0; i < 50; ++i) {
            auto tx = concurrent.begin_edit(concurrentId, Change::Pixels);
            tx.set_property(std::to_string(worker) + ":" + std::to_string(i),
                            std::int64_t(i));
            concurrent.commit(tx);
            concurrent.snapshot(concurrentId);
          }
        } catch (...) {
          failed = true;
        }
      });
    for (auto &worker : workers)
      worker.join();
    check(!failed &&
              concurrent.snapshot().data().metadata.properties.size() == 200 &&
              concurrent.history().pixels.retained_planes == 2,
          "Concurrent disjoint patches merge without copying pixel storage");
    std::cout << "Native application checks passed\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
