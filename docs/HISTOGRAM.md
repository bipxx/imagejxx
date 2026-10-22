# Native Dataset histograms

`accumulate_dataset_histogram` calculates a histogram independently of threshold
estimation. C++ and Python share the implementation. Requests capture one T/C/Z
plane and an optional area ROI. `ProcessorRaw` follows the existing raw processor
256-bin convention; `UniformRange` accepts 1–65,536 bins and finite lower/upper
bounds. Counts include both endpoints. Underflow, overflow, quiet NaNs and empty
finite populations have separate counters. See `dataset_histogram.hpp` for the
precise raw mapping, sample validation and bounded working-storage contract.

```python
request = imagejxx.DatasetHistogramRequest()
request.binning = imagejxx.HistogramBinning.UniformRange
request.bins = 128
request.range = (0, 4095)
result = imagejxx.accumulate_dataset_histogram(dataset, request)
print(result.mean, result.stddev, result.sum)
text = imagejxx.dataset_histogram_csv(result)
```

`minimum`, `maximum`, `mean`, population `stddev` and `sum` describe every finite
ROI sample, including samples outside an explicit bin range. Statistics use raw
samples, not bin centers. Mean and variance use double-precision online Welford
updates; sum uses double-precision sequential addition. They are floating-point
statistics, not exact real arithmetic. Empty finite populations return absent
mean/stddev/extrema and zero sum. NaNs do not contribute. Statistics are collected
during the existing validation/extrema pass and do not acquire another plane or
retain a sample vector. Supported source storage remains scalar UInt8, UInt16,
and canonical widened Float32 with its declared quiet-NaN policy.

`dataset_histogram_csv(result)` returns a detached string with columns
`bin,lower,upper,count`, one row per bin including empty bins, and a final newline.
It uses the classic locale, `max_digits10` edge precision and exact UInt64 count
formatting. Collapsed edges and constant distributions remain representable.
Malformed arrays, nonfinite/decreasing edges and more than 65,536 bins refuse.
Formatting performs no source reads or file writes. The caller owns the returned
text and its eventual publication; string/stream allocation is outside the
histogram accumulator's working estimate.

Choose **Analyze > Histogram...** in either ImGui launcher for the native panel.
Opening captures the active document, its data/metadata/selection revisions and
initial T/C/Z, without reading pixels. The one-based T/C/Z fields choose one XY
plane. Live navigation does not retarget those fields. Calculation requires the
captured source to be active and unchanged. A metadata, pixel or selection edit
requires reopening the panel; display navigation remains allowed.

**Target... > Use scientific target** enables the shared Current/All/First/Last/
Step controls for T/C/Z. Current uses the opening position, independently of the
single-plane text fields and later navigation. Choose whole planes, the captured
active area selection, or positioned area ROI unions. Overlaps count once and
unmatched planes contribute no members. Done closes the target window while
retaining its draft. Calculate pools that frozen grid with the panel's existing
processor-raw/custom bin/range settings. Invalid or unfinished grid fields disable
calculation while the last successful result remains available for export.

Pooled results show their plane count, sampled ranges and ROI policy. These labels
belong to the calculated result, even after further drafts or a mode switch.
Turning scientific mode off restores the single-plane fields and their previous
drafts without recalculating or deleting the pooled result. In scientific mode,
the single-plane T/C/Z and active-ROI controls are disabled; their values do not
override the grid or its ROI policy.


The captured active area ROI is used by default. Uncheck it for the whole plane.
A nonarea selection is refused rather than silently flattened or converted.
**Calculate** uses the processor-raw convention by default. Enable **Custom bins
and range** for 1–65,536 bins with finite inclusive lower/upper bounds. Invalid
or unfinished drafts disable calculation. They preserve the last successful
result, whose plane, edges, counts and statistics remain explicitly identified.

**Log count display (log1p)** changes only plot heights. Hover a bar for its raw
bin interval and integer count. The panel shows sample/NaN/underflow/overflow
counts, raw-sample mean, population SD, minimum, maximum and sum, plus the first
most-populated bin. This mode-bin interval is not an exact raw-value mode for
continuous data; tied bins choose the first. **Export last result CSV** captures
the raw result when clicked, then opens the shared save browser. Later drafts
cannot change that captured text. Saving uses the existing atomic file writer
and overwrite prompt. Source closure releases the source snapshot and disables
calculation while the detached last result can still be exported. Closing the
panel releases both source and result. Existing legacy histogram routes remain
available for legacy image types outside the native service's supported domain.

