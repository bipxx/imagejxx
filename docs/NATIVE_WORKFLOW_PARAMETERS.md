# Workflow parameter authoring

Scalar `contrast_apply` accepts a complete saved command with empty parameters,
or exactly `minimum`, `maximum` and integer `unsigned16_bits` authored parameters.
The shared parameter editor exposes those three fields and preserves any original
capture. Its target uses the existing in-place plane/ROI policies; later steps see
the actual reset presentation. See [Apply workflows and Batch](CONTRAST_APPLY.md).


In either Dear ImGui host, select a processing step in **Run on image** and choose
**Edit this step's parameters...**. Confirm earlier steps first. The native planner
prepares their detached result without publishing files; the form uses that private
input for context-dependent defaults and validation. No live document or workflow
table changes while the form is open.

The form uses the native `OperationSpec`/`OperationParameter` descriptors. Booleans
use checkboxes, named choices use menus, scalar values use exact text, shaped numeric
data uses `{shape, values}`, and image data uses `{width, height, channels, type,
values}`. Image sample type codes are 0 UInt8, 1 UInt16, 2 Float32 and 3 Float64.
Scalar numeric fields can retain real, int64 or uint64 intent; numeric-array fields
also offer shaped arrays. Optional values can explicitly retain automatic/null
intent. Text supports multiline input and escaped JSON text for embedded NULs.
Integer parsing and records preserve signedness and full 64-bit values. Processing
continues to use each existing operation's numeric semantics.

**Keep workflow parameters** validates the complete typed proposal, replaces only
the current step's parameter intent, and clears confirmation for that step and every
later step. It invalidates prepared results and their viewer. Original captures,
current target controls and calibration consent remain intact. Context defaults
materialized in the form become explicit current values when kept. **Discard
parameter edits** leaves the prior parent draft intact; reopening starts from it.
Invalid or unfinished data leaves the form open and changes no parent intent.

Confirm the steps again and use **Prepare edited program**, **Keep edited program
in Workflow**, then Workflow **Save** to persist the changes. Workflow **Load**,
Run, path-free image preview and native session Batch consume the same typed model.
[Per-input Batch editing](NATIVE_WORKFLOW_INPUT.md) reuses the same parameter form.

## Gaussian and rank captures

A bound Gaussian/rank original remains complete immutable history. The editable
current values are separate raw options: Gaussian sigma and scaled-units intent,
or ImageJ rank method and pixel radius. The original record is still fully
validated before kernels, even when current options differ. Gaussian destination
calibration changes still require the parent's explicit consent. Pixel-unit
calibration does not offer enabling scaled units; an already retained true flag
can be cleared. Native destination resolution applies its existing calibration and
effective-sigma bounds. Rank radius/method limits and the 1.54r radius-zero cross
remain unchanged.

C++ callers append `NativeWorkflowOperation::filter_options`, an optional
`NativeWorkflowFilterOptions` variant of `GaussianBlurCommandOptions` and
`RankFilterCommandOptions`. Python callers pass the existing option object:

```python
step = imagejxx.NativeWorkflowOperation(
    "gaussian_blur_imagej", {}, target,
    original=captured_record,
    filter_options=imagejxx.GaussianBlurCommandOptions(0, False))
program = imagejxx.NativeWorkflowProgram([step])
loaded = imagejxx.NativeWorkflowProgram.from_json(program.json())
prepared = imagejxx.prepare_native_workflow(document.snapshot(), loaded.steps)
```

Typed options require empty registry `parameters`, the matching operation name and
plane execution. Without typed options, bound commands retain their old original-
intent execution contract. Registry operation captures continue to retain immutable
history while current explicit parameters execute. C++ also exposes metadata-only
`validate_gaussian_command_options` and `validate_rank_command_options`; destination
resolution remains authoritative for source-dependent limits. Headers, core,
bindings and consumers must be rebuilt together after the appended DTO field;
this increment does not promise compatibility with older compiled DTO layouts.

## Portable versioning and bounds

All-enabled V1 records remain unchanged for programs without authored typed-filter options.
A program containing those options emits closed V2: every operation step adds a
required `filter_options` field, null or one of:

```json
{"kind":"gaussian","sigma":0.0,"scaled_units":false}
{"kind":"rank","method":"mean","radius":0.0}
```

