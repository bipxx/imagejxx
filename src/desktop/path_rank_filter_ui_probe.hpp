#pragma once

#include <functional>
#include <optional>
#include <string>

namespace imagejxx::desktop {

struct PathRankFilterUiRecipe {
  std::string method;
  bool new_document = false;
};

// Exact bounded recipes. Unknown names are not an observation claim.
std::optional<PathRankFilterUiRecipe>
pathRankFilterUiRecipe(const std::string &case_id);

// The existing path-filter driver owns input delivery, hover/clip checks,
// snapshots and the actual shared preview lane. These callbacks perform no
// alternate processing and carry no numerical expectations.
struct PathRankFilterUiActions {
  std::function<void(const std::string &)> click;
  std::function<void(const std::string &, const std::string &)> text;
  std::function<void(const std::string &)> remember;
  std::function<void(const std::string &)> await_preview;
  std::function<void(const std::string &)> await_apply;
  std::function<void(const std::string &)> await_idle;
};

void runPathRankFilterUiRecipe(const PathRankFilterUiRecipe &recipe,
                               const PathRankFilterUiActions &actions);

} // namespace imagejxx::desktop
