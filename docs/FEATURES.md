# Implemented feature coverage

This is a native algorithm library for frequently used ImageJ operations and
selected Fiji workflows. Rows below describe implemented functionality; no
percentage of the complete Fiji plugin ecosystem is claimed.

| Feature family | Python entry points | Native implementation |
| --- | --- | --- |
| Pixel math | `math`, `invert`, `add`, `subtract`, `multiply`, `divide`, `absolute`, `square`, `sqrt`, `log`, `log1p`, `exp`, `gamma`, `clip` | Scalar transforms and bitwise math |
| Image calculator | `calculate` | 12 pairwise arithmetic/logic operations |
| Type and intensity | `convert`, `normalize`, `adjust_brightness_contrast`, `apply_lut`, `equalize_histogram` | Saturating export, scaling, percentile stretch, linear adjustment, LUT and CDF |
| Conversion defaults | `ConversionOptions`, `convert_image`, `invert_uint16` | Configurable RGB weights, scaling/display ranges, uint16 inversion/ranges, consumed shared-panel preferences and whole-dataset type conversion; full calibrated/indexed/stack conversion remains required |
| Noise | `add_noise` | Gaussian, uniform and salt/pepper noise |
| Standard filters | `smooth`, `sharpen`, `convolve`, `gaussian_blur`, `rank_filter`, `median_filter`, `minimum_filter`, `maximum_filter`, `variance_filter`, `unsharp_mask`, `remove_outliers` | True convolution, separable Gaussian, prefix-sum box filtering, rank windows and median outlier replacement |
| Edges and scales | `find_edges`, `laplacian`, `difference_of_gaussians`, `local_maxima` | Sobel magnitude, Laplacian, Gaussian differences and local extrema |
| Fiji-style denoising | `bilateral_filter`, `kuwahara_filter`, `anisotropic_diffusion` | Joint-channel bilateral, minimum-variance quadrant and Perona-Malik diffusion |
| Geometry | `crop`, `paste`, `flip`, `rotate90`, `rotate`, `resize`, `translate`, `affine_transform`, `binning`, `resize_canvas` | Copy/paste, orthogonal and arbitrary rotation, nearest/linear/cubic resampling, affine warp and bin aggregation |
| ROI editing | `fill`, `draw_line`, `rectangle_mask`, `ellipse_mask`, `polygon_mask`, `wand_mask`, `Editor` | Masked fill/paste, ROI rasterization and configurable wand connectivity/tolerance; desktop undo/redo shares native unchanged planes |
| Binary morphology | `BinaryMorphologyOptions`, `binary_erode`, `binary_dilate`, `binary_open`, `binary_close` | Explicit 0/255 foreground, iteration/neighbor count, erosion padding, shared defaults and ImGui controls; complete Binary plugin/output parity remains required |
| Settings | `Settings`, `Editor.update_settings` | Validated C++ schema, consumed defaults, shared searchable ImGui preferences and versioned file migration; remaining option groups are tracked in the gap plan |
| Color | `grayscale`, `rgb_to_hsv`, `hsv_to_rgb`, `split_channels`, `merge_channels`, `in_range`, `color_deconvolution` | Luminance, normalized HSV, channels, channel-range masks and optical-density stain unmixing |
| Global threshold | `threshold`, `auto_threshold`, `auto_mask`, `THRESHOLD_METHODS` | Otsu, mean, ISODATA, Li, triangle, Yen, maximum entropy, percentile, intermodes, minimum, moments and minimum error |
| Fiji-style local threshold | `local_threshold`, `LOCAL_THRESHOLD_METHODS` | Mean, median, mid-grey, Niblack, Sauvola, Bernsen, Phansalkar and contrast |
| Fiji-style local contrast | `clahe` | 256-bin tiled contrast-limited equalization with bilinear LUT blending |
| Grayscale morphology | `morphology`, `erode`, `dilate`, `opening`, `closing`, `top_hat`, `black_hat`, `subtract_background` | Disk/square morphology, gradient and opening/closing background estimation |
| Spherical background | `rolling_ball_background`, `subtract_rolling_ball` | Direct non-flat spherical opening/closing |
| Binary processing | `fill_holes`, `binary_outline`, `skeletonize`, `remove_small_objects`, `clear_border` | Flood fill, inner outline, Zhang-Suen thinning and component filtering |
| Particle segmentation | `label`, `watershed`, `binary_watershed` | 4/8-connected labels, marker-controlled minimum-barrier watershed and automatic distance-maxima seeds |
| Euclidean distance | `distance_transform` | Exact separable squared-distance envelopes with anisotropic spacing |
| Measurements | `statistics`, `histogram`, `line_profile`, `region_properties`, `analyze_particles` | Pixel statistics, calibrated region area/perimeter, centroid, eccentricity, circularity and intensity measurements |
| Stacks | `project`, `gaussian_blur3d`, `map_stack` | Six Z projections, separable XYZ Gaussian and native slice-wise processing |
| Frequency processing | `fft2`, `ifft2`, `power_spectrum`, `fft_bandpass` | Radix-2 complex FFT, real inverse, spectrum display and Gaussian bandpass |
| File I/O | `imread`, `imwrite` | Native 8/16-bit P2/P3/P5/P6 PNM; optional TIFF/Pillow codecs; NumPy NPY |

Fiji references describe the functionality, not code incorporated into this
repository: [CLAHE](https://imagej.net/plugins/clahe),
[Auto Local Threshold](https://imagej.net/plugins/auto-local-threshold), and the
[component license inventory](https://github.com/fiji/fiji/blob/main/LICENSES).
The core processing reference is the
[ImageJ Process menu](https://imagej.net/ij/docs/menus/process.html).

## Compatibility boundaries

These implementations expose explicit array semantics. They do not reproduce
every plugin option or ImageJ's Java state model. In particular, CLAHE uses tiled
LUTs, spherical background estimation runs directly without image shrinking,
watershed omits separator lines, and thinning uses Zhang-Suen. Read `COMPATIBILITY.md` before comparing results to
an existing ImageJ/Fiji macro.

Unimplemented areas include sliding-paraboloid/image-shrinking background approximations, full
AutoThresholder parity, watershed boundary-line insertion,
3D connected components, registration/stitching/tracking, deconvolution with PSFs, full
Bio-Formats and plugin/macro compatibility. These are required migration work;
commercial-license review applies to any third-party code, dependencies and assets
used to implement them. Non-commercial, research-only and unlicensed components
remain excluded. The [editor completeness audit](DESKTOP.md#editor-completeness-audit-2026-10-09)
also tracks configuration, selection editing, slicing, linked XY/XZ/YZ views,
C/Z/T hyperstacks and 3D viewing with implementation order and acceptance criteria.
The [gap closure plan](PLAN.md) assigns every audited area to implementation
milestones and verification gates.
Subsequent deliveries are described in [implementation progress](IMPLEMENTATION.md),
including explicit-axis datasets, calibrated orthogonal views and live document handles.

Both optional desktop interfaces now include calibration containers, ROI
managers, overlays/text, editing history, stacks, measurements and workflow
recording. See [desktop coverage](DESKTOP.md) for implemented UI workflows and
the remaining ImageJ/Fiji parity work.