The shared Window action API provides `show_histogram`, `configure_histogram`,
`configure_histogram_target`, `calculate_histogram`, `export_histogram_csv` and `close_histogram`. Show/calculate/
close accept an empty object. Configure accepts Boolean `custom`, `roi`, `log`, `scientific`
and draft text fields `t`, `c`, `z`, `bins`, `lower`, `upper`; position text is
one-based just like the UI. Invalid drafts are allowed, but unknown fields or
wrong value types refuse atomically. Export accepts only a string `path` and
writes the last successful raw result. The legacy `histogram` action retains its
original plotting behavior for compatibility.

`configure_histogram_target` accepts only a `target` object using the existing
strict target syntax: `scope` is `current_plane`, `all_z` or `ranges`; `t`/`c`/`z`
are zero-based half-open `{start, stop, step}` ranges when scope is `ranges`;
`roi` is `whole_plane`, `active_selection` or `positioned_rois`. It validates and
captures a range draft without reading pixels, and enables scientific mode.
Explicit position lists, volume execution and new-document destinations remain
available only where supported by the native APIs; this panel accepts plane
grids with the in-place destination field and never edits image data.


The calculation is currently synchronous and bounded by native limits and the
processing-memory preference captured at opening. Scientific mode admits all XY
pixels across the target planes, including ROI exterior and unmatched planes,
against the default 16 Mi-pixel total limit. Frontend plot
heights, diagnostics, retained results, CSV text and allocator/provider/RSS costs
are not a total memory cap. Histogram parity remains open for calibrated/color
and other numeric domains, analysis recording,
asynchronous execution and complete reference/resource/platform acceptance.

## Pooled native C/T/Z targets

`accumulate_target_histogram` pools a frozen `ResolvedTarget` without flattening
or concatenating its planes. The C++ core and thin Python binding support Current,
All Z, compact sampled T/C/Z ranges and explicit unique positions. Whole-plane,
active area selection and positioned area ROI unions use the existing target
resolver. Overlapping ROIs count a pixel once; a plane with no matching positioned
ROI contributes no members. Nonmatching nonarea ROIs are ignored; a referenced
nonarea ROI refuses before reading pixels. The target must use plane execution
and the `IN_PLACE` destination field; analysis itself never modifies the source.

```python
request = imagejxx.TargetRequest(
    imagejxx.TargetScope.RANGES,
    t=imagejxx.AxisRange(0, 2),
    c=imagejxx.AxisRange(1, 2),
    z=imagejxx.AxisRange(1, 7, 2),
    roi=imagejxx.RoiPolicy.POSITIONED_ROIS,
)
target = imagejxx.resolve_target(document.snapshot(), request)
options = imagejxx.TargetHistogramOptions()
options.binning = imagejxx.HistogramBinning.UniformRange
options.bins = 512
options.range = (0, 4095)
estimate = imagejxx.estimate_target_histogram(target, options)
result = imagejxx.accumulate_target_histogram(target, options)
text = imagejxx.target_histogram_csv(result)
```

ProcessorRaw uses 256 bins and the same mapping as the single-plane service,
with observed extrema taken over the pooled finite ROI population. UInt8 uses
0..256, UInt16 uses pooled min..max+1, and Float32 uses pooled min..max. UniformRange
uses its supplied inclusive endpoints and 1–65,536 bins. Unbinned statistics use
every finite ROI member, including explicit-range underflow/overflow, in target
order then XY scan order. Full-plane extrema and finite/NaN counters cover every
acquired target plane independently of ROI membership. All acquired samples are
validated, including outside the ROI and on unmatched planes. Quiet NaNs are
counted and excluded; source payload bits are preserved. Other sample policies,
color data and noncanonical or unsupported numeric storage refuse.

Explicit ranges and UInt8 raw binning acquire each target plane once. Nonconstant
observed UInt16/Float32 populations acquire each plane twice, retaining one plane
at a time. Empty/constant observed populations need only the first pass. Empty
raw histograms own finite 0..1 edges with zero counts and absent mean/SD/extrema;
constant Float32 raw histograms have equal edges and put every finite member in
bin zero. Providers must remain immutable between passes; range/count checks are
not a complete content-change detector.

