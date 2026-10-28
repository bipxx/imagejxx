# Shortest delivery paths by API and setting key

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
