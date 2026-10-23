# RGB snapshot and navigation implementation plan

Status: open; reference and installed-editor gap evidence captured on 2026-10-22.
This is part of G057, not a change to the original ImageJ/Fiji scope or acceptance.

Current checkpoint: shared main-panel session transitions and complete Apply are
connected; the unchanged six-case editor probe now matches all 48 checks on both
hosts. The paragraphs below preserve the earlier dependency milestones; their
remaining-work statements describe those checkpoints. Current integration and
qualification limits appear before Observed behavior.

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
mapping editing and independent per-visit relocation remain open. Windows/Linux each qualify
22 native suites and 51,397 unique installed Python checks (6,078 new). The
corrected UI/Batch tail completes the initial full prefix; its report-path setup
error remains recorded. See `reviews/p11v-rgb-session-workflow001.json`. Main brightness/contrast transitions are the next integration gate.

The native transition controller `rgb_contrast_session_editor.hpp` now implements
admitted draft replacement, selection synchronization, explicit/component
promotion, returning visits, Reset and compatible capture refresh. All candidates
validate before adoption; a refusal preserves the previous complete capture.
External pixels/storage/calibration or owner changes require a new controller.
`preview_command()` retains full history without the final stack Apply policy;
`command()` includes that policy. Windows/Linux each pass four native suites, one installed SDK consumer and
23,162 installed Python checks (5,681 new). Main ImGui transition
handling remains an explicit integration task, not passing editor acceptance.

### Main-panel integration packet

1. Adopt the native controller before `tick` replaces `source_` or resets range.
   Admit outgoing visit plus the new source first. Preserve the old intent on
   refusal, show the error and disable stale Apply. Retry only on explicit retry
   or changed limits/source. Handle external edits with an explicit new-session
   action; never silently discard accumulated visits.
2. Route range, Reset, component, Auto and selection events through native state.
   Auto measures a worker-evaluated current-visit baseline with its draft reset;
   all promoted stages and prior visits remain. Keep explicit uniform multi-plane
   targets representable: do not silently narrow them into current-plane visits.
3. Submit complete session previews on workers. Display their baked logical plane
   outputs for every visited position, including pixels outside the current live
   selection. Keep linked XYZ and histogram consumers generation guarded.
4. Add session dispatch to host Apply and recorder validation. Reuse a READY job
   only if the complete command matches; entire-stack final Apply may need a new
   worker because navigation preview excludes its noncurrent-plane policy.
   Publication remains one original-owner/all-revision paired edit.
5. Run the unchanged six-case installed editor probe, then all 378 traces through
   real panel actions/widgets, including blocked/canceled/READY transitions,
   resource refusal/recovery, complete capture/portable replay and paired Undo.
   Qualify standalone C++ and optional Python launches on Windows/Linux before
   changing the editor gap claims below.

The main shared ImGui panel is now connected to the native controller. It
synchronizes before replacing the outgoing source/range, preserves rejected
intent, and exposes explicit restart. Compatible changes refresh captures;
identity-only fresh drafts may adopt a new source after completed Apply/Undo.
An incompatible external edit with pending history is refused. Explicit uniform
targets remain in their own existing profile and cannot silently become visits.
Selection/navigation boundaries switch complete processing, preview and recording
to the session profile; simple single-visit profiles stay available. Frozen
historical masks make stack-policy switching safe for non-area session stages.
The preview target covers whole logical visited planes, including historical
pixels outside the current selection. Auto evaluates the retained current-visit
baseline on workers. Apply captures the full session; only an identical READY
command can be reused, while final stack policy prepares its own guarded job.
Virtual XYZ sampling uses workers even when ordinary async rendering is disabled.
Windows/Linux qualify 29,248 unique installed Python checks, including all 6,086
affected frontend cases rerun after the final busy-state guard. Six native groups
and three installed standalone UI probes also pass. See
`reviews/p11x-rgb-session-panel001.json`. Broader
actual navigation/selection gesture coverage, combined upstream promotion plus
navigation/final-stack traces and dedicated saved-stage authoring remain open.

The combined reference now invokes ImageJ's actual component-selection callback
(`itemStateChanged`) and consumes its requested Reset through the real inherited
implementation. It then changes selection, promotes again, visits B, returns to A,
replaces and resets its draft, invokes manual mapping or actual Auto, and finally
calls the real private `applyRGBStack`. The AWT queue and presentation remain
outside this harness. Two independent compilations/runs reproduce all 4,536 rows:
42 component-promotion pairs × 9 selections × 3 axes × 2 directions × 2 final modes.
The final active processor must remain identical across stack Apply. The fixture
pins all driver bytecode, relevant ImageJ classes/source and Java/Javac binaries.

`test_rgb_combined.py` consumes each row through both the native session service
and the installed shared panel. It verifies promoted snapshots, replacement and
returning Reset, returning Auto range/cycle, final active output and complete
stack output, full capture/workflow replay, worker publication, source immutability
and paired history. Eighty-four further cases use real ImGui input for every
component pair, stack checkbox and Apply, with explicit window focus in the same
headless input session. Windows/Linux each pass all 9,156 installed checks; see
`reviews/p11y-rgb-combined001.json`. Runtime code is unchanged. Actual selection and
navigation gestures, returning Auto/Reset widget delivery beyond these action
checks, richer visit orders and dedicated stage authoring still need coverage.

Fresh scalar Auto qualification now passes the current native/SDK/API/UI checks
on Windows and Linux, plus all 144 Windows composition assertions. Separate
current contracts bind fresh persisted-file identities while preserving all
non-file-identity expectations. See [the scoped report](AUTO_REQUALIFICATION.md).
The historical source gate remains open pending exact original source/executable
recovery. Next editor work covers real selection/navigation/returning Auto/Reset
input and independent stage/visit authoring.

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

The original installed-editor probe captured six All-component cases with
none-to-rectangle selection, every axis and both directions. Its twelve returning
Reset/remap mismatches (36/48 checks) are preserved in the earlier gap evidence.
The unchanged probe now matches 48/48 checks on Windows/Linux. The panel reference
matrix additionally checks all 378 rows through installed shared panel actions,
without changing the ImageJ expected hashes. This does not by itself prove every
real selection/navigation gesture, platform or combined upstream trace.

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
match count. It now exits 0 for all 48 checks, with the expected ImageJ hashes
unchanged. The earlier twelve mismatches remain in the original gap evidence.
The fixture, drivers and probe are commercial-use project code and introduce
no production dependency. Broader sliders/log-plot/Set/calibration, workflow
stage editing and other ImageJ/Fiji/platform gaps remain tracked separately.
