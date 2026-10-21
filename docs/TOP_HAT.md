# ImageJ Top Hat

Finite scalar UInt8, UInt16 and Float32 Top Hat is available in the C++ core,
Python API and shared Dear ImGui dialog in both desktop hosts. The existing
integer-disk `top_hat` operation retains its behavior. RGB and nonfinite rank
profiles remain separate gaps; this checkpoint does not close the rank family
or the broader ImageJ/Fiji release gates.

## Desktop and Python

Use **Process > Filters > Top Hat (ImageJ)**. Controls include radius, Light
Background, Don't Subtract, preview, new-image output and the shared target
chooser. Legacy stacks offer Yes/No/Cancel; scientific sources support C/Z/T
ranges and positioned selections. A contained one-plane preview cannot replace
the accepted complete Apply target. The dialog remembers accepted Top Hat
parameters independently of Minimum, Median, Despeckle and Remove Outliers.
Parameter-dialog cancellation discards drafts; later stack-prompt cancellation
retains already accepted parameters. Radius zero retains the owned five-point
cross behavior and is remembered, unlike ImageJ's reopen-at-two convention.

```python
import imagejxx as ij

pixels = ij.top_hat_imagej(image, radius=2.0, light_background=False,
                          dont_subtract=False, inverted_lut=False)

options = ij.TopHatCommandOptions(2.0, False, False)
command = ij.resolve_top_hat_command(owner.snapshot(), options=options)
proposal = ij.prepare_operation(command.operation, command.target,
                                command.parameters())
owner.commit(proposal.begin_edit())

# Optional Python host; processing and widgets remain shared native code.
editor.apply_top_hat(options=options, wait=True)
```

The pixel API accepts an optional `(x, y, width, height)` extent. It never
modifies its input. Radius must be finite in [0,1000]; Python flags are strictly
Boolean and radii reject Booleans. Unsupported samples, invalid extents and
Float32 subtraction overflow refuse. The pixel API does not edit display state.

The resolved command is immutable and source-bound. Its strict schema1 record
uses operation `top_hat_imagej`, profile `imagej_154r` and exactly radius,
light_background, dont_subtract and inverted_lut options. Replay and explicit
rebind validate every original claim and preserve the recorded LUT direction.
Fresh resolution captures active base-LUT inversion without reading samples.
Grayscale channel mode uses Grays; color/composite uses the active channel LUT;
threshold coloring does not determine polarity. Existing rank/outlier record
grammars remain exact. Native workflows and desktop replay accept the distinct
captured record. Python options are immutable once constructed.

## Compound pixels and display policy

`light_background != inverted_lut` selects closing (maximum then minimum).
Otherwise the background is an opening (minimum then maximum). Both passes use
the qualified ImageJ fractional-radius footprint and nearest-edge extension.
The first pass expands the write rectangle for the second pass's halo. Pixels
outside the final rectangle are restored; irregular masks restrict final writes,
never either intermediate pass. Don't Subtract returns the background.
Subtraction uses two Float32 stages, original minus background then offset.
Closing adds 255/65535 for integers and zero for Float32; negative Float32
closing residuals are intentional.

The editor pairs range, threshold and pixel effects in one guarded transaction.
A live subtraction clears the active processor threshold even for byte images
and collapsed histograms. Detached stack processors leave the active threshold
alone unless a successful nonbyte final stretch resets it. Background-only
skips these effects and histogram admission. A threshold anchored to another
plane remains intact. One targeted active plane uses the live lane; other
scientific Apply targets use detached-plane semantics. This is an explicit
editor extension. Nonbyte final stretching uses the entire active processor's
qualified 0.5% tails, independently of the write ROI.

Canonical channel state persistently receives the active processor's final
range while retaining other ranges, LUTs, visibility and projection. This does
not reproduce ImageJ's transient processor/stored-LUT persistence quirks.
Absent legacy ranges may be materialized when the setter reports them, as in
the existing variance publication policy. That representation change can add a
display-history step even when a byte image's effective default range is equal.
Automatic zero-range setter effects are captured during the existing pixel
pass, without repeating compound filtering or allocating another image.

## Explicit preview phase and publication

