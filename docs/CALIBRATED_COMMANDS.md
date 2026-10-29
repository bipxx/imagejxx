# Calibrated command capture and replay

Status: seven native/Python command families and six shared ImGui routes are
scoped verified. Installed verification passes 2,244 of 2,246 cases with two
optional hidden-OpenGL skips, including 119 original envelope and 52 new Gaussian
cases. Both rebuilt core SDKs pass 4/4 installed clients. Live Gaussian UI remains
pending. [Current evidence](parity/reviews/G034-calibrated-gaussian-command-progress001.json).
This additive interface covers plane export, range reslice, orientation, physical
spacing, finite-scalar Slicer resize, ImageJ projection and Gaussian blur. Existing source-only
command classes and record formats retain their local snapshot semantics.

Capture on the caller thread before queueing work:

```python
capture = app.resolve_target(document_id=source_id)
request = imagejxx.AxisPlaneExportRequest("xz", 1)
command = imagejxx.resolve_calibrated_command(capture, "axis_plane_export", request)
record = command.record()
prepared = imagejxx.prepare_calibrated_command(command)
result = app.publish(prepared)
```

The other operation names are `axis_range_reslice`, `axis_range_orientation`,
`axis_range_spacing`, `axis_range_slicer_resize` and `project_imagej`. They take
their existing typed requests; Python projection takes the dictionary returned
by `imagej_projection_defaults()`. C++ uses the `CalibratedCommandRequest` variant
and `<imagejxx/calibrated_command.hpp>`. The resolved object is immutable; Python
request, limits, capture and record access return independent values.

## Record contract

The `imagejxx.calibrated-command` version 1 envelope separates the original local
source from effective processing metadata:

| Field | Meaning |
| --- | --- |
| `source` | Original document identity, pixel/metadata revisions, shape/type/sample policy and local spatial/time calibration. |
| `binding` | `application` or `detached`; this is a policy, never a live application identity. |
| `bypass` | Captured application ignore-global policy. |
| `effective` | Active global spatial/intensity values, or null for local/detached/bypassed processing. Density preserves exact Float32 payload words in its existing hexadecimal codec. |
| `descriptor` | Existing validated operation grammar describing effective input/output geometry. Its source calibration may intentionally differ from the original local source above. |
| `limits` | Explicit envelope record and command working-memory admission limits. |

Schema, version and operation close the grammar. No application pointer, global
generation or bypass generation is serialized. The immutable in-memory target
retains those publication guards. The nested descriptor is metadata provenance;
use the envelope API for replay rather than dropping its calibration binding.
No source pixels or callbacks are acquired during resolution or validation.

`validate_calibrated_command_record(record)` checks schema, policies, source/
effective consistency and all existing derived/grid claims. It establishes
self-consistency, not authenticity of a historical source or availability of its
live application. Strict replay requires an explicit current capture:

```python
replay = imagejxx.calibrated_command_from_record(
    app.resolve_target(document_id=source_id), record)
```

Replay matches the original source and effective values, binding and bypass
policy. Projection also retains its existing selection/display dependencies.
Application identities and generations come from the supplied current capture.
Resetting global calibration to the same values invalidates old prepared work;
a fresh capture can still replay an equal record. An explicit capture from a
different application with the same source and values deliberately creates that
application's new binding. A detached capture cannot silently replay an
application-bound record.

`rebind_calibrated_command(capture, record, operation, replacement_request)` first
validates the original claims, then resolves the complete explicit request against
the new capture. It can change source/context, but cannot change operation family
or repair malformed original derived claims. It never installs global state from
a record. Legacy snapshot record replay remains unchanged.

## Admission and publication

`CalibratedCommandLimits` defaults to a 16 MiB record bound and 512 MiB working
bound. The record limit is a conservative escaped-metadata bound, not the length
of a particular JSON serialization. Working admission includes the factory's
declared payload plus conservative original-source and envelope metadata copies;
it is not measured RSS. Individual request limits retain their existing scope.

`command.working_bytes` is the admitted bound. A smaller
`runtime_max_working_bytes` rejects preparation before callbacks or image reads;
the runtime cap may tighten request limits but never mutates the command/record.
Preparation consumes only the frozen capture. It does not fetch current global
state on a worker. `Application.publish` atomically validates the target's live
application/global/bypass/source guards before adopting the candidate. Resolution
or standalone record validation is not a substitute for that publication boundary.

