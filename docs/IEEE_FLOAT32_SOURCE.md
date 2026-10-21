# IEEE Float32 source storage and remaining editor work

The [p07w shared editor checkpoint](IEEE_EDITOR_VIEWING.md) now qualifies
IEEE viewing and channel controls in source/installed C++ and Python hosts,
including typed readback, stable range caches and workspace file opening.
Earlier checkpoint-specific evidence and next-step cards below are historical;
full nonfinite processing and Float32 I/O remain open.

The [p07v channel V3 checkpoint](IEEE_CHANNEL_DISPLAY.md) now qualifies native
channel composition, scientific views, display commands and session transport
for IEEE sources. Shared desktop adapter admission and coherent full-host gates
remain next. The checkpoint-specific evidence below is historical for its
named scope.

The p07t checkpoint adds explicit, bit-preserving storage and transport for all
binary32 source patterns, including signed zero, subnormals, infinities and
signaling/quiet NaN payloads. Processing and display admission remain separate
work. This checkpoint does not claim full ImageJ/Fiji or desktop parity.

## Public contract

Opt in with C++ `FloatingSamplePolicy::PreserveIEEEFloat32` or Python
`FloatingSamplePolicy.PRESERVE_IEEE_FLOAT32`. Sessions use the explicit token
`preserve_ieee_float32`. Existing finite-only and quiet-NaN policies retain
their rejection rules and defaults.

The new policy requires owned or reviewed eager grayscale Float32 sources
(S=1). It does not admit RGB, arbitrary virtual sources, Float64 images or
nonfinite metadata. Source values use a reversible binary32-to-binary64 field
mapping internally. The mapping avoids floating-point casts that could quiet
signaling NaNs; arbitrary binary64 NaN payloads are rejected.

```cpp
using namespace imagejxx;
constexpr auto policy = FloatingSamplePolicy::PreserveIEEEFloat32;
Image image(2, 1, 1, PixelType::Float32);
image.pixels[0] = float32_source_sample(0x7f800000u, policy);
image.pixels[1] = float32_source_sample(0x7f800001u, policy);
auto dataset = Dataset::from_stack({image}, {}, policy);
```

The installed SDK consumer exercises this contract. Python buffer ingestion,
NumPy output, plane replacement, scientific axis permutations, regions,
XY/XZ/YZ extraction, copy-on-write history, native V3 sessions and detached V2
transport preserve source bits. Python editor adapters are not yet qualified
for the new policy. Native operation preparation explicitly refuses this policy
before reading pixels until a matching processing profile is qualified.

## Evidence and limits

The independent source-format fixture contains 4,096 representative patterns
and a manually framed V3 archive. It is not an exhaustive enumeration of all
binary32 values. Its generator does not import product code.

- Windows and Linux each pass 19 selected native suites, including 40,404
  source assertions.
- Source and isolated installed Python pass matching 2,759 cases each,
  including 58 new cases and all 2,441 cases from the previous selected gate.
- Buffer cases cover strides, transposes, reversed views, Fortran storage,
  big-endian words, unaligned addresses and read-only arrays.
- The installed public C++ SDK consumer passes source/session/history and
  unsupported-processing checks.

Four existing `test_V22_installed_editor_roundtrip_preserves_policy` variants
are explicitly deselected from this processing-only gate. They require a
coherent desktop rebuild and will return in the display/host gate. The p07s
7,799-case full-host checkpoint remains historical evidence for its qualified
quiet-NaN scope; it does not qualify the new policy.

See the [source review](parity/reviews/ieee-source-review-001.json) and
`build/p07t-ieee-float32-source001/final-audit001.json`. Current qualified
processing-only Python and SDK prefixes are recorded in that audit. No new
dependency, Qt component or non-commercial component was incorporated.
Reference Java runs remain observation-only. Thirteen owned scratch targets
were archived, verified and removed, saving 91,175,048 net bytes.

## Next implementation packets

The [p07u native display profile](IEEE_DISPLAY.md) now implements steps 1 and 2
for read-only grayscale projection. Desktop/channel admission and coherent
host gates in step 3 remain open. The p07t evidence above is historical for
its source/transport scope.

1. Implement the IEEE display/range profile against the 240 pinned ImageJ
   observations in `tests/fixtures/ieee-display-imagej154r.json`. Preserve source
   bits and classify nonfinite values before arithmetic or integer conversion.
   Automatic ranges with an infinite or NaN endpoint produce index-zero pixels
   in the captured renderer; ignoring infinities and displaying a finite ramp
   would be incorrect. Finite manual ranges clip infinities, and equal ranges
   require the captured step behavior. Normal/inverted LUTs and threshold
   modes have separate reference cases.
2. Represent automatic nonfinite range endpoints without JSON null conversion.
   Choose a private computed range representation or a versioned persisted
   contract before changing existing finite manual/channel range schemas.
   Keep the owned threshold tint contract distinct from ImageJ RED_LUT unless
   exact reserved-index rendering is implemented and tested.
3. Admit the profile through both shared ImGui hosts and optional Python UI.
   Batch host changes, rebuild extensions and both hosts coherently, then run
   source/installed actual-input cases, the four excluded editor cases, session
   reopening, preview/history, C/Z/T/ROI and full common regressions.
4. Implement full nonfinite Remove NaNs/rank processing using the 108 actual
   plugin observations in `tests/fixtures/remove-nans-ieee-imagej154r.json`.
   Qualify command admission, records/replay, masks, unchanged payloads,
   cancellation, new-image publication and Undo/Redo before removing the
   temporary processing barrier. Existing quiet-only processing remains the
   currently qualified behavior.
5. Add an explicit Float32 I/O profile and qualify editor import/export. Current
   general scientific TIFF export is finite UInt16; source/session support does
   not establish Float32 TIFF support. Follow with RGB rank and the remaining
   indexed, Fiji, editor and release gaps in the main plan.

The display corpus now qualifies the named p07u native profile; the processing
corpus remains preparation. Neither establishes full editor parity.
All99 remains 42 in progress / 57 open / 0 complete. The overall goal is active.
