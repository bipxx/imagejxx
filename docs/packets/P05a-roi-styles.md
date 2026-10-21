# P05a: typed ROI stroke, fill and width

Status: **partial record staged; acceptance requires the bound final artifact gate**, an
independent M4 vertical increment. Core 14/14, installed SDK 13/13 and desktop
attempt 006 17/17 pass. Fresh installed focused Python attempt 003 passes
186/186, including all 73 pinned comparisons; installed input smoke and full
4,708-case regression pass without skips.
The same-context active-middle-tab close repair and actual pointer/Ctrl+W
regression pass natively; installed attempt-003 binaries and focused checks pass.
Run `m4-roi-style-properties-windows-2026-10-10` is already staged; do not append
it again or replay `record-style-and-surfaces.py`. Native selection/document,
transaction/history and v2 records are the concrete prerequisites; whole M1/M0
closure is not required to begin owned equivalents. P01b2b has separate validator
evidence integrated into the same full regression and final artifact gate. G015/G017 remain partial.

## Outcome and reference

Edit stroke color, fill color and zero/positive stroke width through the native
API and shared Selection Properties panel. Preserve the resulting style in
active/previous selection, stored ROIs, overlays, v2 sessions and frozen pixel
operation records. Ordinary viewing, orthogonal XY and export/flatten consume
one native style resolver.

G017 retains its entire required work: position/size/angle, stroke/fill/style,
coordinate list and exact numeric specification. Text/font, point markers,
position/group/CZT, Image ROI opacity, coordinate/property lists, full binary
interchange and platform/accessibility options stay assigned subsequent slices.
This packet cannot close G017, G015 or M4.

