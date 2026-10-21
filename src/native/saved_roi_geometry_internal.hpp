#pragma once
#include <imagejxx/saved_roi_geometry.hpp>

namespace imagejxx::detail {
std::size_t saved_roi_geometry_display_payload_bytes(
    const DisplayState &display, const SavedRoiGeometryLimits &limits);
// Borrowed preflight for controller staging. Strict replay retains its original
// record while taking fresh publication guards; it does not reimpose visibility.
SavedRoiGeometryEstimate estimate_saved_roi_geometry_preparation(
    const DocumentSnapshot &captured_source,
    const ResolvedSavedRoiGeometryCommand &command,
    const SavedRoiGeometryLimits &limits);
} // namespace imagejxx::detail
