# CFG6: persisted playback preferences and linked consumers (C5 batch)

Status: additive loaded-file clock qualification is local Windows only. The
complete Preferences→playback→fresh reopen chain below remains open. This packet
does not complete ImageJ/Fiji stack viewing, animation export or any family.

## Existing behavior and smallest path

`animation_fps` already exists in Settings, Playback-group Preferences,
PreferencesState, Window::applyPreferences and advanceAnimation. Its default is8,
finite accepted domain0.1..120. Reuse that path; add no setting, timer, playback
sequence implementation or Python-side processor. Existing Z/C/T, range/stride,
reverse/ping-pong and calibrated-time consumers remain authoritative.

The historical animation observer overwrites every host's FPS with60. Its
`fps`/`cal-fps` cases then call updateSettings(15), so they cannot prove saved
Preferences consumption. Preserve those cases and expectations. The additive
`fps-loaded`/`cal-loaded` cases retain the constructed host's loaded FPS, use
actual Animation controls, and forbid setting API substitution. They reuse the
existing15-second/180-frame headless session, fixture and lifetime cleanup.

Independent `tests/python/animation_preferences_reader.py` owns the literal
Float32 words, position traces, timing policy and preservation checks. At loaded
5/20 FPS, ordinary Z traces are `[2,3,0,1,2,3,0]`; scientific calibrated T traces
are `[2,3,4,0,1,2,3,4]` at0.05s independent of FPS. Tick intervals cannot be early
(1e-9 timing comparison allowance) and advance on the next60Hz frame (1e-6
allowance on that upper frame bound). These are clock checks, not image-pixel
tolerances. Initial menu activation may report the existing−1 clock sentinel
only before the first armed clock/tick. Require all XY/XZ/YZ panes, exact current
TCZ, source bits, identity, metadata, calibration, selection/Previous, ROI,
history, dirty and workflow preservation. No production imports in the reader.

## Full actual Preferences packet: exact next implementation

Use a free-function probe beside the current settings drawing probe and the
existing HeadlessInputSession. Keep processing/native clock code read-only
unless an executed independent check reproduces a defect.

| Surface | Narrow change and required result |
| --- | --- |
| New `settings_playback_ui_probe.hpp/.cpp` | Fixed `playback-persist` and `playback-reopen` routes; actual preferences/menu/keyboard/mouse controls, copied observations and bounded lossless transport; no expected pixels/tick positions in producer |
| `main.cpp` | Early `--settings-playback-ui-case CASE FRESH_DIRECTORY PREFERENCES_PATH`; construct Window with explicit paired path before invoking observer; refuse an existing case directory |
| `python.cpp` | Private `_settings_playback_ui_case(case_id,directory)`; separately constructed Editor owns the explicit paired path; no Python implementation of playback |
| `CMakeLists.txt` | Add the one observer translation unit to the existing desktop products; reuse one build/cache/runtime |
| `capture_shortcuts_history_ui.py` | One additive `playback` spec, canonical two cases × two contexts, paired_preferences and existing portable process/storage ownership; original memberships and fatal gates unchanged |
| Independent reader/pytest | Four explicit installed IDs, source/product bindings, fresh persist/reopen process identities, clock/source preservation and literal expected sequences; reuse loaded-FPS assertions where applicable |
| CI source/freeze/profile roles | Add exact producer/reader/clock/source dependencies and the four-ID gate to the existing source-platform orchestration; retain independent packets' diagnostics when another fails |

### Persist route

1. Caller seeds a fresh explicit preferences file with opaque future value/root/
   extras compartments only. Default FPS8 remains the live initial setting.
   Literal tiny TCZYX source and initial camera setup are labeled fixtures.
   Use asymmetric values `1000*t+100*c+10*z+y+x`; preserve source/calibration,
   saved ROI/overlay and selection/Previous. Start paused with no history.
2. Open Edit→Preferences, Search `animation_fps`; prove actual InputDouble
   identity, editable rect, clip/hover/focus and delivered text input. A draft5
   changes neither live FPS nor file. Close cancels. Reopen and Revert another
   draft; verify live/file invariants while paused.
3. Reject0 and121 with setting-specific errors; both live value and file stay
   unchanged. Correct to5; Apply publishes/persists exactly5, preserving all
   other60 settings and all unknown compartments. Do not seed known values to
   avoid the tested Apply path.
4. Physically choose ordinary Z playback through the existing Animation menu.
   Deterministic frames prove forward wrap, minimum0.2s intervals and exactly
   display-only changes. Open Preferences while playing; stage20 without Apply,
   proving clock stays5. Apply20 and prove subsequent intervals use0.05s without
   replacing source, resetting history or redirecting the owner. Positions may
   legitimately advance during editing; compare complete data invariants, not
   an artificially frozen display. Keep sampled first/last-tick transition
   boundaries explicit; no elapsed-time sleeps as the correctness oracle.
5. Physically Stop. Exercise Playback-group Defaults (draft8) then Revert (20),
   without changing saved20. Exercise calibrated T timing using existing options;
   frame interval0.05s remains authoritative. Stop and leave saved20 for reopen.

### Fresh reopen route

Constructor must load the exact persist file in a distinct process before the
observer starts. Check FPS20 in live settings and actual Preferences field,
unknown compartments and exact file bytes. Exercise ordinary playback and
calibrated timing with the same independent oracles. Defaults/Revert/Close must
not rewrite the file or create image history. Release playback's source on Stop
and close the headless session before final observations/retention.

### Bounds and acceptance

Use at most750 actual frames/60 seconds/<16MiB raw per case, four canonical IDs
only. Poll the deterministic ImGui clock; a separate bounded real scheduling
smoke may inspect actual monotonic timing but cannot replace frame correctness.
Every case binds original source/product hashes and explicit installed roots;
do not repin old captures after reader/producer edits. Retain hashes and necessary
lossless captures, then remove owned scratch after resolved process cleanup.
Measure all normalized roots, preserve unknown storage subtrees and use only the
active compiler/runtime. Never recreate historical `build/` generations.

Native/map/domain and existing animation regression checks remain separate from
actual Preferences input. API-only/local/headless results cannot establish wheel,
GL, OS scheduling, macOS, accessibility, commercial release or full-family
acceptance. Missing original scientific fixtures remain missing, unchanged.

## Remaining C5 linked stack/view scope

Use the per-key delivery guide and existing P05/P09 packet receipts to select the
next reproduced gap after this chain. Retain independent obligations for C/Z/T
controls and active-owner transitions; XY/XZ/YZ crosshair/zoom/pan/cache/render
consistency; calibration/time-unit/stride policies; stack range/reslice/projection,
montage and animation/export options. Oblique/curved sampling and interaction
need their own packet and oracle. Passing FPS persistence does not close them.
