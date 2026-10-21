# Scientific RGB TIFF

The native IO service supports `ScientificTiffRgb8V1`: calibrated, classic,
uncompressed TIFF with three interleaved UInt8 RGB components. C/Z/T still
select independent image planes; S=3 identifies per-pixel RGB components. Both
C=1 and C=2 are qualified against actual pinned ImageJ FileSaver/Opener files.
Scalar UInt8, UInt16 and Float32 profiles and their defaults remain supported.

The profile supports both input byte orders, single/multiple strips, C-fast file
page order and native TCZYXS datasets. RGB requires PhotometricInterpretation=2,
SamplesPerPixel=3, uniform 8-bit components and unsigned samples. Alpha, planar,
high-bit RGB, mixed component fields and other unsupported layouts are rejected
before decoding. Output is little-endian, with at most 64 rows per strip.

Read/write byte counts, codec buffers, plane/total sample limits and working
estimates include all components. `max_plane_samples` and `max_total_samples`
count component samples. Export validates each byte and retains read-free planning,
atomic overwrite and cancellation behavior. Scalar formats preserve their prior
wire layout. RGB bits/sample-format tables carry three explicit component values.

```python
from imagejxx import scientific_io as io

result = io.read_image_series("input-rgb.tif")
rgb = result.dataset.numpy("TCZYXS")
request = io.ImageWriteRequest()
request.format = io.ImageFileFormat.SCIENTIFIC_TIFF_RGB8_V1
io.write_image_series("output-rgb.tif", result.dataset, result.metadata, request=request)
```

The C++ format is `ImageFileFormat::ScientificTiffRgb8V1`. The enum value is
appended, leaving existing values and the default UInt16 write request unchanged.
RGB read policy is finite-only. The shared ImGui editor infers the export format
from dtype and component count and displays the reviewed TCZYXS shape. It retains
axes/calibration consent, explicit flat-import consent and pixels/calibration-only
export consent. Python wraps the native service; processing and codecs stay in C++.

## Component-layout records

RGB file actions use record version 4 with explicit `dtype`, `sample_policy` and
`components`. The root profile, reviewed shape and component count must agree.
Version 4 also supports the existing scalar layouts with components=1. Their
producers retain versions 1/2/3, preserving prior contracts. Inert replay drafts
require renewed review; historical metadata does not authorize file access or
publication. Scalar C-axis channels and per-pixel RGB components are validated
separately. Source pixels, clean history and display survive export unchanged.

## Qualification

Ten independent stdlib carriers and ten malformed variants cover C=1/C=2,
both input byte orders, strips, 130-row images, exact interleaved bytes, estimates,
limits and cancellation after row 65. Four actual ImageJ FileSaver carriers
preserve source bytes and dimensions through Opener. Native inversion matches
the pinned current-plane RGB plugin result.

Actual ImGui file controls pass for all four carrier variants in source/installed
C++ and Python hosts. They cover ordinary/scientific opening, explicit flat consent,
startup atomicity, ordered append, calibrated review, linked views, export consent,
cancellation and inert replay. ImageJ reopens all sixteen native exports with
exact RGB bytes, C/Z/T dimensions, spacing and frame timing.

The checkpoint passes 74 Windows native/desktop suites, identical source/installed
9,468-case Python inventories without skips/exclusions, and an installed public
C++ IO SDK consumer. It retains all previous 9,390 cases and adds 78. Thirty-two
verified scratch targets were archived/removed, freeing 1,159,647,489 net bytes.
Evidence: `build/p08c-rgb-tiff001/final-audit001.json`.

The implementation uses the existing reviewed Windows TIFF runtime closure.
No dependency, Qt component, non-commercial component or reference Java runtime
was added to application products. Compressed/tiled files, BigTIFF, high-bit/alpha
color, broader metadata variants, Linux IO certification and remaining
ImageJ/Fiji/editor/release requirements remain open. Compressed TIFF is next.
