# CI2: original Apple SDK archive mismatch

State: original failure diagnosed to its comparison boundary; additive
diagnostics locally checked. The byte-level cause and installed SDK acceptance
remain open. The raw-versus-installed archive inequality remains fatal.

Original run40248817628 at `379621f1055c53bbcd4c26e585a5e4527886f4a4`
is terminal. Intel/Python3.10 job121337284246 completed actual wheel installation,
then both SDK component installs exited0 with complete cleanup. Its scientific
SDK stage failed before consumer configure/build/CTest with
`Installed SDK archive differs from the existing wheel build`.

The retained original artifact12620423140 is29,791 bytes, SHA256
`e08427b3d5cfdab3e0904ee973dc193313ff67a72b09686729e13fd44fe69f9f`.
Its nine members contain the failed receipt, install logs/owned processes,
dependency certificate and source-input metadata. The original job log is53,691
bytes, SHA256 `ecb5d2702a6edb93fa2b16e0dcd60d2d70e4dea70321f1a811142d9d7e3f1cd4`.
Both are retained under
`.tmp/manual-workflow-verification/ci2/apple-sdk379-intel310-001/`, with the
artifact API metadata. No raw/installed archive payload is available there.

The original receipt pins the pre-install core archive at15,227,144 bytes,
SHA256 `d174a185fb5b3dd116d167ae255676916b50db566b32e062905c7e8a524ea830`.
The comparison loop stopped on core first. Installed pins were not serialized;
an IO-archive mismatch is not established. Generated install-script bytes and
the actual post-install transformation were also not retained.

[CMake3.31.6 installation source](https://github.com/Kitware/CMake/blob/v3.31.6/Source/cmInstallTargetGenerator.cxx#L797)
emits post-install ranlib for Apple static libraries. That is a supported cause
hypothesis, not proof of the actual changed bytes or permission to accept the
mismatch. Do not normalize or discard archive differences based on this source
fact alone.

The successor collects both installed archive pins before comparing the first
one and records original pre-install cache pins plus mismatch membership. On
mismatch only, it retains exact bounded UTF-8 generated install-script text
against its original pre-install pin. Missing, changed or oversized script
evidence stays explicitly unavailable; the original archive refusal remains
fatal. Matching archives keep the existing two pin operations without a new
script read. No new archive parser, compiler build or equivalence rule is added.

All30 SDK metadata/source controls pass on the existing Windows/Linux runtimes
(one existing Windows symlink skip). Three additive controls cover matching
archives, first-core mismatch with both installed pins and exact CRLF/Unicode
script bytes, and missing/changed/capped source diagnostics. Receipts are
`sdk-install-diagnostic-controls-{windows001,linux002}`. The first Linux setup
did not start a test child because its source mirror lacked the test/example
inputs; its partial projection, original preimage and setup refusal are retained.
Only the missing source files were projected for the successful successor.

Next use the next justified exact-head hosted attempt to retain the missing
installed/source observations, then classify actual archive changes. Do not
rerun an otherwise valid compiler campaign solely to improve these notes, or
claim a repaired Apple SDK gate from local metadata controls.
The [additive progress index](../parity/reviews/g010-history-reconnection001/sdk-install-diagnostic-progress.json)
binds original artifact/log identities and both-host source/control receipts.
