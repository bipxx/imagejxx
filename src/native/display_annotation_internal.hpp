#pragma once
#include <imagejxx/metadata.hpp>
namespace imagejxx::detail {
// Borrowed-tree preflight, before JSON/base64 or owned payload copies.
void preflight_display_annotation_metadata(const MetadataValue &,
                                           std::size_t cap = 16 * 1024 * 1024);
void preflight_display_annotation_record(const MetadataObject &,
                                         std::size_t cap = 64 * 1024 * 1024);
} // namespace imagejxx::detail
