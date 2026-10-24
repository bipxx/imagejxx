# P13i: Native path filter targets and bounded admission

**State: specified; reference prerequisites pending.** This packet follows the
scoped P13h exit. Reference preparation may overlap P13h verification; product
integration may not. The [three-agent plan](../THREE_AGENT_EXECUTION.md) owns
scheduling. This specification changes no acceptance-family status.

## Deliverable and boundaries

A user draws a native path, chooses a supported Gaussian or rank operation and
an explicit target, previews, applies, undoes/redoes, saves/reopens and replays
the workflow in both C++ and Python ImGui launches. Geometry and pixels outside
the processing mask survive unchanged. Python exposes the same C++ behavior.

Cover G010/G042/G044/G045/G088 clauses consumed by this workflow. Extend the
existing admitted dtype/policy/target combinations; enumerate them from current
APIs at entry. Include Remove NaNs because it shares the region helper. This
packet does not claim true-volume filtering, continuous Booleans, every filter
option, or complete family acceptance. Those requirements remain in the queue.
No Qt, Python processing substitute or new third-party runtime is introduced.

## Exactly three owners

| Owner | Initial file responsibility | Concrete handoff |
| --- | --- | --- |
| A: native | `src/native/gaussian_command.cpp`, `rank_command.cpp`, `target_mask_resources.hpp`, `targets.cpp`, `execution.cpp`, `jobs.cpp`, `application.cpp`, `auto_threshold_stack.cpp`, `native_workflow.cpp`; consumed public headers/feature bindings; feature native/API unit tests | Region/resource contract, call-site audit, checked implementation, boundary tests, header/record compatibility notes |
| B: ImGui | Existing Gaussian/rank/target panels, `src/desktop/legacy_target_resources.hpp` and scheduler consumers, shared request/preview adapters; actual-input probes and host hook changes | Exact file list before edits, state table, shared standalone/Python controls and diagnostic observations |
| C: root | CMake/central registration, reference observer/capture tooling, independent Python/SDK/UI readers, CI profiles, docs/receipts | Frozen references and case IDs, independent expected outputs, installed four-context qualification, retention and landing |

These are candidate files, not a directory-wide license. C freezes the exact
allowlist after A's call-site audit and B's panel audit. Any extra shared file
requires an explicit ownership handoff. A/B do not build, install or create
environments; C holds the build/install/evidence lock. Every worker preserves
others' edits and reports interface changes before changing consumers.

## Entry gates and frozen interfaces

1. P13h is scoped-qualified, or C identifies an independent subtask that consumes
   none of its unfinished interfaces. Do not certify drawing-to-filter replay
   against an unqualified authoring pipeline.
2. C records source identity, all affected call sites, operation/dtype/policy/
   target tables and exact existing command grammar. Legacy payloads stay closed;
   add a version only if the wire contract actually changes.
3. A publishes one region/resource planning contract. Record processor extent,
   canvas-clipped extent, mask origin/shape, empty-target policy, checked byte
   estimates, scratch lifetime and owning reservation layer. B consumes the
   existing typed request; it must not independently calculate bounds or masks.
4. Reference fixtures settle processor bounds before implementation chooses
   them. Sigma above 8.5 can change Gaussian resampling alignment; occupied-mask,
   analytic path and processor bounds must be separate observations.

Reuse `Selection::mask()` / `imagej_shape_mask`: local float32 path coordinates,
existing flattening and even-odd semantics are processing behavior. Analytic
`contains(pixel_center)` is not an interchangeable oracle. Preserve primitive,
rounded and packed regional-mask behavior. Generic masks and ImageJ filter
targets retain their distinct policies for non-area selections.

## C reference prerequisite: I-REF

Use ImageJ 1.54r at the commit/archive identity in
[reference-pins](../parity/reference-pins.json) and
[references](../parity/references.json). Fiji's bundled 1.54p is not a substitute.
The exact source archive has now been restored to the bounded active reference
cache and verified against its original 6,146,010-byte/SHA-256 pin (519 source
files, 17,648,082 unpacked bytes). The reviewed Java runtime remains unavailable;
no Java observation has run. Restore only the remaining exact required inputs
to that shared location; never recreate retired `build/` generations. Review reference-use and
redistribution obligations separately from incorporation into the product.

