# Scientific Dataset TIFF service

The service supports calibrated UInt16, [UInt8](SCIENTIFIC_UINT8_TIFF.md),
[Float32](SCIENTIFIC_FLOAT32_TIFF.md) and [RGB](SCIENTIFIC_RGB_TIFF.md) TIFF.
[Deflate](SCIENTIFIC_DEFLATE_TIFF.md), [PackBits](SCIENTIFIC_PACKBITS_TIFF.md)
and [LZW](SCIENTIFIC_LZW_TIFF.md)
profiles add lossless strip compression
across these layouts and the shared ImGui editor. The original uncompressed
UInt16 contract below remains supported.

The optional `imagejxx::io` C++ target implements inspection, calibrated Dataset
reading, write estimates and atomic streaming export for
`F.G080.tiff-imagej-u16-uncompressed-ifd-czt-v1`. It is independent of Python and
Dear ImGui. The core target remains independent of libtiff.

```console
cmake -S . -B build/io -DIMAGEJXX_BUILD_IO=ON -DIMAGEJXX_BUILD_PYTHON=OFF -DCMAKE_PREFIX_PATH=<reviewed-TIFF-prefix>
cmake --build build/io --config Release
```

An installed C++ consumer requests the optional component explicitly:

```cmake
find_package(imagejxx CONFIG REQUIRED COMPONENTS io)
target_link_libraries(my_application PRIVATE imagejxx::io)
```

Install both `development` and `io-development` components. On Windows the latter
includes the ten reviewed codec headers/import libraries/runtime files. Core-only
discovery does not find TIFF. Optional IO discovery can report unavailable while
core discovery succeeds, including when TIFF discovery is disabled.

```cpp
#include <imagejxx/io.hpp>

auto inspection = imagejxx::inspect_image_file("input.tif");
auto result = imagejxx::read_image_series("input.tif");
auto estimate = imagejxx::estimate_image_write(result.dataset, result.metadata);
imagejxx::write_image_series("new-output.tif", result.dataset, result.metadata);
```

The API admits one homogeneous scalar UInt16 series with linked classic TIFF
IFDs, uncompressed strips, MINISBLACK and TOPLEFT orientation. Reading supports
both byte orders; writing uses little endian. C is fastest in file pages;
returned Dataset planes use native TCZ ordering. Unsupported compression, tiling,
BigTIFF, hidden contiguous stacks, private metadata, overlays, palettes, RGB,
multiple series and OME/vendor metadata remain outside this profile.

Spatial units are px, um or mm; time units are s or ms. Origins must be zero and
direction identity. XY calibration uses positive uint32 rational pairs with at
most 1e-6 relative error. Estimates expose the actual pairs, decoded calibration
and error; Z spacing and frame interval retain binary64 decimal precision.
Inspection reports defaults and source provenance without decoding pixel planes.

ImageJ 1.54r FileSaver emits `unit=\u00B5m`, `loop=false`, `min` and `max` for an
ordinary calibrated scalar stack. The runtime-driven amendment in
[review 004](parity/reviews/scientific-tiff-u16-runtime-amendment-004.json)
admits only that exact escaped micrometer value in spatial unit fields, the
non-looping flag, and paired finite display extrema in [0,65535] with min <= max.
These fields remain in `source_description`; they do not restore document
display/playback state or change samples. Other escapes, unknown keys, looping
playback, incomplete extrema and unsupported values refuse. Export canonicalizes
source provenance, reporting dropped fields in `canonicalized_fields`.

All nine `ImageFileLimits` are positive hard service admission bounds. Checked
metadata/ranges/products precede decoding and user callbacks. Working-byte
estimates cover conservative service workspace rather than process or OS quotas.
The path-free write estimate excludes destination and temporary-path storage;
writing charges those paths and rechecks metadata/workspace limits before any
callback, provider read or temporary creation. Estimates never read provider
pixels. The writer validates each entire borrowed plane before writing its rows.

