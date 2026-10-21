# Reviewed TIFF dependency boundary

The selected Windows x64 release closure is libtiff 4.7.1 with its linked
libjpeg-turbo 3.1.3, liblzma 5.8.2 and external zlib 1.3.1 libraries. Exact source
archive, port/patch, ABI, header, import-library, runtime and notice identities are
recorded in [the IO inventory](../licenses/io-dependencies.json). Source archives
and 24 port members were checked against the installed packages' recorded hashes;
ten selected artifacts also match their own package SPDX records. Six retained
notice files match the selected upstream source archives byte for byte.

The actual DLL imports require `tiff.dll`, `jpeg62.dll`, `liblzma.dll` and
`zlib1.dll`. The aggregate SDK package list omits liblzma and lists two zlib ABIs;
the selected per-package records and actual artifact hashes resolve those entries.
The core's privately prefixed zlib remains independent. TurboJPEG, xz tools/scripts,
JBIG and other optional codecs are outside this closure.

`cmake/ReviewedTiffRuntime.cmake` selects the prefix from the actual `TIFF::TIFF`
release import library. It checks that the headers come from that same prefix,
verifies all selected inputs against `TiffRuntimeManifest.cmake`, and refuses
missing or changed files. If a separate ZLIB target exists, its import-library
identity must agree. The desktop build now runs the checks before compiling and
before copying DLLs. Installation checks the files again before distributing them.
It no longer derives TIFF's closure from the unrelated PNG/ZLIB prefix or silently
skips a missing TIFF codec DLL.

`scripts/check_licenses.py --desktop` also validates the source inventory, retained
notices and generated CMake manifest. To check an installed dependency prefix:

```console
python scripts/check_licenses.py --desktop --tiff-runtime-prefix .tools/imgui-sdk
```

These checks apply to the exact reviewed Windows artifacts. Other binary builds
and platforms need their own delivery evidence. Kernel/UCRT and Microsoft runtime
libraries remain platform prerequisites; they are not bundled by this inventory.

## Linux static closure

The Linux qualification packet now records its own static archive identities,
codec membership, retained notices and installed consumer evidence. It uses
libtiff 4.7.1, libjpeg-turbo 3.1.3, liblzma 5.8.2 and zlib 1.3.1 with the same
17 configured codecs. See [Linux qualification](LINUX_SCIENTIFIC_IO.md) and
[the selected Linux inventory](../licenses/linux-io-dependencies.json). The
Windows fail-closed runtime checks above continue to apply to their exact DLL
profile; they do not enforce arbitrary Linux dependency discovery.

## Core-only SDK and scientific service

The installed package supports `find_package(imagejxx CONFIG REQUIRED COMPONENTS
core)` without finding TIFF. The optional `imagejxx::io` target is now implemented
and exported separately. Required IO discovery loads the reviewed dependency;
optional IO discovery can report unavailable while core discovery still succeeds.
An installed consumer reads and writes calibrated Dataset TIFFs using only its SDK
prefix. See [the service contract and evidence](SCIENTIFIC_TIFF_SERVICE.md).

The existing admitted profile remains
`F.G080.tiff-imagej-u16-uncompressed-ifd-czt-v1`. Its complete raw IFD admission,
C-fast file-page mapping, calibration/default metadata, rational error reporting,
bounded reading, exclusive streaming writer and callbacks/cancellation now have
native implementation and focused runtime evidence. Actual ImageJ reopening and
FileSaver import pass after the narrowly documented metadata amendment. Existing
desktop TIFF still returns a flat Stack; Python's legacy ndarray codec behavior
is unchanged.

Next, expose this native service through Python and both desktop hosts, with the
remaining acceptance and installed UI checks. Preserve core-only discovery and
the checked codec closure. Neither the dependency nor native-service checkpoint
claims TIFF/OME, microscopy-format, family or matching-release completion.

Evidence: `build/p06r-tiff-dependencies001/tiff-dependency-handoff001.json`.
