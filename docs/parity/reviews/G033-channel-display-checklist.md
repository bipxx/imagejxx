# G033 pinned channel-display workflow checklist

Reviewed 2026-10-15. This is a source-inspected registration checklist for
[G033](../inventory.json), with explicit consuming requirements from adjacent
families. It changes no manifest, accepts no requirement and closes no family.
The 49 existing option requirements remain unchanged. `CD01`–`CD40` below are
review-local candidate labels, not registered requirement IDs or implemented
children. Each needs assignment, applicable surfaces and evidence in the option
inventory before a whole-family exit can be defined.

The [P06c execution card](../../packets/P06c-scientific-channel-display.md) remains
authoritative for its evolving implementation and gate status. Its initial
Color/Grayscale/Composite **SUM** outcome covers a useful subset. It does not
cover every channel-display command, all LUT workflows, GUI Auto-contrast or
channel arrangement. “Follow-on” below means outside that scoped exit; it does
not assert that every legacy adapter lacks all related functionality.

## Consumed reference and evidence boundaries

The standalone reference is **ImageJ 1.54r**, tag `v1.54r`, commit
`62db90a269cccc1ac79f658e4602043214ed055b`. Read-only inspection rehashed
[the local source archive](../../../build/references/downloads/imagej-62db90a269cccc1ac79f658e4602043214ed055b.zip)
to SHA256
`09e31a88191d10c7f79c607cb6bb62342476bde733c5d932d5825fd553796665`.
All 28 members in the identity appendix were read and verified byte-for-byte
against the local inspected tree
`build/references/roi-style-imagej154r/sources/`. Member names below are relative
to the archive prefix
`ImageJ-62db90a269cccc1ac79f658e4602043214ed055b/`. No reference process,
capture, build or product test ran for this review.

The separate [Fiji pin](../reference-pins.json) is
`20261007-1617/latest-win64-jdk`, archive SHA256
`8e33a11c7e79eb02c055bcee40de806d16e59f0b42e58c4689bd23eb47d990bb`.
It bundles **ImageJ 1.54p**, not 1.54r. That archive was not rehashed or executed
here. Fiji descriptor and LUT-file facts below come from the existing pinned
[command candidates](../commands.json) and
[component index](../fiji-components.json); they are not inferred 1.54p runtime
matches. The recorded Azul Zulu 21.0.7 runtime is reference infrastructure only.

Existing numerical reference receipts remain immutable:

| Receipt | Observations | SHA256 |
| --- | ---: | --- |
| [channel-display-entry-001.json](../../../build/p06c-001/channel-display-entry-001.json) | 63 | `5360efa8a9f605d845aec107d6d9e1ee940e9732d0bdafdbd07a4c213d1bb471` |
| [range-edges-001.json](../../../build/p06c-001/range-edges-001/range-edges-001.json) | 8 | `907c30df12529291e295427380b3d18e5e91e964f55791cdd3816cc987fc5c23` |
| [range-edges-002.json](../../../build/p06c-001/range-edges-001/range-edges-002.json) | 1 | `e8f34f29a4d79b9530f706df2fb054f4c40df395e65a3cdcb2a3f57c756cc8f2` |

Those 72 observations cover their recorded numerical/default cases. They do not
establish GUI Auto, Max/Min/Invert, arrangement, all editor/file workflows,
threshold coexistence, platform behavior or product runtime equivalence.
Source inspection identifies required behavior and capture gaps; it supplies
neither a new numerical oracle nor a passed UI gate.

## Entry routes and family boundary

`IJ_Props.txt` pins Image > Color routes `color01`–`color05` and
`color07`–`color11`: Split Channels; Merge Channels; Arrange Channels; Invert
LUTs; Channels Tool (`Z`); Stack to RGB; Make Composite; Show LUT; Display LUTs;
Edit LUT. The Channels Tool **More** menu additionally exposes the alias Create
RGB Image → Stack to RGB, Make Composite, Split/Merge, Show LUT, Invert LUTs and
Red/Green/Blue/Cyan/Magenta/Yellow/Grays. Help describes display modes and commands.

Image > Adjust routes `adjust01`–`adjust03` open Brightness/Contrast (`C`),
Window/Level and Color Balance. Image > Lookup Tables exposes Invert LUT,
Apply LUT and the named tables listed in CD18. File > Import LUT and File > Save
As LUT are separate import/export routes (`import03`, `save13`). Menus also adds
installed `.lut` files dynamically. Those related routes require usable shared
ImGui entry points or an explicitly reconciled equivalent, not necessarily an
AWT-shaped panel or identical window geometry.

G033 owns composition and channel display state. G057 owns the full B&C/W&L
consumer; G058 owns Color Balance; G059 owns LUT display/editor behavior; G080
owns LUT/TIFF interchange; G031/G032 own scientific conversion/split/merge;
G062 and stack/ROI contracts own metadata and positioned-annotation remapping;
G085 owns executable workflow behavior. Their consumed subcontracts remain
required for the connected G033 workflow. Linking them avoids duplicate
implementation; it does not waive the behavior or imply those whole families
must finish before every G033 increment.

ImageJ scalar processor types here are 8-bit, unsigned 16-bit and 32-bit float;
24-bit RGB is a separate conversion/application case. Native Float64 and S4
have no direct counterpart in these inspected scalar composite paths. Their
existing legacy behavior, explicit refusal or declared extension must remain
clear. The `TRANSPARENT=4` constant in `CompositeImage` is explicitly marked
unimplemented; its existence alone is not an additional implemented reference
mode to reproduce.

## Registration candidates

