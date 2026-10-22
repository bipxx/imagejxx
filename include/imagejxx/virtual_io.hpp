#pragma once
#include <imagejxx/io.hpp>
namespace imagejxx {
// Inspect and retain a guarded file handle without decoding pixels. The virtual
// source reads only intersecting strips/tiles, with finite-only sample admission.
// Limits bound retained descriptors plus each concurrent read's scratch/output;
// caller-held images, aggregate concurrent reads and renderer caches are external.
// File identity/size/change tokens are checked before and after every read.
ImageReadResult open_virtual_image_series(const std::string &,
    const ImageReadRequest & = {}, const ImageFileLimits & = {},
    const ExecutionControl & = {});
}