`DatasetHistogramLimits.max_pixels` counts all pixels in every target plane,
including ROI exterior and unmatched planes. Metadata-only admission checks this
before reads or execution callbacks. Its working estimate includes serial source/
mask canvases, returned bins, conservative referenced-geometry/raster scratch,
compact target records and explicit-position capture. Source/provider cache,
history, allocator bookkeeping, Python argument/getter copies, opaque callback
internals, CSV text and total process RSS remain outside that estimate. Existing
ROI raster/union routines are not a certified wall-time or fine-grained cancellation
bound. Execution checks cancellation around plane acquisition and during sample/
edge kernel stages. Progress is 0/1 then 1/1, with native stage progress available;
canceling completion still refuses the detached result.

Target, options and callbacks are captured before execution callbacks. A callback
may navigate or edit the live owner without retargeting the immutable analysis.
Returned bins/options/estimates/target records are detached; results retain no
source or provider handle. `target_record` identifies the original source, data
revisions, ranges/positions and ROI policy. It is provenance, not a complete ROI
geometry file or a replayable histogram command. CSV shares the exact raw-bin
formatter with the single-plane service.

The standalone ImGui panel now uses this service for its scientific target mode.
Both launchers have actual-input grid/ROI/bin/log/export checks. These bounded
functional checks do not establish complete upstream, resource or real GPU/
platform acceptance, nor completion of remaining histogram domains and workflows.

## Saved histogram defaults

Both launchers use the native **Histogram** group in Preferences. A successful
Calculate remembers these defaults; they are applied when a histogram panel is
opened again. Opening, changing drafts, switching modes or closing the panel
does not itself save those drafts. A later Calculate also remembers log changes.

| Setting | Initial default | Consumer |
| --- | --- | --- |
| `histogram_custom` | false | Explicit bins/range versus processor raw |
| `histogram_bins` | 256 | Custom bin count, 1–65,536 |
| `histogram_lower` | 0 | Inclusive custom lower bound |
| `histogram_upper` | 1 | Inclusive custom upper bound |
| `histogram_log` | false | Log1p plot heights |
| `histogram_use_roi` | true | Single-plane active-area selection toggle |
| `histogram_scientific` | false | Scientific target mode on reopening |
| `histogram_roi` | active_selection | Scientific whole/active/positioned area policy |

Single-plane and scientific ROI defaults are independent. Reopening scientific
mode starts at **Current** T/C/Z for the newly captured source. Numeric positions,
First/Last/Step ranges, ROI geometry and export paths are never global defaults.
The last custom bin/range defaults are updated only by a successful custom-range
calculation; processor-raw calculations preserve them, including when hidden
custom fields contain invalid unfinished text. Derived raw histogram extrema
remain local presentation values.

Bounds form one validated setting pair and must be finite with lower < upper,
even while custom mode is off. Use a single `Settings.update` patch to change
both when an intermediate value would be invalid. Recovery migration accepts
valid supplied bounds together; an invalid pair falls back together to 0..1,
produces a warning and retains the existing implicit-write protection. Resetting
the Histogram group resets all eight defaults while preserving other groups.

The native calculation and result are staged before saving preferences. An I/O
save failure leaves the prior result and remembered in-memory defaults intact.
Files protected after recovery or a newer-version load retain their exact bytes;
as with existing implicit saves, the successful result and session defaults can
still update. Unknown preference values, extra data and root extensions survive
normal saves. Result publication after saving uses allocation-free ownership
moves, including on Windows. This is a functional preference/result boundary,
not a transaction across external source providers or a complete memory/RSS cap.

## Captured native histogram commands

`imagejxx/histogram_command.hpp` and the Python API capture histogram intent
without reading source planes. The immutable command retains a source snapshot,
resolved T/C/Z target, ROI policy, raw/custom binning and a log-count presentation
flag. Record V1 uses schema `imagejxx.histogram-command` and profile
`raw_target_histogram_v1`. It contains no sample arrays, CSV path or preferences.

