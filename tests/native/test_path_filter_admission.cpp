#include "../../src/native/auto_threshold_internal.hpp"
#include "../../src/native/imagej_roi_import_internal.hpp"
#include "../../src/native/kernel_control.hpp"
#include "../../src/native/native_workflow_internal.hpp"
#include "../../src/native/native_workflow_program_internal.hpp"
#include "../../src/native/target_mask_resources.hpp"
#include <imagejxx/application.hpp>
#include <imagejxx/auto_threshold_stack.hpp>
#include <imagejxx/automatic_threshold_display.hpp>
#include <imagejxx/gaussian_command.hpp>
#include <imagejxx/native_workflow.hpp>
#include <imagejxx/native_workflow_program.hpp>
#include <imagejxx/rank_command.hpp>

#include <array>
#include <chrono>
#include <future>
#include <iostream>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

using namespace imagejxx;
namespace {
void check(bool value, const char *message) {
  if (!value) throw std::runtime_error(message);
}
template <class Exception = std::invalid_argument, class F>
void refuses(F work, const char *message) {
  bool refused = false;
  try { work(); } catch (const Exception &) { refused = true; }
  check(refused, message);
}
Selection path() {
  return Selection::from_path({0, 2, 2, 1, 6, 2, 1, 2, 6, 4});
}
struct Calls {
  std::array<std::size_t, 3> values{};
  ExecutionControl control() {
    ExecutionControl result;
    result.checkpoint = [&] { ++values[0]; };
    result.progress = [&](auto, auto) { ++values[1]; };
    result.stage_progress = [&](auto, auto, auto) { ++values[2]; };
    return result;
  }
  bool empty() const { return values == std::array<std::size_t, 3>{}; }
};
class NoRead final : public PlaneSource {
public:
  explicit NoRead(DatasetShape shape = {1, 1, 1, 10, 12, 1}) : shape_(shape) {}
  DatasetShape shape() const override { return shape_; }
  PixelType pixel_type() const override { return PixelType::Float32; }
  std::shared_ptr<const Image> read_plane(PlanePosition) const override {
    ++reads;
    throw std::runtime_error("Admission reached a forbidden provider read");
  }
  mutable std::size_t reads = 0;
private:
  DatasetShape shape_;
};
void unchanged(const Document &owner, const DocumentSnapshot &before,
               std::size_t history, const NoRead &source) {
  const auto after = owner.snapshot();
  check(after.shares_data(before) && after.revisions().matches(before.revisions(), Change::All) &&
            after.dirty() == before.dirty() && owner.undo_steps() == history && source.reads == 0,
        "Refusal read pixels or changed the source/history");
}
Image constant_plane() {
  Image image(12, 10, 1, PixelType::Float32);
  image.pixels.assign(120, 10);
  return image;
}
NativeWorkflowOperation gaussian_step() {
  NativeWorkflowOperation result;
  result.operation = "gaussian_blur_imagej";
  result.filter_options = GaussianBlurCommandOptions{0, false};
  return result;
}

void direct_later_position_refusal() {
  auto source = std::make_shared<NoRead>(DatasetShape{1, 1, 2, 4096, 4097, 1});
  DocumentData data{Dataset(source)};
  data.rois = {{"first", Selection("rectangle", {{1, 1}, {3, 3}}), {}, {}, 0, {}},
               {"later-path", path(), {}, {}, 1, {}}};
  Document owner(data);
  const auto before = owner.snapshot();
  const auto history = owner.undo_steps();
  TargetRequest request;
  request.scope = TargetScope::Positions;
  request.positions = {{0, 0, 0}, {0, 0, 1}};
  request.roi = RoiPolicy::PositionedRois;
  const auto gaussian = resolve_gaussian_command(before, request);
  const auto rank = resolve_rank_command(before, request);
  const auto nans = resolve_remove_nans_command(before, request);
  Calls inherited, calls;
  const auto inherited_control = inherited.control();
  ExecutionScope parent(inherited_control);
  const auto control = calls.control();
  const std::vector<std::pair<std::string, OperationParameters>> commands{
      {"gaussian_blur_imagej", gaussian.parameters()},
      {rank.operation(), rank.parameters()}, {nans.operation(), nans.parameters()}};
  for (const auto &command : commands) {
    refuses([&] { prepare_operation(command.first, gaussian.target(), command.second, {}, control); },
            "A later oversized path was not rejected by complete-target admission");
    check(calls.empty() && inherited.empty() && !parent.dispatching_callback,
          "Direct refusal dispatched a user callback or leaked scope suppression");
    unchanged(owner, before, history, *source);
  }
  kernel_checkpoint();
  check(inherited.values[0] > 0, "Direct admission did not restore its inherited scope");
}

void direct_cap_and_frozen_intent() {
  auto source = std::make_shared<NoRead>();
  Document owner{Dataset(source)};
  owner.select(path());
  const auto before = owner.snapshot();
  const auto history = owner.undo_steps();
  const auto command = resolve_gaussian_command(before, {}, {0, false});
  const auto scratch = detail::target_mask_workspace(command.target(), "gaussian_blur_imagej");
  ExecutionOptions options;
  options.max_target_mask_bytes = scratch - 1;
  Calls calls;
  const auto control = calls.control();
  refuses([&] { prepare_operation("gaussian_blur_imagej", command.target(), command.parameters(), options, control); },
          "Direct operation ignored its explicit additional-mask cap");
  check(calls.empty(), "Direct finite-cap refusal called user code");
  unchanged(owner, before, history, *source);
  auto canceled = control;
  canceled.cancellation = std::make_shared<CancellationToken>();
  canceled.cancellation->cancel();
  refuses<OperationCancelled>([&] {
    prepare_operation("gaussian_blur_imagej", command.target(), command.parameters(), {}, canceled);
  }, "Direct admission lost cancellation");
  check(calls.empty(), "Canceled admission dispatched callbacks");

  Document eager(Dataset::from_stack({constant_plane()}));
  eager.select(path());
  const auto eager_before = eager.snapshot();
  auto target = resolve_target(eager_before);
  OperationParameters parameters{{"value", 1.}};
  bool changed = false;
  ExecutionControl mutable_control;
  mutable_control.checkpoint = [&] {
    if (changed) return;
    changed = true;
    TargetRequest whole; whole.roi = RoiPolicy::WholePlane;
    target = resolve_target(eager_before, whole);
    parameters["value"] = 100.;
  };
  options.max_target_mask_bytes = scratch;
  auto prepared = prepare_operation("add", target, parameters, options, mutable_control);
  const auto output = prepared.preview().data().dataset.plane();
  check(changed, "Admitted operation suppressed all execution callbacks");
  for (int y = 0; y < 10; ++y)
    for (int x = 0; x < 12; ++x)
      check(output->at(x, y) == ((x >= 2 && y >= 2 && x + y <= 7) ? 11 : 10),
            "Callback changed the admitted target or normalized parameters");
  check(eager.snapshot().shares_data(eager_before), "Preparing frozen intent published pixels");
}

struct ReleaseBarrier {
  std::promise<void> promise;
  bool released = false;
  void release() { if (!released) { promise.set_value(); released = true; } }
  ~ReleaseBarrier() { release(); }
};

void application_cap_and_queue_policy() {
  auto source = std::make_shared<NoRead>();
  auto owner = std::make_shared<Document>(Dataset(source));
  owner->select(path());
  const auto before = owner->snapshot();
  const auto history = owner->undo_steps();
  const auto command = resolve_gaussian_command(before, {}, {0, false});
  const auto scratch = detail::target_mask_workspace(command.target(), "gaussian_blur_imagej");
  // One staged S>=4 double plane + sixteen S1 workspace planes + four copies
  // of the two-value normalized sigma argument. Sigma0 has no kernel buffers.
  const std::size_t base = 120 * (4 + 16) * sizeof(double) + 2 * 4 * sizeof(double);
  const auto cap = base + scratch;
  Application app;
  app.adopt(owner);
  const auto scheduler = app.job_scheduler();
  scheduler->set_limits({1, cap - 1, 8});
  Calls calls;
  const auto control = calls.control();
  {
    ExecutionScope inherited(control);
    refuses([&] { app.submit("gaussian_blur_imagej", command.target(), command.parameters()); },
            "Application did not subtract its staged/base/extra reservation before mask planning");
    check(calls.empty() && !inherited.dispatching_callback,
          "Application refusal dispatched inherited callbacks");
  }
  unchanged(*owner, before, history, *source);
  check(scheduler->jobs().empty(), "Refused mask planning submitted a job");
  scheduler->set_limits({1, cap, 8});
  ReleaseBarrier release;
  auto signal = release.promise.get_future().share();
  const auto entered = std::make_shared<std::promise<void>>();
  auto entered_future = entered->get_future();
  JobOptions blocker_options; blocker_options.resource_bytes = cap;
  auto blocker = scheduler->submit([entered, signal](const JobContext &) {
    entered->set_value(); signal.wait();
  }, blocker_options);
  check(entered_future.wait_for(std::chrono::seconds(5)) == std::future_status::ready,
        "Queue test blocker did not acquire the scheduler reservation");
  auto queued = app.submit("gaussian_blur_imagej", command.target(), command.parameters());
  check(queued.info().state == JobState::Queued && queued.info().resource_bytes == cap,
        "A valid individually affordable job was refused or mischarged while another job ran");
  check(queued.cancel(), "Queued operation was not cancelable");
  release.release();
  blocker.wait();
  unchanged(*owner, before, history, *source);
}

void pooled_threshold_caps() {
  auto source = std::make_shared<NoRead>();
  Document owner{Dataset(source)};
  owner.select(path());
  const auto before = owner.snapshot();
  const auto history = owner.undo_steps();
  const auto target = resolve_target(before);
  const auto estimated = detail::pooled_threshold_working_bytes(target, 256, false);
  Calls calls, inherited_calls;
  const auto control = calls.control();
  const auto parent_control = inherited_calls.control();
  ExecutionScope parent(parent_control);
  refuses([&] { pooled_auto_threshold(target, "mean", 256, false, false, estimated - 1, control); },
          "Pooled threshold did not apply its remaining target-mask allowance");
  check(calls.empty() && inherited_calls.empty(), "Pooled refusal called user code");
  unchanged(owner, before, history, *source);

  ManualThresholdLimits limits;
  const auto analysis = estimate_manual_threshold(before.data().dataset.shape(), PixelType::Float32, {}, limits);
  const auto pooled = detail::pooled_threshold_working_bytes(target, 256, true);
  limits.max_working_bytes = pooled + 120 + 1 + analysis.analysis_bytes - 1;
  refuses([&] { pooled_automatic_threshold_display(target, "mean", 256, true, false, false, limits, control); },
          "Pooled display passed its full cap instead of the remaining phase allowance");
  check(calls.empty() && inherited_calls.empty() && !parent.dispatching_callback,
        "Pooled display refusal dispatched callbacks or lost scope restoration");
  unchanged(owner, before, history, *source);
}

void workflow_later_refusal_and_disabled() {
  auto source = std::make_shared<NoRead>(DatasetShape{1, 1, 1, 4096, 4097, 1});
  Document owner{Dataset(source)};
  owner.select(path());
  const auto before = owner.snapshot();
  const auto history = owner.undo_steps();
  auto filter = gaussian_step();
  NativeWorkflowOperation prefix;
  prefix.operation = "add"; prefix.parameters = {{"value", 1.}};
  prefix.target.roi = RoiPolicy::WholePlane;
  Calls calls, inherited_calls;
  const auto control = calls.control();
  const auto parent_control = inherited_calls.control();
  ExecutionScope parent(parent_control);
  NativeWorkflowLimits limits{64, 1024ULL * 1024 * 1024};
  for (const auto &steps : {std::vector<NativeWorkflowStep>{filter},
                            std::vector<NativeWorkflowStep>{prefix, filter}}) {
    refuses([&] { prepare_native_workflow(before, steps, limits, control); },
            "Workflow did not preflight a later path refusal before executing its valid prefix");
    check(calls.empty() && inherited_calls.empty() && !parent.dispatching_callback,
          "Refused filter-only/mixed workflow dispatched user callbacks");
    unchanged(owner, before, history, *source);
  }
  filter.enabled = false;
  const auto retained = detail::admit_native_workflow(before, {&filter}, limits, control);
  check(retained > 0 && calls.empty() && inherited_calls.empty(),
        "Disabled filter incorrectly resolved its destination raster");
  unchanged(owner, before, history, *source);
  auto canceled = control;
  canceled.cancellation = std::make_shared<CancellationToken>();
  canceled.cancellation->cancel();
  refuses<OperationCancelled>([&] { prepare_native_workflow(before, {filter}, limits, canceled); },
                              "Workflow admission lost its own cancellation token");
  check(calls.empty() && inherited_calls.empty() && !parent.dispatching_callback,
        "Canceled workflow admission leaked callbacks or scope state");
  limits.max_working_bytes = 0;
  refuses([&] { prepare_native_workflow(before, {filter}, limits, control); },
          "Zero workflow working cap was accepted");
  check(calls.empty() && inherited_calls.empty(), "Invalid workflow limit called user code");
}

void workflow_retained_plus_serial_peak() {
  DatasetReadObserver reads;
  Document owner(Dataset::from_stack({constant_plane()}).observe_reads(reads));
  owner.select(path());
  const auto before = owner.snapshot();
  const auto history = owner.undo_steps();
  auto filter = gaussian_step();
  const std::vector<NativeWorkflowStep> steps{filter, filter};
  const auto initial = prepare_native_workflow(before, steps);
  const auto retained = initial.retained_bytes();
  const auto scratch = detail::target_mask_workspace(resolve_gaussian_command(before).target(), "gaussian_blur_imagej");
  const auto count = reads.counts().plane_reads;
  NativeWorkflowLimits limits{64, retained, retained + scratch - 1};
  Calls calls;
  const auto control = calls.control();
  refuses([&] { prepare_native_workflow(before, steps, limits, control); },
          "Workflow independently admitted retained bytes and mask scratch against the same full cap");
  check(calls.empty() && reads.counts().plane_reads == count,
        "Working-cap refusal read a plane or called user code");
  ++limits.max_working_bytes;
  const auto accepted = prepare_native_workflow(before, steps, limits, control);
  check(accepted.completed_steps() == 2 && accepted.retained_bytes() == retained &&
            !calls.empty(),
        "Serial masks were accumulated into retained bytes or charged per workflow stage");
  check(owner.snapshot().shares_data(before) && owner.undo_steps() == history,
        "Workflow preparation changed its original owner/history");

  // Borrowed callers may declare a larger independently retained input floor.
  // It must participate in the very same working allowance before execution.
  const auto floor = retained + 8192;
  NativeWorkflowLimits floor_limits{64, floor, floor + scratch - 1};
  const auto before_reads = reads.counts().plane_reads;
  calls.values = {};
  refuses([&] {
    detail::prepare_native_workflow_borrowed(before, {&filter}, floor_limits, control, floor);
  }, "Captured-input retention floor was omitted from mask working admission");
  check(calls.empty() && reads.counts().plane_reads == before_reads,
        "Captured-floor refusal leaked a callback or provider read");
}

void workflow_unrelated_retained_carriers() {
  auto source = std::make_shared<NoRead>(DatasetShape{1, 1, 2, 10, 12, 1});
  DocumentData data{Dataset(source)};
  data.rois = {{"consumed-z0", path(), {}, {}, 0, {}}};
  Document bare(data);
  const auto retained_path = [](std::size_t segments) {
    std::vector<double> commands{0, 1, 1};
    for (std::size_t i = 0; i < segments; ++i)
      commands.insert(commands.end(), {1, double(1 + i % 9), double(1 + (3 * i) % 7)});
    commands.push_back(4);
    return Selection::from_path(commands);
  };
  data.previous_selection = retained_path(512);
  data.rois.push_back({"unmatched-z1", retained_path(1024), {}, {}, 1, {}});
  Document owner(data);
  const auto before = owner.snapshot();
  const auto history = owner.undo_steps();
  auto filter = gaussian_step();
  filter.target.scope = TargetScope::Positions;
  filter.target.positions = {{0, 0, 0}};
  filter.target.roi = RoiPolicy::PositionedRois;
  const std::vector<detail::NativeWorkflowStepRef> refs{&filter, &filter};
  const auto bare_target = resolve_gaussian_command(bare.snapshot(), filter.target).target();
  const auto target = resolve_gaussian_command(before, filter.target).target();
  // The supplement is independently obtained from the same consumed contour in
  // an owner with no unrelated carriers. Both added command buffers alone are
  // larger than this allowance: accidentally planning or charging either as
  // target scratch makes the advertised exact boundary observably too small.
  const auto scratch = detail::target_mask_workspace(bare_target, "gaussian_blur_imagej");
  check(before.data().previous_selection->path_commands().size() * sizeof(double) > scratch &&
            before.data().rois[1].selection.path_commands().size() * sizeof(double) > scratch,
        "Unrelated retained contours do not distinguish erroneous scratch charging");
  check(detail::target_mask_workspace(target, "gaussian_blur_imagej", scratch) == scratch,
        "Previous or unmatched saved path was charged as consumed mask scratch");

  Calls calls, inherited_calls;
  const auto control = calls.control();
  const auto inherited_control = inherited_calls.control();
  ExecutionScope parent(inherited_control);
  const auto bare_retained = detail::admit_native_workflow(bare.snapshot(), refs, {}, control);
  const auto retained = detail::admit_native_workflow(before, refs, {}, control);
  check(retained > bare_retained,
        "Unrelated retained path carriers disappeared from workflow retention accounting");
  check(calls.empty() && inherited_calls.empty(),
        "Retained-carrier metadata admission dispatched a callback");

  // Existing retained accounting already owns every carrier. Two serial filter
  // stages need just the one consumed-path scratch peak, not another backing
  // charge and not a sum of per-step or unrelated-path scratch reservations.
  NativeWorkflowLimits limits{64, retained, retained + scratch};
  check(detail::admit_native_workflow(before, refs, limits, control) == retained,
        "Exact retained-plus-consumed-scratch cap double charged unrelated paths");
  check(calls.empty() && inherited_calls.empty() && !parent.dispatching_callback,
        "Exact metadata admission leaked callbacks or inherited dispatch state");
  unchanged(owner, before, history, *source);
  --limits.max_working_bytes;
  refuses([&] { detail::admit_native_workflow(before, refs, limits, control); },
          "One byte below retained plus consumed scratch was admitted");
  refuses([&] { prepare_native_workflow(before, {filter, filter}, limits, control); },
          "Public workflow bypassed retained-carrier working admission");
  check(calls.empty() && inherited_calls.empty() && !parent.dispatching_callback,
        "Retained-carrier refusal dispatched callbacks or failed to restore scope");
  unchanged(owner, before, history, *source);
  check(owner.snapshot().data().previous_selection == before.data().previous_selection &&
            owner.snapshot().data().rois == before.data().rois,
        "Admission changed the consumed or unrelated retained path geometry");
}

// Fixed E6 Run-panel admission fixture. This group does not execute either
// accepted step; it proves the later path-mask working limit precedes reads,
// callbacks, and prefix publication. Its 209x207 source must qualify unchanged
// on both platforms before the corresponding actual-input UI recipe is used.
void workflow_fixed_panel_mask_cap() {
  using Json = detail::session_metadata::Json;
  Json receipt = Json::object();
  constexpr std::size_t host_cap = 1048576;
  constexpr std::size_t generous_cap = 64 * 1048576;
  constexpr std::size_t minimum_retained_margin = 1024;
  constexpr DatasetShape fixture_shape{1, 1, 1, 207, 209, 1};
  const auto sum = [](std::size_t a, std::size_t b) {
    check(b <= std::numeric_limits<std::size_t>::max() - a, "Test byte sum overflow");
    return a + b;
  };
  class ConstantNoRead final : public PlaneSource {
  public:
    explicit ConstantNoRead(DatasetShape shape)
        : shape_(shape), pixels_(std::make_shared<Image>(shape.x, shape.y, 1, PixelType::Float32)) {
      pixels_->pixels.assign(shape.pixel_count(), 10.);
    }
    DatasetShape shape() const override { return shape_; }
    PixelType pixel_type() const override { return PixelType::Float32; }
    std::shared_ptr<const Image> read_plane(PlanePosition) const override {
      ++reads;
      throw std::runtime_error("E6 admission attempted a constant 10 provider read");
    }
    mutable std::size_t reads = 0;
  private:
    DatasetShape shape_;
    // Fixed backing defines the eventual UI input; admission must not read it.
    // No native output supplies the independent future numeric expectation 11.
    std::shared_ptr<Image> pixels_;
  };
  const auto refs = [](const std::vector<NativeWorkflowStep> &steps) {
    std::vector<detail::NativeWorkflowStepRef> result;
    for (const auto &step : steps)
      std::visit([&](const auto &value) { result.emplace_back(&value); }, step);
    return result;
  };
  const auto mask_refusal = [](auto action) -> std::string {
    try { action(); }
    catch (const std::invalid_argument &error) {
      const std::string message = error.what();
      if (message != "ImageJ shape exceeds working-byte limit" &&
          message != "Native workflow retained plus target-mask working-byte limit")
        throw std::runtime_error("Expected mask-working refusal; observed: " + message);
      return message;
    }
    throw std::runtime_error("Later ActiveSelection mask unexpectedly fit the finite cap");
  };
  try {
    auto provider=std::make_shared<ConstantNoRead>(fixture_shape);
    DocumentData data{Dataset(provider)};
    data.selection=Selection::from_path({0.,2.2,1.2,1.,9.2,1.2,1.,2.2,8.2,4.});
    // Freeze no Previous/saved/overlay/opaque metadata carriers for this cap
    // fixture; adding them later invalidates this exact resource qualification.
    Document owner(std::move(data));
    auto display=owner.snapshot().display();
    display.properties["display_range"]=metadata_array({0.,20.});
    owner.set_display(std::move(display));
    const auto before=owner.snapshot();
    const auto undo=owner.undo_steps(), redo=owner.redo_steps();

    NativeWorkflowOperation add;
    add.operation="add";
    add.parameters={{"value",1.}};
    add.target.roi=RoiPolicy::WholePlane;
    NativeWorkflowOperation filter;
    filter.operation="gaussian_blur_imagej";
    filter.filter_options=GaussianBlurCommandOptions{0.,false};
    filter.target.roi=RoiPolicy::ActiveSelection;
    const NativeWorkflowProgramLimits transport_limits{64,host_cap/4};
    const auto program=make_native_workflow_program({add,filter},transport_limits);
    const std::string wire=program.json();
    const auto restored=native_workflow_program_from_json(wire,transport_limits);
    check(restored.json()==wire,"Serialized two-step program did not roundtrip exactly");
    const auto &decoded=restored.steps();
    check(decoded.size()==2,"Serialized program changed its two-step cardinality");
    const auto &decoded_add=std::get<NativeWorkflowOperation>(decoded[0]);
    const auto &decoded_filter=std::get<NativeWorkflowOperation>(decoded[1]);
    const auto add_value=std::get_if<double>(&decoded_add.parameters.at("value"));
    check(decoded_add.operation=="add" && decoded_add.target.roi==RoiPolicy::WholePlane &&
          add_value && *add_value==1. &&
          decoded_add.target.scope==TargetScope::CurrentPlane &&
          decoded_add.target.destination==OutputDestination::InPlace &&
          decoded_filter.operation=="gaussian_blur_imagej" &&
          decoded_filter.target.roi==RoiPolicy::ActiveSelection && decoded_filter.filter_options &&
          decoded_filter.target.scope==TargetScope::CurrentPlane &&
          decoded_filter.target.destination==OutputDestination::InPlace &&
          std::get<GaussianBlurCommandOptions>(*decoded_filter.filter_options).sigma==0. &&
          !std::get<GaussianBlurCommandOptions>(*decoded_filter.filter_options).scaled_units,
          "Serialized program lost the exact prefix/filter/ROI intent");
    const auto path_refs=refs(decoded);
    const std::vector<detail::NativeWorkflowStepRef> prefix_refs{path_refs[0]};
    auto whole_steps=decoded;
    std::get<NativeWorkflowOperation>(whole_steps[1]).target.roi=RoiPolicy::WholePlane;
    const auto whole_refs=refs(whole_steps);

    // Run's NativeWorkflowPanel::show sets record_operation=false. Therefore
    // Prepare complete workflow leaves record_bytes=0 and supplies floor0.
    // It does NOT subtract this existing program's JSON transport length.
    // The private UI table's separate cap/4 structure gate is not reimplemented
    // here; the later actual Run recipe must retain its successful review proof.
    constexpr std::size_t panel_record_bytes=0;
    constexpr std::size_t panel_floor=0;
    constexpr std::size_t available=host_cap-panel_record_bytes;

    const NativeWorkflowLimits generous{64,generous_cap,generous_cap};
    const auto data_bytes=detail::admit_native_workflow(before,{},generous);
    const auto display_bytes=detail::imagej_roi_record_bytes(before.display().properties,generous_cap);
    const auto inputs_bytes=detail::native_workflow_inputs_bytes(path_refs,generous_cap);
    // These are the exact components of prepare_native_workflow's owning floor,
    // each measured by its existing helper or actual C++ type size, not an ABI
    // assumption. The panel's borrowed path still passes floor0.
    const auto captured_floor=sum(sum(data_bytes,display_bytes),
        sum(sizeof(DocumentSnapshot),inputs_bytes));
    const auto target=resolve_target(before,decoded_filter.target,decoded_filter.operation);
    const auto scratch=detail::target_mask_workspace(target,decoded_filter.operation);
    const auto plane_payload=fixture_shape.pixel_count()*sizeof(double);
    check(plane_payload<=std::numeric_limits<std::size_t>::max()/3,"Payload bound overflow");
    const auto three_payloads=3*plane_payload;

    Calls calls,inherited;
    const auto control=calls.control();
    const auto inherited_control=inherited.control();
    ExecutionScope parent(inherited_control);
    const auto prefix_retained=detail::admit_native_workflow(before,prefix_refs,generous,control);
    const auto whole_retained=detail::admit_native_workflow(before,whole_refs,generous,control);
    const auto path_retained=detail::admit_native_workflow(before,path_refs,generous,control);
    receipt={{"contract","p13i.e6.workflow-mask-cap.001"},
        {"state","measured_not_yet_qualified"},{"shape",fixture_shape.dimensions()},
        {"source_constant",10},{"selection_commands",before.data().selection->path_commands()},
        {"source_metadata",{{"title",before.data().metadata.title},
          {"properties",detail::session_metadata::to_json(before.data().metadata.properties)},
          {"extensions",detail::session_metadata::to_json(before.data().metadata.extensions)},
          {"record_layout",detail::session_metadata::to_json(before.data().metadata.record_layout)}}},
        {"source_display",detail::session_metadata::to_json(before.display().properties)},
        {"serialized_program",Json::parse(wire)},{"program_utf8_bytes",wire.size()},
        {"host_cap",host_cap},{"panel_record_bytes",panel_record_bytes},
        {"panel_floor",panel_floor},{"available",available},
        {"source_retained_bytes",data_bytes},{"display_record_bytes",display_bytes},
        {"native_input_bytes",inputs_bytes},{"snapshot_object_bytes",sizeof(DocumentSnapshot)},
        {"public_captured_floor",captured_floor},{"three_double_payloads",three_payloads},
        {"prefix_retained",prefix_retained},{"whole_retained",whole_retained},
        {"path_retained",path_retained},{"mask_scratch",scratch},
        {"required_working",sum(path_retained,scratch)},
        {"minimum_retained_margin",minimum_retained_margin}};
    if (path_retained<=available) receipt["retained_margin"]=available-path_retained;
    else receipt["retained_overrun"]=path_retained-available;
    check(calls.empty() && inherited.empty() && provider->reads==0,
          "Measurement phase invoked an execution callback or read pixels");
    check(path_retained==whole_retained && path_retained>=three_payloads,
          "Only changing ROI policy changed cumulative retained accounting");
    check(captured_floor<=path_retained,
          "Public owning capture exceeds this fixture's cumulative retained budget");
    check(path_retained<=available && available-path_retained>=minimum_retained_margin,
          "Fixed 209x207 fixture lacks the required retained-only margin; refreeze input before UI use");
    check(scratch>available-path_retained,
          "Fixed 209x207 path scratch does not cross the 1 MiB cap; refreeze input before UI use");

    const NativeWorkflowLimits panel_limits{64,available,available};
    check(detail::admit_native_workflow(before,prefix_refs,panel_limits,control,panel_floor)==prefix_retained,
          "The earlier valid whole-plane prefix does not fit the panel cap");
    check(detail::admit_native_workflow(before,whole_refs,panel_limits,control,panel_floor)==whole_retained,
          "Identical whole-plane second step does not fit the panel cap");
    const NativeWorkflowLimits retained_only{64,available,std::numeric_limits<std::size_t>::max()};
    check(detail::admit_native_workflow(before,path_refs,retained_only,control,panel_floor)==path_retained,
          "ActiveSelection has an earlier retained-byte admission failure");
    receipt["panel_admit_refusal"]=mask_refusal([&] {
      detail::admit_native_workflow(before,path_refs,panel_limits,control,panel_floor);
    });
    receipt["panel_prepare_refusal"]=mask_refusal([&] {
      detail::prepare_native_workflow_borrowed(before,path_refs,panel_limits,control,panel_floor);
    });
    receipt["public_prepare_refusal"]=mask_refusal([&] {
      prepare_native_workflow(before,decoded,panel_limits,control);
    });

    // Exact serial-mask boundary, without executing either filter or prefix.
    NativeWorkflowLimits exact{64,path_retained,sum(path_retained,scratch)};
    check(detail::admit_native_workflow(before,path_refs,exact,control,panel_floor)==path_retained,
          "Exact borrowed retained-plus-mask boundary did not fit");
    check(detail::admit_native_workflow(before,path_refs,exact,control,captured_floor)==path_retained,
          "Actual public captured floor was double charged at the exact boundary");
    --exact.max_working_bytes;
    receipt["one_byte_under_refusal"]=mask_refusal([&] {
      detail::admit_native_workflow(before,path_refs,exact,control,captured_floor);
    });
    check(calls.empty() && inherited.empty() && !parent.dispatching_callback && provider->reads==0,
          "Refused admission leaked callbacks/provider reads or scope suppression");
    const auto after=owner.snapshot();
    check(after.shares_data(before) && after.revisions().matches(before.revisions(),Change::All) &&
          after.dirty()==before.dirty() && owner.undo_steps()==undo && owner.redo_steps()==redo &&
          after.data().selection==before.data().selection &&
          metadata_equal(after.display().properties,before.display().properties),
          "Metadata qualification mutated source/display/history");
    receipt["execution_callbacks"]=calls.values;
    receipt["inherited_callbacks"]=inherited.values;
    receipt["provider_reads"]=provider->reads;
    receipt["state"]="qualified_metadata_only";
    std::cout << receipt.dump(2) << '\n';
  } catch (const std::exception &error) {
    receipt["state"] = "failed";
    receipt["failure"] = error.what();
    std::cout << receipt.dump(2) << '\n';
    throw;
  }
}
} // namespace

int main() {
  try {
    const std::pair<const char *, void (*)()> cases[] = {
        {"I-ADMIT-01 later-position", direct_later_position_refusal},
        {"I-ADMIT-02 direct-cap-and-freeze", direct_cap_and_frozen_intent},
        {"I-ADMIT-03 application-queue", application_cap_and_queue_policy},
        {"I-ADMIT-04 pooled-caps", pooled_threshold_caps},
        {"I-ADMIT-05 workflow-refusal", workflow_later_refusal_and_disabled},
        {"I-ADMIT-06 workflow-retained-peak", workflow_retained_plus_serial_peak},
        {"I-ADMIT-07 unrelated-retained-carriers", workflow_unrelated_retained_carriers},
        {"I-ADMIT-08 fixed-panel-mask-cap", workflow_fixed_panel_mask_cap}};
    for (const auto &item : cases) {
      std::cout << item.first << '\n';
      item.second();
    }
    std::cout << "Path filter admission checks passed (8 cases)\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
