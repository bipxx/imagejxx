#pragma once
#include <imagejxx/imagej_rgb_projection.hpp>
#include <imagejxx/channel_display.hpp>

namespace imagejxx {
struct ImageJProjectionDocumentRequest {
  ImageJRGBProjectionRequest projection;
  // The desktop command applies Grays to a scalar C-only projection. The
  // direct API profile does not apply that final desktop presentation step.
  bool desktop_grays = false;
  ChannelDisplayLimits display_limits;
};
// Whole-dataset projection into a fresh, unregistered owner. Density fields in
// projection must be default: the captured document property supplies them.
// Source Pixels/Metadata/Display and candidate data are guarded at publication;
// the stronger paired-display transaction also guards Selection. No source edit
// or application membership change occurs during preparation. The existing
// Application::publish atomically adopts prepared_new_document().
// Overlay coordinates use zero-based optional t/c/z; slice_index is the
// legacy zero-based stack position. Ordinary ImageJ layouts retain its stack/
// hyperstack wildcard rules, including hidden legacy positions. Grouping,
// current-C and explicit multi-axis extensions instead map native contributing
// groups (slice_index is then a Z alias). Full ROI-file import remains separate.
// Numerical and channel-display limits are separate bounds, not a total job or
// provider-cache reservation. Channel reset scans only the eager output planes.
PreparedOperation prepare_imagej_projection(
    const DocumentSnapshot&, const ImageJProjectionDocumentRequest&,
    const ExecutionControl& control = {});
} // namespace imagejxx