```python
import json
import imagejxx as ij

request = ij.TargetRequest(ij.TargetScope.ALL_Z,
                           roi=ij.RoiPolicy.POSITIONED_ROIS)
options = ij.TargetHistogramOptions()
command = ij.resolve_histogram_command(owner.snapshot(), options, request,
                                       log_counts=True)
saved = json.loads(json.dumps(command.record()))
replay = ij.histogram_command_from_record(owner.snapshot(), saved)
result = ij.execute_histogram_command(replay)
csv_text = ij.target_histogram_csv(result)
```

Strict replay requires the recorded document identity, source shape, scalar
pixel type and floating sample policy, plus target-dependent data revisions.
Display navigation does not change the frozen plane positions. Active-selection
and positioned-ROI targets depend on selection revisions; whole-plane targets
do not. Records are revision-bound intent, not portable ROI geometry files or
pixel-content attestations. The original source must still be available for
strict replay. A captured live command can execute its retained immutable
snapshot even after the owning document changes.

`rebind_histogram_command(destination.snapshot(), saved, new_request)` first
validates the entire original record, then captures the explicitly supplied
destination target and its ROI geometry. Original numeric binning and log choices
are retained; defaults are never consulted and numeric ranges are not converted
between storage types. A rebound command has a new source-bound record.

`HistogramCommandLimits.processing` applies the existing target histogram pixel
and working limits. `max_record_bytes` defaults to 8 MiB and bounds conservative
metadata traversal/wire storage. Admission reserves target-record storage plus
64 KiB for command fields, before creating a record. Decoding walks metadata
under a byte/depth budget before schema decoding. `estimate_histogram_command`
includes this additional record allowance in its working estimate; the result's
own estimate describes the lower-level target accumulation. Python conversion
and getter copies, source/cache storage, allocator overhead, opaque callbacks,
CSV formatting and total process RSS remain external. Results retain no source
handles. Cancellation and callback failures discard work without source edits,
history changes, preference saves or file writes. Log affects presentation intent
only; it does not transform histogram counts.

## Recording and replay in both ImGui launchers

Enable **Record actions** in Workflow before Calculate. Each successful histogram
calculation records one native histogram command, including simple one-plane
calculations and scientific pooled targets. Save/load Workflow preserves these
entries. Changing drafts, log display, opening/closing a panel, exporting CSV or
a failed calculation does not add an entry. The last successful command is also
available in panel diagnostics, independently of the recording toggle.

**Replay histogram** checks the active source identity and relevant revisions,
then calculates the captured target and displays its detached result. Navigation
can differ. Replay does not save global histogram defaults; recording it again
adds one entry only after success. Its target remains frozen, including explicit
position order, until **Target... > Choose new target...** replaces it. Source
changes and malformed records preserve the existing result and workflow.

**Rebind histogram...** opens a review on the active destination source. It starts
with Current T/C/Z and whole plane; choose the intended destination planes and
ROI policy using Target, then Calculate. Recorded bins, numeric bounds and log
presentation remain fixed. Destination active/positioned ROI geometry is used;
no original geometry or camera position is silently transplanted. A successful
Calculate saves the used defaults and, when recording, appends a newly bound
command. An already-open panel's detached last result remains exportable while
reviewing the rebind. Explicitly closing a panel releases that result.

The calculation stages the result and command, validates the still-current
active source, reserves the workflow entry and saves preferences before
allocation-free result/workflow publication. Real preference-save failures
preserve prior defaults, result and workflow. Provider callbacks that change
source data or the panel draft refuse stale publication. Panel snapshots and
record captures use shared immutable ownership for a non-throwing final handoff
on Windows and Linux.

Captured histogram entries support individual replay and the reviewed native
Run/Batch path described below; opening review performs no processing.
The desktop now has explicit virtual-provider attachment, described below;
its default eager attachment continues to require an eager table.
The native API and shared workflow review below support histogram steps;
complete ImageJ/Fiji domains/conventions and qualification remain open.

## Native background histogram jobs

`imagejxx/histogram_job.hpp` and the Python API submit an immutable histogram
command to an existing `JobScheduler`. Workers run the C++ accumulator with
scheduler progress/checkpoints and cooperative cancellation. No Python processing
callback runs on the worker. The command snapshot stays alive until work retires;
successful READY result cells retain no source or provider handle.

```python
scheduler = ij.JobScheduler()
job = ij.submit_histogram(scheduler, command)
job.wait()                  # waits for READY or a terminal state
result = job.result         # raises on cancellation or failure
job.complete()              # acknowledge; no source or file publication
scheduler.shutdown()
```

