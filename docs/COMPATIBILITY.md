# Numeric and array conventions

- The registered plane editor operations share the C++ parameter schema and
  preserve existing kernel defaults separately from GUI sample defaults.
  `validate_operation_record` validates explicit argument structure without
  resolving source-dependent defaults or promising that a target is supported.
  Registered operations reject nonfinite outputs before commit, preserving the
  native desktop rule. Raw public kernels retain their documented output policy;
  in particular, an all-foreground raw distance transform can contain infinity.
- Registered label results expose logical `int32` arrays and optional component
  counts in Python; the legacy desktop stores those numeric IDs in Float64 working
  images. Its ROI count is the number of source-plane components intersecting the
  ROI, with connectivity computed before masked application. Per-step workflow
  output collection remains pending.
- Native color fill/strokes honor explicit RGBA alpha. Legacy Python color helpers
  and their `ui.commands.Operation.execute` adapter preserve opaque alpha, including
  old eight-digit color arguments. Grayscale drawing retains byte-rounded
  luminance followed by scaling to the target type's color range.
- Images: `(H,W)` grayscale or `(H,W,3/4)` RGB/RGBA. Stacks: explicit `(Z,H,W)`
  or `(Z,H,W,3/4)` APIs. Other layouts require caller conversion.
- Input dtypes: uint8, uint16, float32 and float64. Binary APIs additionally
  accept Boolean masks. Label arrays accept signed/unsigned integers with
  nonnegative IDs no larger than int32's maximum. Negative label IDs are rejected.
- Strides and non-native byte order are supported through input conversion.
  Inputs are copied into owned native working storage. NaN/Inf inputs are rejected.
- Functions create new arrays. Native kernels release the GIL and share no
  mutable global processing state. Working data and intermediate arithmetic
  use doubles. Memory use is higher than the source pixel dtype.
- Integer export clips to `[0,255]` / `[0,65535]` and rounds half upward.
  Scalar math and spatial editing usually preserve dtype. Derivatives, distance,
  statistics, HSV, concentration maps and aggregate projections use float64.
  Sum binning uses float64; mean binning keeps input dtype.
- Floating color operations assume `[0,1]`. Floating inversion uses `1-pixel`;
  raw pixel math can operate in arbitrary finite intensity units. `convert` with
  `scale=True` maps dtype ranges, and with `scale=False` preserves raw intensities.
- Most spatial filters process all channels independently, including alpha.
  Bilateral and Kuwahara filters use a shared color neighborhood decision.
  Grayscale/HSV/stain conversion ignore alpha and produce one/three channels.
- Borders: `reflect` repeats the edge (half-sample symmetric), `nearest` clamps,
  `wrap` repeats periodically, and `constant` uses the supplied `cval` (or zero
  when the function has no `cval` parameter). Rank footprints are integer-radius
  disks or odd squares. Radius zero is an identity neighborhood.
- Gaussian support radius is `ceil(4*sigma)`; sigma zero is an identity pass.
  This differs from implementations that round the support radius. Box and
  local threshold statistics use square neighborhoods.
- Resize aligns pixel centers; bicubic uses Catmull-Rom weights. Downsampling
  does not automatically antialias. Positive rotation is counterclockwise.
  Affine matrices inverse-map output `(x,y)` coordinates to source coordinates.
- Crop bounds must lie inside the image. Paste and canvas operations clip at
  the destination boundary. Geometric sizes and coordinates use explicit x/y;
  sigma, spacing and shape tuples follow NumPy's y/x (or z/y/x) order.
- Manual thresholds include both range endpoints. Automatic masks use strict
  `>` for bright foreground and `<=` for dark foreground. Automatic thresholds
  use 256 bins by default, including uint16; supply a larger `bins` explicitly
  for finer quantization. Histograms use dtype-wide integer ranges with edges
  `-0.5` to `maximum+0.5`, and observed ranges for floating images. Empty masks
  make pixel statistics raise ValueError; constant contrast images stay constant
  under equalization/CLAHE and normalize to the requested lower endpoint.
- Twelve global and eight local threshold methods are implemented with the
  formulas and options documented in the Python API. They are independent
  implementations; exact ImageJ AutoThresholder values are not promised.
  Intermodes/minimum raise if smoothing cannot find two modes. Minimum-error
  thresholding falls back to Otsu when all candidate class variances are zero.