All rows inherit the evidence obligations after the tables. The source column
names exact members/methods, together with the identity appendix. Defaults and
enable conditions are source observations unless marked **owned**. A candidate
may split into several registered options; none may disappear because its
controls live in an adjacent family.

### Channel state, projections and ranges

| Candidate | Pinned workflow, defaults, types, axes and enable conditions | P06c boundary; remaining work and actual dependency |
| --- | --- | --- |
| CD01 — Tool lifecycle and admission | `ij/plugin/frame/Channels.java`: modeless singleton, reopening brings it forward; active-image changes synchronize mode/checkboxes, C-count changes rebuild the window; in-process location retained. No image returns without mutation. For non-composite input, derivedC is C, except C1 with stack≤4 uses stack size: RGB or1<derivedC<8 prompts Make Composite; other inputs report a composite requirement. Constructor initially shows Composite/checked boxes, then synchronizes the active image. | P06c has an owned explicit enable/disable contract. Reconcile that with reference conversion prompts, active-owner switching, close/reopen, help, shortcuts and error/cancel behavior in both launches; do not claim its initial transient widget defaults are a persisted document default. |
| CD02 — Make Composite | `ij/plugin/CompositeConverter.java`: already-composite input switches to Composite. RGB splits into three byte channels across Z/T. Non-RGB requires C≥2, macro C≥1, or a special three-Z-slice stack. Display Mode choices Composite/Color/Grayscale, default Composite; C1 with Z×T>7 defaults Grayscale in the admitted macro path. Three-slice special case relabels/reinterprets slices as RGB channels. | Follow-on entry/conversion workflow, consuming G031/G032/G060 and atomic source/output publication. The special case changes dimensions/labels before its dialog: capture cancel behavior before choosing an owned corrected policy. RGB single-image source hiding differs from stack closing. An explicit scalar display-state enable is not this conversion. |
| CD03 — Defaults and C cardinality | `ij/CompositeImage.java` constructor/`setupLuts`: Red, Green, Blue, White, Cyan, Magenta, Yellow, White through C8; >8 initializes white tables and Composite requests fall back to Color. Saved per-C ranges/custom tables win; otherwise the first processor range is inherited across C. Constructor without a mode defaults Color, unlike Make Composite's dialog. C1 stacks≤8 may become channels when dimensions were not explicitly set; stack multiplicity/dimensions are validated. Channels Tool caps checkboxes at eight. | P06c scopes palette/range inheritance, explicit >8 normalization, strict stored-state refusal and singleton gray Composite. Qualify each entry context rather than imposing one universal upstream default. Above-eight active-C access and panel presentation still need an explicit functional contract. |
| CD04 — Visibility versus active C | `Channels.itemStateChanged`, `CompositeImage.updateImage`: in Composite, checkboxes toggle contributing channels; in Color/Grayscale, clicking a checkbox selects C while keeping Z/T. Color/Grayscale display active C irrespective of Composite visibility. The source updates active flags on mode/navigation transitions. | P06c scopes renderer contributor semantics and state visibility, plus C navigation consumed from P06b. Reference-equivalent checkbox behavior and mode-transition flag retention/reset need registration and actual input evidence; independent visibility toggles alone do not cover the selection action. |
| CD05 — Mode transitions | `Channels` choices: Composite, Color, Grayscale, separator, Composite Max, Composite Min, Composite Invert. Separator dispatches SUM. Projection is the `CompositeProjection` property; Color/Gray and switching back interact with retained projection and active flags. Min/Invert versus Sum/Max may automatically run InvertAllLuts based on the active LUT. | Only Color/Gray/SUM are in P06c. Projection plus automatic LUT transforms must be one validated, recorded state transition. Do not add unknown fields to strict V1 or silently execute SUM for unsupported projection records. Requires explicit state/action versioning or other reviewed compatible representation. |
| CD06 — SUM byte mapping and edge cases | `CompositeImage.updateImage`, `ImageProcessor.updateComposite`, Byte/Short/Float processors: per-C range → processor-specific byte → LUT → independently saturated RGB sum. Include hidden C0, all-hidden, custom entry0, singleton, constant/equal ranges, fractional endpoints and quiet NaNs. Source has a stale-first-pixel/cache artifact when C0 is hidden. | P06c scopes the captured canonical mappings and **owned** raw-finite-double extension, explicit equal-range policy and fresh zeroed accumulator. Keep those deviations documented. Reuse verified 63+9 evidence only for matching inputs; modes below have different code paths. |
| CD07 — Composite Max | `CompositeImage.updateImage`, `ImageProcessor.updateComposite`: component-wise maximum after each channel's range/LUT mapping; not maximum raw intensity followed by one LUT. Active set, starting accumulator, custom/nonlinear LUTs and projection-triggered LUT transforms matter. | Follow-on native/API/ordinary+XYZ renderer and action schema. Capture independent literals for 8/16/F32, mixed palettes, hidden first/all channels, one contributor, repeated redraw and transitions. No stack geometry prerequisite for this display-only child. |
| CD08 — Composite Min | Same members: component-wise minimum, initializing untouched color components with white when C0 is inactive; tool transition may invert LUTs. Source initialization contains the first-pixel artifact. | Follow-on paired numerical/state/renderer evidence and declared deterministic empty-contributor policy. Must distinguish desired min behavior from upstream cache artifacts and custom-table inversion side effects. Consumes the same projection representation as CD07. |
| CD09 — Composite Invert | `CompositeImage.makeInvertedComposite`: a separate 8/16/F32 raw-sample mapping path, floor/clamp indexing; RGB result sums visible LUT values then subtracts `(visible_count−1)×255` per component and clips below zero. Empty/one-channel cases and equal/NaN ranges cannot be assumed to match SUM mapping. | Follow-on targeted capture before freezing arithmetic, including zero contributors, equal ranges and custom tables. Do not implement this as merely reversing a final SUM RGB image. Its temporary memory/read behavior also needs a bounded native design. |
| CD10 — Invert all LUTs | `SimpleCommands("invert")` dispatches bundled `macros/InvertAllLuts.txt`. It transforms every entry of every channel using component complement plus an HSV half-turn, rounds RGB, switches projection as needed and ends at C1. Channels Help's “linear/single-color only” narrative is stale relative to that dispatch. | Missing full all-C transformation/transition workflow. Requires literal table/projection/position capture, one atomic native action and recording/replay across all C/Z/T. Do not substitute single-channel index reversal or silently preserve active C while claiming exact reference behavior. A corrected position policy would be an explicit owned difference. |
| CD11 — Explicit per-C range | `CompositeImage.setDisplayRange`/LUT setters/getters: ranges are channel display state shared across Z/T, not per-plane pixel edits. Integer processor endpoint conversion and F32 endpoint storage differ. Changing active-C LUT preserves its range. | P06c scopes exact requested ranges, effective-range consumers, quiet-NaN mapping and source/history preservation. Keep intensity calibration (CD16) separate from spatial calibration, and retain legacy flat fields when recognized state is disabled. |
| CD12 — Reset range | `CompositeImage.resetDisplayRange` and `ContrastAdjuster.reset`: U8 default range; U16/F32 processor reset; nonzero default unsigned-16 range can reset all C. `resetDisplayRanges` reads the first C stack processors and is not guaranteed to be current T/Z. | P06c **owned Reset range** explicitly scans committed live XY at captured T/Z, U8[0,255], U16/F32 finite min/max, all-NaN[0,1] plus empty-finite observation. It ignores previews. This does not implement upstream GUI Auto or every upstream initialization/reset/16-bit setting path. |
| CD13 — GUI Auto-contrast | `ContrastAdjuster.autoAdjust`: current processor/raw statistics and ROI; 256-bin histogram; bins exceeding one tenth of pixel count are ignored. Initial threshold divisor5000; repeated Auto halves it until reset. Selected bounds use histogram origin/bin size, equal bounds fall back to extrema and failed bounds Reset. RGB has snapshot/ROI special handling. | Missing G057 consumer and G033 channel targeting. Capture repeated clicks, histogram/ROI rules, uniform/empty-finite/NaN cases, signed/intensity calibration, live T/C/Z, mode and Shift/All targeting. Current Reset is not equivalent. Recorded Enhance Contrast saturated=0.35 is a different reference dispatch; qualify replay policy explicitly. |
| CD14 — Histogram and interactive adjustment | `ContrastAdjuster`: histogram and Log scale (initially off); B&C Minimum/Maximum/Brightness/Contrast sliders and labels; W&L Level/Window controls. Slider scale depends on data/display range. Equal byte bounds may set threshold state. Calibration-bar overlays refuse adjustment. | Missing shared histogram-linked UI/API consumer, with current-C/T/Z/ROI refresh, log/linear plots, clamping, editable numeric counterparts and no hidden pixel writes for scalar display adjustments. Consumes G057 plus threshold/analysis presentation contracts, not just P06c numeric fields. |
| CD15 — Color Balance targeting | `ContrastAdjuster.addBalanceChoices`/`doUpdate`: RGB choices Red/Green/Blue/Cyan/Magenta/Yellow/All; scalar gray uses LUT level; composite presents Channel1–6/All and selects existing C. Missing selected channel falls back to All. Shift or composite All requests all-channel redraw; synchronized ranges occur in Composite display mode, while Color/Gray follows active-channel redraw. Color Balance Apply on a composite image returns without destructive application. | Shared G058/G057 dependency. Freeze any usable >6-channel extension rather than copying a fixed selector limit into native storage. Record explicit resolved channel targets. RGB snapshot/mask changes belong to pixel transactions; scientific scalar display state must not accidentally use that destructive path. |
| CD16 — Set range, propagation and unsigned16 setting | `ContrastAdjuster.setMinAndMax`: calibrated Minimum/Maximum numeric fields; Unsigned16 range choices Automatic,8,10,12,14,15,16-bit. Propagate to other open images defaults false; extra all-other-C checkbox appears for composite C>1 and defaults false. Composite propagation requires matching depth/C count and an open window; copies each C range and restores destination C. RGB propagation refuses. Range-setting changes can trigger reset. | Follow-on shared settings/G057/G062/G085 consumer. Requires calibrated↔raw conversion, all-C and multi-owner atomicity/policy, incompatible/busy/closed destination handling and session/restart setting evidence. `ImagePlus.setDefault16bitRange` is process state; source inspection alone does not prove preference-file durability. |
| CD17 — Set Window/Level | `ContrastAdjuster.setWindowLevel`: numeric Center(Level) and Width plus Propagate to all open images (false); converts calibrated bounds back to raw and rejects reversed bounds. Shares propagation logic and range recording. | Shared G057 closure requirement consumed by channel display. Need exact range equivalence, intensity-calibrated cases, invalid/cancel, per-C navigation preservation and multi-owner record/persistence tests. It is a separate dialog, not an alias for two uncalibrated range inputs. |

