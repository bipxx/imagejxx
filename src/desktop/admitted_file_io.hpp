#pragma once
#include <cstddef>
#include <functional>
#include <string>
#include <vector>
namespace imagejxx::desktop {
// Size is determined from the opened stream; admission precedes vector storage.
std::vector<unsigned char> readBytes(const std::string&,std::size_t,
    const std::function<void(std::size_t)>& admit);
}
