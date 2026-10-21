# Continuous integration

The workflow has independent native and Python jobs. A failed wheel build must
not suppress native C++ or installed-SDK validation. Every command runs in its
own step or through a checked subprocess, so Windows cannot continue past a
failed build and report a later, misleading import failure.

## Complete headless native profile

Run from the repository with CMake, CTest and a C/C++ compiler installed:

```console
python scripts/ci_native.py --output build/ci-native
```

The output directory must be fresh. The runner configures Release with testing
enabled and Python, scientific IO and desktop disabled, builds every configured
target and runs every registered CTest case. It installs the package into that
output directory, builds all examples in `examples/cpp` against the installed
package, and runs their registered tests. An empty test suite is an error.

CMake discovers the installed toolchain. The workflow does not force a Visual
Studio generation that may be absent from a hosted Windows image. The job runs
once per operating system; the core and SDK do not depend on Python's ABI.

The runner retains `receipt.json`, command logs and native/SDK JUnit reports.
Failure receipts preserve the failing command and exit status. The workflow
uploads these small reports; it does not upload build caches or install trees.

## Linux scientific IO and desktop dependency build

The Linux feature jobs use a source-built prefix from:

```console
python scripts/ci_linux_dependencies.py --output build/ci-linux-dependencies
```

This builder requires Python 3.12+, CMake, Ninja, NASM and system X11/OpenGL
development prerequisites. The workflow installs those platform prerequisites
separately. It verifies every source archive's size and SHA-256 against
`licenses/ci-linux-source-dependencies.json`, builds the selected static codecs
and windowing libraries, installs the pinned stb headers and vendored JSON
headers, and retains all selected license notices. It rejects an existing output
prefix and tampered cached archives. Before publishing, it configures, links and
runs a TIFF consumer through each supported CMake config name, including the
`TiffConfig.cmake` entry point selected by newer FindTIFF modules. Each entry
point provides the reviewed static codec imports. No earlier SDK or local qualification folder
is an input. The optional NASM/header-path arguments select platform build tools
and headers; they do not replace any selected codec or desktop library source.

The dependency artifact is shared by the Python 3.10 and 3.12 Linux jobs. These
jobs explicitly enable scientific IO, the C++ application, Python desktop bindings
and test-provider builds. The headless native job remains independent.

