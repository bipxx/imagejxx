#pragma once

#include <imagejxx/channel_display_command.hpp>

namespace imagejxx::detail {

// Core-private decoded effect view. Callers with bounded command envelopes
// preflight their borrowed metadata before requesting this owning parse.
// Source binding remains the existing channel-display command's responsibility.
struct ChannelDisplayCommandDescriptor {
  DatasetShape shape;
  PixelType type;
  FloatingSamplePolicy policy;
  PlanePosition position;
  ChannelDisplayAction action;
  ChannelDisplayVersion version;
  std::optional<ChannelDisplayState> state;
};

ChannelDisplayCommandDescriptor
channel_display_command_descriptor(const MetadataObject &record);

} // namespace imagejxx::detail
