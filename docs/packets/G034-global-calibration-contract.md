# Global calibration: observed semantics and implementation path

Status: native metadata, generic execution, dedicated factories and the new
explicit calibrated command envelope/admission/replay API are scoped verified.
Current ImGui command/recorder dispatch, live caches and persistence remain open. See [the explicit core API](../GLOBAL_CALIBRATION.md).
The thirteen-case [reference](../parity/reviews/G034-global-calibration-reference002.json)
comes from actual pinned ImageJ1.54r ImagePlus/Calibration execution. It does not
invoke the private Calibrator dialog or qualify all calibration functions.
Reference001 is retained unchanged as encoding-limited evidence: the JVM stdout
encoding replaced the normalized micro-sign spatial unit with ?. Reference002
escapes JSON Unicode before printing and preserves the actual U+00B5 unit; use002
for global-calibration comparisons. Numeric/density fields agree in both runs.

## Observed contract

ImagePlus keeps global and local calibration separately. setGlobalCalibration
copies the supplied calibration; effective getCalibration copies it again and
calls setImage for the destination. Mutating the supplied object or a returned
effective copy does not change the global value. Existing and newly created
images see the global calibration. A per-image ignore flag returns local
calibration. Clearing global restores the unchanged local state.

For an8-bit global straight line10+2*x, spatial pixel width0.5 and value unit OD,
the observed effective result is:

| Destination | Width | Density function | Value unit | Value at raw1 |
| --- | ---: | --- | --- | ---: |
| UInt8 | 0.5 | straight line | OD | 12 |
| Unsigned UInt16 | 0.5 | none | Gray Value | 1 |
| Float32 | 0.5 | none | OD | 1 |
| RGB | 0.5 | none | Gray Value | 1 |
| Locally signed16 UInt16 | 0.5 | signed16 line | Gray Value | -32767 |

Ignoring the global setting on the original UInt8 returns its local width2;
clearing returns local widths2 and3 for the two existing integer images. Spatial
and density adaptation are separate. Do not approximate global with a metadata
edit applied to every open document, or merely add a checkbox to the current
per-document intensity dialog.

## Current foundation and next integration seam

GlobalCalibration/EffectiveCalibration and Application Set/Clear/local/effective/
ignore/capture validation are implemented in the native core and Python binding.
The resolver matches the13 initial cases and25 additional known-source None,
16-bit function and custom-table cases in
[reference003](../parity/reviews/G034-global-calibration-reference003.json).
Detached snapshots/processing APIs still consume local calibration. Generic
Application-resolved targets now carry effective metadata and generation guards;
see [execution evidence](../parity/reviews/G034-global-calibration-execution-progress001.json).
No functional Global UI toggle is enabled.

The generic pipeline carries the minted capture through ResolvedTarget and
prepared-operation/job state without changing the original source snapshot.
The next packet extends that contract to dedicated consumer factories.
At Application publication, check its app identity/global generation/own bypass
generation under the same registry lock as existing owner/revision publication.
The standalone validator is read-only and is not a check-then-commit substitute.
In-place pixel edits preserve local calibration; derived outputs need the
operation's explicit effective-calibration output policy. Do not copy a synthetic
overlaid snapshot back to local history or reuse projection policy for every
conversion/operation.

Frontend caches need global/bypass generations in addition to local metadata
revisions, because global Set/Clear deliberately advances no document revision.
Cache a typed capture per relevant revision/generation rather than decode tables
or serialize full calibration on every frame. Wire display/XYZ/world distances,
measurements/profiles/histograms/projection and scientific IO before enabling the
shared control. Live source-bound plots need effective-context invalidation;
retained independent plot snapshots keep their captured data/context.

## Native core implementation order

1. Add a typed, immutable effective-calibration resolver beside existing
   SpatialCalibration/ImageJIntensityCalibration. Inputs are the original
   DocumentSnapshot, optional application global value, its source bit depth,
   destination ignore policy and calibration generation. Outputs are spatial
   and intensity values plus generation; no plane reads or dataset materializing.
   Preserve the source bit depth even for a None density record: dtype changes
   can affect its value unit. Reuse current typed function/lookup/none codecs.
   projected_imagej_intensity_calibration is explicitly a projection-output
   policy, not a generic dtype/global adapter; do not route global through it.
2. Own global value and generation in Application, with explicit copy/set/clear
   and local/effective read APIs exposed through existing Python bindings.
   Independent Application/editor sessions must not share a process-static
   C++ singleton. Document local calibration, pixel data and undo remain intact.
3. Capture the effective value once in target/operation requests. Retain the
   original owner and revisions independently; do not manufacture a snapshot
   whose overlaid metadata can be accidentally committed to local history.
   Application publication validates the captured calibration generation along
   with source/activation guards. Global changes invalidate stale prepared work.