`scripts/ci_python_suite.py` checks that all three installed extensions, the
installed C++ desktop and the built test-only source provider are present, then
runs the complete Python suite under software OpenGL. Its receipt records module
identities and named environment contracts. Modules must come from the installed
package. The Linux codec audit rejects exposed static codec APIs that could bind
to a different library already loaded by Python. Missing bindings or providers,
failures, collection errors and skipped cases leave the complete profile failing.
The test-only provider also measures `sizeof(Image)` and
`sizeof(shared_ptr<const Image>)` using the build's compiler and public headers.
The runner records these measurements and supplies both axis-range memory-check
contracts. These values come from the independent fixture, rather than from the
operation's working-set estimate; the readers retain their exact byte formulas.
The runner first invokes `scripts/ci_observations.py` against the installed C++
application. Its 1,137-launch matrix captures rank filters, rounded creation
and saved selections, ROI import/export, saved-selection budget behavior, and
resize/rotate/move modifiers and directional shadows. The planar selection matrix
covers 621 creation/edit/recovery cases across XY, XZ and YZ, including every
resize handle and modifier combination, rotated profiles, invalid numeric edits
and source/context changes. It supplies `IMAGEJXX_PLANAR_MATRIX_CPP_ROOT` to the
existing independent readers. Each planar case runs in its own application
process and preferences directory. This preserves the previous 95 captures.
The additional scientific/workflow groups supply 96 finite edge BigTIFF cases,
four IEEE Float32 viewing cases, workflow execution and workflow batch. BigTIFF
inputs must match the committed manifest's byte size and SHA-256 before launch;
reader paths retain the expected `capture001` subdirectory. The IEEE observer's
legacy `report.json` and `passed` marker remain unchanged: that marker indicates
probe completion, and the Python readers establish behavior independently.
Workflow collision checks retain case-alias coverage on Windows and use a real
lexical path alias on case-sensitive hosts.
A further 146 captures supply all 18 existing classic-TIFF UI reader modules:
byte/RGB, Deflate/PackBits/LZW, horizontal/floating prediction, tiled storage and
Float32 finite/quiet-NaN/IEEE failure and recovery. They retain each reader's
flat or nested paths. Input hashes are mandatory; manifest byte counts are also
enforced when present, and captures refuse fixture files over 64 MiB. Older
SHA-only manifests record the measured size without changing the committed
fixture metadata. Fifteen standard-library runner checks cover both manifest
schemas, corruption refusal, Float32 argument/transport ordering and legacy editor
envelope preservation/refusal, and complete path-only index membership, identities
and corruption refusal. The native
workflow jobs run these checks on every operating system before building C++.
One aggregate launch now supplies all eight existing persistent-renderer cases
to `IMAGEJXX_SESSION_CPP_REPORT`. The runner checks ordered complete membership,
completion and host identity, then preserves the raw report and artifacts.
The reader uses the original capture root because each case binds its absolute
directory; byte-identical transport copies are retained for archival. Actual
session lifetime, input, failure recovery and resource behavior remain assertions
of the independent reader. S07's deliberate initialization failure has an empty
initial lifecycle list and a separately observed recovery.
Thirty-four playback captures additionally exercise ordinary Z and scientific
Z/C/T controls, live FPS changes, singleton refusal, source/edit/busy guards,
RGB samples and literal IEEE Float32 transport. Independent readers check
positions, linked plane bindings and exact source words through both hosts.
The seven range additions cover inclusive striding, reverse and ping-pong,
off-grid entry, C/T ranges and atomic malformed-range refusal. The options form
uses actual text/choice/OK gestures. Separate physical OK checks preserve
the draft and both documents when its source, metadata or axis changes.
Eight calibrated-time additions exercise the actual Timing selector, equivalent
s/ms/us intervals, strided T playback, FPS independence, atomic invalid timing
and unknown-unit refusal, and stopping after a source edit. Independent readers
compare literal positions and monotonic tick intervals while checking retained
source samples/calibration and linked plane bindings.
Six ordinary-Z timing captures cover forward, stride, reverse, ping-pong,
metadata-edit stopping and unknown-unit refusal. Both hosts use actual timing
and direction controls; independent readers check literal UInt16 samples and
position traces. Public Python API checks additionally reject calibrated C and
scientific Z across singleton/multiple T and C shapes.
The single-ROI export observer polls active file workers every 20 ms while
retaining its 550-frame cap and 10-second worker deadline. It records actual
poll counts and elapsed waits. Both ordinary and rounded export readers verify
those bounds alongside the existing wire, replay, overwrite and no-pixel-read
assertions. This avoids exhausting the frame budget with redundant polls.
Eight scientific shadow captures additionally check the actual direction editor,
private linked preview, all-Z Apply, retained workflow rows and Undo/Redo through
the same C++ host consumed by the independent Python tests.
A further 29 preset cases cover all eight direct entries on single images,
legacy stacks and scientific datasets, four disabled-source cases and visible
atomic refusal of a direct asynchronous Float32 overflow.
Twelve Shadows Demo cases cover complete playback, interruption, source changes,
resource-limit refusal, overflow and unsupported images.
Twenty existing single-report editors and scientific manual-threshold T10 now
feed their independent readers: scientific processing/recording, RGB/indexed/color
conversion, bare edits, ROI import/export and worker controls, planar/display
annotations, scientific ROI management, Z subsets and slicing/resizing/spacing.
RGB and scientific-process probes predate the common report envelope. Their raw
reports remain unchanged; the runner checks their specific completion fields and
records the legacy format without inventing an acceptance claim. Other probes
still require an explicit observed state and false acceptance claim.
Planar tools record changed annotation edits only: no-op scope/vertex/box updates
leave revisions, history and workflow unchanged. Their replay probe uses the
last recorded changed update, checks its stale-revision refusal, and acknowledges
the error through the actual dialog. The large ROI import-limit observer polls workers
every 20 ms, retaining its 30-second worker deadline and 2,500-frame cap.
Four path-only indices additionally supply the existing Auto Contrast and
channel-display readers: eight Auto cases, 18 display cases, ten Max projection
cases and ten Min projection cases. Each index carries only ordered case IDs,
relative report paths, byte counts and SHA-256 hashes. Raw report identities must
match the requested contract/case/host, copied reports must still match their
captured hashes, and every case must finish before the index is published. An
incomplete group fails capture. Index elapsed time measures the actual group
capture and copy work and retains the readers' 60-second bound. Reports and
associated files remain byte-identical; no inline frames or pixels enter an index.
Indexed entries point to original capture directories, preserving the absolute
paths typed during the observed file dialogs. Reader copies remain in the archive;
reports and raw input events are never rewritten to match a copy location.
Two further indices cover eleven scientific Gaussian target cases and ten
scientific slicing cases. They retain frozen T/C/Z targets, masks and positioned
ROIs, committed-data exports beside pending previews, derived-image persistence,
replay, explicit rebind and cancellation. Current scientific Gaussian/threshold
and oriented/spacing/bilinear forms are observed as supported controls; legacy
pixel-edit guards remain. Cancellation checks retain document/history/recording
invariants. Session readers check the exact workspace active-index extension
alongside retained opaque extensions. Invalid orientation ranges are refused by
the disabled Apply control before publication; stale/busy/closed-source checks
retain their separate error paths and numerical/affine oracles.
An additional scientific draft case checks partial First/Last/Step text across
plane changes and explicit correction. A new plane resets a valid ordinary
orientation range to its full extent and step one, retaining orientation flags.
Partial/invalid text and explicit rebind drafts remain available for correction.
The complete-suite runner supplies hashes of the installed desktop extension and
Python UI adapter, alongside the existing installed-executable identity.
Each process gets private preferences. Byte-identical copies supply the readers
with stable group/case paths; original artifacts remain available for absolute
paths embedded in reports. The executable hash must remain unchanged. Reports
are observations with no acceptance claim; independent Python tests check them.

