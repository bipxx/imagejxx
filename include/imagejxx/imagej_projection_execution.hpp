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
  // Optional conversion of a legacy palette by the frontend. Used only when
  // the source lacks canonical channel display and a scalar palette is copied.
  std::optional<ChannelDisplayLut> source_lut;
  std::optional<std::string> output_title;
  std::size_t max_working_bytes = 512ULL * 1024 * 1024;
  std::size_t max_record_bytes = 16ULL * 1024 * 1024;
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
// max_working_bytes additionally bounds the conservative combined processing,
// metadata/wire and publication estimate. Provider-private caches and renderer
// caches retain their separate budgets. Channel reset scans eager output only.
struct ImageJProjectionDocumentEstimate {
  ImageJDatasetProjectionLayout layout;
  std::size_t metadata_bytes = 0, working_bytes = 0;
};
// Metadata-only admission; no provider reads or execution callbacks.
ImageJProjectionDocumentEstimate estimate_imagej_projection_document(
    const DocumentSnapshot&, const ImageJProjectionDocumentRequest&);
PreparedOperation prepare_imagej_projection(
    const DocumentSnapshot&, const ImageJProjectionDocumentRequest&,
    const ExecutionControl& control = {});
} // namespace imagejxx
