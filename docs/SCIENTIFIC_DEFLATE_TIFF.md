# Scientific Deflate TIFF

The C++ scientific IO service imports and exports lossless Deflate strips for
UInt8 grayscale, UInt16 grayscale, Float32 grayscale and interleaved UInt8 RGB.
Components stay on S; explicit IFDs preserve C-fast pages and native TCZYXS
axis order. The existing uncompressed formats, enum ordinals and defaults remain
unchanged. No codec dependency was added: Deflate uses the core's existing
vendored zlib with private prefixed symbols and a capped allocator.

## Profile

Input accepts compression 8 or 32946, predictor 1, chunky components, orientation
1 and fill order 1. All pages must use the same compression tag and layout.
Output uses compression 8, little-endian words and at most 64 rows per strip.
Unsigned scalar types and UInt8 RGB remain finite-only. Float32 reads default
to finite-only; callers must explicitly choose quiet-NaN or full-IEEE admission.
All admitted Float32 words, including signaling NaN fields and signed zero,
are preserved without floating conversion in the codec.

Inspection validates descriptors and file ranges, and never decodes a strip.
Malformed compressed payloads can therefore pass inspection and fail decoding.
The decoder requires exactly the declared decoded byte count, a complete zlib
stream, a valid checksum and no trailing bytes or preset dictionary. Predictor
2/3, tiles, BigTIFF, high-bit RGB and alpha remain outside this profile.
[LZW](SCIENTIFIC_LZW_TIFF.md) is available as a separate profile.
[PackBits](SCIENTIFIC_PACKBITS_TIFF.md) is available as a separate profile. The original independent carriers and upstream checks are retained
under `tests/fixtures/scientific-tiff-deflate` and
`build/p08e-deflate-tiff001/reference002`.

## C++ and Python

Append-only format values 4/5/6/7 are respectively
`ScientificTiffU16DeflateV1`, `ScientificTiffFloat32DeflateV1`,
`ScientificTiffU8DeflateV1` and `ScientificTiffRgb8DeflateV1`.
The matching Python names are `SCIENTIFIC_TIFF_U16_DEFLATE_V1`,
`SCIENTIFIC_TIFF_FLOAT32_DEFLATE_V1`, `SCIENTIFIC_TIFF_U8_DEFLATE_V1` and
`SCIENTIFIC_TIFF_RGB8_DEFLATE_V1` on `ImageFileFormat`.
Select the matching format on `ImageWriteRequest`; all existing public read,
estimate, write, limits, cancellation and overwrite APIs apply.

```python
from imagejxx import scientific_io as io

result = io.read_image_series("input.tif")
request = io.ImageWriteRequest()
request.format = io.ImageFileFormat.SCIENTIFIC_TIFF_U16_DEFLATE_V1
estimate = io.estimate_image_write(result.dataset, result.metadata, request=request)
io.write_image_series("output.tif", result.dataset, result.metadata, request=request)
```

The writer estimates an upper bound without reading provider planes. It holds
one plane, one raw strip and one bounded encoded strip, not the whole file.
The estimate includes codec reservations and both strip buffers. A conservative
1 MiB minimum codec budget applies to Deflate. Encoded strip counts are capped
separately and decoded samples retain the existing plane/total limits.
Output may exceed uncompressed size for incompressible samples; estimates cover
that case. Repetitive-image qualification requires an actual size reduction.

The writer reserves the metadata prefix, streams compressed strips, then patches
only planned strip offsets/counts through the held staged object. Final
verification checks exact finalized metadata, actual size against the frozen
upper bound, sample layout, compression and encoded calibration before atomic
publication. No-replace is the default. Failed patching, cancellation and
callback errors abandon the owned stage and preserve the destination.

## Shared ImGui editor

The scientific export form offers **Uncompressed** and **Deflate (lossless)**.
Uncompressed remains the default. A codec change clears the estimate and loss
consent, requiring another review. Opening a compressed file reports Deflate
and retains the existing explicit axes/calibration and Float32 sample-policy
choices. Both the C++ desktop and optional Python UI use this shared panel.

Compressed open/export actions produce version 5 records with explicit
`compression: "deflate"`, `dtype`, `components`, and `sample_policy`. Existing
uncompressed producers retain record versions 1/2/3/4. Replay restores an inert
form and the selected export codec; it never authorizes a read, loss consent,
overwrite or publication. Source provenance can be exported into either
compressed or uncompressed output through the usual canonicalization rules.

## Qualification boundary

The independent corpus contains 48 valid layouts spanning four sample layouts,
two byte orders, two Deflate tag values and three strip heights, plus eight
malformed descriptor/payload cases. Pinned ImageJ 1.54r Opener reads all 48
independent carriers and all 48 native exports with exact words/components,
C/Z/T, XY/Z calibration and frame intervals. No Java runtime is shipped.

The original Deflate checkpoint passed 76 native/desktop suites and identical
9,622-case source and installed Python inventories, retaining all 9,468 baseline
cases and adding 154. The public installed IO SDK passes. The final audit is
`build/p08e-deflate-tiff001/final-audit001.json`. Cleanup archived and removed
32 owned scratch targets, freeing 2,117,386,832 net bytes. The new
physical mouse/key/text cases cover UInt8, UInt16 and RGB in both byte orders.
The subsequent [Float32 file-control checkpoint](SCIENTIFIC_DEFLATE_FLOAT32_UI.md)
adds six finite/quiet/full-IEEE scenarios across four host contexts, with exact
linked-view words, preview/history, export, cancellation and source replacement.
Its source/installed Python inventories both pass 9,634 cases. ImageJ reads all
six compressed fixtures and sixteen GUI-produced exports exactly. Linux TIFF and
installed Linux product qualification remain open. Whole ImageJ/Fiji family
completion is not claimed; All99 remains 42 in progress / 57 open / 0 complete.

Integer horizontal prediction now has separate explicit profiles; see
[horizontal TIFF prediction](SCIENTIFIC_HORIZONTAL_TIFF.md). The historical
`predictor.tif` case is admitted through its predictor-2 profile, with its test
ID retained and its reconstructed words checked independently. The predictor-1
profiles described above retain their existing serialization and defaults.
