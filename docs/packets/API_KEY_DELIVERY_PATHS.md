# Shortest delivery paths by API and setting key

## Shared histogram/profile plot workflow — 2026-10-28

Legacy histogram/profile plots now reuse view/axes/ticks, applicable style,
snapshots and PNG/CSV export. Scientific Histogram adds Interactive plot from
its last immutable result, including after source closure. Exact bin/count CSV
is reused; opening plots does not re-read source pixels. Reopening one result
shares one payload lease via a weak hint, with independent per-window controls.
Generic kinds/captions carry no invented calibration model. Profiles preserve
existing sampled values/distances/junction behavior and prepare grayscale once.
Legacy histogram retains x,y CSV/bin capacity; scientific CSV retains exact
bin/lower/upper/count. Generic diagnostics avoid duplicate calibration vectors.

Windows expanded915 collected:914 passed, one explicitly gated hidden-OpenGL
settings check skipped. Original783 focused cases remain passing. Five C++ UI
cases pass; new generic case118/160 through both hosts covers physical plot
controls/exports after labeled producer/closure fixtures. Prior cases remain
186/192 and606/608. Linux final math and current profile/histogram PNG consumers
pass against existing archives/SDK, not Linux UI/full SDK. C++/Python PNGs are
byte-identical and final bytes match visually inspected artifacts.
[Scoped evidence](../parity/reviews/G034-generic-plot-progress001.json).

All owned scratch/previews retired; cleanup recovered4,991,833B. Necessary
failed-reader/generated-consumer diagnostics retained. Superseded full captures
retired with identities; full contents unavailable. Known roots8,486,548,659B,
two denied caches keep full8GiB unverified. Same caches/runtime/SDK, commercial
review passes, reference fingerprints unchanged. No speed/full-family promotion:
47 in progress/52 open/0 complete. Next: exact bin/sample hover and captured
target/plane/ROI captions, then calibration settings/import/global/replay and
original-reference/downstream/platform gates. Plot persistence and OS windows
remain open; do not interpret generic viewer reuse as complete ImageJ parity.

Generic APIs: keep_plot_snapshot/close_plot_snapshot retain logical-ID
semantics. show/set_plot_limits, zoom_plot_rectangle, show/set_plot_style,
show_plot_image_export/export_plot_image and export_plot_csv accept optional
plot_id; live paths keep active source/All-revision guards. show_histogram_plot
accepts no arguments and opens the last scientific result as a snapshot. Existing
intensity plot action names remain supported. Profile style: line_width and
curve_color; histogram: curve_color. Legacy generic root=plot, calibration
root=fit_plot; raw histogram counts/edges remain exact in diagnostic/CSV output.

Shortest next path: derive exact hovered bin from retained edges/counts and
nearest profile sample from captured series, using shared view fractions and no
provider reads. Prepare compact target/plane/ROI captions at producer publication,
account their bytes with payload, preserve them after source close, and extend
the118/160 generic case without growing the186-frame calibration snapshot case.
Then audit G034 settings/import/global/replay/downstream keys against original
requirements; source/display/memory compatibility and original reference gates
remain distinct from this focused viewer result.

## Bounded automatic major/minor plot ticks — 2026-10-28

Axes now offers opt-in nice_ticks/minor_ticks Boolean controls, also accepted by
set_intensity_plot_limits. Omitted fields preserve state; defaults remain false.
The shared UI/PNG generator handles linear/log/reversed/constant/extreme ranges
with fixed128-slot storage, at most12 major/96 minor ticks and bounded loops.
Nice majors drive grids; minors are unlabeled and filtered for pixel spacing.
Measured-label overlap suppression and adaptive precision keep nearby labels
distinct. View limits, samples, source/history, style and CSV remain unchanged.

Windows installed783 focused checks pass, zero failures/skips, plus native checks.
Linux math and current PNG consumers pass against existing pinned archives/SDK;
no Linux UI/full SDK promotion. Both UI hosts physically toggle after source
closure and export. Artifact readers find16 minor stems per C++/Python/Linux PNG;
C++/Python outputs are byte-identical and output was visually reviewed. Dedicated
case186/192; calibration606/608. [Scoped evidence](../parity/reviews/G034-calibration-plot-ticks-progress001.json).

Two attempts exposed old initial/reopened dialog assertions requiring exactly
eight fields. Both original expected dictionaries are retained, with separate
checks for the two new false defaults; failed reader preimages/diagnostics kept.
All owned scratch retired; cleanup recovered2,505,476B. Superseded full failed
captures retired with hashes/reduced schema diagnostics; full contents unavailable.
Known roots8,480,310,796B; two denied caches keep full8GiB unverified. Same
caches/runtime/SDK, commercial review passes, original fingerprints unchanged.
No ImageJ tick-convention/performance/full-family acceptance:47 in progress/52
open/0 complete. Next: generic histogram/profile renderer reuse in a new bounded
workflow case; do not extend the186-frame snapshot trace for that larger workflow.

Generic plot delivery path: first inventory the existing histogram/profile
producers and captured units/ROI/T-C-Z semantics. Adapt their prepared numeric
series into one private immutable plot DTO with explicit kind/axis captions,
without inventing calibration provenance or changing producer numerics. Reuse
the existing view/style/ticks/drawing/export and memory admission, preserve
legacy command names, and resolve snapshots/callbacks through logical IDs.
Verify units/bin/sample values against current oracles before adding source-close
lifetime, independent views and PNG/CSV checks in a fresh bounded UI case.

## Shared plot drawing and source-independent PNG export — 2026-10-28

Export PNG now uses the same curve/point/grid/tick/model drawing function as the
UI, including viewport/log/reversed transforms, colors/sizes and annotation
position/wrapping/clipping. Standalone ImGui drawing/font atlas is rasterized on
CPU and encoded with existing stb PNG, then existing atomic replacement writes
the file. No GPU readback, source-provider access or workspace controls. Source,
function and unit captions are included. White background and built-in13px font
are explicit output choices; unsupported glyphs fall back and captions are
bounded/clipped. Other formats/transparency/custom font/DPI remain open.

PNG dimensions dialog precedes the save browser. Deferred save captures immutable
payload plus view/style copies. show_intensity_plot_image_export and
export_intensity_plot_image accept optional plot_id; direct export requires PNG
path and optional width/height, default1024x640. Strict integer bounds256..4096 X,
192..4096 Y and4Mpixels maximum. Managed working memory is reserved before
font/geometry/output allocation or file writes, with checked count/geometry/
encoder bounds and release on failure. This is estimated managed memory, not RSS.

Windows installed778 focused checks pass, zero failures/skips, plus native
consumers. Both UI workflows physically save after source closure. Independent
PNG CRC/filter/pixel readers verify dimensions, green curves/reversed axes,
model placement/pixels, source-close identity, atomic refusal and memory release.
C++/Python PNGs byte-identical. Caption/model and curve outputs visually checked.
Dedicated case178/192 frames; calibration stays606/608. Current Linux shared
drawing/export code compiles/executes against existing pinned archives/SDK and
its PNG passes an independent green-pixel check. No Linux UI/full SDK or OS/GPU
promotion. [Scoped evidence](../parity/reviews/G034-calibration-plot-image-export-progress001.json).

First compile exposed an extracted internal ImFormatString dependency; standard
snprintf fixes the interface. Original-SHA-verified preimage/diagnostics retained.
All owned terminal scratch/previews retired; lossless retention recovered
859,999B. Known roots8,478,179,361B; two denied caches keep full8GiB
budget unverified. Same cache/runtime/SDK, commercial inventory passes, original
reference fingerprints unchanged. No measured speed advantage or family closure:
47 in progress/52 open/0 complete.

Next: add bounded nice/minor tick generation to the shared geometry path, with
linear/log/reversed/extreme-range checks and label-overlap limits. Preserve
existing defaults until separately qualified; cap generated ticks independently
of axis span. Then adapt generic histogram/profile plots without manufacturing
calibration provenance. Additional formats, annotation text/drag/persistence,
independent OS windows and broader calibration/downstream/platform/reference/
performance gates remain open.

