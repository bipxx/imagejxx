# CFG5: Drawing settings through their existing consumers

Frozen preparation contract, 2026-10-26. Consumer batch C3 covers seven existing
settings. Source review found no concrete product defect; this packet closes
missing Preferences-to-consumer proof. It does not replace native algorithms,
qualify the full drawing families or alter the current hosted3868396 inputs.
The execution card remains the only queue. No build/capture pass is claimed.

## Scope and ownership

Use `HeadlessInputSession`, the existing paired capture/source/product admission
and public standalone/Python launchers. Four identities per platform:
`drawing-persist` and `drawing-reopen`, each in `imgui_cpp` and `imgui_python`.
Each reopen is a distinct process reading its corresponding persist file.
No settings API is permitted as the tested input. Fixture construction is labelled.

- A owns only `main.cpp`, `python.cpp` and `CMakeLists.txt`: narrow fixed CLI,
  existing Editor binding and source registration, following CFG2. No new ABI.
- B owns `settings_drawing_ui_probe.hpp/.cpp` and `settings_panel.cpp`: fixed
  input routes and read-only Integer observations by SettingKind. Existing
  Number/Color observations suffice. No Window state/helper or renderer rewrite.
- Root owns independent expectations/readers, paired capture registration,
  source controls and the sole build/install/run/publication lane. Authors do
  not build or run tests. Preserve other work and the four uncommitted CG0 files.

Function: `settingsDrawingUiCase(Window&, case_id, context, fresh_directory)`.
CLI: `--settings-drawing-ui-case CASE FRESH_DIRECTORY PREFERENCES_PATH`.
Python: the existing native Editor object exposes `_settings_drawing_ui_case`.
Report schema: `c3.settings-drawing.ui.001`, acceptance_claim=false.

## Fixed fixture and independent expectations

One ordinary UInt8 RGB owner, width41/height33. Pixel(x,y) is literally
`[10+x,20+y,30+x+y]`; no grayscale conversion or floating rounding is involved.
Opaque foreground `#e01f40` means `[224,31,64]`; background `#1020e0` means
`[16,32,224]`. All unaffected words, owners/carriers, metadata, calibration and
unrelated state must stay exact. Compare complete source arrays, not samples.

Final persisted map:

| Key | Applied value | Consumer and independent predicate |
| --- | --- | --- |
| foreground | #e01f40 | Actual Fill and Brush write literal RGB foreground inside exact masks |
| background | #1020e0 | Actual Clear and Eraser write literal RGB background inside exact masks |
| brush_width | 3 | Integer-centered dab(4,4): exactly x,y=3..5; width1 control paints only(4,4) |
| rounded_corner_diameter | 5 | Actual rounded drag captures diameter5; actual Geometry/Properties input sets center(14.25,12.75), width10.5, height7.5 and Angle(degrees)23 without changing diameter. Compare rotated curve and all1353 mask bits to immutable independent R011. Axis-aligned processing integer-expands fractional bounds; the real rotated kind retains the independent reference domain. No new reference pins or generated observations |
| line_width | 3 | New unstyled line(2,4)→(6,4): filled mask x=1..7,y=3..5; width1 control paints x=2..6,y=4 |
| point_size | 5 | New unstyled point(4,4): rows y=2,6 have x=3..5; rows y=3..5 have x=2..6. Size1 control paints only(4,4) |
| text_size | 23 | Actual text tool/dialog takes default23 and foreground; entering C3 and Apply commits an overlay with exact size/color and unchanged source pixels; Undo/Redo removes/restores it |

Native raster sample centers are integers. Do not introduce a half-pixel input
offset. Rounded radii clamp independently by width and height. Explicit ROI
stroke styling overrides line processing width; create new unstyled selections.
Point processing has its own width semantics. Use actual Fill on line/point
masks as a width effect; no framebuffer extent is inferred from ROI metadata.

## Required input and state sequence

1. Capture fresh runtime, source/product/launcher identities and initial state.
   Open Edit → Preferences → Drawing using observed controls. Record all61
   initial settings and opaque preference compartments for preservation checks.
