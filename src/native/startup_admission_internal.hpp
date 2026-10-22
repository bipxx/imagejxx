#pragma once
#include <imagejxx/session.hpp>
namespace imagejxx::detail {
// Preserve startup's aggregate document/plane/sample/metadata/working limits
// without claiming that a virtual source can be encoded as an eager session.
// Descriptor-only; no pixel reads, image buffers or new document owners.
SessionEstimate estimate_startup_admission(const SessionCapture &,
    const SessionLimits & = {});
}
