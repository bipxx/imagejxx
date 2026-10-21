# Scientific UInt8 TIFF

The native scientific IO service supports scalar UInt8 TIFF alongside UInt16 and
Float32. It preserves ImageJ C/Z/T axes, spatial/time calibration and bounded
provenance through the public C++/Python APIs and both shared Dear ImGui hosts.
Source pixels and document history remain intact during file export.

`ScientificTiffU8V1` admits classic, uncompressed, grayscale 8-bit unsigned TIFF
with an ImageJ description. Input directories can be little- or big-endian and
have single/multiple strips. Dataset addressing is TCZYXS with S=1; file pages
remain C-fast. Output is little-endian with at most 64 rows per strip. RGB,
compressed/tiled files, BigTIFF and broader metadata variants remain open work.

UInt8 uses the finite-only sample policy. Float32 quiet-NaN/IEEE requests are
rejected for byte files before pixel callbacks. Inspection remains descriptor
only. Export validates unsigned integer samples in [0,255], estimates resource
bounds without provider reads, and stages output for atomic publication. A
canceled writer preserves an existing destination and removes temporary output.

```python
from imagejxx import scientific_io as io

result = io.read_image_series("input.tif")
request = io.ImageWriteRequest()
request.format = io.ImageFileFormat.SCIENTIFIC_TIFF_U8_V1
io.write_image_series("output.tif", result.dataset, result.metadata, request=request)
```

The C++ format is `ImageFileFormat::ScientificTiffU8V1`. The new enum value is
appended; existing UInt16/Float32 values and default write format remain unchanged.
The editor chooses the output format from the frozen source descriptor. Scientific
open still requires axes/calibration consent, and export retains the existing
pixels/calibration-only consent and explicit overwrite policy. Ordinary file
opening, startup batches and ordered mixed append use the same review path.
Explicit flat import remains available when the user accepts its metadata loss.

## Scalar file-action records

UInt8 file actions use version 3 of `imagejxx.scientific-image-io`, with explicit
`dtype`, `sample_policy` and a matching format identity. Version 3 supports the
three admitted scalar types; unsigned types require `finite_only`. It validates
root/profile/series agreement, bounded shape/calibration, metadata and replay
policy. UInt16 and Float32 producers retain their existing versions 1 and 2.
Tests also review equivalent older scalar records in the explicit version-3
schema. Historical fields never authorize file reads or publication: replay
restores inert drafts and requires fresh review. RGB components require a
separate format contract before admission.

## Evidence

The independent stdlib corpus contains ten admitted carriers and nine malformed
variants. It covers C/Z/T ordering, both directory byte orders, all 256 values,
130-row images, input strip choices, exact output wire bytes and estimates.
Additional checks cover refusal of float sample policies, read-free planning,
atomic overwrite, cancellation after row 65, record corruption and clean history.

Two actual pinned ImageJ 1.54r FileSaver carriers preserve exact source bytes
through Opener. Native current-plane inversion matches the pinned ImageJ plugin.
Physical ImGui input tests exercise ordinary/scientific file opening, explicit
flat consent, calibration review, browser filtering, startup atomicity, ordered
append, export consent/cancel and inert replay. Both byte orders pass in
source/installed C++ and Python hosts. ImageJ then reopens all eight native exports
with exact bytes, C/Z/T dimensions, spacing and frame timing.

The checkpoint passes 73 Windows native/desktop suites, identical source/installed
9,390-case Python inventories without skips/exclusions, and the installed public
C++ IO SDK consumer. All previous 9,322 Python cases remain, with 68 new cases.
Twenty-nine verified scratch targets were archived/removed, freeing
1,129,110,785 net bytes. Evidence is in
`build/p08b-uint8-tiff001/final-audit001.json`.

The implementation reuses the existing commercially reviewed Windows libtiff
runtime closure. No dependency, Qt component, non-commercial component or Java
runtime was added to application products. Linux IO/product certification and
remaining TIFF/ImageJ/Fiji/editor/release requirements remain open. The later [RGB TIFF profile](SCIENTIFIC_RGB_TIFF.md) preserves C-axis planes
and per-pixel RGB components separately.
