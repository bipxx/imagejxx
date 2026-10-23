# Native scientific selection inversion

Edit → Selection → Invert now works on native scientific documents through the
C++ selection-command service. The optional Python UI exposes the same action:

```python
editor.action("invert_selection")
```

The command requires the idle active document and accepts only an optional
`document_id`. It complements the current area on the XY image grid. Without a
selection it selects the full image. Lines, points and angles are refused before
publication. Disconnected regions and holes are retained as packed masks; an
empty complement stays an explicit empty mask under the native area algebra.
Restore recovers the previous geometry. Native rasterization determines boundary
membership; this increment does not certify every upstream subpixel convention.

Inversion preserves pixels, calibration, C/Z/T navigation, dirty state and pixel
Undo/Redo. Its estimate and the current processing-memory preference are checked
before raster allocation or state replacement. It does not read dataset samples.
When recording is enabled, a changed inversion appends the existing complete
selection-creation command: XOR with the image rectangle, or Replace for an
absent selection. Strict validation, core replay and explicit rebind use the
existing command format. Desktop replay retains its established scientific
creation-context requirement; use ROI Manager New area before replay there.

Public SDK code uses the same service without a desktop dependency:

```cpp
const auto source = document.snapshot();
const auto shape = source.data().dataset.shape();
const imagejxx::SelectionCreationRequest request{
    source.data().selection ? imagejxx::SelectionCreationOperation::Xor
                            : imagejxx::SelectionCreationOperation::Replace,
    imagejxx::Selection("rectangle", {{0, 0}, {double(shape.x), double(shape.y)}})};
const auto command = imagejxx::resolve_selection_creation_command(source, request);
auto edit = imagejxx::prepare_selection_creation(command);
document.commit(edit);
```

Selection-command records now also admit the explicit `preserve_ieee_float32`
source policy. Both nonfinite policies require scalar Float32; replay checks the
actual source policy and never downgrades it. Existing finite/quiet-NaN records
and geometry versions remain compatible. The sample descriptor is metadata:
NaN payloads, infinities, signed zero and subnormals remain in the source storage.
Quiet-NaN/IEEE dataset admission still requires its existing reviewed eager source.

Qualification covers 92 new installed Python checks, all nine native area kinds
plus empty/full masks, six source types/policies, exact recording, replay/rebind,
redo preservation and atomic parameter/owner/budget refusals. The installed SDK
example tests three policies, literal rectangle complements and a throwing lazy
finite provider. A native ImGui input test clicks Invert, Restore, Deselect and
Invert again for six sources, retaining control IDs/rectangles/points. Rendering
can read the displayed plane; the API/SDK checks separately prove no sample IO.

The intended area-XOR/no-selection/non-area behavior was reviewed against
[ImageJ Selection](https://imagej.net/ij/developer/source/ij/plugin/Selection.java.html)
and [Roi.getInverse](https://imagej.net/ij/developer/source/ij/gui/Roi.java.html).
This is not a new pinned ImageJ runtime comparison or OS/OpenGL acceptance.
Legacy-compatible documents retain their earlier route. Grow/shrink and other
selection transformations, the original feature families and the historical Auto
fingerprint gate remain unfinished. See [the scoped receipt](reviews/p12f-selection-invert001.json).
