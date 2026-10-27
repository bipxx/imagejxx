# ImageJ projection channel/density policy observations

This immutable252-case headless corpus uses the existing approved ImageJ1.54r
classes, commit62db90a269cccc1ac79f658e4602043214ed055b. Two fresh captures
match raw/gzip bytes. No target code or GUI executed. Only project-owned
observers were compiled;468 ImageJ classes,581 JDK files and352 extracted Java
sources were verified before/after each run without runtime/cache replacement.

96 channel-only cases cover U8/U16/F32/RGB, six channels, full/even bounds and
groups2/3, across six methods. ImageJ projects the ordinary C-only stack across
C and can retain multiple output C groups. This includes RGB C-only stacks;
the earlier24 RGB-C2 hyperstack rejections remain true for their original shapes.
156 integer density cases cover linear/poly2/zero-clip/custom signed/custom range/
custom IEEE tables for U8/U16, plus U8 optical density. Each uses full/even bounds
and six methods. Raw planes use little-endian typed words (RGB is Java ARGB32).
All source states and used calibration-table words are unchanged after the APIs.

Only calibrated medians read lookup values. Other methods use raw codes; the
custom signed-valued table does not imply signed16 sum correction. Calibrated
U8 median is rounded/clamped to Byte, U16 median returns Float32. Integer outputs
retain density intent; float outputs clear density while retaining value units.

Table words are observed only at source codes actually read. The C++ fixture
fills unused cells with zero because they cannot affect these numeric results.
This is not an oracle for calibration-function generation, full table metadata
publication or calibrated measurements of output codes. Those require the full
source intensity record. Separate analytic tests cover odd table medians,
NaN/infinity conversion, signed16/table overlap and caller mutation after capture.

The grouped calibration fields are separately reproduced source-level steps;
the grouped GUI was not executed. General explicit multi-axis C reduction is
an owned extension, distinct from the observed C-only forms. LUT/overlay/new
window publication and UI interaction remain separate acceptance requirements.
The older corpora and scalar/RGB expectations are unchanged.
