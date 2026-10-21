# Uncompressed tiled TIFF layouts baseline

This page records the P08m checkpoint. The subsequent
[Deflate tile contract](SCIENTIFIC_DEFLATE_TILES.md) adds compressed storage while
preserving these formats and records. The counts below describe P08m.

The native scientific IO service supports uncompressed tiled UInt8, UInt16,
RGB8 and Float32 datasets. Both Dear ImGui hosts expose the same storage choice;
the Python interface delegates processing and IO to C++.

| Samples | C++ format | Python enum suffix | Ordinal | Record version |
| --- | --- | --- | --- | --- |
| UInt16 scalar | `ScientificTiffU16TiledV1` | `U16_TILED_V1` | 24 | 10 |
| UInt8 scalar | `ScientificTiffU8TiledV1` | `U8_TILED_V1` | 25 | 11 |
| UInt8 RGB | `ScientificTiffRgb8TiledV1` | `RGB8_TILED_V1` | 26 | 11 |
| Float32 scalar | `ScientificTiffFloat32TiledV1` | `FLOAT32_TILED_V1` | 27 | 11 |

Python enum names begin with `SCIENTIFIC_TIFF_`. Existing format values, default
strip output and versions 1–10 retain their contracts. Version 10 remains strict
UInt16 uncompressed tiling. Version 11 accepts the three added layouts and
requires explicit tiled storage, dtype, components and sample policy. Replay
opens an inactive review form and requires fresh consent.

Admission requires classic TIFF, complete tile tags without strip tags,
homogeneous positive tile dimensions divisible by 16, exact padded byte counts,
and disjoint bounded payloads and metadata. The reader handles both byte orders
and copies logical row segments, including all RGB components. It ignores edge
padding regardless of its bit pattern. Existing file, sample, codec, metadata
and working-memory limits remain enforced before image allocation or callbacks.

Float32 reads use the existing explicit finite-only, quiet-NaN or full IEEE
sample policy. Signed zeros, subnormals, infinities and NaN payloads survive
without floating-point arithmetic during serialization. A NaN in unused tile
padding does not require a nonfinite sample policy. A logical NaN does. Flat
import retains the selected sample policy, preserves C-fast file page order and
reserves its temporary copy before decoding. Scientific axes and calibration
are discarded only after the existing flat-import loss confirmation.

Output uses little-endian 64×64 tiles with zero padding. Estimates include full
padded payloads, component/sample widths, tables and row buffers without reading
provider planes. Writes validate canonical source samples and retain exclusive
staging, cancellation and atomic replacement. Failed writes leave the destination
unchanged. Selecting tiles in the UI resets compression and prediction to their
supported values and invalidates estimates and consent. Incompatible direct
patches are refused atomically.

```python
from imagejxx import scientific_io as io

request = io.ImageWriteRequest()
request.format = io.ImageFileFormat.SCIENTIFIC_TIFF_RGB8_TILED_V1
io.write_image_series("output.tif", dataset, request=request)
```

The added corpus has 120 struct-built valid carriers, 120 independently written
libtiff 4.7.1 carriers and 51 malformed structures. It covers both byte orders,
six geometries, T/C/Z ordering, RGB components, three floating policies and
independently poisoned padding. LibTIFF and tifffile agree on all 240 valid
logical byte streams. The previous installed service refuses all 582 attempted
inspection/read operations on the new corpus before implementation.

Windows passes 87 native/desktop checks and 11,170 cases in each source and
installed Python run. Linux passes 238 native/desktop checks and 11,160 Python
cases in each source and installed run, with exactly ten Windows-only checks
exempt. All four inventories contain the same 11,170 IDs, preserving the previous
10,602 and adding 568. There are no failures or unexpected skips.

Both independent installed SDK consumers pass all 28 formats. Linux source and
installed C++ each pass 145 UI scenarios. The six added scenarios run in eight
platform/host/installation contexts: 48 observations with 144 exact XY/XZ/YZ CPU
readbacks. These are rendered ImGui input and CPU data observations; they do not
claim OS mouse or GPU output equivalence. Independent libtiff readers verify
1,034 native exports, 517 per platform. Both installed Linux hosts also pass an
extra Float32 tiled scenario from a non-repository working directory while their
staged source and development-cache paths are absent; the reusable development
cache is restored afterwards.

See `build/p08m-tiled-layouts001/qualification001.json` and
`docs/parity/reviews/tiled-layouts-review-001.json`. Test scratch is archived with
CRC and per-entry SHA-256 verification before removal. The native Linux source
and development cache are retained for verified incremental compilation.

Pinned ImageJ 1.54r rejects tiled TIFF; libtiff is the tile oracle. These profiles
do not claim tiled reopening compatibility with ImageJ. Compressed tiles,
prediction, BigTIFF, additional sample layouts and broader metadata remain open.
The [UInt16 baseline](SCIENTIFIC_TILED_TIFF.md) records the preceding packet.
Formal requirement/fixture/accepted-run registration and family/release
certification remain separate from these bounded implementation checks.
