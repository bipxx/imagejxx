# Startup TIFF review

With scientific IO enabled, explicit TIFF paths supplied to either normal desktop
launcher or Python `Editor(images=[...])` now enter review before any startup owner
is attached. Each TIFF opens the shared asynchronous inspector. Choose **Use
reviewed file axes and calibration**, then **Open**, or separately accept **flat
pixels without file axes or calibration**, then **Import flat pixels**. Unsupported
scientific layouts display their inspection error and require the explicit flat
choice; there is no automatic fallback. The existing UInt16 scientific profile
and generic legacy decoder support are unchanged.

The form identifies the current TIFF and the total TIFF count. Its path and
Browse control are locked to the normalized startup plan. Resource limits remain
editable, with fresh consent after a change. The next TIFF starts with neither
choice accepted. `wait()` waits for work; it never grants consent.

Accepted TIFFs stay detached. Their file identity/change guards survive through
subsequent reviews. Once every TIFF is accepted, the existing startup loader stages
the remaining images and sessions, applies the aggregate session admission check,
revalidates all accepted TIFFs, and uses its original single attachment boundary.
Input order, session tables/extensions, activation and current default/history
policies retain their existing behavior. Startup does not record scientific IO
workflow rows or execute historical session workflows.

Closing the form or Cancel IO discards the entire batch. Canceling a running or
ready native IO job also discards it. A processing-memory budget change invalidates
the batch, including a change between the final flat choice and attachment.
Changed accepted files or a failure loading another input publish no documents or
partial tables and report startup failure. Start empty remains available after
that failure. Files are not automatically persisted or modified by review.

The shared import panel also revalidates an ordinary interactive TIFF after its
worker is ready and before publication. A changed file refuses publication and
requires fresh review. This closes the separate ready-result timing gap.

Current evidence: `build/p06zb-startup-tiff001/startup-handoff001.json`.
Validation covers nineteen startup Python cases plus one shared ready-input guard,
eleven native suites, both installed physical ImGui hosts, both ordinary installed
launch commands, all ten existing startup integration cases, desktop/API regressions
and the installed C++ IO SDK. Totals are source219/installed239 Python checks.

Python's mixed path/array/UI-Document convenience launcher now uses a separate
[reviewed append contract](MIXED_LAUNCH.md). It retains accepted prefixes on
cancel/failure, whereas paths-only startup retains atomic publication.
`open_legacy_file` remains available for explicit synchronous imports.
Whole-process allocation bounds, remaining IO/platform faults, general TIFF/OME,
matching complete release products and the full editor/Fiji plan remain open.
