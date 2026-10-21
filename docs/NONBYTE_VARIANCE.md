# Nonbyte ImageJ variance: pixel kernel and remaining editor work

The direct C++ `rank_filter_imagej` and Python `rank_filter_imagej` APIs retain
UInt16 and Float32 input types for variance. Integer variance saturates before
ImageJ rounding. Float32 follows ImageJ's double accumulation and float result,
without an upper clamp. Results that overflow to infinity raise an error before
returning an image; caller storage stays unchanged. This finite-output policy is
an intentional restriction relative to ImageJ. The existing generic
`variance_filter` continues to return Float64.

Editor commands, recorded commands, workflow execution and both ImGui hosts still
reject nonbyte variance before sample reads. They need ImageJ's final display
stretch paired with the pixel edit. This checkpoint does not enable that path.
The byte editor behavior and its previously qualified UI remain unchanged.

The fixture `tests/fixtures/nonbyte-variance-imagej154r.json` retains 504 raw
observations from the pinned ImageJ 1.54r runtime: two types, six signals, three
geometries, seven radii and two write extents. Coverage includes low contrast,
integer saturation, large float offsets, float subnormals and finite inputs that
produce infinity. C++ and Python compare output bits, or require a finite-output
error for the 42 overflow cases. Final display bits are retained as research
observations only. They do not qualify editor ROI, stack or display behavior.
Upstream Java/JDK files remain private development inputs; no dependency was
added to the product.

Next packet, in dependency order:

1. Capture final-processing behavior through ImageJ's actual plugin runner:
   single/current/stack targets, current plane outside the processed target,
   custom initial ranges, constant images, rectangular and irregular masks.
   Distinguish the processor overload's temporary ImagePlus from passing the
   original ImagePlus; do not infer final histogram ROI from the write extent.
2. Qualify a private final-range computation against those observations.
   UInt16 uses the full 65,536-bin histogram. Float32 uses 256 bins and ImageJ's
   exact extrema, reciprocal and bin-edge arithmetic. Each tail excludes
   `floor(pixelCount * 0.5 / 200)` samples with strict cumulative `>` comparison.
   When upper and lower bin indices coincide, retain the original range.
3. Budget histogram storage and any extra active-plane acquisition before reads.
   Poll cancellation during histogram passes. Keep overflow, provider failure,
   cancellation and admission failure atomic, with no history publication.
4. Pair final display and data in the prepared edit, with `Change::All` guards.
   Cover previews, legacy adapters, new-document output, Undo/Redo, camera and
   T/C/Z retention. Resolve workflow display state from computed stage results;
   do not put a guessed range into metadata-only preflight.
5. Lift the existing command and executor type guards together only after the
   preceding evidence passes. Keep Float64/RGB/unsupported sample policies
   explicit. Records store method/radius intent; replay recomputes final range.
6. Observe actual UInt16/Float32 preview, Apply, recording and Undo/Redo through
   both ImGui hosts, including installed products. Run native Windows/Linux,
   source/installed Python and installed C++ SDK checks, then archive verified
   owned scratch directories and retain failure evidence.

This closes only the direct nonbyte pixel-kernel subgap. All99 family counts and
Source901 release status remain unchanged. Advanced filters, threshold/channel
conversion, indexed file formats and the wider release gates remain open.

The subsequent [final-range policy checkpoint](VARIANCE_FINAL_RANGE.md) qualifies
the private range computation against 288 actual plugin-runner captures. The
remaining integration order there supersedes the original next-packet list;
editor/workflow/UI type guards remain in force.
