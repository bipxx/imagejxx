# Native Batch setup save and load

Both Dear ImGui hosts provide **Save setup...** and **Load setup...** inside the
native-session Batch review. A setup contains the common portable program,
per-input program/selector overrides, inherited selectors, input paths, output
stems and output folder. It preserves typed parameters, original captures,
targets, order and disabled rows using the existing V1/V2/V3 program codecs.

Save uses an exclusive temporary sibling and publishes once after source,
settings and cancellation guards pass. The destination parent must exist. Existing
files are preserved unless **Replace** is explicitly accepted in the existing file
browser's replacement dialog. That consent applies to that save and is never stored
in the setup.

Load reads and validates a complete candidate on a worker before replacing the
processing draft. Malformed or canceled work cannot install a partial candidate.
Source/settings changes close the stale review. Load clears step confirmations,
Batch confirmation, processing publication consent, prepared results and private
views. It restores neither input-review fingerprints nor prepared outputs.
Program overrides must be reviewed on their current selected input again before
GUI preparation. Inherited common intent remains visibly inherited.

Neither Save nor Load opens input images, runs workflow steps, creates directories,
changes input files or edits the live image, settings, results or common Workflow
table. Loaded common intent belongs to the Batch draft. Processing publication
still requires full preparation and explicit consent. Embedded input session
workflow tables stay unexecuted.

## Native and Python API

The immutable `NativeWorkflowBatchSetup` exposes `common_program`, `inputs`,
`selection`, `output_directory`, `limits`, `record()` and `json()`. Input DTOs
returned by the setup have empty fingerprints. A null input program or selector
means inheritance; all-disabled programs retain explicit override intent. Empty
input lists are allowed for unfinished setup drafts. Programs remain nonempty.

```python
setup = imagejxx.NativeWorkflowBatchSetup(
    common_program, inputs, output_directory,
    imagejxx.NativeWorkflowBatchSelection(document_index=0))
imagejxx.save_native_workflow_batch_setup("batch.json", setup)
loaded = imagejxx.load_native_workflow_batch_setup("batch.json")
```

C++ provides `make_native_workflow_batch_setup`, record/JSON decoders and matching
save/load functions in `imagejxx/native_workflow_batch_setup.hpp`. Python exposes
`NativeWorkflowBatchSetup.from_record/from_json` and both file functions. File IO,
construction and decoding accept cancellation tokens; native controls also support
checkpoints. Work runs with the Python GIL released after bounded Python admission.
API save defaults to new-file-only; replacement requires `replace_existing=True`.

Native callers retain the existing option to prepare explicitly unreviewed Batch
inputs without fingerprints. Loading a setup never manufactures review evidence:
callers that require guarded processing must review current bytes and attach the
returned fingerprint before preparation. The GUI enforces fresh review for loaded
program overrides.

## Closed format and limits

The root has exactly `schema`, `version`, `common_program`, `selection`,
`output_directory` and `inputs`. Schema is `imagejxx.native-workflow-batch-setup`,
version is 1. Each row has exactly `path`, `output_stem`, nullable `program` and
nullable `selection`. Selectors contain a strict bool `all_documents` and a
nonnegative integer `document_index`; all-document selectors require index zero.
Fingerprints, write consent, prepared plans and arbitrary additional fields are
rejected. Input/output locations are external choices outside program intent.

`NativeWorkflowBatchSetupLimits` defaults to 64 inputs, 64 steps per program,
4,096 total retained steps and 128 MiB of combined wire/retained intent admission.
Desktop setup admission uses at most one quarter of the processing memory setting.
The complete wire graph and row counts are checked before program decoding; each
program then uses the remaining aggregate budget and existing incremental expanded
payload admission. Repeated serialized program records count separately. JSON
transport, escaping, duplicate keys, UTF-8, 128-level depth and four-million-node
limits are enforced before metadata materialization. These are logical copy and
phase limits, not whole-process allocation quotas.

Paths are bounded UTF-8 text without NUL; input paths and flat output stems are
nonempty. Locations are stored verbatim. Loading neither resolves paths nor changes
the working directory; relative locations retain the existing Batch interpretation
relative to the process working directory. Actual filesystem existence, aliases and
processing compatibility are validated during explicit review/preparation.

Scientific raster IO/axis policies, remap/branch authoring, broader IO stress and
matching release qualification remain separate work. No third-party dependency or
commercial eligibility expansion is introduced by this format.
