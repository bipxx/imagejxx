// Public-core SDK scenarios for opt-in atomic rank NewDocument attachment.
// All numerical expectations below are literal radius-zero cross results.
#include <imagejxx/application.hpp>
#include <imagejxx/rank_command.hpp>

#include <chrono>
#include <complex>
#include <iostream>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

using namespace imagejxx;
using namespace std::chrono_literals;
namespace {
std::size_t checks = 0, cases = 0;
std::string current;
void named(const char *name) { current = name; ++cases; }
void check(bool value, const char *message) {
  ++checks;
  if (!value) throw std::runtime_error(current + ": " + message);
}
template <class Error, class Function>
void rejects(Function work, const char *message) {
  bool caught = false;
  try { work(); } catch (const Error &) { caught = true; }
  check(caught, message);
}
const std::vector<double> input{1, 8, 3, 4, 9, 6, 7, 2, 5};
const std::vector<double> minimum{1, 1, 3, 1, 2, 3, 2, 2, 2};
std::shared_ptr<Document> owner(bool constant = false) {
  Image first(3, 3, 1, PixelType::UInt16), second(3, 3, 1, PixelType::UInt16);
  first.pixels = constant ? std::vector<double>(9, 7) : input;
  second.pixels.assign(9, 42);
  DocumentData data{Dataset::from_planes({1, 1, 2, 3, 3, 1},
                                         Stack{first, second}), {"Source", {}}};
  data.metadata.properties["source property"] = std::string("retained");
  data.metadata.extensions["source extension"] = std::int64_t(3);
  data.metadata.record_layout["source layout"] = true;
  data.selection = Selection("rectangle", {{0, 0}, {2, 2}});
  data.previous_selection = Selection("point", {{1, 1}});
  data.rois = {{"saved", Selection("rectangle", {{0, 0}, {1, 1}}), 0, 0, 0}};
  data.overlays = {{{"label", std::string("kept overlay")}}};
  data.spectrum = FourierState(Spectrum{3, 3,
      std::vector<std::complex<double>>(9)}, std::array<int, 2>{3, 3});
  return std::make_shared<Document>(std::move(data));
}
ResolvedRankFilterCommand command(const std::shared_ptr<Document> &source,
                                  OutputDestination destination = OutputDestination::NewDocument) {
  TargetRequest request;
  request.roi = RoiPolicy::WholePlane;
  request.destination = destination;
  return resolve_rank_command(source->snapshot(), request,
                              {RankFilterMethod::Minimum, 0});
}
PreparedOperation prepare(Application &app, const std::shared_ptr<Document> &source,
                           const ExecutionControl &control = {}) {
  const auto resolved = command(source);
  return app.prepare("rank_filter_imagej", resolved.target(),
                      resolved.parameters(), {}, control);
}
void source_unchanged(const std::shared_ptr<Document> &source,
                      const DocumentSnapshot &before,
                      std::size_t undo, std::size_t redo) {
  const auto after = source->snapshot();
  check(after.shares_data(before) &&
        after.revisions().matches(before.revisions(), Change::All) &&
        after.dirty() == before.dirty() && source->undo_steps() == undo &&
        source->redo_steps() == redo, "Source data, revisions, dirty or history changed");
}
void identity_titles_and_retained_records() {
  named("RS01_exact_candidate_identity_titles_records_and_history");
  Application app; auto source = owner(); app.adopt(source);
  const auto before = source->snapshot();
  auto prepared = prepare(app, source);
  check(!prepared.prepared_new_document(), "Rank staging was enabled implicitly");
  auto candidate = prepared.stage_new_document("Rank output");
  check(candidate && candidate != source && !app.contains(candidate->id) &&
        prepared.prepared_new_document() == candidate &&
        prepared.stage_new_document() == candidate &&
        prepared.stage_new_document("Rank output") == candidate,
        "Repeated staging did not return the exact unregistered owner");
  rejects<std::invalid_argument>([&] { prepared.stage_new_document("Other"); },
                                 "A different explicit staged title was accepted");
  const auto staged = candidate->snapshot();
  const auto &data = staged.data();
  check(data.metadata.title == "Rank output" && data.dataset.plane()->pixels == minimum &&
        data.dataset.plane({0, 0, 1}) == before.data().dataset.plane({0, 0, 1}),
        "Staged rank pixels/title/untargeted shared plane differ");
  check(metadata_equal(data.metadata.properties, before.data().metadata.properties) &&
        metadata_equal(data.metadata.extensions, before.data().metadata.extensions) &&
        metadata_equal(data.metadata.record_layout, before.data().metadata.record_layout) &&
        data.selection == before.data().selection && data.previous_selection == before.data().previous_selection &&
        data.rois == before.data().rois && data.overlays.size() == 1 &&
        metadata_equal(data.overlays[0], before.data().overlays[0]) && !data.spectrum,
        "Clone-and-edit annotations, metadata or cleared spectrum changed");
  check(staged.dirty() && candidate->undo_steps() == 1 && candidate->redo_steps() == 0,
        "Staged rank lost ordinary changed clone-and-edit history semantics");
  rejects<std::invalid_argument>([&] { app.publish(prepared, "Other"); },
                                 "Publication changed the frozen explicit title");
  check(prepared.active() && !app.contains(candidate->id), "Wrong-title refusal adopted or consumed output");
  auto result = app.publish(prepared);
  check(result.created && result.document == candidate && result.document_id == candidate->id &&
        app.active_document_id() == candidate->id && !prepared.active(),
        "Publication did not adopt and activate the exact staged owner");
  check(candidate->snapshot().data().dataset.plane()->pixels == minimum &&
        candidate->undo_steps() == 1 && candidate->snapshot().dirty(),
        "Adoption changed rank pixels/history/dirty");
  source_unchanged(source, before, 0, 0);
  candidate->undo();
  check(candidate->snapshot().data().dataset.plane()->pixels == input &&
        candidate->snapshot().data().metadata.title == "Source" &&
        candidate->snapshot().data().spectrum && !candidate->snapshot().dirty(),
        "Published output undo did not restore the complete initial clone");
  candidate->redo();
  check(candidate->snapshot().data().dataset.plane()->pixels == minimum &&
        candidate->snapshot().dirty(), "Published output redo did not restore rank edit");
  source_unchanged(source, before, 0, 0);
  rejects<std::invalid_argument>([&] { prepared.stage_new_document(); },
                                 "Consumed preparation staged again");
}
void default_title_and_generic_compatibility() {
  named("RS02_default_title_and_unstaged_generic_compatibility");
  Application app; auto source = owner(); app.adopt(source);
  auto prepared = prepare(app, source);
  auto candidate = prepared.stage_new_document();
  check(candidate->snapshot().data().metadata.title == "Source (rank_filter_imagej)" &&
        prepared.stage_new_document("Source (rank_filter_imagej)") == candidate,
        "Default rank title was not frozen using the generic naming policy");
  check(app.publish(prepared, "Source (rank_filter_imagej)").document == candidate,
        "Explicit matching default title refused publication");
  auto unstaged = prepare(app, source);
  check(!unstaged.prepared_new_document(), "Another rank preparation inherited a candidate");
  const auto output = app.publish(unstaged, "Legacy explicit rank");
  check(output.document->snapshot().data().metadata.title == "Legacy explicit rank" &&
        output.document->snapshot().data().dataset.plane()->pixels == minimum &&
        output.document->undo_steps() == 1, "Unstaged rank NewDocument behavior changed");
  TargetRequest request; request.destination = OutputDestination::NewDocument;
  request.roi = RoiPolicy::WholePlane;
  auto generic = app.prepare("add", app.resolve_target(request, source->id), {{"value", 1}});
  rejects<std::invalid_argument>([&] { generic.stage_new_document(); },
                                 "Non-rank operation opted into rank staging");
  check(!generic.prepared_new_document(), "Unsupported staging leaked a candidate");
  auto generic_output = app.publish(generic, "Generic");
  check(generic_output.document->undo_steps() == 1 &&
        generic_output.document->snapshot().data().dataset.plane()->at(0, 0) == 2,
        "Existing generic clone-and-commit behavior changed");
  const auto in_place_command = command(source, OutputDestination::InPlace);
  auto in_place = app.prepare("rank_filter_imagej", in_place_command.target(), in_place_command.parameters());
  rejects<std::invalid_argument>([&] { in_place.stage_new_document(); },
                                 "In-place rank request staged an output owner");
  auto axis = app.prepare_axis_plane_export({Plane::XY, 0}, source->id);
  auto axis_candidate = axis.prepared_new_document();
  rejects<std::invalid_argument>([&] { axis.stage_new_document(); },
                                 "Axis output silently changed its distinct staging contract");
  check(axis_candidate && !axis_candidate->snapshot().dirty() &&
        axis_candidate->undo_steps() == 0 && app.publish(axis).document == axis_candidate &&
        axis_candidate->undo_steps() == 0, "Fresh axis clean/empty-history semantics changed");
}
void allowed_presentation_and_policy() {
  named("RS03_candidate_and_source_presentation_policy_changes");
  Application app; auto source = owner(); app.adopt(source);
  auto prepared = prepare(app, source);
  auto candidate = prepared.stage_new_document("Configured");
  auto display = candidate->snapshot().display(); display.zoom = 2; display.crosshair = {2, 1, 1};
  candidate->set_display(display); candidate->set_history_policy(16, 4096);
  source->set_display(display); source->set_history_policy(16, 4096); source->mark_saved();
  const auto before = source->snapshot();
  const auto result = app.publish(prepared);
  check(result.document == candidate && candidate->snapshot().display().zoom == 2 &&
        candidate->snapshot().display().crosshair == display.crosshair &&
        candidate->history_limit() == 16 && candidate->history_pixel_budget() == 4096 &&
        candidate->undo_steps() == 1, "Harmless candidate display/history-policy setup was invalidated");
  source_unchanged(source, before, 0, 0);
}
void configured_history_pruning() {
  named("RS11_configured_positive_budget_pruning_and_unbounded_budget");
  for (int scenario = 0; scenario < 3; ++scenario) {
    Application app; auto source = owner(); app.adopt(source);
    const auto before = source->snapshot();
    auto prepared = prepare(app, source);
    auto candidate = prepared.stage_new_document("Configured history");
    const auto staged = candidate->snapshot();
    check(candidate->undo_steps() == 1 && staged.dirty(),
          "History-policy fixture lacks an initial changed-output undo entry");
    rejects<std::invalid_argument>([&] { candidate->set_history_policy(0, 1); },
                                   "Native positive history-step range changed");
    if (scenario == 0) candidate->set_history_policy(3, 0);
    else candidate->set_history_policy(3, 1);
    if (scenario == 2) candidate->set_history_policy(5, 0);
    const auto expected_undo = scenario == 0 ? std::size_t(1) : std::size_t(0);
    check(candidate->undo_steps() == expected_undo && candidate->redo_steps() == 0 &&
          candidate->snapshot().shares_data(staged) &&
          candidate->snapshot().revisions().matches(staged.revisions(), Change::All) &&
          candidate->snapshot().data_identity() == staged.data_identity() &&
          candidate->snapshot().dirty() == staged.dirty(),
          "Configured policy changed current data identity, revisions, save state or incorrect history");
    const auto result = app.publish(prepared);
    check(result.document == candidate && candidate->undo_steps() == expected_undo &&
          candidate->snapshot().dirty() && candidate->snapshot().data().dataset.plane()->pixels == minimum &&
          candidate->history_pixel_budget() == (scenario == 1 ? 1 : 0),
          "Legitimate configured pruning/unbounded budget refused adoption or changed output");
    source_unchanged(source, before, 0, 0);
  }
}
void candidate_invalidation() {
  named("RS04_modified_candidate_data_history_and_saved_baseline_refuse");
  for (int scenario = 0; scenario < 9; ++scenario) {
    Application app; auto source = owner(); app.adopt(source);
    auto prepared = prepare(app, source); auto candidate = prepared.stage_new_document();
    if (scenario == 0) {
      auto edit = candidate->begin_edit(); auto image = *candidate->snapshot().data().dataset.plane();
      image.at(0, 0) = 250; edit.replace_plane({}, std::move(image)); candidate->commit(edit);
    } else if (scenario == 1) candidate->select(Selection("point", {{2, 2}}));
    else if (scenario == 2) {
      auto edit = candidate->begin_edit(); edit.set_title("Changed"); candidate->commit(edit);
    } else if (scenario == 3) {
      auto edit = candidate->begin_edit(); edit.set_property("changed", true); candidate->commit(edit);
    } else if (scenario == 4) candidate->undo();
    else if (scenario == 5) { candidate->undo(); candidate->redo(); }
    else if (scenario == 6) candidate->checkpoint();
    else if (scenario == 7) candidate->mark_saved();
    else {
      auto edit = candidate->begin_edit(); edit.set_property("changed", true); candidate->commit(edit);
      candidate->undo(); candidate->set_history_policy(1, 1);
    }
    const auto before = source->snapshot(); const auto ids = app.document_ids();
    const auto active = app.active_document_id(); const auto changed = candidate->snapshot();
    rejects<RevisionConflict>([&] { app.publish(prepared); }, "Modified staged candidate was adopted");
    check(prepared.active() && app.document_ids() == ids && app.active_document_id() == active &&
          !app.contains(candidate->id) && candidate->snapshot().shares_data(changed),
          "Candidate conflict changed membership, consumed preparation or overwrote legitimate candidate edit");
    source_unchanged(source, before, 0, 0);
  }
}
void source_invalidation_and_cancellation() {
  named("RS05_stale_closed_and_canceled_source_refuse_atomically");
  for (int scenario = 0; scenario < 6; ++scenario) {
    Application app; auto source = owner(); app.adopt(source);
    auto token = std::make_shared<CancellationToken>();
    auto prepared = prepare(app, source, {token}); auto candidate = prepared.stage_new_document();
    if (scenario == 0) {
      auto edit = source->begin_edit(); edit.set_property("new", true); source->commit(edit);
    } else if (scenario == 1) {
      auto edit = source->begin_edit(); edit.replace_plane({}, Image(3, 3, 1, PixelType::UInt16)); source->commit(edit);
    } else if (scenario == 2) { source->checkpoint(); source->undo(); }
    else if (scenario == 3) app.close(source->id, true);
    else if (scenario == 4) token->cancel();
    else {
      // WholePlane excludes Selection from source dependencies.
      source->select(Selection("point", {{2, 2}}));
      auto result = app.publish(prepared);
      check(result.document == candidate, "Whole-plane rank was invalidated by source selection alone");
      continue;
    }
    const auto before = source->snapshot(); const auto ids = app.document_ids();
    const auto active = app.active_document_id();
    if (scenario == 3) rejects<DocumentUnavailable>([&] { app.publish(prepared); }, "Closed source output was adopted");
    else if (scenario == 4) {
      rejects<OperationCancelled>([&] { prepared.stage_new_document(); }, "Canceled preparation restaged");
      rejects<OperationCancelled>([&] { app.publish(prepared); }, "Canceled output was adopted");
    } else rejects<RevisionConflict>([&] { app.publish(prepared); }, "Stale source output was adopted");
    check(prepared.active() && app.document_ids() == ids && app.active_document_id() == active &&
          !app.contains(candidate->id) && source->snapshot().shares_data(before),
          "Failed publication changed source/membership or consumed the candidate");
  }
  Application app; auto source = owner(); app.adopt(source);
  auto token = std::make_shared<CancellationToken>(); auto prepared = prepare(app, source, {token});
  token->cancel();
  rejects<OperationCancelled>([&] { prepared.stage_new_document(); }, "Canceled unstaged result created a candidate");
  check(!prepared.prepared_new_document(), "Canceled staging exposed a partial owner");
}
void membership_and_constraint_failures() {
  named("RS06_membership_and_constraint_failure_leave_output_retryable");
  Application app; auto source = owner(); app.adopt(source);
  auto prepared = prepare(app, source); auto candidate = prepared.stage_new_document();
  Application foreign; foreign.create(Dataset::from_stack(Stack{Image(1, 1)}));
  rejects<DocumentUnavailable>([&] { foreign.publish(prepared); }, "Foreign application implicitly rebound source");
  bool reject = false;
  const auto lease = candidate->constrain([&](const DocumentData &, const DisplayState &) {
    if (reject) throw std::invalid_argument("Facade cannot attach this output");
  });
  reject = true;
  const auto before = candidate->snapshot(); const auto ids = app.document_ids();
  rejects<std::invalid_argument>([&] { app.publish(prepared); }, "Throwing facade constraint adopted the output");
  check(prepared.active() && !app.contains(candidate->id) && app.document_ids() == ids &&
        candidate->snapshot().shares_data(before), "Constraint failure swapped data or changed membership");
  reject = false;
  check(app.publish(prepared).document == candidate, "Constraint refusal destroyed a retryable prepared output");
  auto duplicate = prepare(app, source); auto registered = duplicate.stage_new_document();
  app.adopt(registered, false);
  const auto registered_before = registered->snapshot(); const auto registered_ids = app.document_ids();
  rejects<std::invalid_argument>([&] { app.publish(duplicate); }, "Already registered candidate was adopted twice");
  check(duplicate.active() && app.document_ids() == registered_ids &&
        registered->snapshot().shares_data(registered_before), "Failed membership adoption detached or changed candidate");
}
void no_op_keeps_source_redo() {
  named("RS07_new_document_noop_preserves_source_redo");
  Application app; auto source = owner(true); app.adopt(source);
  auto edit = source->begin_edit(); edit.set_property("redo entry", true); source->commit(edit); source->undo();
  const auto before = source->snapshot();
  auto prepared = prepare(app, source);
  check(prepared.preview().data().dataset.shares_pixels(before.data().dataset),
        "Constant input was not an unchanged rank result");
  auto candidate = prepared.stage_new_document("Unchanged rank output");
  check(candidate->snapshot().data().dataset.shares_pixels(before.data().dataset) &&
        candidate->undo_steps() == 1 && candidate->snapshot().dirty(),
        "New output did not retain generic title-only clone history");
  check(app.publish(prepared).document == candidate, "No-op output failed exact candidate adoption");
  source_unchanged(source, before, 0, 1);
  source->redo();
  check(source->snapshot().data().metadata.properties.count("redo entry") == 1,
        "Unchanged NewDocument output consumed the source redo branch");
}
void cancellation_before_adoption() {
  named("RS08_cancellation_during_final_validation_precedes_adoption");
  Application app; auto source = owner(); app.adopt(source);
  auto token = std::make_shared<CancellationToken>();
  auto prepared = prepare(app, source, {token});
  auto candidate = prepared.stage_new_document();
  bool armed = false;
  const auto lease = candidate->constrain([&](const DocumentData &, const DisplayState &) {
    if (armed) token->cancel();
  });
  const auto before = candidate->snapshot(); const auto ids = app.document_ids();
  armed = true;
  rejects<OperationCancelled>([&] { app.publish(prepared); },
                              "Cancellation during final validation lost to adoption");
  check(prepared.active() && app.document_ids() == ids && !app.contains(candidate->id) &&
        candidate->snapshot().shares_data(before),
        "Late cancellation adopted, consumed or detached the unregistered owner");
}
OperationJob submit(Application &app, const std::shared_ptr<Document> &source) {
  const auto resolved = command(source);
  JobOptions options; options.retain_result_resources = true;
  return app.submit("rank_filter_imagej", resolved.target(), resolved.parameters(), {}, options);
}
void ready(OperationJob &job) {
  check(job.wait_for(2s) && job.info().state == JobState::Ready,
        "Real retained rank job did not become READY");
}
void canceled_ready_candidate_lease() {
  named("RS09_canceled_READY_candidate_and_pixel_exports_retain_lease");
  Application app; auto source = owner(); app.adopt(source);
  auto scheduler = app.job_scheduler(); scheduler->set_limits({1, 1024 * 1024, 8});
  auto job = submit(app, source); ready(job);
  const auto bytes = job.info().resource_bytes;
  std::optional<PreparedOperation> prepared = job.prepared();
  auto candidate = prepared->stage_new_document();
  check(bytes > 0 && scheduler->usage().retained_bytes == bytes,
        "READY result lacked its declared retained reservation");
  check(job.cancel(), "READY result cancellation refused");
  prepared.reset();
  check(scheduler->usage().retained_bytes == bytes &&
        candidate->snapshot().data().dataset.plane()->pixels == minimum,
        "Cancel/drop preparation released the unregistered candidate's lease or pixels");
  auto exported = candidate->snapshot().data().dataset.plane();
  candidate.reset();
  check(scheduler->usage().retained_bytes == bytes && exported->pixels == minimum,
        "Independent candidate pixel export lost accounting after candidate destruction");
  exported.reset();
  check(scheduler->usage().reserved_bytes == 0 && app.document_ids().size() == 1,
        "Last canceled candidate export failed to release reservation or adopted an output");
}
void successful_ready_detachment() {
  named("RS10_successful_READY_adoption_detaches_owner_but_retains_old_exports");
  Application app; auto source = owner(); app.adopt(source);
  auto scheduler = app.job_scheduler(); scheduler->set_limits({1, 1024 * 1024, 8});
  auto job = submit(app, source); ready(job);
  const auto bytes = job.info().resource_bytes;
  std::optional<PreparedOperation> prepared = job.prepared();
  auto candidate = prepared->stage_new_document("Published retained output");
  std::optional<DocumentSnapshot> exported = candidate->snapshot();
  auto exported_pixels = exported->data().dataset.plane();
  const auto published = app.publish(job);
  check(published.document == candidate && job.info().state == JobState::Completed &&
        candidate->snapshot().data().dataset.plane().get() == exported_pixels.get() &&
        !candidate->snapshot().shares_data(*exported),
        "Publication copied pixels, retained guarded data owner, or adopted another candidate");
  prepared.reset();
  check(scheduler->usage().retained_bytes == bytes,
        "Prepublication candidate snapshot lost its resource lease after adoption");
  exported.reset();
  check(scheduler->usage().retained_bytes == bytes,
        "Prepublication pixel export lost its resource lease after snapshot destruction");
  exported_pixels.reset();
  check(scheduler->usage().reserved_bytes == 0 &&
        candidate->snapshot().data().dataset.plane()->pixels == minimum,
        "Published native owner retained the job reservation after all old exports died");
  candidate->undo(); candidate->redo();
  check(scheduler->usage().reserved_bytes == 0 &&
        candidate->snapshot().data().dataset.plane()->pixels == minimum,
        "Published native history retained/reacquired result lease");
}
} // namespace
int main() {
  try {
    identity_titles_and_retained_records();
    default_title_and_generic_compatibility();
    allowed_presentation_and_policy();
    configured_history_pruning();
    candidate_invalidation();
    source_invalidation_and_cancellation();
    membership_and_constraint_failures();
    no_op_keeps_source_redo();
    cancellation_before_adoption();
    canceled_ready_candidate_lease();
    successful_ready_detachment();
    std::cout << "Native rank staged output checks passed: " << cases
              << " cases, " << checks << " assertions\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
