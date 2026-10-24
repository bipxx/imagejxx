#include "../../src/native/imagej_shape_plan.hpp"
#include "../../src/native/kernel_control.hpp"
#include "../../src/native/target_mask_resources.hpp"
#include <imagejxx/composite_path.hpp>
#include <imagejxx/gaussian_command.hpp>
#include <imagejxx/rank_command.hpp>

#include <array>
#include <cstring>
#include <iostream>
#include <limits>
#include <memory>
#include <stdexcept>
#include <utility>
#include <vector>

using namespace imagejxx;
using namespace imagejxx::detail;

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
std::vector<double> triangle() {
  return {0, 2, 2, 1, 6, 2, 1, 2, 6, 4};
}
std::size_t workspace(const ImageJShapeMaskPlan &plan) {
  return plan.packed_bytes + plan.temporary_bytes;
}
void bounds(const ImageJShapeMaskPlan &plan, std::array<int, 4> stored,
            std::array<int, 4> clipped,
            ImageJAttachmentDisposition attachment = ImageJAttachmentDisposition::Retained) {
  check(plan.stored_bounds == stored, "Stored processing bounds differ from literal reference");
  check(plan.clipped_bounds == clipped, "Clipped geometry bounds differ from literal reference");
  check(plan.attachment == attachment, "Cleared attachment was conflated with an empty intersection");
}
void triangle_mask(const Selection &selection) {
  const auto mask = selection.mask(12, 10);
  for (int y = 0; y < 10; ++y)
    for (int x = 0; x < 12; ++x) {
      const bool expected = x >= 2 && y >= 2 && x + y <= 7;
      check(mask.at(x, y) == (expected ? 255 : 0),
            "Triangle mask differs from literal scan cells");
    }
}

class NoRead final : public PlaneSource {
public:
  explicit NoRead(DatasetShape shape = {1, 1, 3, 10, 12, 1}) : shape_(shape) {}
  DatasetShape shape() const override { return shape_; }
  PixelType pixel_type() const override { return PixelType::Float32; }
  std::shared_ptr<const Image> read_plane(PlanePosition) const override {
    ++reads;
    throw std::runtime_error("Path filter planning read source pixels");
  }
  mutable int reads = 0;
private:
  DatasetShape shape_;
};

void literal_bounds_and_moves() {
  // Actual pinned ImageJ 1.54r / reviewed JDK 21 observations. These integer
  // rectangles are literals, not results from another native bounds API.
  const std::vector<double> fractional{0, 2.2, 1.2, 1, 9.2, 1.2, 1, 2.2, 8.2, 4};
  const auto original = fractional;
  bounds(plan_imagej_shape_mask(fractional, 23, 17), {2, 1, 8, 8}, {2, 1, 8, 8});
  check(std::memcmp(fractional.data(), original.data(), original.size() * sizeof(double)) == 0,
        "Processing quantization changed original binary64 geometry");
  bounds(plan_imagej_shape_mask({0, 1, 1, 3, 8, -2, 10, 10, 1, 8, 4}, 12, 10),
         {1, 0, 7, 9}, {1, 0, 7, 9});
  bounds(plan_imagej_shape_mask(
             {0, 0, 0, 1, 10, 0, 2, 15, 5, 10, 10, 3, 8, 12, 2, 12, 0, 10, 4}, 23, 17),
         {0, 0, 13, 12}, {0, 0, 13, 12});

  auto consecutive = triangle();
  consecutive.insert(consecutive.begin(), {0, -10, -10});
  const auto before = consecutive;
  bounds(plan_imagej_shape_mask(consecutive, 12, 10), {2, 2, 4, 4}, {2, 2, 4, 4});
  triangle_mask(imagej_shape_mask(consecutive, 12, 10));
  check(consecutive == before, "Consecutive-move processing rewrote the owner's commands");
  // A move separated by Close is an isolated contour, not an overwritten Move.
  consecutive = triangle();
  consecutive.insert(consecutive.begin(), {0, -10, -10, 4});
  bounds(plan_imagej_shape_mask(consecutive, 12, 10), {-10, -10, 16, 16}, {0, 0, 6, 6});
  triangle_mask(imagej_shape_mask(consecutive, 12, 10));
}

