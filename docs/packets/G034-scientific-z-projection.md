# Scientific Z projection

This packet extends the existing range/grouped projection workflow to scientific
TCZYXS datasets. G034 remains in progress. The current implementation includes the shared
metadata descriptor, finite native reducer and strict source command.
Prepared publication and UI remain unfinished.

## Implemented descriptor

`dataset_projection.hpp` provides `DatasetZProjectionRequest`,
`DatasetZProjectionLayout` and `describe_dataset_z_projection`. Both metadata
and Dataset overloads use one implementation. Neither reads pixels, calls a
user callback nor creates an output dataset.

| Input | Exact rule |
| --- | --- |
| `first`, `last` | Explicit zero-based inclusive Z bounds; no inferred T-to-Z conversion |
| `group_size` | Zero reduces the full selected range to one Z plane; positive sizes must divide it exactly |
| `active_t`, `active_c` | Captured indices, always validated against the source |
| `all_time_frames`, `all_channels` | Each independently defaults true; false retains only the corresponding captured index |
| `method` | Exact existing maximum/minimum/mean/sum/median/stddev names |
| `max_output_pixel_bytes` | Checked output double-buffer payload limit; not a total worker memory allowance |

Output axes retain T/C/Y/X/S independently; output Z is selected_count/group.
The contributor mapping returns source T/C/Z for an explicit output position and
within-group index. Scientific C is never confused with interleaved RGB/RGBA S.
A one-Z time series remains a time series. Min/max preserve the input pixel type;
the other methods describe the existing native Float64 policy. No ImageJ numeric
equivalence or nonfinite processing qualification follows from that descriptor.

Output spatial origin is the first contributing voxel's world coordinate under
the complete affine transform. Positive grouping multiplies Z pitch by group
size; ordinary reduction retains the existing Z pitch because it has no adjacent
output groups. XY pitch, direction and units remain unchanged. Selecting one T
rebases time origin to that frame; all T keeps the original origin. Frame
interval/time unit remain unchanged. Invalid calibration, address-space overflow,
invalid indices, methods and incomplete groups fail before any provider read.

Native tests cover the asymmetric3T/2C/9Z/5Y/7X/3S case, all four T/C selection
combinations, group boundaries, all methods/types, S1/3/4, a one-Z time series,
affine/time rebasing, exact memory boundary, overflow and invalid inputs.
An unreadable observed provider proves zero plane/region reads. A separate
example exercises the public header and contributor contract. Both are wired
into the existing all-target headless/installed-SDK CI profile.

Local verification compiles the new source with unchanged installed-core
dependencies and runs both native and example consumers. It does not establish
installation of the new symbol into the local SDK or full CMake/platform/UI
acceptance. [Source-bound evidence](../parity/reviews/G034-scientific-layout-progress001.json).

## Implemented finite native reduction

`dataset_projection_execution.hpp` adds `project_dataset_z`. It captures shape,
type, method and layout before callbacks; each T/C/Z group borrows immutable
plane leases and uses the existing projection loop through a private indexed
view. One group reference table and one value scratch vector are reused. There
is no whole-dataset materialization, second numerical kernel or Python loop.
Changing a caller-owned method from a callback cannot retarget the operation.

The request now also carries `max_working_bytes`; the descriptor reports its
checked working charge. Admission includes output pixel buffers, output/owner
tables, one contributor group's decoded buffers/headers/references, and reduction
scratch. Provider-private caches, allocator overhead and future document
metadata/publication retain their separate owners. This is not a total process
heap bound or complete document-job admission. Both caps fail before reads and
before execution callbacks. Pre-cancellation is likewise checked without user
callback dispatch. Captured plane geometry/type is checked after each provider
read; a provider changing its advertised type is refused.

Trusted eager FiniteOnly admission is reused without another input scan;
virtual planes still undergo cancellable finite/range validation. Generic plane
progress reports0 through completion, and cancellation from that callback stops
before the next group.

The API accepts the existing FiniteOnly native profile. Quiet-NaN/IEEE Float32
profiles are explicitly refused before reads; no ImageJ nonfinite qualification
or policy downgrade follows. Output construction validates finite samples;
overflow or invalid virtual-provider samples abort without returning a dataset.
No document annotation/display state enters this API, so it cannot silently
clear source metadata. Prepared document behavior is still required below.

