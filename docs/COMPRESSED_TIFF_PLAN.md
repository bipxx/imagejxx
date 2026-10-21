# Compressed scientific TIFF closure

Current checkpoint (2026-10-20): Deflate, PackBits and LZW strip IO are
qualified on Windows, along with integer Predictor 2 and Float32 Predictor 3
for Deflate/LZW in the C++ service, Python and both ImGui hosts. See
[floating prediction](SCIENTIFIC_FLOATING_TIFF.md),
[horizontal prediction](SCIENTIFIC_HORIZONTAL_TIFF.md),
[LZW](SCIENTIFIC_LZW_TIFF.md), [PackBits](SCIENTIFIC_PACKBITS_TIFF.md) and
[Deflate](SCIENTIFIC_DEFLATE_TIFF.md) for current contracts and evidence.
Pinned ImageJ logs predictor 3 as unsupported; the reviewed libtiff runtime is
its compression/prediction oracle. Linux product qualification and broader
TIFF layouts remain open. The original implementation design below is historical.

The next gap is lossless strip compression for the existing UInt8, UInt16,
Float32 and UInt8 RGB scientific layouts. Compression is not enabled yet.
The qualified uncompressed defaults and record producers remain unchanged.

## Reviewed constraints

The pinned ImageJ 1.54r TiffDecoder recognizes PackBits (32773), LZW (5),
and Deflate (8/32946). ImageReader routes those through separate decoders.
These observations come from the retained upstream source under
`build/references/binary-imagej154r/sources/ij/io`, not from a new runtime dependency.
Do not copy the ImageReader PackBits implementation: its comment credits
Bio-Formats, and the commercial dependency boundary must remain explicit.
Use the already reviewed libtiff closure for codec operations, or implement
an original standard codec only with an independent reference corpus.

Current inspection admits compression=1, predictor=1 and exact raw strip byte
counts. Decoding preallocates a buffer sized by decoded samples; compressed
input needs a separate maximum encoded strip byte count. The codec allocation
cap, decoded sample cap and total working cap must remain independent.
The existing staged writer emits a fixed metadata prefix followed by rows,
then verifies both exact size and prefix bytes before publication.
Compressed sizes cannot be substituted into that model without changing
planning and verification explicitly.

## Implementation order and acceptance

1. **Owned staging patches.** Add an internal bounded patch operation using
   the held writer handle/descriptor. Limit patches to successfully appended
   bytes, preserve the append position, reject invalid ranges without overflow,
   and poison the stream after any callback or syscall failure. Keep identity
   bound cleanup/publication. This is the current implementation packet.
2. **Explicit codec contracts and import.** Choose one lossless codec after
   checking interoperability with actual pinned ImageJ Opener. Add versioned
   format identities and review fields without renumbering current formats.
   Require homogeneous codec/predictor/layout across pages. Account encoded
   strip buffers separately from decoded samples. Admit descriptors separately
   from validating pixel payloads; preserve all old test IDs when a formerly
   rejected compression tag becomes an admitted but malformed payload.
3. **One-pass bounded export.** Freeze descriptors, metadata and limits without
   reading pixels. Reserve the prefix, append actual compressed strips, collect
   bounded offset/count tables, patch the final metadata, close and verify.
   Use the existing staged handle throughout. Do not allocate a whole file or
   pad each strip to its worst-case size. Estimates become genuine upper bounds;
   final verification checks actual size against the bound and compares exact
   frozen metadata plus finalized offset/count fields. Repetitive input must
   produce a measurably smaller file than uncompressed output.
4. **Independent data qualification.** Cover both byte orders, multiple strips,
   rows crossing strip boundaries, U8/U16/F32/RGB, C/Z/T and calibration. Preserve
   every Float32 IEEE word under the opted-in policy. Exercise truncated streams,
   expansion limits, source replacement, cancellation, stage failures, no-replace
   and overwrite races. Capture actual ImageJ reads of native compressed output.
5. **Shared editor and products.** Add codec selection/review to the shared
   ImGui panel and Python API. Version the inert file-action records, retain
   explicit axis-loss and sample-policy consent, test real source/installed
   input in both hosts, and rebuild the public IO SDK. Retain all 9,468 Python
   baseline cases and all prior native/desktop cases.
6. **Remaining IO scope.** Further codecs/predictors, tiled TIFF, BigTIFF,
   high-bit RGB/alpha, broader metadata and Linux IO/product certification
   remain separate requirements after this first compressed profile.

## Current packet evidence

`build/p08d-compressed-tiff001` records the staging prerequisite. It does not
replace the installed RGB TIFF qualification in `build/p08c-rgb-tiff001`.
No public format, Python API, editor record or codec dependency is added by
this prerequisite. All99 remains 42 in progress / 57 open / 0 complete.

The prerequisite passes 75 selected Windows native/desktop suites and 450
focused source Python IO/editor cases. The syscall tests pass 78 checks on
Windows and 56 on Linux, including short/zero/error writes, interrupted POSIX
writes, Windows seek/restore failures, partial-patch cancellation, overflow
refusal, append-position preservation, replacement refusal and stage-name
substitution. The initial Windows test reader omitted delete sharing; its
failed log is retained as `core001`. Fixing the test reader yielded passing
`core002` and final gates. No production change was needed for that failure.
The Linux run qualifies this private primitive, not Linux TIFF products.