### LUT workflows and data-changing entry points

| Candidate | Pinned workflow, defaults, types, axes and enable conditions | P06c boundary; remaining work and actual dependency |
| --- | --- | --- |
| CD18 — Complete named table selection | `IJ_Props.txt lookup04`–`lookup15`, `LutLoader`, Arrange Channels thumbnail popup: Fire, Grays, Ice, Spectrum, 3-3-2 RGB, Red, Green, Blue, Cyan, Magenta, Yellow, Red/Green. More exposes the seven basic named choices. Table construction/interpolation is palette-specific. | P06c's initial eight named colors are not the complete menu. G059 needs each remaining built-in's independently pinned table/meaning, menu/API exposure and custom-table fallback policy. Assets/formulas must pass incorporation review; reference inspection alone grants none. |
| CD19 — Assign/import custom LUT | `LutLoader.showLut`: non-RGB only; current scalar/composite channel normally gets the LUT. **When composite is Grayscale, loading changes mode to Color and assigns the LUT to every C**, restoring active C. Without an image it creates a 256×32 LUT ramp preview. LUT name is stored; Grays removes its name. | P06c scopes explicit named/custom256 tables but not every loader side effect or no-image preview. Register scope conversion, per-C/all-C behavior, names and previews in G059/G080; retain range and pixel bits. File/URL load failures/cancel require visible atomic refusal; network capability must be separately defined. |
| CD20 — Invert LUT, singular | `LutLoader.invertLut`: reverse table indices for active channel/scalar image, retaining display range. Distinct from CD10's all-channel complement/HSV/projection action and from destructive pixel Invert. | Follow-on shared G059 action. Require exact arbitrary/custom entry reversal, C/Z/T behavior, twice-invert identity, native record and both UI entry routes; do not equate table inversion with RGB inversion. |
| CD21 — Show LUT and List | `ij/plugin/filter/LutViewer.java`: current non-RGB channel's RGB curve plot/color bar; List produces table entries (index,R,G,B), with grayscale plot variant. Not a pixel write. | Shared G059/G071 results-view dependency. Actual shared plot/table UI, numeric table API/export, active-C refresh and independent table literals are required; an encoded state dict alone is not this UI. |
| CD22 — Display all LUTs | `SimpleCommands("display")` → `macros/ShowAllLuts.txt`: enumerates available LUT names, creates labeled ramps/montage (four columns), saves/restores settings and uses generated temporary images. | Additional G059 workflow beyond Show LUT. Provide equivalent accessible palette browser/result with full reviewed palette membership, correct labels and no source/settings pollution. Capture output and source/session lifecycle; a private test fixture does not establish user-facing browsing. |
| CD23 — Edit LUT | `ij/plugin/LUT_Editor.java`: image required, RGB refuses, initial map must have256 entries; non8-bit opening resets range. A 16×16 grid permits entry color editing, drag-range endpoints/ramp and index/RGB readout (calibrated value for non8-bit). Open/Save/Set/Invert controls, live updates, outer OK/Cancel and edited-name suffix. Upstream recording is suspended during the editor. | Missing full G059 shared editor/API with validated table edits, preview/draft lifecycle, exact accepted table recording, stale-owner/close/refusal behavior. Opening/range-reset and nested-cancel side effects need capture and explicit owned policy; do not assume all Cancel paths restore every display field. |
| CD24 — LUT map size and scaling | `LUT_Editor.ColorPanel.resize/scale`: Number of Colors clamped2..256; Replication, Interpolation, Spline Fitting; remembered in-process initial scaling choice Interpolation. Shrinking always uses replication; expansion uses selected method. Nested Set cancel invokes LUT restoration. | Follow-on G059 table model/UI/API. Current P06c custom representation is exactly256 RGB entries; choose reviewed normalization or a versioned variable-map representation before claiming map-size parity. Test resulting indexed display and export, not only an editor label. |
| CD25 — LUT interchange | `LutLoader.openLut/openBinaryLut/openTextLut`, `FileSaver.saveAsLut`: raw768 bytes in planar R/G/B order; NIH ICOL32-byte header, smaller tables interpolated; text3/4 columns and256..258 rows interpreted by loader. Loader has local10000-byte ceiling and raw970-byte legacy admission. Save refuses RGB or maps<256 and writes768 bytes. | Shared G080 formats contract, including byte order/layout, supported variants, malformed/truncated/oversized/numeric inputs, cancel and safe file replacement. URL behavior is a separate transport scope. Independently decode saved bytes; no approved asset may be assumed from format support. |
| CD26 — Apply LUT / Apply range | `ij/plugin/filter/LutApplier.java` and `ContrastAdjuster.apply`: destructive display-range application is separate from LUT visualization. Apply LUT admits8G/8C/16/RGB, not F32; active threshold dispatches to Thresholder. Default8/RGB range refuses ordinary interactive Apply. Stack/current-plane choices, ROI masks,16-bit default range, RGB snapshots, virtual-stack refusal and undo limitations differ by route. | Shared G057/G059/G060/filter target/threshold dependencies. Register the two entry routes and explicit target/range/ROI/output policies; preserve scientific atomic transactions, cancellation and history under the owned contract rather than silently reproducing upstream partial writes/no-undo. P06c display changes do not close this pixel operation. |
| CD27 — Arrange full channel order | `ij/plugin/ChannelArranger.java run`: GUI admits2..9 channels; New channel order initially123…C; text rejects duplicates/out-of-range digits/overlength by restoring last valid text. Full permutation splits and remerges actual channel planes across Z/T into a new image and closes source; restores mode and attempts position/overlay mapping. | **Dataset C-axis edit, not display permutation.** Needs shared native axis/position/channel-state/label/ROI/overlay/calibration remapper and one source-bound publication/action contract. Use non-self-inverse permutations and nonzero T/Z/C landmarks. No dependency on a volume renderer. See ordering findings below. |
| CD28 — Arrange subset/reduction | Same command: empty order is no-op; shorter sequence prompts Reduce Number of Channels with confirm/cancel, one remaining C becomes a single-channel image. Static API differs from GUI: it truncates excessive length and does not explicitly reject duplicates. | Shared subset/remapper contract with explicit omitted-channel, wildcard position and active-C policy. Freeze safe native strict index/duplicate semantics and document upstream API differences. Validate full original request before mutation; memory/cancel/annotation refusal cannot close or partially alter source. |
| CD29 — Arrange thumbnails and LUT popup | `ThumbnailsCanvas`: old/new mapping thumbnails at captured Z/T, pointer selection/context LUT popup with all12 built-ins from CD18; drawing temporarily changes mode/C then restores them. LUT popup executes live on source before Arrange confirmation. | Shared ImGui equivalent must expose old/new mapping and keyboard/numeric operation, not just a raw API permutation. Source popup changes may survive outer Cancel; capture that and declare owned draft policy. Avoid claiming passive thumbnails/source cancellation until measured. |
| CD30 — Split Channels | `ChannelSplitter`: Composite splits all C over Z/T, preserves selected Z/T in shown results and closes source; Grayscale mode applies gray LUT to split results. RGB splits into byte red/green/blue stacks, retaining source only with Alt. Static split/getChannel differs from interactive source-closing route. | Shared G032 full-axis command, source-retention option/modifier/API, per-channel calibration/range/LUT/labels/annotations and output ownership. Existing Z-only legacy splitting is not the entire scientific workflow. Remap recognized channel state or refuse opaque transformations explicitly. |
| CD31 — Merge Channels | `RGBStackMerge`: seven selectors C1red/C2green/C3blue/C4gray/C5cyan/C6magenta/C7yellow, including None and source-name autofill. Initial nonmacro Create composite=true, Keep source=false, Ignore source LUTs=false; remembered in-process choices; macro defaults differ. Validates image sizes, stack lengths, depth for composite, Z/T for hyperstacks; multichannel/RGB hyperstack sources refuse. C4+ and some RGB combinations change conversion path. | Shared G032/G060 full-axis source selection, palette/range inheritance, missing/duplicate sources, calibration conflicts and keep/close/output policies. Needs explicit independently tested supported variants, not blanket arbitrary multi-C merge inference. Source mode may be changed and return before merge; capture/reconcile this interaction. |
| CD32 — Create RGB / Stack to RGB | `RGBStackConverter`: composite current Z/T or chosen full axes; general hyperstack dialog offers Slices ifZ>1 and Frames ifT>1, each true; Keep source initiallytrue/remembered, live dimensions/MB estimate. Unchecked axes use current coordinate. C-only Composite produces one RGB image; C-only Color/Gray produces per-C RGB stack. Ordinary2/3-slice8/16 gray stacks and HSB-marked three-channel paths are separate conversions. Selected display modes/LUTs/ranges determine RGB; overlay copies/crops are separate from raster baking. | Shared G031/G060/G062 conversion/output contract. Test all axis choices, nonzero C/Z/T, source keep/close/cancel, calibration/metadata, HSB variant and annotation policy. This is display-rendered S3 output, distinct from P06e's exact scientific scalar-plane/range exports. Reference hyperstack indexing/position edge cases require capture before mirroring. |

