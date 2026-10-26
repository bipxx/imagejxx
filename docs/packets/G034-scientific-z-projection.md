# Scientific Z projection

This packet extends the existing range/grouped projection workflow to scientific
TCZYXS datasets. G034 remains in progress. The first implementation is the shared
metadata descriptor plus the finite native reducer. The resolved command,
publication and UI remain unfinished.

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

## Required remaining implementation, in dependency order

1. **Resolved command and native reduction.** Reuse the existing strict axis
   command-record helpers and reduction implementation. Capture immutable source
   owner/revisions, shape/type/sample policy and full spatial/time calibration;
   record every explicit T/C/Z/group/method/limit field. Validate the entire
   original record before replay/rebind. The finite reducer is implemented;
   nonfinite/ImageJ-specific profiles remain required. Resolve layout once before provider
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
