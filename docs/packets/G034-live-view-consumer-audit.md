# Effective calibration: live consumer integration

This is a source audit and execution specification. [PLAN](../PLAN.md) owns
verified status and counts. Global/bypass controls remain gated until command
and consumer coverage agree. Local metadata and old record formats remain local.

| Consumer / seam | Current finding | Easiest implementation and acceptance path |
| --- | --- | --- |
| `Document::displaySpacing`, ordinary `Window::Impl::canvas` | Display-specific capture implementation is scoped verified in both hosts. `spatialCalibration()` remains the local accessor. | One current whole-plane Application target per facade; reuse `validate_target`. Recompute aspect from captured spacing/direction norms. Validate rotated geometry and local metadata/history invariance in both hosts. |
| `OrthogonalViews::draw` | Spacing and world captions use the view-specific input; Set/Clear/bypass, Z navigation and pixel-cache reuse pass in both hosts. | Change geometry without replacing pixel cache keys. Check XY/XZ/YZ spacing and scale, Set/Clear/bypass, source metadata changes, and no resampling on calibration-only changes. |
| World-coordinate diagnostics and ordinary hover | Geometry uses the view-specific spatial value; independent affine arithmetic passes in both hosts. | Use the same affine convention as `SpatialCalibration::voxel_to_world`. Preserve voxel indices. Check independent affine arithmetic, not another call to the same resolver. |
| `Document::fitSelection`, zoom/fit call sites | They use `displaySpacing`, so share its behavior. | Verify physical fit/zoom after effective changes; do not change stored pixel-coordinate selection geometry. Existing zoom/navigation controls remain acceptance gates. |
| Document-info metadata and time fields | Existing local `calibration`/`time` contract remains. Additive `display_calibration` reports the view input; it is not a persistence source. | Keep local and effective claims explicit. A later time-UI packet must reconcile toolbar, playback and display readouts without repurposing local metadata. |
| Window calibrated playback and navigation toolbar | Playback reads a captured local dataset calibration; toolbar reads `spatialCalibration`. | Capture effective time values when starting playback, validate context before advancement, stop/reopen on relevant changes. Test own bypass, ignored independence, time origin/interval/unit, and unchanged local display indices. |
| Selection enlargement UI/action | Window paths read local spatial calibration for unit labels and distance conversion. | One captured effective draft drives labels and conversion; reject stale Apply. Keep old strict record semantics and explicitly author any contextual rebind. Cover anisotropic scales, raw-pixel mode and no source/history mutation on stale rejection. |
| `Document::measure`, Window measure / ROI measure actions | Legacy implementation reads local JSON scales and raw statistics. | Audit geometry and density separately. Share a typed captured analysis input with measurement preparation; preserve local legacy API behavior where promised. Check area/length/angle, density adaptation, selection membership, source revisions, result units and independent references before claiming ImageJ measurement parity. |
| Window line-profile action and live plot ownership | Legacy path uses `line_profile` over its selected source; all live/captured policies still need audit. | Identify one preparation seam and existing live invalidation key. Live plots refresh on relevant context changes; independent saved plots retain their captured values. Test distance axis, density values, source/selection/plane changes and resource release. |
| `HistogramPanel`, Window histogram jobs/replay | Panel and record resolvers accept source snapshots; contextual admission and retention need integration. | Reuse existing histogram jobs, target controls and cancellation. Freeze effective values with the submitted target and validate before result publication. Preserve strict legacy record semantics; do not silently reinterpret old histograms. Cover global density, source-type adaptation, bypass, stale queued/ready results and saved independent plots. |
| `ThresholdPanel`, Window threshold jobs/replay | Panel target configuration resolves source-only snapshots. | Audit raw versus calibrated threshold units first. Capture effective input once and retain it through preview/Apply. Reuse existing transient preview ownership and publication guards. Test raw/scaled boundaries, source/ROI/plane changes, stale rejection and local metadata preservation. |
| Rank / other generic command dispatch | Gaussian and six axis/projection routes have contextual integration; this does not prove every generic route does. | Inventory dispatch by operation family and its actual spatial/density dependencies. Add only the required envelope alternatives after that inventory, with complete targets and explicit legacy/rebind policy. Batch necessary public variant changes and rebuild installed clients together. |

The view cache retains a single current source capture, sharing its immutable
data with the facade. It releases that capture when replaced, rebound or closed.
Whole-plane guards avoid selection-driven calibration invalidation. A bypassed
capture ignores unrelated global changes; own bypass and source pixels/metadata
still invalidate. Validation does not serialize records or decode density per
frame. Local/bypassed raw display must continue to tolerate opaque future density
records through the existing guard-only target path.

Deliver the display/XYZ slice first. Then integrate analysis preparations and
their live cache owners, followed by time/distance controls and the remaining
command audit. Do not enable Global/bypass controls based only on view tests.
Use the single active compiler cache/runtime, bounded per-case captures and
lossless hash-verified scratch retirement. UI-only checks do not promote Linux,
macOS, original-reference variants, full families or speed/memory superiority.