Inputs are frozen before callbacks. Reads use one opened source handle for raw
validation and libtiff decoding, with identity/size/change checks and a path
identity recheck. This detects observable changes without claiming a cryptographic
snapshot. Libtiff allocation limits and handlers are per handle. The writer
creates an exclusive sibling with one attempt, validates its completed metadata,
then publishes atomically after the final callback/cancellation check. The default
FailIfExists policy also protects a destination created during export.

Native tests cover 5 valid and 34 malformed carriers, allocation/admission limits,
callback failures and mutation, row/completion cancellation, source changes,
publication races, multiple strips and a 65,536-sample row. A stdlib binary reader
checks actual native output independently. Installed SDK checks use only its
prefix; actual ImageJ 1.54r reopening and FileSaver export are retained separately.

## Python Dataset interface

`imagejxx.scientific_io` provides the same typed requests, limits, inspection,
Dataset results and write estimates through a separate optional native extension.
Enable both `IMAGEJXX_BUILD_IO=ON` and `IMAGEJXX_BUILD_PYTHON=ON`, with the reviewed
TIFF prefix and pybind11 available. Building `_scientific_io` also builds its
required `_native` module. The `scientific-python` CMake install component contains
the Python package, both extensions, four reviewed codec DLLs and license notices.
The additive `processing-python` component contains the package and `_native`
without scientific/graphics extensions or TIFF runtime dependencies. Existing
default install components remain available.

```python
from imagejxx import scientific_io as io

inspection = io.inspect_image_file("input.tif")
result = io.read_image_series("input.tif")
print(result.dataset.shape)  # Explicit (T, C, Z, Y, X, S)
estimate = io.estimate_image_write(result.dataset, result.metadata)
io.write_image_series("new-output.tif", result.dataset, result.metadata)

request = io.ImageWriteRequest()
request.replace = io.FileReplacePolicy.REPLACE_EXISTING
io.write_image_series("output.tif", result.dataset, request=request)
```

Paths accept Unicode strings and `PathLike[str]`. Count/limit fields accept integers
without bool coercion. Limits and read/write requests have default constructors
and assignable fields; inspection/results/estimates expose read-only fields and
detached metadata/container values. Inspection shapes are tuples and `dtype` is
`"uint16"`. Dataset, calibration, sample policy and cancellation use the existing
processing module types rather than duplicate registrations.

The Python converter admits only the closed metadata key set and bounded ASCII
strings/string arrays; it checks lengths/workspace before owning copies and does
not recursively clone arbitrary objects. UTF-8 paths are bounded before native
string conversion. Native IO releases the GIL. Optional `cancellation`, `progress`,
`checkpoint` and `stage_progress` keyword arguments use the native controls;
callbacks acquire the GIL, preserve Python exceptions and remain alive throughout
the operation. Coarse progress is `(0,1)` then `(1,1)`; row-stage progress has a
fixed total. Malformed/resource admission has no user callbacks.

Installed and development imports each pass 123 selected checks: all 39 carriers,
independent writer pixels/rationals, metadata and path bounds, callbacks, GIL
release, cross-module cancellation, atomic publication, Dataset/execution/jobs
and legacy ndarray TIFF/PNG/NPY compatibility. Windows module-handle observations
verify all four codec DLLs actually load from the installed package with their
reviewed hashes. Processing-only component import works without those DLLs.
Actual ImageJ1.54r reopens Python exports, and the installed Python API reads
untouched FileSaver exports with literal TCZ pixels and calibration.

## Shared ImGui editor workflows

Both installed launch variants expose **File > Open scientific TIFF...** and
**Export scientific TIFF...** through the same C++ panel and native service.
Desktop builds default `IMAGEJXX_BUILD_IO` to `ON`; processing-only builds default
it to `OFF`. Explicit configuration remains available in both cases.

