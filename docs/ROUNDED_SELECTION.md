# Rounded rectangle selections

**Saved-area modifier checkpoint, 2026-10-21 (work in progress):** ordinary XY
now has explicit actual-input checks for Ctrl-centered corner resize,
Shift aspect-preserving corner resize, Shift rotation snapping (22 degrees to
15 degrees), and Alt movement beyond image bounds. Preview remains unpublished;
Apply creates one history transaction and version-2 workflow record. Undo/Redo
retain samples, selections and row metadata, including literal IEEE payloads.
The four finite/IEEE, axis/rotated cases pass through both hosts, source and
installed, on Windows and Linux: 32 observations, 128 modifier gestures and
13,088 frames. All four profiles pass the twelve focused cases with no skips.
CI now produces 87 C++ cases, including this matrix. See
[the receipt](parity/reviews/p09f-saved-modifiers001.json). This observer-only
increment retains the existing geometry implementation; all resize handles,
combined modifiers, scientific contexts and workflow/rebind gates remain open.
All99 stays 42 in progress / 57 open / 0 complete; all 96 formats remain partial.

**No-movement pointer checkpoint, 2026-10-21 (work in progress):** the existing
fresh-budget release path now has explicit shared-ImGui input coverage. Adjacent
mouse-down/up frames prove no motion was delivered. A one-MiB in-flight API
budget drop refuses visibly and releases all draft memory; recovery retains an
editable pointer/keyboard draft without history or workflow publication. The
numeric panel stays closed by design. All four finite/IEEE, axis/rotated cases
pass through C++ and Python hosts, source and installed, on Windows and Linux:
32 independently checked observations over 8,480 frames. Each profile also passes
the eight focused Numeric/pointer budget tests without skips. This observer-only
increment does not rerun or extend the earlier full-regression claim. See
[the receipt](parity/reviews/p09e-idle-pointer001.json). Resize/rotate and Alt/Ctrl,
workflow/rebind/scientific-context and other feature gates remain open.
All99 remains 42 in progress / 57 open / 0 complete; all 96 formats remain partial.

**Saved-area pointer-budget checkpoint, 2026-10-20 (work in progress):**
new pointer gestures and no-movement draft recreation now read current resource
settings. A saved-area panel opened under a higher budget previously admitted a
new 5.44-MiB pointer draft after Preferences lowered the budget to 1 MiB. Actual
mouse-down now refuses visibly and releases the draft; release-time publication
also refuses an explicitly injected mid-drag API budget drop. Physical Preferences
recovery, Escape cancellation, Shift-constrained movement and Undo/Redo pass for
finite and IEEE Float32 sources with axis-aligned and rotated rounded areas.
Windows/Linux source/installed pass identical sets of 5,041 cases, no skips,
preserving the preceding 5,037; 17 native groups pass per platform. Both launch
modes provide 32 pointer observations over 6,848 shared-ImGui input frames.
Literal samples, global selections, unrelated metadata and history remain intact;
installed SDK consumers and 66-header/12-module product audits pass. Verified
archives reclaim 1,664,699,863 bytes; qualified installs and caches remain.
See [the qualification receipt](parity/reviews/p08z-pointer-budget001.json).
The initial GitHub workflow exposed compiler-selection and missing optional
IO/UI build configuration failures; hosted/macOS CI is not accepted by these
local checks. Repair CI next, then finish remaining pointer modifier/no-movement,
workflow/rebind/scientific-context and rounded promotion gates. All99 remains
42 in progress / 57 open / 0 complete; all 96 format variants remain partial.