### Cross-cutting consumers, persistence and surfaces

| Candidate | Pinned/reference or explicit owned interaction | P06c boundary; remaining work and actual dependency |
| --- | --- | --- |
| CD33 — C/Z/T and XYZ consumers | `CompositeImage.updateImage` selects channel planes at current Z/T; LUT/range state applies across them. `ij/gui/StackWindow.java` supplies C/Z/T controls, lock-enabled selectors and stack-wheel/modifier behavior. ImageJ1 display math alone does not establish a native linked XYZ policy. | P06b supplies navigation; P06c scopes identical owned composition in ordinary XY and linked XY/XZ/YZ, with scalar active-C readback and all contributors in cache keys. Preserve world geometry/aspect/crosshair, active-C selection and final-coordinate source attribution on each axis. General C-axis edits remain CD27–31. |
| CD34 — Preview, threshold and annotations | Source processors/composite paths interact with threshold LUT state; B&C equal-byte bounds can update thresholds. The owned P06c policy selects retained staged/live sources independently per contributing C at final T/C/Z; threshold tints live active-C XY once only when it contributes; annotations render once on XY. | P06c requires independent host/physical evidence for non-active-C previews, mixed live/staged strips, crosshair-away/back, threshold and overlays, source preservation and cache invalidation. These owned rules are not yet proven upstream equivalence by the72 numerical receipts. Future projections must consume the same source selector and overlay boundary. |
| CD35 — Durable native state | Upstream `CompositeImage` holds per-C tables/ranges/mode/visibility; data changes and attribute copying interact with them. Owned native V3 uses strict versioned recognized channel state, legacy flat-field preservation and opaque future-state transport. | P06c scopes Save/Open, absent/known/malformed/future states, count/type changes and explicit disable; every new projection or table model needs compatible persistence and desktop interpretation. Shape-changing commands need matching state remap/refusal before publication, never silent channel deletion or unknown-state interpretation. |
| CD36 — TIFF display metadata | `FileSaver.saveDisplayRangesAndLuts` stores2×C ranges and optional custom LUT bytes; description stores C/Z/T, hyperstack and display mode. General image properties are saved/read through FileInfo, which can carry CompositeProjection. `TiffEncoder/TiffDecoder/FileOpener` implement those transport paths. | Shared G080/scientific-file contract. Require independent reference round trips for ranges/LUT/mode/projection, dimension/order/calibration and explicit behavior for visibility/active position. Source transport code alone does not prove every field is saved/restored. Native V3 qualification does not claim ImageJ TIFF interchange. |
| CD37 — Recording, strict replay and rebind | `Channels` records projection/mode, full active-channel bitstrings or active C depending on mode; scripts and macro strings differ. Range actions record requested/resolved values with precision/type differences; Auto records Enhance Contrast; editor suspends recording. | P06c scopes source-bound normalized Set/Reset/Disable records, exact stored-state replay, explicit full rebind and recording-on/off atomic append. New workflows need their own normalized records, output routing, strict metadata/index validation, preserved navigation/history, callback-safe cancel/error and replay widgets. Native records are not an implemented ImageJ macro interpreter. |
| CD38 — Settings, defaults and draft lifetime | Source remembers Channels location in-process; B&C location uses `b&c.loc`; GUI scale affects controls; unsigned16 default, repeated-Auto state, merge/RGB Keep choices, LUT directory and editor scale choice have different lifetimes. Static/process variables are not automatically preferences. | G001–G012 consumers plus P06c draft lifecycle: enumerate which settings are session, document or application persisted; version/migrate/import/export/reset them with actual effects. Preserve invalid text across unrelated updates, explicit apply/reset success rebasing and source-tab/stale/refusal behavior in both launches. Do not mark37 settings definitions as complete G033 consumer proof. |
| CD39 — Physical and platform usability | Source routes keyboard shortcuts/listeners, pointer popup/grid/thumbnail operations, numeric dialogs, modeless focus and scaled controls; it has Windows/macOS/Linux layout/menu differences. Dynamic LUT menu sorting is explicit on Linux/macOS. Functional replacement need not reproduce platform-specific AWT pixels. | Both installed C++ and optional Python ImGui launches need real pointer/key/text workflows, focus/keyboard alternatives, readable DPI scaling, visible errors/help and file-dialog/cancel behavior. Populate supported OS/architecture/compiler/Python/graphics and assistive-technology evidence; current platform manifest is empty. Headless ImGui input traces are insufficient for real OS/accessibility certification. |
| CD40 — Limits, ownership and type boundaries | Source maintains caches and can allocate/read many channels/planes. Native owned compositor is bounded, metadata-preflights before reads/callbacks and streams planes; source/provider lifetime and preview attribution are explicit. P06c admits scalarU8/U16/F32; legacyF64/S3/S4 remain separate. | Preserve native/Python/SDK limit, overflow, cancellation, callback exception/GIL/lifetime and zero-contributor no-read checks. Desktop applies the compositor/configured cap without claiming total renderer/GPU/process reservation coverage. New projections, Auto histograms, editor results, remap and RGB outputs each need their own preflight/working-set policy. Unsupported paths must refuse atomically, not cast or materialize TCZ silently. |

