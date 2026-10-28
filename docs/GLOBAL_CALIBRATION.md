# Global and effective calibration API

The native core provides immutable calibration values, a metadata-only resolver
and Application-owned global state. This is the foundation for global editor
integration. Existing processing/target APIs and ordinary DocumentSnapshot reads
continue to use local document calibration; downstream consumers and the shared
ImGui Global option are not yet connected. Setting this explicit state alone
does not change processing output or enable global calibration in the editor.

## Values and resolution

GlobalCalibration(spatial, intensity, source_type, source_samples=1) owns validated
SpatialCalibration and ImageJIntensityCalibration values, plus source storage
information. GlobalCalibration.from_snapshot(source) reads the source's local
metadata. It retains neither document nor pixels/provider. Source bit depth is
kept even for None density, because it affects adaptation to another image.
Python properties return immutable spatial values and detached array containers;
modifying a returned array does not mutate a value already owned by Application.
Use the SpatialCalibration constructor to configure replacement values.

resolve_effective_calibration(source, global_calibration=None,
ignore_global=False) returns an EffectiveCalibration. It copies global spatial
values and applies the inspected ImageJ Calibration.setImage density policy:
local signed16 has precedence for a scalar UInt16 destination; other mismatched
integer depths clear density and restore Gray Value; Float32 clears density but
keeps the global value unit; RGB clears both density and its value unit. Native
Float64/RGBA extend the Float32/RGB policies. Compatible lookup records are shared
immutably, preserving exact Float32 words without materializing/recomputing a
table. Ignoring global returns the original local spatial/intensity values.

These are native spatial values. ImageJ pixel-origin coordinates must be
converted to the existing world-space SpatialCalibration convention at an
import/original-reference boundary. The initial reference checks spacing,
X origin, unit and density; complete time/direction/original UI cases remain.

## Application state and capture validation

The API is available in C++ and through imagejxx.Application in Python:

```python
app.set_global_calibration(ij.GlobalCalibration.from_snapshot(app.snapshot()))
captured = app.effective_calibration()
local = app.local_calibration()  # local metadata, regardless of the global value
app.validate_effective_calibration(captured)
app.set_ignore_global_calibration(True, document_id)
app.clear_global_calibration()
```

The global_calibration property returns the immutable value or None;
global_calibration_generation identifies global replacements/clears. Every
explicit Set advances it; Clear with no value is a no-op. Each document has a
separate bypass generation, so changing another document's bypass does not
invalidate a capture. Bypass flags are removed when their owner is closed.
Independent Applications have independent global state.

EffectiveCalibration contains spatial/intensity values, source ID/revisions,
uses_global/ignores_global and both generations. An application-bound capture
also has a weak application identity, with no retained owner/provider. Validation
requires the same application, live membership, matching source Pixels+Metadata
revisions, matching own bypass generation and applicable global generation.
Ignored captures do not depend on global changes. Selection/display/activation
alone do not invalidate this calibration-only capture; prepared operations still
need their normal source/activation/selection guards.

Validation is a read-only check, not an atomic publication API. A later edit
must validate calibration generations under the same application lock as its
publication; check-then-commit outside that lock is not a safe substitute.
Pure detached resolver results cannot be validated as application captures.
Captures remain readable after their source closes but then fail validation.

No local metadata, pixels, revisions, dirty state or undo are changed by global
Set/Clear/ignore/capture. Local edits remain local while global is present; clearing
restores their current values. There is no new persistence/replay schema here.

## Remaining integration

Follow [the global calibration contract](packets/G034-global-calibration-contract.md)
for remaining resolved-command/admission/record integration, display/XYZ,
measurements/profiles/histograms/projection, scientific I/O/session and shared UI
integration. The existing projected_imagej_intensity_calibration helper is an
output policy, not a global adapter. No full ImageJ/Fiji, supported-platform or
comparative performance acceptance follows from the metadata foundation.

## Verified generic processing integration — 2026-10-28

The working tree now carries an application calibration binding in ordinary
Application.resolve_target results. A binding captures application identity,
global generation and the document's bypass generation. When global calibration
is active and not bypassed it also holds the immutable effective value. With no
global value, or while bypassed, raw target resolution does not decode the local
reserved intensity property. Enabling global calibration still invalidates a
previous application target. Another document's bypass change does not.

Application prepare/submit and publication validate this binding. Publication
checks generations under the same registry lock as mutation/adoption; direct
Document writers are also excluded during the final source check and derived
adoption. In-place proposals retain local calibration. Generic derived images
receive effective spatial calibration and compatible density; changing storage
type drops density, retaining the value unit for float outputs. Color outputs
have no density. This policy does not certify every operation's scientific
conversion semantics.

The ResolvedTarget Gaussian overload resolves scaled sigma using the captured
spatial calibration and retains its guard. Pure snapshot target resolution stays
local and detached, including when explicitly passed to an Application. Target
record schema1 is unchanged: runtime application identity/generations and global
density are not serialized. Global replay/session persistence remains open.

The generic source-edit preview retains local metadata; effective derived output
metadata is installed on the created output owner. Specialized axis exports,
reslices/projections and other dedicated preparation routes still require their
own integration. Display/XYZ/measurement/plot caches and the shared Global UI
control remain pending. Installed Windows verification collects1216 cases:1214 pass, two optional
hidden-OpenGL checks skip. All19 new execution cases and six existing C++ UI
regressions pass. Both installed core SDKs pass4/4 CTest clients. This does not
certify the remaining global consumers, full platform matrix or performance.
See [scoped evidence](parity/reviews/G034-global-calibration-execution-progress001.json).

## Verified dedicated application consumers — 2026-10-28

The existing Application.prepare_axis_plane_export, prepare_axis_range_reslice,
prepare_axis_range_orientation, prepare_axis_range_spacing and
prepare_axis_range_slicer_resize now capture the same generation binding as
ordinary application targets. Application.prepare_imagej_projection(request,
document_id, control) adds a dedicated projection entrypoint; Python takes the
existing projection request dictionary and cancellation/progress/checkpoint
keywords. Python's three former direct snapshot preparation shortcuts now carry
an application target captured before callback setup and GIL release.

A private adapter resolves each factory's exact contributor target against the
original immutable source and retains its captured guard. Axis remapping and
physical sampling receive Dataset.with_calibration(effective.spatial), which
shares the original pixel provider. Source transactions and provenance keep the
original owner/revisions/local calibration. Density is installed only on derived
axis owners; projection retains its own existing calibrated numerical/output
policy. Projection admission includes the captured global record and units in
its metadata/working budget before callbacks or pixel reads.

Pure snapshot overloads preserve local geometry, density policy and detached
publication contract. Native command records/resolvers and current ImGui command
routes have not yet adopted these factories, and no Global toggle is enabled.
Installed Windows verification collects1,811 cases:1,809 pass, two optional
hidden-OpenGL checks skip. All67 consumer cases and original axis/projection
regressions pass; RGB Slicer is explicit unsupported-refusal coverage. All11
native control invocations and six C++ UI regressions pass. Both installed core
SDKs pass4/4 CTest clients. No full platform/family acceptance follows.
See [scoped evidence](parity/reviews/G034-global-calibration-consumers-progress001.json).
