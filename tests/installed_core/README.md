# Installed core SDK consumer

This standalone project links only to the installed `imagejxx::core` target.
It does not add the producer project or its private include directories.

Generate a fresh carrier, then configure this fixture in explicitly owned test
scratch. Use absolute paths and forward slashes for CMake arguments on Windows.
Windows builds require the existing x64 MSVC developer shell.

```text
python scripts/calibration_sdk_profiles.py <scratch>/profiles.txt
cmake -S tests/installed_core -B <scratch>/consumer -DIMAGEJXX_SDK_ROOT=<installed-sdk> -DIMAGEJXX_SDK_PROFILE_CARRIER=<absolute-carrier> -DCMAKE_BUILD_TYPE=Release
cmake --build <scratch>/consumer --config Release
ctest --test-dir <scratch>/consumer -C Release --output-on-failure
```

The three clients exercise the public core API, prepared calibration/projection
publication, and pinned plot profiles. The profile client checks 128 exact table
profiles and 43 fitted-plot profiles on both integer types. Fitted values and
limits use `3e-6` relative and `1e-6` absolute tolerances; table coordinates and
sampled IEEE words are exact. Reference coefficients isolate plot behavior from
optimizer differences.

Reuse the product compiler cache and installed SDK. This consumer configuration
is transient test scratch; after terminal process proof, retain concise results,
hashes and necessary compressed diagnostics, then remove its generated files.
These checks qualify the installed core contract, with separate IO, Python/UI,
other-platform and performance gates.
