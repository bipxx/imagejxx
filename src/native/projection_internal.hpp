#pragma once
#include <imagejxx/image.hpp>
#include <memory>

namespace imagejxx::detail {
// Borrowed immutable planes; caller resolves method and validates finite samples.
// Scratch is reused across groups and must have exactly one entry per plane.
Image reduce_projection_group(const std::vector<std::shared_ptr<const Image>> &,
                              const std::string &, std::vector<double> &);
}
