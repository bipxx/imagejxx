#pragma once

#include <imagejxx/document.hpp>
#include <imagejxx/execution.hpp>
#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace imagejxx {

enum class ChannelDisplayMode { Color, Grayscale, Composite };
enum class ChannelDisplayProjection { Sum, Max, Min };
enum class ChannelDisplayVersion : std::uint8_t { V1 = 1, V2 = 2, V3 = 3 };
// V3 is explicit IEEE Float32 channel admission. Nonfinite automatic ranges
// use {"float32_bits":[low_word,high_word]} on the wire, never JSON NaN/null.
// V1/V2 retain finite range contracts and finite/quiet source policies.
using ChannelDisplayTable = std::array<std::array<std::uint8_t, 3>, 256>;
struct ChannelDisplayLut {
  // Named tables: Grays, Red, Green, Blue, White, Cyan, Magenta, Yellow.
  // A custom table requires an empty name; the two forms are exclusive.
  std::string name = "Grays";
  std::optional<ChannelDisplayTable> table;
};
struct ChannelDisplayChannel {
  bool visible = true;
  std::array<double, 2> range{0, 255};
  ChannelDisplayLut lut;
};
struct ChannelDisplayState {
  ChannelDisplayMode mode = ChannelDisplayMode::Composite;
  std::vector<ChannelDisplayChannel> channels;
  // Appended for existing {mode, channels} source initializers. V1 is exactly
  // SUM; V2 explicitly retains Sum/Max/Min even outside Composite mode.
  // The added fields change layout: headers/core/consumers must be rebuilt
  // together, rather than mixing pre-V2 and V2 C++ binary layouts.
  ChannelDisplayProjection projection = ChannelDisplayProjection::Sum;
  ChannelDisplayVersion version = ChannelDisplayVersion::V1;
};
struct ChannelDisplayRange {
  std::array<double, 2> range{0, 1};
  bool empty_finite = false;
};
struct ChannelDisplayLimits {
  std::size_t max_pixels = 16ULL * 1024 * 1024;
  std::size_t max_working_bytes = 128ULL * 1024 * 1024;
};
struct ChannelDisplayEstimate {
  int width = 0, height = 0;
  std::size_t pixels = 0, working_bytes = 0;
  std::vector<int> contributing_channels;
};
using ChannelDisplayPlaneProvider =
    std::function<std::shared_ptr<const Image>(Plane, int coordinate, int t, int c)>;

// Canonical state is scalar U8/U16/F32 only. V1/V2 ranges are finite low <= high;
// V3 requires IEEE Float32 and also allows ordered infinities or a canonical
// quiet-NaN pair. [0,0] is a literal interval, never an implicit reset. Composite C>8 is
// rejected here and by stored-record parsing. V1 requires Sum and its exact
// four wire fields; V2 requires the additional projection field. Validation
// and encoding never upgrade V1 or downcast an explicit V2 Sum state.
void validate_channel_display(const ChannelDisplayState &, DatasetShape,
                              PixelType, FloatingSamplePolicy);
ChannelDisplayState parse_channel_display(const MetadataObject &, DatasetShape,
                                          PixelType, FloatingSamplePolicy);
// Request bridge with the same strict schema, normalizing only Composite C>8.
ChannelDisplayState parse_requested_channel_display(
    const MetadataObject &, DatasetShape, PixelType, FloatingSamplePolicy);
MetadataObject encode_channel_display(const ChannelDisplayState &, DatasetShape,
                                     PixelType, FloatingSamplePolicy);
// Only exact schema imagejxx.channel-display and exact integer versions 1/2/3 are
// recognized by these generic validation hooks; unknown/future values stay
// opaque. The shape overload cannot validate dtype or sample policy.
void validate_known_channel_display(const DisplayState &, DatasetShape);
void validate_known_channel_display(const DisplayState &, const Dataset &);
// Interpretation is stricter: absent property -> nullopt; any present unknown,
// future, malformed, or unsupported value throws instead of selecting legacy.
std::optional<ChannelDisplayState>
recognized_channel_display(const DisplayState &, const Dataset &);
// Explicit requests alone normalize Composite C>8 to Color, retaining the full
// channel list and the state's projection/version.
ChannelDisplayState normalize_channel_display(ChannelDisplayState, DatasetShape,
                                              PixelType, FloatingSamplePolicy);
