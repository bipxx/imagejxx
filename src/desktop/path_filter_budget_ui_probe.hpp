#pragma once

#include <functional>
#include <optional>
#include <string>

namespace imagejxx::desktop {

struct PathFilterBudgetUiRecipe {
  bool rank = false;
};

std::optional<PathFilterBudgetUiRecipe>
pathFilterBudgetUiRecipe(const std::string &case_id);

// The shared path-filter Driver owns physical input, actual preferences,
// scheduler observations and bounded snapshots. No filter is implemented here.
struct PathFilterBudgetUiActions {
  std::function<void(const std::string &)> click;
  std::function<void(const std::string &, const std::string &)> text;
  std::function<void(const std::string &)> remember;
  std::function<void(const std::string &)> milestone;
  std::function<void(const std::string &)> await_preview;
  std::function<void(const std::string &)> await_apply;
  std::function<void(const std::string &)> await_release;
  std::function<void(const std::string &)> await_refusal;
  std::function<void(const std::string &)> await_preview_refusal;
  std::function<void()> await_cancelled;
  std::function<void(int, const std::string &)> memory_request;
};

void runPathFilterBudgetUiRecipe(const PathFilterBudgetUiRecipe &recipe,
                                const PathFilterBudgetUiActions &actions);

} // namespace imagejxx::desktop
