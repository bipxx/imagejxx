# Portable native workflow programs

## Manual threshold programs (V9)

A saved manual-threshold V1/V2/V3 command can now run as a typed workflow step.
Pass its unchanged record as `original`; an empty parameter map uses the
captured inclusive bounds. To author new bounds, supply exactly numeric `lower`
and `upper`. They must be finite, ordered and losslessly represented. Review
the destination target explicitly: Run uses the destination selection, not the
selection or document identity in the historical record.

```python
import imagejxx as ij

captured = ij.resolve_manual_threshold_command(
    document.snapshot(), ij.ManualThresholdRange(40, 120)).record()
step = ij.NativeWorkflowOperation("threshold", {"lower": 50, "upper": 90},
    ij.TargetRequest(ij.TargetScope.ALL_Z, roi=ij.RoiPolicy.ACTIVE_SELECTION),
    original=captured)
program = ij.NativeWorkflowProgram([step])
```

Typed manual intent selects closed Program V9, with the V8 operation field set;
disabled steps count toward that choice and still undergo structural validation.
V1–V8 reject typed manual steps. Ordinary registry `threshold` steps retain their
existing behavior. Direct strict Replay still requires its original source;
Run explicitly rebinds. Eager finite scalar U8/U16/F32/F64 sources are supported.
Metadata preflight rejects unsupported lazy or partial-promotion prefixes before
processing reads. Proven full eager-producing conversion/filter prefixes can be
used. Pixel-only workflow publication preserves live display state and uses one
atomic history entry. See [MW1's current qualification scope](packets/MW1-manual-threshold-workflow.md).

Scalar `contrast_apply` accepts a complete saved command with empty parameters,
or exactly `minimum`, `maximum` and integer `unsigned16_bits` authored parameters.
The shared parameter editor exposes those three fields and preserves any original
capture. Its target uses the existing in-place plane/ROI policies; later steps see
the actual reset presentation. See [Apply workflows and Batch](CONTRAST_APPLY.md).


`NativeWorkflowProgram` is an immutable, owner-independent sequence of typed native
workflow steps. The C++ core encodes and validates intent without reading dataset
pixels, opening files, executing kernels or committing edits. Original captured
records remain historical evidence; current choices and targets resolve against
the destination supplied to the planner.

## Use from Python

```python
import imagejxx as ij
step = ij.NativeWorkflowOperation(
    "add", {"value": 5},
    ij.TargetRequest(ij.TargetScope.ALL_Z, roi=ij.RoiPolicy.WHOLE_PLANE))
program = ij.NativeWorkflowProgram([step])
loaded = ij.NativeWorkflowProgram.from_json(program.json())
prepared = ij.prepare_native_workflow(document.snapshot(), loaded.steps)
# Inspect prepared.preview and prepared.outputs before a separately authorized commit.
```

`record()` and `from_record()` provide the corresponding metadata dictionary
interface. `NativeWorkflowProgramLimits(max_steps=64, max_record_bytes=...)`
controls admission. Returned steps retain their immutable parent instead of copying
large payloads. Native construction/encoding/decoding release the GIL; conversion
of Python values retains Python ownership and checks bounds before native copies.

For C++, include `<imagejxx/native_workflow_program.hpp>`. Use
`make_native_workflow_program(steps)`, `native_workflow_program_from_json(text)` or
`native_workflow_program_from_record(record)`, then pass `program.steps()` to
`prepare_native_workflow` or `prepare_native_workflow_batch`. Accessors are
`record()`, `json()`, `steps()` and `limits()`. The installed SDK exports this API.

## Desktop persistence

In either Dear ImGui host, confirm every step in **Run on image**, choose
**Prepare edited program**, then **Keep edited program in Workflow**. This changes
the workflow table without changing image pixels. **Save Workflow** writes the
portable root; **Load Workflow** restores explicit targets, private native payload
edits and export choices. Invalid loads preserve the existing table. Preparing a
program needs no output paths or publication consent. Loading never restores file
write consent, and Run and session Batch require fresh review.

Both legacy `imagejxx-workflow` version 1 files and portable files are accepted by
the shared desktop. The legacy Python `imagejxx.ui.workflows` helpers remain the
operation-only V1 interface; use `NativeWorkflowProgram` for portable native intent.

## Closed version 1, 2 and 3 records

The root contains exactly `schema`, `version`, `steps`, with schema
`imagejxx.native-workflow-program` and version `1`, `2` or `3`. Unknown fields are rejected.
All-enabled programs retain V1/V2. V3 persists any disabled row with a required
boolean `enabled` on every step and nullable `filter_options` on operations. See
[persistent order/enabled semantics](NATIVE_WORKFLOW_ORDER.md).
V1 remains canonical when no step has authored typed-filter options. V2 adds the
required nullable `filter_options` field to each operation step; import/export
grammar is unchanged. See [parameter intent and V2 grammar](NATIVE_WORKFLOW_PARAMETERS.md).
Every step includes `kind` and `original` (null or a validated capture).

| Kind | Additional fields |
| --- | --- |
| `operation` | `operation`, `parameters`, `target`, `use_destination_calibration` |
| `import` | `mode` (`append` or `replace`), `limits`, `inputs` |
| `export` | `name`, `choices` |

Parameters use explicit `{kind, value}` tags: `null`, `bool`, `int64`, `uint64`,
`real`, `text`, `array`, `image`. Array values preserve shape and finite numeric
values; images preserve positive width, height, channels, sample type and values.
Integer signedness, including full uint64 metadata, survives round trips.

Targets contain exactly `scope`, `roi`, `mode`, `destination`, `t`, `c`, `z`,
`positions`. Destination is `in_place`. Axis ranges are null or
`{start, stop, step}`; stop may be null. Positions are ordered `[t,c,z]` triples.
The public target enums define numeric values. Unused ranges/positions, invalid
combinations and duplicate positions are refused. Omitted ranges retain their
meaning after loading; changing scope clears obsolete range/position choices.

Import inputs reuse the existing V3/V4 source archive and mapping/replacement
codecs. Export choices contain exactly `limits`, `packaging` (`roi` or `zip`),
`compress`, `members`. A member contains `name`, `source_kind` (`archive` or
`generated`), `source_index`, `annotation_kind`, `annotation_index`,
`projection_kind`, `projection`, `canvas`, `options`. Projection is canonical
base64 ImageJ wire data. Geometric mask projections carry `[width,height]` canvas;
other projections carry null. The native decoder validates and reencodes these
choices before accepting them. Existing typed codecs define nested limits and
projection options; callers should construct typed steps instead of hand-writing
wire records.

Original mixed and legacy geometric import/export captures remain intact and are
validated before kernels. Registry captures are audit history: current explicit
parameters and targets execute. Bound Gaussian/rank captures retain their original
parameter/rebinding contract when no current typed options are supplied. Separate
V2 raw filter options can author current intent while fully validating the original;
Gaussian calibration consent remains required. Changing an upstream annotation
requires renewed downstream export projection review.

## Bounds and remaining scope

Defaults are 64 steps and 128 MiB logical record/retained typed requests, with a
separate UTF-8 transport bound. Hard ceilings are 4096 steps and 1 GiB. Desktop
admission uses the smaller of one quarter of its processing setting and 1 GiB.
JSON admission rejects duplicate keys, invalid UTF-8, depth over 128 and more than
four million nodes. Escaped transport is accounted before JSON materialization.
Decoded steps are charged incrementally, including expanded image/mask projections,
before decoding later steps. Per-step codecs also enforce their own phase limits.
These are logical copy and phase admission limits, not whole-process memory quotas.

Programs contain flat export names, not output paths or destination document
identities. A historical original can contain its original source identity. The
model provides neither filesystem publication nor automatic execution of embedded
session workflows. Existing per-file publication and guarded document commit
contracts still apply.

The [private prepared viewport](NATIVE_WORKFLOW_VIEWPORT.md) is available in both
hosts. [Parameter authoring](NATIVE_WORKFLOW_PARAMETERS.md) and persistent
[reorder/disable](NATIVE_WORKFLOW_ORDER.md) are also available. [Per-input Batch authoring](NATIVE_WORKFLOW_INPUT.md) is now available. No whole ImageJ/Fiji
feature family is declared complete by this increment.


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
