# Three-agent delivery specifications

Reviewed 2026-10-24. This is the detailed dispatch companion to the
[execution plan](../THREE_AGENT_EXECUTION.md), not a replacement for the
[M0–M13 requirements](../PLAN.md#scope-and-completion-rule). Exactly three
agents include the coordinator. No additional worker or nested agent is needed.
Existing product changes remain WIP until their scoped exit is verified.

**Current checkpoint:** [P13i scoped qualification](../parity/reviews/p13i-path-filter001/receipt.json)
now passes the final Windows/Linux matrix, including the later batch boundary
regression. Q0 below is its preserved closeout contract. Complete reviewed
landing, then dispatch CG0 gates and independent CFG1; neither successor is
qualified by P13i's results.

CFG1 subsequently [passed its scoped consumer/UI exit](../parity/reviews/cfg1-settings-wand001/receipt.json).
Its [execution](CFG1-execution.md) and [clause ledger](CFG1-qualification-ledger.md)
bind the two setting consumers; remaining configuration and geometry work stays
open. The [CG0 feasibility review](CG0-predicate-feasibility.md) records concrete
unresolved certificates; it does not approve a new product dependency.

## Review findings and scheduling decisions

1. Close P13i qualification before changing its consumed product interfaces.
   E1–E6 receipts qualify their declared slices; they do not qualify E7.
2. The P13i catalog now has 64 routes, hence 128 standalone/Python reader cases
   per platform. The former 58/116 count predates six stack-choice routes.
   The local Windows chunk ledger says `passed`; this review does not convert
   it into canonical, independently re-audited cross-platform acceptance.
3. Continuous Boolean geometry is distinct from the existing image-grid region
   Boolean implementation. Topology and numerical policy are an entry gate.
4. Configuration, selections, filtering and XY/XZ/YZ viewing remain explicit
   delivery tracks. Existing implementations must be audited before scheduling
   replacements; a missing receipt and a missing feature are different gaps.
5. Keep commercial eligibility review at discovery and dependency selection.
   NC/research-only material is excluded from incorporation. Other licenses
   require their actual obligations to be reviewed; commercial use does not
   imply permission to omit notices or source-distribution obligations.

## Dispatch and handoff protocol

| Role | Exclusive responsibility | Handoff artifact |
| --- | --- | --- |
| A: native | Packet-named public headers, C++ implementations, feature bindings and native unit tests | API/algorithm contract; version decisions; admission terms; cancellation/error behavior; exact changed-file list |
| B: shared ImGui | Packet-named controllers, panels, render consumers and actual-input probes, shared by C++ standalone and optional Python launch | Widget/default/state table; settings consumers; exact event sequences; lifecycle/refusal checkpoints |
| C: coordinator | Independent expectations/readers, CMake/central registration, scripts, SDK/install/CI, documents, evidence, cleanup and git | Frozen inputs and selectors; platform/host coverage ledger; source/product/report hashes; terminal outcomes and residual gaps |

Each dispatch states: **You are not alone in the codebase. Preserve others'
edits. Edit only the named allowlist. Report interface changes before touching
a consumer. Do not spawn agents. A/B do not build, install, commit or push.**

At entry C expands each directory responsibility below into exact filenames,
records existing dirty-file owners and obtains A/B acknowledgement. A proposed
filename or API below is a design target, not an already implemented interface.
Shared `window.cpp`, public headers and registration changes need an explicit
old-owner/new-owner handoff. C holds the sole build/install lock.

Execute one vertical packet at a time. During qualification A/B may prepare
read-only successor contracts and independent reference cases. They must not
change frozen consumers or start an overlapping integrated feature. Stop a
verification run only after identifying its owner and state; an expired tool
observation is not evidence that the process exited.

## Q0: finish P13i, then land the checked packet

Authority: [P13i specification](P13i-path-filter-targets.md) and
[remaining handoff](P13i-three-agent-handoff.md#e7--final-qualification-and-landing).

| Agent | Remaining work | Exit |
| --- | --- | --- |
| A | Hold current native source; deliver complete planner/admission call-site ledger, dtype/empty-target table, synchronous/asynchronous and manual V1/V2/V3 compatibility boundaries | Every affected consumer has an implementation and an independent case; typed manual-threshold workflow-program support remains explicitly open |
| B | Hold current UI source; deliver all 64 route/control/state rows, including failed Preview recovery and Gaussian/Mean Stack Yes/No/Cancel with display movement | Actual input is distinguished from external test injection; both launches use shared UI; no missing declared state |
| C | Audit Windows ledger and retained archive identities; qualify current Linux stack product and full route union; check current profile guards, four genuine selected CI routes, installed SDK and Python/dependency boundaries | All required contexts and exact IDs pass on matching consumed inputs; no skipped required cases, missing archives, new exclusions or count-only acceptance |

The final native selection is the handoff's 24-group union. The affected API
selection includes 394 filter/admission, 1,602 wire/type/scope, 380 pinned arrow
cases, plus 41 manual/synchronous API cases. Installed SDK targets are 414
path/filter/threshold and 35 authoring checks with 96 header identities. These
are collection targets and existing run scopes, not fresh pass claims.

Run UI captures in at most two-route chunks unless a measured peak admits a
larger chunk. Retain/verify/clean each terminal chunk before the next. Freeze
the exact union of 128 reader IDs per platform, not merely its cardinality.
Reuse a prior result only if all consumed product, reader, fixture and driver
bytes match; record reader-only addenda separately from compiled inputs.

## CG0: continuous geometry design gate

Agent A's [concrete decision proposal](CG0-continuous-geometry-decisions.md)
now supplies proposed APIs and bounded algorithm/version choices. Its ratification,
predicate-feasibility and independent-reference gates remain open.

Requirements: G016/G018/G019; consumes immutable native path carriers and
qualified authoring/history contracts. This gate may be prepared during Q0;
CG1 implementation starts after its interface and algorithm decisions freeze.

**A specification.** Review the existing raster APIs in
`include/imagejxx/selection_region.hpp`, `src/native/selection_boolean.cpp` and
`selection_region_boolean.cpp`. Propose a separate continuous operation API
with operation, ordered operands, explicit fill rules, limits and execution
control. Return native path geometry, an explicit empty result and an estimate
of retained/temporary storage and intersection work. Document:

- Finite binary64 coordinate envelope; winding rules; contour closure; treatment
  of open/stroked paths, point/line operands and image-grid masks.
- Coincident/overlapping edges, tangencies, near-coincident intersections,
  zero-length segments, self-intersections, holes and disconnected islands.
- Curve splitting and parameter ordering; which original segments are retained;
  a quantified approximation/error policy for any reconstructed segments.
- Precision/robust-predicate policy, deterministic ordering, tolerance units and
  scale behavior. Never introduce an unexplained epsilon or silent rasterization.
- Incremental admission before growth, checked arithmetic, bounded intersection
  work, cancellation checkpoints, callback exceptions and publication rollback.

Freeze native binary64 precision separately from pinned reference float32
conversion. Existing empty-command rejection must remain compatible; decide an
explicit representable zero-area result rather than using null selection, which
can mean WholePlane. A canonical zero-area closed path is a proposal to validate,
not a frozen choice. NativePathV4 creation currently allows Replace only:
continuous creation/workflow composition needs an explicit additive record branch,
strict old-version controls and a style/attribute inheritance policy.
Start promotion decisions with rectangle/polygon/freehand and native paths;
ellipse/rounded promotion needs a separately specified curve representation and
comparison policy before it can join the supported matrix.

Compare an in-house implementation with eligible pinned dependencies. C records
license, transitive sources and redistribution obligations before incorporation.
Unresolved topology behavior or eligibility blocks this packet's consumers.

Proposed CG1 A allowlist: new `include/imagejxx/path_boolean.hpp`,
`src/native/path_boolean.cpp`, a single named private implementation header,
`src/native/path_boolean_bindings.cpp` and `tests/native/test_path_boolean.cpp`.
Reuse `ImageJCompositePath` and `SelectionBooleanOperation`; do not introduce
a competing geometry model. Freeze estimate/combine signatures at CG0. C owns
source/binding/export registration and independent Python/SDK tests.
CG2 extends only the named selection-creation resolve/prepare/binding and
workflow-program seams after a separate ownership freeze. New creation/program
versions are proposals requiring strict legacy controls, not assignments to
already-supported versions. Do not change the processing rasterizer just to
implement continuous algebra.

Admission distinguishes input/output segment, intersection/event, subdivision,
work and working-byte caps. Validate every operand, including malformed later
operands after an apparently decisive empty intermediate. Freeze operands and
limits before callbacks. Workflow accounting includes retained geometry and
proven serial scratch peak without treating retained allowance as scratch.
Preserve disabled originals, explicit extent/rebind, structural no-op/history
rules and every unchanged legacy codec grammar.

**B specification.** Specify operation labels and operand order, especially
left-minus-right; active/Previous and ordered ROI-manager entries; explicit
continuous versus existing grid behavior; Preview/Apply/Cancel; empty-result
display; warning/refusal recovery and stale-input invalidation. Existing grid
commands retain their behavior. Do not select an implementation through a label
that silently changes saved commands. Freeze saved/planar scope per subpacket.

**C specification.** Create a decision table and independent analytic/reference
cases before code. Required IDs `CG-01` disjoint, `02` containment/hole, `03`
crossing curves, `04` tangent, `05` coincident/reversed edge, `06` self-crossing,
`07` tiny/large coordinates, `08` off-canvas, `09` empty/degenerate, `10`
resource/cancellation, `11` stale publication/history, `12` codec/replay.
For each record operation/operand order/fill rule, topology, bounds, selected
membership probes and geometric comparator. Raster agreement alone cannot prove
continuous topology. Freeze numeric tolerances and reference provenance.

**CG0 exit:** reviewed decision record, eligible algorithm, proposed API and exact
file allowlists, expected fixtures and supported matrix, no unresolved mandatory
contract. No feature completion is claimed by this design gate.

## CG1–CG3: bounded continuous geometry implementation

| Packet | A deliverable | B deliverable | C acceptance |
| --- | --- | --- | --- |
| CG1: native operations | Frozen CG0 algorithm and immutable geometry API; all four operations; explicit refusals for unsupported operand kinds | Private active/Previous draft and diagnostics against the real core | CG-01…10 native/API/installed SDK; all four operations; independently verify topology, membership, limits and unchanged inputs |
| CG2: active and saved ROI workflows | Atomic transactions; Previous-selection policy; ordered manager operands; strict replay/rebind and additive record handling | Real operand selection, preview lifecycle, Apply/Cancel, Undo/Redo, empty-result behavior | CG-11/12 and actual-input routes for active and saved sources; source pixels, unrelated rows, positions, attributes and metadata unchanged |
| CG3: planar consumers | Explicit XY/XZ/YZ local/world coordinate mapping and intrinsic off-canvas extents | Linked planar editing, navigation, draft rendering and recovery | Asymmetric calibration on all three planes, noncurrent T/C/Z, stale-owner refusal, session reopen and both launches/platforms |

CG2 consumes CG1; CG3 consumes both plus the existing planar contract. Primitive
promotion and curve variants must be enumerated at CG0. A subpacket refusal is a
recorded residual requirement, not permission to omit those variants forever.

## Remaining tracks: concrete packet boundaries

Audit each row against the current inventory first. C records exact missing
command/option child IDs under its G families, existing implementation/receipt,
dependency, owner and acceptance row. Only missing or invalidated clauses enter
implementation. The execution-plan queue gives the default order; a blocked
geometry reference need not block independent settings or viewing work.

The [CFG1 settings-consumer audit](CFG1-settings-consumer-audit.md) inventories
all 61 current keys and proposes a four-route Wand preference/consumer/persistence
packet with literal mask oracles. It is source-audited preparation, not executed
UI acceptance or a new settings implementation.

| Track / bounded packet | A specification | B specification | C acceptance and dependency |
| --- | --- | --- | --- |
| CFG1 settings and resource consumers, G001–G012 | Typed defaults/ranges; load/save/reset/import/export; migration and unknown-key policy; live versus next-operation semantics; resource refusal | Every setting has a control, validation message, reset behavior and observable consumer; persist accepted values, recover rejected values | Setting→native consumer→UI consumer→persistence→test table. Change a nondefault value and exercise the consumer before/after restart; malformed file and unwritable destination. Reuse settings implementation |
| SEL1 tool and manager remainder, G013–G025/G080 | Inventory remaining tools and transforms; specify geometry/coordinate/width rules, captured TCZ positions, ROI import/export and authored history | One named tool per packet; pointer/keyboard modifiers, numeric edit, manager multi-select, cancel and failed Apply | Independent geometry/position/attribute checks, mixed primitive/path/region inputs, Undo/Redo, workflow replay and reopen. Consume CG only where continuous algebra is needed |
| EDIT1 pixel/clipboard/text | Explicit draw/fill/clear/paste masks, dtype conversion and clipping; floating-paste ownership and commit; text/font encoding | Floating paste drag/anchor/cancel, clipboard errors, text/overlay editing and focus behavior | Exact outside words, one publication/history entry, Unicode, OS clipboard/installed font tests and both launch modes. Headless routes do not replace real OS checks |
| VIEW1 linked XY/XZ/YZ and C/Z/T, G026–G038 | Axis/calibration/origin mapping; capture versus current navigation; interpolation and border policy; channel visibility/LUT/composite behavior | Linked crosshair, three plane selectors, pan/zoom/aspect, physical coordinates, keys/wheel, channel controls and persistence | Fixture T=2,C=3,Z=5,Y=7,X=11 with unequal spacings and nonzero origins; independent sample formula, all axes/flips, first/last/singleton, crosshair/picking alignment, reopen |
| VIEW2 slicing and stack outputs | Orthogonal/reslice range, step, spacing/orientation/interpolation; projection/montage/animation; new-output axes/calibration and resource preflight | Full dialogs, preview, cancel/progress, range validation, output navigation and remembered parameters | Nonunit steps, reversed/invalid ranges, border samples, output shape/metadata and stale-source refusal. Oblique/curved slicing is a separate VIEW3 sampler/interaction contract |
| FILTER1 remaining 2D methods, G042–G061 | Per-command kernel, border, dtype/channel/nonfinite and target matrix; no-op and outside-word policy; settings consumers | Specialized controls/defaults, actual preview/retry/cancel/Apply, scope and legacy stack choices | Pinned eligible reference per option; whole/active/positioned/empty scope; one-byte-under budget; exact/toleranced comparator fixed before execution. Reuse P13i admission, never widen support implicitly |
| FILTER2 true volume | Neighborhood and spacing, boundary/halo rules, time/channel independence, tile equivalence and cancellation | Clearly named 3D operation, physical-unit parameters and progress/recovery | Asymmetric volume oracle that differs from independent 2D slices; tile/full equivalence and insufficient-memory refusal |
| ANALYSIS1 G062–G073 | Calibration/units, threshold/ROI scope, labels/connectivity, statistic/result schema and deterministic ordering | Measurement/particle/profile dialogs, table/plot selection, export and settings | Known calibrated shapes/signals, positioned scopes, empty results, numeric precision, table round trip and stale owner; one analysis workflow per packet |
| IO1 G074–G084 | Per-format series/axes/dtype/calibration/ROI metadata, malformed-input bounds and atomic writes | Series/options, file info/recent/recovery/export/print/close flows | Pin every format variant and read/write direction; metadata round trip, truncated input, disk failure and recovery. Reuse existing TIFF/session codecs |
| DATA1 / VOL1 G039–G041/G090/G091 | Tile/pyramid/multisource/cache contract; volume/surface transfer and picking; bounded ownership | Large-data navigation, volume/surface controls, transfer editing, progress/cancel | Dataset larger than cache; managed bytes and RSS measured separately; cold/warm latency, tile seams, cancellation and real GPU/backend checks |
| FIJI1 per eligible package, G092–G099 plus discoveries | Exact version, source/transitive/model/data eligibility; native algorithm and package-specific result/axis contract | Complete named package workflow and recoverable failure states | Redistributable reference inputs and full end-to-end numerical/UI/SDK cases; schedule registration, stitching, tracking, segmentation etc. by their actual data/model dependencies |
| AUTO1 G085–G089/M12 | Versioned commands, deterministic replay/rebind, batch isolation and native extension ABI/lifecycle | Recorder, workflow/script editor, batch/jobs and extension install/remove/error flows | Captured-versus-current scope, disabled steps, malformed/old versions, no-prefix publication, restart/unload and optional-runtime absence. Native equivalents do not confer Java JAR compatibility |
| RELEASE1 M0/M13 | A supplies remaining core/platform compatibility fixes | B supplies genuine OS/GPU input/accessibility fixes | C covers Windows/Linux/macOS core, standalone and optional wheels; clean install/uninstall, notices/SBOM, performance, full eligible-command inventory and strict certification. Retired G016/Auto evidence gaps remain explicit |

## Mandatory acceptance for every packet

Use child IDs with separate implementation, verification and evidence-availability
states. An acceptance row names input/options/dtype/axes/target, expected output,
comparator, platform/launch, actual UI events, source/product/report identities,
terminal outcome and residual clauses. API presence or a screenshot is insufficient.

All applicable rows cover C++ core, installed public C++ SDK, Python binding,
standalone ImGui and Python launch; Windows/Linux scoped checks precede landing,
with macOS and genuine OS/GPU/accessibility gates tracked for release. Preserve
legacy defaults and codecs; reject malformed/unsupported inputs explicitly.
No-op, Cancel, error, stale result and failed allocation preserve source/history
according to the frozen contract. Test cancellation/throws/reentrancy where the
operation exposes them. Include config consumption, replay and save/reopen.

Only C may mark a packet scoped-qualified after auditing every required row.
Full 99-family completion is a separate coverage decision. Current inventory
remains **47 in progress, 52 open, zero complete**; no percentage is inferred.

## Storage, efficiency and resume requirements

Reuse one compiler cache and one runtime per platform; never recreate `build/`.
Count `.tools`, dependencies, source mirror, scratch and retained evidence in
the combined 8 GiB budget. Older measurements that omitted SDK roots do not
establish current headroom. Measure before/after substantial work and admit peak
raw scratch plus compression workspace and expected cache growth.

Use fresh owned scratch, no per-agent build trees/environments/worktrees. After
success or failure retain concise diagnostics and necessary lossless captures,
verify archive/member hashes, then remove only checked absolute owned paths,
without traversing links. Retiring superseded evidence requires an additive
availability record. Do not archive complete caches, SDKs or environments.

Each resume card states current packet/gate, source freeze, A/B/C owners, live
process or terminal state, next exact command/selector, required storage and
blocker. Documentation-only review validates links, inventory and diff hygiene;
it does not rebuild the application or manufacture fresh test evidence.
