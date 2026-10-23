# Native scientific sessions

Version5 extends native sessions with immutable continuous path selections.
The frame uses `IMAGEJXX5\n` with the same uint64 little-endian JSON length and
single zlib stream as Version3. Encoding selects V5 when any active, previous,
or saved ROI selection is a native path; otherwise it selects V4 for intrinsic
regional selections and V3 for earlier geometry. V5 admits regions as well as
paths. A V5 file without a native path is rejected. V2/V3/V4 remain closed to
native paths; changing a version field cannot authorize their payload.

A V5 path selection has exactly `kind`, `points`, `width`, `attributes`, and
`path`. Its kind is `path`, its points array is empty, and its path is the
complete absolute move/line/quadratic/cubic/close command stream. Coordinates
retain binary64 precision; selection geometry is not replaced with a raster.
Finite coordinate, opcode, segment, byte and session node/depth/workspace limits
apply. Active, previous and saved geometry, opaque attributes and scientific
positions retain their existing ownership and history semantics. The native
codec does not imply desktop support for this new selection kind. Legacy UI
dictionary adapters explicitly refuse paths until their migration is complete.
See [the native path contract](parity/PATH_SELECTION.md).

The reserved metadata extension `imagejxx.planar_annotations` version1 stores
plane-native annotations with T/C/Z/Y/X extent binding, Selection geometry,
explicit XY/XZ/YZ plane and fixed voxel coordinate, optional T/C, name and opaque
attributes. It uses ordinary nativeV3 extension transport. Known malformed
records or unsupported extent changes refuse; they never become XY selections.
See [P06p](packets/P06p-planar-annotations.md) for current implementation and
remaining UI/transform work.

The public C++ API in [session.hpp](../include/imagejxx/session.hpp) and the
top-level Python `imagejxx` session API use one native codec. Version3 preserves
eager native document snapshots with explicit scientific axes. Its implementation
and qualification boundary are tracked in the [P06a card](packets/P06a-scientific-session-codec.md).
This format does not enable desktop C/Z/T navigation, composite rendering,
virtual sources or external-source recovery.

## Framing and parsing

| Byte offset | Length | Version3 content |
| --- | --- | --- |
| 0 | 10 bytes | ASCII `IMAGEJXX3` followed by newline (`0A`) |
| 10 | 8 bytes | Unsigned little-endian length of the uncompressed JSON bytes |
| 18 | Remaining bytes | Exactly one zlib stream containing that JSON |

The JSON length must be positive and fit the configured limits and addressable
storage. Truncation, a decompressed-length mismatch, additional zlib streams and
trailing compressed bytes fail. There are no archive members, member paths or
external payload files. No ZIP dependency is required.

The encoder emits compact UTF-8 JSON. Readers accept different key ordering and
ordinary JSON whitespace within the same limits. Object keys must be unique at
every level. Integers must fit signed or unsigned 64-bit representation; finite
JSON numbers retain their integer-versus-floating representation. NaN and
infinity are not JSON metadata values. Bounds on bytes, nodes and container depth
apply before DOM and pixel materialization.

All structural objects listed below require exactly their stated keys. An
unknown or missing structural key fails. Extensibility belongs in the declared
metadata containers, whose unknown members are preserved.

## Version3 record

The root has exactly these fields:

| Field | Value |
| --- | --- |
| `format` | String `imagejxx-session` |
| `version` | Integer `3`, matching the frame version |
| `documents` | Array of document records in saved order |
| `results` | Array of native metadata values |
| `workflow` | Array of native metadata values; loading does not execute them |
| `extensions` | Object of opaque session-scoped metadata |

An empty document array is valid. Results and workflow are metadata arrays,
without a new table or action schema imposed by this transport.

Each document has exactly these fields:

| Field | Value |
| --- | --- |
| `axes` | String `TCZYXS` |
| `shape` | Six positive int32 dimensions `[T,C,Z,Y,X,S]` |
| `pixel_type` | `uint8`, `uint16`, `float32` or `float64` |
| `sample_policy` | `finite_only` or `preserve_quiet_nan_float32` |
| `storage` | String `Float64LE` |
| `planes` | Array of base64 strings, one for each T/C/Z plane |
| `calibration` | Spatial/time calibration record |
| `metadata` | Native document metadata record |
| `display` | Native presentation record |
| `selection` | Selection record or null |
| `previous_selection` | Selection record or null |
| `rois` | Array of saved ROI records |
| `overlays` | Array of native metadata objects |
| `fourier` | Fourier record or null |

`C` is the scientific channel axis. `S` is the independent interleaved sample
axis and must be 1, 3 or 4. Singleton axes remain explicit. Plane ordering is
T-major, then C, then Z: the plane index is `(t*C+c)*Z+z`. Within a plane,
samples are ordered Y, X, then interleaved S. All positions are zero-based.
Checked products determine exact plane and payload sizes before decoding.

### Backing samples and declared pixel type

Every plane payload contains exactly `Y*X*S*8` bytes: IEEE binary64 sample bits
in little-endian order, base64 encoded with canonical padding. This is the native
`Image::pixels` backing representation, independently of `pixel_type`.

Native finite storage can retain UInt8-tagged values such as `-1`, `0.5` and
`300`, or a Float32-tagged value `1+2^-40`. Version3 retains these backing bits
without clipping, rounding or changing the declared type. A typed NumPy export
or another ordinary image conversion can still convert to that declared type;
such an export is not a raw backing-bit inspection.

`finite_only` admits finite backing samples under the existing native source
rules, including the finite Float32 range check. The explicit
`preserve_quiet_nan_float32` policy requires declared Float32 and scalar S=1.
It uses the existing represented-Float32 bit contract to preserve admitted quiet
NaN payloads and signed zero. Signaling NaNs, infinity and unrelated nonfinite
policies are not admitted. Opaque binary sample payloads do not relax the finite
metadata rule.

### Calibration, metadata and display

`calibration` has exactly `spacing`, `origin`, `direction`, `unit`, `time_unit`,
`frame_interval` and `time_origin`. Spacing/origin contain three numbers;
direction contains nine row-major numbers. Existing native calibration
validation applies. Spatial coordinates refer to voxel centers, and T uses the
stored time origin and frame interval.

`metadata` has exactly `title`, `properties`, `extensions` and `record_layout`.
Title is a string; the other fields are native metadata objects. Unknown
members within those objects survive. Native metadata supports null, booleans,
signed/unsigned 64-bit integers, finite floating numbers, strings and recursive
objects/arrays. The existing metadata equivalence policy permits normalization
between equal nonnegative signed/unsigned integers and between string lists and
string-only arrays; the format does not carry C++ variant-alternative tags.

`display` has exactly `t`, `c`, `crosshair`, `orthogonal`, `linked_zoom`, `zoom`,
`plane_zoom`, `pan` and `properties`. Crosshair contains integer X/Y/Z indices;
plane zoom contains three numbers; pan contains three numeric pairs. Current
position, cameras and native display properties are validated against the
dataset. Global `display_range`, `lut`, `custom_lut` and `manual_threshold`
remain in `properties` under their existing policies.

The document-scoped future channel-display extension point is:

```text
documents[i].display.properties.channel_display
```

Producers using this reserved location identify their record with an integer
`version` member in an object. For example, an opaque future producer could store
`{"version":7,"vendor_options":{"example":true}}` there. P06a transports the
value as native metadata: it does not interpret the version, validate a channel
list, apply defaults or discard unknown future payloads. P06c owns the
interpreted version, validation, rendering and UI contract. This reservation
does not define channel ordering, colors, visibility, ranges or composite modes.

The interpreted schema is now `imagejxx.channel-display`. Version1 has exactly
`schema`, `version`, `mode`, `channels` and means SUM. Version2 additionally
requires `projection` with the closed values `sum`, `max`, `min`. Explicit V2
SUM retains version2 through edits and Save/Open. Both versions strictly
validate the full per-channel visibility, finite range and named/custom LUT
state; a recognized malformed record is rejected. Unsupported headers remain
opaque to native transport, while explicit desktop/API interpretation refuses
them. An unknown projection inside supported version2 is malformed, rather than
an opaque future record.

