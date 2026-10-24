#pragma once

#include "desktop.hpp"
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace imagejxx::desktop {

enum class PathEmptyFilterUiKind {
  ZeroArea, OneAxisZero, OffCanvas, PositionedUnmatched
};
struct PathEmptyFilterUiRecipe {
  PathEmptyFilterUiKind kind = PathEmptyFilterUiKind::ZeroArea;
  bool rank = false;
  bool scientific() const { return kind == PathEmptyFilterUiKind::PositionedUnmatched; }
};
std::optional<PathEmptyFilterUiRecipe>
pathEmptyFilterUiRecipe(const std::string &case_id);
std::vector<double> pathEmptyFilterUiCommands(const PathEmptyFilterUiRecipe &);

// Private observation only. The actual UI submits the job. This submitting-
// thread scope holds its first worker_entered event before processing reads;
// it retains no document, result, job or strong resource lease.
class PathEmptyFilterReadFence {
public:
  PathEmptyFilterReadFence(std::string operation, std::uint64_t source_id,
                          std::string role);
  ~PathEmptyFilterReadFence();
  PathEmptyFilterReadFence(const PathEmptyFilterReadFence &) = delete;
  PathEmptyFilterReadFence &operator=(const PathEmptyFilterReadFence &) = delete;
  bool entered() const;
  void bind(std::uint64_t job_id, std::optional<std::uint64_t> generation);
  void release();
  void abort() noexcept;
  Json diagnostics() const;
  Json events() const;
private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

struct PathEmptyFilterUiActions {
  std::function<void(const std::string &)> click;
  std::function<void(const std::string &, const std::string &)> text;
  std::function<void(const std::string &, const std::string &)> field;
  std::function<void(const std::string &)> remember;
  std::function<void()> expose_panel;
  std::function<void(const std::string &, bool)> fenced_preview;
  std::function<void(const std::string &, bool)> await_preview;
  std::function<void(const std::string &)> await_cancelled;
  std::function<void(const std::string &)> fenced_apply;
};
void runPathEmptyFilterUiRecipe(const PathEmptyFilterUiRecipe &,
                               const PathEmptyFilterUiActions &);

} // namespace imagejxx::desktop