**Saved-area live-budget UI checkpoint, 2026-10-20 (work in progress):**
saved-area Numeric Apply now reports publication failures in the root error
modal after releasing its draft. Previously, lowering the memory budget while
an edit was open correctly refused publication but closed the local error panel,
leaving no visible explanation. Actual Preferences controls now qualify entry
refusal before navigation, live lowering, Apply refusal and recovery on finite
and IEEE Float32 sources with axis-aligned and rotated rounded selections.
Windows/Linux source/installed each pass the same 5,037 cases, no skips,
preserving all preceding 5,033 IDs; 17 native groups pass per platform.
Both launch modes supply 32 observations over 5,344 shared-ImGui input frames.
Checks retain literal Float32 words, unrelated selections/attributes, history
and version-2 workflow records through refusal, Apply and Undo/Redo. Installed
SDK consumers and 66-header/12-module product audits pass. Verified scratch
archives reclaim 1,557,003,829 bytes; qualified installs and caches remain.
See [the qualification receipt](parity/reviews/p08y-saved-area-budget001.json).
Next: pointer release under live budget changes, modifier/cancel and
workflow/rebind/scientific-context UI gates. All99 remains 42 in progress /
57 open / 0 complete; all 96 format variants remain partial.

The C++ core, Python API and shared Dear ImGui editor support editable rounded
boxes, creation gestures, saved defaults, corner properties and stored-area
editing. Standard ImageJ ROI projection, native-envelope export/replay and explicit
promotion in import review are supported. Remaining processing/resource and
workflow parity gates are still open. This increment does not close the
broader ImageJ/Fiji feature objective or a complete All99 family.

## Geometry and public API

`rounded_rectangle` stores two corners; `rotated_rounded_rectangle` stores the
center and two axis endpoints. Both require an exact integer `corner_diameter`
attribute in 0..INT_MAX. Ordinary rectangles retain their old geometry even if
they carry an opaque attribute with that name.

```python
import imagejxx as ij

selection = ij.Selection(
    "rounded_rectangle", [(6, 5), (24, 19)],
    attributes={"corner_diameter": 8},
)
rotated = ij.SelectionEditor(selection, (33, 41)).rotated(31)
```

Move, resize, rotation and box properties retain the diameter and unrelated
attributes. Corner-aware hit testing excludes the cut-off corners. The model
exposes `is_rounded_box`, `corner_diameter` and `rounded_path(processing=False)`.
`SelectionFrame(center, u, v)` constructs a validated frame in Python.
`rounded_rectangle_path(frame, diameter)` returns the fixed 44-value ShapeRoi
contour, with finite supported coordinates and clamped corner radii. Rotation
and reflection retain continuous geometry; the native path uses fixed storage.

Unrotated processing truncates the native origin and ceils each extent without
wire binary32 quantization. Rotated processing uses the actual transformed
contour. The legacy ImageJ rounded-mask helper retains its separate float-wire
quantization and diameter range 1..60535. The pinned ImageJ short decoder treats
the following unsigned-short range as negative, so that transport limit is
intentional. The projection API below chooses a carrier without changing native
processing membership.

## ImageJ interchange

`imagej_roi_standard_projection(record, width, height, limits)` constructs a
standard ImageJ carrier for an editable rounded selection on a bounded canvas.
An axis-aligned box uses the rounded primitive only when binary32 wire bounds
preserve its integer processing bounds and its diameter fits 1..60535. Rotated,
zero-diameter, oversized-diameter and precision-sensitive boxes use the native
processing contour as a ShapeRoi path. Both retain exact processing membership.
The standard carrier consumes the geometry diameter attribute; ordinary byte
codec metadata/style validation remains strict. Conflicting retained geometry
must be cleared explicitly. The byte codec and legacy mapping defaults remain
unchanged.

The native envelope retains the exact editable geometry, diameter, opaque
attributes, plane, position and row metadata alongside the standard carrier.
Both desktop launch modes export rounded rows through their existing reviewed,
atomic file workflow. Captured export replay retains the validated carrier;
explicit rebind uses new intent. Export workers do not read image samples.