`ExecutionOptions.phase` defaults to `ExecutionPhase.APPLY`. The shared job
path explicitly submits previews with `ExecutionPhase.PREVIEW`; phase is not a
filter parameter, record field or job-label convention. Top Hat preview accepts
one in-place plane and uses that plane's captured channel range. It stretches
the rectangular processor result before restoring an irregular write mask.
Apply stretches the completed masked result. Empty write masks stay read-free.

A whole-image-bounds triangle over constant Float32 value 7 demonstrates the
difference: preview retains [7,127], while Apply yields [0,6.97265625], with
identical masked pixels. Two pinned ImageJ observations qualify the processor
behavior; actual input case R44 verifies the renderer's numerical ranges.
The direct preview reference explicitly restores its saved mask and does not
claim to automate ImageJ's own preview UI.

Preview proposals expose their temporary snapshot through `.preview` but reject
`begin_edit`, `stage_new_document` and `Application.publish`. They cannot create
history or workflow entries through publication. Apply records only successful
publication. Source revision, cancellation, generation and resource guards stay
shared with the other rank jobs.

New-image staging preserves source pixels, display and history. The new owner's
first operation retains a paired source display snapshot: Undo/Redo restores
pixels, range and threshold together. Qualification caught and fixed the staged
factory's omitted display-pairing flag; ordinary publication already supplied
that flag. The regression is also exercised directly for all three scalar types.

Resource admission includes the first-pass image beyond the generic detached
output, both simultaneously live footprint arrays and optional histogram arrays
and control storage. Tests cover radius 1000 on a one-pixel image, changing image
extent and background-only admission without histogram reads. Reservations are
conservative accounting, not an allocation arena or a bound on provider caches.

## Qualification

Pinned ImageJ 1.54r commit: `62db90a269cccc1ac79f658e4602043214ed055b`.
Reference coverage consists of 576 plugin-runner cases, 144 channel-runner cases,
216 direct preview-flag cases and two full-bounds-mask observations. The 576-case
capture separately verifies observed plugin options. Verified upstream classes
and runtime remain qualification artifacts; none ships as a Java dependency.
Commercial provenance continues the existing rank commercial reviews. No new
runtime dependency, plugin incorporation or non-commercial asset was added.

Current gates:

- Windows: 47 selected native/desktop suites, including all 40 shared widget
  cases. The final47-suite run passed46 and refused a stale renderer test
  directory; after verified archive/removal, that unchanged suite passed alone.
- Linux: ten selected native suites against the current sources.
- Native command: 8,547 checks/platform; preview preparation: 2,184;
  retained pixel profile: 7,245; final-display backend: 7,184.
- Source and installed Python: identical 7,417-case inventories, retaining all
  previous 5,921 cases and adding 741 pixel, 741 command and 14 UI/phase cases.
- Actual input R38-R44 in source/installed C++ and Python hosts: parameters,
  recording, stack choices, scientific targets, source-preserving new image,
  preview ranges/pixels and Undo/Redo. Prior R14-R37 remain qualified.
- Installed public C++ consumer and 120 separate genuine-venv projection checks.
  Headers, core, hosts and extensions are rebuilt and checked together.

Initial test namespace/history setup errors, a reference mask-setup error,
installation log-label collision, misinvoked projection wrapper and the real
staged-display/catalog failures are retained with corrected evidence. Assertions
remain strict. See [UI review](parity/reviews/tophat-ui-review-001.json) and
`build/p07p-tophat-ui001/tophat-ui-handoff001.json` for pinned receipts and
verified scratch cleanup. Older API-only checkpoints remain historical evidence.

## Remaining work

Continue with Remove NaNs and the remaining RGB/nonfinite rank profiles, sharing
pixel selection code and batching host rebuilds where practical. The next
reference corpus already contains 84 Remove NaNs runner cases. A 168-plane
independent numeric check exposed 285 signed-zero tie differences from ordinary
sorting; implementation must retain ImageJ's existing qualified selection order
and qualify payload preservation. This is preparation, not a shipped NaN filter.
Indexed formats, remaining commercially usable Fiji components and broader
release qualification remain in the full plan. All99 remains 42 in progress /
57 open / 0 complete; Source901 and the historical release gate are unchanged.
