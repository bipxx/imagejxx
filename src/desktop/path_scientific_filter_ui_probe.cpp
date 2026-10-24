#include "path_scientific_filter_ui_probe.hpp"

#include <array>

namespace imagejxx::desktop {

std::optional<PathScientificFilterUiRecipe>
pathScientificFilterUiRecipe(const std::string &case_id) {
  if (case_id == "scientific-gaussian-active")
    return PathScientificFilterUiRecipe{false, false};
  if (case_id == "scientific-gaussian-positioned")
    return PathScientificFilterUiRecipe{false, true};
  if (case_id == "scientific-rank-mean-active")
    return PathScientificFilterUiRecipe{true, false};
  if (case_id == "scientific-rank-mean-positioned")
    return PathScientificFilterUiRecipe{true, true};
  return std::nullopt;
}

void runPathScientificFilterUiRecipe(
    const PathScientificFilterUiRecipe &recipe,
    const PathScientificFilterUiActions &actions) {
  actions.click("host.menu.Process");
  actions.click("host.menu.Process.Filters");
  actions.click(recipe.rank ? "host.menu.command.rank_filter_imagej"
                            : "host.menu.command.gaussian_blur_imagej");
  if (recipe.rank) {
    actions.click("rank.method");
    actions.click("rank.method.mean");
    actions.text("rank.radius", "2.5");
  } else {
    actions.text("gaussian.sigma", "1");
  }
  actions.expose_panel();

  constexpr std::array<const char *, 3> axes{"t", "c", "z"};
  constexpr std::array<const char *, 3> first{"1", "1", "2"};
  constexpr std::array<const char *, 3> last{"2", "3", "4"};
  constexpr std::array<const char *, 3> step{"1", "2", "2"};
  for (std::size_t index = 0; index < axes.size(); ++index) {
    const auto prefix = std::string("target.") + axes[index];
    actions.click(prefix + ".range");
    actions.field(prefix + ".first", first[index]);
    actions.field(prefix + ".last", last[index]);
    actions.field(prefix + ".step", step[index]);
  }
  actions.click(recipe.positioned ? "target.roi.positioned_rois"
                                  : "target.roi.active_selection");
  actions.field("target.preview.ordinal", "6");
  actions.remember("target_configured");

  actions.click(recipe.rank ? "rank.preview" : "gaussian.preview");
  actions.await_preview("preview_hidden", false);
  actions.click("target.preview.show");
  actions.await_preview("preview_shown", true);
  actions.navigate("t", 0);
  actions.remember("navigated_t");
  actions.navigate("c", 1);
  actions.remember("navigated_c");
  actions.navigate("z", 2);
  actions.remember("navigated_z");
  actions.click("target.preview.show");
  actions.await_preview("preview_restored", true);
  actions.navigate("t", 0);
  actions.navigate("c", 1);
  actions.navigate("z", 2);
  actions.remember("apply_away");
  actions.click(recipe.rank ? "rank.ok" : "gaussian.ok");
  actions.await_apply("applied");
}

} // namespace imagejxx::desktop
