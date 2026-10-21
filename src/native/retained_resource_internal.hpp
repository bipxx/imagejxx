#pragma once
#include <imagejxx/dataset.hpp>
#include <stdexcept>

namespace imagejxx::detail {
// Only library-created aliases can be unwrapped during publication.
struct ResourceImageDeleter {
  std::shared_ptr<const Image> original;
  std::shared_ptr<const void> guard;
  void operator()(const Image *) noexcept {
    original.reset();
    guard.reset();
  }
};
inline std::shared_ptr<const Image>
guard_image(std::shared_ptr<const Image> image,
            std::shared_ptr<const void> guard) {
  if (!guard || !image)
    return image;
  const auto pointer = image.get();
  return std::shared_ptr<const Image>(
      pointer, ResourceImageDeleter{std::move(image), std::move(guard)});
}
inline std::shared_ptr<const Image>
detach_image(std::shared_ptr<const Image> image) {
  while (auto tag = std::get_deleter<ResourceImageDeleter>(image))
    image = tag->original;
  return image;
}
class ResourceSource {
public:
  virtual ~ResourceSource() = default;
  // Empty means no change. No pixel reads or deep copies are permitted here.
  virtual std::shared_ptr<const PlaneSource> detach_resources() const = 0;
  virtual const PlaneSource *resource_identity() const = 0;
};
inline const PlaneSource *source_identity(const PlaneSource *source) {
  if (auto tagged = dynamic_cast<const ResourceSource *>(source))
    return tagged->resource_identity();
  return source;
}
inline std::shared_ptr<const PlaneSource>
detach_source(std::shared_ptr<const PlaneSource> source) {
  if (auto tagged = dynamic_cast<const ResourceSource *>(source.get()))
    if (auto detached = tagged->detach_resources())
      return detached;
  return source;
}
struct ResourceAccess {
  static const PlaneSource *source(const Dataset &dataset){return source_identity(dataset.source_.get());}
  static void rebind(SharedPlanes &planes, std::size_t index,
                     std::shared_ptr<const Image> image) {
    auto &entry = planes.planes_.at(index);
    if (entry.get() != image.get())
      throw std::logic_error("Resource rebind must preserve the image address");
    entry = std::move(image);
  }
  static Dataset detach(const Dataset &dataset) {
    return dataset.detach_resources();
  }
  static bool same_source(const Dataset &a, const Dataset &b) {
    return a.source_ == b.source_;
  }
};
} // namespace imagejxx::detail
