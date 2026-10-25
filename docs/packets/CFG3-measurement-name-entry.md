# CFG3: author measurement names in Preferences

Prepared successor only, 2026-10-25. No implementation or acceptance yet. Start
after CFG2 lands. The StringList control retains preloaded custom names but
cannot add an unseen name. Reuse the native validator, result formatter and
existing Preferences transaction; no new processing kernel or list framework.

## Minimal product contract

Add a private pending-name string and actual InputText/Add controls alongside
the Measurements choices. Validate the candidate ordered list with existing
`Settings::set("measurements", candidate)` before swapping only the draft.
Successful Add clears the buffer. Failed Add preserves buffer/list and exposes
the native error. Add to[] starts a one-name explicit list;[] continues to mean
all columns. Do not trim, case-fold or otherwise normalize names.

Native limits remain at most1,024 entries, each1–256 UTF-8 bytes, not all ASCII
whitespace, and no exact duplicates. Raw pending text is not a selected column
and never persists. Apply remains the sole publication/save action. New opening,
Close and Revert clear pending text. Defaults clears it only for Measurements
or All; switching search/group or resetting another group retains the draft.
Preserve existing checkbox removal, All and explicit-list ordering semantics.

## Complete distinguishing workflow

Use an ordinary5×6 UInt8 source: zero background, interior rows[2,3,4] and[5,6,7]
at x1..3/y1..2. Actual Analyze→Analyze particles→OK with connectivity8,
min_area0/max_area null produces one result: min2.0,max7.0,bbox[1,1,3,2],pixels6.
The first three names already exist in raw results but not preset choices.
Generate real results through the menu; do not inject synthetic result rows.

`measurement-names-edit-persist`: generate particles; actual Preferences add
min/max/bbox and select preset pixels, preserving order. Exercise blank,
duplicate and257-byte Add refusal/correction with unchanged whole draft/applied
map, file and results. Apply→Close→visible table→Save CSV using the file browser.
Parsed CSV headers must be[min,max,bbox,pixels], row["2.0","7.0","[1,1,3,2]","6"]
at default precision−1. Keep raw result rows separate from observed table cells.
Remove bbox then Close to prove Cancel; reopen Defaults as draft[], then Revert
to the saved list and Close. Preserve all source words/carriers/history.

`measurement-names-reopen`: a distinct process loads that actual file without
API restoration, generates particles, shows saved names, closes Preferences,
observes table and saves CSV. Preferences/source/history stay unchanged.
Two routes×C++/Python×Windows/Linux are eight planned recipes, not passes.
Scientific particle generation is not implemented by this ordinary-source
packet; global result/Preferences behavior has the same launcher authority.

## Three-role dispatch boundary

- A: new native measurement-name consumer test, existing validator/persistence
  and known component result; no product kernel change without a reproducer.
- B: `settings_panel.hpp/.cpp` buffer/list controls and diagnostics; narrow
  `window.cpp` Results Save/Clear control and rendered header/cell observations;
  new `settings_measurement_ui_probe.hpp/.cpp` and narrow main/Python hooks.
- C/root: independent reader/fresh-pair runner/SDK client, registration,
  source/product/cohort joins, one build/install lock, retention and landing.

At dispatch freeze exact interfaces/files and public SDK particle seam before
editing. Use existing HeadlessInputSession/file-browser adapters and the CFG2
bounded envelope (16MiB/750frames/60seconds). Pin executable and reader inputs
from entry, reference one canonical reader manifest, and verify/retire terminal
scratch. Exactly three agents including root; no new cache/environment/worktree.
Preserve four CG0 WIP files and all original platform/release requirements.
