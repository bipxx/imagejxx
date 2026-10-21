# ImageJ variance final-range policy

The private C++ policy in `src/native/rank_display_internal.hpp` computes the
final display range for finite UInt16/Float32 variance output. It is detached
from document publication. Nonbyte editor, workflow and UI admission remains
guarded; the direct pixel APIs qualified previously remain available.

The pinned fixture `tests/fixtures/variance-display-imagej154r.json` contains
288 observations through ImageJ 1.54r's actual `PlugInFilterRunner`, using macro
options with the original `RankFilters` dialog/runner path. It covers current
slice and complete stack processing, whole-plane/rectangle/polygon selections,
default/custom ranges, low/high/constant/outlier signals and radii 0/1.5/2.85.
The active slice is the second of three 25-by-20 planes. Raw input/output bytes,
initial/final ranges, active slice and retained ROI are captured independently.
The Java runtime and source archive are pinned private development inputs.

Observed behavior:

- Final statistics cover the whole active processor after masking restores
  pixels outside the write ROI. The temporary ImagePlus in ContrastEnhancer's
  processor overload does not retain the original ImagePlus ROI.
- UInt16 uses all 65,536 exact integer bins. Float32 uses 256 bins over observed
  extrema, with double bin arithmetic. FloatProcessor then narrows the final
  display endpoints to Float32.
- Each tail excludes `floor(pixelCount * 0.5 / 200)` samples. Its first accepted
  bin has cumulative count strictly greater than that threshold.
- When both cutoffs occupy the same bin, the plugin retains its previous range.
  This includes equal-ended ranges such as `[1234, 1234]`; the policy returns
  no replacement range, rather than inventing an expanded interval.

The policy reuses the existing dataset histogram service. Its metadata-only
estimate and execution share the default 16 Mi-pixel / 128 MiB working-storage
bounds, including a conservative active-plane charge. Execution captures the
source and controls by value, acquires exactly one plane, validates all samples
and polls cancellation through histogram passes and tail scanning. It never
changes a document or caller storage. This is an internal staging API, not an
installed SDK header or a Python-facing feature.

`tests/native/test_rank_display.cpp` checks every runner range bit for bit,
including unchanged-range outcomes, and verifies pre-read budget refusal,
pre-cancellation, tail cancellation/callback failure, source/control capture and
strict 399/400-sample tail boundaries. Expected ranges come from the retained
runner observations, not another implementation of the histogram algorithm.

Next integration packet:

1. Reconcile equal-ended native display ranges with the legacy renderer's
   current `maximum > minimum` rule. Preserve the exact stored range and choose
   a qualified temporary rendering scale; do not silently change stored bounds.
   Observe source-default UInt16 ranges as well as Float32 ranges, since the
   legacy UInt16 renderer currently defaults to the entire type domain.
2. Capture preview behavior and targets excluding the current plane. Define
   empty-target behavior explicitly. The 288 runner cases do not establish
   those extensions. Qualify threshold and channel-display state handling.
3. Admit the extra histogram before filtering reads, pair the resulting display
   and pixels in one `Change::All` transaction, and carry the pair through legacy
   adapters, workflows, new-document output and Undo/Redo. When the policy
   retains a range, capture its effective pre-filter value before changing data.
4. Lift the command/executor type guards together after the preceding tests
   pass. Record method/radius intent, recompute the range during replay, and
   observe actual input through source/installed C++ and Python ImGui hosts.

No public DTO layout, runtime dependency, editor admission or release status
changes in this packet. All99 remains 42 in progress / 57 open / 0 complete;
Source901 remains the last full source release. Broader filter, conversion,
indexed file-format and release gates remain open.

The subsequent [editor checkpoint](VARIANCE_EDITOR.md) enables the qualified
scalar UInt16/Float32 path, paired history, equal-range rendering and actual-input
checks in both source/installed hosts. Its remaining-work list supersedes the
integration sequence above; this page retains the earlier policy checkpoint.
