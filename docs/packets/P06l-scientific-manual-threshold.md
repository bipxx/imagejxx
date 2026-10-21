# P06l — captured scientific manual threshold

## Current execution card

This increment extends manual thresholding to eager, finite-only scalar C/Z/T
datasets. It retains the native threshold engine, executor, cancellation and
history, with one shared Dear ImGui panel in the standalone C++ host and the
optional Python host. It adds no dependency, third-party code or assets.

The current receipt is `build/p06l-001/final-distribution001.json`. Only a passed
receipt matching the current sources and products establishes this finite
artifact handoff. The G049 family and full product parity remain incomplete.
Preserve P06k and all earlier archived products and reports unchanged.

## Behavior

- **Threshold display...** captures the document, C/Z/T position and selection.
  The histogram and inclusive bounds use original scalar intensities. The panel
  shows all three captured axes, using one-based UI positions.
- **Set** previews a display-only red overlay on the captured plane. **Reset**
  clears it. Cancel restores the opening threshold state when its source data
  still matches. Changing navigation does not retarget the operation.
- **To Selection** creates the threshold membership mask from the captured
  plane, including empty membership and holes. It is transient, preserves pixels
  and pixel history, and rebases the open panel to its new selection revision.
- **Apply** runs the native asynchronous operation with the frozen target and
  ROI. A partial target preserves source dtype and all untargeted samples;
  complete coverage may produce UInt8. A pixel change adds one Undo entry.
  Publication clears threshold display state, preserves current navigation and
  refuses stale or cancelled work. Closing the panel returns keyboard focus to
  the active canvas so document Undo works in either host.
- Scientific recording writes the typed `manual_raw_inclusive_v1` record with
  source identity/data revisions, exact T/C/Z target, finite bounds and canonical
  selection metadata. Strict replay refuses another or changed source. The
  workflow's **Apply bounds to current plane / selection** control explicitly
  rebinds to the destination capture. Legacy T1/C1 recordings retain their format.

Public C++ API: `manual_threshold_command.hpp` provides resolve, strict replay,
validation and explicit rebind. Resolve and record validation inspect metadata;
execution validates source samples. Python exposes the same command and target.

```python
command = imagejxx.resolve_manual_threshold_command(
    owner.snapshot(), imagejxx.ManualThresholdRange(100, 200))
prepared = application.prepare("threshold", command.target, command.parameters())
application.publish(prepared)
```

Desktop actions are `show_manual_threshold`, `set_manual_threshold`,
`reset_manual_threshold`, `manual_threshold_to_selection`,
`apply_manual_threshold` and `replay_manual_threshold`. Replay accepts a `record`
and optional explicit `threshold_rebind` target request. Only current plane or
one explicit position, in-place plane execution and active/whole-plane ROI are
admitted. A rebind uses the destination selection; it never silently restores
the recorded selection.

## Verification and residual scope

The functional increment is registered in
[registration003](../../build/p06l-001/registration003/registration001.json):
81 requirements,35 contracts,140 surface proofs,27 run bindings and51 runs.
All99 required family descriptions and all prior ledger rows are preserved.
Family counts remain41 in progress,58 open,0 complete. Verification is partial;
there are no new formal surface proofs or full-family acceptance claims.

| Executed check | Result |
| --- | --- |
| Native threshold and captured-command suites | 2 passed; separate persistence executable passed |
| Source/cache Python | 77 passed,0 skipped |
| Genuine installed-wheel Python | 95 passed,0 skipped; includes18 legacy actual-UI cases |
| Installed public core-only SDK consumer | Passed;420 independent sample checks plus capture/replay/history/stale scenarios |
| Installed standalone plus optional Python ImGui | Both passed actual Set/Apply/Undo/current-plane rebind inputs |
| Product composition | Genuine wheel101; SDK57; standalone25, combined prefix64 |

Reports are under `build/p06l-001/`: `native002`, `python008`, `installed-api003`,
`sdk001`, `standalone004` and `installed-products004`. Initial failed tests and
metadata proposals remain recorded. The legacy driver now invokes the visible
Edit → Undo command; its literal pixel/history assertions remain unchanged.
The installed test recognizes an explicit genuine `uv --target` site as well as
standard Python installation roots. The worker compares all installed wheel
members before and after execution, with the declared installer RECORD exception.

The current handoff links actual native, source Python, installed wheel, installed
core-only SDK and standalone reports. Independent assertions use asymmetric
T2/C2/Z3 data, off-zero captures, rectangle and holed masks, navigation before
Apply, exact untargeted values, history, stale refusal, cancellation, strict
record validation, explicit replay/rebind and Save/Open. Actual ImGui inputs
cover opening, text fields, Set, Apply, Undo and the workflow rebind in both
hosts. Fixture setup/navigation use disclosed APIs; this is not OS-input/GPU or
whole-family certification. Legacy threshold tests remain required.

Remaining: automatic global/local methods, color thresholding, all-range/volume
targets, general scientific multi-step workflow/batch replay, broader filter
options, platform/accessibility/performance and formal reference/surface proofs.
No family is marked complete. After this finite artifact exit, the next owner
is the regular Z-subset consumer of the shared axis/geometry remapper.