[V3 order/enabled state](NATIVE_WORKFLOW_ORDER.md) retains disabled steps and adds
`enabled` to every row, while keeping V2 filter-option grammar on operations.
Other root/target/parameter/import/export grammar is unchanged. V1 refuses
the new field; V2 requires it on all operation steps and rejects unknown/missing
option fields. V2 containing only null options canonicalizes back to V1. Captured
original versions and their original targets are never rewritten. See
[portable program bounds](NATIVE_WORKFLOW_PROGRAM.md).

The private prefix retains its existing half-cap scheduler reservation. Editor
admission bounds the borrowed current request before copying, materialized defaults,
logical record and escaped JSON transport. Fixed input buffers share a separate
bounded allowance and have no unbounded resize callback. A full buffer blocks Keep;
large drafts may require a higher processing setting. Strict parsing checks each
field and cumulative decoded JSON before metadata/native request copies. These are
phase/copy admission estimates, not whole-process memory quotas.

Closing Run, cancellation, source/Data/program or accepted-setting changes discard
the nested editor and private prefix through the existing guards. No editor control
selects output paths or stages files. Geometry-changing operations remain refused
until an explicit remap/branch and annotation policy exists.

Evidence: `build/p06q-workflow-parameters001/workflow-parameters-handoff001.json`.
Persistent [reorder/disable](NATIVE_WORKFLOW_ORDER.md) and
[per-input Batch authoring](NATIVE_WORKFLOW_INPUT.md) are now available.
Scientific raster IO/axes, fonts/typography
and broader parity/release audits remain open.


[Native Batch setup save/load](NATIVE_WORKFLOW_BATCH_SETUP.md) now persists common
and input-specific intent through the same portable model. Loading requires fresh
input review and publication consent; scientific raster IO and broader parity remain.

## Color-stack workflow intent (p06zu)

`color_stack` is a native workflow opcode, outside the image-operation registry.
It requires a complete `resolve_color_stack_command(...).record()` in `original`
and explicit unit-stride T/C/Z ranges covering the entire current stage, with
WholePlane ROI, Planes execution and InPlace destination. `filter_options` and
`use_destination_calibration` are unavailable. The historical original destination
does not publish an intermediate document; the workflow publishes once.

`parameters={}` reuses captured request intent and T/C/Z pose; that pose must fit
the current stage. Alternatively provide all seven fields, with no extras:

| Field | Values |
| --- | --- |
| `action` | `split`, `merge`, `flatten_rgb` |
| `space` | `rgb`, `hsb8`, `hsb32` |
| `layout` | `preserve_axes`, `imagej_legacy` |
| `invert_rgb` | Boolean; RGB merge only |
| `allow_calibration_reset` | Boolean; explicit full spatial/time reset consent |
| `allow_nonxy_annotation_drop` | Boolean; explicit XZ/YZ removal consent |
| `max_working_bytes` | Positive integer byte cap, further bounded by workflow limits |

An authored map resolves against current stage metadata and view. The complete
original is still independently validated and remains immutable history. Unsupported
dtype/layout, incomplete extents and missing loss consent are rejected during
metadata-only preflight before earlier steps read image planes. Generic filters'
output dtype and partial/empty selection promotion participate in that preflight.
V1/V2/V3 use their existing parameter grammar; no new wire version or DTO field is
needed. Disabled steps remain structurally validated.

Both hosts' Run parameter form offers named choices, boolean loss controls and an
integer byte cap. Keep workflow parameters validates current-stage intent without
publishing. Confirm, Prepare edited program and Keep edited program in Workflow
persist it; Apply alone leaves the stored program unchanged. Session Batch and
per-input authoring use the same model and form. Standalone Image > Type menu, preview and recording are available; see the p06zv update below.

Evidence: [scoped review](parity/reviews/color-stack-workflow-review-001.json).

Standalone component review, preview and recording are now available in both hosts.
See [desktop usage](DESKTOP.md#standalone-color-components-and-stack-layout-p06zv).

Scalar RGB flattening now retains V2 rendering capture through the existing program grammar.
See [desktop semantics](DESKTOP.md#scalar-rendering-and-rgb-stack-flattening-p06zw).
