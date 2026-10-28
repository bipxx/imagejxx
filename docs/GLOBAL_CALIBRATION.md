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
for target/prepared-command propagation, atomic publication guards, display/XYZ,
measurements/profiles/histograms/projection, scientific I/O/session and shared UI
integration. The existing projected_imagej_intensity_calibration helper is an
output policy, not a global adapter. No full ImageJ/Fiji, supported-platform or
comparative performance acceptance follows from the metadata foundation.
