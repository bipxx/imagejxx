#pragma once
#include <imagejxx/image.hpp>

namespace imagejxx {
// Consecutive complete groups, with immutable input and existing projection
// method/dtype policies. Group size must divide the selected Z count exactly.
Stack project_groups(const Stack &, const std::string &method,
                     std::size_t group_size);
Stack project_groups(const Stack &, const std::string &method,
                     std::size_t group_size, std::size_t first, std::size_t last);
} // namespace imagejxx
