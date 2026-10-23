#include <imagejxx/native_workflow_program.hpp>
#include <iostream>
#include <stdexcept>
using namespace imagejxx;
namespace {
std::size_t checks = 0;
void check(bool value, const char *message) {
  ++checks;
  if (!value)
    throw std::runtime_error(message);
}
template <class Function> void rejects(Function action, const char *message) {
  bool rejected = false;
  try {
    action();
  } catch (const std::invalid_argument &) {
    rejected = true;
  }
  check(rejected, message);
}
NativeWorkflowOperation creation(SelectionCreationOperation mode, Selection candidate) {
  NativeWorkflowOperation value;
  value.operation = "selection_creation";
  value.selection_creation = SelectionCreationRequest{mode, std::move(candidate)};
  return value;
}
} // namespace
int main() {
  try {
    Image image(9, 9, 1, PixelType::UInt16);
    for (std::size_t i = 0; i < image.pixels.size(); ++i)
      image.pixels[i] = double(i);
    DatasetReadObserver reads;
    Document owner{Dataset::from_stack({image}).observe_reads(reads)};
    const auto base = Selection::from_packed_region(3, 1, {0xa0}, {-2, 0});
    owner.select(base);
    const auto before = owner.snapshot();
    auto add = creation(SelectionCreationOperation::Add,
                        Selection("rectangle", {{-1, 0}, {2, 1}}));
    auto subtract = creation(SelectionCreationOperation::Subtract,
                             Selection("rectangle", {{0, 0}, {1, 1}}));
    const auto united = Selection::from_packed_region(4, 1, {0xf0}, {-2, 0});
    const auto difference = Selection::from_packed_region(4, 1, {0xd0}, {-2, 0});
    const auto program = make_native_workflow_program({add, subtract});
    check(std::get<std::int64_t>(program.record().at("version")) == 6,
          "Creation program did not use V6");
    const auto restored = native_workflow_program_from_json(program.json());
    auto prepared = prepare_native_workflow(before, restored.steps());
    check(prepared.completed_steps() == 2 &&
              prepared.preview().data().selection == difference,
          "Later creation did not consume preceding stage geometry");
    check(prepared.preview().data().previous_selection == united,
          "Workflow lost intermediate Previous");
    check(owner.snapshot().shares_data(before), "Preparation published live data");
    check(reads.counts().plane_reads == 0 && reads.counts().region_reads == 0,
          "Geometry-only workflow read source samples");
    auto edit = prepared.begin_edit();
    owner.commit(edit);
    check(owner.snapshot().data().selection == difference &&
              owner.snapshot().data().previous_selection == united,
          "Publication lost full geometry/Previous");
    check(owner.undo_steps() == 0 &&
              owner.snapshot().data().dataset.shares_pixels(before.data().dataset),
          "Creation workflow detached pixels or consumed pixel history");
    Document legacy{Dataset::from_stack({image})};
    legacy.select(Selection("rectangle", {{0, 0}, {3, 2}}));
    for (int version = 1; version <= 3; ++version) {
      auto source = version == 3 ? before : legacy.snapshot();
      Selection candidate(version == 2 ? "rounded_rectangle" : "rectangle",
                          {{1, 1}, {4, 3}});
      if (version == 2)
        candidate.attributes["corner_diameter"] = std::int64_t(2);
      const auto captured = resolve_selection_creation_command(
          source, {SelectionCreationOperation::Add, candidate});
      check(std::get<std::int64_t>(captured.record().at("version")) == version,
            "Wrong legacy fixture version");
      NativeWorkflowOperation retained;
      retained.operation = "selection_creation";
      retained.original = captured.record();
      const auto portable = native_workflow_program_from_json(
          make_native_workflow_program({retained}).json());
      const auto result = prepare_native_workflow(source, portable.steps());
      check(result.preview().data().selection == captured.result(),
            "Retained original lost its equation");
      check(metadata_equal(
                *std::get<NativeWorkflowOperation>(portable.steps()[0]).original,
                captured.record()),
            "Program rewrote original provenance");
    }
    // C++ callbacks can reassign the public inputs. Captured steps and controls
    // must retain their original equation/owner, including during admission.
    auto source = before;
    std::vector<NativeWorkflowStep> steps{add, subtract};
    NativeWorkflowLimits limits;
    ExecutionControl control;
    int calls = 0;
    bool inside = false;
    control.checkpoint = [&] {
      check(!inside, "Workflow callback was dispatched recursively");
      inside = true;
      (void)make_native_workflow_program({add});
      inside = false;
      ++calls;
      source = legacy.snapshot();
      steps.clear();
      limits.max_retained_bytes = 0;
      control = {};
    };
    const auto frozen = prepare_native_workflow(source, steps, limits, control);
    check(calls > 0 && frozen.source().same_document(before) &&
              frozen.preview().data().selection == difference,
          "Workflow followed reassigned inputs");
    limits = {64, 1};
    steps = {add};
    calls = 0;
    control.checkpoint = [&] { ++calls; };
    rejects([&] { (void)prepare_native_workflow(before, steps, limits, control); },
            "Tiny workflow budget admitted captures");
    check(calls == 0, "Input-copy budget refusal dispatched callbacks");
    Document navigation{Dataset::from_stack({image})};
    navigation.select(base);
    const auto navigation_source = navigation.snapshot();
    const auto guarded = prepare_native_workflow(navigation_source, {add});
    check(guarded.dependencies() == Change::All,
          "Creation workflow did not guard captured navigation");
    auto display = navigation_source.display();
    display.zoom = 2;
    navigation.set_display(display);
    bool stale = false;
    try {
      auto transaction = guarded.begin_edit();
      navigation.commit(transaction);
    } catch (const RevisionConflict &) {
      stale = true;
    }
    check(stale && navigation.snapshot().data().selection == base,
          "Navigation race published a selection workflow");
    auto disabled = subtract;
    disabled.enabled = false;
    legacy.select(Selection("point", {{2, 2}}));
    const auto skipped = prepare_native_workflow(legacy.snapshot(), {disabled});
    check(skipped.completed_steps() == 0 &&
              skipped.preview().data().selection == legacy.snapshot().data().selection,
          "Disabled creation resolved destination compatibility");
    NativeWorkflowOperation invert;
    invert.operation = "invert";
    invert.target.roi = RoiPolicy::WholePlane;
    auto malformed = add;
    malformed.selection_creation->candidate.width = -1;
    rejects([&] { (void)prepare_native_workflow(before, {invert, malformed}); },
            "Malformed late creation was admitted");
    check(reads.counts().plane_reads == 0 && reads.counts().region_reads == 0,
          "Malformed late intent allowed an earlier pixel read");
    NativeWorkflowHistogram histogram;
    histogram.name = "area.csv";
    histogram.authored_options = NativeWorkflowHistogramOptions{};
    const auto analysis = prepare_native_workflow(before, {add, subtract, histogram});
    check(analysis.histograms().size() == 1 &&
              analysis.histograms()[0].value.visited_count == 1 &&
              analysis.histograms()[0].value.sum == 1,
          "Analysis did not consume final visible intersection");
    std::cout << "PASS V6 creation workflow staging/capture/atomicity/no-IO/analysis "
              << checks << " checks\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
