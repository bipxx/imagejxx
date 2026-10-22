#include <chrono>
#include <filesystem>
#include <fstream>
#include <imagejxx/native_workflow_batch.hpp>
#include <imagejxx/native_workflow_batch_setup.hpp>
#include <imagejxx/native_workflow_program.hpp>
#include <iostream>
#include <limits>
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
void statistics_contract(const std::filesystem::path &root) {
  namespace fs=std::filesystem;NativeWorkflowBatchHistogram r;
  r.name="quoted,\"row\"\n.csv";r.pixel_type="uint8";r.sample_policy="finite_only";r.plane_count=1;
  r.visited_count=r.finite_count=r.included_count=std::numeric_limits<std::uint64_t>::max();
  r.minimum=r.maximum=r.mean=1.;r.stddev=0.;r.sum=double(r.finite_count);
  const auto csv=native_workflow_batch_histogram_csv({r});
  check(csv.find("18446744073709551615")!=std::string::npos);
  check(csv.find("\"quoted,\"\"row\"\"\n.csv\"")!=std::string::npos);
  refuses([&]{native_workflow_batch_histogram_csv({r},1);});
  auto invalid=r;invalid.stddev=-1;refuses([&]{native_workflow_batch_histogram_csv({invalid});});
  invalid=r;invalid.nan_count=1;refuses([&]{native_workflow_batch_histogram_csv({invalid});});
  const auto dir=root/"statistics";fs::create_directory(dir);fs::create_directory(dir/"out");
  auto doc=owner({1,1,1,2,2,1},7);SessionCapture capture{{doc->snapshot()},{},{},{}};
  save_session((dir/"input.ijx").u8string(),capture);
  NativeWorkflowHistogram step;step.name="bins.csv";step.target.roi=RoiPolicy::WholePlane;
  NativeWorkflowHistogramOptions choices;choices.histogram.binning=HistogramBinning::UniformRange;
  choices.histogram.bins=1;choices.histogram.range=std::array<double,2>{0,5};step.authored_options=choices;
  const auto program=make_native_workflow_program({step});
  const auto setup=make_native_workflow_batch_setup(program,{{(dir/"input.ijx").u8string(),"stats"}},(dir/"out").u8string());
  const auto changed=setup.with_histogram_statistics(true);
  check(changed.histogram_statistics() && !setup.histogram_statistics());
  check(native_workflow_batch_setup_from_json(changed.json()).histogram_statistics());
  check(changed.with_histogram_statistics(false).json()==setup.json());
  NativeWorkflowBatchOptions options;options.histogram_statistics=true;
  auto batch=prepare_native_workflow_batch(setup.inputs(),program.steps(),setup.output_directory(),options);
  check(batch.histograms().size()==1 && batch.outputs().size()==3);
  const auto value=batch.histograms()[0];
  check(value.finite_count==4 && value.included_count==0 && value.overflow_count==4 && value.mean==7 && value.stddev==0 && value.sum==28);
  batch.publish();std::size_t encoded=0;for(const auto &out:batch.outputs())encoded+=fs::file_size(fs::u8path(out.path));
  check(encoded==batch.encoded_bytes() && fs::exists(dir/"out/stats.histograms.csv"));
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
    {
      Image image(2,1,3,PixelType::UInt8);image.pixels={10,20,30,20,30,40};Document rgb(Dataset::from_stack({image}));
      SessionCapture capture{{rgb.snapshot()},{},{},{}};const auto input=dir/"rgb-input.ijx";save_session(input.u8string(),capture);const auto raw=bytes(input);
      NativeWorkflowOperation apply;apply.operation="rgb_contrast_apply";apply.parameters={{"minimum",10.},{"maximum",40.},{"channels",std::int64_t(4)}};
      auto prepared=prepare_native_workflow_batch({{input.u8string(),"rgb-output"}},{apply},(dir/"outputs").u8string());
      check(!fs::exists(dir/"outputs/rgb-output.ijx"));prepared.publish();
      const auto result=load_session((dir/"outputs/rgb-output.ijx").u8string());
      check(result.documents[0]->snapshot().data().dataset.plane()->pixels==std::vector<double>({0,20,30,85,30,40}));check(bytes(input)==raw);
    }
    statistics_contract(dir);
    std::cout
        << "PASS native scientific session batch planning and publication\n";
    return 0;
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
