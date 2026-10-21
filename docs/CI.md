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

## Python, scientific IO and desktop integration remains open

The complete Python suite remains enabled. Its six operating-system/Python
combinations are separate from the native jobs. Their green status must not be
inferred from a headless-native result or from earlier local qualification.

The initial public run revealed that the default wheel builds the processing
extension only, while test collection includes scientific IO and desktop modules.
Installing the `io` Python extra supplies Python libraries; it does not enable
`IMAGEJXX_BUILD_IO` or build `_scientific_io` or `_desktop`.

Full hosted acceptance still requires the following work:

1. Build the reviewed TIFF/codec and desktop dependency configurations, retaining
   their license notices. Windows scientific IO currently enforces exact reviewed
   binary fingerprints; a different compiler build cannot bypass that boundary.
2. Build both desktop hosts and scientific Python bindings with those prefixes.
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
