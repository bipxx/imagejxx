#include "channel_display_sources.hpp"

#include <memory>
#include <stdexcept>
#include <utility>

namespace imagejxx::desktop {

ChannelDisplaySources::ChannelDisplaySources(
    DocumentSnapshot live, std::optional<DocumentSnapshot> preview,
    std::optional<ResolvedTarget> target, std::uint64_t generation,
    PlanePosition final_gate)
    : live_(std::move(live)), preview_(std::move(preview)),
      target_(std::move(target)), generation_(generation), gate_(final_gate) {
  const auto &live_dataset = live_.data().dataset;
  const auto shape = live_dataset.shape();
  shape.validate();
  if (gate_.t < 0 || gate_.t >= shape.t || gate_.c < 0 || gate_.c >= shape.c ||
      gate_.z < 0 || gate_.z >= shape.z)
    throw std::out_of_range("Channel display source gate is outside T/C/Z");

  bool usable = false;
  if (preview_ && target_) {
    const auto &preview_dataset = preview_->data().dataset;
    usable = preview_->same_document(live_) &&
             live_.revisions().matches(preview_->revisions(), Change::Data) &&
             shape.dimensions() == preview_dataset.shape().dimensions() &&
             live_dataset.pixel_type() == preview_dataset.pixel_type() &&
             live_dataset.sample_policy() == preview_dataset.sample_policy();
    if (usable) {
      try {
        target_->validate_source(live_);
      } catch (const RevisionConflict &) {
        usable = false;
      } catch (const std::invalid_argument &) {
        usable = false;
      }
    }
  }
  if (!usable) {
    preview_.reset();
    target_.reset();
    generation_ = 0;
  }
}

void ChannelDisplaySources::validateChannel(int c) const {
  if (c < 0 || c >= live_.data().dataset.shape().c)
    throw std::out_of_range("Channel display source C is out of range");
}

bool ChannelDisplaySources::usesPreview(int c) const {
  validateChannel(c);
  return preview_ && target_ && target_->contains({gate_.t, c, gate_.z});
}

bool ChannelDisplaySources::usesPreview(int c, int source_z) const {
  return usesPreview(c) && target_->contains({gate_.t, c, source_z});
}

const DocumentSnapshot &ChannelDisplaySources::selected(int c) const {
  return usesPreview(c) ? *preview_ : live_;
}

std::uintptr_t ChannelDisplaySources::dataIdentity(int c) const {
  return reinterpret_cast<std::uintptr_t>(&selected(c).data());
}

ChannelDisplayPlaneProvider ChannelDisplaySources::provider() const {
  return [sources = *this](Plane plane, int coordinate, int t,
                          int c) -> std::shared_ptr<const Image> {
    if (t != sources.gate_.t)
      throw std::invalid_argument("Channel display provider T differs from its retained gate");
    const auto &dataset = sources.selected(c).data().dataset;
    if (plane == Plane::XY)
      return dataset.plane({t, c, coordinate});
    return std::make_shared<const Image>(dataset.extract_plane(plane, coordinate, t, c));
  };
}

} // namespace imagejxx::desktop