## Per-window plot style and on-canvas model annotations — 2026-10-28

Plot Style now controls curve width/color, measured-point radius/color and an
optional prepared model annotation. Text scale and normalized X/Y place its box
inside available canvas space (X rightwards/Y downwards); wrapping/clipping keeps
it bounded. Formula, coefficients, objective and fit quality reuse immutable
provenance, preserving undefined/unavailable metrics. Text is prepared once and
charged with the existing payload lease; no render-time provenance read/re-fit.

Style belongs to each window, separate from axes. Snapshots copy it then edit
independently; axis reset preserves it. show_intensity_plot_style and
set_intensity_plot_style accept optional plot_id, with guarded live or independent
snapshot semantics. Patch fields: line_width[0.5,8], point_radius[1,12],
font_scale[0.5,2], annotation_visible Boolean, annotation_x/y[0,1], curve_color and
point_color #RRGGBB. Validation stages a candidate; invalid patches preserve state.
Dialog callbacks resolve ID+generation and work after source close for snapshots.

Windows installed770 checks pass, zero failures/skips, plus native consumers.
Both UI workflows physically edit/apply style after source closure; independent
readers derive annotation box position from delivered canvas/text geometry and
verify exact text, sibling/data/history/memory invariants. Dedicated case now
154/192 frames; calibration stays606/608. Linux private C++17 consumer passes
against existing SDK; no Linux UI/full SDK, OS/GPU or whole-family promotion.
[Scoped evidence](../parity/reviews/G034-calibration-plot-style-progress001.json).

One new test mistook a hidden live-window observation for an always-present
getter. It now closes floating windows before checking the unchanged live style;
failed reader preimage/diagnostics retained, product unchanged. All terminal
scratch retired; lossless compression recovered343,830B. Known roots
8,474,770,010B; two denied caches keep full8GiB budget unverified. Same active
caches/runtime/SDK, commercial inventory passes, original fingerprints unchanged.
No matched speed advantage; tracker47 in progress/52 open/0 complete.

Next: image export using the same prepared data/viewport/style/model layout.
Prepare a bounded independent output target, preserve source-closed snapshots,
reserve output memory before writing, and verify dimensions/colors/geometry from
exported artifacts. Avoid capturing workspace controls or re-reading/re-fitting
source data. Nice/minor ticks, generic histogram/profile reuse, custom annotation
text/drag/persistence, independent OS windows and broader calibration/downstream/
platform/reference/performance gates remain open.

## Captured source captions on calibration plots — 2026-10-28

Live and retained calibration plots now display the source title captured when
their shared immutable payload was prepared. Manual Apply, explicit table/fit
plots and asynchronous fitted publication all capture metadata before commit.
Snapshots preserve the original title after rename/closure without retaining a
document/provider or looking up current metadata during rendering. The payload
lease charges title string capacity once; Python snapshot summaries expose exact
source_title. Empty titles display an untitled-source placeholder; CSV unchanged.

Windows installed757 focused checks pass with zero failures/skips, plus native
consumers. New checks cover all three publication routes, Unicode/literal ID and
format text, rename/close retention and large-title quota refusal before commit.
Both UI lifecycle cases retain original Unicode captions through fixture rename
and source closure; snapshot case remains92/192 frames, calibration606/608.
Linux private C++17 payload consumer passes against existing SDK. No Linux UI,
OS/GPU, whole-family or speed promotion; tracker47 in progress/52 open/0 complete.
[Scoped evidence](../parity/reviews/G034-calibration-plot-caption-progress001.json).

All owned terminal scratch retired after lossless verification; compression
recovered326,970B. Known roots8,471,863,221B, two denied caches keep full
8GiB budget unverified. Same caches/runtime/SDK, no new dependencies or reference
fingerprint changes. Next: on-canvas annotation placement/style and image export;
nice/minor ticks and generic plot reuse. Persistence, independent OS windows and
broader calibration/downstream/platform/reference/performance gates stay open.

## Source-independent floating calibration plot snapshots — 2026-10-28

Keep snapshot and Duplicate snapshot now retain immutable prepared plot data and
annotations through one shared payload lease. Each floating window owns its view,
drag/focus state and small managed controller lease. Source changes invalidate
only live plots; snapshots retain their data after source closure, with axes,
pan/zoom/rectangle/details and CSV through the shared renderer. Last owner releases
the managed payload. Admission precedes image commit/publication, preserving the
old plot/calibration/history on quota refusal. No source providers or documents
are retained, no second plot store/worker pool or dependency is added.

Python Editor.plot_snapshots_info reports IDs and managed bytes. Actions
keep_plot_snapshot and close_plot_snapshot manage windows; optional plot_id on
existing limits/rectangle actions selects a snapshot independently of documents.
Omitted/zero plot_id keeps live owner/revision guards. Public IDs are monotonic;
reused internal ImGui slots clear owned input/storage and scope logical controls.
Dialog callbacks resolve ID+generation. Latest explicit focus request wins.

Windows installed753 checks pass, zero failures/skips, plus native projection and
private viewport/annotation/payload consumers. Four C++ UI cases pass. Both UI
frontends physically Keep/Duplicate, edit axes after source closure, export CSV
and close snapshots; readers verify shared charging, independent views, unchanged
samples, expired source weak owner and final managed bytes zero. Dedicated case
uses92/192 frames; original calibration stays606/608. Linux private C++17
consumer passes against existing SDK; no Linux UI/full SDK or OS/GPU promotion.
[Scoped evidence](../parity/reviews/G034-calibration-plot-snapshots-progress001.json).

One753-test run caught snapshot appearance stealing new live-plot focus; explicit
focus priority fixes it, verified by two passing runs. Failed producer delta and
diagnostics retained. Terminal scratch retired after capture hash verification;
lossless retention recovered1,038,313B. Same cache/runtime/SDK; known roots
8,470,427,987B, two denied caches keep full8GiB budget unverified. Managed plot
bytes exclude ImGui/driver/transient allocations and total RSS. Historical
expectations/fingerprints unchanged; commercial inventory passes. No measured
speed advantage or family closure:47 in progress/52 open/0 complete.

These are in-host floating windows, session-only. Independent OS windows require
GLFW/OpenGL platform-window integration and OS/GPU evidence. Next: source-title
captions, on-canvas annotation/style/image export and nice/minor ticks; reuse the
renderer for histogram/profile. Snapshot persistence, broader calibration and
downstream/reference/platform gates and matched performance remain open.

## Rectangle zoom through the existing viewport — 2026-10-28

The shared C++/Python plot canvas supports left-drag rectangle zoom. API callers
use the same implementation:

```python
editor.action("zoom_intensity_plot_rectangle", rectangle=[0.25, 0.25, 0.75, 0.75])
```

Coordinates are fractions of the current visible plot: X left-to-right and Y
bottom-to-top. Either endpoint order works; outside values clip to [0,1]. Four
finite numeric values and a representable nonzero area are required. Existing
source/All revision guards apply; optional document_id must be active plotted
owner. The4-GUI-pixel drag threshold applies to physical gestures, not this API.
Linear/log and reversed axes keep their direction; source/history/samples/CSV
remain unchanged. Gesture state captures viewport/geometry/generation and
cancels on Escape/right-click, lost focus/hidden/layout/view change or replacement.

742 Windows focused checks pass, with actual C++/Python rectangle input and
Linux C++17 private consumer coverage. Current cases606/608 frames retain the
existing cap. [Scoped evidence](../parity/reviews/G034-calibration-plot-rectangle-progress001.json).

Shortest next path: factor a shared immutable prepared plot/annotation payload,
then bounded detached/multiple snapshots with explicit lifetime and memory
admission. Reuse CalibrationPlotView; do not clone source providers or decode
provenance again. Specify in-host floating and OS-window behaviors separately;
OS windows require backend/platform checks. Add a dedicated bounded snapshot UI
case instead of extending the606-frame calibration trace. Nice/minor ticks,
annotation placement/style/export, generic renderer reuse and full platform/
reference/performance gates remain open.99-family status47/52/0 unchanged.

