#pragma once
#include "desktop.hpp"
#include <imagejxx/path_edit.hpp>

namespace imagejxx::desktop {
// UI transport is deliberately separate from legacy polygon vertex commands.
Selection editPathSegments(const Selection &, const Json &, const PathEditLimits &,
                           const ExecutionControl & = {});
class PathSegmentControls {
public:
  using Observe = std::function<void(const std::string &, bool, float)>;
  void draw(const Selection &, bool enabled, const std::function<void(const Json &)> &stage,
            const Observe &);
  void reset();
  Json diagnostics() const;
  bool invalid() const { return !error_.empty(); }
  void clear_error() { error_.clear(); }
private:
  void seed(const ImageJCompositePath &);
  std::optional<ImageJCompositePath> seen_;
  int index_ = 0, opcode_ = 1;
  std::array<std::string, 6> numbers_;
  std::string parameter_ = "0.5", error_;
};
} // namespace imagejxx::desktop