4. Wire existing consumers before exposing a functional UI toggle: display and
   XYZ geometry, calibrated selection distances, measurements/profiles,
   histograms/thresholds, projection/derived output, fitting/plot captions and
   scientific I/O/session workflows. Every consumer must deliberately choose
   local or effective state. Reuse the shared resolver rather than per-panel
   fallback rules. Explicit standalone snapshot APIs keep their local contract.
5. Expose the Global calibration option through the shared ImGui calibration
   controls and Python facade. Editing local state while global is active,
   disabling global, source closure, new documents and per-document bypass
   require original-reference comparisons. Keep modeless jobs and stored
   snapshots bound to their captured generation and immutable values.

## API reuse map and completion gates

| Concern | Easiest existing path | Required proof |
| --- | --- | --- |
| Spatial values | SpatialCalibration in dataset.hpp | Axis/origin/unit/time/direction mapping; ImageJ pixel origins converted to world-space native origins |
| Density | ImageJIntensityCalibration codecs/functions | Original dtype adaptation, signed16 precedence, Float32 unit retention and lookup/function cases |
| Ownership | Application + existing document revision/activation guards | Set/clear never bulk-edits local documents; no source-provider retention |
| Commands | ResolvedTarget/prepared command paths | One effective capture, generation mismatch refusal, correct derived calibration |
| UI/Python | Existing Window/controller actions and bindings | Both actual hosts toggle/use/clear, new/open documents, cancellation and bypass |
| Persistence/replay | Existing session/workflow codecs | Explicit schema/migration and local/effective provenance; no silent local-state overwrite |

Extend the initial observer before claiming complete global semantics: a16-bit
source global applied to8/16-bit targets, custom tables/nonlinear/None/unit-only
records, signed16 variants, local edits while active, ignore/clear transitions,
spatial/time fields and original Calibrator/Set Scale UI. Match downstream
pixels/metadata/measurements, source/history invariants and stale-job behavior.
Full supported-platform/reference and comparative performance gates remain open.
The metadata foundation is verified; generic target/job/publication and scaled
Gaussian integration is scoped verified by execution-progress001.
The remaining consumers and full global workflow are not yet certified.

## Next consumer packet after generic execution verification

1. Application.prepare_axis_plane_export and prepare_axis_range_* currently
   delegate to snapshot-oriented preparation. Capture an application context once
   and pass it through those existing native factories; preserve pure snapshot
   overloads. Attach the same guard to the resulting prepared target before
   admitting/publication. Do not reconstruct global state from serialized data.
2. Axis transforms must remap effective spatial calibration with the existing
   axis remapper, while retaining the original local source snapshot for history
   and revision checks. Projection must use its own existing density-output
   policy on the captured effective input. Do not replace their scientific
   output policies with the generic same-storage filtering rule.
3. Add direct output metadata/pixel and stale-generation tests for each factory,
   then exercise original source/calibration transitions. Reuse installed-core
   consumers and one compiler cache/runtime; batch public-header changes before
   rebuilding. Keep detached overloads and replay boundaries explicit.
4. Only after these native consumers agree, add generation keys to display/XYZ,
   measurements/histograms/profile/live-plot caches. Retained independent plots
   keep captured values. Verify both actual ImGui hosts before exposing Global.

## Dedicated consumer implementation and next command seam — 2026-10-28

The working tree implements all five Application axis factories and a new
Application.prepare_imagej_projection through one private captured-target adapter.
Original source/transaction/provenance stays local; a calibrated Dataset view
feeds existing axis geometry and sampling kernels. Complete contributor targets
retain the original capture's guard. Projection keeps its existing density/output
policy and charges global record/units before processing. Three Python axis
bindings now preserve the application seam. Scoped runtime/installed SDK and
original axis/projection regression verification now passes; see
[consumer evidence](../parity/reviews/G034-global-calibration-consumers-progress001.json).

The remaining command/UI work must use these factories without resolving current
application state on worker threads. Current Window routes are separate:
publishAxisExtraction/publishAxisOrientation/publishAxisSpacing/publishSlicerResize
resolve source-only command records, and project_imagej resolves then queues a
ResolvedImageJProjectionCommand. Their estimates, derived records and replay
checks still describe local calibration. Passing global pixels through an old
local-derived record would misstate geometry/admission and is not an acceptable
shortcut. Keep schema1 snapshot replay local and preserve its fingerprints.