## Prepared calibration annotations reuse path — 2026-10-28

Model and fit details now exposes recorded formulas, a-e coefficients, domain/
clipping notes, objective and fit diagnostics in both ImGui consumers. Private
CalibrationPlotAnnotation reads only bounded named fields and prepares text once
before plot publication. Its8192-byte reserve is included in existing plot
admission; rendering performs no provenance decode or table materialization.
Reuse this immutable DTO for future on-canvas annotation placement and image
export; do not add a parallel metadata cache/store or recompute fits.

Null R-squared is undefined; missing/invalid metric is unavailable. NIH inverse
fits label measured(known), separately from their displayed inverse calibration
formula. Coefficients use locale-independent round-trip text. Unsupported model
provenance leaves literal lookup plotting available. Empty [] round trips can use
legacy string-list storage: accept that representation only when empty and the
recognized model has zero coefficients (OD), preserving strict numeric parsing.

730 Windows checks pass, including all15 models, annotation panel interaction,
source/history preservation, quality edge cases and legacy OD compatibility.
Linux C++17 private tests link the existing pinned core SDK archive and pass;
this is no full Linux UI/SDK or OS/GPU promotion. [Scoped evidence](../parity/reviews/G034-calibration-plot-annotation-progress001.json).

Next: reuse CalibrationPlotView for rectangle zoom; then annotation placement/
style/image export and automatic nice/minor ticks. Detached/multiple snapshots
need explicit lifetime/admission independent of the active document; generic
histogram/profile rendering should share these components. Broader calibration,
platform/reference and matched speed gates remain open;99-family47/52/0 unchanged.

## Calibration plot grid/tick/log options — 2026-10-28

The existing numeric-axis action now accepts optional Boolean log_x, log_y,
grid and ticks. Omitted options preserve the current plot configuration:

```python
editor.action("set_intensity_plot_limits", limits=[1, 65535, 1, 32767],
              log_x=True, log_y=True, grid=True, ticks=True)
```

Select positive limits appropriate to the plotted data. Both endpoints must be
finite, positive and distinguishable on a log axis; reversed limits are allowed.
Nonpositive samples appear as gaps, with a visible notice. Readouts/ticks retain
original units; CSV and source/history are unchanged. New plot/reset restores
linear axes and default grid/ticks. The shared Axis Limits dialog exposes the
same options with its existing generation/All-source guards. No new core SDK API.

707 Windows focused checks pass, including actual ImGui log/grid/tick input in
both consumers and independent screen-coordinate assertions. Final Linux C++17
private viewport arithmetic passes. Scope excludes OS/GPU, Linux UI/SDK and full
ImageJ tick-style qualification. [Scoped evidence](../parity/reviews/G034-calibration-plot-log-progress001.json).

Next shortest path: stage immutable function/fit formula, coefficients, objective
and finite/null R-squared annotations before publication; reuse the viewport for
rectangle zoom, then nice/minor tick/style options and generic histogram/profile
rendering. Detached/multiple plot snapshots need their own explicit lifecycle.
Full calibration/platform/reference and matched speed gates remain open.

## Current calibration plot API path — 2026-10-28

Manual calibration/function dialogs now have automatic Show plot (17c7cab), and
shared calibration plots have cursor readout, pan/zoom/reset (d4e3f37). Numeric
axes now use the same private viewport and renderer, including reversed ranges:

```python
editor.action("show_intensity_plot_limits")
editor.action("set_intensity_plot_limits", limits=[200, 100, 1000, -1000])
```

Open a calibration plot first. Optional document_id must identify its active
source; stale source revisions and stale dialog plot generations refuse. These
actions change view only; CSV samples and image history remain unchanged. New
plots/reset restore original axes. set_intensity_calibration also accepts an
explicit Boolean show_plot; omitted option preserves its earlier setter behavior.
Show plot is an interactive-session preference, not numeric provenance.

Shortest next path: reuse CalibrationPlotView for grid/tick/log/style and
rectangle zoom, with positive-log admission and original CSV units preserved.
Stage annotation text once from owned immutable function/fit provenance before
plot publication: formula, coefficients, objective and finite/null R-squared;
avoid repeated record decoding in draw(). Then separate detached/multiple plot
snapshot lifecycle from active source edits, and reuse the viewport for generic
histogram/profile rendering. Their fixed-view renderer remains open work.

681 Windows focused checks pass, including physical numeric-axis/reversed/Cancel/
stale-draft cases in both frontends. Linux private C++17 arithmetic passes;
current core SDK qualification remains separate from new UI work. Broader
platform/reference/downstream gates and matched performance remain open.
[Scoped evidence](../parity/reviews/G034-calibration-plot-axes-progress001.json).
No99-family status promotion (47 in progress/52 open/0 complete).

## Installed calibration SDKs qualified on Windows/Linux — 2026-10-28

Both existing Release core SDK prefixes are refreshed in place. Actual consumers
compile against installed public headers/libraries; archive/header identities
match their producer caches. The Linux cache now builds the current workspace
instead of its historical source copy, with no new cache or checkout. That copy
is preserved. Linux Python/UI and installed IO SDK qualification remain separate.

The checked-in tests/installed_core fixture uses find_package/core imported
targets and an explicit SDK root. Three CTest clients pass on each platform:
core API, prepared calibration/projection API and pinned plot profiles. Each
platform passes128 exact table profiles plus86 fitted-plot dtype cells (428
unique platform/profile cells total). Fitted tolerances remain3e-6 relative/
1e-6 absolute; reference coefficients isolate plotting from optimizer behavior.
The carrier generator guards original reference fingerprints and fresh output.
Temporary client configuration/objects/executables are retired, not retained
product caches. [Scoped evidence](../parity/reviews/G034-installed-calibration-sdk-progress001.json).

Next: manual calibration/function dialogs automatic Show plot and remaining
plot annotation/interaction/detached-window behavior; then fitting settings/
restarts/global calibration, table import/export/replay/macro and downstream
measurement/histogram. Installed IO, Linux Python/UI, macOS and original-
reference gates remain open. Prior663 Windows focused checks retain their own
scope. No performance comparison or full-family completion is established.

Read-only identity/reference audits preceded retirement of four obsolete Linux
staging installs, recovering135,033,093 logical bytes; compressed necessary logs/
carrier recovered1,170,845 more. Active SDK/runtime/caches and source changes
preserved. Old replaced SDK/staged binaries are unavailable, with identities
retained; no whole cache/SDK/environment archive. One Windows CMake client-harness
path-escaping failure is documented and corrected. Known complete generated
roots8,448,328,095B; two denied caches keep full8GiB budget unverified. Commercial-
use inventory passes. Tracker47 in progress/52 open/0 complete.


## Calibration table plots and physical CSV export — 2026-10-28

C++/Python now expose ImageJIntensityCalibrationPlot through the separate
imagej_intensity_calibration_plot producer. It samples256 Float32 positions
over the full raw range and reads truncated table indices, including implicit
signed16 and arbitrary lookup/OD data. Exact sampled IEEE words preserve NaN
payloads/signed zero. No full16-bit table allocation, model re-evaluation or
provider reads. Admission/cancellation and finite-range fallback are bounded.
Analyze → Calibration Table Plot reuses the visible renderer, memory preference
and scalar revision binding. Table values include their stored clipping policy;
fit plots retain the mathematical model curve.

128 profiles from actual pinned ImageJ tables match coordinates/words exactly.
The sampler policy is audited against Calibrator.showPlot; this observer does
not invoke its private GUI method. Arbitrary/non-finite tables and stale-source
views pass. Both actual Windows hosts physically export256-row table CSV and
measurement3/curve100-row fitted CSV through Unicode path entry/Save, verify
file values, close the plot and preserve source revisions/history. Four cases
use345 frames within352 cap. Final663 focused checks pass without skips, plus
Windows/Linux native source consumers. [Scoped evidence](../parity/reviews/G034-calibration-table-plot-progress001.json).

