# ImageJ Despeckle preset

Despeckle uses the existing C++ ImageJ median profile with a fixed radius of one
pixel: the nine samples of a 3 × 3 neighborhood, including the center. It
preserves UInt8, UInt16 and finite Float32 scalar storage, original-source halo
reads, selections and existing rank job limits. Float64, RGB and nonfinite
sources remain outside this finite scalar profile. Remove NaNs is separate
remaining work.

In both Dear ImGui hosts, choose **Process → Noise → Despeckle**. A single
scalar XY image applies directly; a Z stack opens **Process Stack?** with
Yes/No/Cancel. There is no radius or method dialog. Scientific datasets use the
shared C/Z/T and ROI target chooser, preview and optional new-document output;
this target chooser is an explicit editor extension. Its median radius stays
fixed at one. Applying or cancelling the preset does not change the regular
Median dialog's accepted radius memory.

The public C++ entry points are `despeckle_imagej(image, extent)` and
`resolve_despeckle_command(snapshot, request)`. Python exposes the same names;
the optional Python editor adds `editor.apply_despeckle(request, wait=True)`.
All image processing stays in C++. The pixel API's optional extent is a
half-open rectangle, and samples outside it remain available as halo input.

The preset records the existing `rank_filter_imagej` command with
`method="median"` and `radius=1`. Generic operation dispatch, strict replay,
explicit target rebinding, workflows, cancellation and staged new-document
publication continue through that command. `despeckle_imagej` is a named pixel
API and desktop action, rather than a separate generic operation record ID.
No existing enum values, options layouts, record schema or runtime dependency
change.

Pixels share the existing guarded transaction and Undo/Redo history. Display
bounds, LUTs and manual thresholds are retained; Despeckle does not apply the
variance display stretch. Empty native write masks remain read-free and no-op
results create no pixel-history entry. Indexed metadata follows the existing
qualified rank-filter policy; no indexed file-format parity is added here.

The reference corpus contains 72 actual pinned ImageJ 1.54r Despeckle runner
observations: three scalar types, four signals, current/stack targets and
whole/rectangular/polygon selections. Separate C++ panel and actual ImGui input
checks cover immediate application, stack choices, cancellation, scientific
preview, workflow recording and Undo/Redo in both hosts. This closes a finite
scalar command/preset gap; it does not close the complete ImageJ/Fiji filter
family or the broader release gates.
