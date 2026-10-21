#include <chrono>
#include <filesystem>
#include <fstream>
#include <imagejxx/native_workflow_batch.hpp>
#include <imagejxx/native_workflow_batch_setup.hpp>
#include <imagejxx/native_workflow_program.hpp>
#include <iostream>
using namespace imagejxx;
namespace {
void check(bool b) {
  if (!b)
    throw std::runtime_error("Native batch assertion");
}
template <class F> void refuses(F f) {
  try {
    f();
  } catch (const std::exception &) {
    return;
  }
  throw std::runtime_error("Expected batch refusal");
}
std::vector<unsigned char> bytes(const std::filesystem::path &p) {
  std::ifstream f(p, std::ios::binary);
  return {std::istreambuf_iterator<char>(f), {}};
}
std::shared_ptr<Document> owner(DatasetShape shape, double value) {
  Stack planes;
  for (std::size_t i = 0; i < shape.plane_count(); ++i) {
    Image im(shape.x, shape.y, shape.s, PixelType::UInt16);
    std::fill(im.pixels.begin(), im.pixels.end(), value);
    planes.push_back(std::move(im));
  }
  return std::make_shared<Document>(
      Dataset::from_planes(shape, std::move(planes)));
}
} // namespace
int main(int argc, char **argv) {
  try {
    check(argc == 2);
    namespace fs = std::filesystem;
    auto dir = fs::absolute(fs::u8path(argv[1])) /
               std::to_string(
                   std::chrono::steady_clock::now().time_since_epoch().count());
    check(!fs::exists(dir));
    fs::create_directories(dir);
    fs::create_directory(dir / "outputs");
    auto a = owner({2, 3, 2, 4, 5, 1}, 10), b = owner({1, 1, 3, 3, 7, 1}, 20);
    auto display = a->snapshot().display();
    display.t = 1;
    display.c = 2;
    a->set_display(display);
    auto edit = a->begin_edit();
    edit.set_extension("opaque",
                       metadata_object({{"uint", std::uint64_t(-1)}}));
    a->commit(edit);
    SessionCapture capture{
        {a->snapshot(), b->snapshot()},
        {metadata_object({{"result", 42}})},
        {metadata_object({{"schema", "unexecuted.input.workflow"}})},
        {{"opaque", metadata_array({true, std::uint64_t(-1)})}}};
    save_session((dir / "first.ijx").u8string(), capture);
    save_session((dir / "second.ijx").u8string(), capture);
    auto original = bytes(dir / "first.ijx");
    NativeWorkflowOperation op;
    op.operation = "add";
    op.parameters = {{"value", std::int64_t(5)}};
    op.target.scope = TargetScope::AllZ;
    op.target.roi = RoiPolicy::WholePlane;
    std::vector<NativeWorkflowBatchInput> inputs{
        {(dir / "first.ijx").u8string(), "one"},
        {(dir / "second.ijx").u8string(), "two"}};
    auto prepared = prepare_native_workflow_batch(inputs, {op},
                                                  (dir / "outputs").u8string());
    check(!fs::exists(dir / "outputs/one.ijx") &&
          prepared.outputs().size() == 2);
    prepared.publish();
    check(prepared.items()[0].state == NativeWorkflowBatchState::Committed &&
          prepared.items()[1].state == NativeWorkflowBatchState::Committed);
    auto result = load_session((dir / "outputs/one.ijx").u8string());
    check(result.documents.size() == 2 &&
          metadata_equal(result.results, capture.results) &&
          metadata_equal(result.workflow, capture.workflow) &&
          metadata_equal(result.extensions, capture.extensions));
    check(result.documents[0]->snapshot().data().dataset.plane({1, 2, 0})->at(
              0, 0) == 15);
    check(result.documents[0]->snapshot().data().dataset.plane({0, 0, 0})->at(
              0, 0) == 10);
    check(result.documents[1]->snapshot().data().dataset.plane({0, 0, 0})->at(
              0, 0) == 20);
    check(bytes(dir / "first.ijx") == original);
    refuses([&] { prepared.publish(); });
    NativeWorkflowOperation gaussian;
    gaussian.operation = "gaussian_blur_imagej";
    gaussian.target = op.target;
    gaussian.original =
        resolve_gaussian_command(a->snapshot(), op.target, {2, false}).record();
    gaussian.filter_options = GaussianBlurCommandOptions{0, false};
    auto disabled = op;
    disabled.enabled = false;
    disabled.target.scope = TargetScope::Positions;
    disabled.target.positions = {{999, 999, 999}};
    auto portable = make_native_workflow_program({disabled, op, gaussian});
    auto restored = native_workflow_program_from_json(portable.json());
    check(restored.json() == portable.json() && restored.steps().size() == 3);
    NativeWorkflowBatchOptions all;
    all.all_documents = true;
    inputs[0].output_stem = "all";
    auto every = prepare_native_workflow_batch(
        {inputs[0]}, restored.steps(), (dir / "outputs").u8string(), all);
    every.publish();
    check(load_session((dir / "outputs/all.ijx").u8string())
              .documents[1]
              ->snapshot()
              .data()
              .dataset.plane({0, 0, 2})
              ->at(0, 0) == 25);
    inputs[0].output_stem = "race-one";
    auto review = review_native_workflow_batch_input(inputs[0].path);
    check(review.documents().size() == 2 && review.fingerprint().size() == 64);
    review.verify();
    inputs[0].program = std::make_shared<const NativeWorkflowProgram>(restored);
    inputs[0].selection = NativeWorkflowBatchSelection{false, 1};
    inputs[0].expected_fingerprint = review.fingerprint();
    auto setup = make_native_workflow_batch_setup(restored, {inputs[0]},
                                                  (dir / "outputs").u8string());
    auto setup_copy = native_workflow_batch_setup_from_json(setup.json());
    check(metadata_equal(setup.record(), setup_copy.record()) &&
          setup_copy.inputs()[0].expected_fingerprint.empty());
    save_native_workflow_batch_setup((dir / "setup.json").u8string(),
                                     setup_copy);
    refuses([&] {
      save_native_workflow_batch_setup((dir / "setup.json").u8string(),
                                       setup_copy);
    });
    check(load_native_workflow_batch_setup((dir / "setup.json").u8string())
              .inputs()[0]
              .selection->document_index == 1);
    const auto setup_bytes = bytes(dir / "setup.json");
    auto setup_cancel = std::make_shared<CancellationToken>();
    ExecutionControl setup_control;
    setup_control.cancellation = setup_cancel;
    bool reached_staging = false;
    setup_control.stage_progress = [&](std::string_view stage, std::size_t,
                                       std::size_t) {
      if (stage == "ROI export awaiting publication") {
        reached_staging = true;
        setup_cancel->cancel();
      }
    };
    refuses([&] {
      save_native_workflow_batch_setup((dir / "setup.json").u8string(),
                                       setup_copy, true, setup_control);
    });
    check(reached_staging && bytes(dir / "setup.json") == setup_bytes);
    setup_cancel = std::make_shared<CancellationToken>();
    setup_control.cancellation = setup_cancel;
    bool reached_read = false;
    setup_control.stage_progress =
        [&](std::string_view stage, std::size_t completed, std::size_t total) {
          if (stage == "Session file read" && completed == total) {
            reached_read = true;
            setup_cancel->cancel();
          }
        };
    refuses([&] {
      load_native_workflow_batch_setup((dir / "setup.json").u8string(), {},
                                       setup_control);
    });
    check(reached_read && bytes(dir / "setup.json") == setup_bytes);
    inputs[0].output_stem = "override";
    auto overridden = prepare_native_workflow_batch(
        {inputs[0]}, {}, (dir / "outputs").u8string(), all);
    overridden.publish();
    auto selected = load_session((dir / "outputs/override.ijx").u8string());
    check(selected.documents[0]->snapshot().data().dataset.plane({1, 2, 0})->at(
              0, 0) == 10);
    check(selected.documents[1]->snapshot().data().dataset.plane({0, 0, 0})->at(
              0, 0) == 25);
    inputs[0].program.reset();
    inputs[0].selection.reset();
    inputs[0].expected_fingerprint.clear();
    inputs[0].output_stem = "race-one";
    inputs[1].output_stem = "race-two";
    auto race = prepare_native_workflow_batch(inputs, {op},
                                              (dir / "outputs").u8string());
    {
      std::ofstream f(dir / "outputs/race-two.ijx", std::ios::binary);
      f << "winner";
    }
    refuses([&] { race.publish(); });
    check(race.items()[0].state == NativeWorkflowBatchState::Committed &&
          race.items()[1].state == NativeWorkflowBatchState::Failed &&
          race.outputs()[0].published && !race.outputs()[1].published);
    inputs[0].output_stem = "cancel";
    auto token = std::make_shared<CancellationToken>();
    ExecutionControl control;
    control.cancellation = token;
    auto cancel_state = NativeWorkflowBatchState::Queued;
    refuses([&] {
      prepare_native_workflow_batch(
          {inputs[0]}, {op}, (dir / "outputs").u8string(), {}, control,
          [&](std::size_t, NativeWorkflowBatchState state) {
            cancel_state = state;
            if (state == NativeWorkflowBatchState::Prepared)
              token->cancel();
          });
    });
    check(cancel_state == NativeWorkflowBatchState::Canceled);
    check(!fs::exists(dir / "outputs/cancel.ijx"));
    auto bad = inputs;
    bad[1].path = bad[0].path;
    refuses([&] {
      prepare_native_workflow_batch(bad, {op}, (dir / "outputs").u8string());
    });
    NativeWorkflowBatchOptions tiny;
    tiny.max_working_bytes = 1;
    refuses([&] {
      prepare_native_workflow_batch({inputs[0]}, {op},
                                    (dir / "outputs").u8string(), tiny);
    });
    check(bytes(dir / "first.ijx") == original);
    std::cout
        << "PASS native scientific session batch planning and publication\n";
    return 0;
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
