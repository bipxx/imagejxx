# Scientific Float32 TIFF

The optional native IO service supports `ScientificTiffFloat32V1` alongside the
existing `ScientificTiffU16V1`. Both Dear ImGui hosts use the same C++ reader,
writer, review panel and background jobs. Python adds typed bindings and wrappers;
no image processing or TIFF codec is implemented in Python.

The profile admits classic, uncompressed, scalar 32-bit IEEE TIFF with explicit
ImageJ metadata. It supports little/big-endian inputs, multiple strips, C/Z/T
hyperstacks, calibrated spacing/time and bounded provenance. File planes are
C-fast; Dataset planes retain native TCZYXS addressing. Output is little-endian,
with at most 64 rows per strip. RGB/palette, tiled/compressed TIFF, BigTIFF and
broader ImageJ metadata variants remain separate work.

## Read policy and exact words

`ImageReadRequest.sample_policy` defaults to `FiniteOnly`. Explicitly request
`PreserveQuietNaNFloat32` or `PreserveIEEEFloat32` for wider sample admission.
The finite policy rejects NaNs/infinities; the quiet policy rejects infinities
and signaling NaNs. UInt16 reads retain their finite-only contract. Inspection
reads descriptors without decoding samples. Float32 inspection conservatively
reports the IEEE policy because sample finiteness cannot be established without
reading pixels; it does not authorize a wider read policy.

The native decoder receives packed words in the host byte order, consistent
with [LibTIFF's scanline contract](https://libtiff.gitlab.io/libtiff/functions/TIFFReadScanline.html).
Float32 words pass through the existing reversible source codec instead of
floating-point casts. Signed zeros, subnormals, extreme finite values, infinity
signs and signaling/quiet NaN payloads survive file round trips exactly. The
writer emits integer word fields into bytes. Source words remain unchanged.

```python
import imagejxx as ij
from imagejxx import scientific_io as io

read = io.ImageReadRequest()
read.sample_policy = ij.FloatingSamplePolicy.PRESERVE_IEEE_FLOAT32
result = io.read_image_series("input.tif", request=read)

write = io.ImageWriteRequest()
write.format = io.ImageFileFormat.SCIENTIFIC_TIFF_FLOAT32_V1
io.write_image_series("output.tif", result.dataset, result.metadata, request=write)
```

The C++ equivalents are `FloatingSamplePolicy::PreserveIEEEFloat32` and
`ImageFileFormat::ScientificTiffFloat32V1`. Rebuild C++ consumers against the new
headers: `ImageReadRequest` gained a sample-policy field. The installed IO SDK
consumer is qualified against that layout. Existing UInt16 enum values, format
identity and source-level defaults remain unchanged.

## Editor behavior and records

Open a TIFF through the ordinary file flow or Open scientific TIFF. After
inspection, Float32 files expose Finite only, Preserve quiet NaNs, and Preserve
all IEEE words. A policy change keeps the inspected file descriptor and identity
guard, clears confirmation, and requires renewed axes/calibration consent. A
failed finite read publishes no document and allows an explicit wider policy.
Export chooses the format from the frozen source dtype and requires the existing
pixels/calibration-only consent. Source pixels, history and display remain intact.

Float32 file actions use scientific IO record version 2, the Float32 format ID,
and an explicit sample-policy token. Version 1 remains exclusive to UInt16.
Replay restores inert path/limits/policy drafts and requires fresh review. It
never reuses historical file descriptors to authorize publication. Export replay
uses the current source policy. Corrupt versions/policies/type descriptors are
rejected. Scientific file actions remain individually reviewed rather than batch
authorizations.

## Qualification and remaining work

`tests/fixtures/scientific-tiff-float32/SOURCE.json` identifies 15 independent
stdlib carriers and six malformed variants. Native and Python checks compare
exact words, input byte orders, strips, axes, metadata and output wire fields.
A separate 17,680-word corpus crosses the writer's 64-row strip boundary. Mid-row
cancellation preserves an existing destination and removes staging. Additional
checks cover read-free planning, limits, atomic replacement, editor processing,
undo/redo, export/reopen and replay validation.

The p07z checkpoint passes 72 Windows native/desktop suites, source/installed
9,308-case regression inventories, two supplemental checks in each package
context, and an installed public IO SDK consumer. It preserves every previous
8,979 Python case. Existing shared dialog/viewer regressions are freshly run.
Evidence: `build/p07z-float32-tiff001/final-audit001.json`.

The later p08a checkpoint adds actual shared ImGui input qualification for six
file-control scenarios in source/installed C++ and Python hosts. It covers policy
selection and renewed consent, finite refusal/recovery, cancellation, identity
replacement rejection, preview, processing, undo/redo and export cancellation.
The old finite numeric diagnostic readback and opted binary-word readback formats
are asserted separately without changing their contracts.

Six actual pinned ImageJ 1.54r FileSaver carriers cover finite, quiet-NaN and full
IEEE sources in both byte orders. Their raw source words match independent
stdlib carriers and survive ImageJ's own Opener exactly, including signaling NaNs.
Native filtering matches the actual radius-1 plugin runner. Pinned ImageJ then
reopens sixteen native exports from all four host contexts with exact words,
C/Z/T dimensions, spacing and frame timing. No NaN canonicalization was observed
in FileSaver/Opener transport for this captured corpus. Failed preliminary
processing captures are retained: the final runner uses the required macro thread
and AWT mode and asserts its effective radius explicitly.

The current checkpoint passes 72 Windows native/desktop suites and identical
source/installed 9,322-case Python inventories with no skips or exclusions,
retaining the previous 9,310 cases. It retains raw input reports and exact files;
28 verified scratch targets were archived/removed, freeing 1,160,902,092 net
bytes. The unchanged core/public/IO inputs retain the previous SDK qualification.
See `build/p08a-float32-tiff-ui001/final-audit001.json`.

This is Windows qualification with the existing reviewed libtiff 4.7.1 runtime
closure. Linux TIFF/product certification, broader scalar/RGB, compressed/tiled
and BigTIFF profiles and remaining rank/ImageJ/Fiji features remain open. No
dependency, Qt component or non-commercial runtime was added. Reference Java
classes are archived scratch and are absent from the application products.

The later [UInt8 TIFF profile](SCIENTIFIC_UINT8_TIFF.md) extends scalar IO and
adds explicit generic scalar file-action records. Existing Float32 producer
records retain version 2 and their sample-policy contract.
