# Indexed color quantization

The C++ core and Python interface provide a deterministic ImageJ 1.54r compatible
processor/global-stack quantizer. Typed dataset publication and palette metadata are now available; see [dataset API](INDEXED_DATASET.md). Workflow/menu/format integration remains open.

```python
import imagejxx as ij

# uint8 RGB (H,W,3) or matching planes (N,H,W,3)
result = ij.quantize_indexed_color(rgb, max_colors=64)
indices = result["indices"]       # uint8 (H,W) or (N,H,W)
palette = result["palette"]       # uint8 (256,3)
actual_colors = result["color_count"]
quantized_rgb = palette[indices]
```

`max_colors` is an integer from2 through256; invalid values are rejected instead
of silently clamped. The actual count can be1. Entries after that count are zero;
black is also a valid color within the used portion. Quantization is lossy and
five-bit histogram bins can merge distinct original RGB values. Palette expansion
recovers quantized colors, not necessarily the source. The same palette applies
to every plane; input plane order and geometry are retained. No alpha or dithering
mode is implied. Use the existing RGB conversion first for other input formats.

C++ includes `<imagejxx/indexed_color.hpp>` and calls
`quantize_indexed_color(const Stack&, int, const ExecutionControl&, size_t)`.
`IndexedColorResult` owns `indices`, `palette` and `color_count`. Expanding an index
plane uses `convert_rgb(result.indices[i], {}, result.palette)`. There are no
public JSON or desktop dependencies in the installed core SDK.

`indexed_color_working_bytes(width,height,planes)` admits matching positive
geometry with at most INT32_MAX total pixels. The estimate is4 MiB plus64 bytes
per input pixel and1024 bytes per plane. It covers native double-backed input and
snapshot, output indices and bounded histogram/refinement storage, with allowance
for Python array transport. It is not an allocator/process RSS quota and does not
bound user callback allocations. `max_working_bytes` defaults to512 MiB. Admission
and pre-cancellation precede copying/callbacks. Input/control snapshots precede
kernel checkpoints, so callbacks cannot change the in-flight quantization by
mutating caller buffers. Checkpoints, monotone stage progress, cancellation and
callback exceptions use the existing ExecutionControl/Python interfaces. No owner
or file is published by this API.

The algorithm uses32x32x32 histogram bins, weighted original-color centroids,
deterministic cube selection and dimension ties, and up to five inverse-map
refinements. It intentionally preserves the reference's integer offset division
in nearest-color refinement and its unused-color replacement ordering. One global
histogram replaces the reference stack montage. Separate strict floating-point
compilation prevents contraction/fast-math drift. The100 pinned reference cases
compare both complete used palette rows and exact index bytes. Source/installed
Python checks and Windows/GCC native suites also test resource/callback contracts.

The pinned ImageJ core [identifies its public-domain status](https://imagej.net/ij/disclaimer.html).
The reference derives from commit62db90a269cccc1ac79f658e4602043214ed055b;
its source/class archive pins and member hashes are retained under
`build/p06zx-indexed-core001/reference-policy001.json`. The C++ algorithm is
independently expressed. Java sources, classes and JDK stay outside product
packages. The ImageJ source documents Heckbert median cut and its subsequent
bin-centroid/refinement changes. No runtime dependency was introduced.

## Current integration status

Typed dataset publication, palette metadata, guarded replay, paired history, RGB
reconstruction and native session transport are now verified. The remaining work
is [current-stage palette expansion, native workflows/Batch, shared review and indexed formats](INDEXED_DATASET.md#remaining-integration). This does not complete editor parity.
