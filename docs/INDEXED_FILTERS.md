# Indexed filter admission and palette retirement

The p07c checkpoint closes two indexed filter integration gaps: plugin eligibility
and active palette metadata surviving a transition out of the byte-index domain.
It adds no runtime dependency or existing public DTO layout change.

ImageJ's Filters plugin accepts grayscale/pseudocolor bytes but excludes genuine
indexed color for Invert, Smooth, Sharpen and Find Edges. Its RankFilters plugin
accepts indexed color. The pinned ImageJ 1.54r source and a Java capture confirm
these flags. Native document operations now enforce the four exclusions before
acquiring source samples. The current supported display palette participates in
classification, with the stored palette as fallback. Accepted operations depend
on the display revision, so a later palette/display change invalidates preparation.

Known ineligible workflow steps reject during metadata preflight, before preceding
steps read pixels. A palette produced by quantization is data dependent: its
eligibility is checked against the actual generated table during execution, with
no publication to the original owner on failure. Private deferred descriptors are
retained through byte-preserving rank steps.

Minimum, maximum, mean and median through `rank_filter_imagej` process byte indices
and retain their palette and display. The fixture
[indexed-filter-imagej154r.json](../tests/fixtures/indexed-filter-imagej154r.json)
captures 288 cases across nine palettes, one/three planes and default/limited
display ranges. Half verify rank output pixels; the remaining cases verify plugin
eligibility only. Non-rank fixture output pixels are deliberately unchanged
admission probes and do not certify those kernels' arithmetic.

Native extension filters such as `variance_filter`, variance-mode `rank_filter`,
`laplacian`, `difference_of_gaussians` and `find_edges` can produce floating data.
When indexed UInt8 data leaves the scalar byte domain, the operation now removes
the active `imagejxx.indexed_color` definition and preserves it under
`imagejxx.retired_indexed_palette`. LUT, range, channel display and active threshold
presentation are reset together. `imagejxx.indexed_filter_source` records the
operation, previous display and retirement policy. Pixel data, metadata and display
publish in one guarded edit with paired Undo/Redo. Untouched planes in partial
targets retain their numeric values under the promoted dataset dtype.

This retirement policy also participates in workflow metadata preflight, so a
subsequent numeric conversion sees the actual output domain. The legacy desktop
adapter now carries the complete retired metadata/display through publication and
recording. Both C++ and Python hosts share that adapter and processing core.
Ordinary indexed Image > Type conversion retains its separately qualified policy;
see [numeric type semantics](INDEXED_TYPE.md).

Validation comprises 304 new Python checks, 2,380 source and installed checks,
23 Windows native suites, seven fresh Linux native suites and an installed C++ SDK
consumer. Thirteen targeted checks fail against the preceding installed build and
pass against the new build. Existing raw input scenarios also pass in both source
and installed C++/Python hosts; the new legacy retirement case uses the editor API.
The evidence receipt is
`build/p07c-indexed-filter001/indexed-filter-handoff001.json`.

Remaining work is ordered as follows:

1. Add reference-qualified ImageJ rank variance and its final display-range policy.
   The existing generic floating variance operation is a native extension and is
   not equivalent to ImageJ's byte-preserving rank variance command.
2. Audit other plugin eligibility, dtype and palette policies, plus masks, advanced
   rank methods and remaining filters. This packet does not certify all filters.
3. Qualify active-threshold and channel-display conversion semantics.
4. Implement and independently read real indexed TIFF/PNG files.
5. Continue broader editor, resource, platform and release obligations, including
   historical histogram source-fingerprint requalification.

All99 remains 42 in progress, 57 open and zero complete. Source901 remains the last
full source release. No feature family is declared complete by this checkpoint.

## Byte rank variance (p07d)

The ImageJ rank profile now supports UInt8 variance with palette/range retention
and shared ImGui authoring. See [byte variance scope](BYTE_VARIANCE.md). Non-byte
variance, final contrast stretch and the other remaining filter packets stay open.
Earlier open-work statements above describe their original checkpoint.
