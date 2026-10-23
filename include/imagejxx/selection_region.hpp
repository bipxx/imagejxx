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

struct SelectionRegionBooleanEstimate {
  std::array<int,2> origin;
  std::array<int,2> shape; // height,width of the admitted result frame
  std::size_t pixels=0,working_bytes=0;
};
// Full image-grid area algebra, independent of the document's clipping canvas.
// Legacy masks must match width/height; regions retain their intrinsic extent.
// Empty results are explicit 1x1 zero regions at (0,0). Styles are not combined.
// Estimates borrow geometry/metadata without rasterization or user callbacks.
// The bound covers frozen geometry, packed output, one dense vector raster and
// scratch; it is not RSS and excludes caller/frontend copies and opaque callable
// storage. Pixel limits apply to input masks and the result frame.
SelectionRegionBooleanEstimate estimate_selection_region_boolean(
    int width,int height,const std::vector<Selection> &,SelectionBooleanOperation,
    const SelectionEditLimits & = {});
Selection combine_selection_regions(
    int width,int height,const std::vector<Selection> &,SelectionBooleanOperation,
    const SelectionEditLimits & = {},const ExecutionControl & = {});
// Ordered distinct manager rows are combined regardless of their TCZ positions.
// Only current/Previous selection changes; saved rows and source words survive.
// Transaction admission additionally includes retained source annotation copies;
// publication/history and externally retained previews remain the host's scope.
SelectionRegionBooleanEstimate estimate_roi_region_boolean(
    const DocumentSnapshot &,const std::vector<std::size_t> &,SelectionBooleanOperation,
    const SelectionEditLimits & = {});
DocumentTransaction prepare_roi_region_boolean(
    const DocumentSnapshot &,const std::vector<std::size_t> &,SelectionBooleanOperation,
    const SelectionEditLimits & = {},const ExecutionControl & = {});
}
