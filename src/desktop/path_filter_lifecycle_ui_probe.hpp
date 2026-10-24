#pragma once

#include "desktop.hpp"
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>

namespace imagejxx::desktop {

enum class PathFilterLifecycleUiKind {
  Supersede, InvalidEditor, InvalidExternal, OwnerReplace,
  SelectionRevision, PixelRevision, CalibrationRevision
};

struct PathFilterLifecycleUiRecipe {
  PathFilterLifecycleUiKind kind = PathFilterLifecycleUiKind::Supersede;
  bool rank = false;
};

std::optional<PathFilterLifecycleUiRecipe>
pathFilterLifecycleUiRecipe(const std::string &case_id);

// A submitting-thread RAII observer of genuine OperationJob progress. It owns
// no document, job result or resource lease. Only its first armed positive
// source-copy event may wait; failure cleanup always releases that wait.
class PathFilterLifecycleObserver {
public:
  PathFilterLifecycleObserver(std::string operation, std::uint64_t source_id);
  ~PathFilterLifecycleObserver();
  PathFilterLifecycleObserver(const PathFilterLifecycleObserver &) = delete;
  PathFilterLifecycleObserver &operator=(const PathFilterLifecycleObserver &) = delete;
  void arm();
  bool entered() const;
  void bind(std::uint64_t job_id, std::uint64_t generation);
  void release();
  void abort() noexcept;
  Json diagnostics();
  Json events() const;
private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

struct PathFilterLifecycleUiActions {
  std::function<void(const std::string &)> click;
  std::function<void(const std::string &, const std::string &)> text;
  std::function<void(const std::string &)> remember;
  std::function<void(const std::string &)> await_preview;
  std::function<void()> arm;
  std::function<void(const std::string &)> await_running;
  std::function<void()> release;
  std::function<void(const std::string &)> await_old_settled;
  std::function<void(const std::string &)> await_released;
  std::function<void()> replace_owner;
  std::function<void()> restore_owner;
  std::function<void(PathFilterLifecycleUiKind)> source_change;
  std::function<void()> invalid_external;
};

void runPathFilterLifecycleUiRecipe(const PathFilterLifecycleUiRecipe &recipe,
                                   const PathFilterLifecycleUiActions &actions);

} // namespace imagejxx::desktop
