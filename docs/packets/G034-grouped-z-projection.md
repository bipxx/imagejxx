# G034 grouped Z projection

This packet extends the existing C++ projection reduction with consecutive,
complete groups. It supplies public C++ and Python calls and the shared ImGui
Grouped Z Project dialog; it does not close G034 or full ImageJ/Fiji parity.

The [ImageJ guide](https://imagej.net/ij/docs/guide/146-28.html) describes complete
groups and factor suggestions. The official
[GroupedZProjector implementation](https://wsr.imagej.net/developer/source/ij/plugin/GroupedZProjector.java.html)
confirms that a derived stack retains calibration with Z pitch multiplied by
group size. Only behavior was consulted; no external code or dependency was
incorporated. Repository licensing and Fiji noncommercial exclusions remain.

| Entry | Contract |
| --- | --- |
| C++ header `imagejxx/stack_projection.hpp` | `project_groups(stack,method,group_size)` and additive inclusive zero-based first/last overload |
| Python `project_groups(stack,group_size,method="mean",*,first=0,last=None)` | Consecutive groups, final bound defaults to last slice; nonnegative integer-index protocol; booleans/coerced floats/strings refused |
| Result array | Always retains Z, even one group; source dtype for min/max and existing Float64 policy otherwise; RGBA samples remain independent |
| Image → Stacks → Grouped Z Project… | One-based inclusive Start/Stop; default whole depth, Mean and one complete group; Group size plus read-only first-ten-factor suggestions |
| Editor action `project_groups` | Required integer group_size and one-based optional bounds; unknown parameters, incomplete groups and scientific T/C flattening refused |

Group size must be positive, no larger than the selected count, and divide that
count exactly. No trailing partial group is dropped. Output order follows source
Z order. Group size1 is supported. The editor requires an input stack with at least two slices; its menu is
disabled for a single image. A successful job creates a separate document;
source, selection/metadata revisions, Undo/Redo and dirty state remain intact.
The modal captures its owner/revisions and refuses redirected or stale Apply.
Invalid drafts retain a fixed-height hint row and stay open after refusal.

Whole-stack grouping preserves the source origin/direction/XY/time calibration
and multiplies Z pitch by group size. The native range extension anchors output
Z0 at the first selected source slice using voxel_to_world, including rotated
directions, then scales Z pitch. Source calibration is unchanged. Legacy derived
output drops positioned ROI/overlay rows as the existing projection path does;
full scientific annotation/channel mapping requires its separate typed packet.

The numeric kernel validates the source stack once, reuses the same reduction
for each group and keeps one group-sized scratch vector across all groups.
The binding allocates one contiguous NumPy result and uses the existing sample
conversion policy directly; Python does no pixel processing or result stacking.
The focused public header avoids editing the widely included image.hpp.

For GUI jobs, add grouped output double buffers, Image objects and group-sized
scratch to the existing checked legacy reservation before submission. Overflow
or an insufficient configured processing budget refuses the job. This is a
conservative scheduler reservation, not allocator overhead or total process RSS;
existing source/history/provider/render allocations remain outside that claim.

Verification uses excluded endpoint sentinels and distinct group means/extrema,
all six methods/four dtypes, unit/one/multiple groups, invalid bounds/types,
RGBA independence, calibrated output/session reopen, overflow and resource
refusal, and scientific-axis nonflattening. Native physical UI input exercises
menu, bounds, method, group size, factor suggestions, refusal, Cancel and owner
switches. Installed public C++ SDK and Python origins must match frozen sources.

Existing population stddev and dtype policies remain unchanged; this is not an
ImageJ numerical-reference equivalence claim. Scientific C/T/all-time-frames
commands, annotation/channel metadata policy, full typed replay/recording,
virtual-source/resource qualification, exact standalone/Python interaction
captures and full hosted/Linux/macOS/accessibility/release gates remain open.
