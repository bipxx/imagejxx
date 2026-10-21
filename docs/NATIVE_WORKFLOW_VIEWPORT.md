# Prepared workflow image and annotation viewport

Both Dear ImGui hosts offer a private image viewer in **Run on image**. Confirm all
steps, choose **Prepare image preview**, then **View prepared image...**. Preparation
uses the C++ native workflow planner on a detached snapshot. Pure intermediate
exports are encoded and validated; this action creates no output files or staged
siblings and cannot Apply. Output destinations and publication consent are not
needed. Use **Prepare complete workflow** for the existing staged-output and guarded
document-commit review. That prepared result can also be viewed when its remaining
render budget admits it.

The viewer shares the immutable prepared dataset through a new native document
owner and a private desktop facade. It is never installed in the live document or
application controller. T/C fields, XYZ coordinates, slice sliders, wheel/keyboard
navigation, linked zoom, pan and fit affect only this viewer. Existing orthogonal
renderers draw exact XY/XZ/YZ samples with scientific spacing and display/channel
policies. Existing native geometry and display annotation renderers draw the
prepared plane/T/C-visible geometry, text and embedded image payloads. Annotation
editing controls and publication callbacks are absent from this read-only viewer.
Existing typography and font coverage limitations still apply.

**Close preview** releases its rendering state while retaining the prepared review.
Reopening starts from the prepared display, not the last private navigation state.
Closing Run, changing a target or authored choice, accepting new settings, changing
the recorded workflow or active source/Data, canceling, re-preparing or applying
invalidates the viewer and its caches. Ready cancellation is checked through the
existing atomic epoch before another viewer frame. Running work keeps its retained
reservation until it retires. A stale ready result cannot be installed or applied.
No prefix cache is added: the current prepared result owns the viewer lifetime.

Pure preview preparation reserves the processing cap and limits planner retention
to half that cap. Render admission uses at most the remaining half; a full prepared
workflow uses the smaller of half the cap and its unconsumed retained allowance.
Before constructing the facade or sampling viewer planes, conservative admission
accounts orientation extents, metadata materialization, legacy selection/ROI and
overlay visuals, planar geometry and display annotations. Channel composition has
its existing additional pixel/working bounds. A refusal leaves the source and
outputs unchanged. These logical copy/phase estimates and scheduler reservations
are not whole-process allocation quotas. Larger planes or metadata can require a
higher processing setting.

Viewer textures are released on the creator thread before the GL renderer shuts
down. The same retained preview can move between the headless test renderer and a
real GL context without retaining texture names from a destroyed context.

The core already exposes `PreparedNativeWorkflow::preview()` in C++ and
`prepared.preview` in Python. This increment adds the shared desktop viewer; it
adds no second processing engine, public viewer wrapper or file format.

The evidence for this increment is
`build/p06q-workflow-viewport001/workflow-viewport-handoff001.json`. Coverage includes
path-free preparation, exact three-plane samples, T/C/slice/zoom isolation, visible
geometry/text/RGBA rendering, ready cancellation, re-preparation, target invalidation,
stale Data rejection, render admission, both hosts and installed desktop scenarios.
[Processing parameter controls](NATIVE_WORKFLOW_PARAMETERS.md) now invalidate this
viewer when opened/kept. [Reorder/disable](NATIVE_WORKFLOW_ORDER.md) also discards this viewer. [Per-input Batch authoring](NATIVE_WORKFLOW_INPUT.md) reuses this private preview. No ImageJ/Fiji feature family is declared complete by this checkpoint.


[Native Batch setup save/load](NATIVE_WORKFLOW_BATCH_SETUP.md) now persists common
and input-specific intent through the same portable model. Loading requires fresh
input review and publication consent; scientific raster IO and broader parity remain.
