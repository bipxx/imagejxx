#include "viewport_renderer.hpp"
#include "rendering.hpp"
#include "channel_display_sources.hpp"
#include "../native/kernel_control.hpp"
#include <cmath>
#include <algorithm>

namespace imagejxx::desktop {
ViewportSurface renderViewport(const ViewportRender &s) {
  kernel_checkpoint();
  const auto &live = s.live.data().dataset;
  const auto shape = live.shape();
  if (s.position.t < 0 || s.position.t >= shape.t || s.position.c < 0 ||
      s.position.c >= shape.c || s.position.z < 0 || s.position.z >= shape.z)
    throw std::out_of_range("Viewport position outside captured dataset");
  if(!s.previewTargets.empty()){
    if(!s.preview || shape.s!=3 || live.pixel_type()!=PixelType::UInt8 || s.previewTargets.size()>64)throw std::invalid_argument("RGB preview target union requires bounded RGB pixels");
    for(const auto &target:s.previewTargets)target.validate_source(s.live);
  }
  auto metadata = s.metadata;
  auto channels = recognized_channel_display(s.live.display(), live);
  projectPlanePreviewDisplayEffects(metadata, channels, s.position, s.previewDisplayEffects);
  const ChannelDisplaySources sources(s.live, s.preview, s.target,
                                      s.previewGeneration, s.position);
  // Unchanneled previews are already selected/gated by the host. Channel
  // sources additionally validate each contributor against its resolved target.
  const bool preview = channels ? sources.usesPreview(s.position.c)
      : s.preview && (s.previewTargets.empty()?(!s.target || s.target->contains(s.position)):std::any_of(s.previewTargets.begin(),s.previewTargets.end(),[&](const auto &target){return target.contains(s.position);}));
  const auto &selected = preview ? s.preview->data().dataset : live;
  ViewportSurface result;
  result.policy = selected.sample_policy();
  std::shared_ptr<const Image> liveXY;
  auto original = [&]() -> const Image & {
    if (!liveXY) liveXY = live.plane(s.position);
    return *liveXY;
  };
  auto provider = sources.provider();
  if (channels) result.source = provider(s.plane, s.coordinate, s.position.t, s.position.c);
  else if (s.plane == Plane::XY) result.source = selected.plane({s.position.t, s.position.c, s.coordinate});
  else result.source = std::make_shared<const Image>(selected.extract_plane(s.plane, s.coordinate, s.position.t, s.position.c));
  if (!preview && s.plane == Plane::XY && s.coordinate == s.position.z)
    liveXY = result.source;
  const auto &range = get(metadata, "display_range");
  std::optional<std::array<std::uint32_t, 2>> computed;
  if (channels) result.range = channels->channels.at(s.position.c).range;
  else if (range.is_array() && range.size() == 2)
    result.range = {number(range[0]), number(range[1])};
  else if (s.linked || result.source->type == PixelType::Float32 || result.source->type == PixelType::Float64) {
    result.range = sourceDisplayRange(original(), live.sample_policy(), !s.linked);
    if (result.policy == FloatingSamplePolicy::PreserveIEEEFloat32)
      computed = displayRangeBits(result.range);
    else metadata["display_range"] = result.range;
  } else result.range = {0, type_max(result.source->type)};
  // XYZ freezes the final active XY range across all linked orientations.
  // A captured Top Hat proposal is already projected into local display values.
  if (s.linked && !channels) {
    if (result.policy == FloatingSamplePolicy::PreserveIEEEFloat32)
      computed = displayRangeBits(result.range);
    if (std::isfinite(result.range[0]) && std::isfinite(result.range[1]))
      metadata["display_range"] = result.range;
    else metadata.erase("display_range");
  }
  kernel_checkpoint();
  if (s.plane == Plane::XY) metadata["slice_index"] = s.coordinate;
  if (channels) {
    result.rgb = compose_channel_display(live, *channels, s.plane, s.coordinate,
                        s.position.t, s.position.c, provider, s.channelLimits);
    if (s.plane == Plane::XY && !s.layered)
      renderDisplayOverlays(*result.rgb, metadata, s.position);
    result.channels = channelDisplayDiagnostics(*channels, sources, s.plane,
                        s.coordinate, s.position.t, s.position.c, s.capture, s.channelLimits, s.previewDisplayEffects);
  } else result.rgb = s.plane == Plane::XY
      ? renderImage(*result.source, metadata, !s.layered, result.policy, s.position, computed)
      : displayImage(*result.source, metadata, result.policy, computed);
  bool active = !channels;
  if (channels) {
    const auto estimate = estimate_channel_display(live, *channels, s.plane,
                            s.coordinate, s.position.t, s.position.c, s.channelLimits);
    for (int c : estimate.contributing_channels) active |= c == s.position.c;
  }
  const auto *effect = planePreviewDisplayEffect(s.previewDisplayEffects, s.position);
  if (s.plane == Plane::XY && active && !(effect && effect->suppress_threshold_overlay))
    renderThresholdOverlay(*result.rgb, original(), s.live, s.position);
  kernel_checkpoint();
  return result;
}
} // namespace imagejxx::desktop
