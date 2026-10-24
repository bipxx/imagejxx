#pragma once

#include <functional>
#include <optional>
#include <string>

namespace imagejxx::desktop {

enum class PathSpecializedFilterUiKind { Despeckle, Outliers, TopHat, RemoveNaNs };

struct PathSpecializedFilterUiRecipe {
  PathSpecializedFilterUiKind kind = PathSpecializedFilterUiKind::Despeckle;
  bool inverted_lut = false;
  bool light_background = false;
  bool dont_subtract = false;
  std::string which_outliers = "bright";
  std::string input_pattern = "finite";
};

std::optional<PathSpecializedFilterUiRecipe>
pathSpecializedFilterUiRecipe(const std::string &case_id);

// Controls are delivered by the existing shared Driver. These recipes neither
// implement filtering nor synthesize preview/Apply output observations.
struct PathSpecializedFilterUiActions {
  std::function<void(const std::string &)> click;
  std::function<void(const std::string &, const std::string &)> text;
  std::function<void(const std::string &)> remember;
  std::function<void(const std::string &)> await_preview;
  std::function<void(const std::string &)> await_apply;
};

void runPathSpecializedFilterUiRecipe(
    const PathSpecializedFilterUiRecipe &recipe,
    const PathSpecializedFilterUiActions &actions);

} // namespace imagejxx::desktop