## Ordering is an axis edit: policy decisions still required

For a requested order `[3,1,2]`, `ChannelArranger.run` builds output C1 from
source C3, output C2 from source C1 and output C3 from source C2, across Z/T.
Shortening the order removes channels. A display-only permutation could be an
additional owned view feature, but it would not implement this reference command.

The inspected source has specific hazards that must be captured rather than
silently made into a general remapper contract:

- Active-C and positioned-overlay mapping uses the forward `newOrder[oldC−1]`
  expression, not the inverse needed to follow an original channel through an
  arbitrary permutation. Removed-channel cases need a deliberate policy.
- The source mutates/aliases overlay objects, closes the original and marks the
  result changed. Its static API and GUI validation admit different order sets.
- Thumbnail palette actions mutate source display before final confirmation;
  Cancel is not evidence of universal display rollback. The source also
  temporarily changes C/mode while drawing thumbnails.

Required shared inputs are the dataset-axis mapper; spatial/time and channel
labels; active C; per-channel range/LUT/visibility/projection meaning; current,
previous and stored ROI/overlay positions including wildcard zero; unknown
metadata preservation/refusal; storage limits; and atomic output/history/record
publication. Verify landmark pixels at nonzero T/Z, a non-self-inverse order,
full reversal, deletion before/at/after active C and a one-channel result.
Declare whether the owned editor follows corrected inverse mapping, and retain
the observed upstream discrepancy. Neither scalar slicing's cleared-annotation
output policy nor the compositor's channel vector is a general remapper.

