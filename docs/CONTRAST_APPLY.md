# Contrast Apply core and remaining integration

The C++ core now supplies `apply_contrast_imagej(image, options, control)` for
scalar UInt8 and UInt16 images, and `prepare_contrast_apply(target, options,
limits, control)` for isolated document edits. Python exports the same functions
and typed `ContrastApplyOptions` / `ContrastApplyLimits`. Processing stays in C++.

```python
options = imagejxx.ContrastApplyOptions()
options.minimum, options.maximum = 10, 40
options.unsigned16_bits = 12
target = app.resolve_target(imagejxx.TargetRequest(imagejxx.TargetScope.ALL_Z))
transaction = imagejxx.prepare_contrast_apply(target, options)
app.commit(transaction)
```

One explicit mapping applies to every frozen target plane. Active-selection or
positioned-ROI masks preserve exterior samples; empty masks skip source reads.
The preparation accepts in-place plane-mode targets. It stages replacements,
the affected channels' range reset and threshold clearing together. Commit checks every captured
revision, and Undo/Redo restores pixels and presentation in one step. Unrelated
channels, camera settings, LUTs, selections and metadata remain intact.

ImageJ's scalar Apply narrows endpoints to Java integers and constructs an
integer lookup mapping. The configured UInt16 case has an unusual maximum:
12-bit Apply reaches 4094; automatic UInt16 reaches 65535. Extreme endpoint
subtractions and byte/short narrowing reproduce Java wrap behavior without
signed C++ overflow. Float32/Float64 and packed RGB/RGBA are refused by this
primitive. [ImageJ's Apply implementation](https://imagej.net/ij/developer/source/ij/plugin/frame/ContrastAdjuster.java.html)
defines this arithmetic; the checked-in 1.54r reference fixture invokes its
actual scalar method over every unsigned input value in 64 parameter cases.
That driver overrides AWT construction and post-Apply reset, so its hashes
qualify arithmetic only.

Pixel and working-byte admission precedes provider reads. The working estimate
covers retained replacement samples, a bounded allowance for working planes and
mask storage, replacement tables and channel-state copies. Source storage,
arbitrary metadata, ROI geometry/rasterizer scratch, provider/decoder internals,
commit/history allocations and frontend argument copies remain outside this
estimate. Whole-process memory acceptance is still open. Cancellation is checked
inside validation, mapping, mask scanning and restoration, between planes, and
before returning a proposal. No live document changes during preparation.

Captured commands and workers now provide the next API layer:

```python
command = imagejxx.resolve_contrast_apply_command(
    app.snapshot(), options, imagejxx.TargetRequest(imagejxx.TargetScope.ALL_Z))
job = imagejxx.submit_contrast_apply(app.job_scheduler, command)
if job.wait(5000):
    record = job.record()
    preview = job.preview
    job.publish(app)
```

The closed `imagejxx.contrast-apply-command` V1 record freezes mapping, bit setting,
T/C/Z targets, reset anchor, scalar storage/calibration, all source revisions,
display properties and canonical nativeV3 selection/positioned-ROI geometry.
Strict replay checks these against the original owner. Rebinding validates the
entire original first, then requires an explicit new target request and uses the
destination's geometry and presentation. No source view or ROI is restored.
Records carry intent; automatic UInt16 reset bounds are derived during execution.

Processor reset differs from the pixel mapping's maximum. UInt8 resets to 0–255.
A configured 12-bit UInt16 mapping ends at 4094 but its reset display ends at 4095.
Automatic UInt16 reset uses observed full-plane values after masked edits,
including pixels outside the ROI; a constant plane may have equal bounds.
Threshold presentation clears and indexed palettes/LUTs remain intact. Twelve
additional ImageJ 1.54r cases independently exercise whole planes, partial ROIs
and constant planes with threshold and palette state. The driver uses actual
ImagePlus/processor reset methods while bypassing AWT controls. These checks
correct the earlier transaction foundation's output-bound reset assumption.

For each affected scientific channel, reset prefers the captured T/Z plane if
that plane changed, otherwise its first affected plane. Legacy flat presentation
uses the active affected C or the lowest affected C. Entirely empty masks retain
the native no-change/no-IO behavior. These multi-axis/empty-target extensions,
custom default ranges and signed/intensity calibration need broader UI reference
qualification.

`ContrastApplyCommandLimits` adds bounded metadata/wire admission before command
construction. The command estimate includes metadata copies, ROI scratch and
wire intermediates in addition to the raw processing allowance. The worker job
reserves the larger of its estimate and an explicit job reservation. Escaped
preview snapshots, datasets and native plane aliases keep the lease after cancel,
publication, handle destruction or scheduler shutdown. Published document/history
storage detaches its lease; retained previews still keep theirs. Source caches,
allocator overhead, frontend copies and publication/history allocations remain
external. This is a declared resource bound, not a process-memory measurement.

`publish(app)` runs on the caller's thread, checks membership and all captured
revisions, and pairs pixels with presentation in one commit. Cancellation wins
until publication begins. Queued cancellation releases source captures even if
the job handle survives. Native providers must support immutable worker reads.
The core does not infer an active-document or GUI-thread policy.

The shared Window host now submits these native jobs from the ImGui
**Apply to pixels** button and Python editor actions. Expand **Apply target**
for current/range/all T/C/Z and whole-plane, active-selection or positioned-ROI
choices. UI ranges are one-based; programmatic targets use the native zero-based,
exclusive-stop grammar. The source, endpoints, target, UInt16 range preference
and recording flag are captured at submission. A successful commit publishes
pixels and reset presentation together as one Undo/Redo step.

Apply participates in editor busy state, job reporting and `wait_jobs`.
`cancel_contrast_apply`, the panel Cancel button, global cancellation and job-ID
cancellation all cancel its native job. Closing the contrast panel or source
also cancels pending Apply; changed revisions or active owners discard the work.
Workflow output is allocated before the native commit and published afterwards.
Diagnostic reports expose `contrast_apply` alongside `contrast_panel.apply_target`.
Unsupported floating and packed-color Apply controls stay disabled.

Python editor actions (all processing remains native):

```python
editor.action("show_contrast")
editor.action("contrast_range", minimum=10., maximum=40.)
editor.action("configure_contrast_target", target={"scope": "all_z", "roi": "active_selection"})
editor.action("contrast_apply")
editor.action("wait_jobs")
```

`replay_contrast_apply(record=...)` validates a captured record and strictly
replays it. Supplying an explicit `contrast_rebind` target instead retains its
mapping/bit preference and captures the destination's source, geometry and view.
The workflow panel offers strict Replay and **Rebind Apply...**. Strict stale
captures fail without fallback. Rebind opens a separate destination review,
showing the saved endpoints and UInt16 output setting and shared T/C/Z/ROI
controls. Its **Apply saved mapping** button starts the same native worker job.
Opening, configuring and canceling the review acquire no source samples and
publish no image/history/recording changes. The saved original stays immutable;
editing preferences does not replace its mapping or bit setting.

All captured destination revisions and its active owner must remain current.
A change invalidates the review and disables Apply; reopen Rebind to capture the
destination again. Closing the destination or global cancellation dismisses the
review. Closing the contrast controls also dismisses it. A failed target or
original-record replacement leaves the existing review available to correct or
cancel. Keyboard focus uses the same canvas/shortcut guard as the contrast panel.

```python
editor.action("show_contrast_apply_rebind", record=record)
editor.action("configure_contrast_rebind_target", target={"scope": "all_z", "roi": "whole_plane"})
editor.action("apply_contrast_rebind")
editor.action("wait_jobs")
# Before submitting: editor.action("cancel_contrast_rebind")
```

Native workflow and session Batch now execute scalar Apply through
`NativeWorkflowOperation("contrast_apply", parameters, target, original=...)`.
An empty parameter map retains the complete original capture's mapping and bit
setting. Authored parameters require exactly `minimum`, `maximum` and
`unsigned16_bits`; a supplied original is fully validated and remains unchanged.
Endpoints must be finite and ordered; integer endpoints must convert losslessly.
Bits require an integer from 0, 8, 10, 12, 14, 15 or 16. Typed Gaussian/rank options,
calibration scaling, volume execution and new-document destinations are rejected.

```python
target = ij.TargetRequest(ij.TargetScope.ALL_Z, roi=ij.RoiPolicy.ACTIVE_SELECTION)
saved = ij.NativeWorkflowOperation("contrast_apply", {}, target, original=record)
authored = ij.NativeWorkflowOperation("contrast_apply",
    {"minimum": 10., "maximum": 40., "unsigned16_bits": 12}, target,
    original=record)
program = ij.NativeWorkflowProgram([authored])
restored = ij.NativeWorkflowProgram.from_json(program.json())
prepared = ij.prepare_native_workflow(document.snapshot(), restored.steps)
document.commit(prepared.begin_edit())
```

The existing closed program grammar retains operation parameters and originals.
Recorded Apply rows open in Run/Batch review with their target intent. The shared
ImGui parameter editor exposes the three mapping fields; targets can be edited,
steps reordered/disabled, and reviewed intent saved as a portable program.
Later steps consume the preceding pixels and actual reset display. For example,
a masked UInt16 Apply yielding `[10, 21845, 30, 40]` resets its range to
`10..21845`; a subsequent scaled byte conversion produces `[0, 255, 0, 0]`.

Whole-program admission reads metadata and ROI masks only. Scalar type/geometry
descriptors throw on sample access, retain palettes and clear threshold/spectrum
state for affected planes. Automatic UInt16 extrema use bounded numeric
placeholders during admission and are computed from real outputs during
execution. Data-dependent failures still leave the live owner unchanged.
The budget charges the command's processing/metadata estimate, full conservative
dataset backing and records. Core phase limits remain in effect; this is a
declared budget, not process-RSS measurement. Provider caches, allocator/frontend
and publication/history allocations remain external.

Run publishes one final guarded transaction pairing changed pixels with display;
stale display revisions reject that publication. Empty ROI targets retain their
no-change/no-IO behavior. Disabled Apply steps validate portable choices without
checking compatibility against a destination or reading its samples.

Session Batch uses the same operation in common or per-input programs and Batch
setup codecs. Each selected document receives its own resolved target and reset;
other documents, session results/workflow/extensions and input files are retained.
Ordered histogram outputs describe intermediate stages. Preparation may stage
temporary files; existing no-replace/per-item publication and cancellation/failure
policies govern final files.

The independent `RgbContrastApplyOptions` / `apply_rgb_contrast_imagej` and
`prepare_rgb_contrast_apply` APIs now provide a reference-qualified UInt8 RGB
foundation. Scalar options, V1 records, worker jobs and workflow behavior retain
their contracts. RGB is not enabled in the contrast panel, captured-command
schema or native workflow operation yet.

RGB has separate arithmetic: ColorProcessor narrows the lower endpoint and
wraps its integer subtraction, but divides by the original double endpoint span.
Lookup output clips to bytes. This differs from scalar Apply, especially for
fractional, extreme and collapsed ranges. Component bits are red=4, green=2,
blue=1; nonempty combinations 1..7 are supported. Unselected components retain
their exact bytes. RGBA and other sample types remain unavailable in this API.

```python
options = ij.RgbContrastApplyOptions()
options.minimum, options.maximum, options.channels = 10.9, 40.1, 4
rgb = ij.apply_rgb_contrast_imagej(immutable_baseline, options)
estimate = ij.estimate_rgb_contrast_apply(target, options)
edit = ij.prepare_rgb_contrast_apply(target, options)
document.commit(edit)
```

Always map the immutable baseline once. The actual ImageJ RGB controller first
changes preview pixels; its Apply snapshots that preview and resets the range.
The native transform maps the supplied baseline without hidden mutable preview
state. Two hundred seventy-three ImageJ 1.54r cases cover all byte values in each
component, seven masks, fractional/extreme/signed-zero spans, whole/rectangular/
irregular ROI previews, actual Apply commit and a subsequent preview using the
committed snapshot. AWT reset controls are replaced only by actual processor
reset/range methods; full RGB UI and stack-controller behavior is not claimed.

Target preparation uses frozen options/limits, independent XY planes and the
existing current/all-Z/range/position/ROI policies. It reads selected planes,
preserves pixels outside each ROI, and pairs changed pixels with 0..255 display
reset in one guarded Undo/Redo transaction. Threshold clears and LUT metadata
persists. Empty masks keep the explicit native no-change/no-IO extension.
`RgbContrastApplyEstimate.pixels` counts XY pixels; `samples` counts three per
pixel. Working-byte admission includes three-component processing and masks;
source metadata/providers, allocator/frontend and publication/history remain
external. The API is synchronous; callers must arrange worker-only virtual reads.


### Captured RGB commands and worker jobs

`rgb_contrast_apply_command.hpp` exposes the separate
`imagejxx.rgb-contrast-apply-command` V1 schema with the `rgb_byte_apply_v1`
profile. Options retain the original double endpoints and component bits.
Records capture immutable source storage, calibration, all revision domains,
display properties, T/C/Z reset anchor and canonical target selection/positioned
ROIs. Capture, strict replay, rebinding and estimation do not acquire planes.
Scalar V1 schema/fields and mapping behavior remain unchanged.

```python
command = ij.resolve_rgb_contrast_apply_command(document.snapshot(), options, request)
record = command.record()
ij.validate_rgb_contrast_apply_command_record(record)
strict = ij.rgb_contrast_apply_command_from_record(document.snapshot(), record)
job = ij.submit_rgb_contrast_apply(application.job_scheduler, strict)
if job.wait(5000) and job.ready:
    preview = job.preview
    job.publish(application)
```

Strict replay requires the captured source and all revisions. Explicit
`rebind_rgb_contrast_apply_command(destination.snapshot(), record, request)`
validates the entire original before recapturing destination geometry/display;
it retains mapping/component bits and refuses scalar, RGBA and other unqualified
storage. Neither path restores a recorded view. Reapplying to an already mapped
preview would transform twice; a command always describes mapping its captured
baseline once. A fresh command after publication uses the committed baseline.

`rgb_contrast_apply_job.hpp` returns the shared `ContrastApplyJob` handle.
Native workers alone read the immutable source and prepare a detached result;
caller-thread publication requires source membership and unchanged revisions.
Cancellation can win until publication begins. Queued cancellation releases its
captured source; escaped preview snapshots, datasets and plane aliases retain
their declared result reservation through publication/cancellation/shutdown.
The result guard and publication code are shared with scalar jobs.
`estimate_rgb_contrast_apply_command` and `estimate_rgb_contrast_apply_job`
include bounded metadata/record work and owned processing/result work. Provider
storage, allocator overhead and frontend/history staging remain external.

The record profile deliberately describes the native uniform independent-XY
mapping. ImageJ's active RGB preview commit and processor-narrowed bounds for
other stack planes still need a separately qualified controller path. The shared
editor now exposes this native uniform mapping, as described below.


### Modeless RGB editor integration

The shared native Brightness/Contrast panel now offers RGB component controls
for all seven nonempty masks. `contrast_rgb_channels` takes an integer `channels`
mask (red=4, green=2, blue=1). Manual range/window/level/brightness/contrast drafts
prepare a detached native RGB preview while source pixels, display and history
stay unchanged. Closing the panel discards it. Apply transfers the same READY
job to guarded caller-thread publication; it commits the once-mapped preview
instead of mapping that preview again or acquiring the source a second time.
The committed snapshot becomes the next adjustment's baseline.

Range/component/target changes discard the old preview and schedule the newest
intent. All revision domains, owner/position changes, resource preferences and
close/cancel participate in invalidation. Cancel also suppresses an intent that
has not yet been submitted; a new adjustment or explicit refresh can retry.
`cancel_rgb_contrast_preview` and `refresh_rgb_contrast_preview` are exposed by
the same native host to C++ and optional Python UI. Virtual preparation runs on
native workers. Processing and record admission remain bounded.

The ordinary canvas and linked XY/XZ/YZ views use 0..255 for mapped RGB previews,
independently of the live display range. This prevents a second display mapping.
Background virtual XYZ preview uses the existing `async_viewport` setting;
legacy synchronous virtual XYZ previews are still rejected. The temporary
range override is restricted to mapped UInt8 RGB pixels. Generic Gaussian and
scalar preview range behavior is retained.

RGB target controls and Rebind review also offer an explicit plane-list editor.
It accepts `[[T,C,Z], ...]` with 1-based indices, up to 4096 unique planes in
selected order and 1 MiB of text. Native records keep zero-based positions.
Partial/invalid fields remain private until corrected; range targets remain
compact. Existing scalar panel target-policy admission is unchanged.

Recorded RGB Apply supports the recorder's strict replay and reviewed/explicit
rebind paths, preserving saved endpoint precision and component bits. Native
workflow/Run/Batch adapters are now available as described below. RGB source histograms and Auto are described below;
exact ImageJ slider rules, RGBA/floating Apply, and complete stack controller
semantics remain open. The selected-plane UI applies the native uniform mapping;
ImageJ uses processor-narrowed bounds on other stack planes. This extension and
paired Undo are explicit differences, not complete stack-controller parity.


### RGB workflow, Run and Batch

`NativeWorkflowOperation("rgb_contrast_apply", parameters, target)` adds RGB
Apply to the existing portable program format. Saved choices use an immutable
`original` RGB command record and empty parameters. Authored choices require
exactly `minimum`, `maximum` and integer component bits `channels` (1..7).
Authored choices can replace saved options while retaining the complete original
as provenance; the original is fully validated first. Typed filter options,
calibration scaling, volume execution and new-document targets are rejected.

```python
step = ij.NativeWorkflowOperation(
    "rgb_contrast_apply",
    {"minimum": 10.9, "maximum": 40.1, "channels": 4},
    ij.TargetRequest(ij.TargetScope.ALL_Z, roi=ij.RoiPolicy.WHOLE_PLANE),
)
program = ij.NativeWorkflowProgram([step])
prepared = ij.prepare_native_workflow(document.snapshot(), program.steps)
document.commit(prepared.begin_edit())
```

Whole-program preflight follows evolving shape/type/display descriptors and
uses providers that throw on sample access. It validates UInt8 RGB storage,
resolved ROI/axis intent, record/metadata and processing budgets before any
processing reads. RGB Apply contributes its fixed 0..255 reset range to later
steps. Execution resolves against actual preceding-stage pixels and publishes
the final program as one paired pixel/display transaction. Empty masks retain
the native no-change/no-IO extension. Disabled steps keep valid historical intent
without requiring destination storage or target compatibility.

The shared Run review recognizes recorded RGB Apply, preserves target policies
and exposes Minimum, Maximum and RGB component bits through Edit parameters.
Invalid fields remain private; editing requires affected steps to be confirmed
again. The original recorder history stays unchanged. Session Batch uses the
same operation, common/per-input program transport and selected/all-document
policies. Preparation creates no final outputs; each output uses existing guarded
publication and failure/cancellation cleanup. Input session bytes and unrelated session
metadata/results/workflow remain intact.

This workflow operation retains the native uniform independent-plane profile.
Exact ImageJ stack narrowing and broader calibrated reset/
slider/controller behavior remain open.

### RGB raw histograms and Auto

The C++ `rgb_histogram.hpp` API and Python `accumulate_rgb_histogram` expose
raw 256-bin UInt8 RGB histograms for resolved T/C/Z targets and area membership.
Choose Intensity, Red, Green or Blue. Intensity uses ColorProcessor's ordered
weighted sum, plus 0.5 and Java integer narrowing. Finite weights are neither
normalized nor clipped; invalid member indices refuse the operation. Invalid
projected indices outside the ROI are excluded, while all acquired RGB storage
samples must still be valid bytes. Full-plane projected extrema are a native
extension and can lie outside byte bins for excluded pixels.

`rgb_auto_contrast_from_histogram` makes the byte-histogram decision without
reading or editing a source. It validates the raw histogram and previous divisor,
then reproduces ImageJ's repeated-click cycle, dominant-bin exclusion, area-extrema
fallback and reset. It does not create a captured Auto command. Apply records the
resulting range and component mapping through the existing RGB command format.

The shared ImGui panel submits leased histogram jobs over its captured baseline
and mapped preview. Primary-component plots use the whole plane; combined
component plots use weighted intensity and active area membership. Auto always
uses baseline weighted intensity, irrespective of the selected component. It
uses the active area for statistics, then previews whole-plane mapping on the
selected native target planes. Non-area selections use whole-plane statistics.
Conversion preference weights feed both consumers. Repeated Auto starts from
the captured baseline and carries the divisor; Reset clears the cycle. Cancel,
close, a changed source/intent or superseding weights prevent stale publication.
Auto and plots do not edit document pixels or history; Apply reuses the current
READY mapping and publishes its existing guarded paired transaction.

Admission performs no sample reads or callbacks. Jobs freeze options, targets
and limits before callbacks; the existing HistogramJob scheduler handles
cancellation, completion and resource leases, including escaped typed/base result
aliases. Python receives owning result copies. Conservative admission includes
metadata, geometry, the acquired RGB plane and histogram storage; external source
caches and total process RSS are outside that bound.

Reference cases execute pinned ImageJ 1.54r ColorProcessor/ColorStatistics,
ContrastAdjuster.plotHistogram and autoAdjust methods. The harness bypasses AWT
construction and scrollbar presentation only; upstream processing sources are
not patched. Source and cached class identities are pinned separately. The
reference spans four inputs, four weight policies, five ROI forms, seven
component masks and eleven previous divisors (6,160 rows).

Component snapshot promotion is now connected as described below. ImageJ
commits its active preview once and narrows bounds on other stack planes;
native multi-axis mapping remains an explicit uniform-plane extension. Slider/log-plot/Set/reset/calibration,
RGBA/floating Apply and complete platform/resource acceptance remain required.

### Promoted RGB mapping sequences (core service)

`rgb_contrast_sequence_job.hpp` and Python `submit_rgb_contrast_sequence` compose
1..64 captured RGB mappings, subject to cumulative pixel, record and working
limits. Every command must refer to the same exact immutable owner/data,
revisions, geometry and display. Different sources, staged edits and detached
view changes are refused before processing reads. Each stage has its own frozen
compact ranges or explicit positions and ROI policy. Later stages see the prior
stage's pixels; unchanged virtual planes stay shared. An empty area retains the
native no-change/no-read extension.

This service supplies the ordered processing needed for ImageJ's component
snapshot promotion. A repeated endpoint change should replace the current draft
command; a component change promotes that command to the retained prefix and
starts a new 0..255 draft. It must not append every endpoint adjustment. Auto
reads weighted intensity from the promoted baseline before creating its next
whole-plane mapping. The core reference cases cover both promotion resets,
replacement endpoint choices, overlapping component bits, selected areas and
Auto after a second promotion. The reference invokes actual ColorProcessor
snapshot operations at the boundary used by ContrastAdjuster.itemStateChanged;
Choice/AWT event delivery is outside this core qualification.

The existing ContrastApplyJob retains cancellation, escaped snapshot/Dataset/
plane leases and guarded caller-thread publication. A sequence records an
existing NativeWorkflowProgram containing full historical RGB commands and
explicit frozen targets. Portable replay sees preceding-stage pixels and one
paired Undo/Redo transaction. Scalar and RGB command wire profiles are unchanged.
Conservative admission sums retained per-stage planes, metadata and record work,
and cumulative visited pixel capacity. Source/provider caches, frontend copies,
publication/history staging and total process RSS remain external.

The shared panel now uses this service as described below. Complete source/
geometry/navigation transitions, exact ImageJ active/stack narrowing,
slider/log-plot/Set/reset/calibration and full acceptance remain open.

### Shared RGB component promotion

Choosing a different RGB component keeps the current draft as a promoted prefix
and starts the new component at 0..255, resetting its Auto divisor. Re-selecting
the current component changes nothing. Identity 0..255 drafts need no extra
processing stage. Subsequent endpoint edits replace the current draft, while
Reset discards that draft and retains the promoted prefix. The panel opens a
byte RGB snapshot at 0..255; a stored display range does not implicitly map those
already captured samples again. No document pixel or display edits occur until
Apply.

The full next sequence is admitted before a component choice retires its old
preview. Refusal leaves its component, range, prefix and READY job intact.
Pending endpoint intent is captured in order, and superseded workers cannot
replace later choices. Auto first prepares the promoted prefix on a native
worker, then histograms its weighted baseline with the existing area policy.
The final whole-plane component mapping is prepared separately. Cancel, close,
new range/component/weight/source intent cancels the dependent work. Resource
refresh and compatible presentation changes retain the whole-plane policy.

Apply transfers the current READY job, including its original job identity,
into the existing guarded publisher. A promoted sequence records one complete
portable program; a single mapping keeps its existing RGB command record. Run,
parameter review and session Batch can consume those programs through their
existing adapters. One Apply creates one paired pixel/display history entry;
Undo/Redo preserves prior unrelated history and the captured display state.

Ordinary and XYZ rendering use the union of the bounded frozen targets, rather
than only the current draft target. An earlier adjusted plane remains visible
when a later draft excludes it. Compact/explicit membership remains metadata;
worker requests own immutable value captures and never call ImGui. Current-Z
membership continues to gate linked views under the existing native preview
policy. Neutral RGB display prevents applying the display range twice.

Camera, compatible display properties and unrelated title changes re-capture
commands against the fresh owner revisions, preserving component mappings,
current endpoints and target choices. External pixel changes, active T/C/Z
navigation or changed selection/positioned-ROI geometry clear the native
component prefix. Preserving ImageJ snapshot state through those latter
transitions still requires work and is not claimed here.

The editor reference cases drive the same 378 actual ImageJ promotion sequences
through reset, initial/moved endpoints and promoted Auto, compare displayed and
published byte hashes, and replay each complete program. Other checks exercise
pending drafts, current-component re-selection, compatible metadata, canceled
promoted Auto, eager/virtual XY/XYZ target unions, budget refusal and Auto policy
after zoom/cancel/refresh. The standalone native suite uses real component and
Apply widgets and checks the promoted pixels, reset range and paired history.
These cases do not complete broad widget, stack, reset/calibration or platform/
resource acceptance. G057 remains in progress.

Remaining integration and qualification:

1. Extend actual range/text/positioned-ROI widget and publication failure-injection
   coverage beyond the current real C/Z/ROI radio, Apply/Rebind and workflow mapping
   field/prepare/Apply tests. Broader workflow outputs/operation combinations and
   resource/platform acceptance remain open.
2. Extend mixed workflow acceptance and exact RGB controller behavior.
   Extend widget/guard coverage beyond the RGB controls now qualified above.
   Preserve scalar V1 records and prevent a mapped preview from being mapped
   twice. Qualify current/stack controller behavior: ImageJ commits its active
   preview and uses processor-narrowed bounds on other stack planes. The native
   uniform multi-axis transform and paired Undo are explicit extensions.
   Keep floating/RGBA Apply unavailable until separately qualified; broader
   reset/calibration semantics and platform/resource use remain open.

G057 remains in progress; scalar editor Apply does not close full ImageJ/Fiji parity.


### ImageJ RGB entire-stack preparation

`estimate_rgb_contrast_stack` and `prepare_rgb_contrast_stack`, declared in
`imagejxx/rgb_contrast_stack.hpp` and exposed to Python, accept one to 64 typed
`RgbContrastStackStep` values. Supply promoted active-processor mappings followed
by the current mapping; replace repeated endpoint drafts. The final step's
component and Java-int-narrowed bounds map every other C/Z/T plane. Earlier
promoted stages affect only the active plane. A step's `whole_plane` flag models
an active Auto-style mapping while the restored source selection still applies
to the other planes. This differs from uniform multi-axis Apply.

ImageJ resets the active processor ROI for non-area selections. A straight line
therefore maps its whole active plane and its integer bounding rectangle on
other slices. Points and open polylines/angles use the ImageJ processor mask
inside their bounds, with the rest of the active plane included. Point drawing
size does not widen the processing mask; polygon coordinates use binary32.
Area selections retain their existing membership. Original selection, previous
selection, positioned records and unrelated metadata survive preparation.

The estimator reads no samples and invokes no callbacks. It checks source
metadata before resolving a target, cumulative mapped pixels and conservative
owned output/mask/geometry/transaction work. Preparation freezes inputs before
callbacks, checks cancellation, reads each affected plane once and returns a
private paired pixel/display transaction. Commit checks all captured revision
domains and provides one Undo. Empty membership is an explicit native no-read,
no-change extension and still reports terminal progress. Source/provider caches,
caller copies, publication/history staging and total process RSS remain external
to this estimate. RGB UInt8 finite storage is required. Oversized non-area
integer bounds are refused.

The reference driver calls ImageJ 1.54r's actual private `applyRGBStack`, including
its real macro-confirmed GenericDialog and post-commit setup. Only plot/label/
scrollbar presentation is bypassed; upstream processing code is unmodified.
Its 5,292 cases combine seven endpoint pairs, all seven component bit masks,
nine selection modes, three scientific anchors, optional promoted mappings and
whole-active-plane policy. They compare complete stack and active-plane hashes,
including fractional endpoints, saturation, degenerate ranges and extremes.

This is a synchronous preparation service. Captured command records, leased
background scheduling, portable replay and actual ImGui stack controls still
need to adopt these semantics. Existing UI probes provide regression coverage
for the unchanged panel; they do not certify the new stack interaction. G057
and full ImageJ/Fiji parity remain in progress.