- CLAHE uses 256-bin nonoverlapping tiles and interpolates neighboring tile-center
  LUTs. It is distinct from Fiji's sliding-window variants. Morphological
  background subtraction uses a flat structuring element. `rolling_ball_background`
  separately implements direct spherical non-flat morphology, without ImageJ's
  shrinking or sliding-paraboloid approximation. Its runtime is proportional to
  pixel count times radius squared; ball heights use raw intensity units.
- Any nonzero mask pixel is foreground. Binary outputs use uint8 0/255;
  Boolean grayscale morphology preserves 0/1 through uint8 conversion.
  Fill-holes connectivity describes background connectivity; label connectivity
  describes foreground connectivity. Skeletonization uses a zero exterior.
- Distance is to zero pixels inside the input. A completely foreground image
  therefore has infinite distances. To include an external background, pad the
  mask with a zero canvas before computing distance, then crop.
- Watershed is marker-controlled minimax path flooding with deterministic queue
  order for ties. It preserves seed IDs, returns zero for masked-out/unreachable
  pixels, and does not insert separator lines. Local maxima include pixels equal
  to the neighborhood maximum only when the neighborhood is nonconstant.
  `binary_watershed` generates regional distance-maxima markers with an external
  zero background, rejects flat ridges connected to higher values, and supplies
  a fallback seed in components whose maxima were suppressed. It returns region
  IDs, so converting the result to Boolean loses
  the boundaries between adjacent labels.
- Region bounding boxes are `(x,y,width,height)`. Centroids are `(y,x)` using
  zero-based pixel-center indices times spacing. Region area is occupied pixel
  count times pixel area. Perimeter sums exposed pixel-cell edges; circularity
  uses that perimeter, which differs from ImageJ's traced/subpixel perimeter.
  Integrated density sums intensities times pixel area. Pixel statistics use
  population standard deviation and an uncalibrated pixel-value sum.
- Stain unmixing uses three user-supplied nonnegative absorbance vectors as
  matrix rows, normalizes them, and solves the transposed system in optical
  density. It uses a white-level/255 offset before the logarithm and rejects
  singular matrices. Concentrations are not converted to stain display images.
- FFT zero-pads on the bottom/right to power-of-two dimensions. `pad=False`
  requires those dimensions already. Inverse FFT returns its real component;
  crop to the original shape after an FFT round trip. Frequency cutoffs use
  cycles/pixel, not ImageJ's structure-size parameters. Bandpass crops back to
  the input size after zero-padded filtering.
- Seeded noise is reproducible within one C++ standard library, not necessarily
  across standard-library implementations. I/O does not retain ImageJ-specific
  calibration, ROI, hyperstack or display-LUT metadata.

## Native document records

