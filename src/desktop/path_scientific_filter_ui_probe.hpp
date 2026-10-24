#pragma once

#include <functional>
#include <optional>
#include <string>

namespace imagejxx::desktop {

struct PathScientificFilterUiRecipe {
  bool rank = false;
  bool positioned = false;
};

std::optional<PathScientificFilterUiRecipe>
pathScientificFilterUiRecipe(const std::string &case_id);

// Shared Driver callbacks deliver actual controls and read the native lane.
// Plane indices passed to navigate are zero-based; numeric UI fields are not.
struct PathScientificFilterUiActions {
  std::function<void(const std::string &)> click;
  std::function<void(const std::string &, const std::string &)> text;
  std::function<void(const std::string &, const std::string &)> field;
  std::function<void(const std::string &)> remember;
  std::function<void()> expose_panel;
  std::function<void(const std::string &, bool)> await_preview;
  std::function<void(const std::string &)> await_apply;
  std::function<void(const std::string &, int)> navigate;
};

void runPathScientificFilterUiRecipe(
    const PathScientificFilterUiRecipe &recipe,
    const PathScientificFilterUiActions &actions);

} // namespace imagejxx::desktop
