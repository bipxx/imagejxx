#include "path_filter_stack_ui_probe.hpp"

namespace imagejxx::desktop {

std::optional<PathFilterStackUiRecipe>
pathFilterStackUiRecipe(const std::string &case_id) {
  for (bool rank : {false, true})
    for (const char *choice : {"yes", "no", "cancel"})
      if (case_id == std::string("stack-") + (rank ? "rank-mean-" : "gaussian-") + choice)
        return PathFilterStackUiRecipe{rank, choice};
  return std::nullopt;
}

void runPathFilterStackUiRecipe(const PathFilterStackUiRecipe &recipe,
                               const PathFilterStackUiActions &actions) {
  const std::string panel = recipe.rank ? "rank." : "gaussian.";
  const auto open = [&] {
    actions.click("host.menu.Process");
    actions.click("host.menu.Process.Filters");
    actions.click(recipe.rank ? "host.menu.command.rank_filter_imagej"
                              : "host.menu.command.gaussian_blur_imagej");
    if (recipe.rank) {
      actions.click("rank.method");
      actions.click("rank.method.mean");
    }
  };

  open();
  actions.inspect_initial();
  actions.text(recipe.rank ? "rank.radius" : "gaussian.sigma",
               recipe.rank ? "2.5" : "1");
  actions.remember("configured");
  actions.click(panel + "ok");
  actions.remember("prompt");
  actions.move_display();
  actions.remember("display_moved");
  actions.click(panel + "stack_" + recipe.choice);
  actions.await_closed("chosen", recipe.choice != "cancel");
  open();
  // Do not re-enter parameters: this must show the first OK's accepted value,
  // even when the subsequent Process Stack choice was Cancel.
  actions.remember("reopened");
  actions.click(panel + "cancel");
  actions.await_closed("final_cancel", false);
}

} // namespace imagejxx::desktop
