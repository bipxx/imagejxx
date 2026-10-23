#pragma once
#include <imagejxx/document.hpp>
#include <imagejxx/execution.hpp>

namespace imagejxx {
// Reusable positive regular axis selection. Inverse returns no value for a
// removed coordinate. Nearest keeps the lower source coordinate on a tie.
class RegularAxisMapping {
public:
  RegularAxisMapping(int extent, int first, int last, int step);
  int count() const { return count_; }
  int source_index(int output) const;
  std::optional<int> output_index(int source) const;
  int nearest_output(int source) const;
  bool identity() const { return first_ == 0 && step_ == 1 && count_ == extent_; }
private:
  int extent_, first_, step_, count_;
};
struct ZSubsetRequest {
  int first = 0, last = 0, step = 1; // inclusive zero-based Z, all T/C/S retained
  OutputDestination destination = OutputDestination::NewDocument;
  std::size_t max_planes = 1024*1024;
};
struct PreparedAxisRemap {
  DocumentTransaction transaction;
  DisplayState display;
};
// Eager shared-plane table; never reads or copies sample buffers. Remaps Z
// calibration/origin, positioned ROIs/overlays, slice/plane labels, active Z and
// saved manual-threshold position. Removed positioned records are dropped;
// wildcard/XY geometry and opaque metadata remain unchanged. XZ/YZ regional
// masks gather the full global Z sampling lattice, including off-volume rows.
// Their intrinsic horizontal frame and style survive; canvas masks remain
// clipped to the output volume. Fourier state is
// refused for a nonidentity mapping instead of retaining stale derived data.
PreparedAxisRemap prepare_z_axis_remap(const DocumentSnapshot &, const ZSubsetRequest &,
                                      const ExecutionControl & = {});
} // namespace imagejxx
