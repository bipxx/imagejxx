# RGB snapshot and navigation implementation plan

Status: open; reference and installed-editor gap evidence captured on 2026-10-22.
This is part of G057, not a change to the original ImageJ/Fiji scope or acceptance.

The native session value plan and evaluator are now implemented in
`rgb_contrast_session.hpp` with installed Python bindings. They compose frozen
visit stages against the immutable original and prepare one paired transaction.
The 378 reference cases are consumed at every numerical checkpoint; the 5,292
single-visit stack cases also exercise final narrowed stack mapping. Windows/Linux each pass 39,604 installed Python cases, 22 native suites,
existing standalone UI probes and installed SDK consumers. A final facade-only
export correction is separately verified on both hosts; see
[the scoped receipt](reviews/p11t-rgb-session-core001.json). Main-panel transition retention and dedicated saved-stage authoring
remain open; the six-case native editor gap probe has not been made to pass by
this detached service alone. Combined upstream promotion/navigation/final stack
traces and actual transition widgets remain required for full acceptance.

The next dependency is now implemented as a detached captured-session API:
`rgb_contrast_session_command.hpp` and `rgb_contrast_session_job.hpp` retain the
entire visit plan, validate an independent closed V1, provide strict replay and
explicit source rebinding, and prepare/publish through the common leased Apply
job. Source rebinding retains saved absolute visit coordinates and selections;
it validates the complete original before checking destination compatibility.
It does not infer historical membership from the destination's current selection
or navigation. Windows/Linux each pass 45,319 installed Python cases and the native/SDK/UI
regression scope in [the capture/job receipt](reviews/p11u-rgb-session-command001.json). Main ImGui transition integration remains open; this API alone does not make the
six-case editor navigation probe pass.

Portable session workflow/Batch and shared Run review now use the complete
history through `rgb_contrast_session_workflow.hpp`. A singleton WholePlane
Positions target chooses the destination for the final visit. Every saved T/C/Z
position receives the same checked offset; order, repeats and historical masks
remain intact. The original validates against its own descriptor before shifts
are checked against the destination. Current destination selections are not
substituted. Metadata-only membership preflight, empty no-IO/no-history behavior,
paired all-revision publication and existing container versions are retained.
The review inspects saved visits/mappings and authors the final anchor; saved
mapping editing and independent per-visit relocation remain open. Qualification
is running. Main brightness/contrast transitions are the next integration gate.

## Observed behavior

`RgbContrastNavigationReference.java` invokes actual ImageJ 1.54r
`ImagePlus.setPosition`, `ContrastAdjuster.setup`, mapping, masking and Reset.
It directly subclasses ContrastAdjuster; the older histogram harness's Reset
override is **not** inherited. Plot, label and scrollbar presentation is bypassed;
AWT event delivery is not qualified. No upstream implementation is patched.

The 378 reference rows span T/C/Z navigation, both directions, seven component
masks and nine selection forms. Each visit changes the selection to the next
form, replaces a map, resets, remaps, visits another plane, returns, resets and
remaps again. Hashes use native TCZYXS order, independently of ImageJ's CZT stack
storage. The driver checks these invariants before emitting any row:

- Changing selection on the same processor retains the snapshot object. Reset
  still restores that visit's baseline; range edits replace that visit's map.
- Leaving A retains A's visible adjusted pixels. The first visit to B begins
  with B's original pixels, a new snapshot and range 0..255.
- Leaving B retains its visible adjusted pixels. Returning to A takes A's
  already-adjusted pixels as the new baseline, again with range 0..255.
- Reset on returning A restores the returning-visit baseline. It does not erase
  A's earlier visit or change B. A new map starts from that returning baseline.

Two fresh compilations/runs reproduce the fixture byte for byte. The fixture
pins driver sources, relevant ImageJ source/classes and Java/Javac executables.

The installed shared native editor probe covers six All-component cases with
none-to-rectangle selection, every axis and both directions. It matches 36/48
checks: first maps, selection replacement, selection Reset, first B visits, B
maps and unchanged source pixels. All twelve returning Reset/remap checks
disagree. Exit 1 from this probe is a demonstrated gap, not passing acceptance.
The full 378 rows have not yet been run through the native implementation.

## Required model

Keep source pixels immutable until Apply, while making preview and eventual
Apply reproduce the observed per-plane visit history. A single active-plane
stack prefix cannot describe this: earlier visits to other planes must survive.
Likewise, preserving only endpoints or storing one final map per plane loses
snapshot promotion and selection semantics.

