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
} // namespace imagejxx::detail