## Fiji candidates and commercial eligibility remain distinct

The pinned metadata contains these additional ImageJ2 descriptors in
`Fiji/jars/imagej-plugins-commands-1.0.0.jar`:

| Existing candidate | Class / menu route | Outstanding scope |
| --- | --- | --- |
| `R-08f5772a98f948d0c71aa445` | `net.imagej.plugins.commands.display.ChannelsTool`, Image > Color > Channels Tool | Reconcile dispatch, `no-legacy`/headless descriptor and distinct shortcut against the actual pinned Fiji runtime. |
| `R-b3f7157bda31483eda6d97f9` | `net.imagej.plugins.commands.display.SpectralComposite`, Image > Color > Spectral Composite | Additional spectral-composite behavior/options; not settled by the ImageJ1 modes or this source inspection. |
| `R-c6d5a481b299f6b3dd7cfaa0` | `net.imagej.plugins.commands.display.ShowLUTAsTable`, Image > Color > Show LUT As Table | Reconcile data/table/axis behavior with CD21 and expose any distinct eligible workflow. |
| `R-e716904770fc301939053e40` | `net.imagej.plugins.commands.display.ShowLUT`, Image > Color > Show LUT | Reconcile actual menu dispatch and behavior with ImageJ1 LUT viewer. |

These four candidates retain pending runtime reconciliation and commercial
review; their descriptors are not executed observations. The component index
also lists **36** external `.lut` files under `Fiji/luts/` (paths below are
relative to that directory):

