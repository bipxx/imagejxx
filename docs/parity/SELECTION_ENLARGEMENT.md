# Native selection enlargement service

`SelectionEditor::enlarged` and Python `SelectionEditor.enlarged` now provide the
geometry service for scientific Enlarge/Shrink. Image processing remains in C++.
Edit → Selection → Expand/Shrink now opens the shared native dialog in both
desktop frontends. The dialog captures its document, selection, calibration and
C/Z/T position before editing parameters.

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

## Recorded commands and desktop controls

The public C++ command interface is in `imagejxx/selection_enlargement.hpp`.
`SelectionEnlargementRequest` contains a signed distance and Pixels/Calibrated
units. Calibrated distance is divided by the captured X spacing, matching
ImageJ's rule. The dialog displays the source unit and warns when X and Y spacing
differ. Changing units converts a valid current distance. Incomplete/invalid
text stays visible and disables Apply; Cancel publishes nothing.

The persistent Selection preferences `selection_enlargement_distance` (15 pixels)
and `selection_enlargement_units` (calibrated) supply new-dialog defaults. Shrink
negates the magnitude. These defaults are configured independently: editing an
individual command does not overwrite them or couple preference-file writes to
selection publication. This differs from ImageJ's remembered last-used distance.

Python editor calls use the same compiled service:

```python
editor.action("enlarge_selection", distance=1.0, units="calibrated")
editor.action("grow_selection", radius=2.0)    # pixels by default
editor.action("shrink_selection", radius=2.0)
```

The grow/shrink aliases now use this service for ordinary and scientific
documents. Missing/non-area selections and invalid fields are refused. The old
square-mask alias implementation is no longer used by these actions.

`resolve_selection_enlargement_command` freezes the request and produces a
complete `imagejxx.selection-enlargement-command` version-1 record. It contains
the original area, result, source identity/descriptor/revisions, TCZ position,
requested distance/units, X spacing and unit label. Validation recomputes the
original equation. Destination resource failures cannot bypass original-equation
validation or silently substitute a rebind. Commands, records and preparation have admitted metadata/geometry
working estimates; preparation can tighten the captured cap.

`selection_enlargement_command_from_record` replays the recorded geometry, even
if the live selection has since changed. Source owner, pixels/metadata,
descriptor, TCZ and X calibration must still match. Applying to live destination
geometry requires `rebind_selection_enlargement_command` and an explicit request.
Preparation captures fresh publication guards, including selection and full
Display revisions. A subsequent camera or navigation change therefore refuses
publication. Preparation exposes no pixel edit or pixel Undo/Redo step.

Changed desktop publications append exactly one complete record when recording
is enabled. Exact no-ops append nothing. The Workflow panel provides dedicated
captured Replay and explicit Rebind controls. Records round-trip through native
sessions; reopened owners require explicit rebind. Generic workflow/batch-program
integration remains a separate requirement.

The core command bound includes retained display/record/geometry, enlargement
scratch and preparation's document metadata/annotations. Existing pixels/history,
caller-owned arguments, Python/desktop argument conversion, GUI retained state,
opaque callbacks, allocator overhead and host publication copies are outside it.
Resolve/validate/replay/rebind/prepare report one coarse progress pair, with
cancellable kernel checkpoints. Finish-callback failure returns no result and
publishes nothing. The desktop Apply is synchronous; managed execution and
mid-computation GUI cancellation for large selections remain required.

New qualification adds both units over all 144 reference rows, complete JSON
records, IEEE-word/session preservation, stale/refusal/rebind/resource/finish
callback cases, preferences reopen and ordinary aliases. Actual C++ ImGui input
checks scalar/RGB/IEEE dialogs, unit conversion, warnings, invalid drafts, literal
21-cell output, recording, Cancel and stale navigation. Python Editor API tests
exercise the compiled owner separately; these are not new OS/OpenGL or paired
Python-launch gesture acceptance. Off-grid irregular retention, remaining
reference/presentation/platform/resource cases and all original families remain
open. See [the geometry receipt](reviews/p12g-selection-enlarge001.json) and
[command/editor receipt](reviews/p12h-enlargement-command001.json).
