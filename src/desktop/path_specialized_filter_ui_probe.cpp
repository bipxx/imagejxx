#include "path_specialized_filter_ui_probe.hpp"

namespace imagejxx::desktop {

std::optional<PathSpecializedFilterUiRecipe>
pathSpecializedFilterUiRecipe(const std::string &case_id) {
  using Kind = PathSpecializedFilterUiKind;
  if (case_id == "specialized-despeckle")
    return PathSpecializedFilterUiRecipe{};
  for (const auto *which : {"bright", "dark"})
    for (bool inverted : {false, true}) {
      const auto id = std::string("specialized-outliers-") + which +
                      (inverted ? "-inverted" : "-grays");
      if (case_id == id)
        return PathSpecializedFilterUiRecipe{Kind::Outliers, inverted, false,
                                             false, which, "finite"};
    }
  for (bool light : {false, true})
    for (bool dontSubtract : {false, true})
      for (bool inverted : {false, true}) {
        const auto id = std::string("specialized-top-hat-") +
                        (light ? "light" : "dark") +
                        (dontSubtract ? "-no-subtract" : "-subtract") +
                        (inverted ? "-inverted" : "-grays");
        if (case_id == id)
          return PathSpecializedFilterUiRecipe{Kind::TopHat, inverted, light,
                                               dontSubtract, "bright", "finite"};
      }
  for (const auto *pattern : {"finite", "quiet", "ieee"})
    if (case_id == std::string("specialized-remove-nans-") + pattern)
      return PathSpecializedFilterUiRecipe{Kind::RemoveNaNs, false, false,
                                           false, "bright", pattern};
  return std::nullopt;
}

void runPathSpecializedFilterUiRecipe(
    const PathSpecializedFilterUiRecipe &recipe,
    const PathSpecializedFilterUiActions &actions) {
  using Kind = PathSpecializedFilterUiKind;
  actions.click("host.menu.Image");
  actions.click("host.menu.Image.Lookup_table");
  actions.click(recipe.inverted_lut ? "host.menu.Image.Lookup_table.Inverted"
                                    : "host.menu.Image.Lookup_table.Grays");
  actions.remember("lut_selected");

  actions.click("host.menu.Process");
  actions.click(recipe.kind == Kind::TopHat ? "host.menu.Process.Filters"
                                          : "host.menu.Process.Noise");
  if (recipe.kind == Kind::Despeckle) {
    actions.click("host.menu.action.despeckle_imagej");
    // A plain single plane has a direct Apply path. There is no dialog or
    // Preview action here, and no synthetic dialog state is inserted.
    actions.await_apply("applied");
    return;
  }

  const auto operation = recipe.kind == Kind::Outliers ? "remove_outliers_imagej"
                         : recipe.kind == Kind::TopHat ? "top_hat_imagej"
                                                       : "remove_nans_imagej";
  actions.click(std::string("host.menu.command.") + operation);
  actions.text("rank.radius", "2");
  if (recipe.kind == Kind::Outliers) {
    actions.text("rank.threshold", "3.5");
    actions.click("rank.which");
    actions.click("rank.outlier." + recipe.which_outliers);
  } else if (recipe.kind == Kind::TopHat) {
    if (recipe.light_background) actions.click("rank.light_background");
    if (recipe.dont_subtract) actions.click("rank.dont_subtract");
  }
  actions.remember("configured");
  actions.click("rank.preview");
  actions.await_preview("preview");
  actions.click("rank.ok");
  actions.await_apply("applied");
}

} // namespace imagejxx::desktop
