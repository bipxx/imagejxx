#pragma once
#include "viewport_jobs.hpp"
#include <imagejxx/channel_display.hpp>
#include <imagejxx/targets.hpp>

namespace imagejxx::desktop {
// Entire CPU request owns immutable data and value display metadata. No
// Document frontend, ImGui or GL references cross into a worker closure.
struct ViewportRender {
  DocumentSnapshot live;
  std::optional<DocumentSnapshot> preview;
  std::optional<ResolvedTarget> target;
  std::vector<ResolvedTarget> previewTargets;
  std::uint64_t previewGeneration = 0;
  PlanePosition position{};
  Plane plane = Plane::XY;
  int coordinate = 0;
  Json metadata = Json::object();
  bool linked = false, layered = false, capture = false;
  ChannelDisplayLimits channelLimits{};
};
ViewportSurface renderViewport(const ViewportRender &request);
} // namespace imagejxx::desktop
