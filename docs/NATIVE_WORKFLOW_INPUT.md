# Per-input native Batch programs

Both Dear ImGui hosts offer **Review input...** and explicit document-selection
overrides on each native-session Batch row. The default inherits the common
program and document selector. **Restore inheritance** clears that row's program,
selector and review fingerprint. An all-disabled program override is distinct from
inheritance: it processes no steps and produces a semantic session copy. Portable
programs retain their existing requirement for at least one step.

Review loads a detached snapshot of the selected input document. When execution
selects all documents, the separately labelled review index chooses only the
representative document shown in the editor. Every execution-selected document
must support the effective program during complete Batch preparation.

The input editor reuses parameter, target, import/export-choice, order/enabled,
private preview and portable-program controls. It cannot prepare filesystem
outputs or apply changes to the input or live image. **Prepare edited program**
followed by **Keep input program** asynchronously verifies the reviewed file and
returns immutable intent to that row. The common Workflow table stays unchanged.
Confirm Batch choices and publication consent again before complete preparation.

Editing the inherited document selector clears dependent review fingerprints.
Explicit program overrides survive common-program edits. An override whose
fingerprint has been cleared requires review again before GUI execution. Closing,
cancellation, source/settings changes and downstream edits invalidate private work;
resource reservations remain attached to retiring workers and review snapshots.

## C++ and Python

`NativeWorkflowBatchInput` appends `program`, `selection` and
`expected_fingerprint`. Existing two-field callers retain common-program behavior.
Programs are immutable and share their retained typed step storage. A selector
contains `all_documents` or a zero-based `document_index`; all-document overrides
must carry index zero. Rebuild headers, core, bindings and consumers together after
this DTO layout change.

```python
review = imagejxx.review_native_workflow_batch_input("input.ijx")
row = imagejxx.NativeWorkflowBatchInput(
    review.path, "processed", program=edited_program,
    selection=imagejxx.NativeWorkflowBatchSelection(document_index=1),
    expected_fingerprint=review.fingerprint)
prepared = imagejxx.prepare_native_workflow_batch(
    [row], common_program.steps, "existing-output-directory")
prepared.publish()
```

Review returns immutable document snapshots, the canonical input path, estimated
working bytes and a lowercase SHA-256 fingerprint of the exact decoded session
bytes. It neither runs embedded workflows nor publishes files. `review.verify()`
checks the current file against those bytes. Python review and verification release
the GIL and accept cancellation tokens.

Native callers can omit a fingerprint to retain unreviewed behavior. For guarded
inputs, preparation compares the read bytes before decoding, verifies them again
before staging, and publication verifies every guarded input before publishing its
first output. A publication guard failure deletes owned staging files, publishes
nothing, marks the offending input Failed and others Skipped, and forbids retry.
These are boundary checks, not filesystem locks against concurrent writers.

Distinct override storage is admitted under one quarter of the Batch working-byte
limit before input loading; repeated shallow copies are charged once. Programs
reuse the existing closed V1/V2/V3 grammar and preserve original captures, typed
filter options and disabled rows. Existing codec/planner bounds and execution
checks still apply. Limits describe retained intent and processing phases, not a
whole-process allocation quota.

Native `.ijx` Batch preserves unselected document samples, axes, metadata, results,
workflow tables and extensions. Embedded workflow tables remain unexecuted.
Existing output alias checks, exclusive staging, new-file-only publication and
truthful per-file partial failures also apply to differing input programs.

## Remaining scope

[Batch setup save/load](NATIVE_WORKFLOW_BATCH_SETUP.md) now retains input intent and
requires fresh review after loading. Remap/branch authoring, scientific raster import/export
and axis policy, broader IO stress, typography and release qualification remain
separate work. This increment does not declare ImageJ/Fiji family completion or
extend the commercially eligible dependency set.
