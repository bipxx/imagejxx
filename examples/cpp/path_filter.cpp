// Installed-SDK consumer: public path filters, explicit budgets and no-read refusal.
#include <imagejxx/gaussian_command.hpp>
#include <imagejxx/manual_threshold_command.hpp>
#include <imagejxx/native_workflow.hpp>
#include <imagejxx/rank_command.hpp>
#include <iostream>
#include <limits>
#include <memory>
#include <stdexcept>
#include <type_traits>

using namespace imagejxx;
namespace {
std::size_t checks = 0;
void check(bool value) {
  if (!value) throw std::runtime_error("Installed path-filter contract failed");
  ++checks;
}
template<class Function> void refused(Function function) {
  bool rejected = false;
  try { function(); } catch (const std::invalid_argument &) { rejected = true; }
  check(rejected);
}
class NoRead final : public PlaneSource {
public:
  DatasetShape shape() const override { return {1, 1, 1, 17, 23, 1}; }
  PixelType pixel_type() const override { return PixelType::Float32; }
  std::shared_ptr<const Image> read_plane(PlanePosition) const override {
    ++reads;
    throw std::runtime_error("SDK admission reached a provider read");
  }
  mutable int reads = 0;
};
}

int main() {
  try {
    static_assert(std::is_aggregate_v<ExecutionOptions>);
    static_assert(std::is_aggregate_v<NativeWorkflowLimits>);
    const ExecutionOptions old_options{DatasetPolicy::PreserveUntargeted,
        GeometryPolicy::RequireTransform, false, ExecutionPhase::Apply};
    const NativeWorkflowLimits old_limits{4, 1024*1024};
    check(old_options.max_target_mask_bytes == std::numeric_limits<std::size_t>::max());
    check(old_limits.max_working_bytes == std::numeric_limits<std::size_t>::max());
    auto source = std::make_shared<NoRead>();
    Document owner{Dataset(source)};
    const auto selection = Selection::from_path({0,2.2,1.2,1,9.2,1.2,1,2.2,8.2,4});
    owner.select(selection);
    const auto before = owner.snapshot();
    const auto history = owner.undo_steps();
    const auto gaussian = resolve_gaussian_command(before, {}, {1, false});
    const auto region = gaussian_command_region(gaussian.target(), 0);
    check(region.extent && region.mask);
    check(region.extent->x == 2 && region.extent->y == 1 &&
          region.extent->width == 8 && region.extent->height == 8);
    for (int y=0; y<17; ++y) for (int x=0; x<23; ++x)
      check(region.mask->at(x,y) == (y>=1 && y<=7 && x>=2 && x+y<=9 ? 255 : 0));
    int callbacks = 0;
    ExecutionControl control;
    control.checkpoint = [&] { ++callbacks; };
    control.progress = [&](auto,auto) { ++callbacks; };
    control.stage_progress = [&](auto,auto,auto) { ++callbacks; };
    auto options = old_options;
    options.max_target_mask_bytes = 0;
    refused([&] { prepare_operation("gaussian_blur_imagej", gaussian.target(),
                                    gaussian.parameters(), options, control); });
    check(callbacks == 0 && source->reads == 0);
    NativeWorkflowOperation operation;
    operation.operation = "gaussian_blur_imagej";
    operation.filter_options = GaussianBlurCommandOptions{1, false};
    auto limits = old_limits;
    limits.max_working_bytes = 1;
    refused([&] { prepare_native_workflow(before, {operation}, limits, control); });
    check(callbacks == 0 && source->reads == 0);
    check(owner.snapshot().shares_data(before) && owner.undo_steps() == history);
    check(owner.snapshot().data().selection == before.data().selection);
    // Manual threshold keeps its eager-only source boundary. Native-path
    // records need the new reader; primitive V1/V2 grammars remain unchanged.
    refused([&] { resolve_manual_threshold_command(before, {101, 255}); });
    Image manual_pixels(23, 17, 1, PixelType::UInt8);
    manual_pixels.pixels.assign(391, 100.);
    Document manual_owner{Dataset::from_stack({manual_pixels})};
    manual_owner.select(selection);
    const auto manual_before = manual_owner.snapshot();
    const auto threshold = resolve_manual_threshold_command(manual_before, {101, 255});
    const auto threshold_record = threshold.record();
    check(std::get<std::int64_t>(threshold_record.at("schema_version")) == 3);
    check(std::get<std::string>(threshold_record.at("profile")) == "manual_raw_inclusive_v3");
    validate_manual_threshold_command_record(threshold_record);
    const auto replay = manual_threshold_command_from_record(manual_before, threshold_record);
    const auto replay_position = replay.target().position(0);
    check(replay.target().position_count() == 1 && replay_position.t == 0 &&
          replay_position.c == 0 && replay_position.z == 0);
    check(replay.range().lower == 101 && replay.range().upper == 255);
    check(metadata_equal(replay.record(), threshold_record));
    check(metadata_equal(rebind_manual_threshold_command(manual_before, threshold_record, {}).record(), threshold_record));
    for (const std::int64_t version : {1, 2}) {
      auto legacy_threshold = threshold_record;
      legacy_threshold["schema_version"] = version;
      legacy_threshold["profile"] = std::string("manual_raw_inclusive_v") + std::to_string(version);
      refused([&] { validate_manual_threshold_command_record(legacy_threshold); });
    }
    check(callbacks == 0 && source->reads == 0);
    // Literal two-sample edge-replication case: sigma1 produces [77,178].
    Image pixels(2, 1, 1, PixelType::UInt8);
    pixels.pixels = {0, 255};
    Document eager{Dataset::from_stack({pixels})};
    eager.select(Selection::from_path({0,0,0,1,2,0,1,2,1,1,0,1,4}));
    const auto eager_before = eager.snapshot();
    const auto eager_history = eager.undo_steps();
    const auto command = resolve_gaussian_command(eager_before, {}, {1, false});
    const auto prepared = prepare_operation("gaussian_blur_imagej", command.target(), command.parameters());
    const auto output = prepared.preview().data().dataset.plane({0,0,0});
    check(output->type == PixelType::UInt8 && output->width == 2 && output->height == 1);
    check(output->pixels == std::vector<double>({77,178}));
    check(eager.snapshot().shares_data(eager_before) && eager.undo_steps() == eager_history);
    std::cout << "PASS " << checks << " independent path filter SDK checks\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