| Pinned resource names; each currently pending commercial review |
| --- |
| `16_colors.lut`, `5_ramps.lut`, `6_shades.lut`, `Cyan Hot.lut`, `Green Fire Blue.lut`, `HiLo.lut`, `ICA.lut`, `ICA2.lut`, `ICA3.lut` |
| `Magenta Hot.lut`, `Orange Hot.lut`, `Rainbow RGB.lut`, `Red Hot.lut`, `Thermal.lut`, `Yellow Hot.lut`, `blue_orange_icb.lut`, `brgbcmyw.lut`, `cool.lut` |
| `edges.lut`, `gem.lut`, `glasbey.lut`, `glasbey_inverted.lut`, `glasbey_on_dark.lut`, `glow.lut`, `mpl-inferno.lut`, `mpl-magma.lut`, `mpl-plasma.lut` |
| `mpl-viridis.lut`, `phase.lut`, `physics.lut`, `royal.lut`, `sepia.lut`, `smart.lut`, `thal.lut`, `thallium.lut`, `unionjack.lut` |

Their per-file bytes/hashes and pending code/dependency/model-asset decisions
are preserved in `fiji-components.json`; this review read metadata only and
did not extract, incorporate or newly verify their asset bytes. No `.lut`
members exist in the inspected standalone source archive. `Menus.addLuts`
discovers installed files, strips `.lut`, changes underscores to spaces when
there are no spaces, and conditionally sorts by platform. Thus the eight initial
P06c names do not exhaust even the standalone menu mechanism, much less Fiji.

The user's commercial-use restriction excludes noncommercial/research-only,
unlicensed or incompatible material from incorporation. **Pending review is not
a determination of noncommercial status**, and a copyleft label alone is not
such a determination under the repository policy. Review concrete code,
transitive dependencies, LUT/assets, notices and any replacement independently.
Behavioral examination of the pinned reference does not approve copying source,
macros, tables, icons or models. Eligible discovered workflows remain required;
excluded material must have a recorded reason or an independently implemented
commercially usable replacement where the feature remains in scope.

## Evidence required before registration or acceptance can be complete

Each candidate needs a recorded implementation/evidence state and exact option
membership. The shared matrix below is inherited by every applicable row; a
surface marked inapplicable needs a specific reason (for example, a file dialog
does not belong in the core-only SDK). Cross-family proof may be consumed by
identity, rather than rerun without changed inputs.

| Surface | Required proof |
| --- | --- |
| Pinned reference | Correct 1.54r source/member or distinct Fiji1.54p/component pin; actual bounded captures for unresolved numerics, type/C/Z/T admission, defaults, controls, transitions and source/cancel effects. Independent expected bytes/tables/metadata, with explicit source quirks versus owned policy. Source-only rows remain discovered, not matched. |
| Native C++ core | Public installed APIs for the actual operation/state; literal numeric/state/axis/metadata assertions, strict malformed/overflow/type validation, no data/history writes for display-only actions, explicit pixel transactions for destructive actions, limits/cancel/callback/ownership behavior. New-output mapping includes source preservation and exact output publication. |
| Installed C++ SDK | Fresh core-only consumer with declared headers/link inputs; no GUI/Python dependency; same state/action/result semantics and independent representative mappings, persistence and refusals. A desktop-host check is not this SDK proof. |
| Installed Python | Typed/JSON/NumPy interfaces where relevant, exact dtype/axes and array ownership, strict Boolean/index/lossy-number rules, error/cancel/callback/GIL lifetimes and state/record round trips. Installed artifact/module identity must match the tested source. |
| Shared ImGui controls | Both C++ and Python launches expose every supported option, visible enable/refusal conditions, valid defaults and actual accepted/cancel behavior. Physical pointer/key/text reports plus independent numerical/native-state readers; a direct API call must be labeled API and cannot substitute for the control it bypasses. |
| Configuration/persistence/recording | Demonstrated setting consumer, defaults/reset/import/export/restart scope; native session and required interchange variants decoded independently; exact one-append/no-append, recording-off, strict replay and explicit rebind, stale/foreign/closed/busy refusal. Existing opaque payloads survive transport until a supported interpreter is selected. |
| Interactions/performance | Mixed committed/preview C contributors, XYZ final coordinates, threshold/annotations once, histogram ROI, active-owner switching and cache invalidation; bounded preflight/read/working-set/cancel checks for the actual path. State the measured workload and which allocations are outside a cap. |
| Installed platforms and commercial packaging | Populate supported OS/CPU/compiler/Python/graphics entries, then real keyboard/focus/DPI/dialog/clipboard where applicable and assistive-access tests. Bind source/sdist/wheel/prefix/runtime bytes, licenses/notices and dependency eligibility. Empty platform or review records cannot certify release. |

The current P06c card records passing native/headless-Python/core-only-SDK
development gates and outstanding shared-desktop qualification. This review
does not extend those results to new rows, inherit every old result into a new
binary or establish Windows/macOS/Linux release coverage.

## Bounded readiness decisions after P06c

This checklist supports choosing the next coherent child; it is not another
broad implementation plan. Projection representation plus Max/Min/Invert and
LUT-transition semantics can proceed without full split/merge or a volume
viewer once targeted reference cases are frozen. GUI Auto requires its
histogram/ROI/current-channel consumer and separate live-action versus recorded
Enhance Contrast policy; direct finite-extrema Reset is already a different
operation. LUT viewer/editor/interchange can reuse exact tables and shared
display publication, but variable-map scaling needs a representation decision.
Arrange Channels requires the shared C-axis/position/state remapper before a
safe implementation; its ordering classification is now source-resolved.

