# imagejxx

Native C++17 image editing and scientific processing with a NumPy Python interface.
Includes a standalone **Dear ImGui C++ desktop** and an optional **Python interface
to the same native UI**. Both expose 92 native image operations and broad
ImageJ-style desktop workflows. [Desktop coverage and remaining parity work](docs/DESKTOP.md).
The [AVI movie exporter](docs/DESKTOP.md#avi-movie-export) adds native grayscale/RGB streaming,
Python callbacks/cancellation and shared ImGui range/timing controls.
The [editor completeness audit](docs/DESKTOP.md#editor-completeness-audit-2026-10-09)
tracks configuration, selection editing, slicing, linked XY/XZ/YZ views,
C/Z/T hyperstacks, 3D viewing and the broader Fiji workflow backlog.
The [directional shadow guide](docs/SHADOWS.md) covers the eight native presets,
shared desktop controls, supported pixel layouts and remaining family work.
The [three-agent execution plan](docs/THREE_AGENT_EXECUTION.md) and
[next detailed packet](docs/packets/P13h-interactive-path-authoring.md) define the
current implementation queue, file ownership, behavior and acceptance checks.
The [gap closure plan](docs/PLAN.md) assigns all 99 audited capability areas to
milestones, dependencies and acceptance checks. Its [current gap summary](docs/PLAN.md#current-gap-summary)
prioritizes configuration, editable selections, filtering and linked XYZ/slicing
while retaining the complete commercially eligible ImageJ/Fiji scope.
See [implementation progress](docs/IMPLEMENTATION.md) for the new axis-aware
dataset API, linked XYZ views and targeted Python document control.
The [physical in-plane spacing guide](docs/packets/P05i-physical-inplane-spacing.md)
describes calibrated XZ/YZ sampling and the shared desktop form. Scoped owned
scientific and four-interface evidence is adopted and the Source008 archive is qualified.
The [ImageJ bilinear reslicing packet](docs/packets/P05j-slicer-bilinear-resize.md)
adds a separate finite scalar sampling path, with native geometry and ImageJ scalar
spacing reported separately. Its scoped native, installed API/SDK and paired ImGui tests pass. Formal evidence
adoption and the broader editor/Fiji scope remain required.
The [manual threshold display packet](docs/packets/P05k-manual-threshold-display.md)
adds raw-intensity histogram/range controls, a nondestructive overlay,
threshold-to-selection and captured-plane Apply with native persistence/replay.
Its card records the scoped runtime and archive checkpoint and broader remaining work.
The qualified [scientific session packet](docs/packets/P06a-scientific-session-codec.md)
adds one native lossless TCZYXS codec and Python API. See the
[session format and compatibility contract](docs/SESSION_FORMAT.md) for axes,
storage, limits, metadata and legacy-reader behavior. Both shared desktop launches use this codec; the packet records scoped
verification. The [scientific navigation packet](docs/packets/P06b-scientific-navigation.md)
describes scientific C/T navigation and guarded viewing in both launches. The [execution guide](docs/SOL_ULTRA.md#resume-card) links current implementation and qualification.
The [native ROI rotation and properties increment](docs/IMPLEMENTATION.md#native-roi-rotation-and-selection-properties)
adds editable rotated shapes, typed selection drafts and shared ImGui handles.
Implements a broad set of core ImageJ workflows and selected Fiji processing algorithms.
Native processing kernels are self-contained C++; preferences persistence uses
a private, pinned MIT JSON parser. Python uses NumPy at runtime
and pybind11 for bindings; no Java runtime is required.

## Install

Python 3.10 or newer and a C++17 compiler are required to build from source.
Install Visual Studio's **Desktop development with C++** workload on Windows,
Xcode command-line tools on macOS, or GCC/Clang on Linux. CMake is supplied by
the build backend when needed.

```sh
python -m pip install .
python -m pip install '.[io]'       # PNG, JPEG, BMP, TIFF and other codecs
```

Build the **C++ desktop without Python** with the reviewed desktop dependency
prefix described in [desktop setup](docs/DESKTOP.md):

```sh
cmake -S . -B build/imgui -DIMAGEJXX_BUILD_PYTHON=OFF -DIMAGEJXX_BUILD_CPP_UI=ON -DCMAKE_PREFIX_PATH=/path/to/imgui-sdk
cmake --build build/imgui --config Release
```

The executable is `build/imgui/imagejxx` (or `build/imgui/Release/imagejxx.exe`
on Windows). In this workspace, `powershell -File scripts/run_cpp_ui.ps1` starts
the built application. Use `-Build -DependencyPrefix C:\path\to\imgui-sdk` to rebuild.
Windows builds copy the reviewed runtime DLLs beside the executable.

Build the **optional Python UI** into a wheel using the same dependencies:

Dear ImGui itself is built from the pinned sources in `third_party/imgui`, with
32-bit character input configured consistently in both hosts and backends. The
dependency prefix supplies the windowing/codecs; an external ImGui binary is not
used. The same source and MIT notice are included in source distributions.

```sh
python -m pip install . -Ccmake.define.IMAGEJXX_BUILD_PYTHON_UI=ON -Ccmake.define.CMAKE_PREFIX_PATH=/path/to/imgui-sdk
python -m imagejxx.ui               # Or: imagejxx-ui
```

The UI-enabled wheel contains `_desktop`; processing-only wheels omit it.
The `gui` extra adds no Python dependencies and does not enable CMake features.
UI rendering, tools and editing run in C++; Python can launch the UI with arrays
and access document snapshots. Both UI build options default to OFF. Dear ImGui
and the windowing/codec dependencies permit commercial use; notices are retained
in [the dependency inventory](licenses/desktop-dependencies.json).

On Windows, select the Visual Studio generator if building outside its developer shell:

```powershell
$env:CMAKE_GENERATOR = 'Visual Studio 17 2022'
python -m pip install '.[io]'
```

## Python

```python
import numpy as np
import imagejxx as ij

pixels = np.zeros((64, 64), dtype=np.uint8)
pixels[15:30, 20:35] = 200

smoothed = ij.gaussian_blur(pixels, sigma=1)
mask = ij.auto_mask(smoothed, method="otsu")
mask = ij.fill_holes(mask)
objects = ij.analyze_particles(mask, intensity=pixels, min_area=20)
print(objects[0]["area"], objects[0]["centroid"])

roi = ij.rectangle_mask(pixels.shape, x=10, y=10, width=30, height=30)
editor = ij.Editor(pixels).select(roi).apply(ij.add, 10)
edited = editor.pixels
editor.undo().redo()
```

Images have shape `(H,W)` or `(H,W,3/4)` and dtype `uint8`, `uint16`, `float32`
or `float64`. Stack APIs explicitly accept `(Z,H,W)` or `(Z,H,W,3/4)`. Masks
also accept Boolean arrays. All functions return independent arrays. Native
processing releases the Python GIL. Integer outputs clamp and round to the
nearest integer; use `ij.convert(image, np.float64)` or the calculator's
`dtype=` argument for signed or high-dynamic-range arithmetic.

```python
image = ij.imread("input.tif")       # Optional io extra
result = ij.clahe(image, tile_size=64, clip_limit=2)
ij.imwrite("output.tif", result)

stack = np.stack([pixels, pixels, pixels])
volume = ij.gaussian_blur3d(stack, sigma=(1, 1, 1))
projection = ij.project(volume, method="maximum")
```

With a UI-enabled build, launch the shared ImGui editor from Python:

```python
from imagejxx.ui import launch
window = launch([pixels])
edited_documents = window.documents  # Detached snapshots after closing the UI
```

See [feature coverage](docs/FEATURES.md), [numeric conventions](docs/COMPATIBILITY.md),
and [the public Python functions](src/imagejxx/__init__.py). Examples are in `examples/`.

The compiled operation registry is also available without the UI:

```python
ij.validate_operation_record("add", {"value": 10})
result = ij.execute_operation("add", pixels, {"value": 10})
spec = ij.operation_spec("add")
```

The registry currently describes 86 XY-plane operations. Configurable grayscale
conversion and uint16 inversion consume shared preferences and capture effective
parameters; see [conversion](docs/IMPLEMENTATION.md#conversion-options-and-preferences).
Binary erosion,
dilation, opening and closing have explicit foreground, iteration, neighbor-count
and edge-padding options, shared preferences and native ImGui controls; see
[binary processing](docs/IMPLEMENTATION.md#binary-morphology-options-and-preferences).
Resolved C/Z/T and ROI
targets, isolated preparation and atomic publication are available through the
headless C++ and Python APIs; see [shared dataset execution](docs/IMPLEMENTATION.md#shared-dataset-execution).
The [shared native job service](docs/IMPLEMENTATION.md#shared-native-jobs)
provides asynchronous preparation, caller-thread publication, cancellation,
worker limits and workspace reservations. Both ImGui launch modes include
**Window > Jobs and log**. Canceled queued tasks release their captured data;
preview keys are document-scoped and filter reservations include large
parameter-dependent buffers. True volume methods, complete geometry effects,
full kernel/reader cancellation and managed memory accounting remain in the
[implementation plan](docs/PLAN.md).

## C++

Optional calibrated UInt8, UInt16, Float32 and RGB Dataset TIFF interchange,
including [Deflate](docs/SCIENTIFIC_DEFLATE_TIFF.md) and
[PackBits](docs/SCIENTIFIC_PACKBITS_TIFF.md) and [LZW](docs/SCIENTIFIC_LZW_TIFF.md)
lossless compression [uncompressed tiles](docs/SCIENTIFIC_TILED_LAYOUTS.md) and
[Deflate tiles](docs/SCIENTIFIC_DEFLATE_TILES.md),
is available through the
headless `imagejxx::io` target and `imagejxx.scientific_io` Python module. Build with
`IMAGEJXX_BUILD_IO=ON`; Python additionally requires `IMAGEJXX_BUILD_PYTHON=ON`.
See [scientific TIFF IO](docs/SCIENTIFIC_TIFF_SERVICE.md) for the admitted profile,
typed API, limits and installation. Both ImGui launches provide **Open scientific
TIFF...** and **Export scientific TIFF...**, with metadata review, resource limits,
explicit loss/replacement consent and guarded publication. Desktop builds enable
this service by default; processing-only builds keep it optional. Legacy
`imread`/`imwrite` behavior is unchanged.

```sh
cmake -S . -B build/native -DIMAGEJXX_BUILD_PYTHON=OFF -DBUILD_TESTING=ON
cmake --build build/native --config Release
ctest --test-dir build/native -C Release --output-on-failure
cmake --install build/native --config Release --prefix build/install
```

Consumers can use `find_package(imagejxx CONFIG REQUIRED)` and link
`imagejxx::core`. Public headers include `imagejxx/image.hpp`, `dataset.hpp`,
`document.hpp`, `selection_edit.hpp`, `application.hpp`, `metadata.hpp`, `settings.hpp`, `preferences.hpp`, `operations.hpp`,
`targets.hpp`, `execution.hpp` and `jobs.hpp`. The
[installed SDK examples](examples/cpp/CMakeLists.txt) exercise each interface
without GUI or Python dependencies.

The [settings schema example](examples/cpp/settings_schema.cpp) writes every
native setting definition and its current default as JSON. Build the examples
against an installed SDK, then run `imagejxx_settings_schema_example`; bounds
that do not apply to a setting are `null`. This exports definitions for tools
and integrations; setting-consumer verification is tracked in the plan.

The headless [preferences example](examples/cpp/preferences.cpp) saves and reopens
versioned settings through the same native codec used by the desktop. It preserves
unknown fields, validates before publication, and supports recovery warnings,
blocked automatic saves and explicit backups. JSON headers stay private; an
installed SDK consumer needs only `imagejxx::core`.

```cpp
#include <imagejxx/preferences.hpp>

auto preferences = imagejxx::PreferencesDocument::load("preferences.json");
preferences.settings.set("binary_iterations", std::int64_t{2});
preferences.save("preferences.json", true); // Explicit save permits recovery backup.
auto reopened = imagejxx::PreferencesDocument::load("preferences.json", false);
auto options = reopened.settings.binary_options();
```

The Python binding exposes the same detached document:

```python
preferences = ij.PreferencesDocument.load("preferences.json")
preferences.settings["binary_iterations"] = 2
preferences.save("preferences.json", explicit_save=True)
reopened = ij.PreferencesDocument.load("preferences.json", recover=False)
options = reopened.settings.binary_options()
```

Native save leaves the detached document's recovery flags intact. The existing
Python UI preferences facade clears its warnings only after successful explicit
publication and retains its compatibility path for opaque JSON values beyond
the native integer/Unicode representation.

```cpp
#include <imagejxx/image.hpp>
imagejxx::Image image(64, 64, 1, imagejxx::PixelType::UInt8);
image.at(30, 30) = 255;
auto blurred = imagejxx::gaussian_blur(image, 1, 1);
auto mask = imagejxx::auto_mask(blurred, "otsu", true);
auto labels = imagejxx::connected_components(mask);
```

Native images own interleaved double-precision working storage; `PixelType`
specifies the format used when exporting to Python or PNM. This trades extra
working memory for one shared algorithm implementation and precise intermediate
values. Native callers must keep the public storage and dimensions consistent.

## Verification

```sh
python -m pip install '.[io,test]'
python -m pytest
python scripts/check_licenses.py --installed --extras io,test
python examples/pipeline.py
```

Tests compare numeric kernels to NumPy/SciPy, exercise segmentation and color
round trips, and cover native/Python I/O, invalid inputs, noncontiguous arrays,
ROI editing, undo and concurrent calls. CI configuration covers Windows,
Linux and macOS; only locally executed checks are reported as completed.

## Commercial use and scope

Project code is MIT licensed. Dependency versions are pinned to the reviewed
set and recorded in [Python](licenses/dependencies.json),
[native](licenses/native-dependencies.json) and
[desktop](licenses/desktop-dependencies.json) dependency inventories.
The build retains pybind11's required notice. Research-only, non-commercial,
and unlicensed third-party plugin code is excluded. The license checker rejects
new dependencies without a reviewed commercial-use record.

[ImageJ's core is public domain](https://imagej.net/software/imagej/).
[Fiji lists component-specific licenses](https://github.com/fiji/fiji/blob/main/LICENSES);
its plugin collection cannot be treated as a single permissive source. The
selected Fiji-style kernels here are independent implementations, with no
copied Fiji plugin source or bundled plugin JARs. GPL permits commercial use;
copyleft and a non-commercial restriction are different conditions.

This package provides a native desktop and an array-processing library. Full
configuration, interactive ROI manipulation, linked orthogonal/3D views, C/Z/T
hyperstacks, macro/plugin compatibility, microscopy and DICOM/FITS metadata,
registration and other Fiji workflows remain migration work. See the
[required editor backlog](docs/DESKTOP.md#editor-completeness-audit-2026-10-09)
and [gap closure plan](docs/PLAN.md) for current coverage, delivery order and
acceptance criteria. [Pinned reference reconstruction](docs/parity/README.md)
reproduces the partial ImageJ/Fiji command/component inventory. Non-commercial, research-only and
unlicensed third-party components remain excluded.
Numerical equivalence is documented per algorithm; full ImageJ/Fiji pixel parity
has not been established.

Automatic threshold method behavior and histogram APIs: [Thresholding](docs/THRESHOLDING.md).
