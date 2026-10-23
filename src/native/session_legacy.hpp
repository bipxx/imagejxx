#pragma once

// Private detached-model compatibility projection. This is not an installed
// SDK API or another session encoder/parser.
#include <imagejxx/session.hpp>

namespace imagejxx::detail {
// Returns {document:<legacy document>, native_state:{metadata:<native DTO>,
// display_properties:<present Python legacy display keys>}}. Only a losslessly
// representable eager T=C=1 snapshot is admitted. No owner or frame is created.
std::string session_legacy_record_json(const DocumentSnapshot &snapshot,
                                       const SessionLimits &limits = {});
// Explicit facade migration only. Public V2/V3 file decoding remains closed to
// regional geometry. The result uses the native V4 writer and its limits.
std::vector<unsigned char> upgrade_regional_legacy_session(
    const std::vector<unsigned char> &, const SessionLimits & = {});
} // namespace imagejxx::detail
