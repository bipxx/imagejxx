#pragma once

#include "selection_panel.hpp"
#include <imagejxx/application.hpp>

namespace imagejxx::desktop {
enum class SavedRoiCanvasKind { OrdinaryXY, LinkedXY };
struct SavedRoiCanvasTransform {
  std::array<double, 2> origin{}, extent{}, scale{1, 1};
  std::uint64_t canvas_id = 0;
  SavedRoiCanvasKind kind = SavedRoiCanvasKind::OrdinaryXY;
  bool operator==(const SavedRoiCanvasTransform &) const;
};
struct SavedRoiEditModifiers {
  bool shift = false, ctrl = false, alt = false;
};

// Creator-thread private draft. It never selects a global ROI or publishes a
// document. Root stages recording/focus first and owns the final publication.
class SavedRoiEdit {
public:
  SavedRoiEdit() = default;
  ~SavedRoiEdit() { cancel(); }
  SavedRoiEdit(const SavedRoiEdit &) = delete;
  SavedRoiEdit &operator=(const SavedRoiEdit &) = delete;
  SavedRoiEdit(SavedRoiEdit &&) noexcept = default;
  SavedRoiEdit &operator=(SavedRoiEdit &&other) noexcept {
    if (this != &other) { cancel(); state_ = std::move(other.state_);
      pathTransform_=other.pathTransform_;pathAspect_=other.pathAspect_;pathCenter_=other.pathCenter_; }
    return *this;
  }
  void begin(const SavedRoiGeometryCapture &, std::size_t row,
             const SavedRoiGeometryLimits &, const ExecutionControl & = {});
  void begin_pointer(const SavedRoiGeometryCapture &, std::size_t row,
                     const SavedRoiCanvasTransform &, SelectionHandle hit,
                     std::array<double, 2> down,
                     const SavedRoiGeometryLimits &,
                     const ExecutionControl & = {});
  void update_pointer(std::array<double, 2> point, SavedRoiEditModifiers,
                      const SavedRoiCanvasTransform &,
                      const ExecutionControl & = {});
  void stage_numeric(const Selection &, const ExecutionControl & = {});
  void show_numeric(SelectionPanel &, const ExecutionControl & = {});
  ResolvedSavedRoiGeometryCommand command(const ExecutionControl & = {});
  PreparedSavedRoiGeometryEdit prepare(
      Application &, SavedRoiGeometryInteraction,
      const ExecutionControl & = {});
  void working_limit(std::size_t cap);
  void invalidate_preview() noexcept;
  void cancel() noexcept;

  bool active() const noexcept;
  bool pointer_active() const noexcept;
  bool pointer_valid() const noexcept;
  void path_transform(bool value) { pathTransform_ = value; }
  bool path_transform() const { return pathTransform_; }
  void path_flags(bool aspect, bool center) { pathAspect_ = aspect; pathCenter_ = center; }
  std::size_t row_index() const;
  const SavedRoiGeometryCapture &capture() const;
  const DocumentSnapshot &source() const;
  const Selection &original() const;
  const std::optional<Selection> &candidate() const noexcept;
  const SavedRoiGeometryLimits &limits() const;
  const std::optional<SavedRoiCanvasTransform> &transform() const noexcept;
  std::vector<SelectionHandle> handles(const ExecutionControl & = {}) const;
  std::optional<SelectionHandle> rotation_handle(
      double distance, std::array<double, 2> metric = {1, 1}) const;
  std::optional<SelectionHandle> hit_test(
      std::array<double, 2> point, std::array<double, 2> tolerance,
      std::optional<SelectionHandle> rotation = {},
      const ExecutionControl & = {}) const;
  // Declared private D03 working admission, not allocator/RSS/global-ledger use.
  std::size_t retained_count() const noexcept;
  std::size_t retained_working_bytes() const noexcept;
  Json diagnostics() const;

private:
  struct State;
  std::shared_ptr<State> state_;
  bool pathTransform_ = false, pathAspect_ = false, pathCenter_ = false;
  const State &require() const;
};

// Borrowed for one canvas draw only. The owner mints the actual mouse-down
// capture and stages/publishes on release; no release coordinate is supplied.
struct SavedRoiCanvasHooks {
  SavedRoiEdit *edit = nullptr;
  bool pointer_enabled = false;
  std::optional<std::size_t> *selected_vertex = nullptr;
  std::function<void(const Selection &)> accept_numeric;
  std::function<void(const SavedRoiCanvasTransform &, SelectionHandle,
                     std::array<double, 2>)> begin;
  std::function<void(std::array<double, 2>, SavedRoiEditModifiers,
                     const SavedRoiCanvasTransform &)> update;
  std::function<void(const SavedRoiCanvasTransform &)> release;
  std::function<void()> cancel;
};
} // namespace imagejxx::desktop
