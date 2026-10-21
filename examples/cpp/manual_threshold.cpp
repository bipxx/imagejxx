// Installed public C++17 SDK consumer; no repository-private headers or helpers.
#include <imagejxx/application.hpp>
#include <imagejxx/manual_threshold.hpp>

#include <iostream>
#include <memory>
#include <numeric>
#include <stdexcept>
#include <vector>

namespace {
using namespace imagejxx;
std::size_t passed = 0;
void require(bool value, const char *message) {
  if (!value) throw std::runtime_error(message);
  ++passed;
}
} // namespace

int main() {
  try {
    using namespace imagejxx;
    Image untouched(3, 3, 1, PixelType::UInt16);
    untouched.pixels = {700, 700, 700, 700, 700, 700, 700, 700, 700};
    Image selected_plane(3, 3, 1, PixelType::UInt16);
    selected_plane.pixels = {5, 5, 5, 5, 900, 5, 5, 5, 5};
    const auto original_plane = selected_plane.pixels;
    const auto dataset = Dataset::from_stack({untouched, selected_plane});
    const auto shape = dataset.shape();
    const ManualThresholdRange range{5, 5};

    const auto estimate = estimate_manual_threshold(shape, dataset.pixel_type(),
                                                     {0, 0, 1});
    require(estimate.pixel_count == 9 && estimate.analysis_bytes == 4104 &&
                estimate.mask_bytes == 72 && estimate.selection_bytes == 2,
            "SDK estimate counted more than the selected XY plane");
    const auto summary = analyze_manual_threshold(selected_plane, range);
    require(summary.minimum == 5 && summary.maximum == 900 &&
                summary.sample_count == 9 && summary.matched_count == 8,
            "SDK threshold raw extrema or inclusive count differs");
    require(summary.histogram.counts.size() == 256 &&
                summary.histogram.edges.size() == 257 &&
                summary.histogram.edges.front() == 0 &&
                summary.histogram.edges.back() == 65536 &&
                summary.histogram.counts[0] == 8 && summary.histogram.counts[3] == 1 &&
                std::accumulate(summary.histogram.counts.begin(),
                                summary.histogram.counts.end(), std::uint64_t(0)) == 9,
            "SDK UInt16 histogram does not use bounded raw intensity bins");
    const auto mask = manual_threshold_mask(selected_plane, range);
    require(mask.type == PixelType::UInt8 && mask.width == 3 && mask.height == 3 &&
                mask.channels == 1 && mask.pixels ==
                    std::vector<double>({255, 255, 255, 255, 0, 255, 255, 255, 255}),
            "SDK threshold mask changed type, extent, inclusive bounds or hole");
    const auto selection = manual_threshold_selection(selected_plane, range);
    require(selection.kind == "mask" && selection.packed_mask() ==
                std::vector<unsigned char>({0xf7, 0x80}),
            "SDK threshold selection lost the central hole or packed bit order");
    require(selection.mask(3, 3).pixels == mask.pixels &&
                selected_plane.pixels == original_plane,
            "SDK threshold selection changed source pixels");

    Application application;
    const auto id = application.create(dataset, "Manual threshold SDK");
    const auto original = application.snapshot(id);
    auto view = original.display();
    view.properties["lut"] = std::string("Fire");
    view.crosshair = {0, 0, 1};
    set_manual_threshold_state(view, shape, {range, {0, 0, 1}});
    application.set_display(view, id);
    const auto enabled = application.snapshot(id);
    const auto state = manual_threshold_state(enabled.display(), shape);
    require(state && state->range.lower == 5 && state->range.upper == 5 &&
                state->position.t == 0 && state->position.c == 0 && state->position.z == 1,
            "SDK typed threshold display state did not round-trip");
    require(enabled.shares_data(original) &&
                enabled.data_identity() == original.data_identity() && !enabled.dirty() &&
                application.history(id).undo_steps == 0 &&
                enabled.revisions().pixels == 0 && enabled.revisions().metadata == 0 &&
                enabled.revisions().selection == 0 && enabled.revisions().display == 1,
            "SDK threshold preview changed data identity or pixel history");
    reset_manual_threshold_state(view);
    application.set_display(view, id);
    require(!manual_threshold_state(application.snapshot(id).display(), shape) &&
                std::get<std::string>(application.snapshot(id).display().properties.at("lut")) == "Fire" &&
                application.history(id).undo_steps == 0,
            "SDK threshold reset changed unrelated presentation or pixel history");

    application.select(selection, id);
    require(!application.snapshot(id).dirty() && application.history(id).undo_steps == 0 &&
                application.snapshot(id).revisions().pixels == 0,
            "SDK threshold-to-selection created a pixel edit");
    TargetRequest request;
    request.scope = TargetScope::CurrentPlane;
    request.roi = RoiPolicy::ActiveSelection;
    const auto target = application.resolve_target(request, id, "threshold");
    require(target.position_count() == 1 && target.position(0).z == 1,
            "SDK current-plane target did not capture the selected Z");
    // Navigation after capture must not move the operation to another plane.
    view = application.snapshot(id).display();
    view.crosshair[2] = 0;
    application.set_display(view, id);
    ExecutionOptions options;
    options.dataset = DatasetPolicy::PreserveUntargeted;
    auto prepared = application.prepare("threshold", target,
        {{"lower", 5.0}, {"upper", 5.0}}, options);
    const auto preview = prepared.preview();
    const std::vector<double> expected{255, 255, 255, 255, 900, 255, 255, 255, 255};
    require(preview.data().dataset.pixel_type() == PixelType::UInt16 &&
                preview.data().dataset.plane({0, 0, 1})->pixels == expected &&
                preview.data().dataset.plane({0, 0, 0})->pixels == untouched.pixels,
            "SDK Apply preview changed ROI holes, untouched plane or source dtype");
    require(application.snapshot(id).data().dataset.plane({0, 0, 1})->pixels == original_plane &&
                application.history(id).undo_steps == 0 && !application.snapshot(id).dirty(),
            "SDK preparation published pixels before Apply");
    const auto published = application.publish(prepared);
    require(!published.created && published.document_id == id && !prepared.active(),
            "SDK threshold publication did not consume the in-place operation");
    const auto applied = application.snapshot(id);
    require(applied.data().dataset.pixel_type() == PixelType::UInt16 &&
                applied.data().dataset.plane({0, 0, 0})->pixels == untouched.pixels &&
                applied.data().dataset.plane({0, 0, 1})->pixels == expected &&
                applied.display().crosshair[2] == 0 && applied.dirty() &&
                application.history(id).undo_steps == 1,
            "SDK Apply changed captured scope or failed to create one undo step");
    application.undo(id);
    const auto restored = application.snapshot(id);
    require(restored.data().dataset.pixel_type() == PixelType::UInt16 &&
                restored.data().dataset.plane({0, 0, 0})->pixels == untouched.pixels &&
                restored.data().dataset.plane({0, 0, 1})->pixels == original_plane &&
                !restored.dirty() && application.history(id).undo_steps == 0 &&
                application.history(id).redo_steps == 1,
            "SDK one-step undo did not restore both original planes and dtype");

    ExecutionControl canceled;
    canceled.cancellation = std::make_shared<CancellationToken>();
    canceled.cancellation->cancel();
    bool cancellation_rejected = false;
    try {
      (void)manual_threshold_mask(selected_plane, range, {}, canceled);
    } catch (const OperationCancelled &) {
      cancellation_rejected = true;
    }
    require(cancellation_rejected && selected_plane.pixels == original_plane,
            "SDK pre-canceled mask modified its source or returned successfully");
    bool range_rejected = false;
    try {
      (void)analyze_manual_threshold(selected_plane, {6, 5});
    } catch (const std::invalid_argument &) {
      range_rejected = true;
    }
    require(range_rejected && selected_plane.pixels == original_plane,
            "SDK invalid interval was admitted or changed source pixels");

    std::cout << "Manual threshold SDK: " << passed << " checks passed\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "Manual threshold SDK failed: " << error.what() << '\n';
    return 1;
  }
}
