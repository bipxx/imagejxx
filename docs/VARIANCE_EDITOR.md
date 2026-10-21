# Nonbyte ImageJ variance in the editor

Scalar finite UInt16 and Float32 variance now uses the C++ editor transaction
path, with the existing Python interface and shared Dear ImGui hosts. It retains
the input pixel type. UInt16 saturates before rounding; a Float32 result that
overflows to infinity aborts preparation. The generic `variance_filter` remains
an independent Float64 operation.

The accepted source may have a valid manual threshold and recognized V1/V2
channel-display state. Unknown/future channel-display schemas are refused before
source reads. Float64, multicomponent and nonfinite
rank sources remain outside the finite scalar profile. Byte/indexed variance
keeps its earlier palette and display policy.

For an accepted nonbyte command, metadata-only admission checks the active
plane's final histogram before filtering acquires samples. The existing
histogram service's 16 Mi-pixel / 128 MiB working bound applies. Scheduled jobs
also reserve histogram counts, edges and control storage; the existing generic
plane reservation covers the captured plane. These are conservative operation
bounds, not a process RSS limit or a charge for opaque provider caches.

The effective initial display range is retained before the first actual filter.
UInt16 initial endpoints use processor integer rounding, Float32 endpoints narrow
to Float32, and requested `[0, 0]` resets to source extrema. Undo restores the
original requested properties. Bounds that would require UInt16 signed-integer
wraparound or nonfinite Float32 endpoints are refused before source reads.
Final processing then uses the whole active output plane, after masked pixels
have been restored. This is independent of the filter write ROI and may use an
untargeted active plane. UInt16 has 65,536 bins; Float32 has 256. Each tail uses
the ImageJ 0.25% strict cumulative cutoff, and Float32 final endpoints narrow to
Float32. If both cutoffs coincide, the effective initial range is retained.

Resulting equal-ended ranges are stored exactly. The temporary scalar display
uses the typed processor's step mapping, including UInt16 endpoint rounding;
neither source samples nor stored bounds are expanded to make rendering work.
LUTs, unrelated display properties, camera and T/C/Z position are retained.

With canonical channel display, a successful stretch updates only the active
channel's range. Other channels retain their ranges even if their pixels were
also filtered. LUT tables, visibility, mode, wire version and Sum/Max/Min
projection are preserved. Canonical state wins over legacy flat bounds; the
legacy `display_range` and LUT fields remain exact. Collapsed cutoffs retain the
original requested channel bounds, including fractional/equal endpoints and
literal `[0,0]`. Channel zero/zero never requests an implicit range reset.
Active channel bounds requiring integer wraparound or nonfinite Float32
endpoints remain outside this qualified profile.

Forty-eight additional pinned runner cases qualify pixels, final active
processor bounds and RGB in composite/color/grayscale modes, current-plane and
whole-stack targets, variable and constant images, with hidden StackWindows and
windowless calls. Stored LUT bounds and post-navigation processor bounds are
observed separately. ImageJ can leave the stored LUT bounds stale, and color or
grayscale channel switching can restore them. This editor deliberately stores
the final visible processor range in canonical state so that channel switching,
sessions, workflows and Undo/Redo retain a coherent result. It does not claim
that ImageJ's stale stored LUT arrays are updated by the upstream plugin.

Threshold bounds do not limit filtering or histogram samples. When the two
final histogram cutoffs differ, the typed display setter also clears the active
plane's threshold, even if the resulting display bounds equal the previous
bounds. When both cutoffs fall in the same bin, the display setter is skipped
and the threshold remains. This conditional behavior matches 72 additional
pinned ImageJ plugin-runner observations across both types, current/stack
targets, whole/rectangular/polygon selections and two threshold LUT modes.
The editor's threshold overlay remains its existing red tint; the reference
LUT modes qualify processor state and pixels, not new overlay styles.

A threshold anchored to a different T/C/Z plane is retained, an explicit editor
extension of the single-processor reference behavior. Empty write masks remain
read-free and retain their threshold. Cancellation, overflow and rejected
publication do not change the owner's threshold. Threshold reset shares the
pixel/display history pair, including workflow and new-document output.

Histogram work reports its own stages and cancellation checkpoints without
resetting stack plane-progress counters.

Pixels and the resulting range share one `Change::All` guarded transaction.
Undo/Redo restores both, including when only the paired display changes. A
repeat with identical pixels and display creates no extra history entry. Normal
transient display changes keep their existing history behavior. New-document
output, native workflows and the legacy adapter carry the same pair. Records
retain method/radius and target intent; replay recomputes the range.

Owned editor extensions are explicit: an empty write mask stays read-free and
does not adjust the range; a target excluding the active plane computes final
presentation from that untouched plane. Preview shows the prepared Apply result,
including the restored mask and final range. It does not claim ImageJ's internal
preview-before-mask-restoration timing.

Evidence includes the 288 pinned actual ImageJ plugin-runner cases, 16 typed initial-range/reset cases, eight independent
typed equal-range BufferedImage readbacks, native histogram/control regressions
and Python publication tests. Actual input cases R15/R16 exercise UInt16/Float32
spikes; R17/R18 exercise constant sources and equal ranges. Both source/installed
hosts must pass preview, Apply, recording and Undo/Redo checks before this scoped
checkpoint is accepted. Initial-range rounding/reset cases are retained
separately from the earlier final-histogram fixture.

Source/installed C++ and Python ImGui cases R23–R26 exercise channel range
publication and threshold reset/retention with V1/Sum and V2/Max/Min states.
Single-channel legacy adapters have separate publication and read-free empty
selection checks. A standard virtual environment runs the existing strict
installed-origin projection suite without changing its assertions.

Remaining work includes advanced rank/filter features, closer
upstream intermediate-preview behavior if required, indexed
file formats and wider editor/release qualification. No new runtime dependency,
Qt component or public DTO layout is introduced. All99 and Source901 release
counts are unchanged by this scoped packet.
