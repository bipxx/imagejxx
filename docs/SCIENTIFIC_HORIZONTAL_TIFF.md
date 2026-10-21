# Scientific TIFF horizontal integer prediction

The C++ scientific IO service supports TIFF Predictor 2 with Deflate and LZW
for UInt8, UInt16 and interleaved UInt8 RGB. Python and both Dear ImGui hosts
share the implementation. The transform is original code and adds no dependency.

## Format contract

Input accepts compression 8/32946 (Deflate) or 5 (modern TIFF LZW), predictor 2,
both file byte orders and homogeneous explicit IFD layouts. Reconstruction adds
the previous pixel's matching component modulo 256 or 65,536, restarting at each
row. Channels remain on C and RGB components on S. Output uses little-endian
words, compression 8 or 5 and explicit tag 317 = 2.

The transform runs in the existing row/strip buffers without another allocation.
Encoding walks backward so subtraction reads original samples; decoding walks
forward so addition reads restored samples. Checkpoints bound long-row work.
Existing strip size checks, codec limits, read-free estimates, cancellation and
atomic publication apply. Estimates include the additional predictor tag.

Float32, uncompressed/PackBits predictor-2 pairings, predictor 3, unknown values
and mixed page predictors are refused. Floating prediction needs its own format
and compatibility evidence. Predictor-1 formats, enum values 0–15, default export
format and older record producers remain unchanged.

## API and editor

Six append-only C++ enum values 16–21 are available in this order:

| Codec | UInt16 | UInt8 | RGB8 |
| --- | --- | --- | --- |
| Deflate | `ScientificTiffU16DeflateHorizontalV1` | `ScientificTiffU8DeflateHorizontalV1` | `ScientificTiffRgb8DeflateHorizontalV1` |
| LZW | `ScientificTiffU16LzwHorizontalV1` | `ScientificTiffU8LzwHorizontalV1` | `ScientificTiffRgb8LzwHorizontalV1` |

Python exposes matching uppercase names:

```python
from imagejxx import scientific_io as io
result = io.read_image_series("input.tif")
request = io.ImageWriteRequest()
request.format = io.ImageFileFormat.SCIENTIFIC_TIFF_U16_DEFLATE_HORIZONTAL_V1
io.write_image_series("output.tif", result.dataset, result.metadata, request=request)
```

In either ImGui host, select Deflate or LZW, then **Prediction → Horizontal
(integer)**. Prediction defaults to None. Changing codec or prediction clears
the estimate and export consent. Switching to an incompatible codec resets
prediction to None; an explicit incompatible draft is rejected atomically.
Float32 does not offer this control.

These profiles produce version 8 workflow records with explicit compression,
`predictor: "horizontal"`, dtype, components and sample policy. Replay restores
an inert review form and requires fresh consent. Merely reviewing a record does
not modify a document or publish a file.

The historical Deflate/LZW `predictor.tif` fixtures now represent admitted
predictor-2 streams. Their old manifest classifications remain historical;
their test IDs are retained and check independently reconstructed pixels.
PackBits and uncompressed predictor refusals remain in place.

## Qualification

See `build/p08i-horizontal-tiff001/final-audit001.json` for exact gate receipts,
source and product identities, retained captures and cleanup evidence. The
independent corpus contains 84 valid and six refused files, covering endian
order, row/strip resets, wraparound, ramps, constants, one-pixel rows, RGB stride
and CZT ordering. Output strips are also decoded independently in Python.

Windows passes 79 native/desktop suites. Source and installed Python each pass
10,133 cases: all previous 9,910 plus 223. All 445 focused cases are retained in
the full inventories. A separate installed SDK consumer exercises all six
profiles. Twelve actual input scenarios run in four host contexts. Pinned
ImageJ verifies 216 distinct reads: 84 independent files, 84 native exports and
48 UI exports, with exact pixels, dimensions, calibration and timing.

One early editor run used the preceding desktop binary while its replacement
was still building. Its failed receipt is retained; the subsequent focused and
full runs use the current binary. Actual ImGui event streams and CPU readbacks
are checked; this does not claim OS-input or GPU equivalence. Linux product
qualification, floating predictors, legacy codec variants, tiles/BigTIFF,
broader metadata, high-bit/alpha and remaining ImageJ/Fiji/editor gaps stay open.

Float32 Predictor 3 now has separate explicit profiles; see
[floating TIFF prediction](SCIENTIFIC_FLOATING_TIFF.md). It is qualified against
libtiff; pinned ImageJ does not implement its decoder. The integer profiles
and their record producers remain unchanged.
