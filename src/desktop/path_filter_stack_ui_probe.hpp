#pragma once

#include <functional>
#include <optional>
#include <string>

namespace imagejxx::desktop {

struct PathFilterStackUiRecipe {
  bool rank = false;
  std::string choice;
};

std::optional<PathFilterStackUiRecipe>
pathFilterStackUiRecipe(const std::string &case_id);

// The shared Driver delivers actual input and observes native state. The one
// external display-only stimulus is labeled separately and earns no UI credit.
struct PathFilterStackUiActions {
  std::function<void(const std::string &)> click;
  std::function<void(const std::string &, const std::string &)> text;
  std::function<void(const std::string &)> remember;
  std::function<void()> inspect_initial;
  std::function<void()> move_display;
  std::function<void(const std::string &, bool)> await_closed;
};

void runPathFilterStackUiRecipe(const PathFilterStackUiRecipe &recipe,
                               const PathFilterStackUiActions &actions);

} // namespace imagejxx::desktop
