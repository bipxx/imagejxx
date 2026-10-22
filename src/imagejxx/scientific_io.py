"""Optional native calibrated Dataset TIFF interchange.

Build with IMAGEJXX_BUILD_IO=ON and IMAGEJXX_BUILD_PYTHON=ON. All parsing,
processing, admission, cancellation and publication run in the C++ service.
This API uses explicit TCZYXS axes; legacy ``imread``/``imwrite`` stay separate.
"""
from __future__ import annotations

import os
from importlib import import_module as _import_module

try:
    _io = _import_module("._scientific_io", __package__)
except ModuleNotFoundError as error:
    # Preserve loader diagnostics for a present but broken extension.
    if error.name != "imagejxx._scientific_io":
        raise
    raise ImportError(
        "Scientific Dataset IO is not built. Configure IMAGEJXX_BUILD_IO=ON "
        "with IMAGEJXX_BUILD_PYTHON=ON and the reviewed TIFF dependency prefix."
    ) from error

ImageFileFormat = _io.ImageFileFormat
TiffContainer = _io.TiffContainer
FileReplacePolicy = _io.FileReplacePolicy
ImageFileLimits = _io.ImageFileLimits
ImageSeriesInfo = _io.ImageSeriesInfo
ImageFileInspection = _io.ImageFileInspection
ImageReadRequest = _io.ImageReadRequest
ImageReadResult = _io.ImageReadResult
ImageWriteRequest = _io.ImageWriteRequest
ImageWriteEstimate = _io.ImageWriteEstimate
format_id = _io.format_id
float32_format_id = _io.float32_format_id
uint8_format_id = _io.uint8_format_id
u16_deflate_horizontal_tiled_format_id = _io.u16_deflate_horizontal_tiled_format_id
u8_deflate_horizontal_tiled_format_id = _io.u8_deflate_horizontal_tiled_format_id
rgb8_deflate_horizontal_tiled_format_id = _io.rgb8_deflate_horizontal_tiled_format_id
u16_lzw_horizontal_tiled_format_id = _io.u16_lzw_horizontal_tiled_format_id
u8_lzw_horizontal_tiled_format_id = _io.u8_lzw_horizontal_tiled_format_id
rgb8_lzw_horizontal_tiled_format_id = _io.rgb8_lzw_horizontal_tiled_format_id
float32_deflate_floating_tiled_format_id = _io.float32_deflate_floating_tiled_format_id
float32_lzw_floating_tiled_format_id = _io.float32_lzw_floating_tiled_format_id
u16_tiled_format_id = _io.u16_tiled_format_id
u8_tiled_format_id = _io.u8_tiled_format_id
rgb8_tiled_format_id = _io.rgb8_tiled_format_id
float32_tiled_format_id = _io.float32_tiled_format_id
u16_deflate_tiled_format_id = _io.u16_deflate_tiled_format_id
u8_deflate_tiled_format_id = _io.u8_deflate_tiled_format_id
rgb8_deflate_tiled_format_id = _io.rgb8_deflate_tiled_format_id
float32_deflate_tiled_format_id = _io.float32_deflate_tiled_format_id
u16_packbits_tiled_format_id = _io.u16_packbits_tiled_format_id
u8_packbits_tiled_format_id = _io.u8_packbits_tiled_format_id
rgb8_packbits_tiled_format_id = _io.rgb8_packbits_tiled_format_id
float32_packbits_tiled_format_id = _io.float32_packbits_tiled_format_id
u16_lzw_tiled_format_id = _io.u16_lzw_tiled_format_id
u8_lzw_tiled_format_id = _io.u8_lzw_tiled_format_id
rgb8_lzw_tiled_format_id = _io.rgb8_lzw_tiled_format_id
float32_lzw_tiled_format_id = _io.float32_lzw_tiled_format_id
rgb8_format_id = _io.rgb8_format_id
u16_deflate_format_id = _io.u16_deflate_format_id
float32_deflate_format_id = _io.float32_deflate_format_id
u8_deflate_format_id = _io.u8_deflate_format_id
rgb8_deflate_format_id = _io.rgb8_deflate_format_id
u16_packbits_format_id = _io.u16_packbits_format_id
float32_packbits_format_id = _io.float32_packbits_format_id
u8_packbits_format_id = _io.u8_packbits_format_id
rgb8_packbits_format_id = _io.rgb8_packbits_format_id
u16_lzw_format_id = _io.u16_lzw_format_id
float32_lzw_format_id = _io.float32_lzw_format_id
u8_lzw_format_id = _io.u8_lzw_format_id
rgb8_lzw_format_id = _io.rgb8_lzw_format_id
float32_deflate_floating_format_id = _io.float32_deflate_floating_format_id
float32_lzw_floating_format_id = _io.float32_lzw_floating_format_id
u16_deflate_horizontal_format_id = _io.u16_deflate_horizontal_format_id
u8_deflate_horizontal_format_id = _io.u8_deflate_horizontal_format_id
rgb8_deflate_horizontal_format_id = _io.rgb8_deflate_horizontal_format_id
u16_lzw_horizontal_format_id = _io.u16_lzw_horizontal_format_id
u8_lzw_horizontal_format_id = _io.u8_lzw_horizontal_format_id
rgb8_lzw_horizontal_format_id = _io.rgb8_lzw_horizontal_format_id



