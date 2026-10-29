# Calibrated command capture and replay

Status: scoped verified native/Python APIs. All 119 new command cases pass;
combined installed verification passes 2,190 of 2,192 cases with two optional
hidden-OpenGL skips. Both core SDKs pass 4/4 installed clients.
[Evidence](parity/reviews/G034-calibrated-command-progress001.json).
This additive interface covers plane export, range reslice, orientation, physical
spacing, finite-scalar Slicer resize and ImageJ projection. Existing source-only
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

Both ImGui hosts now use this envelope for six live global/bypassed actions and
individual recorder replay/rebind. Dialogs capture on opening; captured geometry
drives spacing/Slicer defaults/previews and changed context disables Apply.
Projection reserves command admission and publishes through its original capture.
Ordinary local actions retain old records with guard-only application bindings;
strict legacy replay remains local. Save/Open preserves new workflow records as
session data, without persisting/restoring application global state. Bulk native
workflow conversion explicitly rejects unsupported schemas.

Actual UI cases each use 119/128 frames. Installed verification passes 2,192 of
2,194 cases with two optional skips; CI actual capture/reader and 23 transport
checks pass. [UI evidence](parity/reviews/G034-calibrated-command-ui-progress001.json).
Scaled Gaussian/live replay, display/XYZ/analysis caches and Global/bypass controls
remain open, along with global-state session/I/O migration, original Calibrator
variants, supported platforms, RGB Slicer and matched speed/memory comparisons. Follow [the active plan](PLAN.md) and
[the calibration contract](packets/G034-global-calibration-contract.md).