Final010 passes the original layout consumer, new execution consumer, unchanged
legacy core consumer and expanded public example, compiled from current sources
against unchanged installed-core dependencies. The new symbols have not been
installed into the local SDK; complete source build/installed SDK qualification
is pending CI. Numeric controls include24 method/T/C combinations plus72
method/type/S1/3/4 pairs, analytic population variance and even median, ordinary
and grouped output, one-Z time series, no T/C/S mixing and immutable eager
inputs. Provider tests prove exact selected-plane reads and at most one live
group, plus cleanup on failure/bad samples, unsupported policy, memory refusal,
pre/in-reduction cancellation, callback exception and finite output overflow.
[Current source-bound reduction evidence](../parity/reviews/G034-scientific-reduction-progress001.json).

## Implemented source command

`dataset_projection_command.hpp` adds immutable resolution, strict record
validation/replay, explicit rebind and `execute_dataset_z_projection`. Resolution
and all record checks are metadata-only. V1 has an explicit native_finite profile
and intended new_document destination; numerical execution returns only a Dataset.
It does not publish a document, clear annotations or qualify output metadata.

The closed record retains source owner, Pixels+Metadata revisions, complete
TCZYXS shape/type/sample policy and spatial/time calibration. Every explicit
T/C/Z/group/method/cap input survives, including otherwise inactive T/C indices
and zero ordinary-group mode. Integer fields reject Boolean/float approximations;
flags require Boolean values. Derived shape/type/calibration, source contributors
and portable buffer sizes are recomputed from the original metadata. Missing,
unknown, unsupported and contradictory fields fail before rebinding. Replay
rejects owner, data revision and metadata drift; display/transient-selection
changes do not retarget these numerical inputs. Execution also rechecks captured
provider metadata before callbacks/reads. Captured snapshots still compute their
original pixels after a live document edit; prepared publication must guard that
live owner separately.

Working charges contain ABI-sized ownership tables, so they are not serialized
as portable claims. The original working cap remains an exact positive request;
validation does not claim that a positive cap was admitted on this ABI. Binding
and execution enforce it on the current platform. Explicit rebind validates the
entire old grammar/portable claims, then uses the caller's complete new request
and cap. Runtime admission can only tighten a resolved request and never edits
its immutable record. Insufficient/zero execution allowance fails before reads
or user callbacks.

Final014 passes five source consumers: layout, numerical execution, unchanged
legacy core, command and public SDK example. Tests cover closed nested fields,
exact uint64 revisions, integer/flag refusal, malformed original rebind, owner
and pixel/metadata revision conflicts, calibration checks, permitted display/
selection drift, positive-but-insufficient proposals, captured request identity,
provider metadata drift, cap tightening and unchanged source history. An
unreadable provider proves metadata paths make zero pixel/region reads.
[Current command evidence](../parity/reviews/G034-scientific-command-progress001.json).
The new symbols still need installed-SDK/full-CMake/platform qualification.

## Pinned runtime observations now available

[The reference corpus](../parity/reviews/G034-imagej-z-projection-reference001/README.md)
contains324 ordered cases with300 successful results and24 exact ImageJ RGB
hyperstack rejections; valid single-RGB-channel hyperstacks are also captured.
Two independent observer invocations reproduce the same bytes. Only the observer
is compiled; approved ImageJ/JDK classes are reused unchanged. The offline
reader, analytic numeric checks and metadata checks require no Java or native
processing package and are wired into CI.

This resolves the reference-discovery step, not implementation parity. Next
build an explicit ImageJ numeric/type/nonfinite profile; do not cast native
Float64 results or add a new Float64 viewer solely to accommodate that profile.
The matrix includes UInt16 median -> Float32 and signed16 median calibration,
which static return-type assumptions missed. Preserve native_finite behavior.
Grouped public API results and driver-reproduced plugin calibration steps have
separate fields and no GUI-execution claim. Full document policy, independent
UI/reference/platform gates and the original missing inputs are still required.

