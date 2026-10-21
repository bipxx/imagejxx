# Float32 Deflate file controls

The shared ImGui file workflow is qualified for finite, quiet-NaN and full-IEEE
Float32 Deflate in source and installed C++/Python hosts. The core IO service,
processing implementation and public SDK remain unchanged from the prior
[Deflate profile](SCIENTIFIC_DEFLATE_TIFF.md).

| Scenario | Input | Verified behavior |
| --- | --- | --- |
| TF01 | finite, little-endian | Switch IEEE/finite policy, view, preview, process and export |
| TF02 | quiet NaNs, big-endian | Explicit quiet policy and lossless filtered export |
| TF03 | full IEEE, little-endian | Explicit IEEE admission, exact signaling/infinity/zero words |
| TF04 | full IEEE, big-endian | Finite-policy failure leaves no document; renewed consent recovers |
| TF05 | quiet NaNs, little-endian | Cancel reviewed import; no document or output |
| TF06 | full IEEE, little-endian | Replaced input identity refuses publication |

Successful cases open the linked XY/XZ/YZ views through actual controls and
verify exact source words in each plane, then restore ordinary viewing. The
Remove NaNs dialog previews without changing the source, applies radius 1,
and preserves expected display and undo/redo semantics. Export selects Deflate
through its actual combo control, requires fresh loss consent, and retains the
source sample policy in a version 5 record. Cancelled exports produce no file;
replay restores the codec choice with consent cleared and leaves source/output
unchanged. The identity replacement in TF06 is explicitly declared fixture
preparation and receives no physical-input credit.

The original struct/zlib generator emits six carriers in
`tests/fixtures/deflate-float32-tiff-imagej154r`, preserving the source words and
filter oracle from the previously pinned ImageJ corpus. Pinned ImageJ 1.54r
Opener reads all six carriers, and its actual Remove NaNs plugin reproduces the
expected filtered words. ImageJ also reopens all sixteen successful GUI exports
with exact words, C/Z/T, spacing and frame timing.

The final audit is `build/p08f-deflate-float-ui001/final-audit001.json`.
It records 76 passing Windows native/desktop suites, 166 focused Python cases,
and identical 9,634-case source/installed Python inventories without skips,
retaining every prior 9,622 case ID and adding twelve. Six scenarios in four
host contexts yield 24 physical observations. All 264 frozen core/IO/public
inputs and 104 installed SDK files match the prior qualified checkpoint; the
existing public SDK consumer evidence is reused on that exact identity.
Eighteen scratch targets were archived/removed, freeing 1,411,749,759 net bytes.
Raw observations, upstream logs and three installed product prefixes remain.

The observations cover actual ImGui input events and CPU cache/upload-input
values. They do not claim OS-input or GPU equivalence. Linux TIFF/product
qualification, further codecs/predictors, tiled/BigTIFF, broader metadata,
high-bit/alpha and remaining ImageJ/Fiji/editor/release requirements remain open.
All99 remains 42 in progress / 57 open / 0 complete.
