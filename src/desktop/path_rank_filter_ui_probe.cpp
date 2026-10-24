#include "path_rank_filter_ui_probe.hpp"

#include <array>

namespace imagejxx::desktop {

std::optional<PathRankFilterUiRecipe>
pathRankFilterUiRecipe(const std::string &case_id) {
  for (const auto *method : std::array<const char *, 5>{
           "minimum", "maximum", "median", "mean", "variance"}) {
    const auto prefix = std::string("rank-") + method;
    if (case_id == prefix + "-in-place")
      return PathRankFilterUiRecipe{method, false};
    if (case_id == prefix + "-new-document")
      return PathRankFilterUiRecipe{method, true};
  }
  return std::nullopt;
}

void runPathRankFilterUiRecipe(const PathRankFilterUiRecipe &recipe,
                               const PathRankFilterUiActions &actions) {
  const auto open = [&] {
    actions.click("host.menu.Process");
    actions.click("host.menu.Process.Filters");
    actions.click("host.menu.command.rank_filter_imagej");
    actions.click("rank.method");
    actions.click("rank.method." + recipe.method);
  };

  open();
  actions.text("rank.radius", "1");
  actions.click("rank.preview");
  actions.await_preview("preview_radius1");
  actions.text("rank.radius", "2.5");
  actions.await_preview("preview_radius2p5");
  actions.click("rank.cancel");
  actions.await_idle("cancelled");

  open();
  actions.text("rank.radius", "2.5");
  if (recipe.new_document)
    actions.click("rank.new_document");
  actions.remember("configured");
  actions.click("rank.preview");
  actions.await_preview("preview_apply");
  actions.click("rank.ok");
  actions.await_apply("applied");
}

} // namespace imagejxx::desktop