Next priority is installed C++ SDK integration: update the one active Windows
SDK in place using the existing cache's development component, then verify real
consumers of the fit/plot/prepared APIs. Reuse the configured Linux cache/SDK
for its corresponding refresh. Record identities and complete storage before/
after; do not create another compiler cache, SDK copy or environment. Manual
dialog automatic plots, full plot annotations/interactions, settings/restarts/
global calibration, table import/export/replay and downstream behavior remain.
Older cards preserve historical scopes; this block defines current next work.

One cache/runtime reused. Required captures/logs compressed and hash-verified;
scratch retired. Total5,003,578 bytes recovered through identity/log retention,
one stale executable and superseded247 capture retirement. Its compact numeric/
CSV values and member hashes remain; omitted full bytes are unavailable.
Source/objects/target preserved. Known complete roots8,589,087,663B; two denied
caches keep full8GiB budget unverified. Dependency commercial-use inventory
passes. Tracker47 in progress/52 open/0 complete; no comparative speed claim.


## Automatic fit plots and visible dock lifecycle — 2026-10-28

Polynomial/nonlinear dialogs now capture Show plot (interactive defaulttrue,
remembered within the Window session). The strict editor action accepts the
same Boolean. Plot generation runs on the existing fitting worker with4MiB
additional declared admission and retained Ready resources. Metadata publishes
first under existing guards, then compile-time noexcept swaps install the plot
and a staged scalar document/revision binding. No snapshot/map copy follows
commit; no-op edits bind to unchanged revisions. Numeric provenance stays free
of presentation flags. Failure/cancellation preserves the prior valid plot.

Final531 focused checks pass with no skips, plus current Windows native checks.
Both actual Windows hosts physically toggle off/on, verify worker plot stage/
Ready retention, automatically display a visible plot and close it through its
visible toolbar. Cancel preserves the previous plot; changed units add one Undo
entry. Four cases use301 bounded frames (cap320). Fresh-context API checks use
four initialization frames and require actual visible output.
[Scoped evidence](../parity/reviews/G034-intensity-auto-plot-progress001.json).

Scope correction: earlier fit-plot captures established payload/coordinates,
not active dock visibility. Begin(false) was ignored and controls could describe
stale items. The renderer now respects visibility, selects the Plot tab and
retains focus through its initial appearing/docking frames. A compile-time
snapshot-swap rejection and two UI failure runs remain preserved. Historical
expectations/fingerprints are unchanged.

Next: manual calibration/OD/lookup table plots using the separate upstream
integer-indexed table policy; reuse the same presentation preference and renderer
without another pool/store. Add physical CSV export evidence. Then fitting
settings/restarts/global calibration, table import/export/replay/downstream and
the batched installed C++ SDK refresh. Macro defaults and full platform gates
remain open. No speed advantage or family completion is established.

Same cache/runtime reused. Required success/failure captures and logs remain
compressed/hash-verified; scratch retired. Total5,084,949 bytes recovered through
identity/log retention, a stale test executable and superseded208 capture
retirement. Its compact plot data/member hashes remain; omitted original full
bytes are unavailable. Source/objects/target preserved. Known whole-root
8,588,836,987B; two denied caches keep the full8GiB budget unverified. Dependency
commercial-use inventory passes. Tracker47 in progress/52 open/0 complete.


## Shared fit-plot data and ImGui consumer — 2026-10-28

C++/Python now expose bounded ImageJIntensityFitPlot data through the existing
native evaluator and immutable fit provenance. Analyze → Calibration Fit Plot
draws measured points and fitted curves in the existing Plot panel with actual
X spacing. It honors processing-memory settings, invalidates stale sources,
skips non-finite curve segments and bounds drawing coordinates. CSV export and
Close controls are implemented; their physical-input gates remain open.

43 pinned ImageJ getPlot profiles produce86 dtype comparisons across all14
fitted functions, irregular measurements, request256 domain0..255 and1000
sample cap. Curve X coordinates match exactly; curve Y/limits use declared
3e-6 relative/1e-6 absolute tolerances. Reference coefficients isolate plotting
from optimizer differences. NIH Rodbard keeps original plot axes and labels its
swapped fitting-error objective. Plot curves precede UInt8 table zero clipping.
Final522 focused checks pass, no skips, plus Windows/Linux native consumers.
Both actual hosts physically select the menu and render the expected curve/
scatter; previous fit, Ready, Cancel and session cases remain within256frames.
[Scoped evidence](../parity/reviews/G034-intensity-fit-plot-progress001.json).

Next: capture the interactive Show plot option and construct its plot data on
the existing fitting worker, then publish it with the successful metadata edit.
Preserve failure/cancel atomicity and charge the additional declared resources.
Manual calibration/OD/lookup plots require the separate upstream table-sampling
policy; do not substitute the fitted-model producer. Then settings/restarts/
global calibration, table import/export, replay and downstream consumers. Batch
the installed C++ SDK refresh after these public calibration contracts settle.

Same cache/runtime reused. Java/compiler/UI scratch retired; required logs and
captures compressed/hash-verified. Identity/log compaction and one stale path-
filter admission executable recovered3,727,680 bytes; source/objects/target
remain. Known whole-root8,588,489,486 bytes; denied pytest trees keep the full
8GiB budget unverified. Commercial-use inventory passes. Tracker47 in progress/
52 open/0 complete; no comparative speed claim.


## Physical cancellation of running calibration fits — 2026-10-28

Both actual Windows hosts now submit polynomial/nonlinear fits through the
dialog, render Processing while the genuine job is Running, and click its
Cancel button with pointer input. All four cases end Canceled, release the
reservation and preserve data, all revisions and Undo history. Existing draft
Cancel, successful Apply, Ready retention and session reopen checks remain.
Final 420 focused checks pass without skips; current Windows native checks pass.
[Scoped evidence](../parity/reviews/G034-intensity-fit-cancel-ui-progress001.json).

The declared one-shot private barrier holds the genuine worker before compute;
it creates no normal-processing delay or public setting. This proves physical
Running-state cancellation wiring, not numerical-stage cancellation latency.
Independent API tests already cover solver/table checkpoints. The driver keeps
bounded waits/failure cleanup and now allows 256 frames (248 observed) to retain
the previous cases plus this scenario. No pinned historical expectation changes.

Next fit-plot path: first inspect the pinned upstream axis/objective behavior.
Add bounded C++ plot data using the existing evaluator/provenance, expose the
same data in Python, then render pairs and curve in the existing ImGui Plot
panel with arbitrary X spacing. Do not duplicate formulas or infer fractional
curve values from an integer lookup table. Cover NIH Rodbard objective/axes,
non-finite domains, cancellation and source ownership. Then finish settings/
restarts/global calibration, table import/export, replay and downstream behavior.

One cache/runtime reused. Current captures and necessary logs are losslessly
compressed and hash-verified; scratch retired. Identity/log compaction plus one
stale manual-threshold path test executable recovered 3,333,204 bytes; source/
objects/target remain. Known whole-root 8,587,438,804 bytes; two denied caches
leave full 8 GiB budget unverified. Commercial-use inventory passes. Tracker
stays 47 in progress / 52 open / 0 complete; no comparative speed claim.


## Background calibration fitting — 2026-10-28

Polynomial and nonlinear fits now use the existing owned worker and prepared
metadata publication in both ImGui hosts. Descriptor admission precedes input
copying and callbacks; requests freeze before execution. Numerical/table stages
report progress and check cancellation. Publication keeps the same document and
pixels, refuses stale/closed/cancelled sources and creates one Undo/Redo edit.
The existing scheduler retains the admitted result through Ready and releases
its reservation after publication. Python callback ownership is safe across
execution-scope copies without the GIL.

Final 420 focused checks pass without skips, plus Windows and Linux native
consumers. Both actual Windows hosts verify fit Apply, unchanged source and
retained reservation at Ready, publication and session reopen. API tests cover
in-solver/table cancellation, cancel-after-Ready, stale display/metadata, closed
owners, callback failure and input freezing. This does not establish a physical
Cancel click while Running, latency percentiles or an RSS/private-cache bound.
[Scoped evidence](../parity/reviews/G034-intensity-fit-worker-progress001.json).