void degenerate_and_extreme_metadata() {
  bounds(plan_imagej_shape_mask({0, 1, 1, 4}, 12, 10),
         {1, 1, 0, 0}, {1, 1, 0, 0}, ImageJAttachmentDisposition::Cleared);
  bounds(plan_imagej_shape_mask({0, 1, 2, 1, 6, 2, 4}, 12, 10),
         {1, 2, 5, 0}, {1, 2, 5, 0});
  bounds(plan_imagej_shape_mask({0, 2, 1, 1, 2, 6, 4}, 12, 10),
         {2, 1, 0, 5}, {2, 1, 0, 5});
  bounds(plan_imagej_shape_mask({0, 30, 30, 1, 34, 30, 1, 34, 34, 1, 30, 34, 4}, 12, 10),
         {30, 30, 4, 4}, {0, 0, 0, 0});
  // Metadata-only pinned observation. No huge reference mask or Java numerical
  // result exists; this asserts only bounds and native bounded planner storage.
  const auto huge = plan_imagej_shape_mask(
      {0, -1e9, 1, 1, 50, 1, 1, 50, 6, 1, -1e9, 6, 4}, 80, 10);
  bounds(huge, {-1000000000, 1, 1000000050, 5}, {0, 1, 50, 5});
  check(huge.packed_bytes == 100 && workspace(huge) < 16384,
        "Off-canvas extent was charged as an enormous unbounded mask");
}

void exact_work_and_workspace_caps() {
  const auto path = triangle();
  const auto plan = plan_imagej_shape_mask(path, 12, 10);
  check(plan.packed_bytes == 15 && plan.flattened_edges == 3 && plan.work_units > 0,
        "Planner omitted packed storage, flattened edges or traversal work");
  const auto cap = workspace(plan);
  check(workspace(plan_imagej_shape_mask(path, 12, 10, {}, cap)) == cap,
        "Exact advertised scratch boundary was refused or changed");
  refuses([&] { plan_imagej_shape_mask(path, 12, 10, {}, cap - 1); },
          "One byte below advertised workspace was admitted");
  refuses([&] { plan_imagej_shape_mask(path, 12, 10, {}, 0); },
          "Zero scratch allowance was admitted");
  ImageJRoiLimits limits;
  limits.max_path_work = plan.work_units;
  check(plan_imagej_shape_mask(path, 12, 10, limits).work_units == plan.work_units,
        "Dry run changed complete traversal work at its exact boundary");
  triangle_mask(imagej_shape_mask(path, 12, 10, limits));
  --limits.max_path_work;
  refuses([&] { plan_imagej_shape_mask(path, 12, 10, limits); },
          "Late scan work overflow escaped metadata admission");
  refuses([&] { imagej_shape_mask(path, 12, 10, limits); },
          "Raster execution used a weaker traversal work limit than admission");
  limits = {};
  limits.max_mask_pixels = 119;
  refuses([&] { plan_imagej_shape_mask(path, 12, 10, limits); }, "Canvas limit was ignored");
  limits = {};
  limits.max_vertices = 2;
  refuses([&] { plan_imagej_shape_mask(path, 12, 10, limits); }, "Segment limit was ignored");
  // A wire/retained-byte cap is not a synthetic raster scratch cap. Preserve
  // the existing distinction for a 44-double rounded processing contour.
  Selection rounded("rounded_rectangle", {{2, 2}, {6, 6}});
  rounded.attributes["corner_diameter"] = std::int64_t(0);
  const auto contour = rounded.rounded_path(true);
  const std::vector<double> synthetic(contour.begin(), contour.end());
  limits = {};
  limits.max_bytes = 64;
  refuses([&] { ImageJCompositePath retained(synthetic, limits); },
          "Retained path construction lost its original byte limit");
  bounds(plan_imagej_shape_mask(synthetic, 12, 10, limits), {2, 2, 4, 4}, {2, 2, 4, 4});
  const auto mask = imagej_shape_mask(synthetic, 12, 10, limits).mask(12, 10);
  for (int y = 0; y < 10; ++y)
    for (int x = 0; x < 12; ++x)
      check(mask.at(x, y) == ((x >= 2 && x < 6 && y >= 2 && y < 6) ? 255 : 0),
            "Low wire-byte cap changed synthetic rounded rasterization");
}

