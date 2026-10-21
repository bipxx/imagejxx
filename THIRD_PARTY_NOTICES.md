# Third-party notices

The original imagejxx C++ and Python code is licensed under MIT (see `LICENSE`).
No ImageJ Java source, Fiji plugin source, JARs or pretrained models are redistributed.
Non-commercial, research-only, unlicensed and unreviewed components are excluded.

The headless native preferences and scientific session codecs privately compile **nlohmann/json 3.12.0**
under MIT. Its complete reviewed modular headers and license are retained in
`third_party/nlohmann_json`, with exact input fingerprints in `SOURCE.json`.
`licenses/native-dependencies.json` records this core scope; the full permission
notice remains in `licenses/nlohmann-json.txt`. The installed C++ SDK exposes no
JSON types and requires no external JSON package.

The headless native scientific session codec privately compiles the unmodified
compression/decompression core of **zlib 1.3.1** under the Zlib license, with
`Z_PREFIX` symbols. Reviewed sources, required internal headers and the original
license are retained in `third_party/zlib`; `SOURCE.json` pins the official archive
and every retained input. `licenses/zlib.txt` preserves the same full notice.
Gzip file API implementations, contrib/minizip and tools are excluded. The
installed headless C++ SDK requires no external zlib package or zlib runtime DLL.

The optional desktop is **Dear ImGui 1.91.9 with docking**, under MIT, including its
GLFW/OpenGL3 backends, embedded default font, and string helpers. **GLFW 3.4** uses
the zlib/libpng license. **nlohmann/json 3.12.0** uses MIT. Selected **stb** headers
(image reader/writer and TrueType rasterizer, reviewed 2024-07-29) use their MIT
alternative. Full notices are retained in `licenses/imgui.txt`, `glfw3.txt`,
`nlohmann-json.txt`, and `stb.txt`. The embedded ProggyClean font is copyright
2004, 2005 Tristan Grimmer (MIT); its full notice is in `proggy-clean.txt`. Both C++ and Python launch modes use this same
native desktop. Qt and PySide are no longer dependencies.

The selected unmodified ImGui 1.91.9-docking sources are retained in
`third_party/imgui`, including the original MIT notice and exact upstream archive
and file fingerprints in `SOURCE.json`. Both desktop hosts and backends compile
these same sources with `IMGUI_USE_WCHAR32`, so supplementary Unicode input uses
one consistent ABI. No external ImGui binary or additional font asset is included.

Native image I/O and interchange use **zlib 1.3.1** (Zlib), **libpng 1.6.50**
(PNG Reference Library License version 2), and optional **libtiff 4.7.1**
(libtiff license). The tested TIFF library enables **libjpeg-turbo 3.1.3**
(BSD/IJG/Zlib) and **liblzma 5.8.2** (0BSD for the library). Only jpeg62 and
liblzma libraries are bundled; TurboJPEG and xz command-line tools/scripts are
excluded. Notices are in the corresponding `licenses/*.txt` files.

`licenses/io-dependencies.json` pins the exact Windows x64 release TIFF closure,
including its source archives, vcpkg port/patch identities, import libraries,
headers, runtime DLLs and retained notices. The selected TIFF runtime requires
`tiff.dll`, `jpeg62.dll`, `liblzma.dll` and `zlib1.dll`; uncompressed input does not
remove those linked dependencies. Build and installation checks refuse changed or
missing reviewed artifacts. The aggregate SDK package list is not authoritative
for the omitted liblzma entry or the two listed zlib ABIs; the selected binaries
and their own package/source identities are recorded explicitly. The core's
privately prefixed zlib remains independent of this optional dynamic closure.
This boundary does not supply a scientific Dataset TIFF service or qualify a
new format. Other platforms and new binary builds need their own delivery review.

This software is based in part on the work of the Independent JPEG Group.
This software includes code from XZ Utils <https://tukaani.org/xz/>.

`licenses/desktop-dependencies.json` records reviewed versions, license sources,
components and redistribution obligations. Windows UI-enabled wheels bundle
GLFW and enabled PNG/TIFF/compression runtime DLLs. C++ app builds copy these
libraries alongside the executable and install their notices. System OpenGL,
platform windowing APIs and compiler runtimes remain external. Other-platform
native libraries remain external and must accompany a distribution with their
applicable notices. Local system fonts may be used for rendering; no system
font files are redistributed.

The Python extension uses **pybind11 3.0.4**, copyright Wenzel Jakob and
contributors, under BSD-3-Clause. Its full notice is retained in
`licenses/pybind11.txt`. **NumPy 2.2.6** is an external BSD-3-Clause dependency
with additional notices in its platform wheels. Keep those notices when
redistributing NumPy. The processing core does not link to OpenBLAS or FFTW.

Optional Python codecs **Pillow 12.2.0** (MIT-CMU) and **tifffile 2024.12.12**
(BSD-3-Clause) retain their own distribution notices. They are not required by
the native ImGui UI. Additional imagecodecs backends are not declared or installed.

Build/test tools and their reviewed versions are recorded in
`licenses/dependencies.json`, including scikit-build-core (Apache-2.0), pytest
(MIT), SciPy (BSD-3-Clause), packaging (Apache-2.0 OR BSD-2-Clause), and pathspec
(MPL-2.0, build only). Runtime notices must follow the actual components shipped;
the inventory does not audit every Fiji plugin or grant patent clearance.