Next: close the physical running-cancel gate, then fit plots/settings/restarts/
global calibration, table import/export, replay and downstream consumers.
Broader platform/SDK/original-reference gates and matched performance remain
open. Family tracker stays 47 in progress / 52 open / 0 complete.

One compiler cache/runtime reused. Necessary captures and logs are losslessly
compressed and hash-verified; scratch retired. Preliminary compile/probe
failures remain documented. Identity/log compaction and one stale test binary
recovered 5,741,321 bytes; source/objects/target remain. Known whole-root storage
is 8,588,976,282 bytes; two denied caches keep the full 8 GiB budget unverified.
Commercial-use dependency inventory passes. No comparative speed claim.


Nonlinear fit update,2026-10-28: fit_nonlinear covers all ten nonlinear models
through the shared evaluator and immutable generator. Analyze dialog exposes
starting coefficients ([] auto), iteration/tolerance and data/unit controls.
Rodbard NIH Image uses measured(known) objective. Next reuse existing owned jobs
and guarded prepared metadata publication for progress/cancel; synchronous
bounded callbacks are not full fitting lifecycle parity. No alternative store,
codec, worker pool or solver backend needed.
[Evidence](../parity/reviews/G034-intensity-nonlinear-fit-progress001.json).


Polynomial fit update, 2026-10-28: fit_polynomial supplies coefficients and
bounded raw-pair/residual provenance to the same immutable function generator.
Shared Analyze dialog reuses native set/Undo/session and reports fit quality.
Keep exact ImageJ line regression because coefficient bits control signed16;
higher polynomial QR is a numerically checked solver, not the ImageJ minimizer.
Next extend nonlinear fitting through this same producer/consumer seam.
[Evidence](../parity/reviews/G034-intensity-polynomial-fit-progress001.json).


Function producer update,2026-10-28: use ImageJIntensityCalibration::function /
Python ImageJIntensityCalibration.function and Analyze → Calibration Function.
All14 numeric built-ins and UInt8 OD reuse immutable lookup/provenance; no new
store/codec.124 pinned all-code configurations pass on Windows, with physical
coefficient/draft/reopen proof. Extend fitting by supplying coefficients to this
same generator; retain ImageJ8/16-bit clipping and signed16 detection rules.
[Evidence](../parity/reviews/G034-intensity-function-progress001.json).


Intensity API update,2026-10-28: ImageJIntensityCalibration.from_record,
uncalibrated/signed16, read_imagej_intensity_calibration and
set_imagej_intensity_calibration are bound to Python. Shared Analyze dialog
uses the same native setter/metadata Undo/session seam; both Windows hosts pass
physical controls and calibrated projection. Extend function/table generation
and fitting here; preserve lookup words and do not add another metadata codec.
[Evidence](../parity/reviews/G034-intensity-ui-progress001.json).


Consumer follow-through,2026-10-28: physical Time/Channels selectors and RGB
clipped Sum now pass both Windows hosts. Next expose existing immutable intensity
read/set APIs through Python and a shared calibration dialog, retaining metadata
Undo/session machinery; function generation/fitting remains required afterward.
[Evidence](../parity/reviews/G034-projection-axis-rgb-ui-progress001.json).


Projection consumer update,2026-10-28: physical Replay/Rebind and grouped-zero
rejection/recovery now pass both installed Windows hosts. Reuse this same
producer/reader for explicit Time/RGB/method/axis cases; do not rebuild the
command/publication machinery. [Evidence](../parity/reviews/G034-projection-replay-ui-progress001.json).

Planning review, 2026-10-25. “API key” here means a named API, command or
configuration key, not an authentication credential. This card supplements the
[execution plan](../THREE_AGENT_EXECUTION.md) and [61-key source audit](CFG1-settings-consumer-audit.md).
Implementation was paused during this review. Current execution has resumed;
follow [CFG3](CFG3-measurement-name-entry.md) and the execution authority for
implementation/evidence status. The original review added no acceptance claim.
All original eligible ImageJ/Fiji requirements remain required. “Shortest” means
reuse the existing implementation and prove the missing behavior, not omit it.

## Common path and work classification

Classify a row before assigning work:

| State | Next action | Stop doing |
| --- | --- | --- |
| Implemented and current evidence adequate | Reuse exact consumed-input/result identities | Rebuilding or writing another equivalent test |
| Implemented, consumer proof missing | One independent consumer oracle and actual interaction; reuse shared persistence/validation tests | Rewriting the kernel, codec or UI merely to make a new packet |
| Implemented, reproduced failure | Smallest repair at the responsible existing seam plus regression | Parallel alternative backend or unrelated cleanup |
| Missing user behavior | Complete one named workflow through core→binding→shared UI→history/reopen | Adding an API-only stub and counting it as editor parity |
| Contract or dependency undecided | A bounded decision experiment with a concrete output and exit | Indefinite reference framework construction or frozen speculative API |

Settings share `Settings::update/reset` → `PreferencesDocument` → existing
`SettingsPanel` → existing consumer. Python uses the existing bindings/facade;
standalone and Python launches share the ImGui implementation. Add a key only
when a pinned missing option has an actual consumer. Keep version-1 persistence
and accepted/rejected draft semantics unless a demonstrated incompatibility
requires an additive change.

For the remaining fields, extend one **read-only observation helper per
`SettingKind`** in the existing schema loop. It records actual item/text identity,
editable geometry/clip/focus, draft and applied value. Keep specialized controls
for shortcuts/measurement lists where their interaction differs. Avoid adding
59 hardcoded field-name branches or a new producer executable per setting.
Consumer-specific assertions still use independent literal/numerical oracles.

## Settings batches and per-key easiest paths

Batch by the **consumer**, not one key per packet. One fresh reopen verifies the
batch's accepted map; one malformed/save-failure/control-lifecycle matrix covers
the shared machinery. Still assert each key's distinct consumer effect. Keep
coupled keys together. Defaults/Revert/Close/Search semantics receive shared
coverage plus targeted consumer regressions, not 61 copies of the same suite.

Fresh-host checks must exercise **each selected consumer**, not merely read
back a map. Preserve distinct persistence commit points: explicit Preferences
Apply, successful Histogram publication and successful Threshold Auto. Shared
codec/Cancel/save-failure checks may be reused; these different acceptance
transactions cannot collapse into one generic save test. Activate coupled
options before checking their effect: RGB weights require weighted conversion,
custom histogram bounds/bins require custom mode. An inactive captured value
does not prove a working consumer.

All keys below already have schema/widgets/consumers in the source audit.
The default first action is verify available evidence; “route” is a proposed
qualification/repair batch, not a claim of missing implementation.