Open inspects the selected file asynchronously, shows TCZYXS dimensions and
calibration, and requires axis/calibration review before reading. The reader
holds file identity and change tokens across review and read. Successful open
adds one clean native Document with source provenance. Unsupported carriers and
changed input leave the document collection unchanged.

Export shows the source title, dimensions, calibration, encoded XY rationals and
estimate. It requires explicit pixels/calibration-only consent because display
settings, annotations and history are excluded. Replacement is separately
opted into. The worker stages output; guarded publication on the host thread
checks the source owner/data and cancellation before the atomic destination
operation. Cancellation of a ready job abandons staging before polling can
publish it. Display navigation does not change exported pixels. The panel
requires a `.tif` or `.tiff` destination.

All nine native resource limits are editable. Changing paths or limits invalidates
review and consent. The working-memory limit respects the processing setting.
The shared file browser filters TIFF names. Successful operations update
`scientific_io_last_directory`; existing preference saving persists it.

Recorded file actions use `imagejxx.scientific-image-io` version 1. Loading an
action restores a draft for fresh review; it never automatically opens or writes
a file, and consent is not carried forward. Whole workflow and Batch execution
reject these file rows. Native `.ijx` Batch policy remains unchanged.

Replay validation admits complete, operation-specific records with closed nested
descriptors. Shapes require six positive native-range TCZYXS integers, scalar
samples and the recorded IFD/sample limits. Calibration requires positive finite
XYZ spacing/frame intervals and canonical units. Export rationals require reduced
positive UInt32 pairs and exact agreement with encoded XY spacing. Source IDs,
nullable data identity, metadata keys/types, default/canonicalization ledgers and
operation-specific consent fields are checked. Provenance text remains bounded
ASCII data; its historical description is not interpreted as live file metadata.
Unknown, missing, mixed-operation or malformed fields are rejected before the
visible review changes. UTF-8 paths are checked without filesystem queries.
Only mode, path and limits enter the new draft; historical identities, descriptors
and consent never authorize an operation.

Native tests corrupt 94 fields/structures in records emitted by actual open and
export. Host checks verify malformed nested records preserve the active review
and document, historical export identities remain inert, and absent paths restore
an idle draft without creating a job or directory. Valid recordings remain
version 1; incomplete records that never matched the recorder are now refused.

Installed physical-input checks exercise both hosts, file browsing, consent,
linked XY/XZ/YZ views, raw-pixel export, preference reload and fresh replay.
Independent TIFF wire checks verify all C-fast pages and exact rationals. The
installed Python package passes 123 selected processing/IO checks and loads six
observed package-local runtime DLLs. Native staging, TIFF and panel tests,
desktop regressions, startup SU01/SU10, installed SDK consumption and six API
lifecycle/race cases pass. Two stale desktop test expectations were updated to
the established Z-origin and canvas-focus contracts without weakening their
pixel/history/selection checks.

