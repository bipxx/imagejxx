# Scientific BigTIFF container

The optional C++ scientific IO service reads and writes BigTIFF for the existing
48 sample/codec/storage profiles: UInt16, UInt8, RGB8 and Float32; strips or tiles;
uncompressed, Deflate, PackBits or LZW; supported horizontal/floating predictors.
The C++ ImGui desktop and optional Python desktop share the container control.
The processing core continues to own parsing, transforms, budgets and publication.

`TiffContainer::Classic` / `io.TiffContainer.CLASSIC` remains the write default.
Set `ImageWriteRequest.container` to `BigTiff` / `BIGTIFF` explicitly.
`ImageFileInspection.container` reports the detected container. The 48 existing
`ImageFileFormat` ordinals still identify sample/codec/storage profiles. BigTIFF
metadata identities use `F.G080.bigtiff-…`; classic identities retain their bytes.
Source metadata can be canonicalized when writing either container.

The reader accepts version 43 with an eight-byte offset width and zero reserved
header field, both byte orders, 64-bit directory counts/links and LONG8 block
offset/count tables. SHORT/LONG block tables remain accepted. Eight-byte inline
values include RGB component triples and calibration RATIONALs. Signed and IFD8
block tables, unsupported tags/types/layouts, mixed page descriptors, overlaps,
directory loops, truncation and resource/arithmetic overflow are rejected.
Input tables are bounded before allocation. Pixel payloads are not decoded during
inspection. The existing decoder and bit-preserving predictor paths are reused.

The canonical writer emits little-endian BigTIFF with LONG8 block tables and
eight-byte-aligned directories, external values and block starts. Encoded byte
counts exclude alignment padding; the frozen bound includes it. RGB SHORT triples
and RATIONALs fit inline. Compression patches use the chosen container width.
The staged descriptor and frozen metadata verification includes the container before atomic
publication. Failed admission and canceled work preserve existing destinations.

Large-file admission stays explicit: default input and output limits remain
512 MiB and 128 MiB. Choosing BigTIFF does not raise memory or file limits.
Callers may increase `ImageFileLimits` within native address/resource bounds.
Sparse test recipes exercise directory, external-text and pixel offsets above
4 GiB in both byte orders; sparse files are removed in `finally` and their logical
zero holes are never archived as multi-gigabyte fixtures. This does not claim
unlimited-memory or out-of-core processing of arbitrary large datasets.

BigTIFF workflow version 15 requires all seven explicit fields: `container`,
`storage`, `compression`, `predictor`, `dtype`, `components`, `sample_policy`.
The container must be `bigtiff`; storage is `strips` or `tiled`; prediction includes
explicit `none`. Earlier records reject the new field and retain their original
version rules. Container changes invalidate estimates, loss consent and overwrite
consent. Replay creates an idle form and requires fresh inspection/consent.

Independent fixtures comprise 432 hand-built valid files, 200 malformed files and
432 files written by the pinned LibTIFF 4.7.1. They cover both byte orders, singleton
and asymmetric C/Z/T shapes, multiple table widths, edge padding and Float32 finite,
quiet-NaN and complete IEEE policies. The prior installed build rejects all these
BigTIFF carriers. No new external dependency or non-commercial plugin is added.

The [BigTIFF specification](https://libtiff.gitlab.io/libtiff/specification/bigtiff.html)
defines the wire structure. Independent checks use the existing reviewed LibTIFF
dependency and separate Python directory/codec decoders.

Broader TIFF/OME/vendor metadata, series/layout coverage and formal reciprocal
fixture/accepted-run registration remain separate requirements. This bounded
container implementation does not complete the ImageJ/Fiji migration or certify
an entire format family or release. All99 remains 42 in progress, 57 open, 0 complete.

Validation covers 18,841 exact case IDs in each source/installed context. Windows
passes all cases; Linux passes 18,831 with exactly ten predeclared Windows-only
exemptions. All 16,678 previous IDs remain. Native suites pass 87/238 checks, SDK
consumers cover 96 container/profile pairs, and each Linux C++ matrix runs 313
scenarios. New actual-control coverage includes 768 generic observations with
2,304 exact XYZ CPU readbacks, plus 16 quiet/IEEE observations and 24 additional
IEEE XYZ readbacks. Independent LibTIFF verifies 3,504 BigTIFF exports. The prior
installed writer produces identical bytes for 1,728 classic exports, and the
48 classic SDK carriers are unchanged. The independent generator reproduces all
632 hand-built carriers without importing product code.

Evidence: `build/p08q-bigtiff001/qualification001.json`,
`docs/parity/reviews/bigtiff-container-review-001.json` and the partial variant
inventory in `docs/parity/formats.json`.

Supplemental registry tooling: 615 of 617 checks pass. Two reproduce the existing
Auto Contrast historical producer-source fingerprint mismatch, documented before
this packet. Its first mismatched header is unchanged from P08p. The global
registry gate remains open; the failed receipt and baseline comparison are kept
in `build/p08q-bigtiff001/parity-check003/baseline-audit001.json`.
