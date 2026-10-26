# G034 inclusive Z projection ranges

This packet adds one required projection option through the existing native
kernel, public C++/Python API and shared Dear ImGui editor. It does not close
G034 or the full ImageJ/Fiji editor goal.

The [ImageJ Z-functions documentation](https://imagej.net/imaging/z-functions)
describes start/stop slices and six projection methods. Only that interaction
contract was consulted; no external implementation, asset or dependency was
incorporated. The new code uses the repository's existing commercial-use
license and dependencies. Broader Fiji eligibility exclusions remain unchanged.

| Entry | Contract |
| --- | --- |
| C++ `project(stack,method)` | Preserved whole-stack behavior and signature |
| C++ `project(stack,method,first,last)` | Inclusive zero-based bounds; reject reversed or out-of-depth ranges |
| Python `project(stack,method="maximum",*,first=0,last=None)` | Inclusive zero-based bounds; omitted last means final Z slice; integer-index protocol supported, booleans/floats/strings refused |
| Image → Stacks → Z Project… | Start/Stop are inclusive one-based slice numbers, initially1/depth; existing six-method choice; actual OK/Cancel controls |
| Editor `action("project",first=…,last=…,method=…)` | Same one-based convention as other legacy stack actions; omitted bounds retain whole-stack behavior; unknown parameters rejected |

The UI captures the current owner/revisions when opening the dialog and refuses
an Apply after owner or revision changes. Existing scientific-source admission
remains enforced; this packet cannot flatten T/C into Z. Processing remains in
C++; the Python wrapper forwards arguments. A separate output is attached only
on successful existing job publication; the source and its history stay intact.

The range overload validates the full stack's shape/type, allocates one result
image and scratch proportional to selected Z depth, and reuses existing numeric
methods. It adds execution checkpoints to the projection loop. Maximum/minimum
retain input dtype; the other methods retain existing Float64 output. `stddev`
continues to mean population standard deviation, as documented before this
change. ImageJ numerical/type compatibility, including its deviation policy,
requires an independent pinned reference packet; it is not inferred here.

Verification requires literal endpoints large enough to expose accidental
whole-stack use; all six methods, single slices, omitted final bound, dtype/RGBA
behavior, invalid/coerced bounds and source preservation. A native whole-Window
case physically opens the menu, types Start/Stop, chooses Mean and submits; it
also checks invalid-range refusal/draft retention and Cancel. Installed Python
checks the public API and the same native editor action. Retain exact source,
product and process identities; local checks do not qualify hosted GL, both
standalone launch contexts, wheels or all platforms.

Remaining G034 obligations include grouped projections, scientific C/T targeting
and all-time-frames controls, complete output calibration/channel/annotation
rules, ImageJ numeric/dtype compatibility and full job/resource/workflow/UI/
platform qualification. Range input alone cannot promote the family to complete.
