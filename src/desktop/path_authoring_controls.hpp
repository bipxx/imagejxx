#pragma once
#include "desktop.hpp"
#include <imagejxx/path_edit.hpp>
#include <imagejxx/selection_edit.hpp>

namespace imagejxx::desktop {
struct PathAuthoringCanvas {
  std::uint64_t id = 0;
  int plane = 0, coordinate = 0, width = 0, height = 0;
  std::array<float, 2> origin{}, scale{1, 1}, low{}, high{};
  bool operator==(const PathAuthoringCanvas &) const;
};
// Creator-thread private collection. Only a matched canvas press/release adds a
// point. Complete native commands and incomplete click groups stay separate.
class PathAuthoringControls {
public:
  using Publish = std::function<void(const DocumentSnapshot &, const Selection &,
                                     const PathAuthoringCanvas &)>;
  void controls(bool enabled, const Publish &);
  bool pointer(const DocumentSnapshot &, const PathAuthoringCanvas &, bool enabled,
               bool hovered, const Selection &style, std::size_t cap, bool input_admitted = true);
  void draw(const PathAuthoringCanvas &, ImDrawList *, std::size_t cap);
  void sync(const DocumentSnapshot &, bool enabled, std::size_t cap = 0);
  void cancel(const char *reason = "canceled");
  bool active() const { return source_.has_value(); }
  bool ready() const;
  const std::optional<Selection> &preview() const { return selection_; }
  const std::optional<PathAuthoringCanvas> &canvas() const { return canvas_; }
  Json diagnostics() const;
private:
  void append(const std::vector<double> &);
  void accept(std::array<double,2>);
  void remove();
  void finish(const Publish &);
  void observe(const char *, bool);
  std::optional<DocumentSnapshot> source_;
  std::optional<PathAuthoringCanvas> canvas_;
  std::optional<Selection> selection_;
  Selection style_{"rectangle"};
  std::vector<std::array<double,2>> pending_;
  PathEditLimits limits_;
  int segment_ = 0, keyboard_frame_ = -1;
  bool press_pending_ = false, new_contour_ = false, after_close_ = false, finish_requested_ = false;
  std::uint64_t generation_ = 0;
  std::string error_, last_cancel_;
  Json controls_ = Json::object();
  std::optional<RoiVisual> visual_;
  std::optional<Selection> visual_selection_;
  std::optional<PathAuthoringCanvas> visual_canvas_;
  std::size_t visual_builds_ = 0, visual_cap_ = 0;
};

// Shared numeric path resize form. Restaging always uses its frozen base.
class PathResizeControls {
public:
  using Observe = std::function<void(const std::string &, bool, float)>;
  void draw(const Selection &, int width, int height, int plane,
            const SelectionEditLimits &, bool enabled,
            const std::function<void(const Selection &)> &stage,
            const Observe &);
  void reset();
  bool invalid() const { return !error_.empty() || unstaged_; }
  bool active() const { return staged_ || unstaged_; }
  Json diagnostics() const;
private:
  void seed(const Selection &, int, int, const SelectionEditLimits &);
  std::optional<Selection> base_;
  std::array<std::string,2> coordinates_;
  int handle_ = 4;
  bool aspect_ = false, center_ = false, staged_ = false, unstaged_ = false;
  std::uint64_t lifecycle_ = 0;
  std::string error_;
};
} // namespace imagejxx::desktop