| Next responsibility | Shortest path | Required checks |
| --- | --- | --- |
| Frozen command capture | Capture the Application target on the caller/UI thread before callbacks or enqueue; pass that immutable capture to the existing worker/factory. | Change global/bypass while queued and while processing; refuse publication without local history or membership mutation. |
| Command estimates and output geometry | Reuse the same effective Dataset view and projection estimator as preparation. Batch command ABI seams in one public-header build. | Estimate/prepared shape, affine, units and caps agree for XZ/YZ, transpose/reverse/flip, scalar/RGB, spacing and Slicer resize. |
| Serialized provenance and replay | Add an explicit effective-context record policy; retain original local source identity separately. Never serialize a live application identity or silently reinterpret schema1 as global. | Old records unchanged; detached replay remains local. New context records require deliberate live-context rebind/validation and lossless density words. |
| Both shared ImGui hosts | Reuse controller queue, candidate staging and atomic Application publication. | Actual controls create/use/clear/bypass; cancellation, stale refusal and undo/derived-owner state agree in C++ and Python launches. |
| Display and analysis consumers | Add captured calibration plus applicable generation keys to display/XYZ/distance, histogram/profile/measurement and live-plot caches. | Global changes work without local revision changes; ignored documents avoid unrelated invalidation; independent plot snapshots retain their values. |

Do not enable Global until the command/admission/record and live consumer seams
agree. Native Application factory support alone is not full ImGui integration,
scientific I/O/session migration or original Calibrator UI equivalence. Existing
full platform and matched speed/memory gates remain open.

Built-module consumer diagnostic:61/61 pass after correcting one new reader
assumption. Existing Slicer resize supports finite scalar storage only; RGB is
still rejected in both snapshot and Application paths without source/history
mutation. This is negative compatibility coverage, not RGB Slicer support. Keep
RGB Slicer as an explicit feature gap and qualify it against the pinned original
before claiming that family complete. The reader preimage/failure diagnostics
are retained. No production source change or rebuild was needed for this fix.

## Current command handoff and UI execution specification

[Calibrated commands](../CALIBRATED_COMMANDS.md) now provide the additive envelope
and explicit current-capture replay/rebind API for all six dedicated factories.
[Verified evidence](../parity/reviews/G034-calibrated-command-progress001.json)
retains119 new cases, the exact2192-case installed union, old codec controls and
current SDK consumers. Existing public command classes/formats are unchanged;
the opaque new type/private descriptor bridges avoided a broad header rebuild.

The next implementation owns actual dispatch, not another record abstraction:

1. In Window live axis/projection actions, capture the target on the caller/UI
   thread before enqueue. For active global state, resolve the new command once,
   reserve its admitted bound, prepare from that frozen capture and stage/adopt
   through the current Application publication path. Never query new application
   state in the worker to fill missing record fields.
2. Preserve legacy local records and explicit detached replay semantics. Detect
   the new schema in recorder/replay/rebind dispatch; require the current capture
   and deliberate context rebind when values differ. Validate original claims
   before replacement intent. Guard-only no-global work must still become stale
   if Global is enabled before publication, without building/serializing an
   unnecessary density envelope every frame.
3. Audit Gaussian and other scaled/resolved consumers as well: the existing
   target-aware Gaussian resolver is a native seam, not complete UI/replay
   integration. Extend explicit context/provenance coverage where needed; never
   pass effective calibration through an old record that claims local geometry.
4. Add generation keys and typed effective values to display/XYZ/selection-world
   distances, measurements/profiles/histograms/thresholds and live plot caches.
   Do not bump local document revisions or dirty history to force invalidation.
   Independent retained plot snapshots keep their captured data/context.
5. Only then expose the shared Global/bypass controls. Both actual ImGui hosts
   must cover create/use/clear/ignore, new/open documents, queued/callback state
   changes, cancellation, derived metadata, local history and new/legacy replay.
   Existing scalar Slicer refusal does not close RGB Slicer support.

Verification storage handoff: include required C++ report producers when adding
legacy UI readers. Batch capture-heavy modules sequentially under the unchanged
64MiB cap. A finally/retention exception must persist failed workflow state;
functional pass alone is not complete verification. Preserve original receipt
preimages when correcting a false passed state, then losslessly retain diagnostics
and retire scratch. Source/fixture expectations and per-case limits stay fixed.

## Shared desktop handoff verified — 2026-10-28

Six live axis/projection actions, captured dialog defaults/previews/admission and
individual recorder replay/rebind pass both actual ImGui hosts. Global/bypass
produce explicit envelopes; ordinary local work retains legacy records with
application guards. Strict legacy replay remains local. Physical error
acknowledgment, workflow scrolling, stale Apply and queued publication refusal
are covered at 119/128 frames each. Session coverage proves local source and
workflow blobs, not global-state migration.

Next: scaled Gaussian/live replay, then typed effective values and generation
keys in display/XYZ/measurement/histogram/plot caches before Global/bypass
controls. Bulk workflows, session/I/O, original/platform/RGB variants and matched
performance remain open. [Evidence](../parity/reviews/G034-calibrated-command-ui-progress001.json).
