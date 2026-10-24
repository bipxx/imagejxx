#pragma once

#include "desktop.hpp"
#include <functional>
#include <optional>
#include <string>

namespace imagejxx::desktop {

enum class PathFilterAdmissionUiKind { Invert, PooledMask, PooledDisplay, Workflow };
struct PathFilterAdmissionUiRecipe {
  PathFilterAdmissionUiKind kind = PathFilterAdmissionUiKind::Invert;
  bool workflow() const { return kind == PathFilterAdmissionUiKind::Workflow; }
  bool manual() const { return kind == PathFilterAdmissionUiKind::PooledDisplay; }
  bool pooled() const { return kind == PathFilterAdmissionUiKind::PooledMask || manual(); }
};

std::optional<PathFilterAdmissionUiRecipe>
pathFilterAdmissionUiRecipe(const std::string &case_id);
Json pathFilterAdmissionUiRecipeJson(const PathFilterAdmissionUiRecipe &);
// Literal serialized input qualified by I-ADMIT-08 on both platforms. This
// returns no computed image, mask, expected output or admission estimate.
Json pathFilterAdmissionProgramFixture();

struct PathFilterAdmissionUiActions {
  std::function<void(const std::string &)> click;
  std::function<void(const std::string &, bool)> flag;
  std::function<void(const std::string &)> remember;
  std::function<void(const std::string &)> milestone;
  std::function<void(const std::string &)> await_preview;
  std::function<void(const std::string &)> await_idle;
  std::function<void(const std::string &)> await_apply;
  std::function<void(const std::string &, bool)> await_workflow;
  std::function<void(int, const std::string &)> memory_request;
};

void runPathFilterAdmissionUiRecipe(const PathFilterAdmissionUiRecipe &,
                                   const PathFilterAdmissionUiActions &);

} // namespace imagejxx::desktop
