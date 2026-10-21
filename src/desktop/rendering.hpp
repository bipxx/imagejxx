#pragma once

#include "desktop.hpp"
#include <imagejxx/channel_display.hpp>
#include <array>
#include <optional>

namespace imagejxx::desktop {
class ChannelDisplaySources;

// Paint annotations onto an already display-mapped RGB image exactly once.
// This does not perform intensity conversion, LUT mapping or threshold tinting.
void renderDisplayOverlays(Image &image, const Json &metadata);
// Native optional zero-based T/C/Z and legacy slice_index constrain the same
// active XY annotation. Missing/null coordinates are wildcards; records stay
// opaque and unchanged in the document/session.
void renderDisplayOverlays(Image &image, const Json &metadata,
                           std::optional<PlanePosition> position);
bool hasStyledOverlays(const Json &metadata,
                       std::optional<PlanePosition> position);
std::vector<OverlayVisual> overlayVisuals(
    const Json &metadata, int width, int height,
    std::optional<PlanePosition> position);

// Metadata-only contributor identity for shared renderer caches and readbacks.
// Source-Z preview membership is expanded only for requested diagnostic capture.
Json channelDisplayDiagnostics(const ChannelDisplayState &state,
                               const ChannelDisplaySources &sources,
                               Plane plane, int coordinate, int t, int active_c,
                               bool include_z_strips,
                               ChannelDisplayLimits limits = {});

// Private display-only policy. Callers must obtain it from the immutable
// Dataset whose pixels are displayed; no processing Settings flag is used.
// rgb_unit_range preserves the ordinary floating RGB [0,1] convention. XYZ
// callers leave it false to retain their existing shared contrast behavior.
// Opted all-quiet-NaN grayscale Float32 has the owned fallback range [0,1].
std::array<double, 2> finiteDisplayRange(
    const Image &image, FloatingSamplePolicy sample_policy,
    bool rgb_unit_range = false);

// Computed view ranges may contain IEEE endpoints. Persist finite manual
// fields separately; diagnostics use exact endpoint words for nonfinite ranges.
std::array<double,2> sourceDisplayRange(const Image &, FloatingSamplePolicy,
                                      bool rgb_unit_range = false);
std::array<std::uint32_t,2> displayRangeBits(const std::array<double,2> &);
Json displayRangeValue(const std::array<double,2> &);
bool sameDisplayRange(const std::array<double,2> &, const std::array<double,2> &);

// The existing desktop.hpp overloads and their defaults remain finite-only.
// These explicit internal overloads project NaNs to LUT index0 in a temporary
// display image while retaining original immutable source/readback bytes.
Image displayImage(const Image &image, const Json &metadata,
                   FloatingSamplePolicy sample_policy,
                   std::optional<std::array<std::uint32_t,2>> computed = {});
Image renderImage(const Image &image, const Json &metadata,
                  bool includeOverlays, FloatingSamplePolicy sample_policy);
Image renderImage(const Image &image, const Json &metadata,
                  bool includeOverlays, FloatingSamplePolicy sample_policy,
                  std::optional<PlanePosition> position,
                  std::optional<std::array<std::uint32_t,2>> computed = {});

} // namespace imagejxx::desktop
