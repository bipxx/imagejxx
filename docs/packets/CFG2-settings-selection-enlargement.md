# CFG2: Preferences-driven selection enlargement

Prepared handoff, 2026-10-25; no CFG2 implementation or acceptance yet. Start
after [CI1](CI1-auto-version-shadow-observation.md) lands. This closes the actual
Preferences → command consumer → fresh-host chain for two existing settings,
not a new enlargement algorithm or a complete settings family.

## Frozen consumer contract

`selection_enlargement_distance` is always a nonnegative **pixel magnitude**:
default15, range[0,1e6]. `selection_enlargement_units` is `pixels` or `calibrated`,
default `calibrated`. Expand signs the magnitude positive, Shrink negative.
Calibrated display multiplies by captured X spacing; changing dialog units
converts the displayed value and preserves the intended pixel geometry. Y
anisotropy produces the existing warning, not a second radius.

Preferences Apply validates the complete map and saves before publishing.
Close/Revert/Defaults are drafts until Apply. Command-specific distance/units
edits never replace the stored defaults. Preserve current metadata-only
selection publication: source pixels, dirty state and pixel Undo/Redo counts
remain unchanged; Previous becomes the old selection. Do not add pixel history
to make this packet resemble a filter.

## Literal fixture and actual routes

Use ordinary[1,1,1,17,17,1] and scientific[2,2,2,17,17,1] UInt8 owners with
spacing[.5,1.5,2]. Scientific current TCZ is[1,1,1]. Initial rectangle R has
corners[(4,4),(12,11)]. The independent results are:

| Authored intent | Expected corners |
| --- | --- |
| Expand2pixels, or Expand1calibrated | E2[(2,2),(14,13)] |
| Expand3pixels, or Expand1.5calibrated | E3[(1,1),(15,14)] |
| Shrink3pixels, or Shrink1.5calibrated | S3[(7,7),(9,8)] |

Derive independent full masks from these literal rectangles. Preserve every
sample word, calibration, metadata, ROI/overlay and navigation state.

Two consume routes use actual Preferences2/pixels → Expand → toggle to1calibrated
→ Cancel → reopen2pixels → Apply E2. Actual Restore returns R. Apply stored
3/calibrated through Preferences → Shrink−1.5 → S3 → Restore R. A per-command
Expand1calibrated gives E2; reopen must still default to1.5calibrated. Cancel.

Persistence and fresh-process reopen routes actually save3/calibrated, load
without API restoration, and exercise E3/S3 on both owner types. The four IDs
are `enlargement-consume-ordinary`, `enlargement-consume-scientific`,
`enlargement-persist`, `enlargement-reopen`. Required contexts are C++/Python on
Windows/Linux, with real persist/reopen pairing. These are16 planned process
recipes, not passing results. Add one negative-distance refusal/correction;
reuse existing codec, generic Defaults/Close and save-failure proof where applicable.

## Exactly three owners

- A: new `tests/native/test_settings_enlargement_consumer.cpp`; existing settings,
  command and pinned enlargement behavior are reused. Check paired atomic
  validation, strict save/reopen/unknown compartments, exact dyadic unit
  consumption and unchanged pixels/history. No native product edit unless a
  distinguishing test reproduces a defect and the interface is reviewed.
- B: `settings_panel.cpp` Number/Choice read-only observations, preserving CFG1,
  conversion and startup shapes; new `settings_selection_ui_probe.hpp/.cpp` and
  narrow `main.cpp`/`python.cpp` hooks. Existing menu/dialog/Restore controls
  suffice; no Window rewrite. Generalize these two SettingKind observations
  rather than add two more name-specific branches.
- C/root: CMake/CI registration, independent Python/SDK/UI readers, one fresh
  pair runner, build/install/evidence/landing. SDK must consume the actual
  UI-written file through public preferences/enlargement headers.

After source handoff, rotate nonauthor review and freeze consumed inputs. Use
one cache/runtime per platform, no extra worktree/environment or nested agent.
Each route remains under16MiB/750frames/60seconds. Admit raw, compression and
link peaks; capture paired contexts in bounded phases and retire verified
terminal scratch. Exact installed products, reader inputs and named cohorts
must be bound from the start. macOS/full CI, real OS/GPU/accessibility and all
remaining families retain their own open requirements.
