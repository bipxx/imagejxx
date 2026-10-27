# Supplemental ImageJ RGB projection observations

This immutable supplement contains124 headless `ZProjector.run` observations
from the existing approved ImageJ1.54r reference classes, commit
`62db90a269cccc1ac79f658e4602043214ed055b`. Two fresh observer runs produced the
same1,402,018 raw bytes and290,268 gzip bytes. `contract.json` pins the exact
transport, raw data, observer/capture sources and reference runtime manifests.
No imagejxx code or GUI executed during observation. No Fiji plugins loaded.

The source is project-owned synthetic RGB data. Five profiles (zero, constant,
spatially stationary, varied, rounding stress) use full seven-slice and even
four-slice ranges, all six methods and both clip-property values:120 cases.
Four larger257-pixel/32-plane sum/SD cases supplement those. Raw planes use
little-endian Java ARGB32 words with opaque alpha, representing RGB24 pixels.
Every source state is unchanged before/after the observed API call.

Only the project-owned `ObserveRGBProjection` and unchanged shared observer were
compiled into fresh scratch. All468 reviewed ImageJ classes,581 reviewed JDK
files and352 extracted Java sources were verified before and after each run.
The cache/runtime were reused. Raw captures and observer class scratch were
retired after verified lossless compression and terminal process receipts. The
two duplicate temporary gzip transports were retired after public-byte checks;
use this public transport. No compiler cache, JDK or dependency checkout is
archived as evidence.

The result confirms Float32 shared-maximum scaling for RGB sum/SD, followed by
independent per-channel range conversion during RGB merge. Sum clipping bypasses
those scales and uses Float32 rounding/clamping. Constant ranges and zero data
map to black; the clip property has no effect on the other five methods. RGB
mean/min/max/median return typed RGB bytes. No inference of GUI interaction,
LUT/overlay publication, saved ROI manager behavior or complete parity follows.

`generate_imagej_rgb_projection_fixture.py` verifies source/data/contract hashes
and transcribes packed expectations, without running the target. It combines
this supplement with the original immutable corpus's72 RGB successes and24
explicit hyperstack RGB-C2 rejections:196 successes and24 negative rows. The
native test exercises two tile layouts, exact packed words, source preservation,
read counts, provider lifetimes/caps and merge cancellation. Offline checks need
no JVM. The original324-case corpus, scalar fixtures and historical fingerprints
are unchanged.

The new public RGB profile admits UInt8 S3/C1. Full ImagePlus compatibility still
requires legacy/simple-composite C-only dispatch, grouped GUI hyperstack routing,
metadata and UI policy. Those paths are not established by this C1 numerical
matrix or by the24 observed C2 hyperstack rejections. Scalar general density
calibration and reviewed virtual IEEE admission also remain integration gaps.
