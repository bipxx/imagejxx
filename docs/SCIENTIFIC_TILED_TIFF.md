# Scalar UInt16 tiled TIFF baseline

This page records the qualified P08l baseline. The subsequent
[uncompressed tiled layouts](SCIENTIFIC_TILED_LAYOUTS.md) packet adds UInt8, RGB8
and Float32 while preserving this contract. The counts below describe P08l.

The C++ reader/writer, Python format API and shared Dear ImGui controls are
qualified on Windows x64 and Ubuntu 24.04 x86_64. This closes the first bounded
uncompressed UInt16 tiled profile. Other tiled sample types/codecs and BigTIFF
remain separate gaps.

The append-only format is ordinal 24, `ScientificTiffU16TiledV1` in C++ and
`SCIENTIFIC_TIFF_U16_TILED_V1` in Python. Its format identity is
`F.G080.tiff-u16-uncompressed-tiled-ifd-czt-v1`. Existing 24 format ordinals,
defaults and record producers remain unchanged.

The profile admits classic TIFF, scalar unsigned 16-bit samples, uncompressed
tiles and the existing calibrated C-fast IFD mapping. Tile dimensions must be
positive multiples of 16 and homogeneous across pages. A complete tile tag set
is required, without strip tags. Counts, byte extents, payload/metadata overlap,
dimensions and all existing sample/file/metadata limits are validated before
pixel reads or user callbacks. Missing, mixed, oversized, overlapping and
truncated structures remain refused. Compressed tiles, other sample layouts,
3D tiles and BigTIFF require separate profiles.

The reader copies only logical row segments from each admitted tile. It handles
either byte order and ignores padded edge samples. Output uses fixed 64 by 64
tiles in little-endian order, with deterministic zero padding. Output estimates
include the full padded payload, offset/count tables and working storage without
reading provider planes. Streaming writes retain the existing exclusive staging
and atomic replacement boundary. Cancellation and callback exceptions preserve
an existing destination.

The export form offers explicit strip/tile storage for scalar UInt16 datasets.
Selecting tiles resets compression and prediction to their supported choices
and invalidates estimates and consent. Direct incompatible option patches are
refused atomically. Tiled import/export records use version 10 with explicit
`storage: tiled`, dtype, component count and finite-only sample policy. Replay
restores an inert review form and requires new consent. Older versions cannot
carry the new storage field. Ordinary export defaults remain strips.

Explicit flat import of an inspected tiled file uses the bounded C++ service.
It preserves the file's T/Z/C page order, discards scientific axis/calibration
meaning only after the existing loss confirmation, and charges the temporary
copy before decoding. The held file identity is revalidated before publication.
Other legacy flat imports continue through their existing loader.

The independent corpus contains 24 original struct-built carriers, 24 carriers
written by the reviewed libtiff 4.7.1 runtime, and 17 malformed structures. It
covers both byte orders, singleton and non-square images, multiple T/C/Z values,
partial edge tiles, and zero/nonzero padding. LibTIFF verifies exact logical
words and full tile padding for both valid corpora; tifffile independently
decodes all 48. The libtiff-written manifest omits inherited physical offsets
from the original corpus; its TIFF bytes and expected logical samples are
unchanged by that manifest correction.

Actual pinned ImageJ 1.54r opening rejects all 49 complete tiled controls,
including the historical `tiled-complete.tif` fixture. A direct decoder probe
records its tiled-format diagnostic, while Opener reports its generic unsupported
format error. LibTIFF is the tiled IO oracle. This profile does not claim that
ImageJ 1.54r can reopen tiled output. The tile dimension and buffer rules follow
[the libtiff tile contract](https://libtiff.gitlab.io/libtiff/functions/TIFFtile.html).

Windows passes 87 native/desktop regression checks. Linux passes all 238 native
and desktop checks. Source and installed Windows Python each pass 10,602 cases;
Linux each passes 10,592 with exactly ten Windows-only DLL checks exempt. Those
ten pass in both Windows runs. The case IDs match across all four inventories,
retaining all previous 10,436 and adding 166. There are no unexpected skips.

Linux source and installed C++ each run 139 actual UI scenarios. Two new tiled
scenarios run in four host/installation contexts on both platforms: 16 actual
observations with 48 exact XY/XZ/YZ CPU readbacks. Separate independent libtiff
readers verify 210 native exports, including IO round trips, all eight desktop
contexts and both installed SDK consumers. Each independent installed SDK
consumer passes all 25 format round trips; the Linux core-only consumer requires
no TIFF, Python or graphics discovery.

Linux compilation uses a verified native filesystem snapshot of 2,216 source,
example, test, notice and documentation files. This avoids repeated header reads
through WSL's Windows filesystem bridge. The runtime/test/notice bytes match the
authoritative worktree freeze. Subsequent Markdown updates are identified
separately. Selected dependency versions and artifacts are unchanged.

Failed attempts are retained separately: the historical native tiled-refusal
expectation, an intentionally stopped mounted-source build, an incomplete source
snapshot, a Windows Python deadline, and an oracle package-target mismatch.
Authoritative successful gates are identified in
`build/p08l-tiled-tiff001/final-audit001.json`. The Windows Python rerun uses a
bounded 360-second deadline after the original 240-second run timed out; its
assertions and case inventory are unchanged.

Verified scratch archives preserve failed attempts, completed captures and build
assets before their originals are removed. Cleanup archived/removed 54 owned
targets, freeing 9,486,111,776 net bytes. Both installed Linux hosts subsequently
passed another actual tiled scenario from a non-repository working directory,
with their staged source and development cache absent. The installed products,
independent validation executables and those final isolation observations remain
available. All selected static libraries, notices and product file identities
are recorded by the delivery audit. No dependency version changed.

Next close the remaining uncompressed tiled sample layouts together: UInt8,
UInt8 RGB and Float32 with finite, quiet-NaN and full IEEE policies. Then qualify
tiled compression/prediction variants and BigTIFF. The next packet is recorded in
`build/p08l-tiled-tiff001/next-packet-plan001.json`. All99 remains
42 in progress / 57 open / 0 complete.
