# IEEE Float32 display profile

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

The p07u profile implements read-only native Float32 rendering with Python
bindings. It covers automatic/manual/equal ranges, normal/inverted grayscale,
red threshold display and black-and-white threshold display. It does not yet
admit IEEE sources into either desktop host, channel compositing, processing
commands or Float32 TIFF I/O.

```python
import imagejxx as ij
import numpy as np

words = np.array([[0xff800000, 0, 0x3f800000, 0x7f800000, 0x7f800001]],
                 dtype=np.uint32)
source = words.view(np.float32)
options = ij.Float32DisplayOptions()
options.range = [0, 1]
result = ij.render_float32_display(
    source, options,
    sample_policy=ij.FloatingSamplePolicy.PRESERVE_IEEE_FLOAT32)
# result.rgb is an independent uint8 HWC image.
# result.range_bits contains two raw binary32 endpoint words.
```

C++ uses the same `Float32DisplayOptions` and `render_float32_display` from
`imagejxx/float32_display.hpp`. `Float32DisplayResult` contains `Image rgb` and
`std::array<uint32_t, 2> range_bits`.

The API defaults to finite-only source admission. Quiet-NaN and full IEEE source
policies require explicit opt-in. Sources must be grayscale Float32 with exact
binary32-representable storage. Invalid intervals, foreign payloads, unsupported
types and inconsistent threshold options fail explicitly. This is a stateless
renderer; it does not reproduce mutable processor palette history or provide
arbitrary/custom LUTs.

Automatic bounds skip NaNs, retain infinities and preserve the first observed
signed-zero endpoint on ties. All-NaN bounds are canonical quiet NaNs. Bounds
are returned as raw words so infinity/NaN values never need JSON numeric
serialization. Existing finite manual/channel display metadata schemas stay
unchanged.

A missing range or `[0, 0]` requests automatic scaling, matching the captured
FloatProcessor reset behavior. Explicit finite bounds round to binary32 and
must remain finite. Equal finite bounds render as a step. Automatic nonfinite
bounds produce base index zero; finite manual bounds clip negative and positive
infinity to the low and high ends. Source NaNs produce base index zero and do
not pass threshold comparisons.

Threshold bounds are finite, ordered, inclusive source-domain doubles. Set both
`threshold` and `threshold_mode`; use `None` and `Float32ThresholdDisplay.NONE`
to disable them. Increasing ranges reserve index 255 for red highlighting,
with nonselected samples capped at 254. Black-and-white mode uses an uninverted
black/white palette. With collapsed or NaN bounds, a fresh ImageJ processor
retains its initial threshold mode and original grayscale palette; the profile
matches that behavior in the captured cases. This profile is separate from the
existing editor's owned threshold tint.

Windows and Linux each pass 20 selected native suites, including 35,293
display assertions. Source and isolated installed Python pass matching 3,022
cases, including 263 new display cases and all 2,759 previous selected cases.
The installed C++ SDK consumer passes. Four editor variants remain explicitly
deferred to coherent full-host qualification. Verified scratch cleanup saves
90,659,566 net bytes. No new dependency or non-commercial component was added.
See the [display review](parity/reviews/ieee-display-review-001.json).

Qualification uses all 240 pinned `FloatProcessor.createImage` observations in
`tests/fixtures/ieee-display-imagej154r.json`, covering ten signals, four range
modes, two LUT directions and three threshold modes. Tests compare RGB bytes,
raw endpoints and unchanged source words. Additional Python cases cover
strides/endian/unaligned/read-only buffers, independent result ownership and
admission errors. This establishes the named profile, not full editor parity.

Next batch the desktop and channel admission changes. Use this native profile
for base Float32 rendering while keeping computed nonfinite endpoint words
outside persisted finite range fields. Rebuild both hosts and extensions
coherently, restore the four explicitly deferred editor tests, and exercise
sessions, scientific slices, preview, threshold overlays and source/installed
actual-input flows. Full nonfinite Remove NaNs/rank processing and Float32 I/O
remain subsequent packets. See the [main plan](PLAN.md) and
[source contract](IEEE_FLOAT32_SOURCE.md).