| Key | Reuse and smallest observable check | Batch |
| --- | --- | --- |
| `theme` | Existing appearance application; change theme and inspect style palette in one live host | C2 appearance |
| `ui_scale` | Existing font/style scaling; assert resulting scale and stable hit geometry at two values | C2 appearance |
| `auto_dpi` | Existing supplied-DPI selection; compare enabled/disabled at nonunit injected DPI; real monitor test remains separate | C2 appearance |
| `canvas_background` | Existing ordinary/linked renderer; inspect a background pixel outside the image | C2 appearance |
| `default_zoom` | Existing owner adoption; open a second image after Apply and verify initial scale; old owner stays unchanged | C2 appearance |
| `interpolation` | Existing display sampler; magnify a tiny step image and distinguish nearest/linear without changing source pixels | C2 appearance |
| `foreground` | Existing paint conversion; actual Fill and Brush consume selected color | C3 draw |
| `background` | Existing clear/erase conversion; actual Clear or Eraser consumes selected color | C3 draw |
| `brush_width` | Existing brush gesture; one stroke has literal diameter/coverage on a small image | C3 draw |
| `rounded_corner_diameter` | Existing selection creation; draw rounded rectangle and inspect native geometric parameter | C3 draw |
| `line_width` | Existing line/polyline creation; inspect resulting width and one line measurement/render effect | C3 draw |
| `point_size` | Existing point/multipoint creation; inspect point width/render extent | C3 draw |
| `text_size` | Existing text dialog default; open after Apply, inspect value and a bounded text render; font provenance remains explicit | C3 draw |
| `show_rois` | Existing ordinary/linked ROI render toggle; keep geometry unchanged while visibility changes | C4 selection |
| `selection_enlargement_distance` | CFG2 qualified actual Preferences→ordinary/scientific enlargement→Restore/override→fresh reopen; reuse its scoped receipt | Done scoped |
| `selection_enlargement_units` | CFG2 proves stored pixel magnitude, calibrated X-spacing display and literal Expand/Shrink results through both launchers | Done scoped |
| `wand_tolerance` | CFG1 qualified actual Preferences→canvas masks→fresh reopen; reuse its current receipt | Done scoped |
| `wand_connectivity` | CFG1 qualified 4/8 masks, invalid 6 refusal and persistence; no duplicate implementation | Done scoped |
| `animation_fps` | Existing playback clock; actual Preferences Apply/draft/refusal/defaults and fresh paired reopen now have local installed C++/Python proof; retain deterministic per-frame timing and separate real scheduling/GL/platform checks | C5 stack/view; scoped local chain |
| `conversion_scale` | Existing `conversion_options()`; nontrivial range conversion with scale on/off | C6 conversion |
| `conversion_weighted_rgb` | Same captured options; colored RGB pixel distinguishes weighted/unweighted conversion | C6 conversion |
| `conversion_red_weight` | Set weighted RGB=true; distinguishable RGB channels, change red weight and independently calculate output | C6 conversion |
| `conversion_green_weight` | Set weighted RGB=true; same distinct-channel fixture, change green weight and independently calculate output | C6 conversion |
| `conversion_blue_weight` | Set weighted RGB=true; same distinct-channel fixture, change blue weight and independently calculate output | C6 conversion |
| `conversion_range_policy` | Existing display/data/dtype policy; one source with all three unequal ranges | C6 conversion |
| `conversion_full_range_inversions` | Existing U16 inversion policy; pair an ordinary value and extrema, compare pinned expected words | C6 conversion |
| `conversion_unsigned16_range` | Existing bit-range choice; boundary values for auto/8/10/12/14/15/16 in one table | C6 conversion |
| `histogram_custom` | Existing HistogramPanel; switch default/custom and inspect the captured histogram contract | C7 histogram |
| `histogram_bins` | Enable custom mode; tiny data with known counts at two active bin counts | C7 histogram |
| `histogram_lower` | Enable custom mode; known sample near lower edge, invalid reversed pair atomic | C7 histogram |
| `histogram_upper` | Enable custom mode; same fixture, exact upper-edge convention and rejected pair | C7 histogram |
| `histogram_log` | Existing plot mapping; counts unchanged while plotted ordinate changes | C7 histogram |
| `histogram_use_roi` | Existing legacy ROI switch; asymmetric inside/outside values distinguish histogram | C7 histogram |
| `histogram_scientific` | Existing target-mode choice; inspect effective mode and execute one TCZ target | C7 histogram |
| `histogram_roi` | Existing whole/active/positioned targets; reuse P13i target semantics and known per-scope counts | C7 histogram |
| `threshold_method` | Existing method registry; reuse pinned numerical suites, verify actual accepted method reaches Auto and reopen | C8 threshold |
| `threshold_histogram_roi` | Existing histogram-versus-Apply separation; Auto input counts change without changing Apply target | C8 threshold |
| `threshold_apply_roi` | Existing captured target; exact outside words and positioned-unmatched controls | C8 threshold |
| `threshold_stack_histogram` | Existing pooled histogram; two different Z planes distinguish single/pooled threshold | C8 threshold |
| `threshold_bright` | Existing object polarity; known bimodal mask changes polarity with documented threshold | C8 threshold |
| `threshold_ignore_black` | Existing histogram exclusion; include zero bin and verify contributing count/threshold | C8 threshold |
| `threshold_ignore_white` | Same fixture with maximum bin; verify exclusion separately then together | C8 threshold |
| `binary_iterations` | Existing `binary_options()` and pinned consumer tests; reuse then check one Preferences-to-command chain | C9 binary |
| `binary_neighbors` | Existing morphology option; small literal neighborhood distinguishes counts 1 and 8 | C9 binary |
| `binary_black_background` | Existing polarity; compare complementary foreground/background interpretation | C9 binary |
| `binary_pad_edges` | Existing erosion edge policy; edge-touching fixture and pinned reference | C9 binary |
| `history_limit` | Existing prepared history policy/HU tests; prune known edits, verify Undo count | C10 resources |
| `history_memory_mb` | Existing obsolete-pixel accounting; shared current pixels and old buffers distinguish zero versus finite budget | C10 resources |
| `async_viewport` | Existing viewport jobs; inspect worker/pending/ready lifecycle and cancellation, not timing alone | C10 resources |
| `viewport_cache_mb` | Existing VirtualViewCache; bounded plane reads demonstrate eviction/reuse. Tiled storage remains separate G041 work | C10 resources |
| `render_memory_mb` | Existing RenderMemory admission; one admitted and one refused surface, recover without owner mutation | C10 resources |
| `processing_workers` | Existing scheduler; deterministic barriers show concurrency ceiling, no sleeps as proof | C10 resources |
| `processing_memory_mb` | Existing P13i per-job admission/recovery proof; reuse unchanged inputs, add only new consumer boundary | C10 resources |
| `measurement_precision` | Existing table/CSV formatter; compare output decimals while raw measurements stay equal | C11 analysis/output |
| `measurements` | CFG3 qualifies new-name Add/Enter, refusals/draft publication, literal control-name/## identities, full CSV, fresh hosts and bounded 1,024-column Results paging including multiline headers | Done scoped; remaining C11 analysis/output stays open |
| `profile_samples` | Existing profile sampler; asymmetric line, automatic versus explicit sample count and calibrated coordinates | C11 analysis/output |
| `print_dpi` | Existing PDF export defaults; inspect physical output size/resolution on a tiny calibrated image | C11 analysis/output |
| `print_landscape` | Same export; inspect page orientation with non-square dimensions | C11 analysis/output |
| `startup_mode` | Existing StartupRequest/plan; fresh host empty versus reviewed workspace | C12 shell |
| `startup_workspace` | Same loader; actual Browse/Apply and exact restored membership, missing-file recovery | C12 shell |
| `shortcut_bindings` | Existing compiler/ShortcutEditor; bind one command, conflict refusal, actual key dispatch and fresh reopen | C12 shell |

C2 now has actual Windows/Linux persist/reopen proof in both C++ and Python for
these six appearance consumers; see the [scoped receipt](../parity/reviews/cfg4-appearance001/receipt.json).
The responsive linked-view repair and focused regressions are included. macOS,
full owning cohorts and strict consumer-ledger admission remain required; this
does not qualify all 61 settings or complete a capability family.

Defaults, ranges, scope and persistence meanings come from the source audit;
this table does not replace them. The next settings batch is selected by missing
evidence and dependency readiness; C2…C12 labels do not force sequential order.

C5 FPS has four local Windows installed Preferences→native clock→fresh reopen
IDs, with unchanged source/selection/history and all linked TCZ planes. The
[C5 receipt](../parity/reviews/cfg6-playback001/preferences-ui-progress.json)
keeps the loaded-file baseline separate from actual Apply and its new required
hosted packet. OS scheduling, GL, Mac and the remaining stack/view inventory in
[CFG6](CFG6-settings-playback.md) remain open.

## API seams: reuse before extending

