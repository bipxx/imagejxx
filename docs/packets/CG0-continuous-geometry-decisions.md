# CG0: continuous geometry decisions

Prepared 2026-10-24 by Agent A. **Design proposal; not a frozen API or a passed
CG0 gate.** This file is the bounded native decision artifact requested by the
[three-agent delivery specifications](THREE_AGENT_DELIVERY_SPECS.md#cg0-continuous-geometry-design-gate).
C owns acceptance of these decisions and the dispatch card. Exactly A, B and C
remain the actors. This preparation changes no product, test or build input.
P13i E7 remains the active product qualification; its sources stay held.

The proposal below makes the intended behavior and refusal boundaries concrete.
The three mandatory gates at the end distinguish a specified contract from an
algorithm whose feasibility and independent expectations have actually been
reviewed. No new numerical observation or test pass is claimed here.

## 1. Existing contracts that must remain frozen

| Existing behavior | Local source and consequence |
| --- | --- |
| `ImageJCompositePath` owns immutable absolute M/L/Q/C/Z binary64 commands, even-odd area fill and implicit subpath closure. Coordinates are finite and in `[-1e9,1e9]`; an empty command stream is rejected. | `include/imagejxx/composite_path.hpp`; `src/native/composite_path.cpp:17-24,51-77,159-235`. Reuse this representation and its coordinate domain. Do not change its constructor or make a second public curve model. |
| Curve subdivision already preserves degree using de Casteljau, subject to binary64 arithmetic. | `include/imagejxx/path_edit.hpp`; `src/native/path_edit.cpp`. Reuse the mathematical construction; do not use the public edit operation repeatedly inside a Boolean solver, with repeated captures/callbacks. |
| `combine_selections` produces a canvas packed mask; `combine_selection_regions` produces a full image-grid region. Ordered subtraction is first operand minus all subsequent operands. | `include/imagejxx/document.hpp:97-102`, `include/imagejxx/selection_region.hpp:39-62`. Keep their signatures, defaults, geometry kinds, result/style policies and path refusals unchanged. |
| NativePathV4 creation is Replace-only; old modes refuse native path operands. | `src/native/selection_creation.cpp:244-252`; historical assertions in `tests/native/test_path_creation.cpp`. Continuous creation must use a new explicit branch. |
| V8 workflows carry explicit authored path extents, validate original-only extents, including disabled originals, and preserve final Previous-selection composition. | `src/native/selection_creation.cpp:1229-1294`, `src/native/native_workflow_program.cpp:574-576,788-789,856-859`. Do not widen V8 to a new equation. |
| A missing active selection can mean WholePlane; a zero-area native path remains an empty scientific target. | P13i empty-target contract and evidence. A Boolean empty result must never become `std::nullopt`, Python `None`, or an unrestricted target. |
| Existing shape rasterization and processor bounds use their pinned processing contract, distinct from retained binary64 geometry. | `src/native/imagej_shape.cpp`, `imagej_shape_plan.hpp`; P13i. CG1 does not edit this rasterizer. |

CG1 is continuous **area** algebra. It is not stroke union, mask tracing, polygon
flattening, curve fitting, nonzero-winding support, or a replacement of existing
grid commands. Those remain separate named requirements, not silent conversions.

## 2. Proposed decisions to ratify

| ID | Proposed decision | Compatibility/limit |
| --- | --- | --- |
| D01 | New native profile `native_binary64_evenodd_v1`, using existing finite binary64 path coordinates without binary32 conversion. | ImageJ comparisons have a separate reference policy in section 7. No promise of matching Java command ordering or all boundary pixels after Java rounding. |
| D02 | Operands are ordered, at least two, individually even-odd filled. Union, intersection and XOR combine those sets; subtraction is `A0 \\ (A1 union ... union An)`. | Reordering subtraction changes the result. Reordering commutative operations preserves the represented area, not necessarily command order. Validate all operands before shortcuts. |
| D03 | Open subpaths are implicitly closed for area, as in the current path carrier. Isolated moves, zero-length edges and one-dimensional contours contribute no area. Width/attributes do not create a stroke. | Explicit Selection kinds line/polyline/angle/point/multipoint are refused by promotion. A path that contains only a line is accepted as zero area. |
| D04 | Empty output is the canonical path command array `[0, +0.0, +0.0, 4]` (`M0,0 Z`), accompanied by an immutable result `empty()` flag. | This is a valid existing path, not an empty array or mask. `Selection::from_path(result.path())` remains explicit empty area. Historical empty-command rejection stays intact. |
| D05 | Zero-area contacts do not create a line/point result. Tangent and shared-boundary events participate in topology but contribute area only through incident faces. | No epsilon may change a point contact into a bridge or remove a narrow positive-area face. Uncertifiable topology is a visible refusal. |
| D06 | Retain L/Q/C segment degree; output boundaries are certified subsegments of original boundaries, with bounded binary64 endpoint reconciliation. No flatten/refit fallback. | Exact unused controls remain unchanged, including signed zero. New/reconciled controls have the explicit error bound below. |
| D07 | CG1 promotion accepts `path`, `rectangle`, `polygon`, `freehand`; paths share immutable commands and linear primitives produce M/L/Z. | Polygon/freehand receive implicit closure. Rotated/rounded/ellipse, region/mask and stroked kinds refuse explicitly. Their later promotion child must freeze its representation; an ellipse is not exactly a polynomial cubic. |
| D08 | Core algebra has no style, owner, canvas, calibration or TCZ dependency. | CG2 captures styles and document dependencies. Off-canvas geometry is retained, not clipped or translated to a processing origin. |
| D09 | Result order is deterministic for the same ordered inputs, options and limits on the qualified Windows/Linux builds. Exact representation equality remains the existing no-op rule. | No second area-equivalence algorithm is introduced to decide document history. CG2 may preserve an unchanged captured operand when the core certifies that identity. |

The empty flag is produced with the path by one private result constructor; it
is not caller-editable metadata and is not serialized as a new Selection field.
On replay, emptiness is recomputed by the versioned equation. Canonical empty
results preserve native V5 session storage without a session version change.

## 3. Exact proposed CG1 public surface

New header `include/imagejxx/path_boolean.hpp` includes the existing
`composite_path.hpp` and `execution.hpp`. The following declarations are a
proposal, not current callable APIs. All sizes use checked `std::size_t` math.

```cpp
namespace imagejxx {
enum class PathBooleanFillRule { EvenOdd };
enum class PathBooleanPrecision { NativeBinary64V1 };

struct PathBooleanOptions {
  SelectionBooleanOperation operation = SelectionBooleanOperation::Union;
  PathBooleanFillRule fill_rule = PathBooleanFillRule::EvenOdd;
  PathBooleanPrecision precision = PathBooleanPrecision::NativeBinary64V1;
};

struct PathBooleanLimits {
  std::size_t max_operands = 256;
  std::size_t max_input_segments = 65'536;
  std::size_t max_input_command_scalars = 1'048'576;
  std::size_t max_output_segments = 65'536;
  std::size_t max_output_command_scalars = 1'048'576;
  std::size_t max_intersection_events = 262'144;
  std::size_t max_subdivision_nodes = 1'048'576;
  std::size_t max_work = 67'108'864;
  std::size_t max_working_bytes = 268'435'456;
  unsigned max_subdivision_depth = 128;
};

struct PathBooleanEstimate {
  std::size_t input_segments = 0, input_command_scalars = 0;
  std::size_t output_segments = 0, output_command_scalars = 0;
  std::size_t intersection_events = 0, subdivision_nodes = 0, work_units = 0;
  std::size_t retained_bytes = 0, scratch_peak_bytes = 0, working_bytes = 0;
  double max_boundary_error = 0;
};

class PathBooleanResult {
public:
  const ImageJCompositePath &path() const;
  bool empty() const;
  const PathBooleanEstimate &estimate() const;
private:
  struct Impl;
  std::shared_ptr<const Impl> impl_;
  explicit PathBooleanResult(std::shared_ptr<const Impl>);
  friend PathBooleanResult combine_composite_paths(
      const std::vector<ImageJCompositePath> &, const PathBooleanOptions &,
      const PathBooleanLimits &, const ExecutionControl &);
};

ImageJCompositePath path_boolean_operand(
    const Selection &, const PathBooleanLimits & = {},
    const ExecutionControl & = {});

PathBooleanEstimate estimate_path_boolean(
    const std::vector<ImageJCompositePath> &, const PathBooleanOptions &,
    const PathBooleanLimits & = {}, const ExecutionControl & = {});

PathBooleanResult combine_composite_paths(
    const std::vector<ImageJCompositePath> &, const PathBooleanOptions &,
    const PathBooleanLimits & = {}, const ExecutionControl & = {});
}
```

Input segment counts include actual M/Z commands, as `segment_count()` does.
Implicit closing edges are charged internal fragments/work, not fabricated input
commands; returned `output_segments` counts actual emitted commands. The private
result constructor keeps geometry/emptiness/estimate consistent. Copies share
the immutable result, with no public constructor or setters.
All limits are positive. Unknown enum casts, overflow and invalid coordinates
refuse. Increasing a limit permits more work; it never changes the precision
profile, snapping rule, successful result or supported geometry kind.

`estimate_path_boolean` executes the same bounded quiet planner and may allocate
admitted scratch; it is **not** a cheap metadata-only exact intersection counter.
It returns no retained geometry and invokes no user callbacks. Its control is
used for cancellation, including inherited scopes. `combine_composite_paths`
plans once and returns the result and estimate together. UI resolve/preview
should consume that single result instead of calling estimate and combine back
to back. A public reusable plan/cache object is not needed in CG1.

The proposed Python surface uses existing operation strings
`union|intersection|subtraction|xor`, with keyword-only `fill_rule="even_odd"`,
`precision="native_binary64_v1"`, `limits`, `cancellation`, `progress`,
`checkpoint`. Names mirror the three C++ functions. Result properties are
readonly `path`, `empty`, `estimate`; limits/estimates use named fields, rejecting
bool/noninteger/overflow for sizes. Estimate callbacks, if supplied through the
shared control adapter, remain suppressed. C owns export/registration; A writes
the feature binding, with no replacement of existing Boolean string parsing.

## 4. Numerical and topology policy

The input double words denote their exact dyadic values. Intermediate topology
decisions must be certified for those values; the output control points are
binary64 approximations to exact split boundaries. Do not classify topology
using the existing public `contains()` as a hidden oracle, a fixed-distance
probe beside an edge, or a global coordinate epsilon.

Proposed reconstruction error budget for a split/reconciled boundary fragment:

1. Let `S` be the largest absolute input control coordinate among that fragment
   and every incident segment whose event determines either endpoint. It is a
   local absolute-coordinate scale, not the image size or all operands' span.
2. Let `u(S)` be the larger adjacent binary64 spacing at `S`; at zero it is
   `denorm_min`. The permitted componentwise boundary displacement is
   `tau = 64 * u(S)` in native coordinate units. Evaluate this bound with checked
   outward rounding; it is not a criterion for merging two vertices.
3. A retained unsplit fragment with no reconciled endpoint has zero displacement.
   For generated controls, certify the polynomial difference against the exact
   restricted input curve over the entire parameter interval. A Bernstein
   control-difference enclosure is sufficient when parameter intervals and
   endpoint reconciliation are themselves enclosed.
4. Certify event order, incident-edge order and face labels independently of
   `tau`. If interval refinement cannot distinguish two required events or
   certify the graph under the finite limits, refuse. Do not erase the face.
5. `max_boundary_error` reports the largest certified bound actually needed,
   including endpoint reconciliation; it is not automatically filled with the
   allowed `tau`. Tests assert it is finite and at most the specified allowance.

This is a **proposed maximum reconstruction error**, not a proven error bound of
an existing algorithm. Gate G-A below requires an implementable certificate
design before adoption. A different bound requires a revised profile decision,
not tolerance relaxation after a failed fixture.

Required platform arithmetic is IEC-559 binary64, gradual underflow and
round-to-nearest. No dependency on Windows `long double` providing extra bits.
No fast-math, unrequested flush-to-zero or compiler-dependent fused contraction
is permitted in the Boolean translation unit. C records the exact per-source
compiler settings; explicit `std::fma` is permitted only where its semantics are
part of the predicate/certificate design. Unsupported floating environment
refuses visibly rather than changing process-global rounding state.

Coincidence is symbolic/certified, not proximity. The mandatory cases include
identical/reversed lines and Q/C spans, partial overlaps from known subdivisions,
degree-elevated coincident curves, repeated contours, tangencies, cusps and
self-crossings. Duplicate contour membership is reduced by parity per operand.
Shared edges are labeled from both adjacent faces, so subtraction and XOR do
not use ad hoc orientation toggles. Degenerate curves are lowered in degree
only when the relevant coefficients are exactly zero. Neither small area nor
small coefficients alone authorize dropping a contour.

The output traverses kept face boundaries with the selected face on the left in
mathematical XY. Even-odd fill makes nesting independent of that orientation.
Start each contour at a certified event/endpoint selected by a deterministic
lexicographic coordinate/provenance key; sort contours by the same stable key.
Do not force a floating minimum found by an unrelated raster bounds algorithm.
New exact-zero coordinates use positive zero; original untouched controls keep
their words. Parameter ordering is increasing along each source segment,
reversing the emitted control order only for a reversed boundary traversal.

## 5. Bounded algorithm recommendation and feasibility gate

**Recommended candidate:** an independently authored bounded planar arrangement
of original line/quadratic/cubic boundary fragments. No third-party geometry
source is currently selected or approved for incorporation by this card.

Proposed phases, all private until successful completion:

1. Borrowed admission checks every option/limit, all path cardinalities and
   promoted operands. Capture immutable handles/options/control after the known
   descriptor allowance is admitted. Normalize implicit closures and consecutive
   moves into private provenance-bearing fragments; never rewrite source paths.
2. Construct conservative interval bounds and a deterministic bounded broad
   phase. Candidate pairs include a curve's self-events and nonadjacent segments
   of the same operand. An adjacent shared endpoint does not excuse testing for
   a second crossing elsewhere. Avoid an unconditionally materialized N-squared
   pair table.
3. Isolate intersection/critical parameters with outward-rounded interval
   subdivision. Exact fallback predicates must resolve endpoint equality,
   collinearity, multiplicity and coincident spans. A finite dyadic/exact
   coefficient representation must have its own charged storage/work; do not
   assume doubles plus `nextafter` certify a double root or coincidence.
4. Split at certified ordered events, create a half-edge arrangement, and label
   faces by propagated per-operand even-odd parity. Apply the Boolean truth table
   to those labels. Winding/face classification uses the arrangement, not pixels.
5. Emit only surviving original boundary subcurves using de Casteljau plus the
   certified endpoint representation. Check continuity, topology, closed
   traversal and the reconstruction bound before making an immutable result.
6. Canonicalize the empty case. Build the public result and estimate, then expose
   callbacks under the frozen control. An exception or cancellation at the final
   callback discards the result.

This decomposition is feasible in principle but the exact fallback in phase 3
is the difficult dependency. An interval-only implementation with “ambiguous
means disjoint” is not acceptable. Before CG1, A must write down how the chosen
predicate/coincidence method terminates or refuses for each CG-03..07 row, with
bounded limb/expansion storage if used. No unpublished proof or existing
implementation is asserted. If this cannot be justified within the proposed
single private implementation module, return to C with a concrete eligible
dependency proposal; do not grow an unreviewed arithmetic framework silently.

| Candidate route | Readiness and decision |
| --- | --- |
| Extend existing grid Boolean code | Rejected for CG1: its result is pixel membership, and native paths are explicitly refused. It remains correct for its existing purpose. |
| Independent bounded curve arrangement | Preferred candidate, conditional on G-A's exact-predicate/coincidence and error-certificate review. It introduces owned implementation, not copied JDK internals. |
| New curve-Boolean dependency | No pinned curve-Boolean library or commercial/transitive review was found in the inspected CMake/native dependency surface. Therefore no library is declared eligible or selected here. C must pin/review source, license obligations, build/toolchain footprint and exact curve/domain behavior before this route can replace the candidate. |
| Polygon clipping after flattening | Rejected for continuous acceptance. It would require a separately named approximation/conversion feature and cannot satisfy curve retention. |

Do not copy `java.awt.geom.Area` implementation into the product. The existing
ImageJ source-bound commercial review authorizes the recorded reference use,
not an unspecified JDK-derived implementation or new third-party dependency.

## 6. Work, memory, cancellation and errors

Count a work unit for every candidate-pair comparison, predicate refinement,
subdivision node, graph event/edge visit and sort comparator invocation. Exact
arithmetic additionally charges each bounded limb/expansion operation, not just
the enclosing predicate call. Charge
coincidence processing and self-intersections; fast paths do not bypass full
operand admission. Every loop is bounded by a relevant count and the total
`max_work`. Poll cancellation before allocation and at most 1024 declared work
units apart. Subdivision depth and total nodes are separate caps.

Before each allocation/growth, check overflow and the new peak while the old
allocation is still live. Charges use actual container capacities and element
sizes for the qualified ABI, including temporary copies during reallocation,
normalized fragments, interval/exact-predicate storage, event lists, graph,
traversal tables and staging plus immutable output. These are logical owned
bytes, not measured RSS; allocator metadata and caller callback storage are
excluded explicitly. Record `working_bytes` as the proven total peak, not the
sum of unrelated phase maxima. `retained_bytes` counts the returned owned result
and command backing; `scratch_peak_bytes` is the maximum additional live scratch
over that retained reservation. Conservative reservations must be identified as
such and included in `working_bytes`.

Use a private per-operation accounting allocator at each allocation boundary
where a standard container may request more than its nominal size; do not assume
`reserve(n)` guarantees capacity exactly `n`. Charge the actual requested element
count before allocation and retain the old charge until deallocation. Immutable
path/result construction outside that allocator needs an explicit conservative
pre-admitted term. This is a local quota mechanism, not a new application-wide
allocator framework.

Caller-owned path backing is shared immutably and is not copied or charged again
as new core storage. Captured handle arrays and any normalized copies are charged.
CG2 separately charges command/original/result and every retained active,
Previous, saved and planar carrier; shared core terms must not be counted twice.
Core limits do not claim to cover the caller's retained old preview, document
history, codec strings or arbitrary callback captures. Host admission supplies
the real remaining allowance before calling the planner. Queueable-job admission
must retain existing per-job versus aggregate reservation semantics.

The entire admission/planning phase uses the existing cancellation-preserving
quiet scope, including inherited scopes. No progress/checkpoint/stage user
callback occurs before successful complete admission. `combine_composite_paths`
then emits the established coarse `(0,1)` and `(1,1)` progress pair, with
checkpoints and exception propagation; estimates emit none. No callback may
redirect the frozen operands, limits or geometry. This deliberately does not
promise visible incremental progress during the difficult quiet planning phase.

Proposed error classes/messages:

| Failure | Contract |
| --- | --- |
| Invalid options, operand count/kind, nonfinite/domain input, integer overflow | `std::invalid_argument` / Python `ValueError`, prefix `Path Boolean invalid input` |
| Input/output/event/node/work/working-byte limit | `std::invalid_argument` / `ValueError`, prefix `Path Boolean limit`, naming the exhausted field |
| Geometry cannot be certified in the chosen finite precision/refinement limits | `std::invalid_argument` / `ValueError`, prefix `Path Boolean unresolved topology`, with operand/segment identifiers; no approximate result |
| Cancellation | Existing `OperationCancelled`; no result/publication |
| User callback exception | Original exception propagates; no result/publication |
| Allocation failure after valid admission | Existing allocation exception; no result/publication and all owned scratch released |

Topology refusal is a bounded operational result, not license to exclude an
entire mandatory topology category. Every declared standard fixture must
succeed; CG-10 adds deliberately exhausted/refused counterparts.

## 7. Local reference identity and independent expectations

Observed local source is ImageJ 1.54r commit
`62db90a269cccc1ac79f658e4602043214ed055b` at
`.cache/native-verification/references/imagej154r/source`.
The source archive SHA256 recorded by the retained restoration receipt is
`09e31a88191d10c7f79c607cb6bb62342476bde733c5d932d5825fd553796665`.
This review read and hashed `ij/gui/ShapeRoi.java` as
`8ab4a507fed1ddd09391c5663d482350c9f035e17788d915b440915277fc419e`,
matching the existing source-bound review.

The existing local reference runtime directory is
`.cache/native-verification/references/imagej154r/reviewed-jdk21`; its `release`
declares Azul Zulu21.42+19-CA, Java21.0.7+6-LTS. Exact member pins remain in
`tests/fixtures/tophat-mask-imagej154r.json::runtime_pins`; the existing
`scripts/capture_path_filter_reference.py::verify_inputs` verifies archive,
source and runtime members. This review only inspected local source/metadata;
it did not execute Java or reverify the entire runtime. C must perform that
identity check for new captures and reuse the existing classes/runtime location.

Source facts that constrain capture:

- `ShapeRoi.makeShapeFromArray` (`ShapeRoi.java:353-384`) builds even-odd
  `GeneralPath` from float commands.
- `unaryOp` (`216-238`) translates both stored local shapes to absolute
  coordinates, calls `Area.add/intersect/exclusiveOr/subtract`, gets integer
  bounds and stores a new local `GeneralPath`. The comment above `not` describes
  the reverse subtraction; use the actual `a1.subtract(a2)` code and observed
  operands, not that comment.
- `getShapeAsArray` (`750-772`) reads float coordinates and adds float origins.
  Capture this wire-facing array **and** the stored local shape/origin separately;
  adding large origins can erase local subpixel information.
- `unaryOp` catches exceptions internally. A returned ShapeRoi is not by itself
  proof that the desired operation succeeded; the independent topology and
  membership checks are necessary.

C should add one bounded reference observer, not modify old fixtures. Each case
records ordered original float input words, actual stored input local paths and
integer origins, operation, result local path words/origin/bounds, returned
absolute array, actual empty/contains observations at declared probes, and a
small full ROI mask only after checked area admission. Probe cases with huge
coordinates are metadata/geometry-only; never request their unclipped masks.
Use a fresh operand pair for every operation because ImageJ mutates the left ROI.
Capture Java's actual segment types and winding rule rather than assuming its
output remains in the input form.

Use two complementary independent comparisons:

1. **Native analytic contract:** exact dyadic line examples and known curve
   subdivision/intersection constructions establish topology and split error
   independently of product routines. Include original binary64 coordinates
   that are not binary32-representable. C supplies literal coordinates or an
   independently justified high-precision oracle; no native output generates
   expected data.
2. **Pinned ImageJ comparison:** compare operations on the actual effective
   float-origin/local geometry. Check component/hole/contact topology and
   membership probes away from the declared boundary enclosure. Compare small
   masks exactly where reference/native representation is supposed to agree;
   retain and review any boundary disagreement instead of widening a tolerance.
   Output command sequence/degree can differ under equivalent representations,
   so command-array equality alone is not the geometric comparator.

Existing P05e/p12o fixtures certify raster membership at their recorded scope,
not continuous curve preservation. New reference files need additive IDs and
provenance; no old hash/expectation is repinned. Retired historical G016 artifacts
remain unavailable. The existing commercial record
`docs/parity/roi-boolean-commercial-review-001.json` is reference-only; C records
the selected new implementation/dependency decision before incorporation.

## 8. CG2/CG3 dependencies; no implementation authorization here

Proposed names are `SelectionCreationGeometry::ContinuousPathV5`, creation
command version5 and workflow program version9. C must reserve/freeze these at
CG2 entry after checking concurrent version assignments. NativePathV4/V8
Replace-only behavior and historical refusal assertions stay unchanged.

For active creation, reuse the existing request/resolved-command/prepare seams.
The new mode supports Replace/Add/Subtract/Intersect/Xor; base precedes candidate
for subtraction. Replace and Add with no base preserve the authored candidate;
other combinations require a base. Unsupported kinds refuse before callbacks.
Proposed new-mode style policy: the candidate's width/attributes survive on the
result, including empty output; attributes never influence geometry. This policy
does not rewrite legacy raster result styling. A structural no-op preserves the
captured Selection exactly and makes no history entry. A changed authored Apply
uses existing one-step path history and final Previous semantics.

The proposed V5 creation root has exactly the old command fields
`schema,version,source,position,operation,base,candidate,result` plus one closed
`geometry_profile` string `native_binary64_evenodd_v1`. Geometry uses the strict
existing nested-attribute native path carrier, not flattened legacy attributes.
The profile fixes numeric semantics; runtime limits are not serialized as intent.
Validation recomputes the complete original equation and verifies canonical
geometry/attributes before source/rebind checks. No authoring override may
silently reinterpret a V5 original under a legacy mode.

Proposed V9 retains V8's explicit `selection_creation_extent` rules, but is
selected by continuous creation intent/original, even if disabled. Original-only
extent derives from the validated original; authored intent requires an explicit
extent. Grid-changing prefixes and later invalid/over-budget steps are fully
preflighted before provider reads or user callbacks. V1-V8 readers continue to
reject the new profile. Mixed old/new programs retain the original legacy
payloads and only use the new outer version where required.

Workflow working admission must include cumulative retained geometry plus the
proven maximum serial algorithm scratch. When mask and Boolean work are disjoint
phases the peak is `max(mask_peak, boolean_peak)`, not their sum; prove their
liveness rather than assuming it. Preserve the owning retained floor and count
unrelated Previous/saved/planar carriers once. Never pass a retained-only limit
as proof of an admitted working peak.

Saved-manager work is a separate CG2 substep: freeze ordered distinct row indices,
combine regardless of TCZ positions consistently with the existing manager, and
leave rows/positions/attributes unchanged. Proposed result style comes from the
first row, explicitly, while preserving the manager's existing transient
selection/Previous policy. A versioned manager capture/rebind schema remains
a **CG2 entry decision**, not an implicit reuse of the two-operand creation
record; do not fold N-row intent into repeated UI-side mutations. Active and
saved changes must not be claimed interchangeable history semantics.

CG3 consumes the same immutable core in the existing planar-local coordinates;
no new XYZ solver. Freeze planar coordinate mapping, plane index, T/C and extent
dependencies before assigning planar files. Preserve saved annotation metadata
and publication/history contracts. P13i filters should consume the resulting
native path through the existing processing planner, including canonical empty.

## 9. Exact CG1 ownership and bounded acceptance

Only after CG0 exit and C's dispatch, Agent A owns these **new** files:

- `include/imagejxx/path_boolean.hpp`
- `src/native/path_boolean.cpp`
- `src/native/path_boolean_internal.hpp` (one private algorithm/predicate module)
- `src/native/path_boolean_bindings.cpp`
- `tests/native/test_path_boolean.cpp`

No existing raster/geometry/selection/workflow file is authorized for change by
CG1. If shared helpers need extraction, send a concrete ownership request and
invalidated-selector list before editing. C owns CMake, central binding/package
exports, strict arithmetic build flags, independent tests/SDK and reference
sources. B owns its separately dispatched private draft/probe files, and cannot
implement a geometry substitute while waiting. C alone builds/installs.

Proposed C registration: target `imagejxx_path_boolean_tests`, CTest
`native_path_boolean`, core-only linking with no display/runtime dependency.
Proposed independent paths (C-owned, not created here):
`tests/python/test_path_boolean.py`, `examples/cpp/path_boolean.cpp`,
`tests/reference/ObserveContinuousPathBooleans.java`,
`tests/fixtures/path-boolean-imagej154r.json.gz`. Freeze final names before work.

Native groups and independent selectors share these stable IDs:

| ID | Required successful cases and independent assertion |
| --- | --- |
| CG-01 | Four operations on disjoint dyadic rectangles, two/three operands, reversed order. Exact vertices, emptiness, two islands; reject unsupported operation/count before callbacks. |
| CG-02 | Containment, nested hole/island and repeated contours under even-odd fill. Reversed contour orientation leaves area unchanged; subtraction order is observed. |
| CG-03 | L/Q, L/C, Q/Q, Q/C and C/C crossing; known dyadic split cases plus general isolated roots. Curve degree/provenance, topology and certified displacement; no line flattening. |
| CG-04 | Endpoint-only contact, line/curve and curve/curve tangency, cusp and contact at an implicit closure. No false bridge or sliver; tangency multiplicity is resolved. |
| CG-05 | Equal/reversed edges, partial coincident spans, degree elevation and repeated identical operands. Union/intersection identity; subtraction/XOR canonical empty; complete validation precedes identity shortcuts. |
| CG-06 | Bow-tie/self-crossing linear contour, self-intersecting cubic, holes touching at a point and several subpaths. Arrangement parity and face traversal agree with independent topology/probes. |
| CG-07 | Powers-of-two scaled copies near `1e-200` and `1e8`, non-binary32 input coordinates, adjacent representable coordinates and near-tangencies. No fixed absolute epsilon; declared unresolved counterparts fail explicitly. |
| CG-08 | Negative/off-canvas paths and translated copies near the allowed envelope. Entire result survives; no canvas size or pixel allocation enters core algebra. |
| CG-09 | M-only, M/Z, horizontal/vertical closed line, zero-length controls, open subpaths, consecutive M, all four empty combinations. Result flag matches canonical empty; converting to Selection never becomes no-selection. |
| CG-10A | Each count/work/depth/byte cap at its advertised boundary and one under; integer overflow; same count with different topology cost. Estimate and combine obey the same definition; failed allocations release owned scratch. |
| CG-10B | Explicit/inherited cancellation and all callback kinds throwing/replacing caller inputs. No user callback on admission/topology refusal; frozen result cannot be redirected; no dataset reads, mutation or leaked reservation. |
| CG-10C | Native/Python malformed size/enum/profile/promotion requests, unchanged input words, result lifetime after source objects die, installed core-only SDK and default/header identities. |

CG-11 (transactions/Previous/history/stale owner) and CG-12 (V5/V9 intent,
legacy refusal, explicit rebind, disabled originals, V5 session reopen and
mixed-workflow later refusal) belong to CG2; planar/local-world cases belong to
CG3. They are dependencies for those packets, not fictitious CG1 passes.

The first verification slice is CG-01/02/09/10 admission plus installed API/SDK.
Then qualify the complete pair/topology matrix CG-03..08 using the same API;
do not expose unsupported hard categories as a completed continuous feature.
Retain failures before changing an algorithm or comparator. C freezes exact
collection IDs and both-platform consumed inputs, serializes builds in existing
caches, and admits capture scratch against the shared 8GiB budget. No new cache,
JDK, worktree or retired build generation is needed for this design.

## 10. Unresolved gates and next dispatch

| Gate | Owner | Concrete remaining decision/evidence | Blocks |
| --- | --- | --- | --- |
| G-D: contract ratification | C, with A/B acknowledgement | Accept or amend D01-D09, API/default limits, reconstruction bound and current CG1 promotion set. Explicitly record the canonical empty path and future version reservations. | B consumers and CG1 public interface edits |
| G-A: algorithm feasibility/eligibility | A proposes, C reviews | Review the exact predicate/coincidence method and interval/error certificate for all mandatory pair classes, with finite storage/work. If the independent candidate cannot support them, select and review one exact pinned dependency before incorporation. No implementation/proof is claimed by this document. | CG1 algorithm implementation |
| G-R: independent reference/comparator entry | C | Verify the existing source/runtime pins; capture bounded actual Boolean geometry and native analytic cases; freeze comparator and expected success/refusal rows. No old raster fixture may stand in for this gate. | Numerical acceptance claims and any reference-dependent algorithm decision |

CG0 can close only when these mandatory gates have recorded decisions with no
unresolved required behavior. CG2's N-row manager record and CG3's planar mapping
are explicitly later entry gates; they do not justify expanding CG1. While Q0
finishes, the next safe work is C's bounded reference/comparator preparation and
A's read-only predicate feasibility review. Product edits still require C's
freeze/dispatch and must not invalidate P13i's held source.

Full G016/G018/G019, M4 and ImageJ/Fiji acceptance remain false. Native algorithm
presence, a design decision, a collection count and a historical unavailable
artifact are not equivalent to qualified execution evidence.