// Preserve every other property and all legacy flat display fields. Removal
// does not interpret the old value and is permitted on legacy datasets.
DisplayState with_channel_display(DisplayState, const Dataset &,
                                 std::optional<ChannelDisplayState>);
ChannelDisplayTable channel_display_lut(const std::string &name);

// Defaults inherit the first C0 live-XY processor reset range across C, with
// all channels visible and Red/Green/Blue/White/Cyan/Magenta/Yellow/White LUTs.
// Default creation uses V1/Sum for old policies and V3/Sum for IEEE Float32;
// range resets retain projection/version. V3 empty_finite reports no finite
// samples, including all-infinity sources; all-NaN ranges retain canonical NaN.
ChannelDisplayState default_channel_display(
    const Dataset &, int t, int z,
    ChannelDisplayMode requested_mode = ChannelDisplayMode::Composite,
    ChannelDisplayLimits = {}, const ExecutionControl & = {});
// Empty channel list means all C; explicit duplicates/out-of-bounds refuse
// before reading. Observations follow requested channel order (or ascending C).
// U8 resets to [0,255]; U16/F32 reset to live-XY finite min/max, preserving
// equality. All-NaN F32 resolves to owned [0,1] with empty_finite=true.
// Default/reset bounds also include the returned channel state, reset
// observations and index scratch; refusal precedes copying that state.
std::pair<ChannelDisplayState, std::vector<ChannelDisplayRange>>
reset_channel_display_ranges(const Dataset &, const ChannelDisplayState &,
                             int t, int z, const std::vector<int> &channels = {},
                             ChannelDisplayLimits = {},
                             const ExecutionControl & = {});
// State wins over legacy_range. Without state, an explicit finite legacy pair
// passes through for any valid native type/S; automatic live reset remains
// scalar U8/U16/F32 only. Existing legacy automatic renderers stay separate.
ChannelDisplayRange effective_channel_display_range(
    const Dataset &, const std::optional<ChannelDisplayState> &, int t, int c,
    int z, std::optional<std::array<double, 2>> legacy_range = {},
    ChannelDisplayLimits = {}, const ExecutionControl & = {});

// Metadata-only preflight: no plane/provider reads, pixel allocation or user
// callbacks. Working estimate includes RGB 3N + scalar scratch N doubles, an
// orthogonal extraction strip W and eight fixed LUT/range scratch slots.
// Borrowed source/provider cache allocations are external to this native
// working bound. There is no TCZ volume copy.
ChannelDisplayEstimate estimate_channel_display(
    const Dataset &, const ChannelDisplayState &, Plane, int coordinate, int t,
    int active_c, ChannelDisplayLimits = {});
// Streams one immutable scalar channel plane at a time into fresh zeroed RGB
// U8-tagged double storage. Color/Grayscale select active C independently of
// visibility and ignore projection. Composite sums with saturation or takes
// component-wise Max/Min of visible LUT colors (C1 uses gray). Min initializes
// every pixel from the first actual contributor. No contributors returns fresh
// black without provider reads; pixel zero has no retained-cache exception.
// The effective projection/contributors/tables/ranges are frozen before any
// provider or execution-control callback; only one plane is retained at a time.
// NaNs map to LUT entry 0, including a nonblack custom entry 0. V3 IEEE
// nonfinite automatic bounds map every base sample to index 0. Finite manual
// V3 bounds clip infinities; every source NaN payload remains unchanged.
// U8 uses a rounded low origin and requested range width; U16 rounds both
// endpoints and adds one to its denominator. F32 endpoints round to binary32
// before double mapping when both endpoints fit finite binary32; wider finite
// endpoint ranges retain raw-double arithmetic as an owned extension. Stored
// requested endpoints remain unchanged. Canonical typed samples follow the
// pinned processor arithmetic. FiniteOnly
// also admits existing noncanonical finite double intensities without casting,
// rounding or mutating source storage; direct arithmetic is an owned extension.
Image compose_channel_display(
    const Dataset &, const ChannelDisplayState &, Plane, int coordinate, int t,
    int active_c, ChannelDisplayPlaneProvider provider = {},
    ChannelDisplayLimits = {}, const ExecutionControl & = {});

} // namespace imagejxx