Reference is ImageJ 1.54r commit
`62db90a269cccc1ac79f658e4602043214ed055b`, archive
`build/references/downloads/imagej-62db90a269cccc1ac79f658e4602043214ed055b.zip`,
SHA256 in `docs/parity/reference-pins.json`. Inspect the pinned
`ij/gui/{RoiProperties,Roi,PolygonRoi,Line}.java` and `ij/plugin/Colors.java`.
The live [Properties documentation](https://imagej.net/ij/developer/source/ij/gui/RoiProperties.java.html)
is contextual; capture the pinned setter/state rules rather than assume its
current source equals the pin. Implement owned C++ equivalents; incorporate no
Java/plugin code, runtime, dependency, model or asset.

## Ownership

Worker owns a new public native ROI-style helper/header and source, its CMake
registration, narrow changes to native document/selection validation and masks,
SelectionEditor and its bindings, shared `selection_panel.{hpp,cpp}`,
`window.cpp` style actions/rendering, `orthogonal.{hpp,cpp}`, `rendering.cpp`,
record adapters as needed, and minimal Python UI model/workflow/rendering
adapters that route new processing to native code. The test worker owns focused
new native/Python style tests, relevant existing selection/desktop tests,
`src/desktop/smoke.cpp`, installed `examples/cpp/selection.cpp` assertions, and
the owned capture driver/corpus. Production and test workers have disjoint
ownership; root runs the capture. Exact new file names and public API go in the
handoff before final integration. No unrelated operation or UI refactor.

Root owns packet/plan/parity metadata, acceptance records, build/install,
installed regression and archive checks. Workers share the checkout and
preserve other changes. P01b2b owns scripts/check_plan.py, parity evidence
helpers/tests and SCHEMA.md; do not edit those files. Serialize shared native
builds/installations and package generation through root.

## Frozen compatibility and behavior

- Keep legacy `Selection.width` finite and strictly positive, its masks and old
  API signatures unchanged. Keep legacy `color` strings byte-identical and
  retain their `#RRGGBBAA` interpretation. Absent new style preserves the old
  display behavior; do not silently migrate old records on load.
- Store opt-in native style in the namespaced selection attribute
  `imagejxx.roi_style`, an object with required integer `schema_version: 1`.
  Canonical colors are RGBA byte arrays
  or null; optional `stroke_width` is finite and nonnegative. Retain unknown
  extension fields inside and outside the style record. Validate recognized
  fields atomically and reject unsupported style versions.
- Public native parsing accepts ImageJ names and unambiguous `#RRGGBB` /
  `#AARRGGBB`, converting to canonical arrays. ImageJ green is full-intensity
  green; do not reuse the legacy half-intensity named-green parser. Null/none,
  omitted values and alpha-zero colors remain distinct. Document both hex
  conventions at their APIs and relevant controls.
- Follow the pinned setter order: setting an area fill alone clears explicit
  stroke, while supplying fill then an explicit stroke yields both. Removing
  fill leaves transparent interior; null stroke without fill uses the default
  outline. The dialog applies its displayed stroke after fill. Do not implement
  unconditional fill suppression of every explicit outline.
- New explicit width zero uses a one device-pixel cosmetic outline independent
  of zoom; positive width scales with the X image magnification. A styled
  line/polyline/angle uses native resolved processing-mask thickness, zero
  resolving to the existing thin one-pixel rule. Preserve raw legacy width;
  area colors/fill never alter its region mask. Point marker size/type stays
  separate. Fill controls apply to area selections, not lines/points.
- Native typed resolution owns validation, color semantics, effective mask
  width and style precedence. Both UI launches and optional Python adapters
  call that implementation. Explicit new style takes precedence over overlay
  foreground fallback without erasing retained legacy attributes.
- Shared dialog drafts remain detached. Apply publishes once only against the
  captured valid document revision; Cancel/Escape, invalid values, busy work and
  stale/closed ownership produce no partial publication. Preserve current
  geometry/style and unrelated view/display state according to existing merge
  rules. Do not add pixel history for a transient active-selection edit.
- Stored ROI/overlay metadata edits remain one-step undoable. Persistence keeps
  active and previous selection, positioned stored records, nested extensions
  and style values through native/Python v2/session/ROI JSON paths.
- Later pixel operations freeze the complete styled selection in their workflow
  record and load/replay it. Standalone selection-style recording remains
  assigned to G085; retain existing properties action behavior.
- Render each ROI with its own resolved stroke/fill/alpha in ordinary and
  orthogonal XY. Fill masks preserve holes/disconnected components. Zero-width
  bitmap outlines use screen-space contours, not magnified filled edge pixels.
  Export/flatten blends alpha with the documented pixel rule. Keep XZ/YZ XY-ROI
  projection unimplemented; do not draw incorrect projections.
- Cache derived visual layers by relevant selection/annotation revisions,
  style and coordinate state; clip drawing to the viewport. Do not regenerate
  identical full masks for every frame. Complete large-ROI managed accounting,
  reference raster/antialias and DPI/platform certification stay explicit gaps.

Canonical stored example (colors/width are optional; the version is required):

```json
{
  "imagejxx.roi_style": {
    "schema_version": 1,
    "stroke_color": [0, 255, 0, 255],
    "fill_color": null,
    "stroke_width": 0.0
  }
}
```

Absent fields, explicit null colors and alpha-zero arrays have different
semantics. Apply changed dialog fields; a changed fill also reapplies the displayed
stroke after fill. An unchanged absent fill must not be normalized to null. The public native helpers are defined in
[`roi_style.hpp`](../../include/imagejxx/roi_style.hpp).

## Verification, progress and resume

Add native/Python color and typed-style tests, setter-order/alpha/null/zero
cases, negative/nonfinite/type/version rejection, mask geometry and unchanged
legacy cases. Capture pinned direct ROI setter/state behavior where practical;
do not label a capture or an approximate display raster as a reference match.

Exercise actual shared-panel mouse/keyboard controls in both launch modes:
detached draft, Apply/Cancel/Escape, no-op, stale/busy rejection, retained view
state, fill-only and simultaneous stroke/fill. Verify individual stored ROI
styles in orthogonal XY and alpha export, masks with holes, native/Python
sessions, metadata undo and frozen operation replay.

Root builds the fresh native standalone and UI-enabled wheel, installs it and
runs affected native/reference/Python/render checks, the full installed Python
regression, installed SDK, standalone render, dependency/catalog integrity and
final source/wheel/installed consistency. Use fresh evidence under
`build/plan-implementation/p05a-roi-styles-001`. Counts alone do not prove this
packet. Preserve every prior accepted run and full family requirements.

## Progress and next action

All reports below are under `build/plan-implementation/p05a-roi-styles-001`.
The partial run is staged and immutable-bound. Acceptance requires a successful
`recorded-artifacts-001.json` / `recorded-artifacts-001.log.state.json`; if either
is absent or unsuccessful, finish the final gate before treating this increment
as accepted. Production and interaction sources are frozen after desktop attempt 006.
Installed focused attempt 003 and full 4,708-case regression pass. Session
`5156` completed with exit 0; `python-full-identity-001.json` binds the parsed
report and verifies all 102 production/test and five validator sources unchanged.
The implementation/test processes completed. Check current process handles
before resuming a still-running final archive gate; a remembered session ID is
not proof of activity. Final acceptance is established by the bound report.

| Gate | Observed result | Reuse or remaining work |
| --- | --- | --- |
| Core-only native | `core-tests-002.log`: **14/14**; `core-identity-002.json` binds 57 source files, library and ROI test executable. | Core files remain unchanged. Verify their identities before reuse. |
| Installed headless SDK | `sdk-tests-001.log`: **13/13**; `sdk-identity-001.json` binds 43 source/artifact/output identities and checks all 14 installed headers. | Core/SDK inputs remain unchanged. All 37 native setting definitions/defaults match the accepted typed baseline; this does not prove consumers. |
| Desktop | `desktop-tests-006.log`: **17/17**; `desktop-identity-006.json` binds the tested executable and 102-source snapshot. | Fresh-context owner/draft and same-context close/surviving-tab regressions pass with 102 unchanged frozen sources. |
| Captured and compared reference | `reference-capture-001.log` succeeds; `python-focused-003.xml` has **73 passing direct pinned comparisons**, plus passing fixture metadata/scope checks. | Reuse only with matching native extension, corpus and test identities. This does not certify the upstream dialog or display antialiasing. |
| Installed focused Python | `python-focused-003.xml`: **186 passes, 0 failures/errors/skips**. All 154 native API cases and 32 shared-panel cases pass. | Retain attempt 001's 178/185 failure evidence. Fresh installed attempt-003 UI gates and full 4,708-case regression pass. |
| Installed standalone input smoke | `installed-desktop-smoke-003.log` succeeds against `desktop-003/bin/imagejxx.exe`. | Verified executable matches native attempt 006; artifact/session hashes are recorded. |
| Distribution/installation | `package-build-003.log` and `package-install-003.log` succeed; `distribution-003` supplies the installed UI-enabled wheel. | Wheel/installed extension bytes match. Refresh the source archive after final metadata/handoff writes. |
| Final integration/acceptance | Full installed regression passes **4,708/4,708**, no skips. Licenses, Ruff F and the 86-operation installed catalog pass. | The partial run and three child records are already staged. Finish or reuse the bound final archive check; never record them twice. |

The corpus is `tests/fixtures/roi-style-imagej154r.json`, SHA256
`e8e845400f8d69c1e9d41e693b1b70a57e71b05b3c19a108d8953edaea3d186a`:
19 color, 36 setter-state and 18 width cases. The reference-only Java/JDK files
remain under excluded `build/`; they add no third-party product runtime or
asset dependency. All 73 observed comparisons concern direct pinned setter and
color behavior, not reference UI or raster parity.

`installed-artifact-identities-003.json` records the wheel SHA256
`4f177bc114a982ee0308b38edd27157900b0e4833e75ad656da7f7f3e70441c9`,
installed `_native` SHA256
`d469c07c3bb282dcedcc764b7d877afb90479a3796eb97914e0229a5327bf980`,
installed `_desktop` SHA256
`482f285260d9db7bb541f3664286f1f21b67cb728f678d7dc2f5fd9423f8b40e`,
and installed executable SHA256
`82a54ad454ad98e57a8354ceb2da541b7f55b68f65915354b4c0bf32ea2470de`.
`python-focused-identity-003.json` binds the current 186 passing cases and 73
reference comparisons to these extensions and snapshot 006. The native
extension remains byte-identical to attempt 001. Preserve every older attempt's
actual evidence; current source-archive consistency is still pending.

The latest focused failure produced two specific corrections:

- Three expectations incorrectly required an explicit null for an unchanged
  absent fill. `test_imgui_roi_style.py` now checks only changed fields and adds
  actual non-null-fill-to-none input/Apply coverage.
- Four repeated view/cache checks exposed an ownership bug with two documents.
  They already rendered four frames. `Window.renderHeadless` creates a fresh
  ImGui context on each call; retained tab visibility state let the first tab
  replace the requested second document. `window.cpp` now resets that state at
  both context entry points and prevents interim default-tab publication.
  Python tests require the styled owner; `test_desktop.cpp` adds an unstyled
  first owner, styled second owner and actual Properties draft/Apply across
  fresh contexts. It verifies the second owner's revisions and preserves the
  first owner. These changes pass native attempt 005 and installed focused attempt 002.

Independent review then found a same-context close case: closing B in [A,B,C]
promotes C at index 1, but unchanged index-based tab tracking can let ImGui choose
another owner. Production owns `window.cpp`'s successful-removal invalidation,
W-key mapping for real Ctrl+W input and observed tab-header coordinates. The test
worker owns `test_desktop.cpp`'s actual A→B, Ctrl+W and surviving C→A→C checks.
These changes pass desktop attempt 006 and installed focused attempt 003. They add no processing or core API.

Native core/CMake/bindings, SDK example, Python tests, corpus/capture driver and
`smoke.cpp` remain frozen. Root owns every build, installation, reference
execution, identity catalogue and acceptance mutation. Source snapshot 005
proved exactly three files changed since 004 and all 57 core sources unchanged.

Next exact actions, in dependency order:

1. Inspect `recorded-artifacts-001.json` and its `recorded-artifacts-001.log.state.json`.
   When it passed and current source/installed identities match, retain this
   recorded partial increment and P01b2b, then start P01b2c1. If absent, finish
   the source-only archive/check below. If failed or invalidated, preserve the
   staged record/binding and reports; root stages a replacement run with a new
   ID and fresh bound report/log paths, following the execution guide's
   replacement-run rule. A differently named success cannot accept this run.
   Freeze the replacement metadata before its archive check.
2. Reuse `final-staging-identities-001.json`: 102 production/test files,
   57 core files, 43 SDK identities, five validator files, tested installed
   extensions/executable, original 22 runs/39 children/two bindings and all
   99 full family contracts are preserved. `metadata-focused-001.xml` passes
   409 cases; `plan-integrity-001.log` reports the current 24/42/3 graph and
   0/168 qualified contexts. Strict reference/family/surface checks reject
   the real remaining gaps. Recheck identities before reusing these gates.
3. All packaged handoff/run/binding writes must precede the last archive.
   For an absent final gate, build only the source archive into
   `recorded-dist-001`, retaining `distribution-003`'s tested wheel, then run
   `scripts/check_distribution.py --wheel <distribution-003 wheel> --sdist
   <recorded-dist-001 archive> --installed --output <recorded-artifacts-001.json>`.
   The [execution runbook](../SOL_ULTRA.md#4-record-once-and-refresh-archives-after-metadata-changes)
   supplies exact source-build/check commands; restore this packet's paths.
   Root owns this gate. No recording helper remains to be run.

The staged increment adds three **partial** G017 children for stroke color,
fill color and stroke width, plus `F.roi-style-imagej154r`. They share the 73-case
corpus; this is not 73 independent matches per child. The current graph has
24 runs, 42 partial children, three immutable bindings and three fixtures.
No qualified runtime surface producer is claimed: **0/168 contexts** remain.
Pending upstream commercial provenance is explicit, and no reference Java/JDK
code or runtime is incorporated. The three new typed contracts preserve
inheritance/null/alpha distinctions without inventing scalar defaults.

Retain the failed core attempt 001, desktop attempts 001–003 and Python focused
attempt 001. Their repaired causes were respectively invalid fixture setup,
uninitialized test draw lists, a Ctrl+A shortcut leaking through Properties,
cross-context cache comparisons, and the latest field-presence/tab issues.
No tolerance or cache algorithm was loosened. Same-context cache observations
use bounded actual-frame traces and exact device clipping keys.

Limits remain explicit: approximate display raster/antialiasing, device extent
over 32,768 or unrepresentable absolute integer coordinates rejected,
incomplete large-ROI resource accounting, unverified DPI/platform behavior,
and the remaining G015/G017 properties and M4 requirements. Ordinary and
orthogonal XY consume ordered per-record overlay layers; full XZ/YZ ROI
projection stays assigned. This packet cannot close a family or milestone.
