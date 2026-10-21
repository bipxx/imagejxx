# IEEE Float32 Remove NaNs

The C++ Remove NaNs kernel and source-bound commands now accept explicitly opted
`PRESERVE_IEEE_FLOAT32` datasets. The optional Python interface and both shared
ImGui editor job paths use the same native implementation. Other IEEE processing
operations retain their existing admission checks.

The filter replaces NaN centers using the ImageJ 1.54r NaN-aware median. Infinity
centers remain unchanged and infinities participate in neighboring medians.
Untouched signaling/quiet NaN payloads, infinity signs and signed zeros retain
exact source words. All-NaN neighborhoods produce canonical positive quiet NaN.
Original-source halos, rectangular extents and restored irregular ROI masks
follow the existing ImageJ profile. Radius remains finite in [0, 100].

```python
import imagejxx as ij
import numpy as np

words = np.full((5, 5), 0x40e00000, np.uint32)  # 7.0
words[2, 2] = 0x7f800001                      # signaling NaN
words[0, 0] = 0x7f800000                      # positive infinity
filtered = ij.remove_nans_imagej(words.view(np.float32), radius=1)

source = ij.Dataset(words.view(np.float32), axes="YX",
    sample_policy=ij.FloatingSamplePolicy.PRESERVE_IEEE_FLOAT32)
owner = ij.Document(source)
command = ij.resolve_remove_nans_command(owner.snapshot())
prepared = ij.prepare_operation(command.operation, command.target, command.parameters())
owner.commit(prepared.begin_edit())
owner.undo()  # restores the original signaling NaN word
```

Qualification uses all 108 independently captured cases in
`tests/fixtures/remove-nans-ieee-imagej154r.json`, including 324 source planes,
current/all-Z targets, rectangle/polygon masks, signaling NaNs, infinities,
subnormals and extreme finite samples. Native command checks cover scientific
and legacy execution, preview, history, records/replay, rebinding and sessions.
Python adds strided, reversed, Fortran, big-endian, unaligned and read-only
buffers; bitwise no-ops; metadata-only resolution; and editor in-place/new-output
paths. The previous finite-only and quiet-NaN storage policies remain strict.

The processing evidence is in `build/p07x-remove-nans-ieee001/final-audit001.json`.
The subsequent shared dialog evidence is in
`build/p07y-remove-nans-ui001/final-audit001.json`.

R52-R59 exercise real ImGui pointer/button, Ctrl+A and text events in all four
source/installed C++ and Python contexts. They cover current/all-Z selection,
scientific C/T targeting, preview isolation, stack Yes/No/Cancel, new output,
polygon protection, no-op redo preservation and undo/redo. R57 uses canonical
all-NaN input; R59 uses negative signaling all-NaN input and verifies canonical
positive quiet output plus exact source restoration. Mixed cases include both
infinity signs, signed zero and signaling NaNs inside/outside the selection.
Assertions reconstruct declared words and binary64 storage using integer fields
and check pixels, display/threshold metadata, dirty state, history, workflow,
remembered radius and terminal job-resource usage. The observation scope is
actual ImGui input and CPU cache/upload-input values; it does not claim OS input
or GPU screenshot equivalence.

The packet passes 71 Windows native/desktop suites and identical source/installed
8,979-case Python inventories without skips or exclusions. It retains every
previous case. Existing R14-R51 dialog and IEEE01-IEEE04 viewing cases are freshly
rerun too. The previous Linux core and installed public SDK results remain
applicable because all 249 core/public/IO inputs are unchanged (apart from the
previously recorded documentation-only annotations). The CMake-only test wiring
change puts NaN-document artifacts under the configured run root, preserving
fresh-directory requirements without deleting prior evidence.

Native [scientific Float32 TIFF](SCIENTIFIC_FLOAT32_TIFF.md) now supports file
read/write and shared editor integration, including opted IEEE source words. Other IEEE
rank operations still require their own reference qualification before admission.
