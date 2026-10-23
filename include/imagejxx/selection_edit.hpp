#pragma once
#include <imagejxx/document.hpp>
#include <imagejxx/execution.hpp>

namespace imagejxx {
// A resize gesture may pass through an empty box while crossing its anchor.
// Callers may keep the gesture alive, but must not publish this state.
class CollapsedSelectionResize : public std::invalid_argument {
public:
  CollapsedSelectionResize() : std::invalid_argument("Selection resize is empty") {}
};

enum class SelectionHandleKind { Move, Resize, Vertex, Rotate };
struct SelectionHandle {
  SelectionHandleKind kind;
  std::size_t index;
  std::array<double, 2> position;
};
// Positive local geometry admission limits. This is not the complete saved-row
// command/history/record budget or a process-memory limit. Metadata retains the
// native 128-container depth limit.
struct SelectionEditLimits {
  std::size_t max_xy_pixels = 16777216;
  std::size_t max_vertices = 65536;
  std::size_t max_working_bytes = 268435456;
};
struct SelectionEnlargementEstimate {
  int rounded_pixels = 0;
  std::size_t padded_pixels = 0, working_bytes = 0;
};
// Borrowed geometry admission for command hosts, without constructing an editor
// or allocating a raster. Control is used for cancellation only, not callbacks.
SelectionEnlargementEstimate estimate_selection_enlargement_geometry(
    const Selection &, int width, int height, double pixels,
    const SelectionEditLimits & = {}, const ExecutionControl & = {});
// Borrowed full-frame admission, including intrinsic regional mask storage.
SelectionEnlargementEstimate estimate_selection_region_enlargement_geometry(
    const Selection &, int width, int height, double pixels,
    const SelectionEditLimits & = {}, const ExecutionControl & = {});
// Immutable geometry operations. Coordinates are image-cell boundaries, as in
// Selection. This class stages geometry; the caller commits a document edit.
class SelectionRegion;
class SelectionEditor {
public:
  SelectionEditor(Selection selection, int width, int height);
  // Borrowed preflight precedes owned geometry copies and callbacks. Controlled
  // calls freeze their inputs/control for that call; no callback is retained.
  SelectionEditor(const Selection &selection, int width, int height,
                  const SelectionEditLimits &limits,
                  const ExecutionControl &control = {});
  const Selection &selection() const { return selection_; }
  std::array<double, 4> bounds() const {
    return bounds_;
  } // left, top, right, bottom
  std::vector<SelectionHandle> handles() const;
  std::vector<SelectionHandle> handles(const SelectionEditLimits &limits,
                                       const ExecutionControl &control = {}) const;
  std::optional<SelectionHandle>
  rotation_handle(double distance, std::array<double, 2> metric = {1, 1}) const;
  std::optional<SelectionHandle>
  hit_test(std::array<double, 2> point, std::array<double, 2> tolerance,
           std::optional<SelectionHandle> rotation = {}) const;
  std::optional<SelectionHandle>
  hit_test(std::array<double, 2> point, std::array<double, 2> tolerance,
           std::optional<SelectionHandle> rotation,
           const SelectionEditLimits &limits,
           const ExecutionControl &control = {}) const;
  Selection moved(std::array<double, 2> delta, bool keep_inside = false) const;
  Selection moved(std::array<double, 2> delta, bool keep_inside,
                  const SelectionEditLimits &limits,
                  const ExecutionControl &control = {}) const;
  // Resize indices go clockwise: NW, N, NE, E, SE, S, SW, W.
  Selection resized(std::size_t handle, std::array<double, 2> point,
                    bool preserve_aspect = false,
                    bool from_center = false) const;
  Selection resized(std::size_t handle, std::array<double, 2> point,
                    bool preserve_aspect, bool from_center,
                    const SelectionEditLimits &limits,
                    const ExecutionControl &control = {}) const;
  Selection moved_vertex(std::size_t index, std::array<double, 2> point) const;
  Selection moved_vertex(std::size_t index, std::array<double, 2> point,
                         const SelectionEditLimits &limits,
                         const ExecutionControl &control = {}) const;
  // Insert before the vertex following the nearest segment. A closed shape's
  // last edge returns points.size(); metric scales X/Y into display units.
  std::size_t insertion_index(std::array<double, 2> point,
                              std::array<double, 2> metric = {1, 1}) const;
  std::size_t insertion_index(std::array<double, 2> point,
                              std::array<double, 2> metric,
                              const SelectionEditLimits &limits,
                              const ExecutionControl &control = {}) const;
  Selection inserted_vertex(std::size_t index,
                            std::array<double, 2> point) const;
  Selection inserted_vertex(std::size_t index, std::array<double, 2> point,
                            const SelectionEditLimits &limits,
                            const ExecutionControl &control = {}) const;
  Selection removed_vertex(std::size_t index) const;
  Selection removed_vertex(std::size_t index,
                           const SelectionEditLimits &limits,
                           const ExecutionControl &control = {}) const;
  // Degrees clockwise in image coordinates; default pivot is geometry center.
  Selection rotated(double degrees,
                    std::optional<std::array<double, 2>> pivot = {}) const;
  Selection rotated(double degrees,
                    std::optional<std::array<double, 2>> pivot,
                    const SelectionEditLimits &limits,
                    const ExecutionControl &control = {}) const;
  double rotation_degrees(std::array<double, 2> from,
                          std::array<double, 2> to) const;
  Selection box_geometry(std::array<double, 2> center,
                         std::array<double, 2> size, double degrees = 0) const;
  Selection box_geometry(std::array<double, 2> center,
                         std::array<double, 2> size, double degrees,
                         const SelectionEditLimits &limits,
                         const ExecutionControl &control = {}) const;
  // ImageJ-compatible pixel distance: half-up integer rounding, analytic
  // rectangle/oval bounds, byte EDM below |distance|=256 and float EDM above.
  // Irregular results are clipped packed masks on this editor's XY grid.
  // A completely erased ROI retains its original geometry. No dataset IO.
  // The estimate admits geometry/copies, padded raster/EDM/buffers and result;
  // caller-owned editor, document/history and later serialization are excluded.
  SelectionEnlargementEstimate estimate_enlargement(
      double pixels, const SelectionEditLimits &limits = {}) const;
  Selection enlarged(double pixels, const SelectionEditLimits &limits = {},
                     const ExecutionControl &control = {}) const;
  // Retain the full irregular raster with its local-to-image origin. Projection
  // to a document canvas is a separate, explicitly lossy operation.
  SelectionRegion enlarged_region(double pixels, const SelectionEditLimits & = {},
                                  const ExecutionControl & = {}) const;
  Selection properties(double width, const MetadataObject &patch = {}) const;
  Selection roi_style(const MetadataObject &patch) const;

private:
  friend class SelectionRegion;
  SelectionRegion enlarged_region_impl(double, const SelectionEditLimits &,
      const ExecutionControl &, bool preserve_full_geometry) const;
  Selection selection_;
  int width_, height_;
  std::array<double, 4> bounds_{};
  bool has_bounds_ = false;
  void initialize_bounds(bool validate = true);
  bool contains(std::array<double, 2> point,
                std::array<double, 2> tolerance) const;
  Selection checked(Selection value) const;
};
} // namespace imagejxx