`editable_imagej_roi_mapping(mapping, limits)` explicitly promotes an unchanged
retained rounded primitive to an editable two-corner box, preserving source
provenance, current position and style. It refuses absent/stale descriptors or
geometry whose native processing mask would differ. This API does not promote
arbitrary ShapeRoi paths. Classic bare ImageJ ROI import still produces its
legacy mask by default. For a bare rounded primitive, import review offers
**Edit as rounded rectangle...** after explicit plane/position mapping. This
admits the original first and opens a private native replacement editor. Confirm
the destination and keep the replacement before Validate and review / Apply.
Fractional or out-of-range corner diameter input refuses staging. Editing box
properties preserves the rounded kind, including rotation, diameter and style.
Discard leaves the draft unchanged; Reset bare native edit restores standard
mask import. The original bytes and source mapping remain independently retained
in source archive version 4, and the existing mixed-import record captures the
explicit native row. Captured review, Undo/Redo and cancellation use the ordinary
guards. No mapping or record schema changed. App native-envelope imports also
restore the editable row.

## Creation, configuration and properties

The Rounded rectangle tool shares the native box-drag arithmetic with Rectangle.
It supports Replace, Add, Subtract, Intersect and Xor, including rounded and
rotated rounded bases. The default diameter is captured when a gesture starts.
Replace and Add without a base retain editable geometry; Boolean combinations
retain explicit packed masks. Existing modifier and guard machinery is reused;
complete rounded-specific modifier/scientific-context parity remains to qualify.

The Drawing setting `rounded_corner_diameter` defaults to 8 and is an integer in
0..INT_MAX. It uses the shared native settings registry and preferences storage;
toolbar changes follow the editor's ordinary preferences-save lifecycle. Native
API updates and restart persistence are covered. No new runtime dependency was
introduced, and both desktop launch modes use the same C++ ImGui controls.

Selection properties expose a strict integer Corner diameter field. Invalid or
out-of-range text disables Apply and leaves the source unchanged; a corrected
value restores the preview. Normal selection-only edits retain transient history
and capture Previous. Saved-area corner edits create a guarded metadata history
entry, and the actual Undo/Redo controls restore the expected stored geometry.
Stored-area editing still preserves width, style, unrelated attributes, row
metadata, active/previous selections and source pixels. Only the corner geometry
attribute may differ when both source and replacement are rounded kinds.

Unstyled rounded outlines use the native cubic path in ImGui; existing cached
style/fill rendering and raster outlines recognize both rounded kinds.

## Records and resource bounds

Creation records remain version 1 for existing kinds. Rounded base/candidate/
result geometry requires version 2; version 1 rejects rounded kinds, and version
2 without rounded geometry is rejected. Replay validates the original equation
before source binding; rebind validates that original before applying explicit
new intent. Stored ROI geometry records likewise use strict version 2 when the
original or replacement has a rounded kind, retaining version 1 for old kinds.
The native session geometry carrier remains unchanged.

Rasterization reuses bounded ShapeRoi processing: default caps are 16 Mi pixels
and 64 Mi path-work units. Histogram admission and creation composition/record/
replay estimates charge a conservative additional 512 KiB for curve scratch plus
the temporary packed mask. These admissions precede their command callbacks or
plane acquisition. Scheduled core filters now additionally charge one 512 KiB
curve workspace plus one packed-mask temporary whenever the frozen target can
match rounded geometry. Active selections, stepped ranges and all eight T/C/Z
wildcard patterns are admitted from metadata; range products are not expanded,
and repeated ROIs/planes share the serial raster peak. Whole-plane intent and
unmatched positioned rounded ROIs add no curve charge. Tight budgets refuse
before scheduler admission or source reads. The full frozen target's rounded
raster pixel limit is checked before direct execution starts reading planes.
This is conservative admission, including when non-area ImageJ intent could
dominate a matching rounded ROI. Estimates exclude caller captures, metadata
admission allocations and opaque reader caches; they are not process-memory
guarantees. Legacy generic operation Apply and Preview now supplement their
existing estimate with the same rounded-target workspace. The supplement resolves
the actual legacy operation, slice and all-Z choice; serial all-Z masks share
one scratch peak. Whole-dataset conversion and unused/ordinary geometry add no
rounded charge. Generic unrelated jobs retain their own existing estimates.
Saved-area geometry core/private-editor resource bounds are now qualified below.
Their estimates remain metadata-only for editable rounded geometry: changing a
12x10 canvas to 4096x4096 adds no raster charge. Actual saved-area UI limit changes
and resource-pressure recovery remain to qualify.