`wait(timeout_ms)` and `wait_for(timeout_ms)` return whether work finished and
require a nonnegative integer timeout. `info` exposes queued/running/ready/
publishing/terminal state, native progress and reservation counters. Waiting
does not itself rethrow a worker error; reading the result or completing does.
`cancel()` and scheduler cancellation release queued captures immediately;
running work releases them when it reaches a checkpoint. Provider calls are
not forcibly interrupted. Virtual C++ providers must support immutable reads on
the scheduler's worker threads. Desktop virtual attachment is explicit, as
described below.

`complete(publication)` optionally invokes a callback on the calling thread,
with an immutable native result reference in C++, or an owning detached result
copy in Python. Cancellation can win until publication starts. Once Publishing,
cancel returns false. A publication exception fails the job and clears its
result; Python rethrows the original callback exception to that caller while
storing a plain native diagnostic for later job inspection, rather than retaining
Python traceback objects. Argument/callback capture errors before publication
leave an otherwise READY job available. Completing acknowledges the result; it
does not validate live document membership, active state or revisions. Callers
must stage their own publication and validate the command's source against the
current owner when their workflow requires current data.

`estimate_histogram_job` is metadata-only. It adds typed task/result holders and
captured option strings to the command working estimate. Native working limits
apply before queuing; the scheduler reserves the larger of that estimate and
`JobOptions.resource_bytes`. The document tag is always the command's owner ID.
Labels are captured; the default is `histogram`. An empty supersession key allows
independent jobs; an explicit key supersedes only within that owner ID.

In C++, `job.result()` returns a shared immutable result alias. Such aliases keep
the conservative whole working reservation alive after cancellation, completion,
dropping job handles or scheduler shutdown. Releasing the final alias releases
that reservation and wakes queued work. No plane buffers or source snapshots are
retained by the result. Python `job.result` and publication arguments are full
detached copies; they survive native cell cleanup without retaining its lease.
Dropping all job handles cancels their queued/running/ready work. A result cannot
be read from the cleaned native cell after completion.

Source/cache storage, opaque provider and publication internals, allocator and
scheduler container overhead, frontend argument/getter/result copies, CSV text
and total process RSS remain external to these declared reservations. This
native job infrastructure drives both ImGui histogram panels as described
below. Native workflow execution and session batching are described next; the
desktop workflow review is described in that section.


## Background calculation in both desktop launchers

Calculate and Workflow Replay now queue a native histogram job on the application's
shared scheduler and return immediately. The panel shows Queued/Calculating/
Cancelling status, stage progress and a Cancel calculation button. Processing
inputs and duplicate Calculate requests are disabled until work retires. The last
successful result stays visible and exportable while a replacement is pending.
The shared Jobs panel can also cancel the histogram ticket.

Only the creator thread polls a READY result, stages its detached bins/statistics,
validates the still-current source/draft, saves used defaults when appropriate,
and appends its captured workflow command. Recording is captured at submission;
changing the recording flag later does not alter that job's decision. Replay
still skips preference saving. A real preference-save or workflow-reservation
failure preserves the prior result, remembered defaults and workflow contents.
Source pixels and undo history are unchanged by successful analysis.

Panel closure, source closure, source data/selection changes, native document
activation changes and draft changes cancel or discard stale work. Source-view
navigation within the same dataset does not retarget captured planes. Cancellation
is cooperative at native checkpoints; the panel retains its last successful
result unless explicitly closed. Queued cancellation retires immediately.
Closing the Target dialog during work changes presentation only and stays closed
when the result arrives. Explicit-position replay captions preserve their order
instead of treating positions as a range grid.

Programmatic calculate_histogram and replay_histogram actions now submit
asynchronous work. Call poll() while pumping the UI, wait(), wait_jobs, or
wait_histogram to observe completion. The latter accepts only an optional integer
timeout_ms in 1..60000 (default 5000) and propagates histogram publication failures
to the caller. cancel_histogram accepts no parameters. The Python document-index
setter retains its existing busy guard; cancel/finish work before switching
through that setter.

