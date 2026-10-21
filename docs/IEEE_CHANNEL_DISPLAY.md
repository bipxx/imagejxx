# IEEE Float32 channel display V3

The [p07w shared editor checkpoint](IEEE_EDITOR_VIEWING.md) now qualifies
IEEE viewing and channel controls in source/installed C++ and Python hosts,
including typed readback, stable range caches and workspace file opening.
Earlier checkpoint-specific evidence and next-step cards below are historical;
full nonfinite processing and Float32 I/O remain open.

The p07v checkpoint adds native channel viewing for explicit IEEE Float32
sources: defaults, range resets, scalar/color/composite rendering, XY/XZ/YZ
planes, Sum/Max/Min projections, display commands, replay/rebinding and native
session transport. Python calls the same C++ implementation. Desktop adapter
admission and actual-input host qualification remain the next packet.

## State and range contract

`FloatingSamplePolicy::PreserveIEEEFloat32` requires explicit
`ChannelDisplayVersion::V3`. V3 requires the same top-level fields as V2:
schema, version, mode, projection and channels. Finite/quiet source policies
retain their V1/V2 contracts and defaults; V3 cannot widen those policies.

Finite manual ranges retain their numeric `[low, high]` form. Nonfinite
automatic ranges use exact integer words instead:

```json
{
  "schema": "imagejxx.channel-display",
  "version": 3,
  "mode": "grayscale",
  "projection": "sum",
  "channels": [{
    "visible": true,
    "range": {"float32_bits": [4286578688, 2139095040]},
    "lut": {"name": "Grays"}
  }]
}
```

This example retains negative/positive infinity endpoints without JSON NaN,
infinity or null values. Raw endpoints require exactly two uint32 integers.
Booleans, floats, negative/overflow words and extra fields are rejected.
Ordered infinity ranges and the canonical all-NaN pair `[0x7fc00000,
0x7fc00000]` are allowed. Signaling/payload NaNs, mixed NaN/numeric endpoints,
reversed bounds and raw-form finite pairs are rejected. These restrictions
apply to computed bounds; source pixels retain every admitted payload.

Automatic ranges skip NaNs and include infinities. Defaults inherit the first
C0 live-XY reset range across C, following the existing channel convention.
Explicit reset scans only the requested live channels at captured T/Z.
`empty_finite` still means there were no finite samples, including all-infinity
sources. All-NaN IEEE ranges retain canonical NaN bounds; old quiet-NaN resets
keep their owned `[0,1]` fallback. Python observations encode nonfinite ranges
with the same integer-word object and can be serialized with `allow_nan=False`.

NaNs select LUT entry zero. A nonfinite automatic range maps all base samples
to entry zero; a nonblack custom entry zero remains visible. Finite manual
ranges clip infinities and preserve NaN payloads. Channel `[0,0]` remains a
literal interval; it is distinct from the standalone FloatProcessor profile's
zero-pair automatic reset. Existing finite channel arithmetic and the wider
finite-double endpoint extension remain unchanged.

```python
import imagejxx as ij
import numpy as np

pixels = np.array([[0xff800000, 0, 0x3f800000, 0x7f800000]],
                  np.uint32).view(np.float32)
data = ij.Dataset(pixels, axes="YX",
                  sample_policy=ij.FloatingSamplePolicy.PRESERVE_IEEE_FLOAT32)
state = ij.default_channel_display(data, mode="grayscale")
automatic = ij.compose_channel_display(data, state)
state["channels"][0]["range"] = [0, 1]
manual = ij.compose_channel_display(data, state)
```

## Commands and evidence

IEEE display commands use an explicit V3 envelope with `display_dependency`
and `navigation` fields, retaining resolved state and reset observations.
Projection selection preserves V3. Set/Disable/projection selection and strict
replay do not read pixels; explicit range reset and reset rebinding do. Display
publication shares source storage and adds no pixel Undo/Redo or dirty change.
Projection preflight includes the raw-range object's metadata cost. Source
processing admission remains independently guarded.

Windows and Linux each pass 35 selected native suites, including 11,252 IEEE
channel assertions. Source and processing-only installed Python pass identical
3,398-case inventories, retaining all 3,022 previous selected cases and adding
149 IEEE channel cases. A genuine venv passes 180 installed-only projection
checks, with archived real module origins verified. The installed C++ SDK
consumer passes. Four editor variants remain explicitly deferred to the shared
host gate. Seventeen owned scratch targets were archived, verified and removed,
saving 100,548,664 net bytes. See the
[channel review](parity/reviews/ieee-channel-review-001.json).

Qualification compares all 80 non-threshold channel cases from the pinned
ImageJ renderer corpus. The separate native Float32 renderer retains its full
240-case threshold qualification. Literal scientific tests independently
derive source coordinates and RGB composition for T/C/Z, XY/XZ/YZ and all
three projections. Additional checks cover sessions, records, malformed range
words, old-policy guards, cancellation, resource refusal and unchanged source
bits. Future-version placeholders in three older native tests move from 3 to
4; their opaque transport/refusal assertions remain intact.

## Next shared-host packet

Batch the remaining adapters before rebuilding both hosts and extensions:

1. Admit the new policy in `src/desktop/document.cpp`, typed Python desktop
   buffer transport and legacy source/session adapters. Preserve signaling
   words through canonicalization/readback and exact policy tokens. Channel
   state still requires native V3 sessions.
2. Add an explicit computed-bound override to the native Float32 renderer, or
   an equivalent shared private mapping path. Orthogonal views must use the
   live XY processor range even when an extracted XZ/YZ plane has different
   extrema. A nonfinite common range must stay at index zero in every plane.
   Keep computed endpoint words out of finite manual range metadata.
3. Route IEEE base rendering through the native profile and the existing LUT
   tables. Handle infinities explicitly in label-color arithmetic, avoiding
   NaN/Inf-to-integer conversions. Extend only the private read-only threshold
   overlay admission; finite-only threshold editing remains separately scoped.
4. Update orthogonal cache keys and diagnostics to carry raw endpoint words.
   NaN array comparison must not force a rebuild each frame; diagnostics must
   not silently turn endpoints into JSON null. Update channel diagnostics and
   the channel panel to show computed bounds and accept finite manual edits
   without dropping V3 during projection selection.
5. Rebuild both C++ and optional Python ImGui hosts coherently, restore the four
   explicitly deferred editor variants, and qualify source/installed actual
   input, scientific slicing, custom LUTs, thresholds, session reopening,
   source readback and history. Keep current processing-only prefixes distinct
   from the historical p07s full-host products until these gates pass.

Full nonfinite Remove NaNs/rank processing, Float32 I/O, RGB rank and remaining
ImageJ/Fiji/editor/release gaps remain open. No new dependency or non-commercial
component was incorporated. See [the plan](PLAN.md),
[the native renderer](IEEE_DISPLAY.md) and [source storage](IEEE_FLOAT32_SOURCE.md).