## Next decision before document/UI integration

The pinned source review confirms that scalar Sum/SD use Float32, SD uses the
sample denominator, and projection remaps overlays and copies channel LUTs with
method-specific range handling. The current native Float64/population profile
must stay separate. [Pinned ZProjector source](https://github.com/imagej/ImageJ/blob/62db90a269cccc1ac79f658e4602043214ed055b/ij/plugin/ZProjector.java).
Grouped projection redirects hyperstacks to ordinary projection.
[Pinned GroupedZProjector source](https://github.com/imagej/ImageJ/blob/62db90a269cccc1ac79f658e4602043214ed055b/ij/plugin/GroupedZProjector.java).

Include one-Z time series and distinguish grouped UI dispatch from direct API
behavior in the new reference cases.

The pinned runtime capture is now complete for the named matrix above. Use it
and supplement uncovered regimes before choosing prepared metadata policy or adding a new
Float64 channel-display version. Reuse the existing approved reference runtime
and fresh owned driver scratch. This source review is not an output corpus or
parity acceptance. New observations cannot replace the six missing original
readiness inputs or relabel broader historical recovery tracking.

## Required remaining implementation, in dependency order

1. **Resolved command and native reduction.** Reuse the existing strict axis
   command-record helpers and reduction implementation. Capture immutable source
   owner/revisions, shape/type/sample policy and full spatial/time calibration;
   record every explicit T/C/Z/group/method/limit field. Validate the entire
   original record before replay/rebind. The finite reducer and strict native
   command/replay/rebind are implemented; ImageJ-specific profiles remain required. Resolve layout once before provider
   reads. Reduce each T/C group through immutable plane references, reusing one
   group-sized scratch buffer; do not materialize/copy the whole dataset or
   introduce a Python kernel. Full working admission must include outputs,
   retained contributor planes, tables, scratch and metadata/publication state;
   this descriptor's pixel cap is only one component. Preserve cancellation,
   failure atomicity and finite/nonfinite method policies with independent
   asymmetric numeric oracles. Keep pinned reference expectations unchanged.
2. **Document metadata and display.** Add an explicit many-to-one contributor
   map; the existing regular Z subset map is not sufficient. Specify and test
   channel-display visibility/LUT/range/version handling for selected C and
   Float64 outputs (currently unsupported by canonical channel display).
   Define positioned/wildcard XY and XZ/YZ ROI/overlay, labels, saved manual
   threshold, source properties, Fourier and opaque metadata behavior before
   publication. Preserve originals and provenance; silent blanket clearing is
   not the scientific parity solution. These policies remain unfinished.
3. **Prepared operation and shared ImGui workflow.** Use existing native jobs,
   source guards, output staging and atomic derived-document publication.
   Extend the shared projection dialog with captured current/all T/C intent and
   existing one-based inclusive Z/group controls. Bind Python to the same native
   command and use strict integer/boolean validation; no separate processing or
   publication implementation. Verify actual interaction, stale-source refusal,
   cancellation, source/history preservation, save/reopen and both launchers.
4. **Independent acceptance.** Recover the original reference bytes under their
   unchanged pins; qualify full scientific method/type/sample/metadata families,
   installed C++/Python consumers and platform gates. Existing grouped/range
   checks and predecessor UI success cannot satisfy these remaining requirements.

No dependency or plugin was added. All99 original requirement/status triples,
commercial-use constraints and the broader historical reference recovery scope
remain unchanged. Compiler/runtime generations are reused; verification retains
small receipts and generated identities, then retires terminal scratch.

## Private ImageJ scalar numerical seam

`src/native/imagej_projection_numeric.hpp/.cpp` is a private, allocation-free
scalar primitive, not a new dataset API. Its caller supplies unsigned U8/U16
codes or raw Float32 words and a method; signed16 is explicit and only valid for
U16. It validates types, count, sample encoding and median scratch capacity and
overlap before scratch writes. Sum and integer mean round each accumulation to
Float32; Float32 mean skips NaNs with a double accumulator/count; SD is sample
SD with separately rounded double products; min/max use finite sentinels;
median sorts NaNs last and negative zero first, and preserves even-add overflow.
U16 median returns Float32. Signed16 subtracts density bias for sum and median,
while typed min/max/mean retain unsigned codes for later density metadata.

The generated fixture transcribes84 scalar cases/336 rays from the immutable
reference corpus. Windows MSVC strict and Linux GCC strict source consumers pass:
317 non-NaN results exact type/bits,19 NaNs equivalent by class. Arithmetic NaN
payload/sign parity is not claimed. The original Java observations and strict
native_finite records remain unchanged. CMake registers the test and applies
strict arithmetic flags; CI also verifies fixture transcription without Java.
Local checks are standalone source consumers, not full CMake/install acceptance.

Next reuse the primitive's rules in bounded streaming/tiled dataset execution;
resolve ImageJ one-Z time dispatch explicitly, admit IEEE outputs, then complete
RGB scaling and metadata before prepared publication, bindings and UI. The ray
primitive alone does not establish those remaining behaviors or full G034.

## Separate ImageJ scalar dataset execution

`imagej_dataset_projection.hpp` adds a separate request, metadata descriptor and
`project_imagej_dataset` result. It supports U8/U16/F32 S1 with the pinned scalar
numerical profile; existing native_finite requests, records and processing are
unchanged. `ImageJDefault` resolves Time exactly when Z==1; otherwise Z. Explicit
Time retains all Z, an owned extension for multi-Z sources. Z projection retains
all or current T; Time ignores `all_time_frames`, matching observed one-Z
ImageJ dispatch. Scientific C remains independent, with all/current selection.
Bounds are explicit inclusive zero-based coordinates on the resolved axis;
positive groups divide the range exactly. Scientific grouped T/C behavior is
an owned extension, not proof of the ImageJ grouped GUI hyperstack path.

Input integers must be exact typed unsigned codes. Default finite Float32 source
intermediates round to Float32 at ingestion; reviewed IEEE input words are
decoded without changing source buffers. Float outputs retain IEEE admission,
including sum/median overflow. The result carries signed16 density intent for
integer outputs; Float32 results clear that intent. Publication must consume
this field because Dataset has no density calibration member.

Ordinary geometry copies the source without first-contributor or time-origin
rebasing. Positive grouping multiplies Z pitch even for Time and retains voxel
origin indices, represented here by the voxel index of world zero. Consequently
world origin can change; copying world origin with a changed pitch would be
incorrect for the pinned nonzero ImageJ voxel-origin cases. Affine extension
checks preserve that same invariant. Grouped expected geometry comes from the
observer's separately reproduced source-level calibration steps, not GUI input.

The executor captures a Dataset value and request before callbacks. Reassigning
caller aliases cannot retarget it. Metadata-only checked estimates admit output
buffers, owner tables, one fallback decoded plane/region, and reducer scratch
before callbacks or pixels. These exclude provider-private caches, allocator
control overhead and document publication, and are not total heap bounds.
`tile_width`/`tile_height` default1024/32 and clamp to XY. Each source region
is released before the next contributor. Nonmedian state depends only on tile
size; median stores one bounded pixel-by-contributor tile plus one ray scratch.
Both paths poll cancellation, including validation/partition/sort inside long
median work. Callback, reader and metadata-drift failures return no result.

True tiled readers avoid fallback decodes. Legacy default region adapters may
redecode a full plane for every tile; larger rectangles reduce this cost at the
expense of explicitly charged scratch. No random-access cost capability exists
on PlaneSource today, so no universal decode/performance guarantee is claimed.
Do not replace this with whole-volume retention or a Python pixel loop.

The fixture transcribes all228 non-RGB success rows from the immutable corpus,
reorders Java t,z,c planes to native t,c,z explicitly, and uses captured output
words as expectations. Two tile choices exercise partial XY rectangles. Each
lane checks3840 output samples:3802 exact non-NaN type/bits and38 NaNs by class,
plus exact source-word preservation. Provider tests cover finite/fallback/tiled
paths, read counts, one lease, caps before callbacks/reads, invalid configuration,
provider/callback failures, source/request alias capture, metadata drift,
long-median cancellation and explicit multi-Z Time behavior. Existing336 scalar
rays and the public SDK-intended example also pass locally. CMake registers the
new test and SDK example; CI checks fixture regeneration. Local source consumers
link unchanged installed dependencies, not a newly installed SDK. RGB merge,
LUT/overlay metadata, prepared publication, strict ImageJ records, bindings and
shared UI remain to do; full G034 and editor parity are not established.

Reviewed IEEE source admission is currently eager-only under Dataset's existing
policy. Unknown virtual readers remain FiniteOnly; this executor rejects their
NaNs/infinities rather than bypassing that admission rule. Reviewed virtual IEEE
provider admission is a remaining integration requirement before claiming
nonfinite virtual-stack ImageJ parity. Likewise, downstream finite-only commands
and older display/session metadata do not become IEEE-capable merely because
this executor can return such pixels.

## Explicit ImageJ RGB numerical profile

`imagej_rgb_projection.hpp` adds `ImageJRGBProjectionRequest` (the existing scalar
request fields plus `clip_when_summing`), `describe_imagej_rgb_projection` and
`project_imagej_rgb_dataset`. A shared private descriptor/executor handles both
profiles. The scalar API/header and its S1-only rejection test remain unchanged.
RGB admission is UInt8 S3/C1; RGBA, UInt16/Float RGB and scientific multichannel
RGB are not admitted by this reviewed profile. Existing native extensions retain
their APIs/behavior. The24 captured C2 hyperstack errors are reproduced before
callbacks/pixels; they are not evidence that all possible C2 legacy shapes fail
in ImageJ. Simple-composite C-only dispatch still needs separate observation.

The executor reduces each interleaved component using the same typed scalar
arithmetic, reading one region for all three colors. No full split-channel stacks
or extra output image are allocated. RGB sum/SD first hold Float32 reductions in
the output double-backed buffer. Default merge follows ImageJ's Float32 shared
maximum multiplication and subsequent independent channel min/max conversion;
these stages must not be algebraically cancelled. Sum clipping instead adds
0.5f and clamps each channel to255. Arithmetic NaNs arising from zero/constant
ranges convert to zero using defined Java-compatible byte conversion, avoiding
undefined native nonfinite casts. Other methods already produce UInt8 samples.
The clip property is ignored outside RGB sum. Final output is finite UInt8 S3
with signed16 density intent false.

Output, fallback/region and reducer charges include S3. Range scratch is charged
conservatively for sum/SD, including clipped sum. Maximum/scale/conversion/clip
passes poll cancellation. Source/request snapshots and metadata drift guards
are shared with scalar execution. Row-span output stores avoid extra per-sample
coordinate divisions. Provider tests verify24 reads for the chosen grouped RGB
geometry rather than three separate channel passes, a single fallback lease,
cap-before-read/callback rejection, invalid typed components and clip alias
capture. This proves access/count behavior, not decode throughput or total heap.

The immutable RGB supplement adds124 varied/degenerate/rounding-stress cases;
two fresh observations have identical hashes. Combining it with the original
corpus gives196 RGB successes and24 explicit negative rows. Two tile layouts
check11176 packed RGB words/lane exactly, plus unchanged source words and
cancellation during scaling/conversion/clipping. Windows/MSVC and Linux/GCC
source consumers pass, along with unchanged336 scalar rays,228 scalar dataset
cases x2 tiles, and scalar/RGB SDK-intended examples. Actual full CMake/install
qualification belongs to successor CI. No historical fixture or capability
status changed. [Reference scope](../parity/reviews/G034-imagej-rgb-projection-reference001/README.md).

Remaining before the UI milestone: simple-composite/default C-only dispatch,
general density calibration (including scalar median pixel values), grouped GUI
hyperstack routing, prepared metadata publication with source guards, strict
profile records, Python/shared ImGui controls and actual history/reopen checks.
Private accumulator/range sizes enter scratch charges, so future strict records
must exclude both working_bytes and ABI-dependent reduction_scratch_bytes from
portable derived guarantees. Reviewed virtual IEEE and downstream IEEE admission,
original reference readiness and full ImageJ/Fiji acceptance remain open.
