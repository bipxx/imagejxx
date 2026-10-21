# PNM publication

The C++ core `write_pnm`, Python `imwrite`, and desktop pixel saves now share one
streaming atomic writer. It emits binary P5 grayscale or P6 RGB for UInt8/UInt16,
with big-endian 16-bit samples and the existing round-to-nearest/clamp policy.
The filename suffix does not change the sample layout. Read support remains
P2/P3/P5/P6; no additional format or dependency is introduced.

Encoding uses a 64 KiB sample buffer, a small ASCII header and one exclusive
sibling. After encoding, the writer flushes/closes the stage and replaces the
destination once. Failure before publication abandons only the owned stage.
The desktop's predictable `.imagejxx.tmp` intermediate and complete encoded-file
readback are removed. This buffer bound excludes the caller's double-backed
image, desktop canonicalization and operating-system allocations.

Dimensions and pixel-vector length must agree. Empty/NUL paths refuse before
staging. Nonfinite samples refuse without changing an existing target or
publishing a partial new target, including failures after multiple chunks have
already been written. Saving remains synchronous; this API adds no asynchronous
job or cancellation contract.

The retained baseline proves existing-target truncation on late core encoding
failure, desktop fixed-suffix file destruction on success and failure, and NUL
prefix-target damage. Against the previous installed build, the corrected Python
regressions expose seventeen failures. The repaired core and desktop pass thirty-
two new source and installed checks. Native and installed headless C++ SDK checks
cover literal output, rounding/clamping, invalid storage, late NaN/infinity,
occupied/locked destinations, missing parents and stage cleanup. Independent
NumPy byte construction verifies all four installed SDK outputs, and executable
imports exclude TIFF, desktop and Python runtimes.

Current evidence: `build/p06za-pnm-ownership001/pnm-handoff001.json`.
Validation totals are native10 suites, source199/installed219 Python checks,
both installed physical ImGui workflows, API9 and desktop/startup/SDK regressions.
Real disk-full/flush/close fault injection, POSIX ownership hardening, broader PNM
reader resource qualification and matching full release products remain open.
This checkpoint does not certify a whole format, platform or editor family.
