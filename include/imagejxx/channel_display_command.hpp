#pragma once

#include <imagejxx/channel_display.hpp>
#include <optional>
#include <vector>

namespace imagejxx {

enum class ChannelDisplayAction { Set, ResetRange, Disable, SelectMax, SelectMin };

// Set and ResetRange require a complete state. Only ResetRange accepts channel
// indices; an empty list selects every C, otherwise caller order is retained.
// Disable accepts no state or indices and never interprets an existing property.
// SelectMax/SelectMin also accept neither; each resolves once from the captured
// recognized display, including an all-channel table transform and C0 effect.
// Either intent's resolved projection may be Max or Min. SelectMax transforms
// an inverted effective LUT; SelectMin transforms a non-inverted effective LUT.
// Resolved SelectMax-Min and SelectMin-Max always record deliberate C0, even
// when the source already selected C0. Native classification uses current state
// (gray in Grayscale/singleton Composite), not a historical Java
// processor cache; a Gray entry/repeat can therefore differ from Java cache state.
struct ChannelDisplayRequest {
  ChannelDisplayAction action = ChannelDisplayAction::Set;
  std::optional<ChannelDisplayState> state;
  std::vector<int> reset_channels;
};

class ResolvedChannelDisplayCommand {
public:
  const DocumentSnapshot &source() const { return source_; }
  ChannelDisplayAction action() const { return action_; }
  const std::optional<ChannelDisplayState> &state() const { return state_; }
  const MetadataObject &record() const { return record_; }
  ChannelDisplayVersion version() const { return version_; }
  std::optional<int> navigation_channel() const { return navigation_; }
  std::optional<std::uint64_t> display_dependency_revision() const {
    return displayDependency_;
  }
  // Execution provenance only: replay retains its historical dependency in the
  // record but applies the resolved effect independently of current Display.
  bool requires_captured_display() const { return requiresCapturedDisplay_; }

private:
  friend ResolvedChannelDisplayCommand resolve_channel_display_command(
      const DocumentSnapshot &, const ChannelDisplayRequest &,
      const ChannelDisplayLimits &, const ExecutionControl &);
  friend ResolvedChannelDisplayCommand channel_display_command_from_record(
      const DocumentSnapshot &, const MetadataObject &);
  ResolvedChannelDisplayCommand(DocumentSnapshot source,
                                ChannelDisplayAction action,
                                std::optional<ChannelDisplayState> state,
                                MetadataObject record,
                                ChannelDisplayVersion version,
                                std::optional<int> navigation = {},
                                std::optional<std::uint64_t> display_dependency = {},
                                bool requires_captured_display = false);
  DocumentSnapshot source_;
  ChannelDisplayAction action_;
  std::optional<ChannelDisplayState> state_;
  MetadataObject record_;
  ChannelDisplayVersion version_ = ChannelDisplayVersion::V1;
  std::optional<int> navigation_;
  std::optional<std::uint64_t> displayDependency_;
  bool requiresCapturedDisplay_ = false;
};

// Set/Disable inspect metadata only and invoke no user callbacks. ResetRange
// scans the captured source's live planes at its T/Z, never a staged preview;
// its record contains the resolved ranges and empty-finite observations.
// SelectMax/SelectMin have no pixel/provider reads. Positive limits and a checked
// C-state/table/record working bound precede cloning and callbacks; max_pixels is not
// an image-area limit for this metadata-only intent. Captured inputs are owned
// before checkpoint callbacks; valid pre-cancellation invokes no user callback.
ResolvedChannelDisplayCommand resolve_channel_display_command(
    const DocumentSnapshot &source, const ChannelDisplayRequest &request,
    const ChannelDisplayLimits &limits = {},
    const ExecutionControl &control = {});
void validate_channel_display_command_record(const MetadataObject &record);
// Replay validates the complete original record before comparing the binding.
// It applies recorded state without pixel reads or range recomputation.
ResolvedChannelDisplayCommand channel_display_command_from_record(
    const DocumentSnapshot &source, const MetadataObject &record);
// Explicit rebind never repairs malformed original records from a new source.
// Ranges are recomputed only when explicit_request requests ResetRange.
ResolvedChannelDisplayCommand rebind_channel_display_command(
    const DocumentSnapshot &new_source, const MetadataObject &record,
    const ChannelDisplayRequest &explicit_request,
    const ChannelDisplayLimits &limits = {},
    const ExecutionControl &control = {});
// No publication. Require the same owner, Pixels+Metadata revisions and source
// shape/type/sample policy, while retaining current navigation and unrelated
// display properties. A fresh SelectMax/SelectMin additionally depends on
// captured Display; replay does not. A recorded C effect preserves fresh T/Z/cameras.
// Transient-selection revisions may differ.
DisplayState command_display_state(
    const ResolvedChannelDisplayCommand &command,
    const DocumentSnapshot &current_snapshot);

} // namespace imagejxx
