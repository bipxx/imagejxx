#pragma once
#include <imagejxx/image.hpp>
#include <functional>
namespace imagejxx::detail {
// Shared existing P5/P6 encoder; emits header then bounded packed chunks.
// No path ownership/publication. The sink owns its explicit IO controls.
void encode_pnm_payload(const Image&,const std::function<void(const unsigned char*,std::size_t)>&);
}
