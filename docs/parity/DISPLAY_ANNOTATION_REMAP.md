# Affine display annotations and Z-subset alignment

Text and embedded images retain payload-local coordinates, exact styling and
source bytes. Each planar display row has a row-major 2×3 affine transform
`[m00,m01,offset_u,m10,m11,offset_v]` from payload-local to plane-local image-cell
boundaries. The identity is the default. Coefficients are finite and bounded by
±1e9; a zero determinant is rejected. C++ and Python expose complete transforms,
forward point mapping and inverse mapping. Python returns detached coefficients.

Display sets use V2 exactly when at least one row has a nonidentity transform.
Every V2 row carries six coefficients, including identity rows in a mixed set.
V1 remains closed to affine fields. Store/update/delete equations use V2 if any
before/request/after set is V2; originals remain complete. Nested versions,
strict fields and the full equation are checked before replay/rebind. Native
sessions transport the typed extension without altering payloads or transforms.

A Z subset with first source index `f` and positive step `s` maps plane-local
boundaries by `u' = u`, `v' = v/s + 0.5 - (f+0.5)/s`. XZ/YZ display rows compose
this mapping with their retained transform. Text bounds, angle, font size/style,
justification/baseline and colors remain exact; embedded RGB/RGBA bytes, alpha,
opacity and zero transparency remain exact. Rotated text therefore undergoes
the correct anisotropic affine change instead of changing only its bounds or
font size. XY rows retain their complete transform while fixed Z positions are
remapped or removed. Off-volume content remains represented for clipped display.
Identity subsets preserve exact storage/history. Other annotation and dataset
subset policies remain in the shared remapper.

Shared axis-command source records also transport the admitted grayscale IEEE
Float32 policy. Display annotation storage and subset replay preserve infinities,
NaN payload words and signed zero without reading or rewriting source samples.

The ImGui renderer transforms actual glyph/background vertices and image quads,
retaining image texture data. The property panel shows the retained transform,
offers numeric coefficient edits and explicit reset to identity, and continues
to edit payload-local text/image properties. Standard ImageJ display-ROI export
refuses nonidentity transforms until explicit affine flattening/projection is
implemented; it never silently drops a transform.

The actual-input subset recipe covers apply, undo/redo, explicit workflow rebind,
new-document publication, numeric transform edit/restore and Save/Open. It
records real glyph/image vertex positions, UVs and colors before/after mapping.
The independent reader converts observed canvas coordinates to plane coordinates
and checks the Z mapping. A hidden native OpenGL screenshot confirms visible
embedded image output. The unchanged legacy display UI/GL and ImageJ display-ROI
tests are included alongside the previous regional/creation/workflow matrix.

Windows/Linux qualification passed for this scope; see the
[scoped receipt](reviews/p12u-display-remap001.json). Continuous composite paths, other axis/affine
operation policies, explicit transformed ImageJ export, full font/layout
compatibility and the other original open/in-progress requirements remain work.