Simple and scientific panels now use the same native target accumulator. Uniform
ranges and raw UInt8 acquire each selected plane once. Nonconstant raw UInt16/
Float32 targets require two passes, including a simple one-plane calculation;
empty/constant raw populations require one. Numeric bins/statistics/CSV retain the
native target contract. Frontend result copies and plot buffers remain outside
the native scheduler reservation. Existing unsupported legacy histogram domains
still use their legacy adapter; this does not establish full ImageJ/Fiji histogram
parity or complete resource/platform acceptance.


## Histograms in native workflows and session batches

`NativeWorkflowHistogram(name, target, original=None, limits=None, enabled=True, authored_options=None)`
adds read-only analysis to the ordered native workflow. Supply a complete
histogram command record as `original`, explicit `authored_options`, or both.
Without authored options it preserves the captured numeric/log choices; `target`
explicitly chooses the destination stage's planes and ROI geometry. The original
capture is validated before rebinding, rather than requiring its historical owner
to still exist. Target and sample-domain compatibility are checked against the
metadata of every preceding stage before any processing reads.

```python
target = imagejxx.TargetRequest(imagejxx.TargetScope.ALL_Z,
    roi=imagejxx.RoiPolicy.WHOLE_PLANE)
original = imagejxx.resolve_histogram_command(snapshot, request=target).record()
steps = [imagejxx.NativeWorkflowHistogram('before.csv', target, original),
    imagejxx.NativeWorkflowOperation('add', {'value': 5}, target),
    imagejxx.NativeWorkflowHistogram('after.csv', target, original)]
program = imagejxx.NativeWorkflowProgram(steps)
prepared = imagejxx.prepare_native_workflow(snapshot, steps)
for output in prepared.histograms:
    print(output['step_index'], output['name'], output['result'].mean)
```

The C++ equivalent is `NativeWorkflowHistogram` in `native_workflow.hpp`;
`PreparedNativeWorkflow::histograms()` returns detached result objects, each with
its step index, output name, rebound command record and `TargetHistogramResult`.
Python getters return owning copies. Histograms do not mutate the owner, advance
the detached editing stage, create undo entries, remember defaults or write files.
Mixed processing can still be committed as one guarded transaction; an
analysis-only proposal is an empty edit. Cancellation or a callback failure
aborts the entire preparation without publishing a partial result.

Programs retaining original histogram choices use the closed V4 grammar;
authored choices use V5 as described below. V1/V2/V3/V4 programs keep their
existing encodings when authored choices are absent. V4/V5 retain explicit enabled flags and typed filter
options on operation steps. A disabled analysis validates its original/portable
intent but does not check destination compatibility, read samples or emit output.
Output names are unique flat tokens shared with annotation exports, including
disabled steps. A `.csv` suffix is conventional; no extension is added.

Native session batching emits one CSV per enabled histogram per selected document:
`<output_stem>.d<one-based-document-index>.<name>`. It uses the same native CSV
formatter as individual histograms. Annotation exports and then histogram CSVs
precede the complete session for each input. All destinations are planned before
temporary files are created. Existing destinations and publication races preserve
foreign files under the existing new-files-only, partial-publication batch policy.
Input files, untouched documents and existing session results/workflow tables
remain preserved; histogram tables are separate CSV products. Common programs,
per-input program overrides and batch setup records can carry V4 intent.

Per-step histogram limits combine with the cumulative native workflow bound.
Admission conservatively charges each histogram's working estimate, retained
record/result and CSV formatting/copy scratch (512 bytes per bin plus fixed
allowance). Source/provider caches, allocator overhead, frontend copies and total
process RSS remain external. No new scalar/color/calibration domains are admitted.

Both ImGui launchers review recorded histogram entries and V4 programs through
**Run all** and native **Batch**. Each enabled histogram has independent plane,
range/stride/explicit-position and whole/active/positioned ROI choices, plus an
editable unique CSV token. **Use edited histogram choices** enables bin/range/log
authoring; turning it off restores the original numeric capture. The original
record remains unchanged in either case.

Confirm every step, then use **Prepare image preview** to inspect each histogram
plot and statistics alongside the prepared image without writing files or Results.
**Prepare complete workflow** runs the ordered analysis/edit steps on workers and
stages selected outputs. For Run, leave **Optional CSV file** empty for analysis
without file output, or choose a new `.csv` path with the field or Browse and
accept per-file publication. Batch writes a CSV per selected document using the
reviewed token. Duplicate/invalid tokens, unsupported stages and invalid targets
refuse before processing reads. Invalid/existing output paths refuse publication.

