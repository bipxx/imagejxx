# P06q — native ImageJ ROI interchange

## Current foundation checkpoint

`build/p06q-roi-codec001/foundation-handoff001.json` records the implemented
binary reader/writer, Python interface, Native15 plus restricted host,259
Python checks, a fresh installed public SDK consumer, and nine actual pinned
ImageJ/native decode comparisons (18 decodes). All final checks have zero skips.
This is a verified foundation, not completed ROI/ZIP/plane/UI interchange or a
new wheel/source distribution. Preserve the preceding shape-profile901 release.

The original C++ implementation follows the pinned
[ImageJ decoder](https://raw.githubusercontent.com/imagej/ImageJ/62db90a269cccc1ac79f658e4602043214ed055b/ij/io/RoiDecoder.java)
and [encoder](https://raw.githubusercontent.com/imagej/ImageJ/62db90a269cccc1ac79f658e4602043214ed055b/ij/io/RoiEncoder.java)
format facts. No Java implementation/runtime or new dependency enters the core.
The [authorship/integration review](../parity/imagej-roi-codec-commercial-source-review-001.json)
retains nine changed/new source/test/data files with before/after snapshots.
It is a root attestation, not independent legal review or full-family acceptance.

## Implemented contract

`imagej_roi.hpp` exposes `ImageJRoi`, `ImageJRoiLimits`, `decode_imagej_roi`
and `encode_imagej_roi`; the same names are exported by Python. Inputs and
outputs are detached bytes/value records. There is no image sampling,
filesystem access, document publication, plane guessing or Java dependency.

The supported profile covers rectangle, ellipse, straight line, polygon,
freehand, polyline, angle and point/multipoint geometry, with freeline/traced
wire type identity retained. The reader handles supported records in versions
217–228; the writer produces canonical228 records with finite binary32
coordinates. Styles, UTF-16 names/raw property text, group, explicit C/Z/T,
stroke scaling, point type/size and counters have typed transport. Unknown or
unmodeled types/options/extensions and opaque native attributes refuse rather
than being discarded. Transparent black's ambiguous zero color field and native
point processing widths that the file cannot express also refuse explicitly.

The binary C/Z/T zero sentinel maps to native `None`; binary1 maps to native0.
The separate legacy stack position remains a raw positive index, not a guessed
scientific Z position. No `.roi` geometry becomes an active XY selection.

Default bounds are16MiB input/output,65536 vertices, and1MiB text. Bounds,
integer arithmetic, non-overlapping regions, finite float values, Unicode
scalars, truncation, source-type/geometry consistency and unsupported trailing
data are checked before acceptance. Limits are local codec bounds, not a
whole-process memory quota. Geometry is validated again after wire quantization.

```python
import imagejxx as ij

roi = ij.ROI("region", ij.Selection("rectangle", [(2.5, 1), (6.5, 5)]),
             t=0, c=0, z=0)
wire = ij.encode_imagej_roi(ij.ImageJRoi(roi))
record = ij.decode_imagej_roi(wire)
assert (record.roi.t, record.roi.c, record.roi.z) == (0, 0, 0)
```

## Reference scope and observations

Fresh ImageJ classes were compiled from the pinned1.54r source archive with the
existing archive-verified Fiji JDK under excluded build trees. Nine owned test
inputs generated actual ImageJ encoder records. Native readers compare against
independent geometry/style/position declarations and raw field expectations.
ImageJ then decodes both reference and native-written files; observations match
for bounds/type, names, C/Z/T, colors/group, stroke width/scaling and properties
getter output. This does not qualify every geometry, raster, mask or UI path.

The source runtime reports a missing glasbey LUT when assigning groups. Those
exact advisories are retained; unexpected stdout still fails framing. No asset
was downloaded or added, and group/display LUT rendering is not qualified.
ImageJ's properties setter/getter changed non-ASCII property text in these
observations. The codec retains the actual UTF-16 wire text without normalizing
it. A future native metadata envelope should use an ASCII transport to survive
the upstream property parser; raw text transport does not certify logical
Unicode property-map semantics.

Failed compiler/default-constructor, initial style-version roundtrip and stdout
framing attempts remain. Superseded passing pytest fixture trees and empty
passed-job temporary directories were cleaned; final captures, source holds,
SDKs, raw logs/JUnit and reference provenance remain.

## Required next sequence

1. Extend composite coverage beyond the declared bounded fixtures below. Add remaining
   spline, arrow, rounded, text/image and option/display behavior rather than
   silently flattening it. Unsupported currently means an open required gap.
2. Implement the ZIP container through existing private compression support:
   checked CRC, sizes, duplicate/unsafe names, member and aggregate expansion
   caps, and malformed/truncated/archive-structure tests. Never extract entries
   into caller paths. Keep complete-set and omission tests independent.
3. Add explicit plane/fixed-axis/T/C and extent mapping. Preserve original
   validation before explicit rebind. Bare legacy stack indices need an explicit
   interpretation. XZ/YZ coordinates must never silently become XY processing
   selections. Preserve opaque attributes in a versioned native envelope and
   reject stale envelope/standard-geometry mismatches after external edits.
4. Prepare an atomic native batch import through existing document transactions
   and typed annotation commands: one accepted import, one history/recording
   event; no-op/stale/invalid import changes nothing. Reuse existing UI manager,
   file picker and publication infrastructure in both ImGui hosts.
5. Qualify actual UI import/export and matching wheel/SDK/standalone/source/member/
   notices artifacts. The new public header changes the expected SDK/wheel name
   maps; derive the explicit delta before adapting old exact-count gates.

The historical global audit failures remain open. No formal run, reference
contract or family status was promoted by this foundation. All99 families,
commercially eligible pinned Fiji, filtering/configuration/selection/viewing,
platform/performance and complete release obligations remain assigned.

## Rotated binary record increment

`build/p06q-rotated-roi001/rotated-handoff001.json` records the next bounded
increment: native active rotated rectangle/ellipse records, subtype retention,
and explicit preservation of inactive rectangle parameters on polygon records.
The pinned ImageJ programmatic rectangle constructor emits polygon wire type0;
its decoder uses that polygon payload even when subtype5 is present. Native
decoding preserves this behavior instead of promoting it to an active box.
Active freehand subtype5/3 records map to native center and orthogonal axes.
Ellipse export chooses the longer major axis; binary32 quantization is explicit.
Generated fallback vertices obey the caller's vertex bound.

Verification:294 source Python checks, Native15 plus restricted host, a fresh
installed public SDK consumer, and five pinned ImageJ/reference-native pairs
with ten actual decodes. Owned inputs cover both endpoint directions, a direct
programmatic rectangle, active rectangles prepared through the same protected
type assignment as the GUI constructor, and ellipses. This API preparation is
not mouse-driven GUI evidence. Actual ImageJ class/type, bounds, axis parameters,
names, C/Z/T and stroke width match for each pair. The broader basic fixture
suite still checks styles/properties. Raster equivalence is not qualified.

The initial exact-float native assertion was corrected to a binary32 tolerance;
the failed run is retained. Native002 passed; an unnecessary Native003 repeat
then failed only the fresh session-artifact directory guards. Both runs remain
retained. Final Build004/Native004 use fresh session fixtures and
freeze the final reference-test source. Python003 and SDK001 pass.
No Java code or runtime enters product roots, no dependency was added, and no
wheel, ZIP, UI interchange or complete release claim is made by this increment.

## Composite paths and masks

`build/p06q-composite-roi001/composite-handoff001.json` binds the subsequent
composite increment. The native codec retains binary32 absolute path values
with move, line, quadratic, cubic and close commands. Native scan conversion
uses even-odd membership, .01 curve fill flatness, bounded depth10 subdivision
and the existing ImageJ center-on-edge convention. It produces an existing
packed-mask selection on an explicit caller-supplied canvas; import without a
canvas refuses. Original curved paths and geometry outside that canvas remain
in `ImageJRoi.composite_path` for export. This does not add a new persistent
document geometry kind or imply plane identity; those mappings remain required.

```python
record = ij.decode_imagej_roi(data, mask_width=512, mask_height=256)
mask = record.roi.selection.mask((256, 512))
data = ij.encode_imagej_roi(record)
```

Export rerasterizes a retained path and checks it against the current mask;
stale geometry refuses instead of silently restoring an old path. A caller
editing the mask can explicitly clear `composite_path` to export its new
pixels. Packed masks without retained paths export exact unions of rectangles,
with identical runs merged over adjacent rows. A98-row solid region is one
rectangle, including under a five-segment limit. Empty masks remain explicit.
Decoded style/position/name/properties handling uses the existing codec paths.

`ImageJRoiLimits` adds `max_mask_pixels` (16Mi) and `max_path_work` (64Mi).
Existing max_vertices bounds input segments and flattened edges. Byte/scalar
counts are checked before path allocation/copy; sorted intersections, scan work
and filled pixels consume work budget. Canvas extents are positive and checked
before packed allocation. Coordinates outside finite +/-1e9 refuse explicitly;
there is no silent clamping. These are local algorithm bounds, not a process
memory quota. Long native work polls existing cancellation; Python bindings
release the GIL after copying mutable inputs. Cancellation delivery through
every future UI job path remains a separate obligation.

Final Build004, Python003 (376 passed, zero skips), Native002 (15 plus restricted
host), SDK002 and Reference003 pass. The SDK consumer calls composite APIs
through a fresh installed header/library. Pinned ImageJ generates56 owned
inputs covering holes/islands, self-crossings, half-pixel boundaries, clipping,
open/empty contours, quadratics, cubics and48 varied curved inputs. Native
12x10 masks match actual ImageJ masks. ImageJ then reads each original, retained
path export and plain-mask export:168 decodes. Retained paths match exactly;
plain-mask exports match masks, class, name, C/Z/T and stroke width. This is API
and raster evidence for these bounded inputs, not UI interaction or all-shape
acceptance. Initial test-adapter mistakes remain in the failed Python001 run.

New implementation is original native C++, with no new dependency or imported
Java product code. Reference Java/JDK remains excluded from shipping roots.
Authorship/source-delta attestation and a journaled cleanup receipt accompany
the handoff. Superseded passing pytest scratch and empty job temp directories
are removed; failures, final scratch, SDKs, reference outputs and source holds
remain. No wheel/source/standalone release was qualified here. Rounded, arrow,
spline, text/image, display options, ZIP, metadata/plane mapping, atomic import
and both ImGui workflows remain required.

## Rounded rectangles and display-option transport

`build/p06q-rounded-roi001/rounded-options-handoff001.json` records the next
increment. `ImageJRoi.rounded_rectangle` retains x,y,width,height and integer
corner diameter alongside a canvas mask. `imagej_rounded_rectangle_mask` is the
shared native/Python constructor for that mask. Explicit positive canvas sizes
are required for decode, with the existing pixel/path/work limits. Fractional
wire bounds remain separate from ImageJ's integer rounded-mask processing
bounds: the pinned double constructor truncates x/y and takes ceil(width/height).
The mask uses bounded cubic corner paths through the existing shape rasterizer.
Diameter must be an integer1..60535; unsupported negative signed wire fields
refuse explicitly. Large positive diameters clamp to the processing dimensions
as part of rounded-corner geometry, while the original diameter is retained.

```python
box = [1.25, 1.75, 8.5, 6.5, 4]
mask = ij.imagej_rounded_rectangle_mask(box, 12, 10)
record = ij.ImageJRoi(ij.ROI("rounded", mask))
record.rounded_rectangle = box
data = ij.encode_imagej_roi(record)
restored = ij.decode_imagej_roi(data, mask_width=12, mask_height=10)
```

Export preserves rectangle wire type1 and the corner field rather than changing
the record to a generic composite. It verifies retained parameters against the
current mask. After editing mask pixels, explicitly clear `rounded_rectangle`
to export a generic composite of the new pixels. Mixing rounded and composite
sidecars, using rounded parameters on a non-mask selection, missing canvases,
invalid diameters and resource overruns refuse.

The codec now transports overlay labels/names/backgrounds/bold/scalable labels
(bits8,16,32,64,2048) and point labels/delete prompts (bits1024,4096), alongside
the existing font size, label color and point style. Point-only flags on other
geometry refuse. These flags remain in the explicit binary DTO's `options`;
document metadata mapping and both ImGui renderers still need implementation.
Transport support does not establish label appearance, preferences, font
availability, UI prompts or overlay raster parity.

Final Build002, Python002 (482 passed, zero skips), Native001 (15 plus restricted
host), SDK001 and Reference001 pass. The SDK consumer exercises rounded masks
and options through installed public headers/library. Pinned ImageJ generates
20 rounded inputs spanning integer/fractional/negative/tiny boxes and five
diameters,32 overlay flag combinations and four point-label/prompt combinations.
Native rounded masks match actual ImageJ12x10 masks. ImageJ reads both original
and native exports (112 decodes) with matching class, float bounds, corner
diameter, mask, overlay settings, point settings, name, C/Z/T and stroke width.
Color fixtures use an opaque label color; arbitrary font/platform rendering is
not qualified. The reference fixture includes the actual binary records and
observations; runtime/classes remain under excluded build trees.

No new dependency or third-party product code was added. The source-delta
attestation and journaled cleanup bind this increment. Superseded passing pytest
scratch and empty job temp directories are removed; final evidence and SDK
remain. Arrow, spline, text/image records, full display behavior, ZIP, plane and
native envelope mapping, atomic imports, both ImGui workflows and matching
release artifacts remain required. No99-family or complete interchange closure
is claimed.


## Arrow binary records and processing masks

`build/p06q-arrow-roi001/arrow-handoff001.json` records native arrow subtype2
on line wire type3. `ImageJArrow` holds the original endpoints, style (filled,
notched, open, headless or bar) and head-size byte. The record's options retain
double-headed/outline flags (bits2/4). The processing selection is an explicit
canvas mask; positive canvas dimensions are required for import.

```python
arrow = ij.ImageJArrow([8.25, 12.5, 48.75, 32.25], style=1, head_size=10)
mask = ij.imagej_arrow_mask(arrow, 2.5, 64, 48, double_headed=True)
record = ij.ImageJRoi(ij.ROI("arrow", mask))
record.arrow = arrow
record.options |= 2
data = ij.encode_imagej_roi(record)
```

The native helper constructs binary32 arrow points and unions head fill,
butt-capped shaft/wing strokes and bounded miter joins using the existing
native shape rasterizer. It returns a packed mask with matching stroke metadata.
The original endpoints/settings stay in the DTO for lossless record export.
Export verifies the retained arrow against the current mask and stroke width;
stale settings refuse. After editing pixels, explicitly clear `arrow` and its
arrow-only option bits to export the new mask as a generic composite. Rounded
and arrow sidecars cannot coexist. Arrow-only flags on other records refuse.

This increment follows the pinned ImageJ **processing `getMask` contract**.
Double-headed mode changes its tail inset. ImageJ's separate drawing paths also
render a flipped head, and outline drawing uses additional stroke behavior;
those operations are not silently substituted for `getMask` and remain required
in the editor/rendering work. Outline flags are retained, not rasterized as an
outline-only processing selection. A wire record without a positive stroke
uses ImageJ Arrow's constructor width2. Degenerate endpoints after binary32
quantization refuse; resolving upstream null-mask cases remains an explicit gap.

Resource gates cover explicit canvas pixels, total generated polygon segments,
per-component path work and aggregate mask-union work under the existing limits.
Coordinates outside finite +/-1e9 and stroke widths outside (0,1e6] refuse
explicitly. Miter length is limited to ten half-widths with bevel fallback.
These are local bounded-algorithm contracts, not full process memory quotas.
Native loops use existing cancellation checkpoints; the Python wrapper copies
mutable arrow input before releasing the GIL. UI job/cancellation paths still
need their own evidence.

Final Build002, Python001 (900 passed, zero skips), Native001 (15 plus restricted
host), SDK001 and Reference001 pass. The fresh installed SDK consumer calls the
new public C++ type/helper and binary codec. Actual pinned ImageJ generates380
owned arrows: all five styles, widths1/2.5/7, head sizes0/10/30, all four flag
combinations, both endpoint directions, plus horizontal/vertical/short/clipped
examples. Native64x48 masks match all actual reference masks. ImageJ reads both
original and native files (760 decodes), matching arrow class, mask, integer
bounds, style, head size, flags, stroke width, name and C/Z/T. Test fixtures also
check native properties, malformed/truncated records, stale masks, missing
canvases, illegal styles/strokes and resource limits. The byte can retain larger
head sizes; this reference matrix does not qualify every byte value or geometry.

Original C++ implementation adds no third-party dependency or Java product
source. Source-delta attestation and journaled cleanup accompany the handoff;
reference classes/runtime, raw outputs, SDK and final test fixtures remain.
No new wheel/source/standalone artifact was qualified. Spline and text/image
records, complete arrow/overlay drawing, ZIP, metadata/plane mapping, atomic
import, both ImGui hosts and99-family release obligations remain required.

## Spline control points and sampled geometry

`build/p06q-spline-roi001/spline-handoff001.json` binds the spline increment.
`ImageJSpline` retains control points, original vertex wire type and explicit
integer-coordinate mode. Supported types are polygon0, freeline4, polyline5,
freehand7, traced8 and angle9. Native sampled processing geometry is a polygon
for closed curves and a polyline for open curves, including spline-fitted
angles. The original angle controls remain in the DTO; this does not qualify
angle measurement on sampled points or control-handle editing.

```python
spline = ij.ImageJSpline([[2.25, 3.5], [24.75, 4.25],
                          [17.5, 18.75], [4.5, 16.25]], type=0)
record = ij.ImageJRoi(ij.ROI("spline", ij.imagej_spline_selection(spline)))
record.spline = spline
data = ij.encode_imagej_roi(record)
restored = ij.decode_imagej_roi(data)
```

The native implementation uses centripetal knot spacing with duplicate-point
spacing floors, natural cubic interpolation and bounded periodic continuation
for closed curves. Default sample counts follow the pinned uncalibrated
perimeter/length behavior, including freehand smoothing, traced perimeter and
the minimum100 samples. A float traced constructor without an integer perimeter
buffer follows the observed minimum100 behavior; legacy integer traced records
retain their distinct length-derived count. Integer inputs and offsets are
preserved on export, with checked signed/unsigned header ranges. Binary32
coordinate/base normalization follows the pinned input/output conventions.

The codec fits only explicitly supported type/version/subtype combinations.
Export verifies sampled geometry against retained controls and automatically
writes the fit flag. A fit flag without controls refuses. After editing sampled
geometry, explicitly clear `spline` and bit1 to export the new geometry without
the old controls; also clear `source_type` when changing its wire geometry kind.
Conflicting source types, ambiguous geometry sidecars, non-integral integer
controls, exhausted knot precision, invalid coordinates, input/sample count,
byte and conservative work-budget overruns refuse. Native processing widths
other than1 require an explicit mapping and refuse rather than disappearing;
stroke style metadata still uses the existing binary fields. Open-line stroke
pixel behavior, width mapping and native angle-measurement integration remain
required. Point/rotated spline flags and custom sample-count persistence are
not claimed by this wire increment.

Final Build004, Python001 (984 passed, zero skips), Native001 (15 plus restricted
host), SDK001 and Reference004 pass. The installed SDK consumer exercises float
and integer spline types/helpers/codec. Pinned ImageJ generates48 float spline
inputs across six wire types and eight control variants, including reversals,
duplicates, integer-axis shapes, negative coordinates, sharp turns and long
curves with more than100 samples. Twelve additional owned legacy inputs set the
documented fit flag on actual integer encoder output; the current ImageJ fit
API enables float storage, so these are explicit wire preparations. Actual
ImageJ decoding supplies independent sample/mask observations for all60 inputs.
Native sampled coordinates match exactly after restoring Java's printed float
values to binary32, and30 closed-area32x24 masks match. ImageJ reads each
original/native pair (120 decodes), with identical samples, fit state, type,
closed-area mask, name, C/Z/T and stroke width. Native tests check properties,
stale samples, limits, unsupported types and all float/integer truncations.

Reference001 failed on decimal text versus binary32 comparison. Reference003
failed because a traced integer constructor had mutated the caller's arrays;
the final capture derives declared controls from the actual wire fields.
Raw attempts and exact driver revisions remain. Preliminary Reference002 is
diagnostic only: its original driver path was subsequently revised, so do not
reuse that receipt as current identity evidence. The earlier source is retained
as `SplineReference-v1.java`; only Reference004 qualifies this increment.
These source/runtime fixtures remain excluded from shipping products.

Original native implementation adds no dependency or third-party product code.
The source-delta attestation and journaled cleanup bind the current files;
failed captures, final SDK/tests, source holds and raw reference observations
remain. No wheel/source/standalone release or full spline/editor parity is
claimed. Text/image records, complete drawing/measurement/editing behavior,
ZIP, plane/native envelope mapping, atomic imports, both ImGui hosts and full
ImageJ/commercially eligible Fiji scope remain required.

## Typed text annotation transport

`build/p06q-text-roi001/text-handoff001.json` records the text annotation
increment. Public C++/Python `ImageJTextRoi`, `decode_imagej_text_roi` and
`encode_imagej_text_roi` preserve subtype1 records on rectangle wire type1.
The annotation holds raw stored x/y/width/height, UTF-8 text and font name,
integer font size, font style, justification, raw draw-string-mode bit, angle,
name/properties, legacy/C/Z/T positions, group, wire stroke/fill and overlay
settings. Text/font payloads use checked UTF-16 conversion, including surrogate
pairs. Versions218..224 omit the angle; versions225..228 carry binary32 angle.
The writer emits version228. Per-field text and total byte limits apply before
allocation; text/payload/header2/name/property overlaps and malformed/truncated
or unknown nonzero extensions refuse. Unicode loops poll native cancellation.

```python
text = ij.ImageJTextRoi()
text.bounds = [3.25, 4.5, 50, 20]
text.text = "measurement λ😀\nsecond line\n"
text.font_name = "SansSerif"
text.font_size = 14
text.justification = 1
data = ij.encode_imagej_text_roi(text)
restored = ij.decode_imagej_text_roi(data)
```

This is a separate annotation DTO with **no processing ROI/selection field**.
The generic `decode_imagej_roi` directs text records to the explicit text API.
Pinned ImageJ TextRoi recalculates layout bounds from font metrics and
justification while loading. Raw wire bounds are therefore not promoted to a
native processing rectangle or claimed as a glyph mask. Native layout/font
resolution, actual drawing, document annotation storage, editing and both ImGui
workflows remain required. The pinned draw-string-mode setter/getter ignores
the bit; transport preserves it as raw metadata. Justification3 is likewise a
raw transport value that ImageJ clamps to left. Native UI mapping must account
for these semantics instead of treating every retained bit as an applied mode.
Wire stroke width is retained even though the pinned text replacement object
does not apply the original ROI stroke width. No font asset was added.

Final Build003, Python001 (1178 passed, zero skips), Native001 (15 plus restricted
host), SDK001 and Reference002 pass. A fresh installed SDK consumer uses the
new public annotation API and verifies generic-processing decode rejection.
Pinned ImageJ generates144 owned annotations spanning two logical fonts,
ASCII/Unicode/multiline content, four font styles, three justifications and
three angles. Native transport matches raw wire bounds/content/font fields and
metadata. ImageJ reads original/native pairs (288 actual decodes) with matching
text getter, resolved layout bounds, font/style/size, alignment, draw mode,
angle, colors, effective stroke width, name/properties and C/Z/T. These are
ImageJ runtime observations, not native font-layout or glyph-raster evidence.
Additional native/Python tests cover raw ignored flags, justification3, all
truncations, malformed UTF-16/counts/style fields, size limits and owned older
218/224 records without angle fields. Older-version preparations have native
unit evidence only. The fresh reference stages retain their own immutable
driver source copies to avoid mutable-driver identity reuse.

Original native code adds no dependency or imported third-party product code.
Source-delta attestation, exact source/product receipts and journaled cleanup
accompany the handoff. Final tests/SDK, held sources, raw reference/runtime
evidence and nonempty useful temp trees remain. No new wheel/source/standalone
release, full text-editor parity or family closure is claimed. Image ROI
payload/native pixels, complete text/spline/arrow/overlay UI behavior, ZIP,
plane/native-envelope mapping, atomic publication and matching artifacts remain
required across full ImageJ/commercially eligible Fiji scope.

## Embedded image annotations and native TIFF pixels

`build/p06q-image-roi001/image-handoff001.json` records the image annotation
increment. Public C++/Python `ImageJImageRoi`, `decode_imagej_image_roi` and
`encode_imagej_image_roi` carry subtype4 records on rectangle type1. The typed
annotation holds an integer origin, owned RGB8 display pixels, retained TIFF,
wire opacity byte (0..255), zero-transparency flag and common raw metadata.
Generic processing-ROI decode directs these files to the annotation API; there
is no implicit processing selection field. Fractional stored origins follow the
pinned decoder's truncation to the integer image origin; export rejects integer
origins that would lose binary32 precision.

```python
annotation = ij.decode_imagej_image_roi(data)
pixels = annotation.pixels  # detached uint8 H,W,3 NumPy copy
pixels[0, 0] = [255, 0, 0]
annotation.pixels = pixels
annotation.serialized_image = b""  # explicitly discard stale encoded pixels
annotation.opacity = 127           # wire byte, not a0..1 floating value
data = ij.encode_imagej_image_roi(annotation)
```

The new core memory decoder supports classic TIFF in both byte orders,
uncompressed chunky8-bit grayscale (black/white zero), palette and RGB, with
bounded single/multiple strip layouts. It expands display pixels to RGB8,
preserving the original TIFF bytes for unchanged-image export, including its
palette/description/resolution data. ImageJ's ImageRoi serialization turns the
displayed image into its embedded image; original16-bit and float source
processors in this corpus serialize as8-bit display data. Native decoding does
not claim to recover scientific source precision absent from the file.

Retained TIFF is decoded again and compared with current dimensions/type/pixels
before export. A mismatch refuses; explicitly clear `serialized_image` to write
a new minimal uncompressed RGB TIFF. Pixel edits are not silently overwritten
by an old payload. Zero-transparency is an explicit typed flag and determines
its corresponding wire option; other raw display flags are retained. Native
opacity blending, ImageJ's transparency threshold/alpha behavior, rotations,
document publication and both ImGui render/edit paths remain required. The
decoded RGB bytes are not a claim of native composited/raster appearance.

Header/field types and counts, duplicate/unknown tags, strip sizes/offsets,
metadata/pixel aliases and nonzero unclaimed data are checked before expanded
RGB allocation. Next-IFD/multi-page, compression, other depths, non-default
orientation/planar/predictor/sample layouts, BigTIFF and additional pixel-affecting
metadata refuse explicitly. These remain required coverage gaps, not excluded
features. Palette and strip offsets are cached outside pixel loops. Existing
encoded-byte/work bounds apply, with new `max_image_pixels` default4Mi (RGB
double storage is at most96MiB at that cap). This is an allocation/algorithm
bound, not a whole-process quota; retained input/output and validation copies
also consume memory. Native scans/writes use cancellation checkpoints. Python
copies mutable inputs before releasing the GIL.

Final Build003, Python001 (1265 passed, zero skips), Native001 (15 plus restricted
host), SDK001 and Reference003 pass. The fresh installed SDK consumer creates,
decodes and edits image annotations and checks stale payload rejection. Pinned
ImageJ produces60 owned examples from grayscale, palette,16-bit, float and RGB
processors, with three opacity values, both transparency states, positive and
negative origins and both TIFF byte orders. Native RGB bytes match all actual
ImageJ observations. ImageJ reads each original, retained-payload export and
fresh native-TIFF export (180 decodes), matching pixels, bounds, opacity,
transparency flag and C/Z/T. Native fixtures also check raw name/properties and
payload retention. Independently constructed two-strip black/white-zero TIFFs
test both byte orders and alias rejection; these have native unit evidence only.
Malformed tags, sample/count fields, offsets, directory chains, corrupt/truncated
outer records and every embedded-payload truncation refuse. RGB8 input types,
pixel/byte/work caps and wrong codec entry points are covered.

No dependency, external asset or third-party product code was added; desktop
TIFF libraries are not required for this core memory codec. Source-delta
attestation and journaled owned cleanup accompany the handoff. Final SDK/tests,
source holds and raw reference/runtime evidence remain; no wheel/source/
standalone release or family closure is claimed. Continue bounded CRC ZIP,
explicit plane/native metadata envelopes, atomic batch publication, complete
annotation layout/compositing/editing in both ImGui hosts, remaining embedded
format variants and matching release artifacts across the original full scope.

## Bounded ROI ZIP container

`build/p06q-roi-zip001/zip-handoff001.json` binds C++/Python raw member transport
through `ImageJRoiZipEntry`, `ImageJRoiZipLimits`, `decode_imagej_roi_zip` and
`encode_imagej_roi_zip`. Stored and raw-deflate archives are supported; the
writer defaults to deflate and emits canonical UTF-8 names. Both paths reuse
the existing private prefixed zlib, adding no dependency or runtime DLL.

```python
members = ij.decode_imagej_roi_zip(archive_bytes)
for member in members:
    print(member.name, len(member.data))
archive_bytes = ij.encode_imagej_roi_zip(members, compress=True)
```

Every member must have a flat safe lowercase `.roi` filename and an Iout header
of at least64 bytes. No member is extracted to disk, silently omitted, or
promoted to a semantically validated ROI. Exact canonical Unicode duplicates
refuse; case-distinct names remain distinct. UTF-8, CP437 and checked Unicode
path extras are supported. Path separators, control/invalid Unicode names,
reserved device names, links, directories, non-ROI members and name conflicts
refuse. Central-directory ordering is retained. Typed decoding must follow
once a canvas and plane interpretation are supplied; a container header is not
evidence that the enclosed geometry/options are supported.

Validation checks a unique end record, single-disk classic directory bounds,
all local/central names, flags, sizes, dates and CRC declarations, optional
signed/unsigned data descriptors, exact compressed-stream consumption, expanded
lengths and actual CRC32. All local records must exactly cover the prefix before
the central directory: overlap, hidden/unreferenced local files, prefixes/gaps
and omitted central members refuse. Metadata extras have checked lengths;
ZIP64/encryption extras refuse. Comments are bounded metadata and are not
executed. ZIP64, split/encrypted archives, other compression methods, unsafe or
nested/mixed-file containers remain explicit compatibility gaps where required
by the overall scope; none is silently accepted or dropped.

Default limits are64MiB archive,4096 members,16MiB/member,64MiB total expanded
bytes,1024 raw/decoded name bytes and192MiB native working budget. Size arithmetic
is checked, including conservative compression bounds on32-bit zlib integer
types. Central/local graph and aggregate expansion limits are checked before
member decompression. Native/Python input-copy bounds, metadata, output reserve,
largest compressed temporary and codec workspace are accounted conservatively;
caller-owned Python/foreign allocator memory is not a whole-process quota.
CRC and compression/decompression chunks poll native cancellation. Decode is
all-or-nothing in memory; document publication remains a separate next step.

Final Build004, Python004 (1450 passed, zero skips), Native002 (15 plus restricted
host), SDK002 and Reference002 pass. The installed SDK consumer reads/writes a
mixed native set through the public core. Independent Python ZIP readers and
writers verify stored/deflated and streamed archives, comments and empty sets.
Tests cover signed/unsigned descriptors (including a CRC equal to the descriptor
signature), every CP437 high-half byte against the standard codec, Unicode path
CRC, expansion bombs/size lies, aggregate/working bounds, links, ZIP64 extras,
all truncations, corrupt data/metadata, central-order changes and actual omitted
central members that a separate directory reader would otherwise ignore.

The independently declared reference corpus has29 entries from all eight
existing geometry/annotation fixture groups, including case-distinct and Unicode
member names. Actual JDK stored and streamed-deflated archives roundtrip through
native code without byte/member changes. Pinned ImageJ decodes every member in
both original and native archives:116 actual decodes, matching bytes, names,
classes and positions. This uses the Java ZIP APIs plus ImageJ RoiDecoder, not
mouse-driven RoiManager UI. The initial reference harness failed on Unicode
Windows command-line paths; the final harness uses unique ASCII input paths
and an owned UTF-8 manifest, keeping archive names separate from filesystem
case aliases. Failed artifacts remain. Exact module/fixture/class identities
allow reuse of that reference after final test-only changes.

Original format implementation, existing compression dependency and CP437 source
facts are recorded in the authorship/source-delta attestation. Journaled cleanup
removes superseded passing pytest scratch and empty job temp directories while
retaining failures, final tests/SDKs, raw references and held sources. No new
wheel/source/standalone release, semantic import or family closure is claimed.
Next: explicit plane/fixed-axis/T/C/extent and legacy stack mapping, ASCII native
metadata envelope with stale-standard-geometry checks, atomic batch import and
shared ImGui file/manager workflows. Complete annotation layout/compositing/
editing, remaining format variants and matching release artifacts remain required.

## Explicit plane and legacy-position mapping

`build/p06q-roi-mapping001/mapping-handoff001.json` records a pure mapping layer
in public `imagej_roi_mapping.hpp`. `ImageJRoiPlaneMapping` declares full TCZYXS
source extent, XY/XZ/YZ plane, optional explicit fixed coordinate, standard-Z
interpretation and legacy-stack interpretation. `map_imagej_roi` admits an
existing geometry DTO; `decode_imagej_roi_mapped` also decodes binary geometry
on the chosen plane's actual canvas. A `MappedImageJRoi` retains the complete
typed source record and original context separately from its native annotation
and current target extent. No document, dataset sample or processing selection
is read or modified; codec admission may rasterize owned annotation masks.

```python
mapping = ij.ImageJRoiPlaneMapping()
mapping.extent = [3, 4, 5, 7, 11, 3]  # T,C,Z,Y,X,S
mapping.plane = "xz"
mapping.coordinate = 2              # fixed Y, not scientific Z
mapped = ij.decode_imagej_roi_mapped(data, mapping)
annotation = mapped.annotation
```

T/C wildcards stay optional and zero remains a real positioned index. Standard
file Z means scientific Z by default; on XZ/YZ a positioned standard Z refuses
unless the caller explicitly chooses `ImageJZPositionMapping.FIXED_AXIS`.
An unpositioned ROI needs an explicit fixed coordinate. A caller coordinate
cannot silently override a conflicting file coordinate. File C/T and fixed
positions must fit the original declared source. Plane geometry and packed-mask
dimensions are validated against their own axes, without cropping or XY
projection; existing native geometry rules allow off-canvas control points.

Nonzero legacy stack positions default to rejection. Explicit
`ImageJLegacyPositionMapping.FIXED_AXIS` interprets the one-based index along
the selected view's fixed axis without inventing T/C values.
`HYPERSTACK_CZT` is restricted to XY and uses C-fastest, then Z, then T order;
it never counts RGB sample components as scientific channels. Simultaneous
legacy and standard positions must agree. The source DTO retains the original
legacy index and wildcard fields rather than rewriting them to inferred CZT.

`rebind_imagej_roi_mapping` takes a complete replacement annotation and target
extent. It validates the original source and current binding before inspecting
the new target, including at the Python boundary, and then validates the entire
replacement. It does not infer a transform, project geometry or carry old T/C
values into an explicitly wildcard replacement. Read-only result getters give
Python copies, so mutating a returned source/context cannot invalidate the
captured original. `validate_imagej_roi_mapping` rechecks that admission under
the requested byte-codec limits. Native replacement annotations retain their
existing native validation limits; codec limits govern the retained source.

This is **not yet a persistent metadata envelope**. Raw source properties,
group, styles/options and other byte-codec fields remain in `source_record`;
opaque native metadata can be supplied on a complete native replacement. That
data must be serialized and bound to the actual standard geometry before UI
interchange is accepted. Reserved `imagejxx.roi.*` property keys currently
refuse, including future versions, instead of falling back to XY. Ordinary
property values mentioning the namespace remain ordinary text. Text/image
annotation DTOs need their separate plane-aware publication path; they are not
silently admitted as geometric processing ROIs.

Final Build004, Python003 (1602 passed, zero skips), Native002 (15 plus restricted
host), SDK002 and Reference003 pass. A fresh installed SDK consumer includes the
new public mapping header and exercises XZ mapping, all-position hyperstack
conversion, validation and explicit YZ rebinding. Python tests cover all three
planes, boundary-to-voxel coordinates, mask extents, wildcard/zero positions,
conflicts/out-of-range values, metadata retention, immutable-source copies and
original-before-target validation. Pinned ImageJ generates60 actual RGB
hyperstack `getStackIndex` values and legacy ROI records across T3/C4/Z5;
native mapping matches every declared T/C/Z. This is index/record evidence,
not actual UI view/import evidence. Final tests caught and fixed a reserved-key
prefix-length bug; failed Python002 evidence remains.

Original native code adds no dependency or imported product code. Source holds,
commercial-source attestation, exact source/product receipts, installed header
proof and journaled cleanup accompany the handoff. Superseded passing pytest
scratch and empty job temp directories are removed; failures and final evidence
remain. The additional public header changes future wheel/SDK member maps; no
new distribution was qualified here. Continue the ASCII native envelope with
stale standard-geometry/name/position/style checks and original-extent validation,
then atomic mixed-record import, both ImGui workflows, complete annotation
rendering/editing and matching release artifacts across the full original scope.

## Unchanged standard ROI envelope increment (2026-10-18)

`build/p06q-roi-envelope001/envelope-handoff001.json` binds the current scoped
increment. C++ core and Python interfaces expose `encode_imagej_roi_envelope`
and `decode_imagej_roi_envelope` through the existing public mapping header.
The standard `.roi` remains visible to ImageJ. Its sole outer property is
`imagejxx.roi.v1: <canonical base64>` followed by newline. Non-ASCII raw source
properties remain inside the binary envelope, avoiding ImageJ's legacy property
parser altering their characters. No dependency or imported product code was
added; the source attestation is `docs/parity/imagej-roi-envelope-commercial-source-review-001.json`.

The binary payload is deliberately closed and nonrecursive: magic `IJX` and
version byte1; six big-endian positive-int32 TCZYXS dimensions; plane enum
XY0/XZ1/YZ2; optional explicit coordinate as uint32 with absent=0xffffffff;
Z-position and legacy-position policy enums; length-prefixed original UTF-8
properties; and length-prefixed canonical original standard ROI bytes. Every
length is checked before copying; text/ROI/geometry limits still apply. The
ASCII outer property is charged at two UTF-16 bytes per code unit, including
base64 expansion. Unknown versions, invalid enums/extents, noncanonical base64,
extra properties, trailing payload fields and truncated regions refuse.

A bounded private preview reads only the outer property. Complete typed codec
admission follows using the original envelope's declared plane canvas. The
stored original must itself pass explicit mapping admission and canonical
encoding. Its properties must agree with the separate original-property field.
Only then does the decoder admit the actual outer record, restore the original
properties for comparison, and compare its complete canonical standard encoding
to the stored binding. Changed geometry, name, T/C/Z, group and display fields
refuse as stale. This comparison detects stale metadata; it is not an authenticity
mechanism. The returned record retains its original mapping and standard wire
metadata; later rebinding still validates that original before a new target.

This version intentionally exports **unchanged source bindings at ImageJ wire
precision**. It refuses native rebound annotations rather than emitting a
standard ROI that disagrees with them. It does not serialize arbitrary native
attributes or preserve extra native floating precision. Those remain explicit
next steps: a native row payload plus an independently validated standard
projection, including original/source and current/rebound contexts. Text and
image annotation DTOs remain separate; no processing-rectangle conversion is
introduced. No document publication or new ImGui import/render/edit UI is claimed.

Pinned ImageJ1.54r performs42 actual decode/re-encode cycles across XY/XZ/YZ,
first/last fixed coordinates and rectangle, ellipse, line, polygon, polyline,
point and multipoint records. The same42 instances are then renamed in ImageJ;
native decoding rejects every edited record. Reference002 is the accepted
corpus and the checked-in fixture is `tests/fixtures/imagej-roi-envelope-imagej154r.json`.
Reference001 exposed ImageJ point behavior: it clears scale-stroke option8192
and materializes zero counter entries. Strict binding correctly reports these
metadata edits as stale. Accepted point inputs explicitly declare options0 and
zero counters; arbitrary native-default point round trips are **not** credited.
A future explicit projection must accommodate that behavior while retaining
original source metadata; do not relax all metadata comparisons to hide it.

Final Build004, Python003 (1691 passed, zero skipped), Native003 (15 plus the
restricted planar host), SDK001 and Reference002 pass. Python regression cases
include original-plane composite masks, legacy policies, wildcard/zero positions,
all six extent dimensions, corrupt binary fields, text/byte caps, trailing data,
external edits and unknown envelopes. The installed SDK consumer exercises the
new envelope API and its stale/rebound refusals. Initial build001 had a duplicate
test-local variable; Python001 assumed mutable ROI fields, and Native001 assumed
native-object equality after wire normalization. Corrected tests check the
wire-precision contract explicitly. All failed attempts remain available.

Source freezes, held before/after files, product/header hashes, raw Java outputs
and cleanup receipts accompany the handoff. Exact owned superseded successful
pytest scratch (7597 bytes) and empty job temps were removed; final and failed
evidence, useful caches and SDK/reference assets remain. No new source archive,
wheel or standalone distribution was qualified. Family status, historical audit
failures and the full original commercially eligible ImageJ/Fiji scope remain
assigned. Continue full native/rebound projections, typed text/image plane
records and atomic complete-batch publication, both ImGui workflows and complete
annotation rendering/editing, then matching release artifacts.

## Native ROI projection envelope increment (2026-10-18)

`build/p06q-roi-native-envelope001/native-envelope-handoff001.json` binds the
version2 increment. Public C++ and Python APIs add
`encode_imagej_roi_native_envelope(mapped, standard_projection, limits)` and
`decode_imagej_roi_native_envelope`; the existing generic envelope decoder
recognizes both versions. Version1 export keeps its unchanged-source contract.
Version2 supports explicit rebound rows and retains the current native
annotation's full native coordinates, selection/row opaque attributes, name,
plane, fixed coordinate and optional T/C, plus all six target dimensions. Source
provenance is retained at the ImageJ byte codec's **wire precision**, not as an
arbitrary exact in-memory DTO serializer.

The sole outer property is `imagejxx.roi.v2: <canonical base64>` and newline.
Payload fields are magic `IJX`/version2, a length-prefixed version1 source
binding, six big-endian target dimensions, length-prefixed native planar JSON,
and length-prefixed explicit standard projection bytes. The existing bounded
planar codec validates native metadata/geometry before serialization and after
parsing. The ASCII payload is preflighted with base64/UTF16 expansion against
the ROI text limit. JSON parsing rejects duplicate keys, depth beyond64 and
more than1M parser events; canonical spelling, closed native schema, exactly
one row, exact extent and complete payload consumption are required.

The original source envelope is admitted before target/native parsing. Only
version1 provenance is permitted, so recursive native envelope chains refuse.
The standard projection must pass typed mapping admission on the current row's
plane/canvas with fixed-axis Z interpretation and legacy position0. Its name,
geometry, known rendering style, T/C and standard Z must encode identically to
the native row after the standard codec's declared quantization. Native opaque
attributes stay in the native payload; they never leak into unsupported ImageJ
fields. Wire-only options, point counters/styles, sidecars, group and original
projection properties are explicitly chosen by the caller and pass existing
typed codec checks. Sidecars inconsistent with current geometry still refuse.
On load, this native/projection equation is checked before comparing the actual
outer standard record to the retained projection. Changed standard metadata or
geometry refuses as stale; successful load returns a validated explicit rebind
with retained source provenance. No implicit XY projection or clipping occurs.

Pinned ImageJ1.54r runs42 actual native-envelope decode/re-encode cycles across
three planes, first/last fixed coordinates and seven geometry kinds. Native
coordinates with extra precision and opaque nested metadata, including uint64
max values and Unicode, survive exactly; all42 actual ImageJ name edits refuse.
The original source is a point record with scale-stroke flag8192 and absent
counters. Explicit point projections use canonical flags/zero counters, while
all original source settings survive unchanged inside the envelope. This
addresses the prior point-normalization obstacle through an explicit projection,
without weakening stale checks. Accepted Reference004 and the checked-in
`tests/fixtures/imagej-roi-native-envelope-imagej154r.json` retain these cases.

Reference001 exposed a separate **remaining projection stability gap**: ImageJ
rectangle float-width arithmetic can change the standard width by one float
unit on an otherwise unedited read/save. Strict native stale detection refuses
that changed binding. The actual input/output is a checked-in negative fixture;
it is not credited as a supported unchanged cycle. The accepted corpus uses
stable standard coordinates while the native coordinates retain extra precision.
Do not interpret42 cases as arbitrary-coordinate ImageJ re-save compatibility.
Reference002/003 additionally caught tuple/list comparison errors in the
reference harness; accepted Reference004 compares normalized point containers.
All failed reference attempts remain available.

Final Build002, Python002 (1767 passed, zero skipped), Native002 (15 plus the
restricted planar host), SDK001 and Reference004 pass. Regressions cover native
precision and opaque metadata across all planes, source/target separation,
original-before-invalid-target admission, complete mask canvases, style/geometry/
name/position disagreements, malformed/duplicate/deep/noncanonical JSON, nested
envelopes, byte/text limits, explicit legacy refusal and external edits. The
fresh installed SDK consumer exercises rebound version2 export, generic decode,
exact native annotation retention and projection disagreement refusal. C++ core,
Python interface and both desktop products rebuild together; this is not new
UI import/render acceptance.

No dependency, restricted feature or imported product code was added. Source
holds, current product/header pins, source attestation, raw Java evidence and
journaled cleanup accompany the handoff. Superseded passing Python001 scratch
(7597 bytes) and empty job temps were removed; final/failure evidence and useful
SDK/build/reference assets remain. No distribution or whole-family completion
is claimed. Continue atomic complete geometric batch import and separate native
text/image plane models, both ImGui workflows and complete annotation rendering/
editing; address standard-projection arithmetic stability and remaining format
variants, then qualify matching source/wheel/SDK/standalone artifacts against
the complete commercially eligible ImageJ/Fiji scope. Historical audit failures,
all99 family obligations and the source901 qualified release boundary remain.

## Atomic geometric ROI import increment (2026-10-18)

`build/p06q-roi-import001/import-handoff001.json` binds the current increment.
The installed `imagej_roi_import.hpp` and Python interface expose immutable
resolved batches, append/replace modes, complete semantic admission from a list
of `ImageJRoiImportItem` records, direct ZIP import and one prepared transaction.
Each item has a source label, complete bytes and an optional mapping. A present
mapping means an ordinary geometric ROI with that explicit context; absent
means a self-describing envelope. There is no guessed XY default. ZIP import
requires exactly one mapping choice per member in container order, clamps ZIP
entry/aggregate expansion limits to the import limits, and processes the entire
member set. No filesystem extraction, skipped member or partial prefix is used.
Text/image annotation records remain explicitly outside this geometric admission
path and refuse rather than becoming processing rectangles.

Resolution preflights all borrowed names/byte totals before input copies and
geometry admission, validates existing native rows and retained source archive,
then admits every member through the existing typed codecs/mapping/envelope
checks. Every mapped target must match all six current document dimensions,
including sample axis S. An explicit rebind must already be represented by the
native envelope; the importer never changes planes, clips masks or fabricates
T/C/Z. Native row precision and opaque attributes survive. Append preserves
existing native rows; replace explicitly replaces the native row set and source
archive after validating the existing provenance. Empty imports refuse rather
than acting as an implicit clear.

The source archive extension is `imagejxx.imagej_roi_import_sources`. Its closed
version1 schema contains an ordered entries list with `name`, canonical base64
`data` and `mapping` (null for envelopes; otherwise complete extent/plane/
coordinate/position policies). Original bytes preserve wire-only groups,
point styles/counters/options, source properties and geometry sidecars that do
not otherwise belong to native processing annotations. Existing archive entries
are preflighted, structurally checked and fully readmitted under their original
contexts before a later import. This archive is **provenance**, not an implicit
association to row indices after edits. Native row edits are never replaced by
re-decoding historical inputs. Renderer/export integration still needs explicit
source associations/current projections; do not apply archive entries to edited
rows by matching index or name. Append retains earlier sources; explicit replace
replaces the archive. The generic metadata transaction API can explicitly clear
an obsolete/corrupt archive; this importer does not silently discard it.

Default limits are4096 entries,8MiB aggregate original bytes,16MiB logical source
provenance and32MiB conservative decoded retention. Configurable hard ceilings
are4096,64MiB,64MiB and128MiB respectively. Native annotation caps still apply.
Source archive preflight also bounds depth64 and1M nodes. Decoded charging covers
existing publication rows plus every new mapped source/current/publication
owner, including points, packed masks, metadata, counters and retained paths/
spline controls, before accumulating each record. Shared masks/metadata are
charged conservatively at logical owners. This bounds retained batch growth;
per-member decoding/raster scratch remains governed by the ROI codec limits.
Python item construction separately caps an owned item at the ordinary codec's
16MiB default byte cap. ZIP buffers are moved into items to avoid a second copy
of all inflated members.

Only successful complete resolution yields a prepared transaction. It publishes
both native-row and source-archive extensions with Pixels|Metadata dependencies.
A concurrent data/metadata change rejects commit; display-only changes remain
compatible. Document validator rejection leaves both extensions/history unchanged
and the transaction active. No document dataset samples are acquired by import;
owned annotation mask rasterization is distinct. Undo/redo restores the entire
batch and its source metadata together. Both extensions persist through native
session save/load; session pixel serialization itself is outside the no-read
claim.

Final Build003, Python003 (1792 passed, zero skipped), Native003 (15 plus the
restricted planar host regression) and SDK002 pass. Regressions include mixed
ordinary/enveloped XY/XZ/YZ batches, first/last fixed coordinates, wildcard
positions, metadata retention, append/replace and prior-source limits, invalid
last members, duplicate labels, missing mappings, sample-axis extent mismatch,
empty input, malformed old provenance, byte/text/decoded caps, row caps, stale
commit and compatible display changes, stored/deflated ZIP complete mapping and
semantic failure, exact source archive session persistence, undo/redo and native
validator rollback. A small-wire pair of1024x1024 mask records verifies aggregate
decoded retention: one fits a500000-byte logical budget, the pair refuses before
publication. Python002's added mask test initially omitted required axes; the
corrected YX fixture is in the passing final run and the failure remains.

One additional test imports all42 prior actual pinned ImageJ native-envelope
outputs as a single batch, verifies every retained native plane/coordinate/
point array, then undoes the whole batch. A stale actual ImageJ edit in the last
member refuses without changing the source. This reuses the qualified prior
ImageJ corpus; **no new Java capture** is claimed for this transaction increment.
The fresh installed SDK consumer includes the new header and exercises two-member
publication, provenance, undo/redo, invalid-member refusal and validator rollback.
Both desktop products rebuild, but no new actual desktop import/render/edit flow
was exercised. Workflow command recording/replay integration remains open.

No dependency, restricted feature or third-party product code was added. Held
before/after source, current product/header pins, source attestation and journaled
cleanup accompany the handoff. Superseded successful Python001 scratch (7597
bytes) and empty job temps were removed; final and failed evidence, useful build
caches and SDK assets remain. The new public header changes future wheel/SDK
member maps; no new distribution is qualified. Next integrate the import
controller and workflow recording, both ImGui file/mapping/preview/publication
flows, separate native text/image plane models and complete mixed annotation
render/edit/export. Explicit source associations, standard-projection arithmetic
stability, remaining format variants and matching distributions remain assigned
across the full commercially eligible ImageJ/Fiji objective. No family or whole
goal completion is claimed; historical audit failures and the source901 qualified
release boundary remain unchanged.

## ROI import workflow and shared controller increment (2026-10-18)

`build/p06q-roi-workflow001/workflow-handoff001.json` binds this increment.
Every resolved import exposes `record()` in C++ and Python. Public native APIs
validate a record without a live document, replay it against its captured source,
or explicitly rebind it with a complete new item set/mode/limits. A shared pure
admission function is used by both ordinary import and recorded equation
validation; no synthetic dataset, plane read or Java process is used to validate
record semantics. Existing atomic geometric import behavior remains intact.

The closed V1 schema is `imagejxx.imagej-roi-import-command`, with exactly
`schema`, `version`, `source`, `mode`, `limits`, `before`, `before_sources`,
`after`, `after_sources` and `destination`. Source includes owner ID, all six
shape dimensions, type/sample policy, revisions and complete calibration. The
record retains every import and ROI resource limit, including zero-valued
optional codec budgets. Destination is `in_place`. Native rows use the existing
bounded planar grammar; absent prior source archive is represented distinctly
as null. For append, the source-input suffix starts after the admitted historical
archive; replace uses the complete resulting archive as its input set. There is
no independent duplicated input list that could disagree with source provenance.

Validation preflights the borrowed record (256MiB logical bytes,4M nodes,depth80),
checks exact fields/types/version/limits, decodes the original row set, admits
historical original sources and reconstructs the proposed complete import from
the retained suffix. Both derived native rows and source archive must match the
reconstructed equation. Historical source prefixes cannot be substituted while
claiming the captured originals. Every old/new source still passes the prior
codec/mapping/envelope/resource rules. Import/archive limits remain unchanged;
the larger record cap accommodates two claimed states and schema overhead.
Original validation completes before current-owner checks or an explicit target
rebind. Replay then requires matching owner, pixel/metadata revisions, complete
descriptor/calibration, native rows and source-archive presence/value. Compatible
display changes do not invalidate it. Rebind validates the original record first
and requires new explicit items/context, never guessing mapping from a target.

Records round-trip through native session workflow metadata. A loaded session
has its own document identity, so explicit rebinding remains necessary; merely
loading a workflow does not authorize applying captured inputs to another owner.
The record is an equation/integrity contract, not an authenticity mechanism.
Pure API validation/replay/rebinding acquires no dataset samples. Session pixel
serialization itself remains outside that claim.

The shared desktop controller adds the strictly parameterized
`replay_imagej_roi_import` action. It admits the original record before action
capabilities, requires the idle active captured source, checks revisions and the
existing visual memory estimate, then uses a dedicated immutable-command
publication method. That method permits exactly the reserved native annotation
and import-source extensions and rejects other document changes/foreign or stale
owners. The existing `commitPlanarAnnotation` path is unchanged and continues to
reject import provenance. Workflow output storage is prepared before document
commit and published only after successful commit. Equal row/archive replacement
commits still pass transaction validation, but add neither history nor workflow
entries. Recording-disabled imports still preserve document provenance.

Final Build002, Python002 (1818 passed, zero skipped), Native002 (15 plus expanded
restricted planar host) and SDK002 pass. Pure tests cover append/replace source
prefixes, before/after equations, wrong fields/types/limits/shape, corrupt/stale
original inputs, original-before-rebind refusal, foreign owner, stale publication,
display-only compatibility, record copies, bounded depth, exact optional-zero
limits and session workflow persistence. Existing42-record ImageJ corpus import
and all earlier codec/mapping/envelope regressions still pass; no new Java capture
is claimed. The installed SDK consumer validates, replays and rejects a forged
import equation through the updated public header/core library.

Programmatic tests exercise the actual shared `_desktop.Editor` controller:
complete row/archive/workflow publication, stale replay refusal, undo/redo,
no-op replacement, recording disabled, unknown action parameters, inactive
scientific destination and visual-budget refusal. Each failure preserves document
and workflow state. The C++ restricted host verifies that the old publication
route rejects an import, the new opt-in publishes both extensions with shared
pixels, stale reuse refuses and undo restores the original state. The C++ app
and Python desktop products rebuild with the same controller route. This is
controller/API evidence: **no new actual rendered UI or standalone interaction**
is claimed, and no file/mapping form or workflow replay widget is added here.
A recorded import already applied to its source normally becomes stale; the
future workflow UI must expose explicit mapping/rebind review rather than a
button that silently reuses or guesses contexts.

Source holds and current product/header pins accompany the handoff. Desktop
before-files were recovered from the last qualified source archive only after
matching their exact hashes to this turn's pre-desktop source freeze. Original
code adds no dependency, restricted feature or third-party product code. Exact
owned superseded successful Python001 scratch (7597 bytes) and empty job temps
were removed; final evidence, source holds and useful caches/SDKs remain. No new
distribution is qualified.

Continue both ImGui file/mapping/preview/publication flows, workflow review and
explicit rebinding widgets, asynchronous/cancellable large imports and end-to-end
matching host artifacts. The shared controller route currently executes
synchronously; resource bounds do not substitute for responsiveness qualification.
Separate native text/image plane models, complete mixed annotation rendering/
editing/export, explicit current-source associations, projection arithmetic
stability, remaining format variants and matching source/wheel/SDK/standalone
qualification remain required across the full commercially eligible ImageJ/Fiji
objective. No family or whole-goal completion is claimed. All99-family obligations,
historical audit failures and source901 qualified release boundary remain active.

## Shared ImGui geometric ROI import panel increment (2026-10-18)

`build/p06q-roi-panel001/panel-handoff001.json` binds the new shared dialog.
File → Import ImageJ ROIs opens it in both the native C++ application and the
optional Python frontend. Users can type a `.roi`/`.zip` path or invoke the shared
file browser, load the complete member list, select append/replace and review
per-member interpretation. Standard ROIs start with no selected plane; native
reserved-property envelopes retain their stored mapping. Explicit slice choices
use positive 1-based UI indices; file-position, scientific-Z/fixed-axis and
legacy fixed-axis/C-fastest policies pass to the existing strict native mapper.
A bulk mapping button copies only explicitly selected choices to standard
members. No member is silently skipped. Native envelopes with incompatible
extents require explicit rebinding rather than automatic geometry changes.

Loading performs bounded regular-file reads in64KiB chunks and enforces the
existing raw/ZIP expansion limits. Header-property preview identifies reserved
native-envelope namespaces without accepting geometry; complete semantic
admission still happens during validation. Both loading and complete resolution
run on one owned `std::async` worker. It receives owned immutable input/snapshot
copies, uses the existing native execution/cancellation scope and never calls
ImGui or publishes document/workflow state. The host thread polls completion
without blocking. Controls disable during active work; Cancel invalidates the
worker generation and token, so discarded results cannot become prepared edits.
The panel destructor cancels and joins its worker. Reopening waits for a prior
canceled worker to drain instead of creating an unbounded worker set.

Validation admits every input and the existing provenance through the qualified
core importer, then checks the visual memory estimate. A successful review shows
import/final counts and the selected record's name/type/plane/slice/T/C. Mapping,
interpretation, path or mode changes invalidate that review. Apply is disabled
until a complete prepared result exists. Publication uses the prior dedicated
shared controller path, rechecks owner/revisions/resources and atomically commits
native rows, raw source archive and optional workflow output. Active document or
pixel/metadata changes discard the draft; display changes do not reinterpret
file mappings. Dialog visibility blocks global editor shortcuts consistently with
other forms. The original restricted annotation publication path stays unchanged.

This review is a **mapping/metadata review**, not a graphical pre-publication
annotation overlay. Post-import native annotations are rendered by the existing
plane-aware layer machinery. The worker is panel-owned rather than a global Jobs
entry. Cooperative checkpoints are implemented, but hard filesystem interruption,
worst-case in-flight cancel latency and global Jobs integration are not qualified
here. The tested Cancel discards a prepared replacement; it is not evidence for
interrupting an intentionally stalled worker. The browser callback is wired to
the existing shared file browser, but this increment's input cases use typed
paths; new Browse interaction and Unicode filename coverage remain assigned.
There are no workflow review/rebind widgets yet.

Actual shared-ImGui input cases run through the C++ executable, Python `_desktop`
and a fresh installed C++ component. They use physical mouse/key/text events for
menu opening, path entry, loading, plane/Z-policy choices, validation, Apply,
replacement cancellation, undo/redo and bulk mapping. Owned fixture setup and
session I/O/concurrent metadata changes are explicitly identified API actions.
Each case has210-or-similar bounded frames and47 observed control activations;
worker completion can vary frame timing. A valid geometric prefix plus malformed
last member refuses without partial publication. Unselected plane and XZ
scientific-Z policy errors leave the document unchanged. Editing a reviewed
mapping disables Apply; explicit fixed-axis policy admits the complete batch.
A source change invalidates a prepared form. All final worker futures are drained.

Independent Python assertions check the original scalar pixel formula for every
saved state, exact native precision/opaque metadata, source-group/properties
retention, before/after publication/history/workflow, complete source archive,
undo/redo and session persistence. They verify imported layers on XZ slice2 and
YZ slice5 (UI numbering), separate nonzero canvas IDs and exact geometry in the
actual ImGui draw diagnostics. They compare both hosts' semantic states, and the
finalizer also compares the installed C++ component. This is real ImGui input and
draw-data evidence; no new framebuffer screenshot/export qualification is claimed.
The fixtures are original native ROI/ZIP observations, not a new Java capture.
All previous pinned ImageJ corpus checks continue to pass in the regression set.

Final Build003, C++ UI002, Python003 (1823 passed, zero skipped), Native002 (15 plus
restricted host), SDK002 and Standalone001 pass. Python003 reads the final C++002
capture explicitly. The first build had a missing initializer brace in new
control diagnostics; the corrected build passes and the failed log remains.
The new panel is shared C++ code, so Python does no image processing and Qt is
not introduced. No dependency, restricted feature or imported product code was
added; the source attestation is
`docs/parity/imagej-roi-panel-commercial-source-review-001.json`.

Exact before-integration source copies, held final source, product/header pins,
raw final inputs/reports and a cleanup journal accompany the handoff. Superseded
successful Python001/002 scratch/captures, C++001 capture and SDK001 generated
prefix/consumer products were removed after final success:106419154 bytes
(about101MiB). Their logs, receipts and source projects remain. Failed Build001,
final Python003/C++002/installed captures, SDK002, current caches and source holds
remain. Cleanup targets were resolved under this exact owned proof root and
checked for reparse points before deletion. No earlier protected historical tree
was enumerated or changed.

The installed C++ component is exercised, but there is no new full source/wheel/
SDK/standalone release qualification or formal parity credit. Continue graphical
pre-publication overlays, workflow mapping/rebinding widgets, global Jobs and
cancellation-latency qualification, separate native text/image plane models and
complete mixed annotation import/render/edit/export. Explicit source associations,
point/overlay wire semantics, remaining formats, projection arithmetic stability
and full matching artifacts remain assigned across the commercially eligible
ImageJ/Fiji objective. No family or goal completion is claimed; all99-family
obligations, historical audit failures and source901 qualified source-release
boundary remain active.

## Graphical import previews and ordinary XY rendering (2026-10-18)

`build/p06q-roi-preview001/preview-handoff001.json` binds this increment.
After complete validation, pending geometric annotations draw on matching linked
XY/XZ/YZ or ordinary XY canvases before Apply. Image-space geometry uses the same
`roiVisual` builder and `drawRoiVisual` path as live native annotations; no second
approximate ROI rasterizer or Python processing path is introduced. The import
worker prepares immutable row/visual bundles under the prior visual resource
estimate, native cancellation scope and captured metadata/extent. Device-space
stroke coverage remains creator-thread cached and follows viewport transforms.
This cache estimate is not a whole-process memory quota; existing canvas/source
storage and transient construction still have their existing lifetime rules.

The panel offers Show prepared annotations and Review on image. The latter
opens a compact review window near the viewport bottom, leaving the image
visible, with Edit mapping choices, Apply and Cancel. Append retains committed
native layers and adds pending layers. Replace temporarily suppresses committed
native layers and shows the proposed replacement set; toggling preview off
restores the committed view for comparison without changing the document.
Unrelated legacy ROIs are retained. Staged rows have no edit handles/pointer
publication, and live native annotation editing disables during active graphical
review. Ordinary import forms temporarily use navigation rather than legacy
selection creation, avoiding accidental edits while reviewing.

Preview ownership/revision and processing-cap checks run before drawing. Mapping
edits, cancellation, source metadata/pixel/owner changes, resource-cap changes or
render failure clear or invalidate the prepared review; Apply still uses the
qualified atomic controller path. A display-only T/C/crosshair change preserves
the prepared mapping and filters actual visibility using the current display,
not a frozen preview channel/slice. Drawing clips to the destination canvas.
Native preview geometry stays immutable after transfer; mutable device coverage
is used only on the creator UI thread. Successful publication drops the staged
cache and the normal committed renderer owns subsequent frames. Raster scanning
now has native cancellation checkpoints, also benefiting controlled image-space
visual preparation. In-flight UI/file-I/O cancellation latency is not qualified
by these checkpoints alone.

This work also closes an ordinary-canvas omission: imported native planar rows
previously had a linked-view rendering path only. Ordinary XY now has a separate
read-only native annotation lane, reusing the existing panel's visual cache and
visibility rules without injecting the rows into legacy processing selections or
legacy ROI-manager storage. Native rows remain visible after Apply/undo/redo
alongside unrelated legacy ROIs. Their editing controls remain in the existing
native annotation workflows; this does not claim a new ordinary native-geometry
editing toolbar. Explicit inactive ordinary document IDs are now rejected when
opening the import form instead of being ignored in favor of the active source.

The orthogonal pixel metadata key now excludes the import source archive as it
already excluded the native planar row extension. Provenance never changes image
pixels. Independent UI checks show pixel extraction build counts remain[1,1,1]
through review/toggle/invalidation and annotation-only Apply, rather than causing
an XZ/YZ resample/upload. Idle preview device coverage also reuses its cache.
This is a targeted annotation/provenance cache correction, not broad performance
qualification for every image-editing operation.

Actual ImGui contract002 cases run in C++ and Python hosts and a fresh installed
C++ component. The expanded driver has about331 bounded frames and69 observed
controls per case, with source/worker scheduling allowed to vary frame count.
Physical controls cover compact review, preview off/on, mapping invalidation,
replacement comparison and cancellation, plus ordinary XY import, native/legacy
coexistence and undo/redo. Controlled fixture display navigation is explicitly
listed as an API action: changing channel or fixed slice removes unmatched pending
layers while leaving review ready, and restoring navigation restores the preview.
Concurrent metadata changes remove it before publication. Typed path, mapping,
review and Apply actions remain real mouse/key/text events.

Independent Python assertions verify exact XZ/YZ pending geometry and canvas IDs,
replacement XY slice3 (1-based), committed-layer suppression/restoration, current
channel/slice filtering, cache reuse and pixel extraction stability. They also
verify ordinary XY pending geometry before any rows/source archive/history change,
then identical committed geometry on the ordinary canvas, a preserved legacy ROI,
unchanged pixels, one undoable native import and undo/redo behavior. Semantic
states match across the two hosts and installed C++ component. The C++ restricted
host constructs the actual image-space preview without an ImGui context, verifies
source/cap binding and deterministically cancels native visual preparation through
the execution scope while preserving the document. This verifies preparation
cancellation; it is not a mid-stall UI cancel-latency benchmark.

Final Build002, C++ UI002, Python002 (1827 passed, zero skipped), Native002 (15 plus
expanded restricted host), SDK001 and Standalone002 pass. All earlier codec,
ImageJ corpus, mapping, envelope, atomic import and workflow/controller checks
remain in that regression run. The source fixtures are original observations;
no new Java capture or framebuffer screenshot/export qualification is claimed.
The graphical evidence is actual ImGui input and draw data. No dependency,
restricted feature or third-party product code was added; the source attestation
is `docs/parity/imagej-roi-preview-commercial-source-review-001.json`.

Held exact before/after source, current product/header pins, final raw reports
and source inputs accompany the handoff. Superseded successful Python001 scratch,
C++001 capture and Standalone001 prefix/capture were removed after final success:
102492129 bytes (about98MiB). Their receipts/logs remain, as do final reports,
SDK001, current caches and source holds. Cleanup targets were resolved under this
owned root and checked for reparse points; no protected historical tree was used.
The installed C++ component is exercised, but source901 remains the last qualified
source-release boundary and no new full release/formal family credit is claimed.

Continue workflow mapping/rebinding widgets, global Jobs integration and actual
UI cancellation/latency qualification, Browse interaction and Unicode filenames,
separate native text/image plane models, complete mixed import/render/edit/export,
explicit source associations, point/overlay semantics, remaining formats and
projection arithmetic stability. Matching full source/wheel/SDK/standalone
qualification and the full99-family commercially eligible ImageJ/Fiji objective
remain active, including the historical audit failures. No whole-goal completion
is claimed.

### Captured import workflow review and explicit destination rebinding

`build/p06q-roi-rebind001/rebind-handoff001.json` binds the next scoped increment.
The C++/Python API `decode_imagej_roi_import_request` first validates the complete
captured command equation, then returns copied retained inputs, mode, limits and
recorded source extent. Append returns only new inputs after the validated prior
archive prefix; replace returns the complete replacement. It does not sample
dataset pixels or provide permission to retarget an envelope.

Workflow displays “Import ROIs” and “Review import on active image...”. The shared
`show_roi_import_record` action requires an idle active destination and starts
original validation on the existing owned cancellable worker. No members become
available from an invalid command. The review form shows current and recorded
TCZYXS dimensions, seeds original mapping choices, retains original inputs and
limits and requires “Use reviewed mappings on this destination” before validation.
The prepared native command and graphical preview still invalidate on edits or
source changes. Apply uses the existing guarded row/archive/workflow publisher.
It adds a new command without changing the captured command or original document.

Explicit confirmation allows reviewed standard ROI mappings to use the current
destination extent. Native envelopes preserve their stored extent and refuse
incompatible targets before publication of any member. Editing a native envelope's
projection/source association remains assigned; this form does not fabricate one.
Captured append prefixes remain validated even when only the suffix is imported.
Switching a member to envelope interpretation clears its explicit bare mapping.

Build001, Native001 (15 plus restricted host), C++ UI001 (existing file-import
regression), C++ Rebind001, Python001 (1833 passed, zero skipped), SDK001 and
Standalone001 all pass. Real mouse input drives workflow review, destination
confirmation, XZ/fixed-axis choices, Validate, Apply and Cancel in both ImGui hosts
and the fresh installed C++ app. Each rebind report retains119 frames/23 controls.
Independent assertions verify no publication before confirmation, complete
forged-command refusal, new destination command identity, unchanged original and
source, raw byte/properties/group and opaque precision retention, cancel after
append review, mixed envelope extent refusal and a bare-only different-extent
import. The latter captured command has an existing two-source archive prefix,
so it also proves suffix-only rebinding through the actual widgets.

Final binding independently compares semantic document/workflow states across all
three hosts, normalizing only owner IDs, and asserts exact XZ/YZ prepublication
draw-layer points, fixed coordinates and nonzero canvas identities. The graphical
evidence remains actual ImGui input/draw data. No framebuffer or new Java capture
is claimed. Existing pinned codec/envelope/ImageJ corpora remain in regression.
No dependency, restricted feature or third-party product code was added; the
owned-source attestation is
`docs/parity/imagej-roi-rebind-commercial-source-review-001.json`.

Exact before holds where available, final source holds, source freezes, current
product/header pins, installed products and raw captures remain. Resolved owned
temporary/compiler consumer intermediates were checked for reparse points and
journaled before deletion (713862 bytes); the SDK consumer executable and project
inputs remain. No protected historical path or unowned `imgui.ini` was touched.

Next complete global Jobs integration and actual UI cancellation/latency tests,
Browse/Unicode interaction, explicit envelope reprojection/source associations,
separate native text/image plane models and complete mixed render/edit/export.
Point/overlay semantics, remaining formats, projection arithmetic stability,
matching full distributions, historical audit failures and all99-family
ImageJ/commercially eligible Fiji obligations remain active. Source901 is still
the last fully qualified source-release boundary. This scoped increment earns
no whole-family or whole-goal completion claim.

### Shared ROI Jobs and actual running/ready cancellation

`build/p06q-roi-jobs001/jobs-handoff001.json` binds the scheduler integration.
The shared import panel no longer owns a separate `std::async` thread. File
loading/member classification, captured command admission and native geometry/
visual preparation submit source-owned, superseding jobs to the existing native
JobScheduler. Worker count, capacity and declared workspace admission now use the
same settings as other image jobs. File reads and native raster/visual preparation
report execution stages through JobContext. Jobs and log exposes source, label,
phase, progress, estimated workspace and terminal error/state. Its private input
diagnostics also retain row labels/progress and current scheduler usage.

Loading/captured admission complete when their fully validated members install
on the creator thread. Geometric validation leaves its job Ready while the
review is shown; Jobs Cancel therefore invalidates an already prepared preview.
Apply calls native Job.complete around the existing atomic row/archive/workflow
publisher. Cancellation wins before publication; once publication begins, the
existing scheduler's Publishing rule applies. No worker accesses Window/ImGui or
publishes data. Source/generation/capability guards and visual budgets remain.
The result cell is owned independently of the panel, synchronized before transfer
and released on cancellation/failure/obsolete generation. Destruction cancels and
waits for the current job; Window poll, bounded wait_jobs and wait include ROI work.
The Ready preview is an editable draft and does not make Window.busy true.

Reservations are declared estimates for running work: load/admission reserve64MiB,
and validation reserves the current visual-processing cap. Retained documents,
drafts, command data and caches are outside this estimate. This adds scheduler
admission and visibility, not a whole-process memory/RSS quota. Native import,
wire/provenance and visual budgets still independently admit their own payloads.
Existing scheduler queued/running cancellation semantics are reused; a dedicated
queued ROI physical UI case remains required.

The new `--roi-import-jobs-ui-case` / `_roi_import_jobs_ui_case` runs actual ImGui
mouse/key/text input in the C++ and optional Python hosts. Owned input is a128x128
image and32 standard4096-vertex polygons. The driver observes Running with actual
native “Area polygon rasterization” stage/checkpoints, then physically clicks the
matching Jobs row's Cancel. It retries and physically clicks the form's Cancel
during another running preparation. No injected worker gate or sleep is used to
hold production work. A small import then reaches Ready, is canceled through Jobs,
revalidated and applied; its final job is Completed and it records one undoable
row/archive/workflow edit. An invalid ROI produces a global Failed row without
changing the successful state. Each case ends with zero running/queued/reserved/
retained scheduler workspace, and disabled terminal Cancel controls.

Build002, Native002 (15 plus restricted host), C++ file UI001, C++ workflow
Rebind001, C++ Jobs002, Python001 (1837 passed, zero skipped), SDK001 and installed
Standalone001 pass. Each new Jobs case retains183 frames/39 controls. Independent
assertions cover raw event delivery, enabled nonzero hit identities, Running at
mouse press before cancellation, actual native stages, cancel_requested/terminal
Canceled, no annotation/archive/history/workflow/pixel changes from canceled work,
Ready cancellation disabling Apply, distinct successful retry identity, failed
Jobs rows and released reservations. Final binding compares exact semantic
document/workflow states across all three hosts after normalizing owner IDs.

Mouse release through native finalization and two observation frames measured
at most18.9389ms across these owned cases (test limit2000ms). This qualifies this
actual native polygon cancellation route through HeadlessInputSession, not visible
OS-window scheduling, arbitrary providers, blocking file IO or platform-wide tail
latency. File reads still require the OS read to return before their next bounded
checkpoint; stalled-read cancellation/destruction latency remains unqualified.

The first C++ Jobs run completed its scenarios but exceeded the64MiB report bound
because it retained unrelated full editor diagnostics on every frame. The final
producer retains actual input, ROI form, Jobs table and host controls, preserving
the same limit. Its failed receipt/logs/input fixtures, exact reconstructed/pinned
compiled probe and repair receipt remain. A subsequent evidence-script line-ending
assertion repair is separately recorded; it changed no product source. No failed
evidence was rewritten or removed.

Source holds, final product pins, installed products, raw reports and the current
cache remain. Verified owned empty temporary directories and SDK compiler
intermediates were journaled before removal (713454 bytes); the SDK executable and
inputs remain. No protected historical root or unowned `imgui.ini` was inspected
or changed. No dependency, restricted feature or third-party product code was
added; the attestation is
`docs/parity/imagej-roi-jobs-commercial-source-review-001.json`.

Continue Browse/Unicode interaction, queued ROI UI and stalled-file-IO cancellation
qualification, explicit envelope reprojection/source associations, native text and
image plane models, complete mixed render/edit/export, point/overlay semantics,
remaining formats and projection arithmetic stability. Matching full distributions,
historical audit failures and all99-family commercially eligible ImageJ/Fiji
obligations remain active. Source901 is still the last qualified source-release
boundary; no whole-family or whole-goal completion is claimed.

### Source-bound Browse and supplementary Unicode input

`build/p06q-roi-browse001/browse-handoff001.json` binds actual Browse interaction.
An immutable file-choice request retains the source snapshot, draft generation
and current path. The shared picker checks the active owner, Pixels/Metadata
revisions, generation, idle state and file-import mode before accepting a choice.
Source changes/reopening expire the modal; acceptance checks again. Cancel keeps
the prior prepared command/preview and its Ready job. Choosing a different file
invalidates that review before any load. No load or publication occurs from Browse
alone. The existing generic file picker now supports optional prompts/extensions;
ROI mode enables `.roi`/`.zip` filtering with case-insensitive extensions and keeps
folders visible during a text filter. Unsupported typed extensions disable Open.
File labels draw unformatted, and IDs hash the complete raw path instead of
treating literal `##`/`###` as ImGui label syntax or using unstable row indices.

The first actual case exposed supplementary Unicode replacement in the external
16-bit ImGui build: typing an emoji folder produced U+FFFD and enumerated the
wrong parent. The fixture was preserved unchanged. The exact upstream archive
already identified by the SDK SPDX record was acquired and verified against its
SHA512. Nineteen selected unmodified core/docking, std::string helper, GLFW/OpenGL3
and required header/license files now live in `third_party/imgui`, with a complete
SOURCE.json/SHA256SUMS manifest. Both hosts and all backends compile these sources
with the same `IMGUI_USE_WCHAR32` definition. CMake checks all selected file hashes
before compilation and refuses a conflicting preexisting ImGui target. The core
SDK remains independent of the desktop dependency. Source distributions include
the pinned payload and license; no external ImGui binary or new font is bundled.

The wide-input case then exposed a form/modal title collision during double click.
The chooser now has the distinct title **Choose ROI file**. File identities are
stable after insertion/reordering, including names sharing a literal `###` suffix.
The first two failed receipts/logs/fixtures remain, along with the exact first
probe and held compiled wide transition inputs. Old cache binaries were
superseded by repairs and are not claimed as retained products or release evidence.
The commercial-source attestation explicitly distinguishes original owned changes
from the unmodified MIT upstream payload:
`docs/parity/imagej-roi-browse-commercial-source-review-001.json`.

Build003, Native002 (15 plus expanded restricted host), C++ Browse003, existing
file/rebind/Jobs cases, Python001 (1876 passed, zero skipped), SDK001 and installed
Standalone001 pass. Each Browse case retains245 real-input frames/49 controls.
Independent assertions cover case-insensitive files, folder/text filtering,
noncolliding literal-hash labels, stable identities after an owned file insertion,
single-row/Open and double-click selection, prepared-draft preservation on Cancel,
Unicode filesystem reads and ZIP member names, original source bytes/properties/
groups, no changes before Apply, complete one-step publication, undo/redo, folder
navigation/Up, unsupported typed paths and generic Open/Cancel. Active-owner,
metadata and same-owner draft-generation changes close the chooser with no open
popup or stale path injection. Expanded native host checks cover token reuse,
reopening, a different owner, metadata changes and disallowing Browse in workflow
mode. Existing license-policy tests plus eight new mutation cases reject NC,
archive/manifest changes, altered/missing/extra source and traversal. Final binding
compares exact semantic states across both hosts and the installed C++ app after
normalizing owner IDs. The pinned notices match byte for byte.

Owned compiler intermediates and empty scratch directories were checked and
journaled before deletion (713862bytes). Final and failed evidence, the upstream
archive/provenance, installed products and active cache remain. No font assets or
restricted features were added; correct Unicode text/IO is qualified, full glyph
rendering and OS-native file dialogs are not. Queued ROI physical UI and stalled
file-IO latency, envelope reprojection/source associations, native text/image
models, mixed editing/render/export, point/overlay semantics, remaining formats,
projection arithmetic stability, matching full releases and historical audit
failures remain assigned. All99-family ImageJ/commercially eligible Fiji scope
remains active; source901 is the last fully qualified source-release boundary.

### Queued import cancellation, replacement and direct captured-input retry

`build/p06q-roi-queue001/queue-handoff001.json` binds the next scoped lifecycle
increment. A queued form can now be reopened immediately: cancel retires the
queued task, poll consumes the terminal result and the new draft receives a fresh
generation. If the job races into Running, the existing nonblocking wait/refusal
path remains. A cancelled/failed captured-input form now offers **Retry captured
inputs**, calling the same full original-command admission helper as its initial
load. No original byte, workflow command or destination changes from admission.
The checked **Jobs and log** menu item now toggles visibility; its keyboard
command retains open/focus behavior. Previously the menu left the panel open and
covering short import forms even after a purported close click.

The fixed native queue producer uses a declared generic scheduler job reserving
the configured workspace budget. That job has a15s watchdog and RAII cancellation/
release cleanup. This is intentional native scheduler contention, not a stall
injected into ROI processing or a simulated blocked filesystem read. Each ROI
ticket is observed Queued with zero checkpoints, no acquired reservation and zero
peak reservation before real mouse actions cancel it. Cases cover file loading
through Jobs, validation through the form, source closure through the Window
action, reopening through the actual File menu, and captured admission through
Workflow/Jobs. Releasing contention cannot install obsolete input or annotations.
The closed source's held snapshot remains identical. A successful retry applies
one import; direct captured-input retry creates a distinct admitted job and
exposes the original member without publishing another edit. All five fixture
jobs finish Completed, and all final scheduler usage is zero.

Build006, Native004 (15 plus restricted host), C++ Queue005, existing file/rebind/
Jobs/Browse003 captures, Python003 (1880 passed, zero skipped), SDK003 and installed
Standalone003 pass. Both ImGui hosts and the installed C++ app retain209 input
frames/41 controls each. Independent assertions check raw delivery, nonzero enabled
hits, native queue/reservation/checkpoint states, cancel ownership, original data/
rows/source archive/history/workflow preservation, queued owner closure, clean
replacement after release, direct retry and exactly one successful publication.
Final binding compares exact semantic states after normalizing owner IDs.

Failures remain part of the evidence. The initial test driver called the native
Document ID field as a function; its failed build and exact source remain. The
checked-menu defect first surfaced as a covered Load click; subsequent diagnostic
capture identified the still-visible Jobs panel. Independent Python assertions
then caught a retry that clicked a covered workflow button without executing.
The form's new direct retry is exercised and requires a populated admission result.
The broader run also observed the older natural polygon fixture already Ready
before the observer could see Running. Its assertions were retained; the fixture
now performs longer real work with eight8192-vertex polygons on a32x4096 canvas.
It still requires actual rasterization/Running at press, no injected ROI gate,
the same2s release-to-finalization bound, unchanged pixels and no resource leaks.
The failed Python001/002 trees, affected C++/installed observations, logs/receipts
and exact affected sources remain. They were not relabeled as accepted outputs.

The new queue producer also writes a bounded failure observation on exceptional
unwind. Its scheduler access is private to the fixed producer; no generic Python
scheduler injection API was added. No dependency, restricted feature, third-party
product code or font was added. The owned-source attestation is
`docs/parity/imagej-roi-queue-commercial-source-review-001.json`. Verified owned
superseded green captures/SDK products and compiler intermediates were journaled
before deletion:183170537bytes (about175MiB). Final/failure observations, affected
installed binaries, source holds and the active cache remain. No protected
historical tree or unowned `imgui.ini` was used.

### Next envelope-rebinding contract (planned, not implemented)

The current import item admits bare geometry with a mapping or an immutable
envelope. It cannot express a new native row/extent while retaining the exact
original envelope bytes. The next core change must add an explicit complete native
replacement specification (target TCZYXS extent plus native planar row), mutually
exclusive with a bare mapping. Validate the original envelope first, then use
native mapping rebinding; do not re-encode a new envelope and pretend those
rewritten bytes were the imported source. Preserve opaque native attributes,
double precision and original wire metadata; mismatched mask canvases refuse
until the user explicitly supplies a compatible replacement.

Version the closed source archive and command equation to carry this additional
choice while retaining exact original transport bytes. Keep V1 records unchanged
and readable; emit V2 only when the new choice or a V2 before-state requires it.
Append must re-admit the full historical prefix and expose only the new suffix;
replace must retain its complete explicit replacement. Charge retained replacement
rows/metadata at their logical owners within existing resource limits. Validate
the original command before current-source lookup/rebinding and preserve atomic
row/archive/workflow publication, no-op suppression, undo/redo, cancellation and
source revision checks. Qualify C++/Python and installed SDK behavior before adding
the UI's explicit destination/plane/slice/T/C replacement controls and preview.
Later source-to-current-row associations must be explicit; raw archive entries
never acquire an implicit row-index binding from this transport extension.

Stalled-file-IO/destruction latency, complete Unicode glyph rendering, native
text/image models, full mixed edit/render/export, point/overlay semantics,
remaining formats and projection arithmetic stability remain assigned. Matching
full source/wheel/SDK/standalone releases, historical audit failures and all99
ImageJ/commercially eligible Fiji families remain active. Source901 is still the
last qualified source-release boundary; no whole-family or whole-goal completion
is claimed.

### Native replacement API with original transport and versioned equations

`build/p06q-roi-replacement001/replacement-handoff001.json` binds implementation
of the preceding core contract. `ImageJRoiNativeReplacement` contains the complete
target TCZYXS extent and planar annotation. An import item has an optional
`native_replacement`, mutually exclusive with its bare mapping. C++ aggregate
initializers with the original three fields and Python three-argument constructors
remain valid. The Python replacement's extent/annotation and item properties are
read-only copies. Complete original envelope decoding and retained-source mapping
validation precede replacement geometry/extent admission. Nothing modifies or
re-encodes the original ROI bytes. The native mapped record retains its original
wire/source mapping and separate current native row/target extent.

Source archive V2 retains `name`, original base64 `data`, `mapping` and
`native_replacement`. The latter is null for unchanged old/bare entries, or an
exact closed `{extent, annotation}` object using the existing native planar row
codec. All six target dimensions remain explicit. At least one replacement is
required for V2; unnecessary/malformed source upgrades refuse. New bare-only
archives remain V1. Append can promote a V1 prefix without changing its three old
fields or source bytes; replace fully admits the old archive before discarding
it and can emit a bare V1 archive. Historical archived choices remain provenance,
not automatic associations with current rows after later edits.

The command reader validates V1/V2 closed equations and requires the canonical
version: V2 if either its before or after archive is V2, otherwise V1. Changing a
V1 record's version alone still refuses. Every historical original/choice is
admitted before suffix interpretation; reconstructed rows/archive must equal the
captured after-state. Native record validation/replay/request extraction/rebinding,
session transport and the shared desktop replay/no-op publisher use the same
logic. New replacement rows are charged at logical input, mapped and publication
owners, including borrowed Python items before vector copies. The extracted
suffix is budgeted before retaining each replacement. Historical entry counting
validates one original at a time instead of constructing an unused whole-prefix
item vector; the before/after equation still validates that complete history.

Build004, Native001 (15 plus restricted host), Python001 (1907 passed, zero
skipped), SDK001, API001, existing C++ file/rebind/Jobs/Browse/queue UI001 and
installed Standalone001 pass. The installed SDK's expanded ROI test imports an
envelope into a different XZ target, preserves its original metadata/bytes,
validates V2 replay, rejects a version downgrade and verifies undo/redo/no-op
without pixel reads. Twenty-seven new Python cases cover all three planes,
exact original transport/source metadata, opaque uint64/null attributes and
native precision, V1-prefix promotion, V2 suffix extraction, return-to-V1 replace,
malformed fields/version/extent/rows/bytes, corrupted historical replacements even
under replace, original-before-bad-target refusal, all six dimensions, late-member
atomic refusal, constructor types/budgets, new-owner rebind, controller no-op and
session persistence. A25-entry provenance history with current rows deleted
remains replayable under a16KiB decoded-retention cap because only the new suffix
is retained. Dataset observers remain unchanged during import/replay.

`api001/native-replacement-api001.json` captures a concrete V1-envelope import
with an explicit native point in XZ on a different extent. It retains original
wire/base64/SHA256, both extents, group/properties, complete V2 command/archive,
double coordinates, opaque attributes, one undo step, shared pixels, exact module
origins and zero plane/region reads. Independent final binding checks these
values. The previous UI captures are regressions; no actual replacement-control
interaction or new Java reference capture is claimed. One evidence-finalizer
attribute-path assertion was corrected in a new script/repair receipt; product
sources/tests did not change from that correction.

Exact before/final source holds, SDK/header/core/executable pins and raw evidence
remain. Verified compiler intermediates/empty scratch were journaled before
removal (767598bytes). No protected historical tree or unowned `imgui.ini` was
touched. No dependency, restricted feature, font or third-party product code was
added; the attestation is
`docs/parity/imagej-roi-native-replacement-commercial-source-review-001.json`.

The next packet must add shared ImGui controls for complete native replacements
and preview, using the new specification without rewriting source bytes or
inferring any archived row index. Explicit source/current-row associations remain
separate. Native text/image models and full mixed render/edit/export, stalled IO,
point/overlay semantics, remaining formats and projection stability remain
assigned, alongside matching full releases, historical audit failures and all99
commercially eligible ImageJ/Fiji families. Source901 remains the last fully
qualified source-release boundary; no whole-family or whole-goal completion is
claimed.


### Shared native replacement form and preview (2026-10-18)

The next scoped increment is implemented. Both Dear ImGui hosts expose **Edit
native replacement...** on envelope members. A scheduler worker fully admits the
original envelope and any captured/staged replacement before seeding the form.
It parses the immutable destination annotation list once off the UI thread;
frames reuse that list. Original source bytes, source metadata and historical
command equations remain separate from current native geometry.

The form requires **Use this destination extent** and edits plane, one-based
fixed slice/T/C or wildcard membership, name, box center/size/angle, vertices and
stroke/fill/width. It can explicitly load a complete annotation from the current
image. Untouched geometry and style are not rebuilt, preserving exact native
coordinates and opaque attributes. Geometry changes retain opaque selection/row
attributes. Packed masks retain their canvas; wrong plane dimensions refuse.
Selecting Keep current geometry is harmless. Stage vertex preserves a pending
vertex before choosing another; Keep includes the current vertex. Discard closes
the editor without replacing the previous staged or captured choice.

The parent form disables competing draft edits while the editor is open. Keep
stages a separate ImageJRoiNativeReplacement; Validate/preview/Apply use the same
bounded resolver, visual admission, Ready job and atomic publisher as other
imports. No source bytes are re-encoded. Reset removes only a new override;
captured V2 choices remain immutable input. Workflow review seeds its captured
replacement and still requires the parent destination confirmation. Source
revision/owner changes and import cancellation release the private editor/list.

`build/p06q-roi-replacement-ui001/replacement-ui-handoff001.json` binds Build004,
Native002 (15 plus restricted publisher host), all prior C++ file/rebind/Jobs/
Browse/queue UI002, replacement UI003, Python002 (1911 passed, zero skipped),
SDK001 and installed Standalone001. Four new independent Python checks cover
actual controls in both hosts. Final replacement cases record352 C++/installed and354 Python raw-input
frames, each with68 activated controls; the installed executable produces the same
native metadata. The observations include different-extent refusal, missing
confirmation, exact XZ point coordinates/T/C/name, in-image preview before any
mutation, one atomic apply, undo/redo, captured V2 seed and discarded edit,
rotated rectangle center/half axes/style, original group/Unicode properties,
unchanged raw file bytes, shared pixels, explicit destination mask loading,
wrong-canvas refusal, compatible YZ mask preview and cancellation without changes.
All historical import regressions remain green. No new upstream reference capture
or installed Python wheel is claimed in this increment.

The commercial-source attestation is
`docs/parity/imagej-roi-native-replacement-ui-commercial-source-review-001.json`:
original project-MIT integration/tests, no dependencies, imported third-party
product code, fonts or restricted features added. Before/final source holds,
final raw observations, receipts, SDK/desktop products and useful cache remain.
Only exact owned superseded green captures, obsolete test scratch, compiler
intermediates and empty temporary directories were removed after journaling
(310969726bytes). No protected historical root or unowned imgui.ini was touched.

The next bounded packet should resolve explicit source/current-row pairing
before adding export controls:

1. Expose bounded, fully validated historical source inspection without treating
   archive ordinals or duplicate names as persistent row identities. Original
   transport and stored replacement choices remain immutable provenance.
2. Define an explicit pair request with a frozen owner/revision, chosen source
   member and chosen current native row, plus guards against source replacement,
   row reorder/deletion and ambiguous duplicates. Admission must precede any
   projection/export. Rebinding to another owner requires fresh explicit choices.
3. Qualify the pure native API, Python interface and replay/refusal equations
   first, including opaque attributes, exact bytes, budgets and no pixel reads.
   Then add shared UI source/row choices and projection preview. Do not silently
   infer a pairing from archive and row indices or matching names.

Native text/image models and full mixed rendering/editing/export, mask painting,
remaining point/overlay semantics, formats, projection stability and stalled-file-
IO remain separate work. The new worker-seed phase uses the common cancellation
mechanism but its running cancellation is not independently qualified here.
Matching full releases, historical audit failures and all99 families remain
active; source901 remains the last fully qualified source-release boundary.
No whole-family parity credit or whole-goal completion is claimed.


### Explicit source/current-row pairing API (2026-10-18)

The next native increment implements the catalog and explicit pair contract.
`include/imagejxx/imagej_roi_pairing.hpp` exposes immutable source summaries,
ImageJRoiSourceCatalog and ResolvedImageJRoiPairing. Inspection admits the whole
V1/V2 archive before returning ordinal/name/byte-size/mapping/replacement summaries.
Resolve requires a source-member index and an independently chosen current-row
index in the catalog's frozen snapshot. Duplicate names are allowed and never
match a row automatically. Only the selected original item is retained; its raw
bytes and historical native replacement remain unchanged. The mapped result
rebinds the original source basis to the complete chosen current native row and
all six actual target dimensions, preserving double coordinates and opaque data.

The closed `imagejxx.imagej-roi-pairing` V1 record has exactly schema, version,
source descriptor, limits, annotations, sources, source_index and annotation_index.
It captures both complete input lists; indices have meaning only in those frozen
inputs. Decode admits every historical original/choice before using an in-range
selection. Replay validates that record before looking up current state, then
checks owner, Pixels/Metadata revisions, all dimensions/type/policy/calibration
and exact rows/archive. Changed, reordered, deleted or replaced inputs refuse.
Display-only changes remain valid. Explicit rebind validates the original record
first and requires both fresh choices on the new snapshot. No document metadata
extension, automatic persistent link or ordinal identity was introduced.

The existing bounded import machinery now exposes shared private limit codecs,
source summaries and selected-pair admission. Captured native rows are charged
before decoding; selected stored replacement metadata is borrowed and preflighted
before its owned native copy. Original transport, selected replacement, mapped
record and native row owners remain accounted. Source-history admission retains
one decoded original at a time. An eleven-entry archive with ten65KiB historical
replacement attributes and a single small current row qualifies selection of its
first bare source under a16KiB decoded budget; selecting a large stored replacement
refuses under that same cap. No unselected suffix item vector is constructed.

`encode_imagej_roi_pairing_envelope` requires the current snapshot, resolved pair
and an explicit standard ImageJRoi projection. It revalidates the captured pair
against current state, then calls the existing V2 envelope encoder, including
wire-precision geometry/style/name/position agreement and caller-chosen wire-only
metadata. It returns bytes, writes no file and does not mutate image/history/
provenance. C++ and Python expose the same contract. A pairing record captures
selection choices; the separate output projection is not yet an export workflow
record or a published file.

`build/p06q-roi-pairing001/pairing-handoff001.json` binds Build003, Native003
(15 plus restricted publisher host), Python004 (1940 passed, zero skipped),
SDK001, API001, all prior C++ import UI002 and installed Standalone001. Twenty-nine
new Python cases cover duplicate labels, source0/current-row1 pairing, precise
native/opaque preservation, closed-record tampering, original-before-bad-index
refusal, row edit/delete/reorder, source reorder/replacement, pixel/metadata stale
refusal, display-only replay, explicit new-owner rebind, strict Python indices,
empty catalog, input/retention limits, V2 historical choice versus edited current
row, selected-only retention and compatible/mismatching projection export.
The expanded installed SDK test includes the new public header, catalog/explicit
pair API, record validation/replay, range refusal, new-owner refusal/rebind and
unchanged native data/read observers.

`api001/native-pairing-api001.json` captures one retained bare source paired with
current row1 in XZ, native precision and opaque uint64/null attributes, original
and output bytes/base64/SHA256, complete selection record/archive, source group7
versus explicit projection group9, two existing undo steps, shared data and zero
plane/region reads. Independent final binding parses ROI property offsets, V2
payload, nested V1 source binding and standard-projection properties to verify
exact original bytes, complete current native row and caller-chosen projection.
SDK/header/core/executable/module origins and source freezes bind the final work.

Initial native/Python probes exposed a record-reader error that treated zero
indices as positive sizes; a dedicated nonnegative addressable index reader fixes
it. Two later test assumptions were corrected: projection properties live inside
the envelope's standard binding, and a deliberate test pixel replacement performs
its own read before replay. Replay checks compare counters after that mutation;
read-only catalog/pair/export observations remain zero. Those failed receipts and
fixtures remain, with all final gates green. No failure or old audit was suppressed.

The commercial-source attestation is
`docs/parity/imagej-roi-explicit-pairing-commercial-source-review-001.json`:
original project-MIT code/tests, no dependencies, fonts, third-party product code
or restricted features added. Exact owned superseded green capture/test scratch,
SDK compiler intermediates and empty temporary directories were journaled before
removal (294898242bytes). Failures, final raw observations/fixtures, receipts,
source holds, installed SDK/desktop/consumer and useful cache remain. Protected
historical roots and unowned imgui.ini were not touched.

Next implement shared ImGui source-member and current-row choices without a
preselected inferred pair. Resolve on a worker, show the paired native preview,
require explicit compatible projection/wire metadata choices, revalidate on
export and use the existing bounded file transport. Captured export workflow
semantics and failure/cancel behavior must be qualified alongside both hosts.
Native text/image models and mixed annotation workflows, mask painting, remaining
formats/point/overlay/projection qualification, stalled IO, matching full releases,
historical audit failures and all99 families remain assigned. Source901 remains
the last fully qualified source-release boundary. No pairing UI, installed Python
wheel, new release, whole-family parity credit or whole-goal completion is claimed.


### Shared explicit pairing, projection review and file export (2026-10-18)

Both ImGui hosts now expose **File → Export paired ImageJ ROI...** through the
shared RoiExportPanel. Source member, current native annotation and standard
metadata choice all start unselected. The catalog/current rows are admitted once
on a scheduler worker; source and annotation choices are independent even for
duplicate labels. The form requires explicit native fixed-axis/file-Z semantics
and legacy-position clearing. Fresh standard metadata starts from defaults;
retained metadata starts from the admitted original. An explicit reset clears
source geometry tags/sidecars, point counters/types/sizes and option flags while
retaining group/properties/overlay metadata. Group and properties have independent
overrides. Retained incompatible tags refuse rather than silently changing them.

Preparation resolves the explicit native pair, builds the matching standard
projection, encodes its native envelope and admits/builds the selected visual on
a worker. Native double coordinates, row/selection opaque attributes, all six
extents and exact original transport remain separate from the reviewed standard
projection. Review on image uses the shared clipped linked-plane visual with an
export label; ordinary/orthogonal editing is blocked during this private review.
Import/export forms cancel each other. Processing-cap, source owner/data/revision
changes discard stale prepared work. The save picker reuses the bounded generic
browser, .roi filtering and request-generation/source guards. Browse Cancel keeps
the current prepared review; output-path changes clear overwrite consent.

Native atomic-file transport now also provides a private StagedAtomicFile owner.
A worker reserves an exclusive sibling, writes with checkpoints, flushes and
closes it. It never publishes the destination. UI harvest retains that staged
owner until the source guard and scheduler Job.complete publication boundary.
The final rename is the last fallible file operation; no callbacks run after it.
Existing atomic_replace_file callers retain replacement semantics. The new
no-replace path uses MoveFileEx without REPLACE_EXISTING on Windows and link on
POSIX; the latter branch has not been runtime-qualified here. Abandoning a staged
owner removes only its exclusive sibling. Explicit replacement remains required
when an existing file or a competing late-created destination is encountered.
File write/flush is off-thread; stalled IO and final-rename latency remain separate
qualification. This is not a whole-process memory or responsiveness guarantee.

Ready cancellation and source change before UI publication discard the sibling.
Filesystem diagnostics are rendered from numeric error codes and UTF-8 paths;
Windows filesystem_error.what() can contain locale bytes for Unicode paths and
must not be copied directly into JSON diagnostics. The destination-race fixture
exposed and qualified that correction. A first retained-metadata fixture also
confirmed that a rectangle source type cannot silently become a point projection;
the form now exposes the explicit geometry/options reset used for that change.

`build/p06q-roi-export-ui001/export-ui-handoff001.json` binds Build006, Native002
(15 plus restricted publisher host), the independent preferences/atomic-file
regression Atomic002, SDK002, Python002 (1943 passed, zero skipped), all prior C++
import UI002, Export UI006 and installed Standalone002. Three new independent
Python tests consume real shared UI input in both hosts. Final C++/Python/
installed captures each use243 input frames and48 activated controls, with
bounded raw events, observations and output files retained. They qualify blank
independent selectors, missing confirmation/metadata refusal, invalid group,
incompatible retained source-type refusal and explicit reset, group/property
overrides, exact XZ point/opaque state, linked-plane preview, Unicode/literal-hash
paths, Browse Cancel, actual new-file export, unconfirmed overwrite refusal,
confirmed overwrite, fresh metadata, Ready-job cancellation, source change while
staged, a late destination race and cleanup of all owned siblings. The producer
explicitly declares fixture API actions for holding the UI pump at a staged Ready
boundary, cancelling that scheduler job, changing title and creating the competing
file; these are not claims of physical Jobs Cancel or naturally stalled IO.

The initial observer assertion incorrectly counted live viewer reads as export
reads. Final observations start counters before the validation/export input and
hold the UI pump through worker wait/harvest. Counts remain unchanged across the
actual preparation/staging boundaries, while total viewer counters remain nonzero.
Both exported and overwritten states share the exact original document data,
three existing undo steps and an unchanged empty workflow. A deliberate fixture
title edit adds the fourth undo step only after staging the refused stale file.
Failed earlier receipts/fixtures remain, including the Unicode diagnostic error
and the earlier observer assertion. No failure or historical audit was suppressed.

Independent final binding parses ROI UTF-16 property offsets, the V2 native
payload, nested V1 source binding and reviewed standard projection. C++, Python
and installed exports are byte-identical; retained original bytes match source0,
current row1 preserves exact doubles and opaque fields, and the explicit outer
group/property choices remain separate from original source metadata. SDK/core/
executable/module origins, complete source freezes and before/final holds bind the
work. The legacy preferences codec/recovery/Unicode/backup/atomic replacement
regression passes against the changed core. No new Java reference capture or
installed Python wheel is claimed.

The commercial-source attestation is
`docs/parity/imagej-roi-shared-export-ui-commercial-source-review-001.json`:
original project-MIT panel/transport integration and tests, no dependencies, fonts,
third-party product code or restricted features added. Exact owned superseded
green captures, SDK/standalone products and compiler intermediates, plus empty
temporary directories, were journaled before removal (218786251bytes). Final
raw observations/files, failures, receipts, source holds, installed SDK/desktop/
consumer and useful cache remain. No protected historical root or unowned
imgui.ini was touched; the legacy window source's line-ending style was preserved.

Export currently leaves workflow unchanged. Next implement a closed typed export
record with the fully admitted pairing, explicit standard projection and output
equation; validate the original before review/replay, require explicit destination
and overwrite choices, and reserve any workflow publication before the final file
boundary. Then expose reviewed replay/rebind in both hosts and qualify failures,
cancel/source changes and output integrity. ZIP/multiple-member export, picker
save/stale/keyboard and running/stalled-IO qualification, native text/image models,
mixed workflows, remaining point/overlay/formats/projection semantics, matching
full releases, historical audits and all99 families remain assigned. Source901
remains the last fully qualified source-release boundary. No export workflow
capture, full ROI interchange, whole-family parity credit or goal completion is
claimed.


### Captured export equation and reviewed workflow publication (2026-10-18)

`include/imagejxx/imagej_roi_export.hpp` exposes bounded export limits,
ResolvedImageJRoiExport, validation/request extraction, same-source replay and
explicit rebind. The closed `imagejxx.imagej-roi-export` V1 record contains exactly
schema, version, pairing, limits, projection and output. It is path-free and does
no file IO. Original pairing/history admission precedes projection/output and new
owner interpretation. Projection decode/re-encode and base64 must be canonical;
encoding the admitted pair with that projection must equal the captured output.
Unknown fields/versions, stale owners and mismatching output refuse. Fresh rebind
requires explicit pairing/projection after full original validation.

Logical record storage defaults to64MiB with a128MiB hard ceiling. ROI wire fields
have independent bounds. Borrowed metadata traversal charges the skeleton and
exact base64 lengths before their allocation. An independent Python32-byte-node/
UTF-8 ledger qualifies its exact accepted boundary and one-byte-below refusal.
This is logical retention, not whole-process RSS. Mutable Python projections are
owned under the GIL before native work releases it; rebind validates the original
before copying its new projection. The direct pairing-envelope binding received
the same ownership correction.

The shared panel prepares a typed command and stages its exact output. Before
file publication, Window verifies the idle active frozen source and constructs/
reserves OutputPublication for the workflow value. After the final rename, its
workflow move is noexcept and allocation-free. Malformed workflow data refuses
before replacement and leaves destination bytes unchanged. Recording off writes
the reviewed file without appending. Neither route changes document rows/archive
or adds undo steps. Generic workflow/batch execution directs captured exports to
individual review; destination and overwrite choices are never taken from a record.

Workflow renders Export ROI and **Review export on active image...**. A worker
validates the complete captured equation before exposing current catalog/rows.
Source, row, metadata and output path start blank; overwrite and destination
confirmation start unchecked. Captured projection metadata is an explicit template
choice; the new row supplies geometry/name/position. Incompatible tags require an
explicit reset. Retry inspected inputs repeats full original admission. Original
commands remain unchanged. **Go to annotation plane** sets the chosen fixed-axis
crosshair and specific T/C, enabling linked views without source-data mutation.

`build/p06q-roi-export-command001/export-command-handoff001.json` binds Build005,
Native002 (15 plus restricted host), preferences/atomic regression Atomic001,
SDK001, Python003 (1973 passed, zero skipped), all previous import UI002, Export
UI003 and installed Standalone001. Thirty new Python cases cover record fields/
versions, original/projection/output tampering, canonical bytes/base64, exact
budgets/types, same/stale/new owners, request copies, JSON/session transport,
restored-owner rebind, generic workflow refusal, actual captured-review controls,
and file/workflow publication. The expanded installed SDK test resolves,
validates, extracts/replays and rebinds the new public type and refuses bad output/
new owners without pixel reads.

Actual paired/installed captures retain raw input, complete workflow equations
and files. They qualify normal recorded export/replacement, blank captured-review
choices, explicit destination confirmation, identical same-owner output, different
new-owner source1/row0 choices, off-plane Go to/preview, reservation failure before
replacement, recording off, invalid original/retry and original immutability.
Existing Unicode, overwrite-race, Ready-cancel and staged-source guards remain
covered. Successive publications append1/2/3/4 entries; recording off leaves4.
Native metadata/undo remain unchanged until the deliberately staged-title fixture
adds its fourth original-document undo step. Worker counters do not advance across
validation/staging; total viewer reads remain nonzero. Fixture API actions for
cloning/activating a destination, malformed workflow data and staged job/source/
race boundaries are declared. Physical Jobs Cancel and stalled IO are not claimed.

Independent final binding parses every output's V2 payload, nested V1 source,
native row and standard projection. Stored projection/output equal actual bytes;
original raw bytes match the explicit member and native state matches the chosen
row. C++/Python/installed point and rebound files are identical. Header/core/
executable/module identities, source freezes and before/final holds bind the work.
Initial probes retained an underestimated positive budget and an off-screen
malformed-step fixture selection. The test now derives the exact logical boundary
and reviews the malformed entry in the first visible row. Failed receipts/fixtures
remain; no failure or historical audit was suppressed. No new Java reference
capture or installed Python wheel is claimed.

The attestation is
`docs/parity/imagej-roi-captured-export-commercial-source-review-001.json`:
original project-MIT code/tests, no dependencies, fonts, third-party product code
or restricted features added. Exact owned superseded green captures/test scratch,
SDK intermediates and empty temporary directories were journaled before removal
(365951330bytes). Failures, final raw observations/files, receipts, source holds,
installed SDK/desktop/consumer and useful cache remain. Protected historical roots
and unowned imgui.ini were not touched; legacy window line endings were preserved.

Next close multiple-member/ZIP export with one shared frozen source guard and
explicit flat unique names. Admit history once; avoid serializing/retaining an
archive-sized pairing suffix per member. Bound aggregate wire/projection/native/
record storage, resolve the complete list before output, and reuse the guarded
file/workflow boundary. Qualify both hosts, planes/T/C, duplicate/unsafe names,
late-member failures, budgets, source changes, cancel/retry, exact originals and
installed consumers. Native text/image mixed workflows, remaining picker/input/
running/stalled-IO and projection qualification, matching full releases, historical
audits and all99 families remain active. Source901 remains the last qualified
source-release boundary. No multiple-member export, full interchange, whole-family
parity credit or goal completion is claimed.

### Shared-source multi-member ZIP export API increment

C++ `imagej_roi_zip_export.hpp` and Python now expose explicit item/limit/result/
request types plus resolve, validate, request extraction, replay and rebind.
Each member supplies a flat unique name, source index, annotation index and
standard projection. No automatic source-to-row links are introduced. A shared
capture admits all original history and current rows once; materialized members
retain only their selected original basis and edited native row. Resolution is
complete-or-refuse and performs no file IO, extraction or document mutation.

The closed nine-field V1 `imagejxx.imagej-roi-zip-export` record contains schema,
version, source, limits, annotations, sources, members, compress and output.
Rows/archive are shared once across the record. Every member contains name,
source_index, annotation_index and canonical projection. Validation reconstructs
the exact deterministic ZIP equation after admitting the full original history;
replay also checks current owner/revisions/shape/descriptor/calibration and exact
rows/archive. Display-only changes pass. Rebind validates the original before
requiring a fresh complete explicit item list. Original input archives remain
byte-exact; output envelopes bind their canonical original basis when inputs
already contain V2 envelopes.

Input projection/name retention and retained results have separate bounded phases
against the decoded-source cap, each seeded by current native rows. Canonical
projection owners are charged consistently on fresh resolution and record decode.
Wire/name/count/aggregate/archive/working limits and logical record/base64 storage
are preflighted. Retained member bytes are subtracted from ZIP codec working
capacity before its temporary copy. These are logical storage bounds, not an RSS
claim. Python owns mutable projections before released-GIL work, fetches each
custom sequence index once, and returns lifetime-safe member/mapping views without
eagerly copying the complete wire batch. Exact Unicode names preserve established
case-distinct behavior; reserved/unsafe names and exact duplicates refuse.

`build/p06q-roi-zip-export001/zip-export-handoff001.json` binds Build002, Native002
(15 plus restricted host), Atomic001, API001, SDK001, Python003 (2023 passed,
zero skipped), all prior C++ UI002 probes and installed Standalone001. Fifty new
Python cases cover compressed/stored output, XY/XZ/YZ and packed masks, T/C,
precision/opaque metadata, names, late-member failures, closed-field tampering,
original history, source changes, display-only replay, new-owner rebind, session
transport, exact record/decoded-budget boundaries, input copies and view lifetime.
A repeated-source case with eleven historical originals and sixteen explicit
members fits a128KiB decoded cap without retaining an archive suffix per member.
The installed SDK consumer exercises the new public header and equation/rebind.

Independent stdlib ZIP inspection checks CRC, method, order and member bytes for
both compression modes. Direct V2/V1 parsing verifies native JSON equals the
explicit selected row, projection equals the record and nested original basis
equals its selected archived source basis. Mixed-plane API capture leaves undo4
and shared pixel data unchanged, with zero plane/region reads. Existing actual
single-export/import UI tests pass; they do not qualify ZIP widgets. Initial
Native001/Python001 failures retain an incorrect case-alias rejection expectation;
tests were corrected to established exact identity, with a positive case-distinct
test. No failure or historical audit was suppressed.

`docs/parity/imagej-roi-zip-export-commercial-source-review-001.json` attests
original project-MIT implementation/tests, no new dependencies or third-party
product code, and no restricted features. Exact owned superseded successful
captures, test scratch, SDK intermediates and empty temp directories were pinned
before removal (366109474bytes). Failures, final raw evidence/files, receipts,
source holds, installed products/consumer and useful cache remain. Protected
historical roots and unowned imgui.ini were untouched.

Next add the shared explicit member queue with independent names/source/row/
projection choices, per-member preview and full-batch preparation. Reuse the
guarded staged-file/workflow boundary for a reviewed ZIP destination; qualify
both hosts and installed launch, late failures, source changes, cancel/retry,
overwrite races and typed workflow review/rebind. ZIP GUI publication, native
text/image mixed workflows, remaining qualification, matching releases, historical
audits and all99 families remain active. No new release, whole-family credit or
goal completion is claimed. Source901 remains the last full source-release proof.

### Shared explicit ZIP queue and guarded workflow/file UI increment

Both hosts now expose File → Export ImageJ ROI ZIP, using the existing member
projection editor and the native shared-source batch API. Every member requires
independent source/current-row/metadata choices, explicit position interpretation,
validated projection and a flat exact unique `.roi` name. Adding clears those
choices. The queue retains canonical projected items, not full pairing records
or repeated archived-source suffixes. Before copies, projection/name/count
retention is charged against the declared native source cap. Final batch admission
also charges current rows, selected mappings, wire/projection/record and ZIP
working storage through the API. Names preserve established case-sensitive
Unicode identity. ZIP preview names render literal hash characters.

Member editing and queue review have separate views within the shared panel so
the controls fit the existing desktop window. Members may be removed; removal,
compression or member-choice changes invalidate prior batch results. Complete
preparation resolves every member once against one frozen capture and builds an
aggregate-bounded linked-plane preview. Go to member moves fixed-axis/T/C state
without changing the source. All prepared annotations render on their applicable
planes. Output requires a `.zip` path; Browse filters ZIP and preserves the review
on Cancel. Replacement consent starts false and clears on path changes.

The existing private staged atomic file worker is reused. UI completion checks
source owner/shared data/revisions and scheduler cancellation before publication.
Workflow storage/value allocation is reserved before the final rename; successful
publication appends one closed typed ZIP equation without allocating afterward.
Malformed workflow data refuses before the destination changes. Recording off
leaves the log unchanged. Ready cancellation, late destination creation and source
changes discard the owned sibling. Numeric filesystem errors retain UTF-8 paths.

Workflow review recognizes the ZIP schema, fully validates the original equation
before admitting current choices, starts an empty queue and blank source/row/
metadata/path with no replacement consent, and requires destination confirmation.
An explicitly selected captured member may supply standard metadata; incompatible
retained geometry requires explicit reset. Fresh queue resolution on the frozen
destination creates a new typed entry while retaining the original unchanged.
Generic run_workflow/batch reject ZIP exports before executing any step so every
destination receives individual review. Invalid original/retry exposes no catalog.

`build/p06q-roi-zip-ui001/zip-ui-handoff001.json` binds Build003, Native002 (15 plus
restricted host), Atomic002, SDK002, Python002 (2028 passed, zero skipped), all
previous C++ UI003 regressions, ZIP UI003 and installed Standalone002. Five new
Python checks inspect actual raw C++/Python controls, mixed XY/XZ/YZ previews,
explicit names/choices, compressed/stored output, native precision/opaque rows,
workflow equations, original admission, new-owner review, generic batch refusal,
and cancellation/race/stale/recording guards. Final C++/Python/installed ZIP probes
use109 controls and534 frames each. API fixtures for source imports/native rows,
cloned owner, malformed workflow, staged cancellation/source/race boundaries are
declared. Captured review is opened by its actual workflow button. Physical Jobs
Cancel and stalled IO are not claimed.

Independent stdlib ZIP CRC/method/order inspection and direct V2/V1 parsing bind
every output to its exact stored projection, selected native JSON row and original
archived source bytes. Compressed, stored and rebound files match all three
hosts. Original undo4/native extensions are unchanged across export; new-owner
review creates no image undo, and the deliberate final title change adds one.
Pixel read counts do not advance across final preparation/staging checks while
the frame pump is held; the live viewer still reads pixels outside those checks.

Initial evidence retains a captured point-template/rectangle refusal until the
fixture explicitly reset retained geometry. Python001 also retains a nonexistent
enum expectation and a clone fixture constructed from DocumentData, which lost
display state and included viewer reads. Tests now use public plane strings
and the snapshot-clone constructor. No failure or historical audit was suppressed.
The final full selected suite passes; no new Java reference or Python wheel proof
is claimed.

`docs/parity/imagej-roi-zip-ui-commercial-source-review-001.json` attests original
project-MIT UI/tests with no dependencies, fonts, imported third-party product code
or restricted features added. Exact owned superseded successful captures/installed
copies and SDK intermediates were pinned before removal (268442846bytes). Failures,
final raw C++/Python/installed reports/files, receipts, source holds, installed
SDK/desktop/consumer and useful cache remain. Protected historical directories and
unowned imgui.ini were untouched; legacy window.cpp remains CRLF.

Next close native text/image mixed workflows and remaining standard projection/
wire feature qualification. Continue picker/input/running/stalled-IO checks and
matching release evidence, then resolve historical audits and all99 commercially
eligible families. This qualifies the declared ZIP controls and guarded publication
increment, not whole interchange, full editor parity, a new release or goal
completion. Source901 remains the last fully qualified source-release boundary.

### Native planar text/image display metadata and command increment

The remaining text/image gap was not binary transport: typed ImageJ text and
embedded-image codecs already existed. The native planar model held selection
geometry only. This increment adds independent typed display payloads and rows
without representing text/image content as a processing selection or changing
the existing selection annotation schema.

Public C++ `planar_display_annotations.hpp` and Python now expose immutable Python
TextAnnotationPayload, ImageAnnotationPayload and PlanarDisplayAnnotation values.
Text retains double bounds/font size/angle, UTF-8 content/font name, font style,
justification, string-mode flag and explicit ARGB foreground/optional background.
Image rows retain exact owned RGB8/RGBA8 bytes, independent double display bounds,
opacity and zero-transparency. Common name/plane/fixed-coordinate/T/C and opaque
attributes are independent of processing selections. Explicit zero is positioned;
T/C null is wildcard. Off-canvas positive-size bounds remain exact for clipping.
No font discovery/layout, texture upload, rasterization or filesystem is performed.

The reserved `imagejxx.planar_display_annotations` extension has a closed V1
`imagejxx.planar-display-annotations` grammar with schema/version/extent/rows.
Extent binds all six TCZYXS dimensions, with exact positive native integers.
Every row has name/payload/plane/coordinate/t/c/attributes. Tagged text/image
payload fields are closed. Strict finite geometry/text/flags/colors/dimensions,
exact RGB/RGBA pixel length and canonical base64 are admitted before retention.
Limits are4096 rows,1MiB text/row,4096-byte name/font name,1Mi image pixels/row,
RGB/RGBA channels,16384 maximum pixel edge/font size,16MiB logical encoded set,
1M metadata nodes and80 nesting levels. Conservative container overhead and
aggregate payload/attribute preflight occur before owned record/base64 copies.
Python validates borrowed metadata and owned byte length before native conversion.
These are logical storage bounds, not whole-process RSS limits.

DocumentData validation recognizes the reserved extension, so document/session
admission cannot retain malformed content. Native store/update/delete primitives
produce ordinary metadata edits with pixel/metadata dependencies and undo/redo;
equal updates are no-ops and deleting the last row removes the extension. Source
pixel storage remains shared. Processing selection, saved processing ROIs and the
existing planar selection annotation extension remain independent.

`planar_display_annotation_command.hpp` provides resolved immutable commands,
request extraction, validation, replay, explicit rebind and prepare. The closed
nine-field V1 equation binds schema/version/source/operation/index/annotation/
before/after/destination. Store/update/delete request grammars are checked, all
derived claims decoded, and the complete original equation validated before
rebind. Replay also guards owner, pixel/metadata revisions, all shape dimensions,
pixel policy/type, calibration and exact before rows. Display-only navigation
passes. Another owner requires a fresh explicit request. Each set remains bounded
and whole-command metadata is preflighted at64MiB. Python inputs are owned before
released-GIL native work; payload/attribute/record access cannot mutate commands.

`build/p06q-display-annotations001/display-annotations-handoff001.json` binds
Build003, Native002 (16 including the new native model/command test), restricted
host, Atomic001, API001, SDK001, Python001 (2105 passed, zero skipped), prior paired
UI001 regressions and installed Standalone001. Seventy-seven new Python cases
cover both payloads/all planes, precision, positioned-zero/wildcard T/C, session
transport, update/delete/no-op/undo, independent selection rows, strict input
types, malformed reserved extensions, closed-command tampering, stale/new owners,
original-before-rebind, ownership and resource boundaries. Maximum1Mi-pixel RGB
and RGBA payloads pass; a third maximum RGBA row exceeds aggregate capacity and
refuses without changing document/history. The installed C++ consumer links both
new headers and exercises encode/decode, primitives, command/replay/rebind and
refusals. No source plane/region reads occur across native model operations.

Independent API receipt inspection checks exact text/layout fields, raw decoded
RGBA bytes, common positions/opaque metadata, mixed-row order and the handwritten
store/store/update before/request/after equations. Update undo/redo restores exact
sets. The first native test expected DocumentData pointer sharing across metadata
edits; the corrected assertion checks shared dataset pixel storage. Its failed
Native001 receipt remains. No failure or historical audit was suppressed.

`docs/parity/planar-display-annotations-commercial-source-review-001.json` attests
original project-MIT model/commands/bindings/tests with no new dependencies,
fonts, imported third-party product code or restricted features. Exact owned empty
temp directories, superseded focused scratch and SDK intermediates were pinned
before removal (481030bytes). All failures, final raw observations/reports,
receipts, before/final source holds, installed SDK/desktop/consumer and useful
cache remain. Protected historical directories and unowned imgui.ini were untouched.

Next render/edit these typed layers in both ImGui hosts, including clipped planes,
font/rotation/layout semantics, image alpha/opacity/transparency, memory admission,
input/history/workflow behavior and session restore. Add explicit typed ImageJ
text/image plane mapping, original-wire native envelopes and mixed atomic
import/export/ZIP queues; integrate transform/remap behavior. The current ROI
panels still handle selection annotations. Remaining qualification, matching
releases, historical audits and all99 commercially eligible families remain
active. No display-layer UI, mixed interchange, new release, whole-family credit
or goal completion is claimed. Source901 remains the last full source-release proof.

### Shared native display editor and clipped linked-plane rendering increment

Both hosts now expose File → Text and image annotations. New/stored typed display
rows share name, plane, fixed coordinate, T/C/wildcard and U/V/width/height controls.
Text editing includes content, font alias/size, rotation, justification, baseline
mode, ARGB foreground/background and explicitly synthetic bold/italic. Image
editing retains pixel bytes while changing bounds/opacity/zero transparency.
Explicit current-plane capture replaces the private image payload with RGB display
pixels and its dimensions. Store/update/delete resolve native typed equations,
preserve exact native doubles and opaque attributes and create ordinary undo.
Equal updates suppress history/workflow changes. Closing/Escape, source changes,
busy state and external undo cancel private forms. Display-only navigation passes.

The new restricted desktop publication route may change only the display extension.
It checks owner, pixel/metadata dependencies and disallows display-history override,
pixels, titles, processing selections and other metadata. The window publisher
guards the idle active source, admits graphics storage, reserves workflow output
before commit and appends without allocation afterward. Recording off preserves
the log. Malformed workflow data refuses before document/history changes. Typed
workflow labels/replay buttons and replay_display_annotation action are added;
explicit rebind_request validates the captured original before parsing new inputs.
Dedicated captured rebind controls and generic mixed workflow execution are not
qualified here.

A per-document creator-thread renderer draws native display rows on ordinary XY
and linked XY/XZ/YZ canvases, with canvas clipping and exact plane/T/C filtering.
It preserves native row order, independent of existing selection annotation rows.
Text glyph quads use declared UI-font metrics, multiline advance/justification,
plane-center rotation and anisotropic projection; baseline mode shifts the first
line by the font ascent. Bold/italic are synthetic and labelled as such. ARGB
foreground and rotated bounds background retain alpha. SansSerif/ImGui UI font
are declared current-UI-font aliases. Other families show a visible error until
explicit mapping; unavailable glyphs use the font's fallback and are counted.
Full font registry, glyph coverage, shaping and upstream-exact layout remain open.

Images upload owned nearest-sampled RGBA textures. Original alpha is multiplied
by opacity with integer rounding; RGB black becomes transparent only when chosen.
Stored RGB/RGBA bytes remain untouched. Texture caches survive unrelated metadata
edits, invalidate changed payloads and release while their GL context is alive.
The shared processing memory cap bounds conservative draw/cache estimates before
publication/rendering. Capture admits a conservative legacy conversion workspace
estimate before reads and uses bounded native composition for channel display.
Current color/grayscale/composite state and shared legacy linked-view contrast are
honored. Native display metadata is excluded from linked pixel-cache identity.
View rendering and explicit capture still read source pixels; pure publication
does not sample them. No whole-process RSS quota or running capture cancellation
is claimed.

`build/p06q-display-ui001/display-ui-handoff001.json` binds final Build008,
Native002 (16 plus expanded restricted host), Atomic002, SDK002, Python002
(2111 passed, zero skipped), all prior C++ UI002 regressions, Display UI004,
installed Standalone002 and dialog API002. Six new Python checks cover actual raw
controls, native precision/style, no-op and invalid input, font diagnostics/mapping,
RGBA alpha/opacity/transparency, colored-channel capture, workflow reservation/off,
delete/undo/source guards, typed replay/rebind/original-first validation and zero
source reads during direct publication. Each final C++/Python/installed new case
uses52 controls and289 frames. Default eight-entry history capacity is respected.
Fixture API actions for source rows, channel display state, undo, malformed workflow,
title change and hidden GL rendering are declared; mouse/keyboard control evidence
is retained separately from those fixtures.

Native OpenGL screenshots from C++/Python/installed launch and an actual dialog
layout capture supplement the input probes. Independent PNG decoding checks each
1250x820 RGB screenshot and its large embedded-red region over a grayscale source.
Final renderer/dialog screenshots were visually inspected. Independent metadata/
base64 checks bind exact text doubles/ARGB/style, unchanged original RGBA bytes,
derived [255,0,0,62,0,0,0,0] display bytes, explicit YZ/C/T and red-channel capture,
and handwritten store/update/delete equations for all seven recorded GUI steps.
All three hosts retain identical final native display metadata. Dialog rendering
alone changes no document data/history/workflow. No ImageJ typography reference
capture, session-launch UI matrix or new installed Python wheel is claimed.

Failures retain initial C++ condition-scope naming conflicts, probe temporary
transaction binding, a row-control key mismatch and an omitted composition-provider
argument. The probe's expected history count was corrected to the consumed default
eight-entry limit. Final review found capture initially used only legacy display
metadata; it now uses current native channel composition and linked-view contrast,
with a red-channel case proving the correction. No failed or historical audit
evidence was suppressed.

`docs/parity/planar-display-ui-commercial-source-review-001.json` attests original
project-MIT UI/renderer/tests, no new dependencies, fonts, imported third-party
product code or restricted features. Existing UI-font/OS-font behavior is reused.
Exact owned superseded successful captures/test scratch/installed copies and SDK
intermediates were pinned before removal (626626132bytes). Failures, final raw
observations/GL screenshots, receipts, source holds, installed SDK/desktop/consumer
and useful cache remain. Protected historical roots and unowned imgui.ini were
untouched; window.cpp retains CRLF.

Next close typed ImageJ text/image plane mapping and original-wire native
envelopes, followed by mixed complete-or-refuse import/export/ZIP queues. Continue
font registry/glyph/layout, canvas editing/transform/remap, captured rebind and
session-launch qualification, remaining input/IO/projection checks, matching full
releases and historical audits. All99 commercially eligible families remain active.
This qualifies the declared native display editor/renderer increment only; no
mixed interchange, new release, whole-family credit or goal completion is claimed.
Source901 remains the last full source-release proof.

### Typed display ROI mapping and original-wire envelopes

The public C++/Python bridge maps bare ImageJ text/image records to native display
rows on all three planes. Source bytes/typed record/mapping are immutable and
shared across explicit current-row and target-extent rebinding. The existing
selection position resolver is factored into one shared implementation; processing
selections remain independent. Closed IJD1 native envelopes retain exact native
layout, RGBA/scale and opaque attributes alongside the unchanged bare original
and a canonical chosen standard projection. Full decode returns the projection's
custom properties/group and every explicit choice for exact re-encoding.

Projection agreement checks all native-representable fields. Fractional font
rounding, image alpha removal, unscaled projection and fractional origin rounding
require independent explicit options. No implicit flattening occurs. Unspecified
text color requires an exact declared default; irrelevant defaults/options refuse.
Retained TIFF must match the chosen RGB projection. Rebinding can explicitly
change native kind without rewriting the original source. The decoder validates
the original before current state, canonical base64/binary/JSON, closed native
schema, field equation and exact outer wire. It rejects duplicates, trailing data,
malformed policies, reserved-property nesting and inconsistent projection bytes.
Python preflights borrowed DTOs before owning copies, admits the original before
visiting new target choices, and releases the GIL only after ownership. Conservative
logical allocation budgets cover source/native/projection and expanded envelopes;
they do not establish a process RSS quota. Native image rows retain the existing
one-million-pixel/16MiB set limits.

`build/p06q-display-mapping001/display-mapping-handoff001.json` binds Build002,
Native001 (17 tests plus restricted host), Focus001 (69 new Python checks),
Python001 (2180 passed, no skips), installed SDK001 consumer, Standalone001,
Atomic001 and all nine existing C++ UI probes. API001 independently parses IJD1
segments and checks original wire, all six source/target dimensions, source
position, native JSON, projection wire and acknowledgment flags. The C++ consumer
also proves shared original storage across rebinds. Tests cover precision, alpha,
explicit losses, all planes, source ambiguity, ownership, original-first refusal,
canonical/tamper/limit refusal, cross-kind edits, document publication and undo.
No mixed UI integration or installed Python wheel/full release is claimed.

`docs/parity/imagej-display-roi-commercial-source-review-001.json` retains source
before/after identities and attests original project-MIT implementation with no
new third-party code, dependencies, fonts or restricted features. Exact owned
successful temporary/pytest scratch was pinned before removal:288647107bytes.
Final raw observations, receipts, source holds, SDK/desktop and active cache remain.

Next close the gaps in dependency order:

1. Add a versioned heterogeneous original-source archive and atomic mixed batch
   admission. Keep raw incoming envelopes separate from their original bare basis;
   validate the complete source/member graph and aggregate budgets before mutation.
2. Extend pairing, replay/rebind and single/ZIP export equations to typed display
   rows with independent source/row/projection choices and original-first admission.
3. Extend shared queue controls and linked previews in both hosts, then qualify
   mixed failure atomicity, stale-source cancellation, workflow/file publication
   and installed consumers before considering any broader acceptance.

Font registry/layout/glyph parity, canvas editing/remap, session launch, remaining
IO/input qualification, matching releases and historical audits remain open.
All99 families remain42inprogress57open0complete. The goal remains active and
Source901 remains the last full source-release proof.

### Mixed typed import, V3 archive and atomic command publication

`imagej_annotation_import.hpp` and matching Python bindings now admit ordered
geometric/text/image members together. Items explicitly select geometric or
display interpretation; a bare display item requires its plane mapping and any
unspecified-color interpretation. Native envelopes require no bare mapping and
may instead have an explicit complete native replacement. The original transport
is admitted before that replacement. Native records keep input order while the
two published annotation sets keep their own relative order. No source/row index
association is inferred. Raw input bytes, including incoming envelopes, remain
distinct from the mapped record's immutable original bare basis.

The existing source key now supports V3 through the mixed API. Its closed root
retains schema/version/entries; each entry is exactly kind/source, with kind
selection or display. The source object is exactly name/data/mapping/
native_replacement. Geometric choices retain their existing mapping/replacement
grammar. Display choices retain all six dimensions, explicit plane/position
policies/color interpretation and a closed native display set containing exactly
one replacement row. Old V1/V2 archives are admitted completely before conversion;
unchanged raw original data, labels, mappings and replacements survive migration.
Unknown fields/kinds/versions, noncanonical base64/entry claims, malformed members,
aggregate input/retention/provenance budgets and invalid historical originals refuse
the complete operation. Python sequences are snapshotted once before all-item
preflight and owned conversion; custom sequences cannot substitute a later item.

Append preserves both existing native sets and source history. Explicit replace
validates history first, then replaces both sets and archive. Empty native sets
remove their reserved extension. One Pixels|Metadata-dependent transaction publishes
both sets plus history; invalid resolution publishes no prefix, stale commits
refuse, display navigation can coexist, undo restores the original snapshot, and
session serialization retains V3. No pure import/inspection operation reads source
pixels or changes processing selection state.

The closed twelve-field V1 imagejxx.imagej-annotation-import-command binds source,
mode/limits, both before/after sets, before/after source archive and destination.
Validation admits the complete historical prefix before extracting a requested
suffix, reconstructs the entire equation and checks exact output. Replay guards
owner/revisions, extent/type/sample policy/calibration, current sets and original
archive. Explicit rebind admits the original before visiting new Python choices.
Transport label changes in a new suffix are valid requests and do not rename the
native row; commands are equations, not cryptographic signatures. Changing the
source payload without its derived native state or rewriting a historical prefix
refuses. Existing geometric-only commands retain their unchanged grammar.

`build/p06q-mixed-import001/mixed-import-handoff001.json` binds final Build002,
Native002 (18 plus restricted host), Focus002 (56 new Python checks), Python001
(2236 passed, no skipped), SDK001 installed consumer, Standalone001, Atomic001,
all nine prior C++ UI probes and API002. API002 checks exact legacy migration and
interleaved raw originals, handwritten Unicode text/color/position and precise
RGBA/scale/opaque claims, exact replay record, one-step undo and zero dataset reads.
Maximum supported 1024x1024 native RGB import passes with an explicit aggregate
retention budget. ZIP transport decoding and mixed semantic admission are tested
for both stored/deflated members; this does not add a mixed ZIP dialog or a new
combined ZIP API. No new Python wheel or full source release is qualified.

The initial Python test wrongly required a changed suffix transport label to
refuse; it was replaced with a source-payload/derived-state disagreement and an
explicit valid-label test. The first API checker used lowercase serialized plane
names instead of the native schema's uppercase names; its corrected variant also
keeps a separate snapshot variable for import-origin equality checks. Those failed
raw attempts remain. No product failure was hidden. Final source freeze includes
the new native test and is checked against all final run captures.

`docs/parity/imagej-annotation-import-commercial-source-review-001.json` records
original project-MIT authorship/integration, source before/after identities and no
new dependencies, fonts, imported third-party implementation or restricted feature.
Exact owned successful temporary/pytest scratch and SDK intermediate CMakeFiles
were journaled before deletion:288387695bytes. Final raw observations/receipts,
failures, held source, installed SDK/consumer/desktop and useful cache remain.
Protected historical roots and unowned imgui.ini were untouched.

Next integrate V3 admission into typed pairing and single/ZIP export/workflow
equations, then mixed queue controls and linked previews in both hosts. Currently
the older geometric-only importer/pairing/export deliberately refuse V3 rather
than silently omit its display entries. Preserve complete historical admission,
independent source/current-row/projection choices, explicit projection losses,
stale-source guards and publication reservation. Font registry/layout, canvas
editing/remap, session launch, remaining IO/input qualification, matching releases
and historical audits remain open. All99 families remain42inprogress57open0complete;
the goal remains active. Source901 is still the last full source-release proof.


### Mixed typed pairing and single/ZIP export equations

`imagej_annotation_export.hpp` and Python bindings now provide a snapshot-bound
source catalog and explicit source/category/current-row pairing across V1/V2/V3
histories. The entire original graph is admitted before choices. Source labels,
duplicate names, edits/reordering/deletion and source indices never infer current
row associations. Selection/display category mismatch refuses; a display source
may explicitly bind to either native text or image payload without changing its
original basis. Catalog summaries are bounded; current native sets are admitted
independently from historical source extents. Selected originals are cached once
per unique source during a ZIP build and rebound explicitly to each chosen row.

Single ROI and ZIP APIs share one pure engine. Each request explicitly supplies
a portable flat .roi name, source index, native category/index, typed standard
projection and display projection options. Wire-only metadata remains chosen by
the caller. Standard projections are canonicalized under their wire representation;
the native envelope keeps full current precision, attributes, alpha and scale.
Font rounding, alpha removal, unscaled image representation, fractional origin
rounding and unspecified text color retain their explicit acknowledgments.
Selection projections refuse display options. All members/indices/names/projections
and aggregate budgets must succeed before returning an output; nothing is written
or published. Stored/deflated ZIP encoding uses the existing bounded CRC/name/
transport codec. Safe case-distinct names remain distinct.

The closed ten-field V1 imagejxx.imagej-annotation-pairing record binds source,
limits, both current sets, complete source archive and independent indices/category.
The closed eleven-field V1 imagejxx.imagej-annotation-export record binds schema,
version, source, limits, annotations, display_annotations, sources, members,
packaging, compress and exact output. Each closed member binds name, source_index,
annotation_kind, annotation_index, projection_kind, canonical projection wire and
all options. Shared sources/sets appear once, not once per member. ROI packaging
requires one member and no ZIP compression. Validation reconstructs canonical
members/output, including exact base64, and refuses mismatched claims. Current
replay checks source owner/revisions, shape/type/sample policy/calibration, both
native sets and complete archive. Rebind admits the original before new choices.
No path, destination or overwrite consent is stored in these equations.

Python owns mutable projection DTOs before releasing the GIL, snapshots sequence
elements once and retains nested result/member/mapped owners. Original archive
admission precedes new sequence visitation, and direct pairing encode checks the
live guard before owning a new projection. Source admission retains its bounded
archive/current-row budget; ZIP working bytes additionally cap logical retained
export assembly for ROI and ZIP. Display wire text may use its existing64MiB hard
bound in this mixed API; legacy geometric exporter limits remain unchanged. Native
maximum1024x1024 RGB/RGBA payloads pass both packaging modes with explicit enlarged
source/text/record/member/display limits. Budgets apply to logical phases and codec
working storage, not whole-process RSS. Single-file export does not inherit a
smaller ZIP member cap when its own output limit permits the result.

`build/p06q-mixed-export001/mixed-export-handoff001.json` binds final Build004,
Native004 (19 plus restricted host), Focus004 (70 new Python checks), Python002
(2306 passed, none skipped), SDK001 installed consumer, Standalone001, Atomic001,
all nine existing C++ UI probes and API002. Native/SDK checks exercise mixed
catalog/pairing, single and stored/deflated ZIP output, request decode, guarded
replay/rebind, invalid batch refusal and zero source reads. Python adds precise
text/RGBA/source-basis round trips, all choice/category/field/flag/limit refusals,
V1/V2 compatibility, case-distinct names, missing/deleted rows, ownership/lifetime,
source-first refusal, stale-source/display concurrency and maximum payloads.

API002 independently checks ZIP names/compression/CRC/member bytes, parses IJD1
segments, matches unchanged bare originals and canonical projection bindings, and
checks handwritten native precision, Unicode text, opaque uint64 attributes,
ARGB/layout/plane/T/C and RGBA values against actual output. Single member bytes
match ZIP member bytes. Pure resolution/replay leaves document data/history and
pixel observers unchanged. New maximum RGBA cases were added after initial green
qualification; Build003/004 core/library/PYDs/desktop are byte-identical and the
only source difference is that Python test expansion. Existing UI/SDK regressions
are reused with those identities; expanded native/Python/API verification is fresh.

Failed Native001/Focus001 exposed zero indices incorrectly using a positive-size
parser and an unqualified byte-wrapper collision with std::data. Both were fixed.
Max001/Focus002 exposed envelope text/member budgets and the need to separate
source admission from output assembly limits. Failed raw evidence remains. No
restricted dependency/font/code was introduced; original project-MIT integration
and source identities are retained in
`docs/parity/imagej-annotation-export-commercial-source-review-001.json`.
Cleanup journaled and removed577266325 owned successful scratch/intermediate bytes.
Final raw outputs/receipts, failures, source holds, SDK/consumer/desktop and useful
cache remain; protected historical roots and unowned imgui.ini were untouched.

Next close shared mixed import queue/publication, then pairing/projection/ROI/ZIP
queue controls and captured workflow review/rebind. Reuse existing workers,
exclusive staged-file handling, original/source guards, workflow reservation and
typed linked renderer. Qualify raw controls in both hosts and installed desktop,
including late invalid members, source changes, cancellation, reservation/file
failures and undo. The native backend now supports V3 mixed export; geometric-only
dialogs still refuse V3 until that integration. Font registry/layout/glyph parity,
canvas editing/remap, session launch, remaining IO/input qualification, matching
releases and historical audits remain open. All99 families remain42inprogress57open0complete;
the goal is active. Source901 remains the last full source-release proof.

### Shared mixed import UI, typed draft renderer and guarded publication

The existing File → Import ImageJ ROIs... form now classifies and reviews complete
geometric/text/image inputs in both hosts. File/browse, worker scheduling, generation
guards, cancellation, captured review and geometry replacement use the existing
path. Typed members or V3 source history choose the mixed native resolver; older
geometry-only histories retain their original command grammar. Every bare member
still requires explicit plane/position choices. Unspecified text color requires a
confirmed eight-digit ARGB interpretation; missing/malformed choices refuse the
whole preparation. All input bytes remain unchanged, including native envelopes.

Native display envelope replacement has its own reviewed editor. It retains the
admitted payload and opaque attributes, or explicitly chooses an existing native
display annotation as a complete template. Name, plane, one-based slice/T/C and
wildcard membership are independent explicit choices; destination confirmation
and complete native validation precede staging. Discard does not publish. Payload
content/style editing remains in the existing native text/image editor, not this
replacement form. Captured mixed import commands load the same member/destination
review. The replay_imagej_annotation_import action validates the original equation
and current source before publication. Generic mixed recipe execution is pending.

The restricted desktop facade opts into exactly the planar selection extension,
planar display extension and source archive. It rejects other document/pixel changes
and stale ownership/revisions. Window publication checks the idle active source,
admits both visual sets, reserves workflow storage before committing and publishes
the captured command afterward. Recording-off leaves workflow data unchanged;
equal before/after sets/archive suppress recording. One native transaction provides
whole-batch undo and session persistence. Any preparation/reservation failure
publishes no prefix. Source changes close stale forms; display navigation alone
does not invalidate metadata review. Go to selected member sets its fixed-axis/T/C
view explicitly.

Typed drafts use a detached metadata snapshot with shared source pixels. Per-pane
creator-thread renderers reuse the native text/image renderer, clipping and plane/
T/C filtering. Mixed replacement hides both live annotation sets; append retains
the current sets. Distinct immutable extension identity refreshes drafts sharing
an owner/metadata revision. Draft texture/CPU caches clear when preview closes;
textures release while GL is alive. Geometry now submits before typed display
layers in both ordinary and linked canvases, matching publication order. The
separate draft geometry highlight remains a preview visual. The compact review
form was enlarged/repositioned so Apply/Cancel remain above the status bar.

`build/p06q-mixed-import-ui001/mixed-import-ui-handoff001.json` binds final Build005,
Native003 (19 plus expanded restricted host), Python002 (2311 passed, no skips),
SDK002, Standalone002, Atomic002, all prior C++ UI002 regressions, Mixed UI004 and
API001. Five new Python checks combine actual paired raw control traces, complete
metadata/color/source equations, typed precision/alpha and draft visibility,
captured replacement, reservation failure, recording-off, late invalid input,
stale-source cancellation, undo/reopen and actual OpenGL output. Each new native/
Python/installed case exercises56 controls with a bounded raw frame log; asynchronous
worker settling can change idle frame counts. The native host test checks the new
restricted publication route, exact source/cap preview binding, stale rejection,
shared pixels and undo.

API001 independently derives full annotation/source-archive outputs from handwritten
native rows, exact ZIP inputs and explicit mapping/color choices, then derives
the complete replacement equation. Original transport bytes survive replacement.
Observed pure replay acquires no additional plane/region samples; UI rendering and
explicit session IO still read pixels. Actual ImGui index ranges prove typed draft
layers submit after geometry. Native OpenGL PNGs show embedded color over a gray
source and were visually inspected; independent PNG/alpha checks pass. Mixed
import sessions reopen with exact native metadata. No upstream typography reference,
new installed Python wheel, session-launch matrix or full release is claimed.

The first Python check caught an inaccurate ARGB field hit point: it targeted the
label rather than the short input, leaving the old color. The point was corrected,
native malformed-ARGB/exact-color assertions were added, and the failed raw attempt
remains. Visual review also caught a clipped review footer and typed layers being
covered by the geometry preview; layout and actual draw submission order were
corrected before final qualification. Final expanded regressions were rerun.

`docs/parity/imagej-mixed-import-ui-commercial-source-review-001.json` binds original
project-MIT implementation, source before/after identities and no added dependency,
font, imported third-party implementation or restricted feature. Before cleanup,
raw Python capture folders and superseded successful captures were pinned and
losslessly retained in retained-runtime001.zip with member SHA/size verification.
Final new captures remain directly available in API001/C++/installed stages. Cleanup
removed860968057 owned scratch/superseded SDK/desktop bytes while retaining50663217
compressed evidence bytes: net810304840bytes reclaimed. The failed color-check
evidence, final raw observations/receipts, source holds, active SDK/desktop and useful
cache remain. Protected historical roots and unowned imgui.ini were untouched;
window.cpp retains CRLF.

Next extend the existing export form with the admitted mixed catalog, explicit
source/category/row/template choices, display projection loss confirmations,
linked typed preview and complete ROI/ZIP queue validation. Reuse staged-file
publication, current-source checks and workflow reservation, then add captured
mixed export review/rebind and raw C++/Python/installed probes. Dedicated GUI import
limit controls, broader bare-input remapping and direct payload editing during
import review remain pending. Continue typography/font registry, canvas editing/
remap, generic mixed workflow execution, session launch, remaining input/IO checks,
matching releases and historical audits. All99 families remain42inprogress57open0complete;
the goal remains active. Source901 is still the last full source-release proof.

### Created-annotation export bases, prerequisite for mixed export controls

Review of the planned UI exposed a backend gap: native annotations created in the
editor have no imported source entry to pair with. Single ROI and mixed ZIP APIs
now accept an explicit generated basis. `fresh_source=True` requires Python
`source_index=None`; C++ uses the default unassigned index. The selected canonical
standard projection supplies the new bare basis. Exact current native geometry,
text/image payload, precision and opaque attributes remain independently bound
inside the existing native envelope. The operation writes no file, mutates no
document/archive/undo history and reads no source samples.

This choice does not infer historical provenance. Every original archive entry
is admitted first; category, row, plane/positions, projection agreement, loss
options, output limits and current-source guards remain enforced. Generated and
archived members may share one ZIP. Records containing a generated basis use
closed V2 member grammar with explicit `source_kind` and null generated index.
Entirely archived exports retain V1 grammar/output unchanged. Validation,
decode-request, replay and original-first rebind cover both versions; changing a
source tag/index/version or output without satisfying the full equation refuses.

`build/p06q-generated-export001/generated-export-handoff001.json` binds Build001,
Native001 (19 plus restricted host), Focus001 (33 new checks), Python001 (2344
passed, no skips), SDK001, Standalone001, Atomic001 and ten C++ UI001 regressions.
Checks cover created geometric/text/RGBA rows without source history, stored and
deflated ZIPs with independent CRC/member extraction, exact generated basis bytes,
native precision/attributes, mixed archive/generated batches, real-bool and index
ambiguity refusal, each image/text loss choice, unspecified text color, closed
grammar/output tampering, original-first history admission, stale owners/revisions,
explicit rebind, no mutation and zero dataset samples. The installed C++ SDK
consumer exercises generated bases too; the installed desktop runs its mixed
import regression. No mixed export UI acceptance is inferred from those probes.

`docs/parity/imagej-generated-export-commercial-source-review-001.json` records
original project-MIT implementation and source before/after identities, with no
new dependencies, fonts, third-party implementation or restricted features. Raw
Python capture folders were pinned and losslessly retained with SHA/size/CRC
verification before cleanup. Cleanup removed292011042 owned bytes and retained
16753843 compressed evidence bytes: net275257199 reclaimed. Receipts, source holds,
native/installed captures, current SDK/desktop products and useful cache remain.
The three preliminary UI draft files were moved into the owned
`build/p06q-mixed-export-ui001/drafts001` directory; none is production code or
claimed compiled. Protected historical paths and unowned imgui.ini were untouched.

Implementation order for the next increment:

1. Compose a mixed component into the existing export form. Admit captured
   originals before any destination choices; inspect rows/catalog on the shared
   worker. Keep legacy geometric V1/V2 dialogs and their regressions working.
2. Require independent archive/generated source, category and current row choices.
   Offer fresh, original or explicitly selected captured metadata templates. Reset
   loss acknowledgments when source/row/template changes; require plane/position
   confirmation and any representation reset or embedded-image regeneration.
3. Build native and standard projection previews from detached metadata with
   shared source pixels. Prevent duplicate display-layer alpha blending; handle
   linked XY/XZ/YZ membership and release textures while GL remains available.
4. Review each member and the complete ordered ZIP on the worker, with explicit
   safe names, duplicate refusal, compression and queue invalidation. Avoid UI
   copies of large image payloads; bound retained job and queue work.
5. Reuse staged atomic files, current-owner/revision checks and workflow reservation
   before publication. Keep overwrite/destination consent explicit. Route browse,
   cancellation and worker generations across legacy/mixed components so stale
   results cannot target the newly opened form.
6. Add paired real-input C++/Python and installed probes: created/archive sources,
   projection losses, previews, queue mutation, captured review/rebind, cancellation,
   stale source, failed reservation, overwrite, recording-off and file equations.

Large import-limit controls, bare-input remapping/payload editing, generic mixed
workflow execution, typography/font registry, canvas editing/remap, session launch,
remaining IO qualification, matching releases and historical audits remain active.
All99 families remain42inprogress57open0complete; the goal remains active and
Source901 remains the last full source-release proof.

### Shared mixed ROI/ZIP export controls and typed projection previews

The existing File export menus now open a mixed component for documents with
display rows, V3 history, created rows without imported sources, or captured mixed
export records. Older geometric V1/V2 dialogs retain their grammar and controls.
Both Dear ImGui hosts use the same implementation. Generated/archive basis,
category, current row, fresh/source/captured projection metadata and file-position
interpretation are explicit. Names do not infer associations. Every captured
original is admitted before current catalog/row/template choices become available.

Display controls require the applicable font rounding, alpha omission, unscaled
bounds, rounded origin and zero-color interpretation. Native geometry, text size,
RGBA pixels and opaque attributes remain exact. Representation reset and embedded
TIFF regeneration are separate choices; incompatible retained TIFF pixels refuse.
Common group/properties/overlay label fields survive an explicit cross-kind reset.
Group/properties overrides are independently reviewed and range checked.

Inspection, projection construction, queue admission, complete ZIP assembly and
atomic sibling-file staging run on the shared scheduler. Queue payloads use shared
immutable references on the UI thread; worker preflight precedes large owned copies.
ZIP requires unique portable member names, complete ordered review, and new review
after compression/removal. Expanded resource controls re-inspect originals and
clear the previous queue/choices. Logical phase budgets remain distinct from RSS;
maximum-size GUI admission still requires further qualification.

Native and standard projection previews use detached metadata sharing source
pixels and the existing typed renderer. Selected display rows suppress live display
layers while the preview is shown; repeated native rows render once, avoiding
double alpha blending. Hide preview restores live layers. Linked and ordinary
canvas paths retain clipping/membership behavior and GL lifetime cleanup. Member
navigation uses native plane/fixed coordinate/T/C. A compact review window keeps
edit/output/export/close controls visible.

Publication checks the active owner/pixel/metadata identity and reserves workflow
before publishing the staged destination. Existing-file consent, competing files,
Ready cancellation, stale sources and malformed workflow reservation are checked.
Export changes no document metadata or undo history; recording-off changes no
workflow. Captured review starts with explicit new choices and no output path, and
keeps the original record unchanged. A route epoch prevents same-owner legacy and
mixed browser requests from coinciding across independent worker generations.
Generic mixed workflow/batch execution remains a separate integration gap.

`build/p06q-mixed-export-ui001/mixed-export-ui-handoff001.json` binds final Build006,
Native002 (19 plus restricted host), Python002 (2349 passed, zero skips), SDK002,
Standalone002, Atomic002, ten prior C++ UI003 regressions and Mixed Export UI005.
Five new Python checks combine paired actual C++/Python controls, native/standard
image alpha, geometric/text precision, complete file/ZIP equations, stdlib ZIP CRC
and ordered extraction, generated/archive source handling, explicit losses,
duplicate/compression/removal invalidation, retained TIFF regeneration, metadata
overrides/reset, malformed originals, destination consent, cancellation, file races,
stale staging, recording-off and failed reservation. The new native and installed
probes each exercise171 physical controls and836 bounded raw frames; worker idle
frame counts may vary. Direct protocol checks additionally cover same-owner browse
routing across both components. API fixture actions are identified in the reports.

The installed desktop executes both import and mixed export cases. Real native
and installed OpenGL screenshots were visually inspected; PNG color checks pass.
The shared core's installed SDK consumer retains generated/archive export coverage.
A regression run caught legacy scheduler initialization order: moving the shared
scheduler into the mixed component before initializing the legacy member left the
legacy pointer empty. Initialization order was corrected and the complete suite
rerun. The failed cpp-export001 process receipt and source-freeze evidence remain.
The full regression was repeated after tightening common template metadata and
adding cross-component browse checks; final source identities match all final runs.

`docs/parity/imagej-mixed-export-ui-commercial-source-review-001.json` binds original
project-MIT code and before/after source identities. No dependency, font, imported
third-party implementation or restricted feature was added. Final Python captures
and superseded successful raw captures were pinned, losslessly compressed and
verified by member size/SHA/CRC before cleanup. Cleanup removed1041556429 owned
bytes and retained62688476 compressed evidence bytes, reclaiming978867953 net.
Final native/installed captures, current SDK/desktop products, receipts, source
holds, the failed regression and useful cache remain. Protected historical paths
and unowned imgui.ini were untouched; window.cpp retains CRLF.

Next close dedicated import-limit controls and qualify maximum GUI resource paths.
Then broaden bare-input remapping and payload review, integrate generic mixed
workflow/batch execution, and continue typography/font registry, canvas editing and
remap, session launch, remaining IO checks, matching releases and historical audits.
This packet qualifies the declared mixed export UI increment; it does not provide
a new installed Python wheel, full source release or whole-family acceptance.
All99 families remain42inprogress57open0complete; the goal remains active.
Source901 remains the last full source-release proof.

### Explicit import limits and maximum embedded-image GUI paths

Both hosts now provide a separate Import limits window with17 exact integer
fields covering batch members/input/provenance/decoded data, ROI wire/text/vertices/
mask/path/image bounds, retained display storage, and ZIP archive/member/count/name/
working bounds. Supported-range tooltips explain the native embedded-image cap.
Default and large-input presets fill a draft. Invalid/overflowing values refuse;
cancel preserves active limits and preparation. Accepted changes invalidate old
preparation; file inputs clear and reload, while captured inputs keep their
admitted immutable originals and re-review with the new explicit limits. Captured
limits retain exact byte precision. A malformed original cannot expose the limits
editor or use new limits to bypass admission.

Single-ROI file size is checked against the smaller batch/wire bound before
allocation. ZIP checks compressed archive size and expanded member/total/count/
name/working caps, combining transport and semantic limits. Loaded binary inputs
are shared immutably in the draft; owned request copies move to the preparation
worker. Replacement readers also capture shared wire bytes. Single-ROI load
estimates avoid reserving the ZIP workspace; configured decoded/ZIP budgets apply
to their relevant loading phases. Logical phase limits, retained draft storage,
scheduler estimates and whole-process RSS remain distinct; no RSS quota is claimed.

`build/p06q-import-limits-ui001/import-limits-ui-handoff001.json` binds Build007,
Native002 (19 plus restricted host), Python002 (2354 passed, zero skips), SDK002,
Standalone002, Atomic002, eleven prior C++ UI003 regressions and Limits UI005.
Five new Python checks combine paired C++/Python physical control logs with strict
integer/default/preset/cancel behavior, tiny input/decoded refusals, exact captured
limits and foreign-owner review, malformed-original admission, source/history
purity, independent ZIP CRC/member extraction and native/standard payload equations.
The final native and installed limits cases each exercise116 physical controls;
bounded frame counts vary with worker timing (1415/1391 observed). Fixture-only API
actions, including maximum RGBA creation and foreign destination activation, are
identified separately in the raw reports.

The boundary fixture is a1024x1024 embedded display image: maximum native RGB
import, an explicit exact4194304-byte RGBA row with scaled bounds, single ROI and
ZIP export, then native ROI and ZIP reimport. The native ROI is34839236 bytes;
default limits refuse it before loading, while explicitly larger limits admit it.
The compressed ZIP is also refused under default expanded-member limits and
admitted after explicit larger limits. Native RGBA bytes, bounds, opaque attributes
and the bare imported RGB original remain exact; the standard projection is RGB
with explicit alpha omission and unscaled bounds. Importing the identical ZIP
creates no additional document edit or workflow entry. Source scientific pixels
remain shared. This qualifies the declared embedded-payload boundary, not a maximum
scientific dataset size or arbitrary multi-member stress matrix.

Captured review uses an exact non-MiB decoded budget (134217711 bytes), admits the
original before exposing controls, requires explicit destination confirmation,
refuses under a newly chosen tiny budget, and succeeds after explicit larger
limits. Cancel leaves the foreign destination unchanged. The original captured
record retains its limits. Replacing a known captured after-display field with an
invalid value refuses before the limits control becomes available.

Actual native and installed OpenGL limits/maximum-image views were inspected;
the field list scrolls independently and Apply/Cancel remain visible. Maximum
preview PNG color checks and native/standard alpha checks pass. SDK consumption
of the unchanged public core interface and three installed desktop cases pass.
No new installed Python wheel, full release or upstream-exact typography is claimed.

A legacy captured-import regression exposed Apply moving below the visible dialog
when the new button added a row. The limits button now shares the destination
confirmation row, restoring the prior layout; native and full Python regressions
were rerun. The failed cpp-rebind001 receipt/log/source-freeze remain, together with
an explicitly labeled one-line reconstruction whose bytes/SHA match that failed
source freeze. Final source identities match every final qualified run.

`docs/parity/imagej-import-limits-ui-commercial-source-review-001.json` records
original project-MIT implementation and before/after identities, with no new
dependency, font, imported third-party implementation or restricted feature.
Raw Python captures and superseded successful native/installed captures were
pinned, losslessly retained and verified by member SHA/size/CRC before cleanup.
Cleanup removed1340606264 owned bytes and retained61271711 compressed evidence
bytes, reclaiming1279334553 net. Final captures, source holds, current SDK/desktop
products, receipts, the failed layout regression and useful cache remain. Protected
historical paths and unowned imgui.ini were untouched; window.cpp retains CRLF.

Next broaden bare-input remapping and payload editing within import review, then
integrate generic mixed workflow/batch execution. Continue typography/font registry,
canvas editing/remap, session launch, remaining IO stress, matching releases and
historical audits. All99 families remain42inprogress57open0complete. The goal
remains active; Source901 remains the last full source-release proof.

### Bare-source native edit API prerequisite and source archive V4

The mixed C++/Python importer now accepts an explicitly mapped bare source plus
a complete native replacement. Original transport bytes, original mapping and
edited native row remain independent. Geometry, positions, text precision/content,
RGB/RGBA payloads and opaque attributes are retained through their native models.
Original byte-codec/mapping admission occurs before applying a native override;
valid replacement data cannot hide an invalid original, unspecified source text
color or ambiguous legacy stack position. Source and destination extents may differ
when the explicit native row is valid for the destination.

Source archive V4 uses the existing heterogeneous entry fields but permits the
new bare-mapping/replacement combination. It is canonical only when at least one
such combination exists. V3 keeps its exclusion rule and unedited/envelope-only
inputs continue to use V3. Append retains V4 while the edited source survives;
Replace can produce V3 after admitting the entire original history. Legacy V1/V2
geometric resolvers retain their exclusion rule and reject the new combination.
The private geometric admission helper has an explicit opt-in used by the mixed
API/V4 reader; legacy paths keep the default exclusion. The outer import/export
command fields and native ROI/IJD1 envelope formats are unchanged; the nested source
archive carries the version change. V4 catalogs, pairing, export, replay/rebind,
request decoding and session preservation are qualified. UI source-version routing
recognizes V4, but no bare-edit authoring controls are claimed yet.

Both Python import-item constructors retain the16MiB default ownership cap and
accept an explicit positive integer max_input_bytes up to64MiB. This consents to
the input copy only; semantic wire/batch/decoded/provenance limits remain separate.
Default behavior and constructor positional arguments remain compatible. Large
native RGBA envelopes can now be explicitly copied and reimported through Python,
matching the core's supported input bounds rather than hitting a fixed16MiB cap.

`build/p06q-bare-edit001/bare-native-edits-handoff001.json` binds Build005, Native003
(19 plus restricted host), Python003 (2391 passed, zero skips), SDK002, Standalone002,
Atomic002, twelve prior native UI002 cases and three focused export diagnostic
reruns. There are37 new Python checks. They cover independent original/current rows
for all three kinds, precise geometry/text/RGBA/opaque attributes, source/target
extent separation, canonical V4/V3 transitions and downgrade refusal, full historical
admission even for Replace, legacy exclusions, sessions/captured equations, replay/
rebind original-first checks, original color/legacy-position interpretations,
constructor integer/byte ownership bounds, maximum1024x1024 bare RGBA edit/export,
and explicit large Python native-envelope reimport. Pure resolutions read no
scientific dataset samples. Session IO remains explicit and may read pixels.
Native import/export tests and the installed SDK consumer exercise V4 sources too.
Installed desktop import/export/limits cases and all existing UI regressions pass.

A prior120-second whole-suite run timed out without an assertion report; partial
legacy ROI export fixture files were its newest artifacts. The process tree was
terminated and verified empty. A diagnostic rerun enabled verbose test progress,
noninteractive Windows fatal-error handling, traceback diagnostics and a180-second
supervisor bound; all2391 checks passed in71.8s. Three focused legacy export reruns
also pass. The isolated timeout root cause remains unconfirmed, so the entire
failed python002 stage, logs and source freeze remain intact. This scoped API
increment does not claim a long-running race qualification or a new full release.

`docs/parity/imagej-bare-native-edits-commercial-source-review-001.json` binds
original project-MIT implementation and before/after identities. No dependency,
font, imported third-party implementation or restricted feature was added. Successful
raw Python captures and superseded native/installed captures were pinned, retained
losslessly, and verified by member SHA/size/CRC before removal. Cleanup removed
1206003620 owned bytes, retaining61576716 compressed evidence bytes and reclaiming
1144426904 net. Final captures, current SDK/desktop, source holds, receipts, useful
cache and the timed-out stage remain. Protected historical roots and unowned
imgui.ini were untouched; window.cpp retains CRLF.

#### Next UI integration contract

1. Add Edit mapped annotation to bare members. Admit the original with its explicit
   source mapping on the worker, apply any existing native override, then seed the
   existing geometry/display replacement editor. An unselected/ambiguous mapping
   must refuse before opening an editable row. Keep the raw shared byte buffer.
2. Freeze the complete admitted bare mapping while an edit is staged, including its
   original extent, position policies and text fallback. The current destination
   can have another extent. Do not silently rebuild the source mapping using the
   destination shape. Capture the admitted source_mapping from the worker result;
   preserve it in subsequent requests. Lock interpretation/mapping controls until
   an explicit reset. Apply-to-all mappings must skip edited members.
3. Route geometry-only bare edits through the mixed resolver. Effective geometric
   replacements must be forwarded for bare inputs as well as envelopes. Display
   requests forward the original mapping and complete native replacement. Explicit
   reset may drop the draft bare override and unfreeze its mapping; it must never
   mutate the captured original command or incoming bytes. Preserve existing
   envelope reset behavior and normal legacy dialogs.
4. Reuse DisplayAnnotationPanel through a private document/facade, rather than
   duplicate text/image field logic. A native Document(snapshot) shares source
   pixels and preserves calibration/sample/display policy; replace only its private
   display rows with the edited draft. The facade constructor's admission copies
   plane-table identity, not samples. Verify no samples are read on opening; only
   the explicit capture-image action may read them. Apply callbacks commit only
   to this private document. The real document changes only on complete import Apply.
5. Give the native panel a draft mode with an initial selected row, distinct title/
   help, scrollable fields and visible footer. Keep its ordinary mode/raw keys stable.
   On closing, return only the selected committed private row, not un-applied or
   invalid input fields. Seed that row into the outer replacement draft and reset
   destination confirmation. Parent Stage remains required; parent discard, close,
   source change or memory-cap change must dispose the private context. Avoid any
   implicit member deletion when manipulating private rows. Preserve opaque row
   attributes on property edits; new/template replacement remains explicit.
6. Add paired C++/Python/installed physical-input cases for bare geometry remaps,
   text/font/color/content edits, image bounds/opacity/zero/capture edits, cross-kind
   templates, exact old source mapping versus new native extent/plane/T/C, reset,
   cancellation/staleness, malformed original-first refusal, all-or-nothing Apply,
   recording-off, undo/session, and independent native/source/archive equations.
   Preserve source-frozen build/test boundaries and cleanup owned scratch only.

After these controls, integrate generic mixed workflow/batch execution, then
continue typography/font registry, canvas/remap/session launch, IO stress, matching
releases and historical audits. All99 families remain42inprogress57open0complete;
the goal remains active. Source901 is still the last full source-release proof.


### Bare mapped annotation and private payload editing UI increment

The preceding UI integration contract is implemented and qualified in
`build/p06q-bare-edit-ui001/bare-edit-ui-handoff001.json`. Bare geometric/text/image
members now enter the existing native replacement editors after worker admission
of the original. Captured original mapping, extent, position policies and text
fallback remain independent from the native replacement and current destination.
Staging locks source interpretation; bulk mapping skips these rows and explicit
reset restores the original bare source. Geometry-only bare overrides route through
the mixed resolver and V4 archive too.

Display replacements use DisplayAnnotationPanel on a private native document and
facade sharing source pixel storage, calibration, display and sample policy. The
private row replaces only that draft's display set. Opening reads no samples;
explicit capture is the pixel-reading operation. Child Apply updates only the
private document. Closing returns its selected committed row, ignores unapplied
fields and resets outer confirmation. Parent Stage and full import Apply remain
required. Discard, close, cap changes and stale sources destroy the private context.
The normal annotation editor and its existing raw control keys remain compatible.

Build003, Native001, Focus001, Bare002, twelve prior native UI cases, Python001,
SDK001, Standalone001 and Atomic001 bind twenty passed source-frozen jobs.
Python001 passed 2,399 checks, zero failures/errors/skips, in about79 seconds;
eight new checks independently inspect both hosts' physical input reports. Cases
cover precise native point/text fields, source ZIP byte equality, RGB originals
versus RGBA templates, opaque attributes, color admission, locked/reset/bulk mapping,
private/no-read/unapplied/cancel behavior, failed reservation, undo/redo, distinct
old/new extents and T membership, recording-off, stale/cap disposal, session reopen,
alpha composition and native OpenGL preview. Installed desktop runs mixed import,
mixed export, resource-limit and new bare-edit cases. Native19 plus restricted host
and an installed public SDK consumer passed. The prior API packet's isolated timeout
remains unexplained and retained there; this run does not establish its cause.

`docs/parity/imagej-bare-edit-ui-commercial-source-review-001.json` binds before/after
source identities: original project-MIT code, no new dependency/font/imported product
code or restricted feature. It is an authorship/integration attestation, not an
independent legal review. Final native/installed captures, SDK, source holds and
active build cache remain. Scratch was SHA/size-verified in a lossless archive
before journaled contained deletion: 428,786,066 removed, 42,296,187 retained,
386,489,879 net reclaimed. No historical/protected root was touched.

#### Next: generic mixed workflow and batch execution

1. Audit the generic runner's supported typed schemas and reject unsupported mixed
   steps before starting jobs or writing output. Per-row import/export review and
   direct guarded replay already exist; generic run/batch remains separate work.
2. Add one native planning/execution path over detached scientific documents. Reuse
   original-first V4 admission/rebind and native annotation/source equations. Require
   explicit per-destination mapping and native replacement decisions; preserve T/C/Z,
   calibration, sample/display policy and opaque attributes. Avoid routing scientific
   documents through the legacy Python model adapter's limited [Z,]YXS copies.
3. Plan the entire workflow against evolving detached state. Define import/export
   ordering, no-op handling, source/history ownership, cancellation checkpoints and
   memory reservations. Publish the active-image result atomically only after all
   steps pass. Validate export destinations/conflicts before writes and use the
   existing atomic export publisher; document per-file batch failure behavior.
4. Expose the same planner in both hosts, with reviewable explicit destination
   choices and progress/cancel behavior. Python should bind/adapt native execution.
   Keep existing legacy schemas and dedicated import/export review behavior stable.
5. Qualify import followed by filtering/export and repeated imports, V3/V4 history,
   distinct destination extents, unsupported schemas, late bad originals, stale
   sources, reservation refusal, cancellation/no-op, output conflicts and installed
   consumers. Reuse current fixtures/probes rather than duplicate codec work. Then
   continue typography/font registry, canvas/remap/session launch, IO stress and
   matching releases/historical audits.

This closes the declared bare-edit review controls only. All99 families remain
42inprogress57open0complete; the broad goal remains active. Source901 remains the
last full source-release proof.


### Native scientific workflow planner foundation

`build/p06q-native-workflow001/native-workflow-handoff001.json` qualifies the shared
native planning/execution prerequisite for generic mixed Run/Batch. It does not
qualify generic host controls or external publication. The existing runner was
audited: desktop runWorkflow requires legacy editability and rejects mixed schemas;
batch creates its directory before full validation, reads legacy image stacks and
writes NPY. The Python adapter also has a limited [Z,]YXS model contract. These
paths require explicit migration rather than routing scientific data through them.

The new public C++ API and Python bindings support ordered processing intent,
explicit mixed import/rebind and pure mixed ROI/ZIP export/rebind. Complete request
structure, supported operation/target policy, captured originals, bare originals
and mapping/native replacements are admitted before processing reads. Later row
pairing/output equations use evolving detached state. Private owners preserve the
source dataset descriptor, calibration, sample/display policy and opaque metadata,
with at most one temporary undo entry. Each export retains its exact intermediate
snapshot and bytes. The final proposal exposes one Data-guarded transaction for
the real owner. Empty/export-only proposals are no-ops; current display navigation
can coexist. No log mutation or filesystem write occurs in this path-free planner.

Captured Gaussian/rank intent is validated and rebound with explicit TargetRequest;
parameter overrides are refused. Gaussian destination calibration changes require
explicit consent. Import/export originals remain independent from new explicit
items/projections. V4 source history survives processing and repeated import.
Cancellation/progress/throwing callbacks abort the proposal; late invalid pairings,
stale publication, resource refusal and duplicate/path output tokens cannot publish
partial live data. Cumulative logical admission includes full backing per processing
step, metadata and request/output charges. It is conservative rather than a process
allocation quota; kernel/codec phase scratch and custom reader caches are separate.
Python reuses existing bounded constructor adapters, then holds immutable request
owners across GIL release and uses a private borrowed-view path to avoid copying
all imported image payloads again.

Twenty final source-frozen jobs passed: Build005, Native004, Focus004, Python001,
SDK001, Standalone001, Atomic001 and thirteen native UI regressions. Python001 passed
2,814 checks in about75 seconds, zero failures/errors/skips. This includes35 new
planner cases and380 additional existing targeting/execution/jobs/Gaussian/rank
workflow checks beyond the previous ROI suite. Native20 plus restricted host passed.
The installed SDK consumer compiles the new header and executes ordered scientific
import/export/filter/reimport, intermediate snapshots, atomic undo/redo, original-first
refusal, limits, cancellation, stale/no-op and captured Gaussian cases. Installed
desktop import/export/limits/bare-edit regressions passed too. No new UI acceptance,
wheel/source release or family completion is inferred from these regressions.

Initial build/test failures are retained: the first build had incorrect shared-image
access in the new fixture; initial execution exposed an invalid zero-history policy,
corrected to bounded one-entry history. Subsequent fixture repairs supplied explicit
unpositioned-source coordinate/projection Z and used the immutable calibration
constructor. Final independent Python and native/SDK equations pass. The previous
API packet's isolated timeout remains unexplained in its retained evidence.

`docs/parity/native-workflow-commercial-source-review-001.json` binds original
project-MIT source identities; no dependency, font, imported product code or
restricted feature was added. This is an integration/authorship attestation, not
independent legal review. All failed runs, final native/installed captures, SDK,
source holds and active cache remain. SHA/size-verified lossless scratch retention
preceded contained journaled deletion: 369,887,390 bytes removed, 37,803,717 retained,
332,083,673 net reclaimed. No historical/protected root was touched.

#### Next host and destination integration contract

1. Add a closed host workflow compiler over the recorded table. Validate all schemas
   and explicit per-step import items/native replacements, processing targets,
   calibration consent and export projection choices before starting jobs or creating
   directories. Reuse this planner; do not use the legacy scientific flattening path.
   Preserve dedicated per-row review/replay and legacy workflow behavior.
2. Carry the original active-owner/activation, Data revisions, resource cap and
   recording state through preparation. Reserve the full workflow-log publication
   before live mutation. Publish the planner transaction through the controller as
   one native edit, preserving current display and rejecting stale/closed/busy owners.
3. Compile output tokens to explicit filesystem destinations. Preflight the entire
   set, including canonical/case/path aliases, existing-file policy and conflicts
   with batch inputs. Intermediate export records refer to detached owners: guard
   publication with the workflow's original source rather than replaying them on the
   live owner. Reuse atomic file staging and define/document multi-output and per-file
   batch failure/cancellation semantics before enabling writes.
4. Replace legacy batch image loading/output with scientific native/session-aware IO.
   Plan each destination against its full axes, calibration, sample policy and
   annotation history. Display per-item status and committed output information;
   cancellation must not misreport already committed files as untouched.
5. Qualify shared C++/Python controls and installed consumers for mixed repeated
   imports/filter/exports, V3/V4, distinct destination extents and mappings, malformed
   late inputs, unsupported schemas, reservations, stale/cap/closed owners, no-ops,
   recording-off, source/output conflicts and cancellation. Add explicit axis-remap
   step types before admitting geometry changes; do not silently clear annotations.
   Then continue typography/fonts, canvas/remap/session launch, IO stress, matching
   releases and historical audits.

All99 families remain42inprogress57open0complete. The broad goal remains active;
Source901 remains the last full source-release proof.


### Shared native Run review and staged new-file publication increment

`build/p06q-workflow-run-ui001/workflow-run-ui-handoff001.json` qualifies native
Run integration in both ImGui hosts. Scientific datasets and supported captured
native import/export programs enter a closed shared review. Each step requires
confirmation; processing provides current-plane/all-Z/ranges/ordered-positions and
whole-plane/active-selection/positioned-ROI choices, plus Gaussian calibration
consent. Native and legacy geometric import/export records are fully admitted and
compiled to the shared native planner; original mappings/replacements/projections
remain explicit retained choices. Legacy export limits are carried through their
pairing source limits. Unknown schemas/fields and malformed originals refuse before
job admission. Legacy batch also validates supported schemas before mkdir/job start.

The new review currently reuses captured import/export choices. It does not yet
embed full per-step mapping/payload/projection editors or prepared image preview.
The existing dedicated review flows can author those choices. Scientific batch,
axis-changing steps and broader typed workflow schemas remain pending. Ordinary
legacy-only Run behavior is preserved; generic captured-export Run now opens explicit
native review instead of unconditionally refusing, while unsupported Batch stays
refused. No full workflow-family credit is claimed.

A logical JSON tree/byte scan with fourfold copy allowance bounds review admission
before record decoding/copying. Step requests retain immutable shared ownership;
preparation borrows those owners, avoiding another imported image-payload copy.
The native worker plans all steps, checks output extensions/existing parents,
canonicalizes parents and rejects path aliases (Windows ordinal case-insensitive
comparison). No parent directory is created. Every output is staged through the
existing exclusive-sibling atomic writer after complete plan success. Prepared
results keep the scheduler's declared reservation until released. Pixel/source
records, exact current rows and V4 original/native separation remain unchanged.

Apply validates active source and Data revisions, visual admission and table state
before file publication and one controller transaction. It retains the program
table; replay does not append itself. Current display can remain fresh. Files are
new-only and publish individually before the document commit. A later failure
leaves earlier files intact, reports their preallocated published flags and keeps
the workflow document edit unapplied. Retry of the same attempted bundle is disabled.
This is per-file atomicity, not an all-files filesystem transaction. Cancel can stop
preparation or invalidate a ready result; Apply begins the synchronous publication
boundary. Off-thread Window.cancel uses atomic epochs and scalar scheduler IDs, so
it does not touch creator-thread panel fields; an epoch also closes the submission/
ready cancellation race. Source/activation, accepted cap, recording or table changes
dispose uncommitted state. The scheduler can reject cap reductions below retained
reservations without accepting a settings change.

Build006, Native001, Focus002, Run005, thirteen prior native UI cases, Python001,
SDK001, Standalone001 and Atomic001 bind twenty-one passed source-frozen jobs.
Python001 passed 2,820 checks in about77 seconds with zero failures/errors/skips.
Six new paired-host tests inspect physical inputs and independent output/session
semantics; two older export checks now assert explicit Run review and unchanged
Batch refusal. Cases prove V4 mixed import/filter/ZIP, exact scientific T/C/Z planes,
precise point/opaque uint64 fields, explicit target freezing versus live camera,
original/native ZIP equations, one undo/redo, new-file races, no-overwrite winners,
partial status, alias refusal, cancel/close, off-thread ready cancellation, results
refusal, stale/cap cleanup, recording-off, session pixels/metadata and OpenGL layout.
Metadata-only admission is measured without renderer frames; renderer-inclusive
opening reads are separately recorded and are not claimed to be zero. Native20
plus restricted host, installed public SDK and five installed desktop cases passed.

Retained failures document the initial busy-without-job optional access, corrected
by a job-info-based processing overlay, and a missing capture in the cancellation
implementation. Later fixture repairs refreshed a disabled host control after
staleness, tested cap disposal via an accepted increase instead of a reservation-
violating decrease, and isolated admission reads from ordinary rendering. Python
checks also exposed creator-thread cancel dispatch behind the general busy gate;
that route now runs before it. All final cases pass. Earlier isolated API timeout
evidence remains retained in its original packet; its cause is not established here.

`docs/parity/workflow-run-ui-commercial-source-review-001.json` binds original
project-MIT before/after identities. No dependency/font/imported product code or
restricted feature was added. It is an authorship/integration attestation, not
independent legal review. Final native/installed captures, SDK, source holds and
active cache remain. SHA/size-verified lossless scratch retention preceded contained
journaled deletion: 469,485,418 bytes removed, 44,270,869 retained, 425,214,549 net
reclaimed. All failed stages remain intact; no protected/historical root was touched.

#### Next: scientific Batch and complete per-step authoring

1. Reuse the confirmed immutable program/target model and native planner for Batch.
   Add explicit input selection and existing output-directory selection. Load native
   sessions through the bounded core session APIs, preserving T/C/Z/Y/X/S, calibration,
   sample/display policy and annotation history. Define explicit multi-document input
   handling; do not silently take one owner or flatten scientific inputs through
   readImages/NPY. Keep legacy raster behavior where its axis contract is explicit.
2. Preflight the whole output plan before creating anything: every input and output
   token/path, canonical/case aliases, source/output conflicts, duplicate basenames,
   existing-file policy and per-item mapping/target compatibility. Stage complete
   native session outputs with all metadata; preserve inputs. Carry root owner/
   activation/program/cap guards through worker preparation and publication.
3. Declare batch per-item failure/cancellation semantics before enabling writes.
   Show queued/prepared/committed/failed/canceled items and every committed path.
   Reuse new-only atomic staging and preallocated publication status. Do not report
   already committed files as untouched. Bound aggregate retained plans and session
   codec phases; avoid holding an unbounded set of decoded scientific documents.
4. Reuse RoiImportPanel, display/geometry replacement editors and RoiMixedExportPanel
   on private prefix snapshots for per-step authoring inside Run. Return typed choices
   rather than duplicating payload/codec logic or mutating the live image. Invalidate
   downstream prefix caches when a step changes; retain immutable bytes to avoid
   repeated large copies. Add complete summaries (including generated-source labels),
   prepared image/annotation preview, step edit/reorder/disable and explicit limits.
5. Qualify both hosts and installed consumers for multi-input scientific sessions,
   V3/V4/repeated imports/filter/exports, differing extents and explicit replacements,
   unsupported/malformed late inputs, source/output conflicts, alias races, partial
   commits, stale/cap/closed owners, reservations, no-ops and cancellation. Add remap/
   branch step types before admitting geometry changes; preserve annotation policy.
   Continue typography/fonts, canvas/remap/session launch, IO stress, matching releases
   and historical audits without treating this increment as whole-family completion.

All99 families remain42inprogress57open0complete. The broad goal remains active;
Source901 remains the last full source-release proof.


### Native scientific session Batch checkpoint (latest)

This checkpoint supersedes the preceding scientific Batch-pending clauses for
native `.ijx` session inputs. `build/p06q-workflow-batch001/workflow-batch-handoff001.json`
binds the public C++/Python API, shared ImGui review, source holds and 22 passed
source-frozen jobs. No whole-family or release completion is claimed.

`native_workflow_batch.hpp` adds immutable Python request DTOs and a move-only
prepared bundle. Selected-document-index or all-documents processing uses the
existing native workflow planner and borrowed immutable step requests. Sessions
decode one at a time; whole-batch semantic/path/output-encoding admission precedes
exclusive staging. Bounds cover input/output counts, path/name sizes, aggregate
retained outputs and codec/planner phases. These are logical admission budgets,
not whole-process allocation quotas. Input tables/extensions and unselected
documents are retained; embedded input workflows are never executed.

The existing output parent must exist and every destination must be new. Exports
use `<stem>.d<one-based-document-index>.<step-token>` and complete sessions use
`<stem>.ijx`. Duplicate/hardlinked inputs, duplicate/case-aliased destinations,
source/output conflicts and existing files refuse. Publication runs in input order,
each input's exports before its session. A late creation race preserves earlier
files, marks partial/failed and later skipped items, retains output flags and
disallows retry. Preparation/ready cancellation removes owned staging. Publication
is a synchronous boundary without user callbacks; live/input documents never change.

Both hosts expose physical input and folder browsing, per-input output-stem edits,
explicit document policy, retained import/export confirmations, filter targets,
calibration consent and per-file policy confirmation. Worker progress is synchronized;
ready results retain the scheduler reservation. Root owner/Data/program/settings
guards apply at publication. Cancel remains callable off-thread through atomic
epochs/job IDs. A scrolling child keeps the footer accessible. Final-stage
cancellation reports canceled/skipped rather than stale prepared labels.

Build006, Native003, Focus004, Batch004, Run002, thirteen prior native UI002 jobs,
SDK002, Standalone002, Atomic002 and Python002 passed. Python002 contains 2,845
checks with zero failures/errors/skips: nineteen new core batch checks and six
paired-host UI checks. Native21 plus restricted host, fifteen native UI cases,
an installed batch SDK consumer and six installed desktop cases passed. Evidence
includes V4/native RGBA imports and ZIP exports, exact scientific planes, unselected
quiet-NaN payload bits, tables/extensions, mixed extents, per-file races/partial
publication, later malformed input, cancellation/staleness and unchanged input bytes.
Earlier failed fixture/build stages remain intact. Final native/installed captures,
SDK, source holds and active cache remain. Verified lossless cleanup removed
1,086,740,148 bytes, retained 98,251,940, and reclaimed
988,488,208 net bytes, only within this owned packet root.

`docs/parity/workflow-batch-commercial-source-review-001.json` attests original
project-MIT authorship/integration, with no new dependencies/fonts/imported product
code or restricted feature. It is not independent legal review.

#### Next packet: complete per-step review authoring

1. Build a bounded private prefix snapshot for the selected workflow step. Preserve
   immutable captured originals and share dataset storage; do not run filesystem
   exports or mutate the live document to produce a review prefix. Show explicit
   memory-limit and unsupported-step refusals before opening nested editors.
2. Reuse RoiImportPanel/native replacement editors and RoiMixedExportPanel against
   that prefix. Return typed import mapping/payload and export source/current-row/
   projection choices. Keep the captured original equation separate from destination
   choices. Closing a child without confirmation leaves the parent draft unchanged.
3. A changed step invalidates downstream prepared prefixes and publication staging.
   Require renewed confirmation where destination compatibility changes. Add the
   image/annotation prepared preview and step edit/reorder/disable controls under
   the same root guards and retained-resource ownership. Avoid a second codec/editor.
4. Extend batch to explicit differing per-input mappings/targets only after this
   typed model exists. Preserve the current closed preflight and per-file reporting.
   Add geometry-changing remap/branch step types with explicit annotation policies
   before admitting them. Keep scientific raster IO/axis policy separate from
   native-session batching and legacy raster behavior.
5. Qualify canceled/stale nested drafts, changed extents, opaque precision/RGBA
   payloads, repeated import/export steps, exact targets, reservation limits and
   partial publication in both hosts and installed consumers. Continue typography,
   canvas/session launch, IO stress, matching releases and historical audits.

All99 families remain42inprogress57open0complete. Source901 remains the last full
source-release proof; the broad parity goal remains active.


### Private workflow-prefix choice authoring checkpoint (latest)

`build/p06q-workflow-author001/workflow-author-handoff001.json` binds the shared Run
review's nested import/export choice editors and 22 passed source-frozen jobs. This
implements the preceding private-prefix/typed-choice integration for open Run drafts.
It does not complete prepared viewport, portable program authoring, processing
parameter editing, reorder/disable or differing per-input Batch choices.

After earlier steps are confirmed, **Edit this step's choices...** prepares an
asynchronous native prefix on a detached owner. It reuses the same effective target
builder as full Run. Earlier pure exports are validated and encoded without any
filesystem stage/publication. Prefix retention and preparation reserve half the
processing setting; child phases retain their existing separate job reservations.
Canceled running child work keeps the prefix reservation until retirement, and
new work waits for that retirement. Source/activation/program/accepted-setting
guards cover the parent and children. Atomic epoch/scalar job cancellation remains
safe off-thread; waiting and job diagnostics include nested workers.

RoiImportPanel draft mode reuses mapping, precise geometry, private text/image
payload and explicit-limit editors. RoiMixedExportPanel draft mode reuses the
source/current-row/projection/ZIP authoring model but exposes no path, overwrite,
browse or filesystem-publication control. Its confirmation callback returns the
path-free resolved export. Both forms return typed requests to the parent after
full validation. The immutable original equations remain separate from new choices.
Edited records and the original table share bounded logical retention admission.
Closing a child keeps the prior parent draft. Reopening uses the authored request.
An accepted change clears confirmations from that step onward; earlier processing
target changes do the same. Parent Apply still stages new files only after complete
planning and publishes through the existing guarded one-document transaction.

Build006, Native001, Focus003, Run004, fourteen other native UI001 cases, SDK001,
Standalone001, Atomic001 and Python001 passed. Python001 has 2,850 checks with zero
failures/errors/skips. Five new paired-host checks extend the existing Run fixture
to physical nested authoring, exact double geometry/opaque uint64 fields, Unicode
text and precise font payloads, archived original wire, projection group, ZIP member
name and uncompressed output, downstream reconfirmation, reopening/discard,
off-thread cancellation and stale source disposal. Native21 plus restricted host,
fifteen native UI cases, installed SDK compatibility and six installed desktop
cases passed. The rendered nested form was inspected with accessible Keep/Close
controls and no file output controls. Earlier failed stages remain intact: one
compile name collision and fixture mistakes selecting a generated rather than an
archived source and using the display editor's cancel key for a geometry editor.

Original project-MIT source identities are attested in
`docs/parity/workflow-author-commercial-source-review-001.json`; no dependency,
font, imported product code or restricted feature was added. This is an authorship/
integration attestation, not independent legal review. Verified lossless cleanup
removed 424,863,649 bytes, retained 41,303,462, and reclaimed
383,560,187 net bytes within this owned packet root. Final captures,
SDK products, failures, source holds and active cache remain.

#### Next packet: portable edited programs and prepared viewport

1. Introduce a typed C++ edited-program/draft model, available through Python, that
   owns immutable originals separately from explicit typed choices and targets.
   Define a closed, versioned portable record and complete structural/resource
   validation before copying/decoding or running kernels. Keep old captured records
   readable; do not serialize private owner IDs as destination intent. Round-trip
   mappings, payload precision, limits, source pairing, projection, packaging,
   calibration consent and explicit targets. Separate saved intent from file-write
   consent and output paths. Run can then save/reopen its edited draft explicitly.
2. Build prepared image/annotation views from the selected prefix/final proposal,
   using an independent native display state and the existing raster/annotation
   renderers. Never present a changed prefix on the live image's canvas. Reuse
   immutable dataset storage and bounded prefix caching; invalidate all dependent
   caches when effective earlier steps, root Data or limits change. Release pure
   export buffers when only a preview snapshot is needed. Preserve job/reservation
   ownership through nested cancellation and disposal.
3. Add processing parameter editing with the existing typed registry validation,
   plus step reorder/disable through the program model. Revalidate the entire new
   order and require confirmation after dependency changes. Geometry-changing
   steps still require explicit remap/branch and annotation policy; do not silently
   admit them as in-place processing. Keep external publication absent from review.
4. Reuse portable typed choices for common and differing per-input Batch review.
   Validate every selected document and output plan before staging. Preserve native
   sessions, original inputs/tables, alias/no-overwrite handling and truthful per-file
   partial status. Keep scientific raster IO/axis policy separate from session Batch.
5. Qualify round-trip programs, exact targets, nested payloads, malformed originals,
   canceled/stale previews, cache invalidation, reordered repeated import/export
   steps, disabled outputs, memory admission, partial publication, both hosts and
   installed consumers. Continue typography/fonts, canvas/session launch, IO stress,
   matching releases and historical audits without declaring whole-family completion.

All99 families remain42inprogress57open0complete; Source901 remains the last full
source-release proof. The broad parity goal remains active.


## Portable native program checkpoint and next packet

The portable intent gap is implemented through `NativeWorkflowProgram`, its C++
record/JSON API and Python class, and shared Run Prepare/Keep plus Workflow
Save/Load. Explicit targets, native V4 payloads, export options/member compression,
mask projection canvas and all original mixed/legacy captures survive. The same
steps feed session Batch. Strict closed schema, duplicate/UTF-8/depth/node checks,
pre-materialization escaping admission and incremental retained decoded payload
accounting bound the transport/model. Destination confirmation, output paths and
write consent remain a new review. See `docs/NATIVE_WORKFLOW_PROGRAM.md` and
`build/p06q-workflow-program001/workflow-program-handoff001.json` for scope/evidence.

Next implement the prepared image/annotation viewport as one bounded packet:

1. Reuse existing image and annotation renderers with private display state and the
   prepared detached snapshot. Preview must not install the private owner in the
   live document, change live display/undo, or stage outputs.
2. Bind preview and any bounded prefix cache to the complete immutable program,
   source/Data revisions, accepted settings, explicit targets and authored choices.
   Invalidate dependent cache entries on edits. Retain reservations while canceled
   workers retire; refuse stale ready results using existing epochs/context guards.
3. Qualify both hosts with exact XY/XZ/YZ image planes, geometric/text/image overlays,
   source immutability, zoom/navigation isolation, cancellation, staleness and bounds.
   Preserve the full Run Apply and native session Batch publication regressions.
4. Then add processing parameter forms and reorder/disable against the portable
   model. Reconfirm dependencies and explicitly handle disabled outputs. Reuse the
   same typed model for per-input Batch choices; do not create another codec/editor.
5. Keep remap/branch policies, scientific raster IO/axes, typography/fonts,
   canvas/session launch, IO stress, matching releases and historical audits open.

All99 families remain42inprogress57open0complete; Source901 remains the last full
source-release proof. This checkpoint closes one bounded gap, not the broad goal.


## Prepared viewport checkpoint and next parameter-authoring contract

The private prepared image/annotation viewport is implemented in both hosts.
Path-free preparation uses the native planner, encodes pure exports without staging,
and forbids Apply. A detached native owner and desktop facade share prepared pixels
and complete metadata; the existing orthogonal and annotation renderers provide
XY/XZ/YZ, scientific T/C navigation, slice/zoom/pan/fit and read-only geometry, text
and embedded image layers. Existing ready/source/program/settings/epoch guards bind
its lifetime. Re-preparation and changed targets replace caches, rather than reusing
stale planes. No prefix cache was added. Render extent, metadata/legacy visuals,
planar geometry and display annotation admission precede viewer sampling. Textures
retire while GL is alive. Full prepared results also expose the viewer when the
remaining logical render allowance admits it.

See `docs/NATIVE_WORKFLOW_VIEWPORT.md` and
`build/p06q-workflow-viewport001/workflow-viewport-handoff001.json` for evidence and
limits. Processing parameter authoring is next:

1. Reuse `OperationSpec`/`OperationParameter`, existing descriptor choices/defaults,
   native validation and current field widgets. Preserve exact bool/int64/uint64,
   real, text, shaped numeric and image parameter types; do not route through a
   lossy JSON-number or string-only representation.
2. Keep captured originals immutable and validate them before computation. Registry
   captures already allow current explicit parameter intent. Bound Gaussian/rank
   captures currently forbid parameter overrides: resolve a separately validated
   authored typed-filter intent and any necessary versioned codec extension before
   enabling those controls. Preserve units, calibration consent and exact targets;
   do not drop the original or silently bypass its validation to enable editing.
3. Changed parameters clear the prepared viewer/result and every dependent step's
   confirmation. Private prefix/snapshot preparation supplies any context-dependent
   defaults. Accepting a draft updates typed intent; discarding leaves prior intent.
   Persist edited intent through Save/Load and native session Batch, using one model.
4. Refuse geometry-changing operations until explicit remap/branch and annotation
   policy exists. Bound array/image editing and lifetime before copies; keep external
   publication absent from the editor. Qualify native, both hosts and installed
   consumers with non-default values, exact parameter types, malformed/canceled/stale
   edits, dependent confirmation and portable round trips before broader controls.
5. Next add reorder/disable with explicit disabled-output behavior, then common and
   differing per-input Batch choices. Scientific raster IO/axes, typography/fonts,
   canvas/session launch, IO stress, matching releases and historical audits remain.

All99 families remain42inprogress57open0complete; Source901 remains the last full
source-release proof. The broad parity goal remains active.


## Parameter-authoring checkpoint and next reorder/disable contract

The shared Run form now authors current registry parameters and separate raw
Gaussian/rank options on a private prefix. Descriptor-based scalar/choice/bool,
shaped numeric and image fields preserve native parameter types. Strict bounded
field/cumulative parsing, invalid Keep, discard/reopen, cancellation, stale
source/settings and downstream confirmation are covered. Full original captures
stay immutable and validated before kernels; Gaussian calibration consent remains.
Closed V2 adds nullable typed filter options to operation steps; ordinary intent
continues to emit V1. Save/Load, preview and native session Batch consume the same
model, including actual shared V2 Batch publication. The installed SDK consumer
round-trips and executes V2 typed intent too. See `docs/NATIVE_WORKFLOW_PARAMETERS.md`
and `build/p06q-workflow-parameters001/workflow-parameters-handoff001.json`.

Next implement reorder/disable as persistent native intent, not a display-only mask:

1. Define an explicit enabled state in the typed model and versioned portable codec,
   preserving every disabled step's current intent and complete original capture.
   Keep prior V1/V2 reads and normal-program behavior. Rebuild headers/core/bindings/
   consumers together for DTO changes. Do not encode disabled steps by deleting them.
2. Validate every retained step's closed schema, raw choices/options and original
   history, including disabled steps, before kernels. Disabled steps must not resolve
   destination compatibility, run kernels/imports, encode exports, stage paths or
   consume publication consent. Count all retained steps for model limits; expose
   truthful executed counts and original program indices in outputs/status.
3. Reorder whole typed entries and their immutable capture references. Preserve
   export tokens and payloads; do not rewrite historical identities or silently
   repair source indices/geometry after a move. A new incompatible order must fail
   normal native preparation before publication. Reset affected confirmations,
   ready viewer/prefix caches, browse routes and file-publication consent.
4. Add Up/Down and enabled controls in the shared review with no mutation while
   worker/child work is active. Keep and Save retain order/enabled state. Reopening,
   private prefixes, image preview, full Run and scientific session Batch must use
   the same active-step semantics. Disabled export rows have no file/path obligation
   and cannot reserve/publish an output; partial-publication status remains truthful.
5. Qualify native/API, both hosts and installed consumers with mixed repeated
   import/filter/export moves, disabled outputs, malformed disabled originals,
   incompatible new order, empty active programs, cancellation, stale caches,
   exact targets/types, Save/Load and Batch reuse. Then reuse this model for common
   and differing per-input Batch choices without another codec/editor.

Remap/branch and annotation policies, scientific raster IO/axes, typography/fonts,
canvas/session launch, IO stress, matching releases and historical audits remain.
All99 families remain42inprogress57open0complete; Source901 remains the last full
source-release proof. The broad parity goal remains active.


## Reorder/disable checkpoint and next per-input Batch contract

Persistent native order/enabled intent is implemented. Both shared reviews move
whole entries and retain disabled rows, original captures and stable export tokens.
Structure changes invalidate ready viewers/prefixes, routes, all confirmations,
paths and consent. Strict V3 requires enabled flags on all rows; ordinary all-enabled
intent retains V1/V2. Disabled steps undergo bounded owner-free retained-intent
validation, including original captures and stored projection wire, but do not bind
to the destination or produce a resolved workflow export output. Executed counts
exclude disabled steps; output indices include retained positions. Private-prefix
diagnostics distinguish retained/executed counts. All-disabled Apply is a guarded
no-op, while Batch requires session-output consent and emits semantic session copies
only. See `docs/NATIVE_WORKFLOW_ORDER.md` and
`build/p06q-workflow-order001/workflow-order-handoff001.json`.

Next implement differing per-input Batch choices without another processing model:

1. Extend native Batch input intent with an explicit optional immutable program
   override, distinguishing inherited common intent from an input-specific program.
   Reuse V1/V2/V3 and all original/typed parameter/enabled/order semantics. Bound the
   aggregate retained override requests before session loading/copying. Keep existing
   callers and common-program behavior; qualify installed consumers after DTO changes.
2. Define explicit per-input document selection inheritance/override (all documents
   or an index), rather than silently taking a review document as execution selection.
   Every selected document must support its effective program. Keep embedded input
   workflow tables unexecuted and preserve unselected documents and session fields.
3. Add shared input-row review using a detached selected input session snapshot.
   Reuse Run's existing parameter/import/export and order/enabled controls on that
   private input; return owner-free typed intent to its parent row. Bind drafts to
   the input content/selection, parent program/settings and cancellation epoch.
   Refuse changed/stale input before preparation/publication, and retire retained
   reservations with workers. Review must not modify input files or the live anchor.
4. Show inherited versus overridden choices and provide explicit restore-inheritance.
   Default common changes invalidate dependent inherited reviews; they must not erase
   explicit overrides. Reset destination/confirmation/consent on meaningful changes.
   Input-specific paths/stems remain external publication choices, outside programs.
5. Prepare every effective input/document program and all output paths/encodings
   before staging. Include varied output tokens and enabled sets in aggregate/path
   admission, canonical/case/hardlink aliases and truthful per-file partial status.
   No retries or silent fallback to common intent after an override fails.
6. Qualify native/API, both hosts and installed consumers with inherited/overridden
   mappings, targets, arrays/images, typed filters, reordered/disabled exports,
   differing document selections, restore-inheritance, stale/canceled input review,
   malformed late originals, memory limits, alias/race/partial publication and
   source/table/sample-bit preservation. Keep scientific raster IO/axis policies
   separate from native session Batch.

Remap/branch and annotation policy, scientific raster IO/axes, typography/fonts,
canvas/session launch, IO stress, matching releases and historical audits remain.
All99 families remain42inprogress57open0complete; Source901 remains the last full
source-release proof. The broad parity goal remains active.


## Per-input Batch checkpoint and next execution packet

Per-input program/document-selection authoring is implemented through the same
immutable V1/V2/V3 model and shared draft editor. Content fingerprints bind the
review to decoded input bytes; guarded Keep, preparation and publication refuse
changes. Restore-inheritance and off-thread review cancellation are explicit.
Aggregate distinct override intent is admitted before input loading, and every
selected document must support its effective program. Existing mixed-program,
metadata retention, exclusive staging and partial-publication contracts still apply.
See `build/p06q-workflow-input001/workflow-input-handoff001.json` and
[NATIVE_WORKFLOW_INPUT](../NATIVE_WORKFLOW_INPUT.md) for scope and limitations.

Next close native Batch setup persistence without another processing model:

1. Define one versioned, closed native setup DTO/codec containing common portable
   program, explicit inherited/overridden row intent, document selectors and output
   stems. Keep input/output locations as external setup choices with explicit
   validation; never serialize write consent or a prepared result. Reuse program
   codecs rather than copying grammar or silently executing captured records.
2. Bound bytes, depth, nodes, input/step counts and aggregate expanded payloads
   before materializing programs or opening input files. Reject duplicate/unknown
   keys, invalid types, inconsistent selectors and unsupported versions. Preserve
   exact original captures, integers, arrays/images and disabled rows.
3. Add shared Save/Load controls with existing browse and worker infrastructure.
   Build/validate a whole candidate before replacing the active setup. Cancel/stale
   load leaves the current draft intact; load creates no input/output directories,
   runs no workflows and gives no publication consent. Require fresh review for
   loaded program overrides instead of trusting persisted fingerprints.
4. Keep edits reversible and inherited versus overridden intent visible after
   roundtrip. Meaningful changes invalidate private work, confirmations and consent.
   Preserve application settings, active documents and embedded session tables.
5. Qualify native/API codecs, both ImGui hosts and installed C++ consumers with
   mixed overrides, all/index selection, V1/V2/V3, arrays/images, malformed late
   rows, memory/cancellation/staleness, roundtrip and independent output equations.
   Hold sources during jobs and clean only verified owned passed-run disposables.

After setup persistence, reassess remap/branch and scientific raster IO/axis policy
against the existing family inventory. Full family/release completion remains open;
All99 families remain42inprogress57open0complete and Source901 remains the last full
source-release proof. Do not broaden the commercially eligible dependency set.


## Native Batch setup checkpoint and next execution packet

The closed native setup model and both shared Save/Load controls are implemented.
Whole-candidate worker admission, immutable V1/V2/V3 programs, inherited/explicit
row selectors, external paths/stems and explicit browser Replace are covered.
Fresh review/consent after Load and cancellation/stale-source publication guards
preserve existing input, live-document and embedded-table contracts. Read
`build/p06q-batch-setup001/batch-setup-handoff001.json` and
[NATIVE_WORKFLOW_BATCH_SETUP](../NATIVE_WORKFLOW_BATCH_SETUP.md) for evidence/bounds.

Next address the ready scientific IO gap using the existing admitted profile,
without opening a new format candidate:

1. Consume `docs/parity/reviews/scientific-tiff-u16-entry-001.json` and
   `scientific-tiff-u16-dependency-decision-002.json`; inspect their current-state
   assumptions and refresh source/build/dependency identities. Preserve the exact
   single-series UInt16, uncompressed-strip TIFF profile and declared CZT/calibration
   policy. An entry candidate is not an implemented or redistribution-qualified exit.
2. Implement the headless Dataset/series/metadata service behind optional
   `imagejxx::io`, independently of Dear ImGui and Python. Keep `imagejxx::core`
   dependency scope unchanged. Conditional installed package discovery must apply
   only to IO consumers, with exact codec/runtime notice closure verified.
3. Admit offsets/counts, dimensions, products, memory/IO limits and metadata before
   allocation. Refuse unsupported variants and ambiguous axes according to the
   existing frozen contract. Use bounded reads and exclusive publication with
   cancellation/staleness; preserve UInt16 samples and calibrated axis descriptors.
4. Make Python and both desktop launches consume the native service; avoid another
   separate reader. Present explicit series/axis choices and loss/refusal policies.
   Native-session Batch remains .ijx-only until a separately versioned raster-input
   descriptor and axis policy are implemented and qualified.
5. Qualify independent bounded fixtures, malformed/overflow inputs, calibration and
   axis ordering, installed SDK-only read/write, both UI launches and actual ImageJ
   interoperability for the frozen profile. No OME/general TIFF/vendor-format or
   family completion follows from one profile. Preserve every earlier scoped proof.
6. Keep source snapshots stable during jobs; verify archive retention before
   removing exact owned scratch. Preserve failures, final products, SDK and cache.

Remap/branch, broader microscopy formats, typography/fonts, canvas/session launch,
IO stress, matching releases and historical audits remain required. All99 families
remain42inprogress57open0complete; Source901 remains the last full source-release
proof. The broad implementation goal stays active.


## Scientific TIFF dependency prerequisite handoff

The selected source/port/artifact/notice closure is reviewed and checked by the
build and installer. Explicit core-only SDK discovery, required absent-IO refusal,
headless codec linkage, current installed desktop/runtime deliveries and negative
artifact mutation checks pass. Read
`build/p06r-tiff-dependencies001/tiff-dependency-handoff001.json` and
[SCIENTIFIC_TIFF_DEPENDENCIES](../SCIENTIFIC_TIFF_DEPENDENCIES.md).

The next implementation action is the actual optional `imagejxx::io` target and
`include/imagejxx/io.hpp` service from the frozen entry, not another dependency
candidate. Add its separate export and conditional dependency discovery only when
IO is requested. Implement the complete admitted UInt16 profile: raw metadata
preflight, C-fast file pages mapped into TCZYXS, calibration/default reporting,
explicit rational estimates, bounded read/streaming atomic write and control/error
contracts. Then make Python and both desktop forms consumers, retaining legacy
ndarray compatibility and refusing unsupported variants. Actual ImageJ reopening,
independent malformed/precision/resource fixtures and installed SDK read/write
remain unexecuted requirements. Native .ijx Batch is unchanged. All99 families and
the full implementation/release objective remain open.

## Scientific TIFF native-service checkpoint

The optional headless `imagejxx::io` service and installed SDK now implement the
admitted UInt16 profile. Read [the service contract](../SCIENTIFIC_TIFF_SERVICE.md)
and `build/p06r-tiff-service001/tiff-service-handoff001.json`. Raw admission,
bounded decoding, explicit rational write estimates and exclusive streaming atomic
publication are implemented. Twenty-three affected native invocations pass;
independent binary-output checks and actual bidirectional ImageJ 1.54r interchange
pass. Five valid and 34 malformed carriers exercise the parser. Native/session/
preferences/ROI/workflow regressions include the shared atomic-file change.

Runtime revealed that ordinary FileSaver adds an escaped micrometer unit and
loop/min/max description fields. Review 004 records the exact narrow grammar
amendment and preserved failed evidence. The untouched reference export now reads
through the installed SDK. Source description retains display provenance; no
Document display/playback restoration is claimed.

Next implement typed Python Dataset bindings backed by this service, then shared
scientific open/export forms and installed checks for both UI launch variants.
Keep legacy ndarray/flat-stack codecs and .ijx Batch policy unchanged. Complete
remaining literal TU01–TU09 cases, resource/IO fault coverage and matching release
checks before profile acceptance. All99 families remain42inprogress57open0complete;
Source901 remains the last full source-release proof. No live owned job remains at
this checkpoint; failed attempts, installed SDKs and active caches are retained.

## Scientific TIFF Python consumer checkpoint

The optional typed `imagejxx.scientific_io` module now calls the shared C++ service.
Read `build/p06r-tiff-python001/tiff-python-handoff001.json` and the updated service
documentation. Development and installed package checks each pass123, including
39 carriers, independent binary output, bounded conversion, GIL/callbacks,
cross-module Dataset/cancellation, Dataset/execution/jobs and legacy codecs.
Actual ImageJ1.54r opens Python-written TIFFs and its untouched exports read through
the installed Python API. Runtime module handles identify the four reviewed codec
DLLs inside that package. A separate processing-only component has no such DLLs or
scientific/graphics extension dependency.

Next implement the shared scientific open/export forms in the C++ Window and its
Python host, including explicit dataset/loss policy, lifecycle and publication,
preferences/recording and installed physical UI checks. Do not silently redirect
legacy codecs or .ijx Batch input routing. Remaining literal TU01–TU09 cases,
fault/resource coverage and matching wheel/source/release delivery stay open.
