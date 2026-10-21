#include <imagejxx/application.hpp>
#include <algorithm>
#include <iostream>
#include <limits>
#include <type_traits>

using namespace imagejxx;
namespace {
std::size_t checks = 0, groups = 0;
void check(bool value, const char *message) {
  ++checks;
  if (!value)
    throw std::runtime_error(message);
}
template <class E = std::exception, class F> void rejects(F function) {
  bool rejected = false;
  try { function(); } catch (const E &) { rejected = true; }
  check(rejected, "Expected history-policy refusal");
}
Image plane(double value) {
  Image result(4, 3);
  std::fill(result.pixels.begin(), result.pixels.end(), value);
  return result;
}
Dataset fixture() { return Dataset::from_stack({plane(0), plane(0)}); }
std::shared_ptr<Document> edited(const char *title = "Owner") {
  auto document = std::make_shared<Document>(fixture(), title);
  for (int i = 0; i != 3; ++i) {
    auto edit = document->begin_edit();
    edit.set_title(std::string(title) + std::to_string(i));
    document->commit(edit);
  }
  return document;
}
void same_history(const HistoryInfo &a, const HistoryInfo &b) {
  check(a.undo_steps == b.undo_steps && a.redo_steps == b.redo_steps &&
            a.step_limit == b.step_limit && a.pixel_budget == b.pixel_budget,
        "History counts/policy changed on refusal");
  check(a.pixels.logical_bytes == b.pixels.logical_bytes &&
            a.pixels.current_bytes == b.pixels.current_bytes &&
            a.pixels.retained_bytes == b.pixels.retained_bytes &&
            a.pixels.obsolete_bytes == b.pixels.obsolete_bytes &&
            a.pixels.retained_planes == b.pixels.retained_planes &&
            a.pixels.exact == b.pixels.exact,
        "History storage changed on refusal");
}
void same_snapshot(const DocumentSnapshot &a, const DocumentSnapshot &b) {
  check(a.same_document(b) && a.shares_data(b) &&
            a.revisions().matches(b.revisions(), Change::All) &&
            a.dirty() == b.dirty() && a.data_identity() == b.data_identity(),
        "Policy changed document data/revisions/save identity");
  const auto &x = a.display(), &y = b.display();
  check(x.t == y.t && x.c == y.c && x.orthogonal == y.orthogonal &&
            x.linkedZoom == y.linkedZoom && x.crosshair == y.crosshair &&
            x.zoom == y.zoom && x.planeZoom == y.planeZoom && x.pan == y.pan &&
            x.revision == y.revision && metadata_equal(x.properties, y.properties),
        "Policy changed display on refusal");
}
void second_owner_rejection() {
  Application app;
  auto first = edited("First"), second = edited("Second");
  app.adopt_all({first, second});
  bool deny = false;
  auto lease = second->constrain([&](const DocumentData &, const DisplayState &) {
    if (deny) throw std::invalid_argument("Second owner refuses");
  });
  const auto a = first->snapshot(), b = second->snapshot();
  const auto ah = first->history(), bh = second->history();
  const auto ids = app.document_ids();
  const auto active = app.active_document_id();
  deny = true;
  // Input is intentionally reversed; lock/staging order remains ascending ID.
  rejects<std::invalid_argument>([&] {
    app.prepare_history_policies({second->id, first->id}, {1, 1});
  });
  same_snapshot(a, first->snapshot()); same_snapshot(b, second->snapshot());
  same_history(ah, first->history()); same_history(bh, second->history());
  check(app.document_ids() == ids && app.active_document_id() == active,
        "Refused history batch changed membership/activation");
}
void abandon_move_and_explicit_ids() {
  Application app;
  auto owner = edited();
  app.adopt(owner);
  const auto before = owner->snapshot();
  const auto history = owner->history();
  {
    auto prepared = app.prepare_history_policies({owner->id}, {1, 1});
    check(prepared.active() && !prepared.committed(), "Prepared batch state");
  }
  same_snapshot(before, owner->snapshot()); same_history(history, owner->history());
  {
    auto prepared = app.prepare_history_policies({owner->id}, {1, 1});
    prepared.unlock(); prepared.unlock(); prepared.commit();
    check(!prepared.active() && !prepared.committed(), "Unlocked batch must abort");
  }
  same_history(history, owner->history());
  rejects<std::invalid_argument>([&] { app.prepare_history_policies({}, {0, 0}); });
  rejects<std::invalid_argument>([&] { app.prepare_history_policies({0}, {1, 0}); });
  rejects<std::invalid_argument>([&] {
    app.prepare_history_policies({owner->id, owner->id}, {1, 0});
  });
  rejects<DocumentUnavailable>([&] {
    app.prepare_history_policies({std::numeric_limits<std::uint64_t>::max()}, {1, 0});
  });
  {
    auto empty = app.prepare_history_policies({}, {2, 3});
    check(empty.active(), "Valid empty batch is prepared");
    empty.commit(); empty.commit(); empty.unlock();
    check(empty.committed() && !empty.active(), "Empty batch commit state");
  }
  same_history(history, owner->history());
  Application other_app;
  auto other = edited("Other"); other_app.adopt(other);
  auto first = app.prepare_history_policies({owner->id}, {1, 0});
  auto second = other_app.prepare_history_policies({other->id}, {2, 0});
  PreparedHistoryPolicies moved(std::move(second));
  check(!second.active() && moved.active(), "Move construction transfers batch");
  first = std::move(moved); // releases the old app's locks before retirement
  check(!moved.active() && first.active(), "Move assignment transfers batch");
  same_history(history, app.history(owner->id));
  first.commit(); first.commit(); first.unlock();
  check(first.committed() && other_app.history(other->id).step_limit == 2 &&
            other_app.history(other->id).undo_steps == 2,
        "Moved batch published the intended owner exactly once");
  app.close(owner->id, true);
  rejects<DocumentUnavailable>([&] { app.prepare_history_policies({owner->id}, {1, 0}); });
}
void shared_planes_and_budget() {
  Application app;
  auto owner = std::make_shared<Document>(fixture());
  const auto external = owner->snapshot();
  {
    auto edit = owner->begin_edit(); edit.replace_plane({0, 0, 0}, plane(1)); owner->commit(edit);
  }
  {
    auto edit = owner->begin_edit(); edit.replace_plane({0, 0, 1}, plane(2)); owner->commit(edit);
  }
  {
    auto edit = owner->begin_edit(); edit.set_title("Third"); owner->commit(edit);
  }
  owner->undo();
  auto other = edited("Other");
  app.adopt_all({owner, other});
  const auto before = owner->snapshot(), other_before = other->snapshot();
  check(owner->undo_steps() == 2 && owner->redo_steps() == 1,
        "Shared-plane fixture has undo and redo");
  const auto bytes = plane(0).pixels.size() * sizeof(double);
  auto prepared = app.prepare_history_policies({other->id, owner->id}, {1, bytes});
  prepared.commit(); prepared.unlock();
  same_snapshot(before, owner->snapshot()); same_snapshot(other_before, other->snapshot());
  const auto h = owner->history();
  check(h.undo_steps == 1 && h.redo_steps == 1 && h.step_limit == 1 &&
            h.pixel_budget == bytes, "Both history directions/policy published");
  check(h.pixels.exact && h.pixels.current_bytes == 2 * bytes &&
            h.pixels.retained_bytes == 3 * bytes && h.pixels.obsolete_bytes == bytes &&
            h.pixels.retained_planes == 3,
        "Shared pixel accounting excludes externally retained snapshots");
  check(external.data().dataset.plane({0, 0, 0})->pixels.front() == 0 &&
            external.data().dataset.plane({0, 0, 1})->pixels.front() == 0,
        "External old snapshot remains immutable after pruning");
  check(owner->snapshot().data().dataset.plane({0, 0, 0}) ==
            before.data().dataset.plane({0, 0, 0}) &&
            owner->snapshot().data().dataset.plane({0, 0, 1}) ==
            before.data().dataset.plane({0, 0, 1}),
        "History policy retains current shared plane identities");
  owner->undo();
  check(owner->snapshot().data().dataset.plane({0, 0, 0})->pixels.front() == 1 &&
            owner->snapshot().data().dataset.plane({0, 0, 1})->pixels.front() == 0,
        "Remaining undo entry is the newest retained edit");
  owner->redo();
  check(owner->snapshot().data().dataset.plane({0, 0, 1})->pixels.front() == 2,
        "Remaining redo restores current samples");
}
void adoption_refuses_later_owner() {
  Application app;
  const auto existing = app.create(fixture());
  auto first = edited("First"), second = edited("Second");
  auto captured = first->snapshot(); auto display = captured.display(); display.zoom = 3;
  const auto a = first->snapshot(), b = second->snapshot();
  const auto ah = first->history(), bh = second->history();
  bool deny = false;
  auto lease = second->constrain([&](const DocumentData &, const DisplayState &) {
    if (deny) throw std::invalid_argument("Later incoming owner refuses");
  });
  deny = true;
  rejects<std::invalid_argument>([&] {
    app.adopt_all_with_history_policy(
        {{first, DocumentInitialDisplay{captured, display}}, {second, std::nullopt}}, {1, 1});
  });
  same_snapshot(a, first->snapshot()); same_snapshot(b, second->snapshot());
  same_history(ah, first->history()); same_history(bh, second->history());
  check(app.document_ids() == std::vector<std::uint64_t>{existing} &&
            app.active_document_id() == existing,
        "Later rejection preserves membership and initial display");
  rejects<std::invalid_argument>([&] {
    app.adopt_all_with_history_policy({{first, std::nullopt}, {nullptr, std::nullopt}}, {1, 0});
  });
  rejects<std::invalid_argument>([&] {
    app.adopt_all_with_history_policy({{first, std::nullopt}, {first, std::nullopt}}, {1, 0});
  });
  auto closed = std::make_shared<Document>(fixture()); app.adopt(closed); app.close(closed->id);
  rejects<DocumentUnavailable>([&] {
    app.adopt_all_with_history_policy({{first, std::nullopt}, {closed, std::nullopt}}, {1, 0});
  });
  same_snapshot(a, first->snapshot()); same_history(ah, first->history());
}
void initial_display_refusal_and_success() {
  Application app;
  const auto existing = app.create(fixture());
  auto owner = edited();
  auto stale = owner->snapshot(); auto display = stale.display(); display.zoom = 3;
  owner->select(Selection("rectangle", {{0, 0}, {2, 2}}));
  const auto before = owner->snapshot();
  const auto history = owner->history();
  rejects<RevisionConflict>([&] {
    app.adopt_all_with_history_policy({{owner, DocumentInitialDisplay{stale, display}}}, {1, 0});
  });
  same_snapshot(before, owner->snapshot()); same_history(history, owner->history());
  auto foreign = std::make_shared<Document>(fixture());
  rejects<std::invalid_argument>([&] {
    app.adopt_all_with_history_policy(
        {{owner, DocumentInitialDisplay{foreign->snapshot(), display}}}, {1, 0});
  });
  auto invalid = display; invalid.zoom = std::numeric_limits<double>::quiet_NaN();
  rejects<std::invalid_argument>([&] {
    app.adopt_all_with_history_policy({{owner, DocumentInitialDisplay{before, invalid}}}, {1, 0});
  });
  auto lease = owner->constrain([](const DocumentData &, const DisplayState &candidate) {
    if (candidate.zoom > 4) throw std::invalid_argument("Zoom constraint");
  });
  invalid.zoom = 5;
  rejects<std::invalid_argument>([&] {
    app.adopt_all_with_history_policy({{owner, DocumentInitialDisplay{before, invalid}}}, {1, 0});
  });
  same_snapshot(before, owner->snapshot()); same_history(history, owner->history());
  check(app.document_ids() == std::vector<std::uint64_t>{existing},
        "Initial-display failures preserve membership");
  auto later = edited("Later");
  auto later_display = later->snapshot().display(); later_display.zoom = 2;
  later->set_display(later_display);
  const auto later_before = later->snapshot();
  auto adopted = app.adopt_all_with_history_policy(
      {{later, std::nullopt}, {owner, DocumentInitialDisplay{before, display}}}, {1, 0}, false);
  check(adopted.document_ids == std::vector<std::uint64_t>{later->id, owner->id} &&
            app.document_ids() == std::vector<std::uint64_t>{existing, later->id, owner->id} &&
            app.active_document_id() == existing,
        "Typed adoption retains input order and make_active=false");
  check(adopted.retirement.committed() && !adopted.retirement.active(),
        "Returned adoption retirement is committed and unlocked");
  const auto after = owner->snapshot();
  check(after.shares_data(before) && after.data_identity() == before.data_identity() &&
            after.dirty() == before.dirty() &&
            after.revisions().matches(before.revisions(), Change::Data),
        "Initial display preserves data, dirty identity and data revisions");
  check(after.display().zoom == 3 && after.display().revision == before.display().revision + 1 &&
            after.revisions().display == before.revisions().display + 1,
        "Initial display publishes exactly one Display revision");
  same_snapshot(later_before, later->snapshot());
  check(owner->history_limit() == 1 && owner->undo_steps() == 1 &&
            later->history_limit() == 1 && later->undo_steps() == 1,
        "Adoption publishes history policy for every incoming owner");
  auto empty_adoption = app.adopt_all_with_history_policy({}, {1, 0});
  check(empty_adoption.document_ids.empty() && !empty_adoption.retirement.active() &&
            !empty_adoption.retirement.committed() &&
            app.active_document_id() == existing, "Empty typed adoption is a no-op");
  rejects<std::invalid_argument>([&] { app.adopt_all_with_history_policy({}, {0, 0}); });
  Application empty;
  auto one = edited("One"), two = edited("Two");
  empty.adopt_all_with_history_policy({{two, std::nullopt}, {one, std::nullopt}}, {1, 0});
  check(empty.active_document_id() == one->id, "Default activation is the last input owner");
  Application unactivated;
  auto initial_first = edited("InitialFirst"), initial_last = edited("InitialLast");
  unactivated.adopt_all_with_history_policy(
      {{initial_last, std::nullopt}, {initial_first, std::nullopt}}, {1, 0}, false);
  check(unactivated.active_document_id() == initial_last->id,
        "An empty application with make_active=false selects the first input");
}
struct Reentry {
  Application *application = nullptr;
  std::weak_ptr<Document> owner;
  int destroyed = 0;
  bool reentered = false, failed = false;
};
class ReentrantSource final : public PlaneSource {
public:
  explicit ReentrantSource(std::shared_ptr<Reentry> state) : state_(std::move(state)) {}
  ~ReentrantSource() override {
    ++state_->destroyed;
    try {
      (void)state_->application->document_ids();
      if (auto owner = state_->owner.lock()) (void)owner->history();
      state_->reentered = true;
    } catch (...) { state_->failed = true; }
  }
  DatasetShape shape() const override { return {1, 1, 1, 3, 4, 1}; }
  PixelType pixel_type() const override { return PixelType::Float64; }
  std::shared_ptr<const Image> read_plane(PlanePosition) const override {
    return std::make_shared<const Image>(plane(9));
  }
private:
  std::shared_ptr<Reentry> state_;
};
std::shared_ptr<Document> retiring_owner(Application &app, const std::shared_ptr<Reentry> &state,
                                        bool register_owner = true) {
  state->application = &app;
  auto owner = std::make_shared<Document>(Dataset(std::make_shared<ReentrantSource>(state)));
  state->owner = owner;
  {
    auto edit = owner->begin_edit(); edit.replace_dataset(Dataset::from_stack({plane(1)}));
    owner->commit(edit);
  }
  {
    auto edit = owner->begin_edit(); edit.replace_plane({}, plane(2)); owner->commit(edit);
  }
  if (register_owner) app.adopt(owner);
  check(state->destroyed == 0 && owner->undo_steps() == 2,
        "Custom source is retained only by obsolete history");
  return owner;
}
void retirement_after_unlock() {
  Application app;
  auto state = std::make_shared<Reentry>(); auto owner = retiring_owner(app, state);
  {
    auto prepared = app.prepare_history_policies({owner->id}, {1, 0});
    check(state->destroyed == 0, "Preparation does not retire live history");
    prepared.commit();
    check(state->destroyed == 0, "Commit retains old histories under locks");
    prepared.unlock();
    check(state->destroyed == 0 && app.history(owner->id).undo_steps == 1,
          "Explicit unlock keeps retired histories alive");
  }
  check(state->destroyed == 1 && state->reentered && !state->failed,
        "Retired source can reenter Application and Document after unlock");
  auto second_state = std::make_shared<Reentry>(); auto second = retiring_owner(app, second_state);
  {
    auto prepared = app.prepare_history_policies({second->id}, {1, 0});
    prepared.commit();
    prepared = PreparedHistoryPolicies{};
    check(second_state->destroyed == 1 && second_state->reentered && !second_state->failed,
          "Move assignment unlocks old state before retirement");
  }
  auto incoming_state = std::make_shared<Reentry>();
  auto incoming = retiring_owner(app, incoming_state, false);
  {
    auto adopted = app.adopt_all_with_history_policy({{incoming, std::nullopt}}, {1, 0});
    check(incoming_state->destroyed == 0 && adopted.retirement.committed() &&
              !adopted.retirement.active() && adopted.document_ids ==
                  std::vector<std::uint64_t>{incoming->id},
          "Returned adoption holder defers custom-source retirement");
    check(app.contains(incoming->id) && incoming->undo_steps() == 1,
          "Returned adoption holder is unlocked for frontend attachment");
  }
  check(incoming_state->destroyed == 1 && incoming_state->reentered && !incoming_state->failed,
        "Adoption result destruction retires sources after attachment");
  auto replaced_state = std::make_shared<Reentry>();
  auto replaced = retiring_owner(app, replaced_state, false);
  auto adopted = app.adopt_all_with_history_policy({{replaced, std::nullopt}}, {1, 0});
  check(replaced_state->destroyed == 0, "Adoption replacement fixture retains its source");
  adopted = HistoryPolicyAdoption{};
  check(replaced_state->destroyed == 1 && replaced_state->reentered && !replaced_state->failed,
        "Adoption result replacement unlocks before retiring sources");
}
template <class F> void group(const char *name, F function) {
  function(); ++groups; std::cout << "HISTORY_POLICY_CASE " << name << " PASS\n";
}
} // namespace
int main() {
  static_assert(!std::is_copy_constructible_v<PreparedHistoryPolicies>);
  static_assert(!std::is_copy_assignable_v<PreparedHistoryPolicies>);
  static_assert(std::is_nothrow_move_constructible_v<PreparedHistoryPolicies>);
  static_assert(std::is_nothrow_move_assignable_v<PreparedHistoryPolicies>);
  static_assert(noexcept(std::declval<PreparedHistoryPolicies &>().commit()));
  static_assert(noexcept(std::declval<PreparedHistoryPolicies &>().unlock()));
  static_assert(!std::is_copy_constructible_v<HistoryPolicyAdoption>);
  static_assert(!std::is_copy_assignable_v<HistoryPolicyAdoption>);
  static_assert(std::is_nothrow_move_constructible_v<HistoryPolicyAdoption>);
  static_assert(std::is_nothrow_move_assignable_v<HistoryPolicyAdoption>);
  try {
    group("second_owner_rejection", second_owner_rejection);
    group("abandon_move_explicit_ids", abandon_move_and_explicit_ids);
    group("shared_planes_and_budget", shared_planes_and_budget);
    group("adoption_later_owner_refusal", adoption_refuses_later_owner);
    group("initial_display_refusal_success", initial_display_refusal_and_success);
    group("retirement_after_unlock", retirement_after_unlock);
    std::cout << "HISTORY_POLICY_RESULT groups=" << groups << " checks=" << checks << " PASS\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "HISTORY_POLICY_RESULT groups=" << groups << " checks=" << checks
              << " FAIL: " << error.what() << '\n';
    return 1;
  }
}
