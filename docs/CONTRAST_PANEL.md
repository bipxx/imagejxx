# Brightness/Contrast

Open **Image → Brightness/Contrast...** in either Dear ImGui launcher. The
modeless panel provides minimum/maximum fields and sliders, brightness,
contrast, window/level, Reset and Auto. Scientific images expose a channel
selector. Manual adjustments affect the active channel when channel display is
enabled; they leave source pixels, dirty state and image undo history unchanged.

The source histogram runs through the native job scheduler. Changing image,
T/C/Z or pixel revision discards the old capture. Display adjustments reuse the
raw histogram. Closing the panel cancels its work and releases its retained
result; cancellation is sticky until Refresh or a new source capture. Histogram
jobs respect the processing pixel/memory limits. Provider and decoder internals
remain outside the core's declared reservation.

Brightness preserves the current window and moves its level across the source
domain. Contrast preserves level and maps its normalized slider to a window of
`domain_width * 2**(8*(0.5-value))`. UInt8 uses the 0–255 domain; UInt16 and
Float32 use the captured plane extrema. These mappings are explicitly defined
local behavior; exact ImageJ slider normalization parity remains to be checked.
Minimum/maximum sliders constrain crossing endpoints; unsupported equal ranges
are rejected before publication. Typed finite endpoints can extend outside the
source domain. Window must be positive and representable.

Auto uses the existing native saturation-aware, selection-aware asynchronous
service and its recording/cycle rules. The checkboxes choose area-selection use
and all-channel scope for composite display. For an image without channel
display, Auto stages a private Color-mode state inheriting its range and LUT.
Only a successful, current result publishes that state; cancellation, admission
failure or stale captures leave the display unchanged. Existing opaque channel
properties are rejected rather than replaced. The original strict Auto API
continues to require recognized channel display.

The additive C++ `Application::submit_seeded_auto_contrast(initial, options,
limits, job_options)` API accepts a typed `ChannelDisplayState`. Python accepts
the existing channel-display dictionary format. The returned job uses the same
`publish_auto_contrast` method as ordinary Auto jobs. Copying frontend arguments
precedes native job admission; the native implementation bounds staged metadata
before copying it into a detached source capture.

Histogram and Auto currently support scalar UInt8, UInt16 and finite/quiet-NaN
Float32. Float64, IEEE-preserving Float32 and packed RGB/RGBA retain finite manual
controls but have no histogram/Auto in this panel. An implicit floating range
must resolve or be set manually before sliders become available. Reset uses the
cached plane extrema, UInt8 0–255, or the configured UInt16 bit-depth range.

Scalar UInt8/UInt16 **Apply to pixels** now uses native captured worker jobs,
shared T/C/Z/ROI controls and a paired pixel/display Undo/Redo step. See
[Apply behavior and editor actions](CONTRAST_APPLY.md). Broader sample-format
histogram support, color Apply, complete ImageJ reset/set/apply semantics, and
full upstream UI/platform/resource qualification remain open under G057. This panel is a scoped increment, not a
claim of complete ImageJ/Fiji editor parity.
