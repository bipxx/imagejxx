#pragma once
#include <imagejxx/axis_remap.hpp>

namespace imagejxx::detail {
// Global Z sampling lattice continues outside the volume. Region storage is
// retained independently of the new volume's visible Z range.
void admit_region_z_subset(const Selection &, const ZSubsetRequest &);
Selection region_z_subset(const Selection &, const ZSubsetRequest &);
} // namespace imagejxx::detail
