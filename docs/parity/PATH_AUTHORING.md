# Native path authoring

The C++17 core provides immutable command edits through
`imagejxx/path_edit.hpp`. The Python interface exports the same native functions:

```python
import imagejxx as ij

path = ij.ImageJCompositePath([0, 1, 2, 3, 3, -4, 8, 15, 11, 3])
divided = ij.split_composite_path(path, 1, t=0.5)
closed = ij.edit_composite_path(
    divided, ij.PathSegmentEdit.InsertBefore, divided.segment_count, [4]
)
```

Indices count commands, including moves and closes. `Replace`, `InsertBefore`
and `Remove` operate on one whole command. Replacement/insertion accepts exactly
one canonical M/L/Q/C/Z command. Removal requires an empty replacement. The whole
result is validated: deleting a move does not silently repair a following curve,
and deleting the last command cannot produce an empty path.

Splitting L, Q or C uses its original continuous parameter, retaining two
segments of the same type. De Casteljau subdivision retains Bézier geometry up
to binary64 arithmetic. Splitting Z inserts a line to the interpolated closing
edge point, then retains Z. Moves cannot split; the parameter must be strictly
between zero and one. All unrelated commands retain their original operands.
No operation rasterizes or reads image samples.

`PathEditLimits` bounds source/result segment and scalar counts and logical
working storage. It charges the source commands, output staging, immutable result
commands and a fixed 4096-byte scratch reservation before allocating the output.
Caller-owned objects, callback captures, allocator bookkeeping and later
document/history/record storage remain separate host responsibilities. This is
not a process memory guarantee. Refused admission or invalid geometry invokes
no callbacks. Cancellation is checked during scans/copies and around callbacks.
Admitted immutable results are built privately before progress callbacks, so
callback mutation of source variables or limits cannot redirect the result.
Progress reports `(0, 1)` then `(1, 1)`; exceptions or finish cancellation abort
the return. Python uses the existing `cancellation`, `progress` and `checkpoint`
keyword conventions.

Windows and Linux each pass 360 focused Python cases without skips, three native
test groups, and an independent installed SDK consumer with 36 path-edit checks.
The new Python cases evaluate subdivided curves against independent Bernstein
equations, including non-midpoint splits, multiple subpaths, exact unrelated
operands, signed zero, limits, grammar failures and cancellation. Existing path
and selection-edit tests cover compatibility, including actual headless ImGui
selection input. They do not establish new segment-authoring UI behavior.

The [shared ImGui segment editor](PATH_SEGMENT_UI.md) now exposes these
operations on active/saved selections and planar annotations, with private
preview, Apply/Cancel and explicit active-selection Undo. See its scoped
qualification receipt; the older figures above describe the core milestone.

The authoring workflow remains incomplete. Next add interactive path drawing
and bounded affine resizing, including collapsed bounds and handle crossings.
Portable active-path intent, full typed filter/resource integration, continuous
Booleans and original ImageJ/Fiji feature-family qualification remain open.