void quiet_scope_and_cancellation() {
  std::array<int, 3> calls{};
  ExecutionControl parent_control;
  parent_control.checkpoint = [&] { ++calls[0]; };
  parent_control.progress = [&](auto, auto) { ++calls[1]; };
  parent_control.stage_progress = [&](auto, auto, auto) { ++calls[2]; };
  ExecutionScope parent(parent_control);
  {
    ExecutionControl child_control;
    child_control.checkpoint = [&] { ++calls[0]; };
    ExecutionScope child(child_control);
    (void)plan_imagej_shape_mask(triangle(), 12, 10);
    refuses([&] { plan_imagej_shape_mask(triangle(), 12, 10, {}, 0); },
            "Quiet refusal did not enforce the cap");
    check(calls == std::array<int, 3>{0, 0, 0} && !parent.dispatching_callback &&
              !child.dispatching_callback,
          "Planner dispatched inherited user code or failed to restore scopes");
  }
  kernel_checkpoint();
  check(calls[0] > 0, "Planner permanently suppressed the parent scope");
  calls = {};
  parent_control.cancellation = std::make_shared<CancellationToken>();
  parent_control.cancellation->cancel();
  {
    ExecutionControl child_control;
    ExecutionScope child(child_control);
    refuses<OperationCancelled>([&] { plan_imagej_shape_mask(triangle(), 12, 10); },
                                "Quiet admission lost inherited cancellation");
    check(calls == std::array<int, 3>{0, 0, 0} && !parent.dispatching_callback &&
              !child.dispatching_callback,
          "Canceled admission leaked callbacks or dispatch state");
  }
}

void target_serial_peak_and_regions() {
  auto source = std::make_shared<NoRead>();
  Document owner{Dataset(source)};
  const auto selection = Selection::from_path(triangle());
  owner.select(selection);
  const auto before = owner.snapshot();
  const auto history = owner.undo_steps();
  const auto command = resolve_gaussian_command(before);
  const auto amount = workspace(plan_imagej_shape_mask(triangle(), 12, 10));
  check(target_mask_workspace(command.target(), "gaussian_blur_imagej", amount) == amount,
        "Target supplement differs from the shared path scratch plan");
  TargetRequest all;
  all.scope = TargetScope::AllZ;
  const auto stack = resolve_gaussian_command(before, all);
  check(target_mask_workspace(stack.target(), "gaussian_blur_imagej", amount) == amount,
        "Serial planes were charged a mask scratch peak per plane");
  refuses([&] { target_mask_workspace(stack.target(), "gaussian_blur_imagej", amount - 1); },
          "Target helper ignored its remaining scratch allowance");
  const auto region = gaussian_command_region(command.target(), 0);
  check(region.extent && region.extent->x == 2 && region.extent->y == 2 &&
            region.extent->width == 4 && region.extent->height == 4 && region.mask,
        "Gaussian path region did not consume planned processor bounds");
  for (int y = 0; y < 10; ++y)
    for (int x = 0; x < 12; ++x)
      check(region.mask->at(x, y) == ((x >= 2 && y >= 2 && x + y <= 7) ? 255 : 0),
            "Gaussian path mask differs from independent triangle cells");
  const auto rank = resolve_rank_command(before);
  const auto rank_region = rank_command_region(rank.target(), 0);
  check(rank_region.extent && rank_region.extent->width == 4 && rank_region.mask &&
            rank_region.mask->pixels == region.mask->pixels,
        "Rank adapter did not consume the shared region");
  all.roi = RoiPolicy::WholePlane;
  check(target_mask_workspace(resolve_gaussian_command(before, all).target(),
                              "gaussian_blur_imagej", 0) == 0,
        "Whole-plane intent inherited unused active-path scratch");
  check(owner.snapshot().shares_data(before) && owner.undo_steps() == history && source->reads == 0,
        "Target planning changed history/geometry or read source pixels");
}