## IEEE Float32 saved-area editing (P08x)

Saved-area metadata editing now supports admitted scalar PreserveIEEEFloat32
sources. Records serialize and decode `preserve_ieee_float32`, alongside the
unchanged finite-only and quiet-NaN policy names. Geometry records retain version
1 for existing area kinds and version 2 for rounded kinds. Strict replay checks
the exact policy; explicit rebinding validates the old record before capturing
the destination policy. Unsupported type/component combinations and unknown
policy names refuse before callbacks or destination/request casting.

The existing Dataset admission rules still require reviewed eager storage for
nonfinite Float32 policies, with scalar grayscale for IEEE. Image metadata,
coordinates and calibration retain their finite-value requirements. This change
extends metadata editing, rather than IEEE numerical-filter qualification.

Windows/Linux source and independently installed packages pass matching sets of
5,033 Python cases each with no skips, preserving all preceding 4,853 IDs. The
180 new cases cover nine area kinds (boxes, ellipses, rotated forms, rounded
forms, polygon, freehand and mask), three source policies, and YX/ZYX/TCZYX.
Literal UInt32 patterns retain quiet-NaN payloads/signs, infinities, signed zero,
subnormals and finite extremes through metadata publication, Undo/Redo and session
round trips. Metadata admission/edit/history operations add no provider reads;
intentional pixel retrieval and session encoding are checked separately.

Seventeen native groups pass per platform. New matrices provide 27 public cases /
369 checks and nine private numeric/pointer cases / 54 checks with no provider
reads. Independent installed core-only C++ SDK consumers repeat the compatibility
matrix and retain prior resource/projection checks. Products match all 66 public
headers and 12 Python modules; app/extensions are coherent. No dependency or
geometry record-version change was added. Older readers reject the new unknown
policy name instead of silently downgrading it.

Evidence: `build/p08x-ieee-saved001/qualification001.json`, matching JUnit
identities, native/SDK receipts, product audits and `ieee-saved-audit001.json`.
Owned scratch is verified and archived before removal; reusable caches and
qualified installs remain. Live saved-area resource pressure and setting changes
still need actual UI input qualification for finite and IEEE sources.

## Saved-area API resource qualification (P08w)

Windows/Linux source and independently installed packages pass the same 4,853
Python cases each with no skips, preserving all preceding 4,721 IDs. The 132 new
cases cover rounded saved-area resolve, estimate, prepare, publish, replay/rebind,
Undo/Redo, all five resource refusals, exact byte boundaries, callback capture,
pre/mid cancellation and malformed-original-first validation. A real compiled
PlaneSource fixture counts and rejects plane/strip/window acquisitions; counts
stay zero across UInt8/UInt16/Float32/Float64 and S1/S3/S4 layouts, axis/rotated
geometry and diameters 0, 6 and INT_MAX.

Fifteen native groups pass per platform. The new public matrix has 72 cases /
2,682 checks, and the private saved-editor matrix has 30 cases / 318 checks. They
cover numeric staging, analytic corner hit testing, pointer move/resize/rotation,
exact preparation limits, candidate invalidation, retained-resource release,
pointer camera staleness, numeric camera refresh and reentrant cancellation.
Independent installed headless C++ SDK consumers rerun the 72 public cases with
zero provider reads, alongside the earlier processing/projection checks. Public
packages match 66 headers and 12 Python modules. Existing metadata-only estimates
pass these gates; no production geometry/schema/dependency change was required.

Evidence: `build/p08w-rounded-saved-resources001/qualification001.json`, matching
JUnit identities, native/SDK receipts, product audits and
`saved-resource-audit001.json`. Owned scratch is verified and archived before
removal; reusable caches and qualified installs remain. These are core/private
editor API checks. Live saved-area UI budget lowering, entry/Apply refusal and
recovery still need actual input qualification, followed by remaining rounded
workflow/modifier/scientific-position and import-promotion resource/context gates.
At that checkpoint the audit found a PreserveIEEEFloat32 policy/record gap;
the P08x implementation above closes its metadata-editing compatibility gap.