Before selecting or declaring a whole-family exit, register these reviewed
candidates, reconcile the distinct Fiji descriptors and dynamic palettes, assign
cross-family consumers, resolve each observed-policy discrepancy and specify
every required API/UI/settings/persistence/platform surface. None of the four
named follow-ons, the initial SUM result, this checklist or registration alone
is complete G033 acceptance.

## Inspected member identities

All members below are inside the verified ImageJ1.54r source archive identified
above. Sizes are bytes; hashes are SHA256. Each local inspected member matched
its archive bytes. This table identifies consumed behavioral sources; it is not
an incorporation manifest or license approval.

| Relative archive member | Bytes | SHA256 |
| --- | ---: | --- |
| `IJ_Props.txt` | 21847 | `e1891ac12a42e8051b2c9ca64974cd6a4f6075dd906d73c86b1f2a7b1db57d19` |
| `ij/Menus.java` | 61585 | `1ff436766d53319840a2aecc62b495bb2481c9b89c81f264f640c095457dfbc4` |
| `ij/CompositeImage.java` | 21489 | `7f97ef93b52967cfadda37fb8c26513de283787a93a918d3e54260e43e432a7f` |
| `ij/plugin/frame/Channels.java` | 11531 | `172f378b8ece0839559f7c291dfcf98c6c9ec619f4055d3303282ba92ffd1ccc` |
| `ij/plugin/ChannelArranger.java` | 10059 | `8aa0472df2b110082823d6fd547880ebc803062ca361e87a94eed45051fa7568` |
| `ij/plugin/frame/ContrastAdjuster.java` | 42982 | `e6cc033f00052659b530cbba144afd6d312721ba01ae39080fcb3700befb0de2` |
| `ij/plugin/CompositeConverter.java` | 3667 | `251b1ef9c8d5c7b04da41c22523deb3db1cca14e8155782666448c9d297d8743` |
| `ij/plugin/RGBStackConverter.java` | 8822 | `d8f6f2b37a328a3070c09c4a0f309d30f4772ee9fb599e48f19d82f7ff5dad68` |
| `ij/plugin/LutLoader.java` | 13837 | `404256a6fa3eca0e2769ae82fbafb4296e123d3de3cc418e3847cd6043238b6a` |
| `ij/plugin/filter/LutViewer.java` | 5448 | `e915489eca94fdb7417249f2a7e38882e09bfd17e9130020ed253d85ff9076b1` |
| `ij/plugin/SimpleCommands.java` | 7476 | `cecd6a8615efaefc0d5a8c900f6f4d195d55203da4f6795b56c94fa3bb170b0f` |
| `ij/plugin/LUT_Editor.java` | 16853 | `d673daa4803f39ae9ee3b8ad1e439f03c3a37bb2a4dc87bb6e1946cc61c4a3f3` |
| `ij/plugin/ChannelSplitter.java` | 5012 | `9ab039ab58d33158bfb5314213faa7879cd92fa0d1f6b942ba8693d54f947115` |
| `ij/plugin/RGBStackMerge.java` | 14032 | `3922a79ed4288727e06622d52e382d4347210becc538c643418d53a82020d3cf` |
| `ij/gui/StackWindow.java` | 11082 | `333063c0a94206e656c1a67d72e7482397336644c911fc659f73539067146b57` |
| `ij/io/FileSaver.java` | 25452 | `0e7165c5d230b4c877b45211703b07670820591e0bf45039ed34100d13eaa8ea` |
| `ij/io/TiffEncoder.java` | 16758 | `efc4852785a35b760e3792c75d8764e17bcfd513751f3b4fce4d48c7df393b98` |
| `ij/io/TiffDecoder.java` | 27894 | `2740feb42614f85a4b80446b132904526adcc49dc99ae6d3eda1165f61bf05e1` |
| `ij/process/ImageProcessor.java` | 95523 | `f98e87d25eb6b1f2c5d8557b73dd33e36fef0494d903d6d250206371dccc279c` |
| `ij/process/LUT.java` | 4198 | `c3e13b697521012d9bf132efd663c5b73350a09a2be07c95a48c4c105d5631ff` |
| `ij/process/ByteProcessor.java` | 40502 | `fc23c95778bd7154eba1ea56d9ea943e7d1d9ea568fe20cf9bf9c6db1db3ad94` |
| `ij/process/ShortProcessor.java` | 40446 | `7484082e91c7095a3c79984c74b4dafa8b6d2912ed3b862e28dd331c8ba3024d` |
| `ij/process/FloatProcessor.java` | 35354 | `0f52a157fe99e9a48ce2c9356fc2c3c435dc74f48e45a575171e60deb694ccf5` |
| `macros/InvertAllLuts.txt` | 2279 | `266c77ef8ebae429260b4aca090e43854953630b637e870cb81a71521ad237cb` |
| `ij/ImagePlus.java` | 108208 | `477e3f5f913b9c42feb68ea35d21836d45fd56920cccbfc5719a06822cbcfab9` |
| `ij/io/FileOpener.java` | 20633 | `983e1ac77a11f94da0d5b705a7f1010b7e99fa50d19673b85ea9de88e6c7aca7` |
| `macros/ShowAllLuts.txt` | 1030 | `8a4a98a8c0c570b63152f2c4863d63ac108be299312ab385b93b17ad78ba3ae2` |
| `ij/plugin/filter/LutApplier.java` | 3315 | `dcc62381c98f8b5b9f6f53c8336d0b525602a465321304f23d22b609872539da` |