Resolved `imagejxx.channel-display-command` workflow records have separate
versions. V1 Set/Reset/Disable fields and defaults remain unchanged. V2 adds
exact nullable `display_dependency` and `navigation` fields. SelectMax and
SelectMin (`select_max` / `select_min`) require V2 and a captured display revision;
either intent can resolve to Max or Min. A transforming intent records complete
transformed tables and deliberate C0. SelectMax resolving Min requires C0;
SelectMin resolving Max requires C0. A no-transform SelectMin resolves Min with
null navigation, retaining the current C on replay. Strict replay applies the
stored result to current navigation,
preserving T/Z; it never reruns the LUT transformation or range scan. Save/Open
preserves historical source IDs and does not execute or rebind a workflow.
See the [adopted contract](parity/reviews/P06c2-v01-adoption-002.json) for the
exact field sets, compatibility and declared reference-context limitations.
The [Min contract](parity/reviews/P06c3-min-adoption-001.json) and its
[publication clarification](parity/reviews/P06c3-min-adoption-002.json) preserve
those schemas. Native Min classifies the serialized logical state, matching
native Max's policy; it does not reproduce Java's Grayscale processor-cache
history. Gray first/repeat therefore remain deterministic after Save/Open.

Arbitrary session-root extensions, such as `opaque_future_state`, remain opaque
metadata and do not define this document display contract. Frontends retain
their capability guards: a file transportable through the headless API can still
be refused by a desktop that cannot represent its axes or display properties.

### Selections, saved ROIs and Fourier state

A non-mask selection has exactly `kind`, `points`, `width` and `attributes`.
Points are numeric pairs; attributes are native metadata. A mask selection has
those fields plus `shape` as `[height,width]` and `mask` as base64 packed bits.
Bit membership is MSB-first within each byte. The packed byte count is
`ceil(width*height/8)`; unused tail bits are retained exactly. Saving a selection
does not rasterize it. A previous selection may belong to an earlier canvas
extent; restoration into a current canvas still requires the usual validation.

A saved ROI has exactly `name`, `selection`, `t`, `c`, `z` and `attributes`.
Each position is an in-range integer or null; null retains the absence of an
axis restriction. Styles remain in the existing selection/ROI metadata.
Overlays retain their native metadata objects and existing validation policies.

A Fourier record has exactly `shape`, `original_shape`, `storage` and `pixels`.
Shape is `[height,width]`; original shape is null or a valid original
`[height,width]`. Storage is `Complex128LE`: real and imaginary IEEE binary64
components alternate in a little-endian base64 payload. Components are finite.
Existing eager-Z Fourier state is supported. T>1 or C>1 combined with Fourier
state is rejected because the current model has no scientific Fourier-position
association.

## Limits and ownership

Every `SessionLimits` field must be positive. Defaults are:

| Field | Default | Scope |
| --- | --- | --- |
| `max_documents` | 64 | Document records |
| `max_planes` | 65,536 | Sum of dataset T*C*Z plane counts |
| `max_samples` | 134,217,728 | Dataset backing samples plus Fourier real/imaginary components; also limits each packed selection canvas |
| `max_json_bytes` | 1,073,741,824 | Uncompressed JSON bytes |
| `max_encoded_bytes` | 1,073,741,824 | Complete framed file bytes |
| `max_working_bytes` | 4,294,967,296 | Conservative codec workspace admission |
| `max_nodes` | 1,048,576 | JSON containers, primitive values and object keys |
| `max_depth` | 160 | JSON container nesting |

Native opaque metadata additionally retains its existing 128-container depth
policy. Caller limits can be smaller; a numeric maximum does not promise that a
capture also fits every other limit. Version2 migration retains its existing
1 GiB uncompressed interchange ceiling even if a caller raises the new limit.

`estimate_session` reads shapes, calibration and annotation/metadata descriptors,
not image samples or provider planes. It first bounds native descriptors before
building a bounded metadata projection. It allocates no image buffers, selection
raster or document owner. Unsupported lazy sources are rejected explicitly.