## Legacy desktop admission qualification (P08v)

Windows/Linux source and independently installed packages pass the same 4,721
Python cases each, with no skips and all preceding 4,719 IDs preserved. Thirteen
native groups pass per platform. New native metadata-only tests cover Add/all-Z,
conversion, ordinary rectangles carrying an opaque corner key, absent selection,
checked overflow and no source reads. Independent installed C++ SDK consumers
retain the processing resource and projection checks. Products match all 66
public headers and 12 Python modules, with coherent app/extensions and no new
runtime dependencies or public schemas.

The actual shared ImGui controls supply 16 observations across 3,600 frames:
axis-aligned/rotated selections, two launch modes, platforms and source/installed
profiles. A 64-square UInt16 Add fixture has a 557,056-byte original estimate;
rounded scratch raises its reservation to 1,081,856 bytes. Both Preview and Apply
refuse a 1 MiB budget before a job is created. Input acknowledges the standard
error modal, raises the budget to 2 MiB through Preferences, then successfully
previews and applies exact masked pixels. Preview does not publish. Undo/Redo
retain selection and metadata. A declared scheduling gate holds the worker for
a queued-job observation; the real Jobs and log Cancel control cancels that job
without acquiring its reservation or changing pixels/history/workflow.

Evidence: `build/p08v-rounded-legacy001/qualification001.json`, exact JUnit
identities, native/SDK receipts, raw-input observation manifests and
`legacy-audit001.json`. Owned scratch and the obsolete unqualified Linux install
are verified and archived before removal; reusable caches and all qualified
installs remain. This qualifies the generic operation Apply/Preview admission
paths. Remaining saved-area and workflow/scientific-context gates remain open.

## Processing qualification (P08u)

Windows/Linux source and independently installed packages pass matching sets of
4,719 Python cases each with no skips, preserving all preceding 2,542 IDs. The
338 new rounded cases cover six geometry cases, active/positioned policies,
UInt8/UInt16/Float32, Add, ImageJ Gaussian, five rank methods, Remove Outliers and
Top Hat. Results match equivalent native masks; untargeted positions and samples
outside membership stay unchanged. Further cases cover read-free budget refusal,
whole-plane intent and Remove NaNs with exact IEEE Float32 payload preservation.
Existing pinned filter numerical suites remain in the regression set.

Thirteen native groups pass per platform. Native lazy-source tests prove budget
refusal without job/source publication, one-workspace charging, all wildcard
patterns, duplicate ROIs, stepped ranges and refusal of an oversized rounded
mask on a later target before any earlier plane is read. Independent installed
headless C++ SDK consumers on both platforms prove budget refusal, an acquired
scratch reservation and exact masked publication/history, and retain the earlier
48-carrier/six-promotion/48-envelope checks. Products match 66 public headers,
12 Python modules and coherent app/extensions. No public schema/API changed or
runtime dependency was added.

Evidence: `build/p08u-rounded-processing001/qualification001.json`, the matching
JUnit identity receipt, native/SDK receipts, product audits and
`processing-audit001.json`. Owned scratch is verified and archived before removal;
reusable caches and qualified installs remain. The audit identifies the separate
legacy generic desktop scheduler as the next resource gap. Earlier physical UI
qualification remains distinct from these core processing checks.

## Import review qualification (P08t)

Windows/Linux source and independently installed packages pass the same 2,542
Python cases each, with no skips, preserving all preceding 2,470 case identities.
Ten native groups and independent installed C++ SDK consumers pass per platform.
Installed products match all 66 public headers and 12 Python source modules.

Actual import controls supply 40 observations across 8,740 frames: axis-aligned,
rotated, zero-diameter and INT_MAX-diameter rounded replacements plus the original
legacy replacement case, in both launch modes, platforms and source/installed
profiles. Checks cover missing mapping refusal, unchanged default mask behavior,
private promotion, unconfirmed destination refusal, invalid/recovered diameter,
style and exact native positions, original byte retention, preview, atomic apply,
Undo/Redo, captured review/discard, explicit reset and cancellation. The observer
scrolls controls into view before delivering input. No new dependency was added.

