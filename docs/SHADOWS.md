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

Remaining family work includes the animated Shadows Demo, direct direction menu
shortcuts, scientific desktop integration, IEEE nonfinite source policies,
broader irregular-ROI/display contexts, and platform/accessibility qualification.
The complete ImageJ/Fiji objective remains tracked in [the gap plan](PLAN.md).
