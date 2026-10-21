#pragma once
#include <imagejxx/metadata.hpp>
#include <imagejxx/rgb_conversion.hpp>
namespace imagejxx::detail {
std::optional<RgbLookupTable> legacy_rgb_palette(const MetadataObject &properties);
}