2. Persist route: actual field text/Enter sets both colors, diameter5, text23,
   and width1 for brush/line/point. Apply/Close. Exercise thin controls, then
   actual Preferences sets final3/3/5 widths and Apply/Close. Consume all seven
   final values. Reopen route loads that final file before any tested input,
   verifies the actual Drawing controls, and consumes all seven without API
   restoration or rewriting the preferences file.
3. Use rectangle bounds(2,2)→(5,5): Fill/Clear expect x,y=2..4 only. Exercise
   Brush/Eraser dabs, rounded creation/actual geometry dialog with Angle23/Fill, unstyled
   line/Fill and point/Fill. Retain rounded_constructed (diameter5 before geometry)
   and rounded_geometry_shown; geometry edits cannot substitute a new diameter.
   Each pixel commit gets distinct pre/after/Undo/Redo/Undo observations; restore
   the exact source between independent operations using real Undo, not an API.
4. Exercise the actual Text dialog and overlay commit/Undo/Redo/Undo. Selection
   changes may change active/Previous selections as declared, but never source
   pixels/history. Preferences edits do not mutate image/history. For every
   real edit, assert one history transaction, actual Undo/Redo behavior, exact
   changed words and unchanged outside words. No direct command substitutes for
   required menu/tool/dialog input.
5. Persist/reopen must bind identical file path/bytes/hash; selected values and
   other54 known values plus unknown compartments survive. Ordinary consumer
   operations cannot rewrite preferences. Keep error/pending-job state explicit.

Use actual item ID, owner window, clip and settled visible geometry before each
click; retain raw input events and meaningful named full snapshots. Scalar
frames must not duplicate full owner/source trees. Preserve750 frames/60seconds
and the existing16MiB protocol ceiling; target at most4MiB/report and admit
simultaneous raw/transport/reader/compression storage before any capture. An
exceeded storage estimate is a storage failure, not a functional pass.

## Verification and remaining boundaries

Root first tests producer/transport/reader contracts and independent literal
oracles, then compiles in the one existing cache only after storage admission.
Run one semantic route before the final four-ID cohort on each required host.
Preserve original failures and exact source/binary identities. Do not replace an
open user app or mutate a binary while its process is live. No extra cache,
runtime, whole-source checkout, native dependency or repeated build generation.

Reuse current native ROI-style/rounded references and API color/text tests at
their original scope; they are supporting evidence, not C3 input acceptance.
Headless input proves shared ImGui dispatch, native effects/history and fresh
configuration consumption. OS reachability, physical input hardware, GL raster
and exact text glyph output require their separate owning evidence. Text
metadata/default proof is not pinned-font rendering proof. All99 family states,
commercial eligibility and outstanding platform/scientific/accessibility/release
requirements remain unchanged.

## Frozen report seam for independent reader work

Top level: schema, acceptance_claim=false, state observed/failed, error,
case_id/context, runtime_before/runtime_after, elapsed_seconds, fixture,
setup_events/events/frames/states/bounds. Runtime uses actual executable,
producer/native artifacts and PID/parent identities. Scalar frame report keeps
input_frame/session, projected Drawing preferences fields/widget controls,
host_controls/dialog/ordinary_canvas/actual_hover/errors, without full draft or
shortcut trees. Named states contain frame/active_id/settings(all61), preferences,
preferences_file(path/exists/byte_count/utf8/SHA), one full owner, workflow,
results/recording/usage/errors and relevant observed controls.

Owner pixels expose shape[33,41,3], U8 hex and distinct actual backing
storage_f64le_hex; no sampled or rounded values replace original words.
selection_observation provides mask_shape[33,41], mask_u8_hex and actual
rounded_path or null. Existing lossless transport handles repeated payloads.

Both routes have initial/preferences_shown/final. Persist additionally has
thin_draft/thin_applied/thin_closed and final_draft/final_applied/final_closed;
reopen has reopened_closed. Each of fill/clear/brush/eraser/rounded/line/point/text
has _before/_after/_undo/_redo/_restored states. Persist also has thin_brush,
thin_line, thin_point with those suffixes. Text has text_opened; rounded has
the two construction/dialog states above. These are observations, never
producer-computed expectations or acceptance verdicts.