Add explicit source/classes/runtime/output arguments to the selected capture
seam; existing retired-path defaults are unsuitable. No unpinned system Java.
Pin input/member hashes and bound heap, wall time and output before execution.
Use the reviewed runtime identity already recorded by repository references.

Capture `ShapeRoi(float[])` commands, integer/float bounds, processor ROI after
attachment, mask origin/dimensions/bytes, full F32 input/output words and output
before/after snapshot-mask restoration. Use fresh original pixels per operation.
Reuse literal hole/island, Q/C, off-canvas, half-pixel, implicit-closure,
self-intersection and empty cases from the pinned composite fixture. Add the
triangle `(2.2,1.2),(9.2,1.2),(2.2,8.2)` on 23x17, at sigma 1 and 12. Freeze
rank operation/radius/options cases from the current API audit as separate rows.

The rectangle `(-1e9,1)` to `(50,6)` on 80x10 is **metadata-first only**.
Pinned source inspection confirms a bounded attachment route:
`ImagePlus.setRoi(roi, false)` clears the processor mask and uses rectangular
`ip.setRoi(bounds)`, then `getProcessor()` reapplies rectangular bounds. Capture
stored integer/float bounds, local path bits, clipped processor ROI, attachment
identity and `ip.getMask() == null`. Use a fresh image and no registered listeners.
Do not substitute the `ImageProcessor.setRoi(Roi)` overload or call
`ShapeRoi.getMask()` / `ImagePlus.getMask()`: these construct the full ROI mask
before clipping. Besides roughly 5 GB of nominal storage, ByteProcessor's Java
integer dimension multiplication can overflow. Guard small masks with a checked
long product before any explicit mask call. A clipped replacement shape changes the experiment. Any
unexecuted extreme case remains explicitly pending; do not fabricate an oracle
from native output. The constructor stores bounds before translating into a
local float path and does not recompute them afterward; capture both stored and
local coordinates. The triangle's predicted processor bounds `[2,1,8,8]` remain
a source-derived prediction until Java execution. Small fixtures must record
the full ROI mask and clipped processor mask separately. For Gaussian use fresh
snapshots, explicit `blurGaussian(ip, sigma, sigma, 0.0002)` and
`ip.reset(processorMask)`; retain output bits before and after mask restoration.
Retain raw observations plus independent reader expectations.

## A implementation specification: I-CORE

Repair or extract the shared `gaussian_command_region` seam used by rank and
Remove NaNs. One planner supplies equivalent extents and mask accounting to
execution, estimates and preflight. Keep the kernel's image-border/halo policy;
ROI edges do not become image boundaries. Retain original commands, contour
order, styles, attributes and off-canvas operands as document geometry.

Audit and integrate all consumers: Application submission, prepare-operation,
legacy desktop scheduler, pooled-threshold workspace and native-workflow
metadata-replayed stage preflight. Admission must cover every selected plane/ROI
before provider reads or user checkpoints, including a valid earlier workflow
operation followed by a refused path-filter operation. Internal cancellation still works.
Do not dispatch inherited user callbacks during rejection-only preflight.

Account for path command/codec copies, unrelated retained path carriers,
one serial raster peak, preview/result retention and publication/history in
their owning layers. Reuse serial scratch; do not retain one mask per plane.
Workflow retained-state accounting must not charge the same backing twice.
Managed reservations and measured process RSS are separate quantities.

Freeze legacy versus scientific empty-target behavior explicitly. Refusal,
cancellation, callback exceptions, source mutation and stale completion preserve
owner state and existing reservations while releasing new private resources.
Successful Apply creates one appropriate history entry; no-op follows the
existing operation policy. Replaying a mixed workflow preserves captured target,
extent and selection history semantics without incidental version promotion.

## B interaction specification: I-UI

