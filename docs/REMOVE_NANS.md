# Remove NaNs implementation

The current profile supports Float32 grayscale with canonical finite samples
and binary32-origin quiet NaNs. The C++ pixel API, Python interface, typed
commands, workflows and both Dear ImGui hosts implement it. Infinity and
signaling-NaN source profiles remain open; the wider ImageJ/Fiji goal remains
active.

## Pixels and command intent

`remove_nans_imagej` replaces only NaN centers using the ImageJ 1.54r fractional
circular footprint, nearest-edge extension and original-source halo reads.
Radius is finite in [0,100]; radius zero uses the five-point footprint. Each row
retains its last non-NaN source sample as the median guess. Partition selection
preserves ImageJ's signed-zero tie order. An all-NaN neighborhood produces
canonical positive quiet NaN. Finite samples and samples outside the write
extent retain their bits. Irregular masks are restored after rectangular
filtering. Cancellation is checked during validation, copy, traversal and
selection.

`RemoveNaNsCommandOptions(radius=2.0)` and `resolve_remove_nans_command` capture
immutable radius-only intent and a source-bound target. Schema-1 `imagej_154r`
records strictly validate options and target claims. Strict replay checks the
original owner/revisions; cross-document replay requires explicit rebinding.
Resolution and `remove_nans_command_region` inspect metadata/geometry without
reading samples. Existing finite-only rank admission remains unchanged.

Scientific and eager legacy execution preserve storage policy, untargeted
planes, ROI geometry, metadata and display state. Bitwise comparisons distinguish
real pixel edits from unchanged NaNs or signed zeros. The legacy mask path
avoids finite-only conversion; `SharedPlanes` carries its policy through legacy
reconstruction. Apply, new-image staging, Undo and Redo preserve source/output
bits. Preview proposals are transient and cannot publish; stale proposals
conflict. Registry and captured workflows serialize and replay, and quiet-NaN
results round-trip through sessions. Workspace admission reserves the footprint
and both median buffers.

## Shared editor

Process > Noise > Remove NaNs opens the shared radius dialog in C++ and Python
hosts. It provides Preview, Create new image, stack Yes/No/Cancel, and scientific
C/Z/T and ROI targets. Apply keeps the full captured target; Preview uses a
contained in-place plane. Accepted radius has independent per-window session
memory, including zero. Stack Cancel retains the accepted value. Invalid radius
and unsupported pixel types cannot Apply.

The Python UI convenience method is:

```python
editor.apply_remove_nans(options=ij.RemoveNaNsCommandOptions(1), wait=True)
```

Range, LUT and manual threshold properties stay unchanged. Existing threshold
overlays now use an explicit read-only quiet-NaN display path: quiet NaNs are
validated by bits and remain untinted. The public finite-only overlay and
threshold analysis/mask/selection contracts remain unchanged. Extending
threshold editing to nonfinite source profiles is separate remaining work.

The editor's existing no-op policy creates no history or recording entry for
bit-identical output. An unchanged all-canonical-NaN image therefore retains
Redo. Preview and Cancel preserve source pixels/history; new-image output leaves
the source unchanged and owns its output Undo/Redo.

## Verification checkpoint, 2026-10-20

- Windows passes 51 selected native/desktop suites, including 43 shared widget
  cases. Linux passes 12 selected native suites. Each platform checks 770,062
  Remove NaNs command assertions, 216,477 pixel assertions and 44 new quiet-NaN
  threshold display assertions, plus the existing finite threshold suite.
- Source and installed Python pass the same 7,799 cases. All 7,417 prior common
  cases remain, alongside 100 NaN pixel cases, 194 command cases, 13 UI cases
  and 75 relevant finite threshold regressions.
- Actual input R45-R51 passes in source and installed C++ and Python contexts:
  stack No/Yes/Cancel, scientific current target, new image, all-NaN no-op and
  full-bounds polygon. Independent readers check effective/requested inputs,
  Float32 and double-storage bytes, preview range, preserved metadata and
  geometry, recording, Undo/Redo and released resources. These are ImGui/CPU
  observations, with no OS-input or GPU-parity claim.
- The upstream triangle rasterization fixture supplies exact mask membership.
  R51 includes a negative quiet-NaN payload outside that triangle but within its
  bounding box, proving mask restoration through Preview, Apply and history.
- Both hosts, extensions, installed Python files, public headers and core/I/O
  libraries are rebuilt and byte-checked together. The installed SDK consumer
  retains Top Hat/Preview checks and adds Remove NaNs command/Undo checks. A
  genuine venv passes 120 strict-origin projection checks; archived origins and
  the SDK build copy remain byte-verified against final installed products.

The first physical run exposed finite-only threshold rendering before the
filter dialog could open. That product defect is fixed with the explicit
private display lane. The initial R51 test expectation used incorrect triangle
boundary intuition; the unchanged pinned ImageJ mask corrected it, and an
outside-mask payload strengthened the test. A combined SDK harness needed an
explicit return after its old `main` became a helper. Raw failures, prior test
sources and counterexamples remain retained; expected pixel/history assertions
were not weakened.

Fifty-two owned scratch targets were archived, hash/size/CRC checked and removed,
saving 2,372,132,629 net bytes. Current installed artifacts and raw observation
reports remain. No new dependency, Qt component, non-commercial plugin or
reference Java/runtime asset was incorporated.

See [UI review](parity/reviews/remove-nans-ui-review-001.json) and
`build/p07s-remove-nans-ui001/remove-nans-ui-handoff001.json`. Earlier pixel and
command reviews remain historical evidence.

## Next gaps

1. Qualify the remaining IEEE nonfinite source profiles across storage, display,
   I/O/session/history and rank operations, including infinities and signaling
   NaNs, before extending their editor admission.
2. Close remaining RGB rank profiles against pinned upstream observations.
3. Continue the indexed-format, commercially usable Fiji, broader editor and
   release gates in the main plan. The aggregate remains 42 in progress,
   57 open and 0 complete; this checkpoint does not mark a whole feature bucket
   or the full requested goal complete.
