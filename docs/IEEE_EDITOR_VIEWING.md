# IEEE Float32 shared editor viewing

The p07w editor uses the native IEEE Float32 renderer in the C++ ImGui host,
the optional Python ImGui host and opted detached Python display conversion.
The profile covers ordinary XY viewing, linked XY/XZ/YZ views, channel V3
composition, finite manual ranges, automatic ranges, LUTs, source readback and
native workspace/session transport. Processing admission remains separate.

```python
import imagejxx as ij
import numpy as np
from imagejxx.ui import Editor

words = np.tile(np.array([
    0xff800000, 0x7f800000, 0x7f800001, 0xffc12345,
    0x80000000, 0, 0x3f800000, 0x3f000000,
], dtype=np.uint32), (2, 4, 1))
source = ij.Dataset(words.view(np.float32), axes="ZYX",
                    sample_policy=ij.FloatingSamplePolicy.PRESERVE_IEEE_FLOAT32)
owner = ij.Document(source)
editor = Editor()
editor.add_native_document(owner)
view = owner.snapshot().display
view.orthogonal = True
view.crosshair = [3, 2, 1]
editor.live_document().set_display(view)
```

The policy remains opt-in and requires owned/reviewed eager grayscale Float32
storage. Finite-only and quiet-NaN defaults retain their previous contracts.
The underlying storage, slices, quantitative source readback and sessions retain
infinity signs, signaling/quiet NaN payloads and signed zeros. Python plane
readback copies validated source words instead of converting signaling NaNs
through floating-point casts. No processing implementation is added in Python.

## Ranges and controls

Automatic processor bounds are scanned without allocating RGB output. Views
retain the live XY processor bounds across linked planes. An XZ/YZ plane that
contains finite pixels still uses the common nonfinite range when XY contains
infinity; it must not independently rescale into a finite ramp.

`Float32DisplayOptions.computed_range_bits` explicitly freezes two uint32
endpoint words. It is mutually exclusive with `range`, accepts ordered numeric
bounds or the canonical all-NaN pair, and treats a zero pair as a literal
interval. The existing standalone `range=[0,0]` automatic-reset convention is
unchanged. `float32_display_bounds` exposes the allocation-free native scan.
The options layout has grown: rebuild public headers, core, bindings and C++
consumers together. The qualified products are rebuilt coherently.

Computed nonfinite bounds stay outside persisted finite manual range fields.
Diagnostics and channel V3 use exact integer endpoint words, avoiding JSON
null substitution. Cache comparisons recognize stable NaN bounds, so unchanged
frames retain their rendered planes. Channel text drafts also remain stable
while typing into an all-NaN range. The panel shows computed nonfinite bounds
and accepts finite overrides; applying/resetting ranges and selecting
Sum/Max/Min retains explicit V3 state and pixel history.

All rendering uses temporary RGB data. NaNs map to LUT entry zero, including
nonblack custom entries; finite manual ranges clip infinities. Label-color
rendering classifies nonfinite source words before integer conversion. The
private read-only threshold tint accepts the IEEE source policy and skips
nonfinite samples. Public finite-only threshold analysis/editing contracts and
unqualified processing guards remain unchanged.

## Qualification

Windows passes 70 selected native/desktop suites and three additional source
suites. Linux passes 36 core suites, including 31,046 computed-range checks.
Desktop mapping passes 13,048 assertions. Source and installed Python pass
matching 8,621 cases, including 113 new viewer cases and all four restored
editor variants, with no skips or exclusions. Both the previous 7,799-case
full-host and 3,398-case core inventories are retained. A genuine venv passes
180 installed projection checks; the installed C++ SDK consumer passes. Thirty
owned scratch targets were archived, verified and removed, freeing
3,156,055,180 net bytes. Full Linux desktop qualification is not claimed by
these core gates.

Native computed-bound checks use all 240 captured ImageJ renderer cases and
separate cross-plane overrides. Desktop mapping checks use all 80 captured
non-threshold cases, plus source/session and private threshold lanes. The
existing native renderer's threshold palette qualification remains in place;
the editor's owned threshold tint is a separate policy.

Four real-input cases cover mixed IEEE words, all NaNs, all infinities and
finite orthogonal slices sharing an infinite XY range. Each runs in source and
installed C++ and Python contexts. Drivers deliver actual pointer, button,
Ctrl+A and text events against observed widgets. Independent Python assertions
compare source words, RGB values, channel state, stable cache build counts,
manual drafts, projection changes and reopened source data. Raw reports and
sessions remain in `build/p07w-ieee-ui001/observations001` and the C++ input
directories; archive receipts retain scratch evidence.

Additional adapter checks use public workspace file opening and detached IEEE
document save/open routes. The four previously deferred quiet-NaN editor
variants are restored. Their loader calls now capture and assert workspace
extensions explicitly, preserving the newer session contract. C++ capture and
SDK jobs do not consume these Python assertions; the audit records the exact
test-only amendment against their otherwise unchanged source/product pins.

See the [editor review](parity/reviews/ieee-editor-review-001.json) and
`build/p07w-ieee-ui001/ieee-editor-handoff001.json` for counts, product hashes,
retained counterexamples and verified cleanup. No new dependency, Qt component,
non-commercial component or reference Java runtime is incorporated.

## Remaining work

Full IEEE Remove NaNs processing is now qualified through the native kernel,
commands, Python and editor APIs; see [IEEE Remove NaNs](IEEE_REMOVE_NANS.md).
Full-IEEE physical dialog/preview/stack-cancel cases now pass in all four
source/installed C++ and Python contexts. Scientific Float32 TIFF file transport
is the next functional packet. Other IEEE
rank operations retain separate admission requirements. Scientific Float32 I/O,
RGB rank and the broader ImageJ/Fiji/editor/release work remain open.
