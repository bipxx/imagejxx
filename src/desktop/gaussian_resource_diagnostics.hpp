#pragma once

#include "../native/retained_resource_internal.hpp"
#include <imagejxx/metadata.hpp>

#include <cstdint>
#include <limits>
#include <memory>
#include <set>
#include <sstream>
#include <stdexcept>
#include <utility>

namespace imagejxx::desktop {
namespace gaussian_resource_detail {
inline MetadataValue address(const void *pointer) {
  if (!pointer)
    return {};
  std::ostringstream text;
  text << "0x" << std::hex << reinterpret_cast<std::uintptr_t>(pointer);
  return text.str();
}

inline std::uint64_t pixel_bytes(std::size_t count) {
  if (count > std::numeric_limits<std::uint64_t>::max() / sizeof(double))
    throw std::overflow_error("Gaussian pixel storage byte count overflow");
  return std::uint64_t(count) * sizeof(double);
}

inline void add_bytes(std::uint64_t &total, std::uint64_t bytes) {
  if (bytes > std::numeric_limits<std::uint64_t>::max() - total)
    throw std::overflow_error("Gaussian pixel storage total overflow");
  total += bytes;
}
} // namespace gaussian_resource_detail

// Inspect an already-held eager preview synchronously. Only metadata escapes:
// no snapshot, prepared result, plane table, image or guard owner is returned.
// Addresses identify actual objects only within their process and lifetime;
// they are not allocator IDs and may be reused after an object is released.
inline MetadataObject inspectGaussianPreviewStorage(const Dataset &dataset) {
  MetadataObject result{
      {"schema_version", std::int64_t(1)},
      {"eager", false},
      {"native_sample_storage", std::string("double")},
      {"native_sample_bytes", std::uint64_t(sizeof(double))},
      {"measurement_scope",
       std::string("held_preview_eager_buffers_including_shared_source_planes")},
      {"address_semantics", std::string("process_lifetime_local")},
      {"allocator_ids", false},
      {"pixel_read_policy", std::string("no_pixel_value_or_virtual_plane_reads")},
      {"pixel_payload_bytes", MetadataValue{}},
      {"pixel_capacity_bytes", MetadataValue{}},
      {"unique_image_count", MetadataValue{}},
      {"unique_buffer_count", MetadataValue{}},
      {"guard_addresses", metadata_array({})},
      {"planes", metadata_array({})}};
  {
    // shared_planes() copies only an eager shared-pointer table; for a guarded
    // source it supplies the existing native guard tag on each entry. A
    // non-eager source returns nullopt without calling read_plane().
    const auto planes = dataset.shared_planes();
    if (planes) {
      MetadataArray rows, guards;
      std::set<const Image *> images;
      std::set<const double *> buffers;
      std::set<const void *> guardPointers;
      std::uint64_t payloadBytes = 0, capacityBytes = 0;
      for (std::size_t index = 0; index < planes->size(); ++index) {
        const auto image = planes->shared(index);
        const auto *tag =
            std::get_deleter<detail::ResourceImageDeleter>(image);
        const void *guard = tag ? tag->guard.get() : nullptr;
        const auto *buffer = image->pixels.data();
        const auto payload =
            gaussian_resource_detail::pixel_bytes(image->pixels.size());
        const auto capacity =
            gaussian_resource_detail::pixel_bytes(image->pixels.capacity());
        images.insert(image.get());
        if (buffers.insert(buffer).second) {
          gaussian_resource_detail::add_bytes(payloadBytes, payload);
          gaussian_resource_detail::add_bytes(capacityBytes, capacity);
        }
        if (guard && guardPointers.insert(guard).second)
          guards.emplace_back(gaussian_resource_detail::address(guard));
        rows.emplace_back(metadata_object({
            {"index", std::uint64_t(index)},
            {"width", std::int64_t(image->width)},
            {"height", std::int64_t(image->height)},
            {"channels", std::int64_t(image->channels)},
            {"image_address", gaussian_resource_detail::address(image.get())},
            {"buffer_address", gaussian_resource_detail::address(buffer)},
            {"guard_tag_present", tag != nullptr},
            {"guard_address", gaussian_resource_detail::address(guard)},
            {"pixel_count", std::uint64_t(image->pixels.size())},
            {"pixel_capacity", std::uint64_t(image->pixels.capacity())},
            {"pixel_payload_bytes", payload},
            {"pixel_capacity_bytes", capacity}}));
      }
      result["eager"] = true;
      result["pixel_payload_bytes"] = payloadBytes;
      result["pixel_capacity_bytes"] = capacityBytes;
      result["unique_image_count"] = std::uint64_t(images.size());
      result["unique_buffer_count"] = std::uint64_t(buffers.size());
      result["guard_addresses"] = metadata_array(std::move(guards));
      result["planes"] = metadata_array(std::move(rows));
    }
  } // All local table/image/guard aliases have been destroyed before return.
  return result;
}
} // namespace imagejxx::desktop
