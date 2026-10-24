# P13i: Top Hat transient Preview display repair

**Resolved:** the renderer repair, private range/threshold effects and their
lifecycle checks pass in [P13i final qualification](../parity/reviews/p13i-path-filter001/receipt.json).
The reproduction/dispatch below records the original failure and frozen repair
contract; it is not a pending implementation task.

State: reproduced renderer omission; implementation pending. This is a newly
reproduced part of E3, within the active P13i packet, not a new product packet.
Exactly A/B/C execute it under the three-agent handoff. Original numeric kernels,
wire versions, historical expectations and all 99 family states stay unchanged.

## Reproduction and independent expectations

The eight existing specialized Top Hat recipes use Float32 23x17 dyadic input,
the fractional P triangle, initial range [-8,8], radius2 and actual LUT input.
Four subtracting variants render Preview with the live range even though the
native prepared snapshot contains a valid phase-specific display proposal.
Apply already uses the correct range. The existing R44 case missed this because
its Preview range equals the source range.

New pinned fixture: `tests/fixtures/path-top-hat-display-imagej154r.json.gz`,
16 actual Java phase observations. Its direct Preview sequence matches the
runner's current-mask restoration; contrast adjustment drops the mask and causes
36 raw unselected writes in each subtracting Preview. Preserve that observation.
Native pixel acceptance still requires original words outside the target.
All 474 prior compiled Java class identities remain unchanged; one class added.
The old wrappers under `scripts/capture_tophat_*_reference.py` are retained source
only and must not recreate their historical build paths.

The new UI assertions intentionally fail four of the eight existing standalone
reports; diagnostics are in `.tmp/p13i-verification/top-hat-display-ui-failure001`.
Existing E3 pixel/options/storage/history results remain scoped evidence; this
range requirement is not qualified until the repair and tests pass.

## Ownership transfer and internal contract

A receives narrow ownership of `src/desktop/gaussian_jobs.hpp/.cpp` for extracting
actual installed Preview effects, plus a new native unit test named
`tests/native/test_preview_display_effects.cpp`. B owns its existing E4 files,
then the render-consumer sections of `window.cpp`, `viewport_renderer.hpp/.cpp`,
`orthogonal.hpp/.cpp` and `rendering.hpp/.cpp` only where required by this seam.
Confirm exact needed files at handoff; no whole-file refactor. C owns CMake,
independent reference/UI/readback tests and receipts. Every worker preserves
others' edits; no worker builds, installs, commits or starts additional agents.

Freeze the following value-only internal interface before B consumes it:

```cpp
struct PlanePreviewDisplayEffects {
    PlanePosition position;
    std::uint64_t generation;
    std::optional<std::array<double, 2>> range;
    bool suppress_threshold_overlay = false;
};
std::optional<PlanePreviewDisplayEffects>
GaussianJobs::preview_display_effects(const DocumentSnapshot& current,
                                     PlanePosition candidate) const;
```

If an existing exact type/name is needed for compatibility, A reports the change
before B edits a consumer. Do not expose a public SDK or wire-format addition.

## Required behavior

- Use actual installed READY Top Hat subtraction results. Empty/skipped work or
  a missing proposal cannot invent an effect. Gaussian, ordinary rank and
  don't-subtract use the live contrast policy.
- Project only actual range and captured-threshold reset, not the whole captured
  display properties. No new histogram, filter execution or provider reads.
- Preserve manual live changes with field-specific baseline comparisons. A live
  range override wins if its presence/value changed since capture. A changed live
  threshold wins independently. Camera, LUT, visibility, opacity and navigation
  changes must not suppress an otherwise valid owned range delta.
- Reuse the pixel Preview ownership/TCZ gate. Ordinary views and linked views use
  the effect for their matching active Preview candidate; other planes stay live.
  Composite contributors apply it to the captured channel only. Preserve the
  existing linked-view contrast scope; do not invent per-pixel contrast modes.
- Never publish the owner's display during Preview. Cancel, source invalidation,
  navigation away and manual overrides must remove the transient presentation.
- Pass frozen values into asynchronous render requests. Workers cannot access
  the controller. Include effective range, threshold/provenance, generation and
  target identity in every affected cache key. Restore live presentation on exit.
- `contrast_source` reports the actual choice; RGB-only neutral contrast helpers
  retain their original contract and must not be repurposed for scalar Top Hat.

## Acceptance and execution

A tests READY/lifetime/target gates; actual proposal extraction; independent live
range and threshold overrides; unchanged source display; no effect for Gaussian,
ordinary rank, don't-subtract and empty work; navigation without data mutation;
resource release without retaining snapshots in effect values.

C independently checks all eight Top Hat Preview/Apply ranges against the new
fixture in both launches/platforms, preserving exact sample/storage words and all
prior assertions. Add focused presentation checks for ordinary synchronous and
asynchronous output, linked XY/XZ/YZ and composite contributors, cache refresh,
live range/threshold overrides and cancellation. Assertions must verify effective
readback as well as diagnostics; a changed label alone cannot qualify rendering.
Existing threshold-reset native references remain required because E3's source
has no manual threshold. Collect exact new cases before running; no skips or new
exclusions. Re-run preceding Gaussian/rank live-contrast controls affected by the
new seam. C alone serializes existing-cache builds and installed acceptance.

E4 lifecycle source may proceed independently while A extracts effects and C
validates references; B finishes and holds its shared probe before integrating
render consumers. Follow the combined 8GiB storage and verified scratch-retention
rules. Do not call E3 or P13i fully qualified from the earlier pixel-only result.
