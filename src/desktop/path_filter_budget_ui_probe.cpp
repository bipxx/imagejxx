#include "path_filter_budget_ui_probe.hpp"

namespace imagejxx::desktop {

std::optional<PathFilterBudgetUiRecipe>
pathFilterBudgetUiRecipe(const std::string &case_id) {
  if (case_id == "budget-gaussian")
    return PathFilterBudgetUiRecipe{false};
  if (case_id == "budget-rank-mean")
    return PathFilterBudgetUiRecipe{true};
  return std::nullopt;
}

void runPathFilterBudgetUiRecipe(const PathFilterBudgetUiRecipe &recipe,
                                const PathFilterBudgetUiActions &actions) {
  const std::string panel = recipe.rank ? "rank." : "gaussian.";
  const auto open = [&] {
    actions.click("host.menu.Process");
    actions.click("host.menu.Process.Filters");
    actions.click(recipe.rank ? "host.menu.command.rank_filter_imagej"
                              : "host.menu.command.gaussian_blur_imagej");
    if (recipe.rank) {
      // The ordinary Rank menu opens Median. Choose Mean through its popup on
      // both entries; its accepted per-method radius remains in this panel.
      actions.click("rank.method");
      actions.click("rank.method.mean");
    }
  };

  open();
  actions.text(recipe.rank ? "rank.radius" : "gaussian.sigma",
               recipe.rank ? "2.5" : "1");
  actions.click(panel + "preview");
  actions.await_preview("ready");

  actions.memory_request(1, "lower_rejected");
  actions.remember("lower_rejected");
  actions.click("preferences.close_button");
  actions.click(panel + "preview");
  actions.await_release("released");

  actions.memory_request(1, "low_accepted");
  actions.remember("low_accepted");
  actions.click("preferences.close_button");
  actions.click(panel + "ok");
  actions.await_refusal("low_apply_refused");
  actions.click("error.ok");

  // Preserve the failed checked Preview request while changing only the real
  // memory preference. Settings must not silently resubmit this request.
  open();
  actions.click(panel + "preview");
  actions.await_preview_refusal("preview_failed");
  actions.memory_request(64, "restored");
  actions.milestone("restored");
  actions.click("preferences.close_button");
  actions.click(panel + "preview"); // Explicit Off.
  actions.click(panel + "preview"); // Explicit On submits a new generation.
  actions.await_preview("preview_retry_ready");
  actions.click(panel + "cancel");
  actions.await_cancelled();
  open();
  // The Driver records the actual accepted options without editing them again.
  actions.milestone("reopened");
  actions.click(panel + "ok");
  actions.await_apply("recovered");
}

} // namespace imagejxx::desktop