`SessionEstimate.source_bytes` is the logical binary64 backing size, including
Fourier components. `json_bytes` is the planned complete uncompressed size;
`encoded_bytes` is a conservative compressed-size bound including the header.
`working_bytes` is a conservative admission estimate, excluding borrowed source
storage, allocator bookkeeping and unrelated application allocations. Bounds
cover decompressed JSON, parser nodes, loaded backing and compression workspace;
they are not a whole-process memory counter. Descriptor admission also uses a
conservative finite-number representation bound, so a tight workspace limit can
reject a capture before its exact projection is built.

Encoding and file saving consume captured immutable snapshots and do not edit
documents. The low-level `save_session` function does not mark an owner saved.
After successful publication, an owner can mark the particular captured snapshot
saved; a newer edit then remains dirty. Shared desktop save workflows perform
this captured-snapshot marking.

Loading validates all records before returning the completed result. Loaded
owners have fresh identities, zero revisions, clean state and empty undo/redo
history. Source identities, dirty state and history are not restored. Recorded
actions remain data and require their existing explicit replay/rebinding rules.

File saving fully encodes before exclusively creating an owned sibling temporary
in the existing destination directory. It writes, flushes and closes before
atomic replacement; failure before replacement removes only its owned temporary
and preserves the old destination. Parent directories are not created. Native
controls provide cancellation/checkpoints; Python additionally exposes
`cancellation`, `checkpoint` and `stage_progress` keyword arguments. Callback
exceptions propagate. Malformed reconstructed positions are invalid archive
arguments, without converting arbitrary callback exceptions into validation
errors.

Input/output paths are captured as absolute paths before their first user hook;
save captures its destination before encoding callbacks. A callback changing CWD
cannot redirect a relative operation. Empty/NUL paths refuse before callbacks.
The shared atomic helper applies the same path capture to immediate, deferred
and streaming output. See [path and fault qualification](ATOMIC_FILE_FAULTS.md)
for retained failing regressions and the remaining filesystem/platform scope.

## Version2 migration and compatibility APIs

The native decoder also accepts `IMAGEJXX2\n`, followed by a four-byte
big-endian JSON length and one zlib stream. It maps legacy `[Z,]YXS` records to
explicit T=C=1 while preserving sample interpretation, legacy defaults and
admitted metadata. Version2's legacy declared-type canonicalization is retained;
Version3's raw-backing preservation is a distinct encoding contract.

Marked Version2 records retain strict `native_document_state` version1 checks:
document count, declared metadata, display-property presence and the scalar wire
projection must agree. Unknown root metadata accepted by the native migration
path is retained in session extensions. Re-encoding through the native API always
produces Version3. Malformed or ambiguous legacy input can fail the new bounded,
duplicate-aware native reader.

The shared Dear ImGui Window save workflow writes Version3 in both launch modes;
desktop opening retains capability checks and Version2 compatibility. Explicit
legacy native `desktop::saveSession` and Python
`imagejxx.ui.persistence.save_session(path, documents, ...)` remain Version2
compatibility APIs. The latter accepts detached legacy UI models.

`imagejxx.ui.persistence.load_session(path, *, extensions=None)` accepts Version2
and Version3 and returns `(documents, results, workflow)` as detached legacy UI
models. Its Version3 path uses the native decoder, then a checked native
projection of every snapshot before constructing the detached models. Only
losslessly representable eager T=C=1 records are accepted: scientific T/C axes,
explicit ROI T/C positions, unsupported display properties and primitive backing
that would change on conversion to its declared type are rejected. This adapter
retains calibration, annotations, Fourier state and supported display values,
including manual threshold state; it does not extend the detached model's axes.

For Version3 files with nonempty session-root extensions, callers must pass a
mutable mapping as `extensions` so those values have an explicit destination.
The mapping is updated after all detached models are staged successfully:

```python
from imagejxx.ui import persistence

opaque = {}
documents, results, workflow = persistence.load_session(
    "eager-z.ijx", extensions=opaque
)
```

Use the native scientific API for records outside that compatibility subset.
The native Python functions below are exported directly from `imagejxx`.