Local raw axis commands do not decode unknown reserved intensity records when
global calibration is absent or bypassed. Projection still requires a supported
density record, as its existing numerical policy does. RGB Slicer resize remains
unsupported; scalar refusal coverage does not certify RGB functionality.

## Remaining integration

Both ImGui hosts now use this envelope for six axis/projection routes plus
Gaussian preview/Apply and individual recorder replay/rebind. Dialogs capture on opening; captured geometry
drives spacing/Slicer defaults/previews and changed context disables Apply.
Projection reserves command admission and publishes through its original capture.
Ordinary local actions retain old records with guard-only application bindings;
strict legacy replay remains local. Save/Open preserves new workflow records as
session data, without persisting/restoring application global state. Bulk native
workflow conversion explicitly rejects unsupported schemas.

Actual UI cases each use 119/128 frames. Installed verification passes 2,192 of
2,194 cases with two optional skips; CI actual capture/reader and 23 transport
checks pass. [UI evidence](parity/reviews/G034-calibrated-command-ui-progress001.json).
Display/XYZ/analysis caches, remaining consumer audit and Global/bypass controls
remain open, along with global-state session/I/O migration, original Calibrator
variants, supported platforms, RGB Slicer and matched speed/memory comparisons. Follow [the active plan](PLAN.md) and
[the calibration contract](packets/G034-global-calibration-contract.md).

## Gaussian command target and replay

Operation `gaussian_blur_imagej` takes `GaussianBlurCommandOptions`. The supplied
capture is the complete target: choose C/Z/T scope, ROI and destination explicitly.
For example:

```python
request = imagejxx.TargetRequest(imagejxx.TargetScope.ALL_Z,
                                roi=imagejxx.RoiPolicy.WHOLE_PLANE)
capture = app.resolve_target(request, document_id=source_id)
command = imagejxx.resolve_calibrated_command(
    capture, "gaussian_blur_imagej",
    imagejxx.GaussianBlurCommandOptions(2, True))
app.publish(imagejxx.prepare_calibrated_command(command))
```

The descriptor has `source`, `operation` and `command`; `command` uses the existing
validated Gaussian grammar for raw sigma, scaled flag, effective X/Y sigmas and
frozen target. It never borrows local calibration to explain global sigma.
Pixel-unit aliases keep their existing unscaled behavior. Other calibrated
command descriptors/formats remain unchanged.

Strict replay takes a current source/context capture and restores the original
recorded target after dependency checks, even if the caller navigated or supplied
a different default scope. Explicit rebinding instead uses the full target of
the new capture and replacement options. Validate the original first; neither
path installs global state from JSON. Whole-plane target selection revision is
canonical zero within this descriptor so irrelevant selection edits do not stale
it. Selection-bound commands retain their actual selection dependency. Pure
legacy Gaussian records and strict legacy replay retain their local semantics.

Admission reuses Application's existing staged-plane/kernel workspace base plus
mask allowance and the envelope/source metadata bound. In-place preparation keeps
local calibration; derived publication uses the generic effective-output policy.
Raw local/bypassed commands preserve opaque future density records. Runtime caps
reject before callbacks/reads; no measured RSS guarantee follows.

C++ `CalibratedCommandRequest` now has a Gaussian alternative. Rebuild SDK clients
and update exhaustive visitors; this does not promise binary compatibility with
the previous variant type. The installed Windows/Linux fixtures pass 4/4 clients.
The 52 new Python cases pass in the 2,246-case installed union (2,244 passed/two
optional skips). [Evidence](parity/reviews/G034-calibrated-gaussian-command-progress001.json).
Live Gaussian preview/Apply and recorder strict replay/rebind now use this
envelope in both ImGui hosts. Dialogs retain a minted effective capture; context
changes disable controls and stale jobs cannot publish. Replay retains original
author limits/record while rechecking the current backend cap. Rebind prefills
raw sigma/scaled intent and authors a complete new target; explicit calibrated
rebind remains an envelope even after Global clear. Ordinary local actions and
strict legacy replay preserve legacy semantics. Stack No retains captured Z;
Yes uses all Z at captured C/T. Preview/cancel never auto-records.
[UI evidence](parity/reviews/G034-calibrated-gaussian-ui-progress001.json).
Live display/XYZ/analysis caches, remaining consumer audit and Global/bypass
controls remain pending. See [the current plan](PLAN.md).