Windows atomic staging now retains the created file object through a DELETE-only
handle after flushing and closing the writer. Cleanup uses handle disposition;
publication uses handle rename. Reusing the temporary pathname cannot redirect
either operation to an unrelated replacement. Successful publication releases
the ownership handle immediately. Cleanup failure never falls back to pathname
deletion. This uses the documented Windows
[handle information API](https://learn.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-setfileinformationbyhandle)
and [object reopening](https://learn.microsoft.com/en-us/windows/win32/api/winbase/nf-winbase-reopenfile).

Remaining TU01–TU09 acceptance cases, broader resource/IO faults and matching
release/wheel packaging remain required. POSIX temporary-file ownership under
hostile path replacement remains open. Fresh review is mandatory for replay.
Existing Python ndarray, ordinary desktop open and flat-stack codecs have not
been redirected. Generalized TIFF/OME, other pixel types and compression remain
outside this admitted profile. No TIFF/OME or ImageJ/Fiji family completion follows.

The [case-level TU audit](parity/reviews/scientific-tiff-u16-coverage-009.json)
now covers all 57 named cases across TU01–TU09 and distinguishes exercised cases
from partial/open proof. An additional six independently built fixtures close
the exact endian-vector, TCZYXS/table and strip-layout literal discrepancies.
Native and source/installed Python checks use the specified six UInt16 values,
`[2,3,2,2,3,1]` dimensions, `10000*t+1000*c+100*z+10*y+x`, strip rows 1/2/9 and
exact `[20,10]` partial-strip byte counts. Independent raw readers verify native
and Python output rationals for nonbinary decimals and the UInt32 reciprocal
boundaries, rather than trusting an estimate or product roundtrip.

Actual failure tests truncate an admitted input before its first sample read,
lock Windows output bytes to cause a real failed write, occupy the exact planned
temporary name, and cancel between planes. They verify preserved target bytes,
unchanged source document state/provider reads and no leaked owned temporary.
The lock test uses the documented
[Windows byte-range lock](https://learn.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-lockfileex);
it does not qualify disk-full, flush/close failures or POSIX behavior. The original
39-carrier corpus remains byte-identical. Neither the audit nor these additional
checks accept the entire profile or complete a family.

An additional 23 independent calibration/metadata carriers exercise six valid
default/provenance cases and 17 invalid numeric cases. Checks assert the exact
ordered default and canonicalization ledgers, legacy Z versus explicit singleton
dimensions, T1 missing interval versus T2 refusal, repeated CRLF descriptions,
spatial/time aliases and direct writer alias canonicalization. Native and Python
checks cover positive XY values immediately outside the UInt32 rational interval;
raw output checks cover values immediately inside it. Invalid Dataset calibration
construction remains distinct from IO planning refusal.

Windows tests lock every sample byte in each of the 34 original malformed and 17
new invalid carriers while leaving metadata readable. Independent second-handle
probes confirm the locks deny raw pixel reads, and a valid-file control fails
under the same lock. Inspect/read still refuse malformed metadata with zero user
hooks. This supplies independent no-sample-read evidence for those Windows cases.
Zero values in all nine resource fields refuse inspect/read/write before hooks,
provider reads or staging. Eight native suites and 60 source/60 installed Python
checks pass, including the preceding nine literal checks in each Python context.
Remaining filesystem faults, POSIX ownership and
instrumentation and legacy desktop routes stay open.

Thirty additional refusal carriers now exercise the remaining named TU06 case
families: bad headers, bounded/truncated IFD and tag tables, strip-count/offset
errors, hidden pages, SubIFDs/multiple document names, unsupported layouts and
calibration semantics. The excluded-layout controls include complete RGB,
16-bit palette, signed, Float32, tiled, PackBits and BigTIFF files. Independent
decoders verify those payloads and the child SubIFD rather than treating arbitrary
corruption as evidence for unsupported-format handling. Tag constants were checked
against the [pinned libtiff header](https://gitlab.com/libtiff/libtiff/-/raw/v4.7.1/libtiff/tiff.h);
the fixture construction and packet decoder are original code.

Windows sample-byte guards now cover all 81 malformed/unsupported carriers
(34 original, 17 calibration, 30 new). Both metadata inspection and full read
refuse with no user hooks and no permitted sample reads. Five additional writer
calibration-policy cases preserve existing targets with no provider reads or hooks.
Eight native suites and 103 source/103 installed Python checks pass, including
the preceding 60 checks in each Python context. These cases qualify refusal
behavior; they do not add support for the excluded formats. Remaining filesystem
and platform faults, ordinary desktop codec
routing and matching release qualification remain open.

Actual ImageJ1.54r singleton runtime checks now cover all eight combinations of
T=1/2, C=1/3 and Z=1/2. Native installed SDK and installed Python outputs reopen
with exact unsigned pixels, C-fast stack indices and anisotropic/time calibration.
Untouched FileSaver exports import through installed SDK/Python and open as eight
clean native editor owners after review. The runtime exposed two real refusals:
plain channel-only and time-only FileSaver stacks omit `hyperstack=true`.

The [runtime amendment](parity/reviews/scientific-tiff-u16-runtime-amendment-005.json)
now permits an absent flag for C/T only when exactly one C/Z/T axis is
non-singleton. Mixed axes still require literal `true`; explicit `false` for C/T
still refuses. Full IFD/count agreement remains required. Source descriptions
remain byte-preserved and the omitted flag stays in `defaulted_fields`. The
writer continues emitting explicit counts and `hyperstack=true`. Six independent
boundary carriers verify the two admissions and four mixed/false refusals.

FileSaver also omits Z spacing for a one-plane image, even with source depth 2.5;
both ImageJ reopening and the native importer default that omitted value to 1.
This is recorded as a default, not as retained source calibration. Canonical
native/Python single-plane exports include depth 2.5, which ImageJ preserves.
Both installed physical ImGui workflows, installed Python129, source acceptance109,
native8, desktop/startup/SDK and nine API lifecycle checks pass after the change.
Remaining filesystem/platform faults, legacy desktop routes and matching release
gates remain open; the full profile and ImageJ/Fiji families are not complete.

Evidence: `build/p06r-tiff-service001/tiff-service-handoff001.json`.
Python evidence: `build/p06r-tiff-python001/tiff-python-handoff001.json`.
UI evidence: `build/p06r-tiff-ui001/tiff-ui-handoff001.json`.
Replay validation: `build/p06t-replay-validation001/replay-handoff001.json`.
Literal/fault audit: `build/p06u-tiff-acceptance001/acceptance-handoff001.json`.
Metadata/boundaries: `build/p06v-tiff-metadata001/metadata-handoff001.json`.
Unsupported/malformed inputs: `build/p06w-tiff-refusals001/refusal-handoff001.json`.
Singleton runtime/reader fix: `build/p06x-tiff-runtime001/runtime-handoff001.json`.

Legacy TIFF save no longer encodes through a predictable `.imagejxx.tmp` file.
A failing regression first demonstrated destruction of an unrelated file at that
name. The encoder now uses a seekable memory client, then publishes once through
the core exclusive atomic-file helper. It removes the intermediate encoded-file
write/readback. The previous 1 GiB encoded-file size limit remains; vector capacity,
codec allocations and legacy image materialization are not a whole-process memory
bound. Callback failures are retained and rethrown outside the libtiff C boundary.
Other byte-backed desktop outputs now share the same publication helper.

Compatibility checks cover 24 native output combinations of UInt8/UInt16/Float32/
Float64, scalar/RGB/RGBA and one/two planes, independently decoded with the pinned
reader. Forty-four Python checks add generic input/output, orientation1–4,
MINISWHITE, PNG/NPY publication, unchanged source/history and occupied-directory
failure. Both installed physical UI workflows, native9 suites, source153 and
installed173 Python checks, startup/SDK regressions and nine lifecycle cases pass.

Ordinary interactive TIFF opening now enters the shared scientific review form
and starts asynchronous inspection. File > Open, Recent, dropped files and Python
UI `open_file` all use `Window::openFile`. No owner is created until the user
confirms the inspected axes or separately accepts flat pixels. Scientific import
preserves TCZ and calibration; flat import uses one Z plane per page and pixel
units. Unsupported scientific layouts stay in review with an error; they require
explicit flat consent rather than automatic fallback. Flat imports use the legacy
decoder and its limits, and do not create scientific workflow records.

Up to fifteen further paths can wait behind a review. Mixed image/session requests
retain order, queued relative paths are resolved at submission, and each following
TIFF needs fresh consent. Cancel closes the review and clears the queue; the
separate queue button clears only waiting paths. Successful scientific imports
update Recent. Failed flat decoding leaves the review and existing owners intact.

`Window::openLegacyFile` and Python UI `open_legacy_file` retain the explicit
synchronous compatibility route. For the matching C3/Z2/T2 fixture this gives
C1/Z12/T1 with pixel calibration. Paths-only startup now collects TIFF choices
in a detached [reviewed batch](STARTUP_TIFF_REVIEW.md) before its existing atomic
attachment. Mixed path/array/UI-Document launch now uses an ordered
[reviewed append queue](MIXED_LAUNCH.md) that retains accepted prefixes on cancel
or failure. Explicit legacy import remains available.
PNM's separate fixed-suffix staging has since been repaired by the shared
[PNM streaming writer](PNM_PUBLICATION.md). Filesystem/platform faults and release
gates remain open. Fourteen new source/installed Python checks cover review,
consent invalidation, generic TIFFs, mixed queues, relative paths and failure
isolation. Both installed physical ImGui hosts exercise normal Open and flat
consent alongside the scientific workflow. This is a scoped migration, not full
TIFF or editor parity.
Legacy repair evidence: `build/p06y-legacy-tiff001/legacy-handoff001.json`.
Interactive routing evidence: `build/p06z-normal-open001/normal-open-handoff001.json`.

The [shared atomic path/fault checkpoint](ATOMIC_FILE_FAULTS.md) now freezes
destinations before callbacks and qualifies actual Linux ENOSPC using a private
1 MiB filesystem. All three shared atomic entry points preserve existing targets,
source bytes and directory membership on that failure. This is helper-level Linux
evidence, not Linux TIFF/product certification or Windows disk-full proof.
Flush/close failure paths and POSIX temporary pathname ownership remain open.


The p06ze shared atomic flush/close checkpoint repairs Windows handle leaks
before and after commit. Private boundary matrices (Windows54/Linux42) preserve
targets and unrelated recycled resources; failing-device durability and POSIX
pathname ownership remain open. See [fault scope](ATOMIC_FILE_FAULTS.md) and
`build/p06ze-flush-close001/flush-close-handoff001.json`. No new product dependency
or non-commercial implementation is introduced. Broad family acceptance is open.


P06zf hardens deterministic POSIX staging substitutions using a pinned parent
and original inode, no-follow entry checks and anchored publication/cleanup.
Concurrent check-to-name-operation mutation remains open. See
`build/p06zf-posix-ownership001/posix-ownership-handoff001.json`; this is not Linux
TIFF/desktop/release acceptance or complete filesystem durability qualification.


P06zg binds Linux no-replace publication to the held descriptor, with no mutable
source-name fallback. WSL 9P demonstrated unsafe descriptor-link behavior and now
refuses that operation before creating a destination link; safe no-replace 9P
support remains open. Replace-existing/cleanup races remain open. Evidence:
`build/p06zg-link-publication001/link-publication-handoff001.json`.


## Virtual TIFF loading (p10u)

Both ImGui launchers offer **Load virtually (read planes and regions on demand)**
after Inspect in the scientific TIFF form. Select it, then confirm the reviewed
axes/calibration and Open. Changing loading mode resets confirmation; flat-pixel
import is disabled while virtual mode is selected. Float32 virtual loading uses
finite-only samples. Existing eager imports retain their supported nonfinite
policies. Startup reviews each file separately and attaches the complete batch
only after every input is accepted and revalidated. Each next file starts with
fresh loading choices and consent.

The C++ entry point is `open_virtual_image_series` in
`imagejxx/virtual_io.hpp`, linked through `imagejxx::io`. Python uses the same
native provider:

```python
from imagejxx import scientific_io as io

opened = io.open_virtual_image_series('image.tif')
data = opened.dataset
region = data.region(100, 200, 64, 32, t=0, c=1, z=2)
xz = data.plane('xz', 200, t=0, c=1)
```

Open inspects existing supported classic/BigTIFF profiles without decoding pixel
payloads. The provider retains bounded descriptors and a guarded file handle;
it decodes only intersecting strips/tiles, with partial row reads for uncompressed
data. Existing codec, integer/floating predictor and endian helpers serve finite
UInt8/UInt16/Float32 and RGB8. XZ/YZ extraction uses regional reads. Full-plane,
NumPy materialization and whole-dataset export naturally read their requested
pixels. No provider tile cache or whole-volume pixel table is retained.

File identity, size and change tokens are checked before and after reads,
including after callbacks. A changed/replaced/removed source refuses new reads;
already returned images remain independent snapshots. This is change-token
guarding, not a stored content-hash attestation. Concurrent calls use independent
decoder buffers; file seek/read pairs are serialized. The provider borrows the
calling native execution scope for cancellation/checkpoints/stage progress and
retains no Python callbacks or cancellation tokens after a call.

`ImageFileLimits` still limits logical total/plane samples, file bytes, metadata,
IFDs and encoded block sizes during inspection. A small region does not bypass
those descriptor/codec limits. `max_working_bytes` bounds retained descriptors
plus each read's conservative codec/row/output scratch; a region can fit when a
full decoded plane does not. Outstanding caller images, aggregate concurrent
reads, renderer caches, allocator overhead and process RSS are external. Large
sources require explicit logical/file limit choices. Startup retains its prior
conservative aggregate document/plane/sample/JSON/working admission, now using
metadata-only virtual descriptors instead of demanding session encodability.
Session estimation/encoding itself remains eager-only.

Recorded virtual imports use closed V16 with `loading: "virtual"` and
`source_format_version` naming the complete original V1-V15 format descriptor.
All original descriptor fields validate under that grammar. Eager records keep
their prior encodings. Replay restores virtual intent without opening the
historical path or retaining consent; Inspect and confirmation are required
again. Virtual file-backed sources participate in the existing worker histogram
and linked XYZ viewer paths. Other file formats, pyramid/tile caching and
asynchronous viewport loading, virtual output propagation, workspace
serialization/materialization choices and full platform qualification remain open.


## Shared virtual XY cache

Preferences → Resources → **Virtual XY cache (MiB)** sets a window-wide
decoded XY retention budget, defaulting to 128 MiB. The same persisted setting
is available through C++ `Window::updateSettings` and Python
`Editor.update_settings(viewport_cache_mb=128)`. Reducing the budget immediately
evicts least recently used entries. Increasing it lets more document planes stay
resident. The budget is positive; invalid updates leave settings and cache intact.

Virtual documents share this cache, including reviewed TIFF startup/import.
Each facade has one current XY slot, keyed by provider identity and T/C/Z.
Metadata and camera changes reuse its pixels. A hit makes that slot most recently
used; a successful miss evicts the oldest other slots as needed. Requested planes
that cannot fit are rejected before the provider call. Providers that return
excess vector capacity are rejected before retention. Failed reads preserve useful
prior entries. Eager plane tables bypass the cache.

The budget counts retained pixel-vector capacity in bytes, conservatively counting
separately retained slots even when providers share buffers. It does not bound
provider scratch/caches, temporary miss buffers, externally held image handles,
allocator overhead, metadata, linked XYZ/readback/display caches, GPU textures,
or process RSS. Per-document decoded view limits still apply. A worker snapshot
gets an independent cache with the captured limit, so worker activity cannot
evict live UI entries. An external shared image handle remains valid after
eviction; borrow `frame()` references only until the next cache-affecting action.

Python `Editor.virtual_view_cache_info` and native
`Window::virtualViewCacheInfo()` return detached budget/retained-byte/entry/eviction
counters. These counters describe this cache alone. Canvas geometry still requests
CPU frames on each draw; when visible planes exceed the budget, eviction can
cause repeated reads even with current textures. Metadata-only geometry and
texture reuse remain a required follow-up before background viewport loading. Document closure does not
leave ownership in the cache: weak slots release their provider and image when
the last owning facade goes away. Remaining work includes aggregate XYZ/display/
texture accounting, tiled/pyramid policies, asynchronous reads and stale-result
rejection. These remain required for complete large-dataset parity.