ROI rotation uses positive clockwise angles in image XY coordinates. This is
distinct from the existing image `rotate` function's counterclockwise convention.
Native `rotated_rectangle`/`rotated_ellipse` records contain a center and two
perpendicular half-axis endpoints. Their masks use polygon coverage or analytic
pixel-center ellipse coverage, respectively. Bitmap rotation inverse-samples
cell centers with nearest sampling. Legacy rectangle/ellipse defaults remain
unchanged; numeric zero-angle box specification returns the legacy kind. These
new geometry rules have native fixtures but lack pinned ImageJ equivalence
certification. See [rotation and properties](IMPLEMENTATION.md#native-roi-rotation-and-selection-properties).

New ROI kinds and attributes round-trip through current native/Python JSON and
v2 session readers. Earlier readers need support for the new kind strings.
The Python model distinguishes unambiguous `mask` metadata from storage while
retaining its old string-bitmap/two-dimension encoding, including records tagged
with another geometry kind. Harmonizing that legacy encoding with native session
migration remains required. Positive width and hex color controls cover an
initial style subset; zero-width, fill/font/style and binary ImageJ interchange
contracts remain part of M4/M8.

Native snapshots and transactions retain recursive metadata, exact int64/uint64
values, selection/ROI attributes, overlays, previous geometry and extra session
and calibration fields. Python getters return detached dictionaries/lists.
Existing C++ scalar/string-list metadata alternatives remain available. Values
must be finite, object keys must be strings, and containers must be acyclic with
at most 128 levels of nesting.

`set_rois` replaces complete records, including their attributes. Reuse attributes
from the source ROI when reconstructing it; an explicitly empty record clears its
styles/extensions. The desktop adapter no longer guesses extension ownership
from names or matching geometry. Canonical geometry/calibration/display fields
use their dedicated APIs, and conflicting desktop extension fields are rejected.

`FourierState` shares immutable complex storage and original Y/X dimensions across
snapshots/history. Its NumPy exports are detached complex128 arrays. Pixel edits
invalidate it unless a replacement is supplied later in the same transaction.
Metadata edits retain it, undo restores it, and legacy v2 sessions preserve their
existing Fourier encoding. Retaining these records does not complete frequency
editing, scientific-format metadata interpretation or v3 sessions.

## Binary morphology defaults

The distinct `binary_erode/dilate/open/close` commands operate on single-sample
uint8 0/255 images. Their defaults select foreground 255, one iteration, one
neighbor and no erosion-edge padding. `black_background=False` explicitly
selects foreground 0. Display LUT inversion does not change these pixel rules.
Existing grayscale `erode/dilate/opening/closing/morphology`, recorded legacy
commands and ROI grow/shrink retain their original behavior.

New binary preference keys are additive in the existing version-1 settings
schema; earlier files load their defaults. Desktop commands consume preferences
and freeze effective parameters in records. C++ and Python array APIs use their
explicit options; `Settings.binary_options()` supplies a detached copy.

All 1,184 captured direct byte-processor fixtures match the pinned ImageJ 1.54r
source. This evidence excludes Binary plugin LUT-dependent defaults, ROI bounds
and masking, threshold state, plugin dialogs, EDM output conventions and the
Fiji archive's separate ImageJ 1.54p baseline. Native resolved targets compute
from each full XY plane and restore pixels outside the captured ROI. Complete
plugin options and UI/reference behavior remain required before certification.

## Configurable grayscale conversion and uint16 inversion

`convert_image` is a distinct ImageJ-style operation; legacy `convert` and
`grayscale` retain their defaults. Input is grayscale U8/U16/F32/F64 or U8
RGB/RGBA. RGB output is grayscale, alpha is ignored, and equal weights are the
default. Weighted RGB uses configurable coefficients; RGB-to-U16 passes through
byte grayscale, including the pinned converter's byte cast. Float64/RGBA support
is a native extension rather than upstream processor certification.

Scaled U16-to-U8 conversion uses half-up-rounded processor display limits and
the inclusive `256/(max-min+1)` scale. Float-to-integer conversion uses a
continuous range. Float32 limits and input values are normalized to Float32;
explicit/display `(0,0)` resets to observed limits. Native Float64 scaling also
handles very wide and subnormal finite ranges without overflowing intermediate
scales. Invalid/nonfinite inputs or unrepresentable Float32 output reject.

Array options default to `range_policy="data"`; desktop preferences default to
display limits. A dialog captures display limits into explicit recorded values,
otherwise uses the configured U16 range or observed pixels. Array calls read no
global desktop settings. `Settings.conversion_options()` returns a detached
value; explicit command arguments override preferences. Default uint16 inversion
uses observed extrema, ignoring display limits; full-range inversion uses
`2**bits-1`, with auto meaning 16 bits.

Type conversion resolves every C/Z/T position and whole planes even with an
active selection. It preserves selection/annotation records and records the full
effective parameters. A native conversion that changes document data creates
one pixel history step; unchanged data retains the native no-op behavior.
Incomplete or ROI-limited
native conversion targets reject before reads/work. Inversion keeps the existing
ROI/plane executor semantics. Cancellation and failure cannot publish partial
conversion.

All 462 pinned direct ImageJ 1.54r processor fixtures match exactly. This excludes
density/intensity-calibrated and signed conversion, indexed quantization,
RGB/HSB stack conversion, LUT/threshold/plugin/GUI behavior, the Fiji archive's
1.54p baseline, and full cross-platform/performance certification. Those remain
required gaps.
