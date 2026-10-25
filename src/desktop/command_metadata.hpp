#pragma once
#include "desktop.hpp"

namespace imagejxx::desktop {
// Descriptor-only form defaults and workspace admission share the existing
// native operation rules and JSON codecs without requesting a source plane.
Json defaults(const Json &command, const OperationDefaultContext &context);
std::size_t extraWorkspace(const std::string &id,
                          const OperationDefaultContext &context,
                          const Json &parameters);
} // namespace imagejxx::desktop