The workflow retains the capture receipt and a compressed archive containing
originals, reader copies, preferences and process logs, including failed attempts.
Other C++ observation contracts remain to be provisioned; the complete Python
suite stays enabled and failures or skips still fail the job.

The Linux source profile does not admit different Windows TIFF binaries or
replace the Windows runtime fingerprint boundary.

The reviewed prefix is supplied with the `CMAKE_PREFIX_PATH` environment variable,
so scikit-build-core retains its own cache search paths for isolated pybind11.
Linux Python extensions hide symbols from the selected static codec/windowing
archives, while leaving the C++ core archive outside that rule. This keeps a
loaded host zlib or image library from replacing the selected static copy.
The native interposition regression includes an unprotected control, and an
isolated wheel probe verifies the actual TIFF, PNG and zlib runtime versions.

## Complete Python integration remains open

The complete Python suite remains enabled for all six operating-system/Python
combinations. Linux has the explicit feature build above; Windows and macOS still
need their reviewed feature profiles. Python acceptance must not be inferred from
a headless-native result or from earlier local qualification.

The initial public run revealed that the default wheel builds the processing
extension only, while test collection includes scientific IO and desktop modules.
Installing the `io` Python extra supplies Python libraries; it does not enable
`IMAGEJXX_BUILD_IO` or build `_scientific_io` or `_desktop`.

Full hosted acceptance still requires the following work:

1. Qualify the Linux dependency/feature jobs on hosted machines and provision
   corresponding reviewed Windows/macOS configurations. Windows scientific IO
   enforces exact reviewed binary fingerprints; a different compiler build cannot
   bypass that boundary.
2. Verify both desktop hosts and scientific Python bindings with those prefixes.
3. Produce fresh C++ UI observation reports and test-provider modules required
   by Python tests. Local `build/` receipts are not repository inputs and cannot
   be assumed to exist on hosted machines.
4. Test source and installed surfaces, report skips and missing fixture contracts
   explicitly, and retain independent SDK and product checks. Do not turn a
   missing UI/IO dependency into an accepted or silently excluded test.
5. Verify the supported macOS feature profiles independently. Local Windows/Linux
   results do not establish macOS acceptance.

The feature inventory and remaining ImageJ/Fiji requirements stay in
[the gap plan](PLAN.md). CI infrastructure changes do not close capability families.
