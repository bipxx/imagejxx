# Manual threshold display and selection (P05k)

## Current execution card

State, 2026-10-15: **scoped runtime verified on matching installed products**.
All 18 paired UI cases, 321 affected Python/API/session cases, four focused native
suites, persistence/replay and the installed SDK consumer passed. The final
[distribution record](../../build/p05k-001/distribution-001.json) owns archive and
installed-member qualification; its presence requires a successful byte check.
Full family, upstream, managed-resource and platform acceptance remains open.
No build or test process is running at this runtime checkpoint.

Own G049's manual scalar threshold interaction and the threshold-to-selection
portion of G019; histogram display consumes G069. Preserve all broader family
requirements. C++17 owns threshold state, mask calculation, validation and
publication; standalone C++ and optional Python launch share Dear ImGui controls.

Next action: if the bound distribution record is absent, finish the source
archive/member check against `build/p05k-dist003` and the matching installed
environment, then clean verified disposable build objects. Once that record is
present, continue [P06a native scientific persistence](P06a-scientific-session-codec.md#current-execution-card).
Reuse qualified P05k products/evidence unless affected inputs change. This native
raw-range/histogram contract is independently specified; ImageJ numerical equality
and full-family acceptance remain separate requirements.

### Executed checks and remaining qualification

| Gate | Observed result and retained report |
| --- | --- |
| Native threshold and affected contracts | Passed 4 suites: document, scalar session metadata, threshold and execution in [native-affected-003.xml](../../build/p05k-001/native-affected-003.xml). Earlier [native-manual-002.xml](../../build/p05k-001/native-manual-002.xml) and the initial failed fixture setup remain retained. |
| Persistence, atomic Apply and replay | Passed against the final build with a fresh artifact directory: [native-persistence-002.log](../../build/p05k-001/native-persistence-002.log). The original passed suite remains in [native-persistence-001.xml](../../build/p05k-001/native-persistence-001.xml). |
| Installed Python API and affected regressions | Passed 321 tests, no skips, [api-regression-003.xml](../../build/p05k-001/api-regression-003.xml), including all 59 threshold API cases. Report002 retained 320 passes and one stale off-canvas-selection assertion; the reader now checks P05e's admitted zero-mask behavior, selection-only revisions and invalid-geometry guards. Production admission was preserved. |
| Installed C++ SDK | Passed 1 rebuilt consumer with 18 assertions, [sdk-002.xml](../../build/p05k-001/sdk-002.xml), against the current installed prefix. |
| Existing native regressions | 55/56 suites passed in [native-regression-001.xml](../../build/p05k-001/native-regression-001.xml). The legacy huge-plane metadata regression was repaired without changing its tests; the affected suite passed in [native-session-002.xml](../../build/p05k-001/native-session-002.xml). These reports have distinct source epochs. |
| Paired shared ImGui | Passed 18/18, no skips, [ui-manual-003.xml](../../build/p05k-001/ui-manual-003.xml): all 9 physical cases per launch, including actual tab mouse-down, captured Apply/Undo and disabled closed-source controls. The failed 14/18 report002 and unsupported-fixture report001 remain retained. This is headless ImGui input evidence; real GPU/platform/accessibility checks remain open. |
| Product/archive identity | `source-inputs-002.json` freezes 149 producer/header/binding/test/SDK inputs. Six paths differ from 001: the panel, host, native scalar-codec absence fix, probe and UI reader. The off-canvas reader correction is additionally shipped/tested in the final source archive. Wheel003, install003 and the distribution record bind the qualified outputs. |

Raw physical observations remain in `build/p05k-ui-temp001` and
`build/p05k-ui-temp002`, with the passed observations in
`build/p05k-ui-temp003`; preserve these alongside failed and passing reports.
The current installed environment is `build/p05k-env001`, with wheel products
under `build/p05k-dist003` and native SDK/application under
`build/p05k-install001`. `wheel-004.log` and `install-003.log` retain terminal
build/install results. Cancel and completed hidden-panel Apply release host
snapshots; a visible direct-action Apply draft retains its required capture.

## Settled contract and verification bounds

`manual_threshold.hpp` owns inclusive finite bounds,256 histogram bins, separate
mask/packed-selection operations, typed display state and RGB overlay blending.
U8 bins span[0,256], U16 bins[0,65536]; floating bins span observed min/max using
overflow-safe normalization. Constant floating planes occupy bin0 and have equal
edges. Matching uses original intensities; LUT/window-level affects only the
background image. The overlay tints matches red on its captured XY T/C/Z plane.
Defaults initialize the draft to the observed finite min/max; persisted state
on that same plane supplies its saved range.

The version1 `manual_threshold` display property persists bounds and T/C/Z with
native/v2 sessions. Absence means disabled. Display changes preserve pixels,
dirty state and pixel undo. Reset disables it; Cancel restores the opening state
while preserving current camera/LUT. Removing its plane or converting scalar data
to color disables it during normalization; malformed schemas remain errors.
To Selection uses a full-canvas packed area, including explicit empty results,
and skips an identical selection. It captures previous selection without adding
pixel history. Apply uses captured current-plane/ROI prepared execution and one
pixel transaction. Tagged workflow records replay with `preserve_untargeted`;
untagged legacy records keep existing behavior.

Native limits:16,777,216 pixels per plane and128MiB per operation's declared
working storage, reduced by the existing processing setting where applicable.
Histogram working storage is4,104 bytes; byte estimates separately cover mask
and packed selection. Borrowed source pixels, Python conversion copies, renderer,
document/history and global resource accounting remain separate obligations.
Finite/canonical sample validation and long loops use existing cancellation.

Focused kernel/persistence tests have30-second deadlines. Initial build cap is
30minutes, four compiler workers,8GiB generated storage; one build/install owner.
Physical cases use at most9×9×2 samples,180 frames and15seconds per case, with
60-second parent deadlines. Native/Python checks run sequentially unless their
inputs and resource budgets are explicitly independent. Keep terminal logs and
raw observations; remove only bounded disposable objects/caches after checks.

## Observable outcome and bounds

Open a dedicated manual threshold panel on an eager scalar document, adjust
lower/upper bounds against a histogram, and see a nondestructive threshold
overlay. Set and Reset alter threshold display state. To Selection creates a
native region retaining holes and disconnected components. Apply writes mask
values0/255 to the current plane/ROI through the shared prepared executor's
`PreserveUntargeted` policy. When pixels or planes remain untargeted, the common
source/result type preserves their values. When the target covers the whole
document, including a one-Z-plane owner with a full mask, the result becomes
UInt8. The panel must preview the effective output type and extent before Apply.
The existing
legacy threshold command remains compatible; its dtype conversion can also
convert untargeted planes and must not be used for this new Apply contract.
Complete ImageJ destructive conversion/stack options remain separate required
children.

Use canonical finite UInt8/UInt16/Float32/Float64 eager T1/C1 inputs for this
child, subject to the settled current operation contract. Identify any narrower
admission explicitly before implementation and leave other types required.
Keep scientific channels distinct from RGB/RGBA samples. Automatic methods,
color threshold, nonfinite policy, scientific C/T, threshold-limited measurements
and complete threshold-family parity receive later children. Existing generic
threshold commands remain compatible; the dedicated Gaussian automatic-preview
workflow is already implemented.

The overlay and histogram must refer to original intensities, independent of LUT
or window/level. Declare histogram binning and endpoint representation for each
type before writing numerical code. Use actual upstream source for claimed
compatibility; independently specified native behavior receives its own named
contract and cannot acquire upstream credit from a synthetic test.

## Consumed foundations and ownership

| Owner/lane | Files and responsibility |
| --- | --- |
| Root integration | `include/imagejxx/document.hpp`, `src/native/document.cpp`, `src/native/document_bindings.cpp`, `src/desktop/window.cpp`, `src/desktop/python.cpp`, `src/imagejxx/ui/app.py`; one authoritative display/request contract, shared lifecycle/panel wiring, targeted Python facade, builds/installations and acceptance. Coordinate any additional public API file before assignment. |
| Native producer | Existing threshold/histogram paths in `src/native/{image,analysis}.cpp` and prepared execution interfaces; add a small focused native module only if these interfaces cannot express the settled contract. Own focused native tests after root assigns exact files. |
| Independent assertions | Analytic and pinned-reference boundary/type/ROI/hole cases, histogram contract and state transitions; focused Python API and installed SDK observations with disjoint test files. Freeze expected values independently from the implementation. |
| UI assertions/review | Actual pointer/key/text interactions for histogram/range/Set/Reset/Apply/To Selection in both launches, then one concrete review of changed state/lifecycle code. Own a distinct test reader; do not fabricate coverage from programmatic actions alone. |

Consume the existing native Document/Application owner, selection revisions,
prepared operation executor, jobs/cancellation and display/orthogonal rendering.
P05j's qualified Source001/002 products and Source005 metadata archive retain their
historical identities. Its formal adoption is independent of this new feature;
do not rebuild those products to restate their acceptance. Workers share the
checkout, preserve others' edits and request root-owned shared-file changes.

## Entry decisions and exit checks

1. Fix inclusive/exclusive endpoints, bounds/invalid values, histogram binning,
   foreground/background convention and current-plane ROI/type behavior under
   `PreserveUntargeted`. Z-range Apply remains a later extension. Preserve public
   defaults; any new consumed preference
   needs schema/persistence coverage. Freeze applicable case IDs and tolerances.
2. Decide threshold state lifetime and persistence explicitly. Overlay changes
   cannot mutate pixel storage or pixel history. Set/Reset, active selection and
   destructive Apply have distinct revision/dirty/history rules. Test the chosen
   round-trip/default/migration policy rather than hiding state in the Window.
3. Capture source identity, plane, options and relevant revisions. Tab switching,
   bounds edits, source edits/close, cancel and stale completion cannot redirect
   or publish work. Declare exact no-op and repeat-Apply behavior.
4. Bound histogram/mask/overlay storage using existing workload/resource policies.
   Set numeric workload, frame, process/storage and cleanup limits in this card
   before starting a gate; source-defined admission alone gives no global-memory
   certification. Poll long scans and reuse keyed cancellation/supersession where
   asynchronous work is necessary.
5. Verify literal bound/tie pixels and all admitted dtypes, irregular ROI with
   holes, Reset and unchanged outside-ROI/source state. Explicitly compare
   one-plane/full-mask conversion with multi-plane current-plane and partial-ROI
   type retention. Verify selection-only history,
   one changed Apply/undo, invalid/stale/closed/canceled publication and round
   trips through C++, Python and both actual ImGui launches. Installed SDK/API
   checks verify public interfaces and matching origins.

Run focused checks during development. After sources settle, build/install the
changed native products and one matching wheel, execute affected installed gates
and relevant regressions once, and record scoped evidence. Reuse unaffected
evidence by exact identity. A passing overlay does not close G019/G049/G069.

Keep detailed logs in existing runners' outputs and one compact ledger here.
Remove verified disposable objects, caches and artificial test scratch after
checks; preserve qualified artifacts, raw observations, failure reports and
snapshots. Refresh source packaging once after final shipped metadata settles.
Do not create further wrapper receipts or repeat settled reviews without a
missing contract, changed input or observed failure.
