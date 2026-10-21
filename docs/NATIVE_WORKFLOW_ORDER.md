# Persistent workflow order and enabled state

Both Dear ImGui hosts provide **Move step up**, **Move step down** and **Step
enabled** in the shared native workflow review. Controls are disabled while a
worker or nested editor is active. Moving a row preserves its complete current
intent, original capture and export token. Disabling retains the row with a visible
`[disabled]` label; it does not delete the row or its payload.

A structure change discards prepared results, the private viewer/prefix, browse
routes, all confirmations, output paths and publication consent. Review every
retained row again, including disabled rows. Confirmed disabled rows explicitly
acknowledge retained intent, without consenting to execute them. **Prepare edited
program**, **Keep edited program in Workflow**, then Workflow **Save** persists
order/enabled state. **Load**, private prefixes, image preview, full Run and native
session Batch use the same typed sequence.

Disabled export rows offer no output-path or Browse controls and have no file
obligation. Full preparation plans paths and stages files only for enabled exports.
An enabled export still requires a new path and publication consent. Moving an
export ahead of its required imports does not repair source indices or projections:
normal native preparation refuses the incompatible order before staging/publication.
Existing per-file partial-publication behavior remains unchanged.

## Native model and V3

Each C++ `NativeWorkflowOperation`, `NativeWorkflowImport` and
`NativeWorkflowExport` appends `bool enabled = true`. Python constructors append
strict `enabled=True`, with a read-only property on immutable DTOs:

```python
step = imagejxx.NativeWorkflowOperation(
    "add", {"value": 5}, target, enabled=False)
program = imagejxx.NativeWorkflowProgram([step])
loaded = imagejxx.NativeWorkflowProgram.from_json(program.json())
prepared = imagejxx.prepare_native_workflow(document.snapshot(), loaded.steps)
assert prepared.completed_steps == 0
assert prepared.outputs == []
```

Reorder by constructing a program from the desired typed step sequence. Program
steps and historical captures remain immutable. The appended fields change DTO
layout: rebuild headers/core/bindings/consumers together. Compatibility with older
compiled DTO layouts is not claimed.

Canonical programs with any disabled row emit closed version 3. Every V3 step has
a required boolean `enabled`. V3 operation rows also require nullable
`filter_options`, as in V2. All other root, typed parameter, target, import and
export grammar is unchanged. V1/V2 reject the new field. All-enabled V3 input
canonicalizes to V2 when authored filter options remain, otherwise V1. Ordinary
all-enabled programs keep their previous canonical version and behavior.

## Validation, execution and counts

Every disabled step still needs structurally valid retained portable intent:
operation/parameter types, target grammar, raw filter options, mappings, projection
choices, limits and the complete original capture are validated before kernels.
This includes bounded projection-wire validation for storage, as on Save. It does
not construct a resolved workflow export result. Disabled native requests use the
portable program admission grammar/record ceiling as well as planner limits.

A disabled step does not resolve destination compatibility, bind targets to the
current dataset, apply imports, run processing or generate a workflow export output.
For example, retained positive positions can be out of range on the current image,
or a disabled rank capture can be retained on RGB. Invalid schema, malformed
originals or raw choices remain errors even when disabled. Disabled rows count
against retained step and input-memory limits; skipping execution does not waive
retention admission. Cancellation checkpoints cover retained-intent validation.

`PreparedNativeWorkflow.completed_steps` counts only executed enabled steps, including
imports/exports. Output `step_index` refers to the complete retained order, including
disabled rows. Stage progress visits retained positions; it is not an executed-step
count. Desktop diagnostics distinguish `prefix_steps` (retained predecessors) from
`prefix_completed_steps` (executed predecessors).

An all-disabled Run prepares zero steps/outputs. Apply is a guarded no-op, changes
neither data nor undo history, and reports that no enabled steps ran. An all-disabled
Batch still publishes explicitly confirmed semantic session copies; it produces no
ROI export outputs and never runs embedded input workflow tables. Source files
remain byte-for-byte unchanged. Input sessions keep scientific axes, sample bits,
metadata, results/workflow tables and extensions under the existing Batch contract.

See [portable program bounds](NATIVE_WORKFLOW_PROGRAM.md),
[parameter authoring](NATIVE_WORKFLOW_PARAMETERS.md) and
[prepared viewer](NATIVE_WORKFLOW_VIEWPORT.md). These logical retention/phase bounds
and scheduler reservations are not whole-process memory quotas.

Evidence: `build/p06q-workflow-order001/workflow-order-handoff001.json`.
[Per-input Batch choice editing](NATIVE_WORKFLOW_INPUT.md) is now available.
Explicit remap/branch policies, scientific raster
IO/axes, typography/fonts and broader parity/release audits remain open.


[Native Batch setup save/load](NATIVE_WORKFLOW_BATCH_SETUP.md) now persists common
and input-specific intent through the same portable model. Loading requires fresh
input review and publication consent; scientific raster IO and broader parity remain.
