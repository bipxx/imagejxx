# CFG1 execution freeze

**Executed result:** [scoped-qualified](../parity/reviews/cfg1-settings-wand001/receipt.json).
See the [clause ledger](CFG1-qualification-ledger.md) for actual four-context
UI/native/API/SDK proof, corrected observers and retained failures. Normal CI
discovery uses committed fresh-process Python counterparts, bound public-wrapper
bytes and verified owned process-tree cleanup; the original freeze below remains
the behavior contract. No native settings/codec/Wand algorithm change was needed.

Activated 2026-10-24 from pushed P13i
`0323a731752a020d8f0913fc593036d71e4193b5`. The preceding turn made concrete
progress: P13i qualification and landing, including the missing batch boundary
regression. Full M0–M13 acceptance remains open. This card freezes the proposed
[settings-consumer packet](CFG1-settings-consumer-audit.md#cfg1-implementation-contract).

## Three owners and files

- A: new `tests/native/test_settings_wand_consumer.cpp` and
  `CFG1-native-handoff.md`. Existing native product APIs are read-only until an
  executed counterexample justifies a named repair. No new kernel or codec.
- B: two field diagnostics in `src/desktop/settings_panel.cpp`; new
  `settings_wand_ui_probe.hpp/.cpp`; narrow hooks in desktop `main.cpp` and
  `python.cpp`. Window/public-header changes require an explicit seam transfer.
- C/root: CMake, capture/profile registration, independent
  `tests/python/test_settings_wand_ui.py`, transport tests, SDK consumer/readback,
  verification, evidence, cleanup, git and these execution records.

Workers share the checkout, preserve others' edits and report interface changes.
Exactly three agents include root/C; no nested agents, worker builds, installs
or commits. C alone holds the build/install lock. CG0 algorithm implementation
does not overlap this integrated UI packet; independent reference/feasibility
preparation may continue.

## Frozen source fixture and entry points

Owned eager UInt8 shape `[1,1,1,3,3,1]`, rows `[10,20,20]`, `[20,10,20]`,
`[20,20,11]`, default calibration, canvas seed `(0,0)`. Initial Selection is
rectangle `[[1,1],[3,3]]`; Previous is line `[[0,2],[2,0]]`. One saved ROI is
`cfg1 retained ROI`, rectangle `[[1,0],[2,1]]`, wildcard TCZ, attributes
`{cfg1:"retain ROI"}`. One point overlay at `(2,0)` has `cfg1:"retain overlay"`.
Retain title `CFG1 Wand preferences`, properties
`cfg1:{label:"retain properties",value:17}`, extension `cfg1:"retain extension"`
and record-layout `cfg1:"retain layout"`. Display range `[0,20]`, zoom 40 and
crosshair `[0,0,0]` are labeled fixture setup. None is a tested settings action.

Standalone entry: `--settings-wand-ui-case CASE_ID FRESH_DIRECTORY PREFERENCES_PATH`.
Python entry: `Editor(preferences_path=...)._settings_wand_ui_case(case_id,directory)`.
Each run gets a fresh process, idle Window and absent owned output directory.
Only `wand-persist` followed by `wand-reopen` intentionally shares one C-owned
preferences path within its launch/platform pair. C creates this version-1
fixture **before** the persist host is constructed:

```json
{
  "format": "imagejxx-preferences",
  "version": 1,
  "values": {
    "wand_tolerance": 0.0,
    "wand_connectivity": 4,
    "cfg1_future": {"items": [true, null, "λ", 18446744073709551615]}
  },
  "extras": {"recent": [], "cfg1_extra": {"label": "μ", "values": [3, 1]}},
  "cfg1_root": {"note": "root", "uint": 18446744073709551615}
}
```

Reopen uses the exact file written by the actual persist UI process, without
restoring settings through an API. Record process IDs, path and byte/hash
continuity. Other routes use isolated preferences files and defaults.

## Observation contract

Schema `cfg1.settings-wand.ui.001`; root fields: `acceptance_claim:false`,
`case_id`, `context`, `status`, `error`, `fixture`, `setup_events`, `events`,
`frames`, `states`, `milestones`, `bounds`. `status` is observed/failed, never an
acceptance verdict. `states` is keyed by the exact checkpoint names in the
parent route table. Missing/duplicate/incomplete states fail acceptance.

Each state records `frame`, full `settings`, `preferences`, `preferences_file`
(path/exists/byte_count/utf8/sha256), `owner`, `workflow`, `results`, `recording`,
`usage`, `error`, and `observations` (preferences_panel/tools/ordinary_pixels).
Owner includes document/data identity, shape/dtype, declared_u8_hex,
storage_f64_hex, selection/previous and their observations, metadata/calibration,
rois/overlays/display/revisions/dirty/undo/redo/history. A selection observation
is null when absent; otherwise it contains `mask_shape:[3,3]`, `mask_u8_hex`,
`packed_mask_hex`. These are observed values, never generated expected answers.

Frames contain ordinal/label/events and rendered input_frame/session,
preferences_panel/host_controls/ordinary_canvas/window_error/error_modal and
actual_hover id/window. Fixture runtime records actual process_id, imgui and
producer_source. C externally pins executable/modules and reader/producer bytes
before/after processes. Events record kind and frame bounds plus actual
control/point/text where applicable. Negative-tolerance refusal/correction use
bounded scalar milestones retaining settings, file, panel, owner identity,
revisions and history. Full source checkpoints are not replaced by milestones.

The two field diagnostics retain real scoped text IDs, item/group rectangles,
editable points, enabled/active/focused/hovered/visible and clip rectangles.
InputInt group geometry must not be reported as its text identity. Clicks and
typing must prove actual focus/hover/clip ownership. Preferences opening/search,
Apply/Revert/Defaults/Close, Wand tool selection and canvas seed use delivered
input. Source creation and opaque-file seeding are labeled setup only.

## Independent acceptance and bounded execution

Expected masks remain literal: 0/4→`ff0000000000000000`,
0/8→`ff000000ff00000000`, 1/8→`ff000000ff000000ff`,
1/4→`ff0000000000000000`. First Wand Previous is the initial rectangle;
subsequent Previous follows the preceding selected mask. Transient selections
preserve pixels/pixel-history/workflow under the existing native policy.
Apply/rejected drafts preserve current selections; rejected invalid pairs
preserve the full live map and preference bytes.

C requires all four route IDs in standalone/Python on Windows/Linux, native
groups, installed public-header readback of the actual UI-written file, relevant
settings/preferences/consumer regression and transport omission/identity/bound
guards. Reuse historical evidence only after identity/availability verification.
Headless ImGui is not real OS/GPU/accessibility certification. No ImageJ parity
claim follows solely from these transparent four native consumer masks.

Keep each capture below 16 MiB, 750 frames and 60 seconds. Reuse the active
compiler caches/runtimes. Count the new CFG1 evidence root alongside existing
roots; the entry measurement is 8,495,283,464 bytes under the combined
8,589,934,592-byte limit. Recheck headroom/cache-growth and link scratch before
building. One route/persistence pair at a time is sufficient. Retain diagnostics
and necessary compressed captures, verify hashes, then remove owned scratch
after every terminal success/failure. Never recreate `build/` or archive whole
caches/environments/SDKs. Overwritten P13i runtime bytes become unavailable
historical evidence unless exact bytes remain; record that additively.
