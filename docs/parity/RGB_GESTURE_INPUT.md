# Combined RGB input qualification

Native scientific documents now route **Select all**, **Deselect** and **Restore**
through the C++ selection controller. The menu commands require the idle active
owner and exact document-ID fields. They preserve pixel storage, navigation and
pixel Undo history and do not materialize samples. Existing legacy documents keep
their established route. No new workflow record format is introduced for these
three metadata commands.

The combined input test consumes the existing independent ImageJ 1.54r fixture.
It covers all 42 distinct first/second component pairs, three navigation axes,
both directions and manual/Auto endings: 504 unique traces. Its source image,
initial position, camera and floating-panel placement are fixture preparation.
The following operations use actual ImGui pointer and keyboard events in one
creator-owned `HeadlessInputSession` per trace:

- ROI manager New area, ellipse tool and a canvas drag from `(17,0)` to `(236,8)`.
- Select all, Deselect and Restore through Edit/Selection, then a further Deselect
  at the promotion boundary.
- Component radios, entire-stack checkbox, and the minimum/maximum text fields.
- Time/channel input or Ctrl-click slice input for departure and return.
- Rectangle tool and canvas drag at the other plane, using the same bounds.
- Returning Reset, optional Auto, and final Apply.

The ellipse and rectangle reproduce the raster memberships in the existing
reference row `roi=8`, `changed_roi=0`, `other_roi=1`; the latter reference uses a
rectangular packed mask. The complete published stack is exported as observed
UInt8 bytes and compared with the independent fixture's SHA256. Final range/cycle,
full session record, source pixel immutability before Apply, live selection and
navigation preservation, and paired Undo/Redo are checked. A separate Python
reader verifies every exported file, matrix identity, field result and required
control delivery. It performs no product processing. Its 504 checks consume the
same 504 executions and are not additional unique gesture cases.

The test records actual control IDs, rectangles, points and frame ordinals.
Controls are scrolled into view; pointer placement is refreshed when layout
moves. Text entry requires the intended active text ID before sending characters.
Focus retries click only the requested field before typing, and are retained in
the control observations. No expected pixel or numerical value is changed to
accommodate delivery failures.

CTest runs a 12-trace subset under the existing owned-fixture wrapper and its
60-second bound. Explicit full runs execute all 504 traces. The current driver
also supports an optional start offset for reproducing one trace. These are C++
headless ImGui deliveries, not OS input or real ImageJ AWT presentation. The Python
UI shares the same compiled controller; installed Python editor and selection
regressions are qualified separately. Other canvas shapes, combined irregular
masks/non-area gestures, richer visit sequences, real OpenGL presentation,
platform/resource acceptance and the original broad ImageJ/Fiji requirements
remain open.

To reproduce, build target `imagejxx_rgb_combined_input_tests`, then run that
executable with a fresh output directory and trace count `504`. Windows builds
place it under the configuration directory (for example `Release`); its runtime
DLLs must be alongside it or on `PATH`. On Linux, use `xvfb-run -a` when a display
is unavailable. Set `IMAGEJXX_RGB_COMBINED_INPUT_REPORT` to the resulting
`results.json` and run `pytest tests/python/test_rgb_combined_input.py`. The reader
needs pytest and the repository fixture, and does not import the product.

See [the scoped receipt](reviews/p12e-rgb-input001.json) for runtime identities,
retained raw observations, setup failures and final checks. The historical Auto
producer fingerprint failure is preserved and remains unresolved.
