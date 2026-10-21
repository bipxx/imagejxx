#pragma once

#include <imagejxx/channel_display.hpp>
#include <imagejxx/targets.hpp>
#include <cstdint>
#include <optional>

namespace imagejxx::desktop {

// A frame-local retained source choice. The final T/Z position gates each
// channel's Gaussian preview independently; an orthogonal extraction then
// retains the staged Dataset's unchanged live planes outside its target.
// Copies retain snapshots/target metadata, without retaining extracted planes.
class ChannelDisplaySources {
public:
  ChannelDisplaySources(DocumentSnapshot live,
                        std::optional<DocumentSnapshot> preview,
                        std::optional<ResolvedTarget> target,
                        std::uint64_t generation, PlanePosition final_gate);

  // The returned closure owns a complete selector copy and can outlive this
  // object. XY shares one immutable source plane; XZ/YZ extracts one plane.
  ChannelDisplayPlaneProvider provider() const;
  bool usesPreview(int c) const;
  bool usesPreview(int c, int source_z) const;
  std::uint64_t generation() const noexcept { return generation_; }
  std::uintptr_t dataIdentity(int c) const;
  const DocumentSnapshot &live() const noexcept { return live_; }

private:
  void validateChannel(int c) const;
  const DocumentSnapshot &selected(int c) const;

  DocumentSnapshot live_;
  std::optional<DocumentSnapshot> preview_;
  std::optional<ResolvedTarget> target_;
  std::uint64_t generation_;
  PlanePosition gate_;
};

} // namespace imagejxx::desktop