void positioned_non_area_coverage() {
  // An oversized canvas makes accidental path admission observable without
  // allocating or reading that canvas. Both range and explicit targets are used.
  auto source = std::make_shared<NoRead>(DatasetShape{1, 1, 3, 5000, 5000, 1});
  DocumentData data{Dataset(source)};
  data.rois = {{"path", Selection::from_path(triangle()), {}, {}, 1, {}}};
  Document owner(data);
  TargetRequest request;
  request.roi = RoiPolicy::PositionedRois;
  request.scope = TargetScope::Positions;
  request.positions = {{0, 0, 0}};
  check(target_mask_workspace(resolve_gaussian_command(owner.snapshot(), request).target(),
                              "gaussian_blur_imagej", 0) == 0,
        "Unmatched path was raster-admitted");
  request.positions = {{0, 0, 1}};
  refuses([&] {
    target_mask_workspace(resolve_gaussian_command(owner.snapshot(), request).target(),
                          "gaussian_blur_imagej");
  }, "Matching oversized path escaped its raster limit");
  data.rois.push_back({"non-area", Selection("line", {{0, 0}, {1, 1}}), {}, {}, 1, {}});
  Document covered(data);
  check(target_mask_workspace(resolve_gaussian_command(covered.snapshot(), request).target(),
                              "gaussian_blur_imagej", 0) == 0,
        "Non-area override still charged an unused path on its plane");
  refuses([&] {
    target_mask_workspace(resolve_target(covered.snapshot(), request), "auto_mask");
  }, "ImageJ non-area override leaked into generic mask semantics");

  data.rois[0].z.reset();
  data.rois.push_back({"z0", Selection("point", {{0, 0}}), {}, {}, 0, {}});
  data.rois.push_back({"z2", Selection("point", {{0, 0}}), {}, {}, 2, {}});
  Document all_covered(data);
  request.scope = TargetScope::AllZ;
  request.positions.clear();
  check(target_mask_workspace(resolve_gaussian_command(all_covered.snapshot(), request).target(),
                              "gaussian_blur_imagej", 0) == 0,
        "Union of positioned non-area overrides failed to cover the path domain");
  data.rois.pop_back();
  Document partly_covered(data);
  refuses([&] {
    target_mask_workspace(resolve_gaussian_command(partly_covered.snapshot(), request).target(),
                          "gaussian_blur_imagej");
  }, "A partial non-area override hid an actually consumed path");
  check(source->reads == 0, "Positioned mask preflight read oversized source pixels");
}

void empty_filter_policy_and_malformed_input() {
  auto source = std::make_shared<NoRead>();
  Document owner{Dataset(source)};
  for (const auto &path : {std::vector<double>{0, 1, 1, 4},
                           std::vector<double>{0, 1, 2, 1, 6, 2, 4},
                           std::vector<double>{0, 2, 1, 1, 2, 6, 4}}) {
    owner.select(Selection::from_path(path));
    const auto before = owner.snapshot();
    const auto region = gaussian_command_region(resolve_gaussian_command(before).target(), 0);
    check(region.extent && region.extent->width == 0 && region.extent->height == 0 && region.mask,
          "Degenerate native path silently became a whole-plane filter target");
    for (const auto value : region.mask->pixels) check(value == 0, "Degenerate path selected pixels");
    check(owner.snapshot().shares_data(before), "Empty path region changed the owner");
  }
  for (const auto &path : {std::vector<double>{}, std::vector<double>{1, 2, 3},
                           std::vector<double>{0, 2}, std::vector<double>{0, 1e9 + 1, 2},
                           std::vector<double>{0, 2, 2, 5}})
    refuses([&] { plan_imagej_shape_mask(path, 12, 10); }, "Malformed path was admitted");
  check(source->reads == 0, "Empty/malformed path planning read source pixels");
}
} // namespace

int main() {
  try {
    const std::pair<const char *, void (*)()> cases[] = {
        {"I-PLAN-01 bounds-and-moves", literal_bounds_and_moves},
        {"I-PLAN-02 degenerate-and-extreme", degenerate_and_extreme_metadata},
        {"I-PLAN-03 work-and-workspace", exact_work_and_workspace_caps},
        {"I-PLAN-04 quiet-and-cancel", quiet_scope_and_cancellation},
        {"I-PLAN-05 serial-and-regions", target_serial_peak_and_regions},
        {"I-PLAN-06 positioned-policy", positioned_non_area_coverage},
        {"I-PLAN-07 empty-and-malformed", empty_filter_policy_and_malformed_input}};
    for (const auto &item : cases) {
      std::cout << item.first << '\n';
      item.second();
    }
    std::cout << "Path filter planning checks passed (7 cases)\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