**Apply complete workflow** appends one summary row per enabled histogram to
Results on the creator thread, after revalidating the live source and reserving
all output rows. Rows include the step/name, finite count, raw extrema, mean,
population SD, sum, NaN and explicit-range outlier counts. Bin plots, a clipped table of exact per-bin counts/edges,
and unbinned statistics remain inspectable in the review, including after Apply. A mixed workflow has
one pixel undo entry; analysis-only Apply creates none. Undoing pixel edits does
not remove published analysis rows or files. Running a recorder table does not
duplicate its entries; use the existing edited-program controls to retain target
or token changes as a portable V4 program.

Preview, queued cancellation, source changes and closure publish no results or
files. Changing workflow context or processing choices invalidates prepared work.
Optional Run CSVs and annotation exports publish individually before the guarded
image edit. If a later file loses a publication race, earlier files remain listed,
the foreign file is preserved, and image/history/Results remain unchanged. Batch
retains its existing per-input partial-publication policy. Full upstream/source,
resource and platform qualification, desktop virtual-source adaptation and the
remaining histogram domains/conventions are still open.

Per-input batch unbinned statistics review/export is still open; current batch
histogram products contain bin counts/edges, while existing session tables are
preserved. Run displays and publishes the unbinned summary rows described above.


## Authoring workflow histogram choices

Both ImGui launchers let each histogram step use its recorded choices or explicit
new choices. After choosing the target, enable **Use edited histogram choices**,
choose **Explicit range and bin count** for 1-65,536 bins with finite low < high,
or disable that option for the 256-bin raw processor profile. **Log counts** changes
the plot presentation. Confirm the step and its successors again. Numeric edits
are independent of the plane/ROI request and do not remember standalone histogram
preferences. Statistics still describe all finite ROI samples, including explicit
range outliers. Invalid active choices refuse preparation without live effects.

Turning **Use edited histogram choices** off retains the original capture's
bin/range/log values. When a step was authored without a capture, choices remain
explicit and that reset switch is disabled. **Prepare edited program > Keep edited
program** stores authored choices as V5 and can reopen them for later execution;
restoring every recorded choice uses the prior V4 encoding again. Complete
preparation/Apply also uses current review choices without implicitly rewriting
the recorder table. CSV paths and file-publication consent remain separate.

The C++ `NativeWorkflowHistogramOptions` value contains `histogram` (the existing
`TargetHistogramOptions`) and `log_counts`. Assign it to the histogram step's
optional `authored_options`. The Python equivalent is:

```python
options = imagejxx.TargetHistogramOptions()
options.binning = imagejxx.HistogramBinning.UniformRange
options.bins = 128
options.range = (0, 4095)
choices = imagejxx.NativeWorkflowHistogramOptions(options, log_counts=False)
step = imagejxx.NativeWorkflowHistogram('histogram.csv', target,
    original=captured_record, authored_options=choices)
```

Omit `original` to author a new step from scratch with explicit choices. Omitting
both capture and choices is refused during program/workflow admission. Supplying
an original always validates it in full, even when overridden or disabled. No
historical capture is synthesized or rewritten. Python option getters and program
records are independent values; mutating them does not modify retained intent.

V5 histogram steps have an explicit `options` field: null retains the original,
or an exact object with `binning`, `bins`, `range` and `log_counts` supplies new
choices. Other V5 step kinds retain the V4 fields. Mixed programs can retain one
histogram and author another. Disabled steps validate portable options without
binding destination compatibility. The same source-independent option validator
serves commands and workflows, and metadata preflight checks every destination
stage before kernel reads. Native batch common/override programs and setup
records carry V5 and use the authored bins for existing per-document CSV output.
No additional pixel/sample/calibration domains or defaults are admitted.


## Batch histogram statistics review and export

Both ImGui launchers show a detached summary for every enabled histogram and
selected input document after Batch preparation. These are unbinned finite ROI
statistics from each ordered workflow stage: minimum, maximum, mean, population
standard deviation and sum, plus visited/finite/included/NaN/outlier counts.
Explicit-range outliers contribute to statistics. Empty finite populations have
absent extrema/mean/SD and zero sum. Bin centers are never used to reconstruct
statistics. Review rows do not append to the live or saved session Results table.