Evidence: `build/p08t-rounded-import001/qualification001.json`, matching JUnit
identities, native/SDK receipts, product audits and raw-input observation manifests.
Owned scratch is verified and archived before removal; qualified installs and
reusable source/build caches remain. This closes the classic primitive promotion
review gap, not arbitrary path inference or the remaining rounded processing and
workflow parity gates.

## Interchange qualification (P08s)

Windows/Linux source and independently installed packages pass the same 2,470
Python cases each, with no skips. Independent headless C++ SDK consumers on both
platforms check 48 carriers, six primitive promotions, 48 native-envelope round
trips and limits. Public packages match all 66 headers and 12 Python modules;
the app and three extension modules are coherent on each platform.

The actual export controls provide 48 observations across 20,422 frames: legacy
point plus five rounded cases, two launch modes, two platforms and source/installed
packages. Cases exercise review, metadata choices, browse cancellation, initial
publication, overwrite refusal/confirmation, captured replay, rebind, cancellation,
race/stale guards and temporary-file cleanup. Linux publication tests run on its
native filesystem; the existing refusal of unsafe WSL 9P no-replace publication
remains intact. A pinned ImageJ RoiDecoder independently accepts all 118 carriers
and matches processing masks, including the 46 earlier reference cases. Java is
reference tooling only; no product dependency was added.

Evidence: `build/p08s-rounded-interchange001/qualification001.json`, exact test
identity receipt, SDK receipts, decoder oracle and observation manifests. Owned
scratch is verified and archived before removal; reusable caches and qualified
installs remain. No complete All99 family is claimed.

## Earlier editor qualification (P08r)

Windows and Linux source and independently installed packages pass identical
sets of 2,053 Python cases each, with no skips. All 1,997 preceding source IDs
remain. Ten native groups pass per platform. Independent headless C++ SDK
consumers cover six saved-corner records, five creation records, legacy version
refusals and atomic settings bounds without GUI, Python or TIFF dependencies.

The actual ImGui cases cover creation/Boolean modes, invalid property input,
valid preview/apply, axis-aligned and rotated saved-corner editing, and physical
menu Undo/Redo. Both launch modes, platforms and source/installed profiles supply
24 observations over 1,752 frames. Independent readers check delivered input,
geometry, record versions, unchanged source pixels and metadata, and data history.
Saved-area input uses a continuous native renderer session; resetting the renderer
intentionally cancels that guarded edit. These observations do not certify every
ImageJ/Fiji UI path or complete visual parity.

The core contour also retains 46 independently captured Java RoundRectangle2D
paths and pinned ImageJ ShapeRoi masks, including oversized diameters, rotation,
reflection and off-canvas cases. Reference capture reverified 469 pinned class
inputs. Java is reference tooling only. The historical histogram parity source-
fingerprint failures are retained and are not reclassified by these checks.

Evidence: `build/p08r-rounded-selection001/qualification-ui001.json`, the two
UI observation archives, native/JUnit receipts and product audits. Installed
packages match all 65 public headers and 12 Python source modules. Both native
apps and all three extension modules were rebuilt coherently. Owned temporary
build/test/shader assets were CRC/SHA-256 verified, archived and removed; reusable
source/build caches and qualified SDK/Python/application installs remain.

## Next gaps

1. Repair hosted CI compiler/build configuration and run the complete declared
   core/scientific/desktop jobs; preserve the initial failing logs. Then extend
   P08z's mouse-down/release budget, Escape and Shift-move coverage to remaining
   resize/rotation/Alt/Ctrl and no-movement re-admission UI cases. P08y retains
   Numeric refusal/recovery coverage in both launch modes.
2. Exercise rounded-specific workflow replay/rebind controls, modifier/cancel
   paths and scientific position/plane guards in both launch modes.
3. Extend primitive-promotion resource/stale-context UI coverage where not already
   proved by the shared import guards and explicit API gates.
4. Refresh related public header documentation and the broader parity inventory
   before claiming a rounded-selection family complete.
