# Scientific LZW TIFF

The native scientific IO service imports and exports modern TIFF LZW for UInt8,
UInt16 and Float32 grayscale and interleaved UInt8 RGB. Python and both ImGui
hosts use the same service. No dependency was added; the C++ codec is original
and uses fixed dictionaries. Existing formats, ordinals, defaults and record
producers remain unchanged.

## Contract and bounds

Input uses compression 5, predictor 1, MSB-first codes and TIFF early change.
The initial code must be CLEAR; EOI is required. The decoder validates dictionary
codes and prefix chains, requires exactly the declared decoded byte count and
accepts only the final zero bit padding, with no trailing bytes. Both TIFF byte
orders are supported. C-fast file pages retain native TCZYXS order; RGB stays on
S. Legacy LSB/non-early-change variants are not qualified by this profile.
The reader's width rule is consistent with the pinned ImageJ reader and
[TIFF LZW conventions](https://libtiff.gitlab.io/libtiff/specification/index.html).

The encoder uses an 8,192-slot fixed dictionary hash; the decoder uses bounded
prefix/suffix/string arrays. The minimum LZW codec reservation is 128 KiB;
encoded/decoded strip buffers are charged separately. Checkpoints cover input,
output and collision-probe work. Tests include cancellation during both encoding
and decoding. Estimates remain read-free, use a checked 12-bit worst-case code
bound and include clear/reset/end overhead. Repetitive inputs shrink; high-entropy
inputs can expand within the bound.

Output is little-endian, predictor 1, with at most 64 rows per strip. Actual
compressed bytes are appended to an owned stage and planned strip offsets/counts
are finalized through its held file object. Verification precedes atomic
publication. Defaults refuse overwrite; failure/cancellation preserve the
existing destination and clean only owned staging.

## API and editor

Append-only enum values 12–15 are `ScientificTiffU16LzwV1`,
`ScientificTiffFloat32LzwV1`, `ScientificTiffU8LzwV1` and
`ScientificTiffRgb8LzwV1`. Python exposes matching
`SCIENTIFIC_TIFF_U16_LZW_V1`, `SCIENTIFIC_TIFF_FLOAT32_LZW_V1`,
`SCIENTIFIC_TIFF_U8_LZW_V1` and `SCIENTIFIC_TIFF_RGB8_LZW_V1`.
Select the matching value on `ImageWriteRequest`.

```python
from imagejxx import scientific_io as io
result = io.read_image_series("input.tif")
request = io.ImageWriteRequest()
request.format = io.ImageFileFormat.SCIENTIFIC_TIFF_U16_LZW_V1
io.write_image_series("output.tif", result.dataset, result.metadata, request=request)
```

Integer/RGB samples remain finite-only. Float32 reads default to finite-only;
quiet-NaN/full-IEEE admission remains explicit. Every admitted Float32 word is
preserved through compression, including signaling NaN fields and signed zero.
Inspection is metadata-only; malformed payloads can pass inspection and fail
reading. The old compression-tag-only negative carriers remain metadata refusals
because their explicit pages have mixed compression tags; their IDs are retained.

The shared export combo offers Uncompressed, Deflate, PackBits and LZW.
Uncompressed remains the default. Codec changes clear estimates/loss consent.
LZW actions produce version 7 records with explicit codec, dtype, components and
sample policy; replay restores an inert form and requires renewed review.
Older record producers and decoder restrictions remain unchanged.

## Evidence

The audit is `build/p08h-lzw-tiff001/final-audit001.json`. Windows passes 78
native/desktop suites, while source and installed Python pass identical
9,910-case inventories, retaining all 9,752 prior cases and adding 158.
The initial focused gate passed 212 cases before the cancellation enhancement;
every one of those IDs also passes in the final full inventories. Updated
installed SDK consumers pass for RGB and high-entropy full-IEEE Float32.

The independent corpus contains 44 valid carriers: 24 ordinary layouts,
16 high-entropy layouts and four precise EOI/width-boundary layouts. Eight
malformed cases cover code/clear/end, size, trailing and descriptor failures.
Independent generation uses a full-string dictionary and receiver-width clock;
the native encoder uses a fixed pair hash and insertion-width counter.
Known vectors, KwKwK data, 9/10/11/12-bit transitions, resets, output bounds and
mid-stream cancellation are covered. Six additional Float32 carriers retain the
pinned ImageJ finite/quiet/full-IEEE source/filter corpus.

Twelve actual input scenarios across four source/installed host contexts produce
48 observations, including startup/append, linked views, Float32 policy recovery,
preview/history, compressed export, cancellation, replaced-file refusal and inert
replay. This is actual ImGui input and CPU-readback evidence, not OS-input or GPU
equivalence. Pinned ImageJ verifies 134 reads: 44 independent carriers, 44 native
exports, 24 scalar/RGB GUI exports, six Float32 fixtures and sixteen Float32 GUI
exports. Its actual Remove NaNs runner matches all six fixture filter results.
No Java runtime/classes ship in the products.

The cancellation enhancement leaves all 44 native outputs byte-identical to the
early ImageJ-observed outputs, verified against current native-test files and
retained archive pins. Final full/native/SDK gates freeze the current runtime.
Two native producer freezes captured a temporary path-only edit to the separate
Java reference scripts; the auditor reconstructs and hashes those exact earlier
scripts. Final interoperability uses pinned stage-local harnesses against current
GUI exports.

Twenty-nine owned scratch targets were archived/removed, freeing 1,891,916,389
net bytes. Raw logs/reports, verified archives and three installed prefixes remain.
Predictors, legacy codec variants, tiled/BigTIFF, broader metadata/high-bit/alpha
and Linux TIFF/product qualification remain open, alongside remaining ImageJ/Fiji/
editor/release requirements. All99 remains 42 in progress / 57 open / 0 complete.

Integer horizontal prediction now has separate explicit profiles; see
[horizontal TIFF prediction](SCIENTIFIC_HORIZONTAL_TIFF.md). The historical
`predictor.tif` case is admitted through its predictor-2 profile, with its test
ID retained and its reconstructed words checked independently. The predictor-1
profiles described above retain their existing serialization and defaults.
