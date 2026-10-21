# Deflate tiled TIFF

The C++ IO core and shared Dear ImGui form now implement predictor-free Deflate
tiles for UInt16, UInt8, RGB8 and Float32. Python exposes the same native service.
The new C++ formats are `ScientificTiffU16DeflateTiledV1`,
`ScientificTiffU8DeflateTiledV1`, `ScientificTiffRgb8DeflateTiledV1` and
`ScientificTiffFloat32DeflateTiledV1`, with ordinals 28–31. Python names use the
`SCIENTIFIC_TIFF_` prefix and `*_DEFLATE_TILED_V1` suffix.

Input accepts TIFF compression tags 8 and 32946. Output uses tag 8, little-endian
64×64 tiles and zero padding. Each admitted tile has one complete zlib stream.
The decoder refuses truncated, corrupt, trailing, concatenated, short or
oversized expansion; buffers and codec allocations stay within preflight limits.
Only logical samples undergo the selected floating policy. Unused padding is
ignored. The uncompressed direct-row path remains available.

Estimates charge padded raw tiles, encoded bounds, tables, provider storage and
working buffers without reading source planes. Streaming output patches admitted
offset/count fields before verification and retains exclusive staging and atomic
replacement. Cancellation and callback failures preserve the destination.

Version 12 workflow records require tiled storage and Deflate, plus dtype,
components and sample policy. Versions 10 and 11 retain their uncompressed
contracts; earlier producers and strip defaults remain unchanged. UI storage and
compression changes invalidate estimates and consent. Replay remains an inactive
review form. Flat import preserves page order and floating policy after explicit
loss consent. No dependency version changed and no non-commercial component or
Qt dependency was added.

The corpus has 288 valid struct/zlib files (both compression tags), 144
libtiff-written files and 116 malformed/unsupported controls. LibTIFF and
tifffile agree on all 432 valid logical byte streams. The previous installed
service refuses all 1,096 attempted inspection/read operations on this corpus.

Windows passes all 87 native/desktop checks and 12,255 cases in each source and
installed Python run. Linux passes all 238 native/desktop checks and 12,245 Python
cases in each source and installed run, with exactly ten Windows-only exemptions.
All four inventories contain the same 12,255 IDs, preserving 11,170 and adding
1,085. There are no failures or unexpected skips. Both independent SDK consumers
pass 32 formats; Linux source and installed C++ each pass 157 actual UI scenarios.

The twelve added UI scenarios run in eight platform/host/installation contexts:
96 observations with 288 exact XYZ CPU readbacks. They cover every sample layout,
quiet-NaN and IEEE failure/recovery, export, processing and inactive replay.
These observations exercise rendered ImGui inputs and CPU data, without an OS
mouse or GPU output equivalence claim. Independent libtiff readers verify 1,856
native exports, 928 on each platform. Both installed Linux hosts also pass a
Float32 Deflate tile scenario from a non-repository working directory while the
staged source/cache paths are absent, then those reusable paths are restored.

The fixed benchmark records 36 exact round trips across twelve profiles, using
three repeats in independent processes. Its periodic finite corpus has shape
1×2×8×257×263×S. It records write/read CPU and wall time, encoded bytes, charged
working bytes and peak process RSS. RSS includes Python and dataset storage;
these measurements set a baseline without a performance acceptance threshold or
a universal speed claim. See `build/p08n-deflate-tiles001/benchmark001/evidence001.json`.

Compilation/linking passes in `windows-runtime003`; its first native test run
hit two stale output guards. `native002` runs all 87 checks successfully with
fresh destinations. Earlier build/type, DLL-locking, fixture-phase and archive
wrapper failures remain in the evidence. Linux reuses a verified native source
snapshot and cache, preserving unchanged file mtimes. The previous P08m installed
products remain unchanged. Test scratch is archived and hash/CRC verified before
removal, while the development cache is retained for incremental builds.

See `build/p08n-deflate-tiles001/qualification001.json` and
`docs/parity/reviews/deflate-tile-review-001.json`. The ledger lists all 32
implemented variants with evidence; family entries remain partial. Formal
fixture/run registration and release certification remain open. Pinned ImageJ
rejects tiled storage; this is not an ImageJ tiled-reopening claim. Other tile
codecs, prediction, BigTIFF, sample layouts and broader metadata remain required.
