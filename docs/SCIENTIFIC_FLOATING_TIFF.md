# Scientific TIFF floating-point prediction

The C++ scientific IO service imports and exports scalar Float32 TIFF Predictor
3 with Deflate or LZW. Python and both Dear ImGui hosts share the implementation.
No dependency was added. The transform is original code; interoperability is
checked with the existing reviewed libtiff 4.7.1 runtime.

## Contract and memory

Input accepts compression 8/32946 or 5, predictor 3, Float32 sample format and
homogeneous explicit IFD layouts in either TIFF byte order. Each row is shuffled
into four byte planes in significance order, followed by byte differencing
across the entire shuffled row. The byte-plane representation is independent of
the directory byte order. Decoding restores interleaved words in the declared
file byte order before the existing source-word transport handles sample policy.

There is no floating-point arithmetic in prediction. Signed zero, subnormals,
infinities and quiet/signaling NaN payloads retain their exact admitted words.
Finite-only remains the default; quiet-NaN and full-IEEE admission remain
explicit. Integer/RGB predictor-3 pairings, Float32 predictor 2,
uncompressed/PackBits predictor-3 pairings, mixed pages and unknown predictors
are refused.

A single row-sized scratch buffer is allocated after preflight and reused for
all rows. Both read and write working-byte estimates charge it. Checked sizes,
codec/strip limits and checkpoints bound the work. The existing staged writer
checks actual size against the bound, verifies the final descriptor including
prediction, then publishes atomically. Callback failure and cancellation preserve
an existing destination and remove owned staging.

## API and editor

Two append-only enum values follow the existing values 0–21:

| Value | C++ | Python |
| --- | --- | --- |
| 22 | `ScientificTiffFloat32DeflateFloatingV1` | `SCIENTIFIC_TIFF_FLOAT32_DEFLATE_FLOATING_V1` |
| 23 | `ScientificTiffFloat32LzwFloatingV1` | `SCIENTIFIC_TIFF_FLOAT32_LZW_FLOATING_V1` |

```python
from imagejxx import scientific_io as io
result = io.read_image_series("finite-input.tif")
request = io.ImageWriteRequest()
request.format = io.ImageFileFormat.SCIENTIFIC_TIFF_FLOAT32_DEFLATE_FLOATING_V1
io.write_image_series("output.tif", result.dataset, result.metadata, request=request)
```

For a Float32 document, choose Deflate or LZW and **Prediction → Floating
(Float32)**. Prediction defaults to None. Integer documents offer their existing
horizontal choice. Codec/prediction changes clear estimates and consent;
switching to an incompatible codec resets prediction. Explicit incompatible
drafts fail atomically.

Version 9 records explicitly store `predictor: "floating"`, codec, dtype,
components and sample policy. Replay restores an inert form requiring fresh
review. Older enum values, format IDs, record producers and defaults remain.
The two new format IDs omit the `imagej` decoder-compatibility label: ImageJ
hyperstack metadata is retained, but pinned ImageJ 1.54r logs predictor 3 as
unsupported. LibTIFF is the prediction/compression oracle. ImageJ-derived filter
expectations come from the existing pinned uncompressed Float32 corpus.

## Evidence

`build/p08j-floating-tiff001/final-audit001.json` binds the current runtime,
source freezes, test membership, products, actual UI captures and cleanup.

- 80 Windows native/desktop suites pass.
- Source and installed Python each pass the same 10,436 cases, retaining all
  10,133 previous IDs and adding 303. All 307 focused IDs appear in both full runs.
- The corpus has 78 independently constructed files, 78 libtiff-produced files,
  six structural refusals and 12 UI carriers. Fixed vectors check byte-plane
  boundaries; Python also decodes output strips independently.
- LibTIFF verifies 278 reads: 78 independent, 78 libtiff-produced, 78 native
  exports, 12 UI carriers and 32 UI exports. These use exact word comparisons.
- Twelve scenarios run in four host contexts, covering policy review, default
  rejection/recovery, exact XY/XZ/YZ readbacks, preview/history, export, cancel,
  source identity replacement and replay. A separate installed SDK consumer
  exercises both full-IEEE profiles.

LibTIFF's fixture writer narrows its resolution argument to binary32 before
forming a rational. Its slightly different Y spacing is asserted exactly;
independent struct carriers retain exact rational 4/5 resolution. This does not
relax pixel-word comparison.

Failed attempts are retained separately: initial Python collection exposed two
missing format-ID attributes; a later test expected exact rational calibration
from libtiff's rounded argument. Both were corrected before the final gates.
The first full source run exceeded its 1 GiB artifact bound, and the corresponding
installed attempt was stopped before reaching the same limit. Neither is
reported as passed. Fresh full runs use a 2 GiB artifact bound and verify owned
process cleanup. Scratch archives are verified before removal.

Actual ImGui event streams and CPU readbacks are checked; no OS-input or GPU
equivalence is claimed. Linux TIFF/product certification, legacy variants,
tiles/BigTIFF, broader metadata, high-bit/alpha and remaining ImageJ/Fiji/editor
gaps remain open.
