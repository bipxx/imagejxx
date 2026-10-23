#pragma once
#include <imagejxx/selection_edit.hpp>

namespace imagejxx {
// Immutable area geometry in a local coordinate frame. A packed mask's extent
// belongs to the geometry, independently of any document canvas. Projection is
// explicit and may discard off-canvas cells; keep the region for further edits.
// as_selection() creates canonical native region storage without projection.
class SelectionRegion {
public:
  SelectionRegion(const Selection &, int width, int height,
                  std::array<int, 2> origin = {0, 0},
                  const SelectionEditLimits & = {});
  const Selection &local_selection() const { return selection_; }
  Selection as_selection() const;
  std::array<int, 2> origin() const { return origin_; }
  std::array<int, 2> shape() const { return {height_, width_}; }
  std::optional<std::array<double, 4>> bounds() const;
  SelectionEnlargementEstimate estimate_enlargement(
      double pixels, const SelectionEditLimits & = {}) const;
  SelectionRegion enlarged(double pixels, const SelectionEditLimits & = {},
                           const ExecutionControl & = {}) const;
  std::size_t estimate_projection(int width, int height,
                                 const SelectionEditLimits & = {}) const;
  Selection project(int width, int height, const SelectionEditLimits & = {},
                    const ExecutionControl & = {}) const;
private:
  friend class SelectionEditor;
  // Internal adoption follows borrowed geometry/raster admission.
  SelectionRegion(Selection, int width, int height, std::array<int, 2>, bool);
  Selection selection_;
  int width_, height_;
  std::array<int, 2> origin_;
};
}
