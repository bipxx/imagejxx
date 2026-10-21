# Scientific PackBits TIFF

The optional C++ scientific IO service and Python interface support PackBits
for UInt8, UInt16 and Float32 grayscale and interleaved UInt8 RGB. Both ImGui
hosts expose the codec. Existing uncompressed/Deflate formats, defaults, enum
ordinals and record producers remain unchanged. No dependency was added.

## Codec and bounds

The original C++ implementation follows the byte-packet and row rules described
in section 10 of the [TIFF PackBits document](https://ftp.nice.ch/pub/next/developer/resources/libraries/_tiff.3.0b.s/tiff/doc/_TIFF6.pdf).
It does not copy ImageJ's Java routine credited to Bio-Formats. Input uses TIFF
compression 32773, predictor 1, chunky layout, orientation 1 and fill order 1.
All pages must use the same codec and layout. Both input byte orders are
supported. RGB components stay on S, separate from C/Z/T.

The decoder writes into a pre-sized strip buffer, rejects truncated or excess/
short decoded streams, and consumes bounded no-op packets. It accepts legacy
strip-spanning literal packets for ImageJ compatibility. The writer packs each
row separately, limits packets to 128 bytes, and emits no cross-row packets.
The checked worst-case bound per row is `bytes + ceil(bytes/128)`; strip/file
bounds sum those row bounds. Constant images measurably shrink; random data may
grow within the bound. No file-wide buffer or padded worst-case strip slots are
used. Encoded and decoded buffers are charged independently, and cancellation
is checked between bounded transfers and codec work.

The writer finalizes strip offsets/counts through the held atomic staging
object, verifies descriptors and finalized metadata, and publishes once. Default
publication refuses replacement; explicit replacement, cancellation and failure
handling retain the existing transactional contract.

## API

Append-only enum values 8–11 are `ScientificTiffU16PackBitsV1`,
`ScientificTiffFloat32PackBitsV1`, `ScientificTiffU8PackBitsV1` and
`ScientificTiffRgb8PackBitsV1`. Python exposes the corresponding
`SCIENTIFIC_TIFF_U16_PACKBITS_V1`, `SCIENTIFIC_TIFF_FLOAT32_PACKBITS_V1`,
`SCIENTIFIC_TIFF_U8_PACKBITS_V1` and `SCIENTIFIC_TIFF_RGB8_PACKBITS_V1`.
Select the matching format on `ImageWriteRequest`.

```python
from imagejxx import scientific_io as io
result = io.read_image_series("input.tif")
request = io.ImageWriteRequest()
request.format = io.ImageFileFormat.SCIENTIFIC_TIFF_U16_PACKBITS_V1
io.write_image_series("output.tif", result.dataset, result.metadata, request=request)
```

UInt8/UInt16/RGB remain finite-only. Float32 reads default to finite-only;
quiet-NaN/full-IEEE policies require explicit admission. Compression preserves
every admitted word, including signaling NaN fields, infinities and signed zero.
Inspection remains metadata-only. Payload errors can therefore be reported only
when decoding begins. The historical `packbits-complete.tif` control is now
admitted; its original fixture manifest/pins and all test IDs remain retained.

## Shared editor and replay

The scientific export combo offers Uncompressed, Deflate and PackBits.
Uncompressed remains the default. Changing the codec clears the estimate and
loss consent. PackBits open/export actions produce version 6 records carrying
explicit compression, dtype, components and sample policy. Older producers
remain unchanged. Replay opens an inert form, restores the codec, and requires
fresh review and consent before any publication.

## Evidence

`build/p08g-packbits-tiff001/final-audit001.json` records 77 passing Windows
native/desktop suites and identical 9,752-case source/installed Python inventories,
retaining all 9,634 previous case IDs and adding 118. The focused Python gate
passes 309 cases; installed RGB and full-IEEE IO SDK consumers pass.

The independent corpus has 24 valid layouts and eight malformed carriers,
covering four sample layouts, both byte orders and three strip heights.
Additional packet vectors cover literals/repeats/no-ops, 127/128/129 boundaries,
row isolation, size bounds and cancellation. Six further Float32 carriers
preserve the pinned ImageJ finite/quiet/full-IEEE source/filter corpus.

Twelve actual input scenarios run in four source/installed host contexts,
yielding 48 observations. They cover startup/append, file review, linked planes,
Float32 policy recovery, Remove NaNs preview/history, export, cancellation,
source replacement and inert replay. The observations concern actual ImGui
input events and CPU readbacks, not OS-input or GPU equivalence.

Pinned ImageJ verifies 94 reads: 24 independent carriers, 24 native exports,
24 scalar/RGB GUI exports, six Float32 fixtures and sixteen Float32 GUI exports.
Its actual Remove NaNs runner reproduces all six fixture filter results. No Java
runtime or classes ship in the installed products. Twenty-seven scratch targets
were archived/removed, freeing 1,682,045,831 net bytes; raw reports/logs and three
installed product prefixes remain.

The initial Float32 fixture builder emitted the wrong compression tag; the
strict Deflate decoder refused that mislabeled packet stream. The generator was
corrected and all final gates use PackBits-tagged carriers. The final native gate
uses byte-identical built products and the corrected fixture inventory. Its
scalar/RGB native observation freeze predates that unrelated Float32 generator
fix; the auditor reconstructs and hashes the entire earlier generator exactly.

[LZW](SCIENTIFIC_LZW_TIFF.md) is now available separately. Predictors,
tiles/BigTIFF, broader metadata, high-bit/alpha and Linux TIFF/
product qualification remain open, alongside remaining ImageJ/Fiji/editor/release
requirements. All99 remains 42 in progress / 57 open / 0 complete.
