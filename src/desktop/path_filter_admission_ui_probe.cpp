#include "path_filter_admission_ui_probe.hpp"

namespace imagejxx::desktop {

std::optional<PathFilterAdmissionUiRecipe>
pathFilterAdmissionUiRecipe(const std::string &case_id) {
  using Kind = PathFilterAdmissionUiKind;
  if (case_id == "admission-legacy-invert-path") return PathFilterAdmissionUiRecipe{Kind::Invert};
  if (case_id == "admission-pooled-mask-mean-path") return PathFilterAdmissionUiRecipe{Kind::PooledMask};
  if (case_id == "admission-pooled-display-mean-path") return PathFilterAdmissionUiRecipe{Kind::PooledDisplay};
  if (case_id == "admission-workflow-later-path-refusal") return PathFilterAdmissionUiRecipe{Kind::Workflow};
  return std::nullopt;
}

Json pathFilterAdmissionUiRecipeJson(const PathFilterAdmissionUiRecipe &recipe) {
  if (recipe.workflow())
    return {{"operation", "native_workflow"}, {"step_operations", {"add", "gaussian_blur_imagej"}},
      {"destination", "in_place"}, {"roi_policies", {"whole_plane", "active_selection"}},
      {"initial_memory_mib", 64}, {"requested_memory_mib", {1, 64}},
      {"rendered_rgb_encoding", "uint8_hex"}, {"fixture_geometry_authoring_credit", false}};
  return {{"operation", recipe.manual() ? "threshold" : recipe.pooled() ? "auto_mask" : "invert"},
    {"method", recipe.pooled() ? Json("mean") : Json()}, {"destination", "in_place"},
    {"roi_policy", "active_selection"}, {"initial_position", {0, 0, recipe.pooled() ? 1 : 0}},
    {"all_slices", recipe.pooled()}, {"bright", recipe.pooled() ? Json(true) : Json()},
    {"ignore_black", recipe.pooled() ? Json(false) : Json()},
    {"ignore_white", recipe.pooled() ? Json(false) : Json()},
    {"stack_histogram", recipe.pooled()}, {"sample_schema", "p13i.scalar-u8.001"}};
}

Json pathFilterAdmissionProgramFixture() {
  return Json::parse(R"({"schema":"imagejxx.native-workflow-program","steps":[{"filter_options":null,"kind":"operation","operation":"add","original":null,"parameters":{"value":{"kind":"real","value":1.0}},"target":{"c":null,"destination":"in_place","mode":0,"positions":[],"roi":0,"scope":0,"t":null,"z":null},"use_destination_calibration":false},{"filter_options":{"kind":"gaussian","scaled_units":false,"sigma":0.0},"kind":"operation","operation":"gaussian_blur_imagej","original":null,"parameters":{},"target":{"c":null,"destination":"in_place","mode":0,"positions":[],"roi":1,"scope":0,"t":null,"z":null},"use_destination_calibration":false}],"version":2})");
}

void runPathFilterAdmissionUiRecipe(const PathFilterAdmissionUiRecipe &recipe,
                                   const PathFilterAdmissionUiActions &actions) {
  if (recipe.workflow()) {
    const auto review = [&] {
      actions.click("host.workflow.run");
      for (int index = 0; index < 2; ++index) {
        actions.click("program.step." + std::to_string(index));
        actions.click("program.scope");
        actions.click("program.scope.0");
        actions.click("program.region");
        actions.click("program.region." + std::to_string(index));
        actions.click("program.confirm");
      }
    };
    actions.memory_request(1, "low_configured");
    actions.click("preferences.close_button");
    review();
    actions.milestone("low_configured");
    actions.click("program.prepare");
    actions.await_workflow("low_refused", true);
    actions.click("program.cancel");
    actions.memory_request(64, "restored");
    actions.milestone("restored");
    actions.click("preferences.close_button");
    review();
    actions.milestone("review_reopened");
    actions.click("program.prepare");
    actions.await_workflow("recovered_prepared", false);
    actions.click("program.apply");
    actions.await_apply("recovered_applied");
    actions.click("program.cancel");
    return;
  }

  if (recipe.manual()) {
    const auto open = [&] {
      actions.click("host.menu.Process");
      actions.click("host.menu.manual_threshold");
      actions.click("threshold.auto.method");
      actions.click("threshold.auto.method.choice.mean");
      actions.flag("threshold.auto.bright", true);
      actions.flag("threshold.auto.ignore_black", false);
      actions.flag("threshold.auto.ignore_white", false);
      // Pooling is disabled once the scientific target is enabled. Select it
      // first, then explicitly bind the independent Apply region and axes.
      actions.flag("threshold.auto.stack_histogram", true);
      actions.click("threshold.targets.open");
      actions.flag("threshold.targets.enabled", true);
      actions.click("threshold_target.t.current");
      actions.click("threshold_target.c.current");
      actions.click("threshold_target.z.all");
      actions.click("threshold_target.roi.active_selection");
      actions.flag("threshold.targets.histogram_roi", true);
      actions.click("threshold.targets.done");
    };
    open();
    actions.remember("configured");
    actions.click("threshold.auto");
    actions.await_preview("automatic");
    actions.click("threshold.cancel");
    actions.await_idle("cancelled");
    open();
    actions.remember("panel_reopened");
    actions.click("threshold.auto");
    actions.await_preview("automatic_reopened");
    actions.click("threshold.apply");
    actions.await_apply("applied");
    return;
  }

  const auto open = [&] {
    actions.click("host.menu.Process");
    actions.click(recipe.pooled() ? "host.menu.Process.Threshold" : "host.menu.Process.Math");
    actions.click(recipe.pooled() ? "host.menu.command.auto_mask" : "host.menu.command.invert");
    if (recipe.pooled()) {
      actions.click("dialog.method");
      actions.click("dialog.method.choice.mean");
      actions.flag("dialog.bright", true);
      actions.flag("dialog.ignore_black", false);
      actions.flag("dialog.ignore_white", false);
      actions.flag("dialog.stack_histogram", true);
    } else actions.flag("dialog.all_slices", false);
  };
  open();
  actions.remember("configured");
  actions.click("dialog.preview");
  actions.await_preview("preview_ready");
  actions.click("dialog.cancel");
  actions.await_idle("cancelled");
  open();
  actions.remember("panel_reopened");
  actions.click("dialog.apply");
  actions.await_apply("applied");
}

} // namespace imagejxx::desktop
