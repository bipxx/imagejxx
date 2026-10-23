# Native selection enlargement service

`SelectionEditor::enlarged` and Python `SelectionEditor.enlarged` now provide the
geometry service for scientific Enlarge/Shrink. Image processing remains in C++.
This checkpoint does not enable the scientific desktop dialogs yet.

```python
geometry = imagejxx.SelectionEditor(selection, (height, width))
limits = imagejxx.SelectionEditLimits()
estimate = geometry.estimate_enlargement(2.0, limits)
larger = geometry.enlarged(2.0, limits)
smaller = geometry.enlarged(-2.0, limits)
```

Distances are pixels, rounded with ImageJ's half-up integer rule. Rectangles and
ellipses change their integer processing bounds directly. Rounded rectangles
follow ImageJ's rectangle branch, which removes rounded corners. Irregular
areas use the reviewed ImageJ EDM candidate propagation, including its float
rounding and tie order. Distances below absolute 256 use rounded, saturated byte
distances; larger distances use float distances. A shrink that erases the entire
ROI returns the original geometry. Empty native masks and degenerate zero-cell polygons stay unchanged. Lines, points,
angles, nonfinite distances and distances outside the supported integer domain
are refused. Zero distance preserves exact input geometry and attributes.

The input editor stays immutable. Results preserve width and opaque style data;
geometry-specific rounded-corner metadata is removed when the result becomes an
ordinary rectangle or mask. Analytic results retain off-image coordinates.
Irregular results are packed masks clipped to the editor's XY grid. The padded
calculation includes the input geometry outside that grid, but the result cannot
retain off-grid irregular geometry for a later offset. That representation gap
remains required before full repeated-operation ImageJ parity.

The borrowed estimator checks geometry, padded pixels and working memory before
image allocation or user callbacks. It charges geometry/result staging, raster
and rounded-path scratch, float EDM, row buffers and packed output. Caller-owned
editor/document/history, opaque callback target internals, allocator bookkeeping
and later record serialization
are outside this geometry-service bound. The configured pixel cap applies to
both the editor grid and padded workspace. Padded dimensions are at most 32767,
keeping ImageJ's signed squared-distance arithmetic in its valid domain.

`enlarged` freezes limits/control before callbacks, reports coarse progress
`(0,1)` and `(1,1)`, and checks cancellation during rasterization, both EDM passes
and result packing. Callback exceptions, cancellation and budget refusal return
no result and do not mutate the source. The Python methods release the GIL and
copy limits before doing so. No dataset samples are needed by this service.

The independent fixture contains 144 actual ImageJ 1.54r observations over nine
source geometries and sixteen distances. It covers fractional/negative rounding,
the byte/float boundary, holes, disconnected areas, deterministic irregular
masks and shrink collapse. Expected pixels never come from imagejxx. The fixture
also captures floating bounds for analytic results. Adding those bounds retained
every original pixel observation unchanged. A further 37 checks cover native
rotated shapes, styles, empty masks, invalid inputs and controlled admission.
The installed core-only SDK example checks a literal 21-cell radius-two result,
analytic rounding, exact/below-estimate budgets, cancellation inside the EDM and
metadata publication over a throwing virtual source.

Reproduce the reference with
`tests/reference/extract_selection_enlargement_fixture.py --imagej-root ROOT
--jdk JDK --output-dir FRESH --fixture NEW.json`, where ROOT contains retained
`sources/` and compiled `classes/` from ImageJ 1.54r. The helper invokes actual
`RoiEnlarger` methods and implements no enlargement algorithm. Its receipt pins
the driver, selected source/classes, Java tools and upstream public-domain notice.

Next, the scientific dialogs must consume this service with pixel/calibrated
units, reusable defaults, captured owner/TCZ/ROI, admitted command records and
strict replay/rebind. Physical dialog input, Save/Open, cancellation and history
must be qualified in both launch modes. These requirements, off-grid irregular
geometry, broader source/reference/resource/platform cases and all original
feature families remain open. See [the scoped receipt](reviews/p12g-selection-enlarge001.json).
