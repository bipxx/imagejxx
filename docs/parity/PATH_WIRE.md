# Native ImageJ composite path interchange

`decode_imagej_composite_roi(data, limits)` imports only ImageJ composite
ShapeRoi records as continuous native path selections. It retains absolute
move, line, quadratic, cubic and close commands, multiple subpaths, implicit
closures and even-odd fill. It needs no canvas and does not allocate a mask or
read image samples. Other ROI types refuse explicitly; use their existing
typed decoders.

`encode_imagej_roi(record, limits)` accepts a native path selection directly.
An empty `composite_path` sidecar uses the selection's commands. A nonempty
sidecar must agree with those commands; after editing native geometry, clear
the stale sidecar explicitly. Export preserves wire-supported names, positions,
properties, groups and rendering styles. Opaque native attributes still require
an explicit mapping rather than disappearing during standard export.

ImageJ wire coordinates are IEEE754 binary32. Import promotes those values to
binary64; export rounds native coordinates to binary32 without changing the
input selection. Wire byte limits, retained native path byte limits and exact
segment limits apply. Pure native path transport does not consume
`max_mask_pixels` or `max_path_work`; both may be zero. Native selection hard
caps and the finite +/-1e9 coordinate domain still apply.

`decode_imagej_composite_roi_mapped(data, mapping, limits)` applies the existing
explicit XY/XZ/YZ plane and position policies while retaining native geometry.
It does not infer a fixed plane from ambiguous legacy positions or reinterpret
scientific Z as a different fixed axis. `map_imagej_roi` and mapping validation
also accept native path records through the same typed encoder admission.

Existing `decode_imagej_roi` and `decode_imagej_roi_mapped` keep their mask-based
composite behavior and require an explicit canvas. Raster consumption remains
available through the native selection's mask operation. Existing envelope and
workflow versions retain their contracts; this packet does not add a native
path envelope version, import/export UI mode, renderer, drawing tool, filter
selection contract or continuous Boolean operation. Desktop path guards remain.

Validation includes the pinned ImageJ 1.54r composite fixtures, independently
specified wire layout and float rounding, legacy masks, XYZ positioning,
metadata/style, stale geometry, malformed/truncated payloads and resource
limits. A standalone installed C++ SDK client exercises the same public API.
Qualification details are recorded in `reviews/p12z-path-wire001.json`.