def _path(path):
    value = os.fspath(path)
    if not isinstance(value, str):
        raise TypeError("Scientific IO paths must be str or PathLike[str]")
    return value


def inspect_image_file(path, *, limits=None, cancellation=None, progress=None,
                       checkpoint=None, stage_progress=None):
    """Inspect series/calibration/provenance without decoding sample planes."""
    return _io.inspect_image_file(
        _path(path), limits if limits is not None else ImageFileLimits(),
        cancellation=cancellation, progress=progress, checkpoint=checkpoint,
        stage_progress=stage_progress,
    )


def read_image_series(path, *, request=None, limits=None, cancellation=None,
                      progress=None, checkpoint=None, stage_progress=None):
    """Return ImageReadResult with a native Dataset and detached metadata."""
    return _io.read_image_series(
        _path(path), request if request is not None else ImageReadRequest(),
        limits if limits is not None else ImageFileLimits(),
        cancellation=cancellation, progress=progress, checkpoint=checkpoint,
        stage_progress=stage_progress,
    )


def open_virtual_image_series(path, *, request=None, limits=None, cancellation=None,
                              progress=None, checkpoint=None, stage_progress=None):
    """Inspect a finite-only TIFF and retain a guarded, regional native provider.

    No pixels decode at open. File change tokens are checked on each read.
    Limits bound descriptors and per-read output/scratch, excluding caller-held
    images, aggregate concurrent reads, provider-independent caches and process RSS.
    """
    return _io.open_virtual_image_series(
        _path(path), request if request is not None else ImageReadRequest(),
        limits if limits is not None else ImageFileLimits(), cancellation=cancellation,
        progress=progress, checkpoint=checkpoint, stage_progress=stage_progress)


def estimate_image_write(dataset, metadata=None, *, request=None, limits=None):
    """Estimate bounds/rational pairs/canonicalization without provider reads.

    Destination and temporary-path storage is checked additionally by the writer.
    """
    return _io.estimate_image_write(
        dataset, metadata if metadata is not None else {},
        request if request is not None else ImageWriteRequest(),
        limits if limits is not None else ImageFileLimits(),
    )


def write_image_series(path, dataset, metadata=None, *, request=None, limits=None,
                       cancellation=None, progress=None, checkpoint=None,
                       stage_progress=None):
    """Export through exclusive staging; default publication refuses overwrite.

    Request REPLACE_EXISTING explicitly to replace a destination atomically.
    Source provenance canonicalization is reported by estimate_image_write.
    """
    _io.write_image_series(
        _path(path), dataset, metadata if metadata is not None else {},
        request if request is not None else ImageWriteRequest(),
        limits if limits is not None else ImageFileLimits(),
        cancellation=cancellation, progress=progress, checkpoint=checkpoint,
        stage_progress=stage_progress,
    )


__all__ = [
    "ImageFileFormat", "TiffContainer", "FileReplacePolicy", "ImageFileLimits", "ImageSeriesInfo",
    "ImageFileInspection", "ImageReadRequest", "ImageReadResult",
    "ImageWriteRequest", "ImageWriteEstimate",     "u16_deflate_horizontal_tiled_format_id",
    "u8_deflate_horizontal_tiled_format_id",
    "rgb8_deflate_horizontal_tiled_format_id",
    "u16_lzw_horizontal_tiled_format_id",
    "u8_lzw_horizontal_tiled_format_id",
    "rgb8_lzw_horizontal_tiled_format_id",
    "float32_deflate_floating_tiled_format_id",
    "float32_lzw_floating_tiled_format_id",
"u16_tiled_format_id", "u8_tiled_format_id", "rgb8_tiled_format_id", "float32_tiled_format_id", "u16_deflate_tiled_format_id", "u8_deflate_tiled_format_id", "rgb8_deflate_tiled_format_id", "float32_deflate_tiled_format_id", "u16_packbits_tiled_format_id", "u8_packbits_tiled_format_id", "rgb8_packbits_tiled_format_id", "float32_packbits_tiled_format_id", "u16_lzw_tiled_format_id", "u8_lzw_tiled_format_id", "rgb8_lzw_tiled_format_id", "float32_lzw_tiled_format_id",    "format_id", "float32_format_id", "uint8_format_id", "rgb8_format_id", "u16_deflate_format_id", "float32_deflate_format_id", "u8_deflate_format_id", "rgb8_deflate_format_id", "u16_packbits_format_id", "float32_packbits_format_id", "u8_packbits_format_id", "rgb8_packbits_format_id", "u16_lzw_format_id", "float32_lzw_format_id", "u8_lzw_format_id", "rgb8_lzw_format_id", "float32_deflate_floating_format_id", "float32_lzw_floating_format_id", "u16_deflate_horizontal_format_id", "u8_deflate_horizontal_format_id", "rgb8_deflate_horizontal_format_id", "u16_lzw_horizontal_format_id", "u8_lzw_horizontal_format_id", "rgb8_lzw_horizontal_format_id", "inspect_image_file",
    "read_image_series", "open_virtual_image_series", "estimate_image_write", "write_image_series",
]