Enable **Export unbinned histogram statistics CSV** before preparing to publish
one `<output_stem>.histograms.csv` per input containing enabled histogram rows.
The default remains per-document bin CSVs and the complete processed session.
Inputs with no histogram rows produce no summary CSV. The summary includes
zero-based input/document/step indices, token, plane count, pixel type, sample
policy, requested bin/range/log choices and exact counters. Absent statistics and
raw requested ranges have blank CSV fields; finite values use round-trip precision.

Summary files use the same new-files-only consent, destination checks and output
limits as other batch products. Each input publishes its bins/exports, optional
summary, then complete session. Cancellation or a later conflict may retain
already published files; it does not publish the complete session for that input
or modify the live owner. Review remains available, and retry requires a new
preparation. Existing session tables and untouched documents are preserved.

The C++ `PreparedNativeWorkflowBatch::histograms()` and Python `.histograms`
expose `NativeWorkflowBatchHistogram` values without source snapshots or bin
arrays. Python returns owning copies, including copied options.
`native_workflow_batch_histogram_csv(rows, max_working_bytes)` formats these
values without opening paths. Set `NativeWorkflowBatchOptions.histogram_statistics`
(Python constructor keyword `histogram_statistics=True`) to include sidecars.
Retained summary storage and opt-in formatting scratch count against aggregate
batch admission; `encoded_bytes` still counts actual encoded output bytes.
Frontend copies, allocator overhead and total process RSS remain external.

Batch setup `.with_histogram_statistics(True)` returns a new immutable setup;
the Python constructor also accepts the flag. Enabled export writes closed V2
setup JSON with a required Boolean `histogram_statistics`. Disabled export keeps
the byte-compatible V1 encoding; V2 false canonicalizes to V1. Save/load restores
the choice without persisting publication consent. Supported histogram numeric
domains remain unchanged. Virtual sources, calibrated/color/other numeric
domains and complete upstream/platform qualification remain open.


## Virtual providers in the desktop

C++ `Window::addVirtualDocument(owner, max_view_bytes)` and Python
`Editor.add_virtual_document(owner, max_view_bytes=128*1024*1024)` explicitly
attach a native document backed by an immutable virtual provider. Providers must
support concurrent immutable reads from the creator/render thread and native
processing workers. This entry point also accepts eager documents. The original
`addNativeDocument` / `add_native_document` retains its eager-only admission and
creator-thread publication constraints. No Python processing callback is added.

Attachment and metadata projection acquire no pixels. `Dataset.has_eager_planes`
(C++ `has_eager_planes()`) discovers table availability without copying it or
reading samples. A virtual desktop State has metadata and no legacy eager frames;
native descriptors supply dimensions and Z navigation. Existing eager-only edit
and serialization paths retain their admission rules.

The ordinary viewer retains one decoded XY plane by pixel-source identity and
T/C/Z. Repeated frames and metadata/camera changes reuse it; navigation or source
replacement reads and validates a new plane before replacing the cache. Retained
immutable image handles keep prior buffers alive independently. Linked XYZ uses
the native regional XZ/YZ extractor and its existing rendering caches.

For virtual datasets, the positive integer `max_view_bytes` cap bounds each decoded XY/XZ/YZ image
buffer before provider calls. Orthogonal admission also checks full XY size,
because a provider may use the default regional fallback. It does not bound the
sum of caches, provider internal allocations, rendering buffers, allocator
overhead or process RSS. Large descriptors can still attach for metadata and
bounded native analysis even when a decoded view exceeds that cap.

Analyze > Histogram uses the same target controls, worker progress/cancellation,
record/replay and guarded creator-thread publication for virtual sources.
Histogram processing limits are separate from the view cap. Provider failures
preserve the prior histogram result, workflow and defaults. Unknown virtual
providers keep finite-only sample admission; quiet-NaN/IEEE storage permission is
not inferred. Scientific TIFF now has file-backed virtual import choices and regional reads;
see [virtual TIFF usage](SCIENTIFIC_TIFF_SERVICE.md#virtual-tiff-loading-p10u).
Other formats, tiled/asynchronous viewport rendering, virtual output-adapter
propagation and complete provider/resource/platform qualification remain open.