| API / surface | Easiest complete path | Add only when necessary |
| --- | --- | --- |
| `Settings::schema/definition/update/reset` | Schema drives validation and existing generic widgets; one shared per-kind observation adapter | A pinned new option with a real consumer; no shadow Python schema |
| `PreferencesDocument::parse/load/json/save` | Existing lossless version-1 codec, whole-map validation and atomic replacement | A demonstrated representation/migration requirement; no feature-specific file codec |
| `Settings::binary_options/conversion_options` | Capture existing adapter result at command resolution | Missing operation-specific option, not another global option resolver |
| `operation_registry/spec/defaults/validate_operation` | Existing registry is the command/option-key source; extend one entry and existing dispatch, consume it from Python and dialogs | A genuinely missing command/parameter; preserve execution versus GUI defaults and do not add a second registry |
| `DocumentSnapshot` / `TargetRequest` / `ResolvedTarget` | Resolve once at request time; keep display navigation separate from captured intent | New supported scope with explicit serialization/compatibility decision |
| resolve→prepare→publish command families | Reuse frozen command, private prepared result and single atomic publication | Missing command type; never compute independently in Python/UI |
| `ExecutionOptions.max_target_mask_bytes` | Existing P13i mask planner admission | Additional owning scratch term with lifetime proof; do not claim RSS |
| `NativeWorkflowLimits.max_working_bytes` | Existing retained/captured floor plus serial scratch peak | New phase estimate integrated once; no double execution merely for a counter |
| `NativeWorkflowProgram` / `Run` | Reuse MW1's central typed manual V1/V2/V3 dispatch, Program V9 and shared Lower/Upper editor; extend other missing commands at their existing seam | Preserve closed legacy grammars and immutable originals. MW1 Windows/Linux consumers and final applicability pass; landing is recorded in its packet card |
| `SelectionEditor` / creation / saved ROI / planar commands | Same immutable geometry with carrier-specific transaction/history adapters | New intent, not a second geometry object per frontend |
| `combine_selections` / `combine_selection_regions` | Preserve existing grid algebra and name continuous behavior separately | Continuous solver after the product precision/backend decision; never relabel a mask result as a curve |
| Proposed `combine_composite_paths` | Select one documented curve-preserving native64 profile/backend; compare against ImageJ's float-effective domain separately; reuse immutable numerical work only where phases permit | Apply creates a separately admitted publication object and rechecks dependencies; never commit a Preview proposal. No default float32 downgrade or two-engine mandate |
| Axis extraction/orientation/spacing/slicer resize | Existing typed commands and one asymmetric calibrated TCZYX fixture | Oblique/curved sampler or missing metadata mapping, not a replacement XYZ stack |
| `JobScheduler`, viewport and render caches | Existing processing and viewport schedulers with distinct ownership/cancellation/generation contracts | Missing consumer integration; no new per-panel threads, oversubscription or unnecessary scheduler unification |
| Native session / TIFF / scientific I/O | Existing format providers and atomic output service | One reviewed format variant/provider; Python remains an adapter |
| `inspect_image_file/read_image_series/estimate_image_write/write_image_series` | Existing inspect→series selection→read/write lifecycle; add the next format variant at provider dispatch | A reviewed codec or metadata/series mapping; preserve limits/replace policy and installed optional-I/O boundaries |
| Public Python wrappers / optional `_desktop` | Existing native bindings, one shared UI; check installed origin and representative launch parity | Python convenience glue only, no processing fallback |

For a command's individual parameter keys, use `operation_spec(id).parameters`
as the authoritative inventory. At entry record each name/type/default/choice,
current dispatch consumer and independent effect; classify it using the table
above. Keep image-dependent defaults and GUI/execution default profiles distinct.
Do not assume these 61 settings are the full ImageJ/Fiji option inventory.
Commands discovered through M0 receive stable child IDs and the same per-key
row before their packet starts; unlisted options are not declared complete.

## G034 current range entry and next missing options

Inclusive legacy-Z projection ranges are implemented through the existing C++
kernel, additive public C++ overload, Python first/last keywords and shared
Z Project dialog. Windows installed API/SDK/native interaction checks pass;
[the range packet](G034-z-projection-ranges.md) and
[source-bound receipt](../parity/reviews/G034-inclusive-z-range-progress001.json)
state exact scope. Reuse this range reduction; do not add a Python projection.

Complete grouped reduction is now implemented in
[the grouped packet](G034-grouped-z-projection.md): one validation pass, one
reused group-sized scratch buffer, one native contiguous NumPy output and
shared UI/metadata/session/resource checks. Its new focused header avoids
changing image.hpp. Use current per-packet evidence, not the unchanged original
family status, to distinguish this implementation from the remaining scientific
scope. Group size must divide selected depth; no partial tail is silently lost.

Pinned runtime reference data now exists for324 projection cases. Use the
reference matrix to implement method-specific ImageJ behavior; UInt16 median is
Float32, sum and average accumulation differ, and metadata/overlay dispatch has
observable special cases. Reuse approved cached classes and the offline corpus;
do not rebuild ImageJ, repeat capture for passing readers, or equate the owned
native_finite profile with compatibility. Add new supplementary cases when needed
without rewriting the pinned corpus.

The native resolved command now exists in dataset_projection_command.hpp:
resolve/validate/from_record/rebind/execute share strict source/typed-request
checks. Reuse it, and keep live document publication guards separate from
captured numerical execution. Before choosing metadata/output display changes,
qualify the pinned ImageJ numerical/type/nonfinite and overlay/calibration path;
native Float64/population output is not the ImageJ default. Avoid a new Float64
viewer version merely to accommodate an unqualified parity UI default.

The finite native project_dataset_z reducer now shares the existing projection
loop through borrowed immutable planes. Reuse it from the future resolved
command; no whole-stack copy or Python kernel is needed. Keep max_working_bytes
admission and method/source capture ahead of reads/callbacks. Nonfinite/ImageJ
profiles and complete document metadata/publication admission are still open.

The shared metadata descriptor now exists in dataset_projection.hpp. Use
DatasetZProjectionRequest/describe_dataset_z_projection for T/C selection,
contributor mapping, affine/time calibration and checked pixel/scratch sizes;
resolution reads no pixels. [Scientific implementation order](G034-scientific-z-projection.md)
keeps the command, reduction, channel/annotation mapping, publication and UI
requirements explicit. A pixel-buffer cap is not complete worker admission.

Next expand a typed Dataset command for explicit captured T/C selections,
all-time-frames and grouped output. Resolve source/range/group policy once,
validate divisibility and memory before reading, preserve T/C rather than
flattening them into Z, and define output calibration/channel/ROI metadata
before UI work. Reuse existing job/derived-output publication. Independent
asymmetric TCZYX values must expose incorrect C/T mixing and group boundaries.
Existing population stddev is not a pinned ImageJ numerical-equivalence claim.
Full legacy/scientific method/type/reference, replay/session, launch and platform
acceptance remain required; G034 is not complete.

## Route for all 99 capability families

This maps every original family to a reuse path. It does not claim those APIs
already implement every command. At packet entry expand the selected row into
the exact missing command/option IDs from the inventory, with current evidence.

| Families | Shortest implementation/qualification path |
| --- | --- |
| G001–G012 | The 61-key consumer batches above; extend shared settings only for newly inventoried options |
| G013–G015, G017 | Existing selection creation/edit/manager controllers; add missing gesture/tool and numeric operation in that controller |
| G016, G018–G019 | Existing grid/edit conversions first; continuous geometry is a bounded independent solver track, not a prerequisite for all selection work |
| G020–G022 | Existing ImageJ annotation/ROI codecs and manager scope/TCZ mapping; one missing interchange or position variant per contract |
| G023–G025 | Existing pixel publication/history and annotations; complete clipboard/floating paste/text flows with OS-specific adapters at the boundary |
| G026–G029, G031–G033 | Existing Dataset/display/axis commands, linked sampler and channel display; close missing actual controls and calibration/axes mapping |
| G030 | A separate oblique/curved sampler consumes existing Dataset/calibration; define interpolation/output axes once and reuse linked UI |
| G034–G038 | Existing stack/extraction/playback/profiles; add missing projection/montage/organization operation, then one complete dialog/output flow |
| G039–G041 | Reuse bounded dataset access/jobs/render caches; one native volume/surface backend behind a small adapter, reviewed before incorporation |
| G042, G044–G045 | Existing filter command/preview/target/admission path; add missing registered kernel/options and reuse lifecycle fixtures |
| G043 | Explicit true-volume neighborhood and spacing implementation; tile halo equivalence, never repeated 2D slices advertised as 3D |
| G046–G053 | Existing histogram/threshold/binary target pipeline; implement missing numerical method once, then connect its specialized dialog/result |
| G054–G056 | Existing operations/execution engine; add frequency/equation/shadow method with explicit dtype/nonfinite semantics and reusable dialog publication |
| G057–G061 | Existing contrast/channel/LUT/conversion/geometry commands; complete missing option or accepted-target branch before new wrappers |
| G062–G065, G068 | Existing metadata/subset/calculator/view/calibration services; one shared coordinate/owner policy and exact derived-output tests |
| G066–G067, G069–G073 | Existing measurement/particle/histogram/profile/table services; independent known-shape/signal oracle and full result/export workflow |
| G074–G082 | Existing I/O/provider/session/interchange lifecycle; per-format eligible codec adapter, format-variant inventory and atomic failure/recovery |
| G083–G084 | Shared ImGui shell plus narrow native OS adapters; menus/focus/keyboard/accessibility and real backend tests remain first-class |
| G085–G089 | Existing workflow/batch/jobs/native Python control; central typed dispatch, recorder/script/editor/extension lifecycle, installed optional-runtime absence |
| G090–G091 | Share G039–G041 data/cache/view infrastructure; package compatibility is an adapter/workflow layer, not a second volume engine |
| G092–G099 | Per-package pinned code/model/data eligibility and minimal native adapter; use existing targets/jobs/results/UI services for complete MorphoLibJ, registration, stitching, deconvolution, TrackMate, skeleton, colocalization and segmentation workflows |