## Public API examples

An installed C++ consumer needs only the public core target and headers. This
example saves two scientific time points with one channel and one Z plane:

```cpp
#include <imagejxx/session.hpp>
#include <cassert>

int main() {
  using namespace imagejxx;
  Image first(3, 2, 1, PixelType::UInt16);
  Image second(3, 2, 1, PixelType::UInt16);
  first.pixels = {0, 1, 2, 3, 4, 5};
  second.pixels = {100, 101, 102, 103, 104, 105};
  Document owner(Dataset::from_planes({2, 1, 1, 2, 3, 1}, {first, second}));
  auto view = owner.snapshot().display();
  view.t = 1;
  view.crosshair = {2, 1, 0};
  owner.set_display(view);

  const auto saved = owner.snapshot();
  SessionCapture capture{{saved}, {}, {}, {}};
  SessionLimits limits;
  limits.max_json_bytes = limits.max_encoded_bytes = 32 * 1024 * 1024;
  limits.max_working_bytes = 128 * 1024 * 1024;
  const auto estimate = estimate_session(capture, limits);
  assert(estimate.planes == 2 && estimate.samples == 12);
  save_session("dataset.ijx", capture, limits);
  owner.mark_saved(saved);

  const auto loaded = load_session("dataset.ijx", limits);
  const auto snapshot = loaded.documents.at(0)->snapshot();
  assert(snapshot.display().t == 1);
  assert(snapshot.data().dataset.plane({1, 0, 0})->at(2, 1) == 105);
}
```

The equivalent native Python interface uses the same codec:

```python
import imagejxx as ij
import numpy as np

pixels = np.arange(12, dtype=np.uint16).reshape(2, 1, 1, 2, 3)
owner = ij.Document(ij.Dataset(pixels, axes="TCZYX"))
view = owner.snapshot().display
view.t = 1
view.crosshair = [2, 1, 0]
owner.set_display(view)

saved = owner.snapshot()
capture = ij.SessionCapture([saved], results=[{"value": 7}], workflow=[])
limits = ij.SessionLimits()
limits.max_json_bytes = limits.max_encoded_bytes = 32 * 1024 * 1024
limits.max_working_bytes = 128 * 1024 * 1024
estimate = ij.estimate_session(capture, limits)
assert estimate.planes == 2 and estimate.samples == 12
ij.save_session("dataset.ijx", capture, limits)
owner.mark_saved(saved)

loaded = ij.load_session("dataset.ijx", limits)
assert loaded.source_version == 3
restored = loaded.documents[0].snapshot()
assert restored.display.t == 1
np.testing.assert_array_equal(restored.dataset.numpy("TCZYX"), pixels)
assert loaded.results == [{"value": 7}]

# In-memory transport uses the same record and limits.
encoded = ij.encode_session(capture, limits)
again = ij.decode_session(encoded, limits)
assert len(again.documents) == 1
```

These scientific records are usable through headless C++ and Python. Passing
them to the desktop remains subject to the editor's current axis/display
admission policy.

The manual threshold display property additionally accepts version two with
`version`, `lower`, `upper`, `t`, `c`, `z` and Boolean `all_z`. All-Z state applies
at the recorded T/C across every Z; `z` retains its captured anchor. Version one
remains the six-field single-plane representation. This is a nested display
property version, independent of the session archive envelope. Unknown fields,
non-Boolean scope flags and unsupported versions refuse. See
[threshold scopes and record compatibility](THRESHOLDING.md).

Manual threshold display version three stores `version`, `lower`, `upper`,
`t`, `c`, `z`, and `ranges`. The latter is exactly three integer
`[start, stop, step]` triples in T/C/Z order, with zero-based half-open bounds,
positive steps and nonempty ranges within the dataset. The anchor T/C/Z may lie
outside the selected grid but must remain inside the dataset. This form excludes
`all_z`. Version-one single-plane and version-two all-Z forms remain supported.
Z subsetting intersects the selected grid and remaps its Z progression; an empty
intersection removes the overlay. These are nested display-property versions,
not changes to the session archive envelope.
