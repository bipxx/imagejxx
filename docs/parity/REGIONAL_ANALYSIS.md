# Regional analysis and contrast

Canonical `Selection` regions retain their intrinsic packed extent and signed
XY origin. Analysis and pixel mapping use only members intersecting the current
image canvas. They preserve the original selection, Previous and saved ROI
geometry. Off-image cells do not contribute to counts or Auto decisions.

Dataset and target histograms accept active regional selections and matching
positioned regional ROIs. Estimates charge retained packed storage separately
from the full consumption canvas and source plane. Histogram commands remain
version 1: they capture target intent, revisions and positions without serializing
selection geometry. Revision guards continue to detect geometry changes.

Scalar Auto uses version 2 when its captured selection is regional. Its closed
regional DTO includes nested attributes, intrinsic shape and signed origin.
Validation checks canonical storage, visible membership against captured counts,
and the complete record's transport depth. Whole-plane Auto retains version 1
when no regional geometry is captured. Ranges, cycle behavior, fallback and
display effects retain the existing equations. Replay uses the captured effect;
explicit rebind computes a new decision against the destination.

Scalar and RGB Apply use schema version 2 when their active selection or
positioned ROI capture includes regional geometry. Source guards, reset policy,
component mapping and arithmetic profiles retain their existing semantics.
Only selected visible pixels are mapped; the resulting transaction retains the
full frame and provides one undoable publication.

RGB stack and session records also use version 2 for regional captures. A session
may have historical regional stages while its live selection is absent or legacy;
the outer record is version 2 while the nested live capture may remain version 1.
Version 1 envelopes reject regional geometry and nested version 2 captures.
Nested stage metadata is admitted at its depth within the complete envelope,
including typed string arrays. Estimates precede provider reads and callbacks.

The shared ImGui contrast plot retains its whole-plane policy. Auto's area policy
and Apply's target policy are independent. The C++ processing path serves the
standalone application and optional Python UI. Actual input tests exercise Auto
and Apply in the shared panel; this does not establish platform certification.

Qualification is recorded in the [scoped receipt](reviews/p12m-region-analysis001.json).
Boolean/rotation, saved ROI geometry commands, axis remapping, ImageJ ROI
interchange and remaining editor/platform requirements are still open.