## Three-agent dispatch and evidence budget

A owns the selected native consumer/command; B owns shared ImGui interaction;
C owns independent expected values, integration and one build/install lock.
For existing-correct behavior A audits/contracts and supplies only missing
boundary tests; it need not invent native changes to occupy a slot. During C's
verification A/B may prepare a different ready packet's source outside the
frozen transitive input closure, with explicit file ownership and no second build.
No overlapping shared-file edits or extra caches/runtimes are allowed.

Use layered evidence: native numeric/options tables cover exhaustive values;
Python binding tests cover marshalling; actual UI checks cover real controls and
critical transitions. Each changed setting still needs its own consumer effect.
Run representative host launch parity early, then the declared full supported
platform/context matrix once on final inputs. Do not infer complete feature
coverage from pairwise sampling; omitted combinations need an independence
argument or explicit unqualified status.

Store one input/product manifest and reference it. Preserve original failures,
concise XML/logs and necessary lossless captures; retire superseded full captures
after verification. Do not copy the entire repository file list into every tiny
packet when a transitive source/tool/fixture closure and base commit suffice.
Prepare one runnable selector manifest at entry; use the same harness locally
and in CI. Resource measurements include every active generated root and peak
raw plus compression workspace. Source-only plan reviews create no build assets.

## Projection numerical delivery checkpoint — 2026-10-26

Reuse the pinned336-ray private scalar oracle and existing native_finite
command guards. Next add the separate ImageJ dataset profile with resolved
axis, density and bounded streaming/tiled execution; do not revise historical
native request/record semantics. Only after RGB/metadata policy is verified
should prepared publication, Python and shared ImGui consume that profile.
See the current execution card for the ordered acceptance boundaries. A passing
scalar kernel is not completed ImageJ dataset/editor behavior.

## ImageJ dataset profile checkpoint — 2026-10-26

The separate scalar dataset descriptor/executor is implemented and locally
verified against228 captured cases. Reuse its resolved axis and rectangular
streaming/tiled path. Next close RGB clip/global-scale behavior, then intensity,
LUT/overlay and prepared publication, a distinct strict ImageJ record, Python
and shared ImGui interaction. Keep legacy native_finite unchanged. Core
scientific grouping must not silently stand in for the grouped GUI hyperstack
dispatch. Legacy default region adapters can repeat full-plane decodes; tile
height now avoids the earlier row-only amplification and the descriptor charges
its scratch. Provider-private caching remains separately owned.

## RGB profile delivery checkpoint — 2026-10-26

Use `ImageJRGBProjectionRequest` -> `describe_imagej_rgb_projection` ->
`project_imagej_rgb_dataset` for the explicit UInt8 S3/C1 profile. These reuse the
scalar executor and keep scalar admission/tests unchanged. RGB clipping is a
flag on this RGB request only. Both profiles must feed the same future prepared
new-document/record/UI seam; avoid duplicate Python loops or channel stacks.
Next close general density and C-only/default dispatch together, resolve grouped
GUI hyperstack routing, then publish metadata and deliver actual shared ImGui
interaction. Do not freeze a strict schema while those source/profile fields
remain unsettled. Both working and accumulator scratch charges have host ABI
components; persist portable contributor/type claims and original raw caps,
not historical ABI admission. Full CMake/install and complete UI acceptance stay
separate from the current passing source consumers.

## Projection policy handoff — 2026-10-26

Channels/default C-only dispatch, optional integer calibration_table and density
preservation intent are implemented and source-verified. Use the existing common
request/executor; no new per-method backend is needed. Next use an immutable
MetadataTree intensity property and the prepared new-document/source-guard seam.
The desktop project handler's materialized frames/Z-only/stale metadata adapter
must be replaced for this workflow. Reuse ask/worker/facade staging; keep grouped
GUI hyperstack forwarding separate. Persist effective table identity/source
metadata in strict profiles, not host-sized working/scratch admission or duplicate
derived tables. Then bind Python/shared ImGui and verify actual dialogs and
history/reopen. Calibration-function generation/full metadata and original gates
remain required; sampled lookup fixtures prove pixel processing only.

### G034 intensity metadata checkpoint — 2026-10-26

Use `read/set_imagej_intensity_calibration` and the existing metadata/session
paths. Exact lookup tables live in shared immutable nodes; do not add a bulk
mutable DocumentData field or another codec. `apply_imagej_intensity_calibration`
binds the request; `projected_imagej_intensity_calibration` handles density/unit
intent. Windows/Linux source tests prove edit/Undo/session/projection bridges.
[Evidence](../parity/reviews/G034-intensity-metadata-progress001.json).
Next reuse `PreparedOperation` new-document staging and atomic publication,
then replace the legacy materializing scientific UI route. Channel display and
overlay mapping must be explicit; an API-only checkpoint is not editor parity.

### G034 prepared publication checkpoint — 2026-10-26

Use `prepare_imagej_projection` → `prepared_new_document` → existing atomic
`Application::publish`; do not add another output/publication framework. Source
All revisions, membership, cancellation and candidate data are guarded. Output
intensity/unit and channel presentation use existing immutable metadata, channel
reset/defaults and V3 encoding. Ordinary overlay literal regressions pass;
grouping/current-C/multi-axis native annotation extensions remain distinct.
[Source evidence](../parity/reviews/G034-projection-publication-progress001.json).
Next complete one shared ImGui workflow, including strict ImageJ records/full
admission and retained metadata differentials in that same consumer patch. In
window.cpp, rank staging near `stage_new_document` and
`publishPreparedAxisExtraction` already stage facade/map/Pane before adoption;
reuse that pattern. Reuse `inheritedDisplayLut` for legacy named palettes.
Keep native finite command replay unchanged; prepared method parameters alone
are diagnostic, not complete replay. Bind Python to the same core path.

### G034 shared consumer checkpoint — 2026-10-28

The strict command, combined admission, Python interface and shared ImGui worker/
facade/recorder path are implemented. Both Windows hosts pass the same physical
Apply/Cancel/current-T/overlay/reopen flow.150 checks pass including legacy and
transport controls; Linux core source checks pass. [Evidence](../parity/reviews/G034-imagej-projection-ui-progress001.json).
Continue at existing consumers: broaden physical replay/rebind/axis/RGB/group
interactions and retained metadata assertions, then calibrations/configuration.
Do not create another projection backend, codec or producer framework. Keep
native finite and ImageJ profiles distinct. Comparative performance remains
unmeasured; use matched workloads after correctness checks, never test duration
or reservation bytes as a speed/RSS surrogate. Full reference/platform gates and
all99 families remain required. Historical G016 inventory evidence is unavailable;
do not recreate retired build generations or repin expectations.
