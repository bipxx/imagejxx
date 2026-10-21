# Predicted tiled TIFF

The C++ IO service, Python API and both Dear ImGui hosts support horizontal
prediction for UInt16, UInt8 and RGB8 tiles, and floating prediction for Float32
tiles. Both Deflate (input tags 8/32946) and LZW are supported. Output uses tag 8
or 5, little-endian classic TIFF, 64×64 tiles and zero logical edge padding.
Eight appended formats occupy ordinals 40–47. Existing ordinals, strip defaults
and workflow versions 1–13 retain their contracts; version 14 is the new strict
predicted-tile producer. Versions 12/13 remain predictor-free.

Integer differences reset at each complete padded tile row and preserve component
stride and file byte order. Float32 transforms shuffle most-significant byte
planes and difference the full shuffled row without floating arithmetic. Decode
inverts full tile-width rows before copying logical samples; nonfinite padding
never enters sample admission. Preflight charges floating scratch at tile width,
including tiles wider than a one-pixel logical image. Existing bounded codecs,
stream patching, cancellation and atomic destination publication are reused.

Version 14 requires tiled storage, Deflate/LZW and an exact compatible predictor,
dtype, component count and sample policy. Missing/unknown/incompatible combinations
are refused. The shared form exposes prediction for tiled export and invalidates
estimates/consent after changes. Replay opens an inactive review form. The generic
TIFF probe now selects the actual floating control as well as horizontal prediction.
No dependency, Qt or non-commercial component was introduced.

The corpus has 504 independently encoded valid carriers, 504 libtiff-written
carriers and 344 malformed/unsupported controls. Two independent decoders agree
on all 1,008 valid logical streams and poisoned padding; the previous service
rejects 2,704 inspect/read attempts. Geometry includes singleton, exact, partial,
wide, tall, non-square and entropy/reset cases in both endian orders and all
floating policies. Export readers verify full padding and exact source words.

Windows passes 87 native/desktop checks and 16,678 Python cases per source/installed
run. Linux passes 238 native/desktop checks and 16,668 Python cases per run, with
exactly ten Windows-only exemptions. All four inventories retain the previous
14,020 IDs and add 2,658. Both SDK consumers pass 48 formats. Linux C++ passes
217 physical UI scenarios per build. The 36 added scenarios produce 288 UI
observations and 864 exact XYZ CPU readbacks across platform/host/install contexts.
These exercise rendered ImGui controls and CPU source data, without an OS mouse
or GPU output equivalence claim. Independent libtiff verifies 4,408 native exports.
Both installed Linux hosts also run while source/build-cache paths are absent.

The initial focused run found an old negative case now requesting supported
integer tiled prediction. Both legacy cases now test an incompatible floating
predictor on UInt16, preserving all four case IDs and atomic-refusal assertions.
The focused negative gate and all final inventories pass. The correction changes
tests only; the byte-verified native source snapshot records that update.

Scratch is CRC/hash verified before removal, purposeful native build caches remain
available, and prior qualified install prefixes are preserved. The existing fixed
benchmark remains the performance baseline; this packet makes no speed claim.
See `build/p08p-predicted-tiles001/qualification001.json` and
`docs/parity/reviews/predicted-tile-review-001.json`.

All 48 format variants remain partial. Formal reciprocal fixture/accepted-run
registration, family/release certification, BigTIFF, broader layouts/metadata and
remaining ImageJ/Fiji editor requirements remain open. This is no ImageJ tiled
reopening claim. All99 remains 42 in progress / 57 open / 0 complete.