Use a native ordered RGB session with frozen per-visit baselines and a replaceable
current draft. Completed visits retain their effective mappings; selection and
component boundaries retain exactly the membership captured at that boundary.
Each visit starts from the previous logical output for its plane. Logical output
may be represented by admitted immutable events rather than duplicate datasets.
Current selection changes do not create a new baseline. Reset discards only the
unpromoted current draft, preserving prior visits and component promotions.

The original source, all effective events, and the last current-plane/entire-stack
Apply policy must form one complete replayable operation. Do not publish a raw
stack command resolved against an intermediate dataset as though its source were
the untouched original. Do not change the meaning of existing V1 scalar, uniform
RGB or stack records. Use a separate closed session profile with explicit version,
limits and validation when existing complete workflow composition cannot express
the required snapshot/selection boundaries without ambiguity.

## Execution order and review gates

1. **Native session state and exact evaluation.** Implement the ordered visit,
   current draft, component promotion, selection boundary and Reset model in
   C++, independent of ImGui. Reuse the exact active ImageJ mapping/mask helpers;
   apply noncurrent stack narrowing only at the final stack Apply boundary.
   Match every plane/checkpoint from all 378 reference rows, not only active
   screenshots. Add promotion-plus-navigation, arbitrary visit order, final stack
   Apply and non-area-selection reference traces before claiming those cases.
   Keep the previously qualified single-plane and entire-stack services intact.

2. **Admission, capture and common jobs.** Before any pixel acquisition or
   frontend state mutation, account for frozen selection geometry, event metadata,
   masks, per-plane staging and results cumulatively. Reject overflow, record
   growth and unsupported types without losing the existing intent. Evaluate on
   workers with cooperative cancellation and progress. Retain resource leases
   through escaped snapshots and publish one original-owner/revision-guarded
   paired transaction using the common Apply job. Test cancellation while blocked,
   empty membership, stale selection/pixels/display, close and shutdown. Expose
   the immutable service/capture/job through the installed C++ SDK and Python.

3. **Complete portable replay.** Capture all effective historical visits and
   their selections, not only the final active stage. Validate the whole original
   before strict replay or explicit rebinding. Specify which saved coordinates
   and selections a destination adopts; reject unused overrides. Cover ordered
   workflow execution, sample-free preflight, Run review and session Batch with
   one guarded final publication. Test JSON round trips, corrupt late events,
   owner/shape mismatches, portable destination rebinding and complete recording.

4. **Shared ImGui transition handling.** Capture the outgoing draft before
   `ContrastPanel::tick` overwrites `source_` or resets its range. Adopt the new
   live revision only after preserving the outgoing visit. Navigation is not a
   pixel edit and must not clear the session. Refresh captures on compatible
   title/camera/display changes. Selection changes update the current draft's
   membership without silently rewriting historical stages. External pixel
   changes, owner replacement and incompatible storage require an explicit
   invalidation policy. On resource refusal after external navigation, preserve
   the complete saved intent, show the error and refuse stale Apply; never silently
   drop visits or publish old work. Worker results remain generation-guarded.

5. **End-to-end qualification.** Run all reference checkpoints through the
   shared native panel in both standalone C++ and optional Python launches.
   Include actual navigation, selection, component, Reset and Apply widgets;
   eager/virtual XY and linked XYZ previews; pending and READY transitions;
   resource refusal/recovery; complete replay; one paired Undo/Redo and source
   immutability. Reuse the READY job only when its complete session matches.
   Run the existing native and installed Python regressions on Windows/Linux,
   installed SDK consumers and commercial-dependency checks. Preserve the
   independent old Auto producer-source failure until separately requalified.

Deliver these packets in dependency order; do not enable incomplete navigation
retention in the main panel while capture/Apply/replay still loses history.
After each packet, archive and verify exact owned scratch before deleting it;
retain final installs, probes, logs and useful build caches.

## Reproduction

With pinned upstream source/classes and a JDK available:

```text
python -I -X utf8 -B tests/reference/extract_rgb_navigation_fixture.py --imagej-root IMAGEJ_ROOT --jdk JDK_ROOT --output-dir NEW_REFERENCE_DIR --fixture OUTPUT_JSON
```

With the native Python UI and NumPy installed in the actual interpreter used:

```text
python -I -X utf8 -B tests/reference/compare_rgb_navigation_editor.py --fixture tests/fixtures/rgb-contrast-navigation-imagej154r.json --output-dir NEW_PROBE_DIR
```

The probe writes exact installed module hashes, numerical observations and a
match count. Exit 1 currently identifies twelve mismatches. It must pass without
changing its expected ImageJ hashes when the six covered transitions are fixed.
The fixture, drivers and probe are commercial-use project code and introduce
no production dependency. Broader sliders/log-plot/Set/calibration, workflow
stage editing and other ImageJ/Fiji/platform gaps remain tracked separately.
