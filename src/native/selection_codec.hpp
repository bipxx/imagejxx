#pragma once
// Core-private canonical nativeV3 annotation grammar. Sessions and command
// records share this codec; it is not an installed SDK or a second wire schema.
#include <imagejxx/session.hpp>
#include <nlohmann/json.hpp>

namespace imagejxx::detail::selection_codec {
using Json = nlohmann::json;
Json encode_selection(const Selection &value, bool allow_regions = false, bool allow_paths = false);
Json encode_roi(const Roi &value, bool allow_regions = false, bool allow_paths = false);
// The caller preflights the complete record and validates returned geometry
// against its dataset descriptor. These functions acquire no image pixels.
Selection decode_selection(const Json &value, const SessionLimits &limits, bool allow_regions = false, bool allow_paths = false);
Roi decode_roi(const Json &value, const SessionLimits &limits, bool allow_regions = false, bool allow_paths = false);
} // namespace imagejxx::detail::selection_codec
