# Selection creation in native workflows

`NativeWorkflowOperation` accepts `operation="selection_creation"`, empty
registry parameters, the current-plane/active-selection target and an optional
typed `selection_creation` request. The request contains the same candidate,
Replace/Add/Subtract/Intersect/XOR mode and geometry choice as the direct C++
creation API. It includes complete style and opaque attributes. An original
V1/V2/V3 creation record may supply the choices when authored options are absent.
Authored options may replace intent while the complete original stays unchanged.

For example, Python can author a full-region step without first editing an image:

```python
choices = ij.SelectionCreationRequest(
    ij.Selection("rectangle", [(-2, 0), (5, 4)]),
    ij.SelectionCreationOperation.ADD,
    ij.SelectionCreationGeometry.FULL_REGION_V3,
)
step = ij.NativeWorkflowOperation(
    "selection_creation", {}, ij.TargetRequest(), selection_creation=choices
)
program = ij.NativeWorkflowProgram([step])
prepared = ij.prepare_native_workflow(document.snapshot(), program.steps)
document.commit(prepared.begin_edit())
```

V6 programs require at least one retained creation step and add a closed
`selection_creation` field to operation rows.
Other operation rows use null. The nested request has operation, geometry and
closed candidate fields with nested attributes. V6 also retains enabled flags,
typed filter options and optional authored histogram choices. Whole-envelope
depth includes the program, step and request containers. V1–V5 remain closed to
creation steps and retain their existing output schemas.

The entire workflow is admitted before processing samples. Creation admission
checks its full geometry, original equation and resource bound. Each enabled step
resolves on the preceding private stage. Later filters use the visible selection
intersection; histogram analysis describes that stage. Disabled steps retain and
validate portable choices without resolving destination compatibility. Creation
steps preserve navigation and make final publication depend on the captured view
as well as document data. The last geometry step supplies Previous, as sequential
execution would, while the live owner receives one guarded transaction.

The public C++ vector entry point admits input storage before capturing the
source, steps, limits and controls. Callbacks cannot redirect the workflow by
reassigning caller variables. Preflight delegates callback dispatch to command
scopes during each step, preventing recursive dispatch of the same callable.
Cancellation and callback exceptions discard the proposal. Private borrowed
entry points retain their immutable-input ownership contract; Python keeps all
read-only step owners alive while execution releases the GIL.

The shared Run review compiles recorded creation commands and retained V6
programs. Its parameter editor exposes Boolean mode, geometry mode, shape,
vertices, stroke width, rounded corner diameter and additional attributes. Keep
changes authored intent, retains the original capture and invalidates downstream
confirmations. Keep Program, reopening, detached preparation and Apply use the
same typed core. V6 programs also survive native sessions and batch execution.

The paired Q04 recipe performs actual vertex/stroke editing, keeps and reopens
the program, confirms both steps, prepares a regional selection plus histogram,
then applies. It checks that preparation leaves the live document untouched and
that final source samples, metadata, navigation and pixel history are preserved.
API tests cover every creation mode/shape, old captures, malformed late steps,
depth, source sample policies, cancellation, batch and small record budgets.

This does not complete regional rotation, continuous composite paths, planar/axis
remapping, ImageJ ROI interchange, persistent selection-tool settings or managed
large-ROI/runtime acceptance. The source-independent typed candidate codec uses
vector storage bounds; binary mask decoding remains part of original-record
validation. Allocator/RSS details, opaque callable storage and custom reader
caches remain outside the logical workflow bounds.

Windows and Linux qualification passed for this scope. See the
[scoped receipt](reviews/p12r-creation-workflow001.json).
