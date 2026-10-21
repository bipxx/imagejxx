# Directional shadow filters

`shadow_imagej` implements ImageJ's eight directional 3x3 shadow presets in C++.
Directions use image X/Y coordinates: `north`, `northeast`, `east`, `southeast`,
`south`, `southwest`, `west`, and `northwest`. Each response retains the center
sample and adds the directional neighborhood response.

```python
import imagejxx as ij

result = ij.shadow_imagej(image, "northeast")
region = ij.shadow_imagej(image, "east", extent=(10, 20, 50, 40))
registered = ij.execute_operation("shadow_imagej", image, {"direction": "east"})
```

```cpp
#include <imagejxx/image.hpp>

auto result = imagejxx::shadow_imagej(source, "northeast");
```

The shared Dear ImGui desktop provides **Process → Shadows → Directional Shadow
(ImageJ)**. Choose a direction, Preview, Cancel or Apply, then use ordinary
Undo/Redo. The C++ and Python desktop hosts use the same native operation and
form. Custom kernel editing remains available through Convolve.

The same submenu also provides **North**, **Northeast**, **East**, **Southeast**,
**South**, **Southwest**, **West**, and **Northwest**. A single ordinary image
runs the selected preset directly as one edit. A legacy stack opens the seeded
form, including **Process all slices**, Preview and Apply; the choice remains
visible before publication. Scientific documents open the native target review
with the direction already captured. All routes reuse `shadow_imagej` and retain
normal Undo/Redo and recording. Unsupported sample layouts and floating policies
disable both the direction entries and the general shadow form.

Scientific documents now use the shared native workflow review. Open the same
Shadows menu, use **Edit this step's parameters** to choose a direction, and
review the target before preparing. Current T/C/Z, all Z at current T/C, and
explicit T/C/Z ranges retain their scientific axes. **Prepare image preview**
and **View prepared image** provide a detached linked XY/XZ/YZ viewer; cancelling
leaves the live image unchanged. Prepare and Apply publish one history
transaction. Recording appends typed direction and frozen ranges while retaining
prior workflow rows. UInt8 gray/RGB, UInt16 gray and finite-policy Float32 gray
are admitted from metadata without reading samples. Other layouts and preserved
nonfinite policies remain guarded.

## Pixel and target behavior

Supported layouts are UInt8 grayscale/RGB, UInt16 grayscale and finite Float32
grayscale. Integer output saturates to the original type. Float32 evaluation
preserves the pinned processor's ordered binary32 arithmetic. A nonfinite result
fails before publication. Scalar image borders replicate edge samples; RGB
retains the image's outer border. The pinned processor fails for some one-column
RGB extents; the native API rejects those extents with a controlled error.

An explicit extent is `(x, y, width, height)` within the image. Pixels outside it
remain unchanged, and the kernel reads neighboring source pixels across its
boundary. Empty extents are no-ops. Registered dataset execution bounds computation
to the write mask's bounding rectangle, then applies the mask. This prevents an
overflow in an unrelated region from rejecting a selected-area edit. Dataset
targets retain unselected C/Z/T positions; preview and cancellation leave the
source unchanged, and a changed publication creates one history transaction.

## Reference and remaining work

[The commercial-use review](parity/shadows-commercial-source-review-001.json)
pins the public-domain ImageJ source used as a behavioral reference. The owned
driver captures 1,920 cases across eight directions, four processor layouts,
ten shapes, three signals, and full/rectangular extents. Fixtures include the
upstream failure cases. Java sources, classes and the JDK stay in private build
directories; the product has no new dependency.

With the reviewed reference archives and JDK cache available, generate a separate
comparison corpus without replacing the committed fixture:

```powershell
python scripts/capture_shadows_reference.py --output build/shadows-reference --fixture-output build/shadows-reference.json
```

Remaining family work includes the animated Shadows Demo, IEEE nonfinite source policies,
broader scientific irregular-ROI/display and positioned-ROI contexts, and
platform/accessibility qualification.
The complete ImageJ/Fiji objective remains tracked in [the gap plan](PLAN.md).