| State/event | Required result |
| --- | --- |
| Edit supported options/target | Validate the same typed request used by API; automatic and explicit Preview share it |
| New request while preview runs | Cancel/supersede older generation; older completion cannot replace current preview |
| Invalid or unsupported target | Visible actionable native diagnostic; no silent whole-plane fallback |
| Preview ready, then budget lowered below retained reservation | Preserve existing settings refusal and current preview ownership |
| Release preview, lower budget, Apply | Admission refuses visibly before work; no pixel/history change |
| Restore valid budget | Same workflow recovers without restarting the editor |
| Cancel, change owner/T/C/Z, close dialog | Release owned drafts/reservations and prevent stale publication |
| Apply, Undo, Redo, reopen/replay | Exact target/output/history through existing command pipeline in both launches |

Show path targets and positioned saved-ROI scope through existing selectors.
Settings must have actual consumers; no decorative toggles. Capture real input
and native state, including current budget, selected target, preview generation,
diagnostic and owner revisions. Label any fixture seed or fault injection.

## C independent acceptance specification: I-VERIFY

At entry expand these IDs into exact collected IDs and route selectors. Counts
are planned coverage, never prefilled passing results.

| IDs | Required independent checks |
| --- | --- |
| I-REF-01..04 | Exact pinned inputs; processor bounds separate from occupied bounds; mask bytes; sigma-1/12 and admitted rank output words |
| I-NUM-01..06 | Each admitted dtype/policy/operation; Q/C, holes/islands, implicit closure, fractional and off-canvas paths; selected words match declared comparator; untouched words exact |
| I-TGT-01..05 | Active/positioned saved targets; stepped T/C/Z; wildcard positions; multiple serial planes; empty intersection and legacy/scientific empty policies |
| I-RES-01..06 | Exact-boundary and one-byte-under admission; zero reads/checkpoints on refusal; unrelated retained paths; no double charge; serial scratch peak; released preview recovery |
| I-STATE-01..05 | Cancellation/callback exception; callback mutation/reentrancy; owner/revision changes; superseded preview; one-step Apply/Undo/Redo and no-op |
| I-WIRE-01..04 | Existing payload compatibility; record/replay drawing-to-filter; explicit extent/rebind rules; session reopen preserves original selection and outputs |
| I-HOST-01..04 | Installed headless SDK; installed Python native origins; standalone direct dependency boundary; real-input C++/Python recipes on Windows/Linux |

Use throwing/counting PlaneSources for no-read checks and literal asymmetric
C/Z/T samples/calibration for target isolation. Expected values cannot call the
planner/filter under test. Preserve existing fixtures, hashes and assertions.
Reference comparators state exact-word or numerical tolerance per case with
justification; do not introduce blanket tolerance to hide regressions.

Freeze the affected regression manifest, native group names and exclusions.
All required selected checks have zero failures/errors/skips. Retired G016
evidence and the single historical scientific-owner exclusion remain visible;
no new failure automatically joins that exclusion. Headless UI, GPU/OS input,
accessibility, macOS and full release acceptance remain separate obligations.

## Execution sequence and exit

1. C obtains bounded reference prerequisites; A audits call sites/contracts;
   B audits panels and state transitions. No overlapping P13h product changes.
2. Freeze reference oracles and exact ownership/interfaces. A implements the
   shared planner; B implements controls/probes; C implements independent readers.
3. Freeze source, build/install once per platform using existing caches. Run
   core/API/SDK and admission boundaries, then actual UI; owners repair failures.
4. Re-run invalidated checks and the declared regression manifest on a final
   common source identity. Record source drift and all unsuccessful attempts.
5. C audits every ID, commercial/material changes and installed dependencies;
   retains verified concise evidence, cleans owned scratch and lands the scoped
   packet. Document performance only where measured.

Receipt follows the three-agent plan: exact inputs/products/reports, commands,
timeouts, terminal state, exclusions, references, remaining clauses and measured
storage. Normal combined working budget is 8 GiB. A scoped exit leaves
`acceptance: false` and `complete_requirement_coverage: false`; family/M13 closure
requires its own complete proof. Missing reference evidence blocks its consumers,
not independent inventory or contract work.
