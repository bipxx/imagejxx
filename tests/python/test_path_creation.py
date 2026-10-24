"""Explicit V4 path publication and V8 workflow intent, history and admission."""
import struct

import numpy as np
import pytest
import imagejxx as ij

COMMANDS = [0, -0., 1, 2, 4, -2, 8, 1, 3, 10, 3, 10, 7, 8, 8, 1, 0, 8, 4,
            0, 2, 3, 1, 3, 3, 1, 3, 4, 4]
EXTENT = [1, 1, 3, 12, 16, 1]


def path(dx=0):
    selection = ij.Selection.from_path(ij.ImageJCompositePath(COMMANDS)).with_attributes(
        {"opaque": [True, 2**64-1, "kept"]})
    return ij.SelectionEditor(selection, (12, 16)).moved([dx, 0])


def owner():
    reads = ij.DatasetReadObserver()
    doc = ij.Document(ij.Dataset(np.arange(576, dtype=np.uint16).reshape(3, 12, 16), axes="ZYX").observe_reads(reads))
    tx = doc.begin_edit()
    tx.select(ij.Selection("rectangle", [(1, 1), (4, 5)]))
    tx.set_previous_selection(path(-5))
    tx.set_rois([ij.ROI("retained", path(20), z=1, attributes={"row": "kept"})])
    doc.commit(tx)
    doc.mark_saved()
    return doc, reads


def request(candidate=None):
    return ij.SelectionCreationRequest(path() if candidate is None else candidate,
        geometry=ij.SelectionCreationGeometry.NATIVE_PATH_V4)


def step(candidate=None, extent=EXTENT, original=None, enabled=True, authored=True):
    return ij.NativeWorkflowOperation("selection_creation", {}, ij.TargetRequest(),
        original=original, enabled=enabled, selection_creation=request(candidate) if authored else None,
        selection_creation_extent=extent if authored else None)


@pytest.mark.parametrize("source_path,candidate_path", [(False, True), (True, True), (True, False), (False, False)])
def test_explicit_v4_replace_history_noop_and_all_retained_carriers(source_path, candidate_path):
    doc, reads = owner()
    if source_path: doc.select(path(-1))
    before = doc.snapshot()
    history = doc.undo_steps
    candidate = path() if candidate_path else ij.Selection("ellipse", [(3, 2), (9, 7)])
    command = ij.resolve_selection_creation_command(before, request(candidate))
    record = command.record()
    assert record["version"] == 4 and record["operation"] == "replace"
    assert command.result == candidate
    ij.validate_selection_creation_command_record(record)
    tx = ij.prepare_selection_creation(command)
    assert tx.preview().selection == candidate and doc.snapshot().shares_data(before)
    doc.commit(tx)
    after = doc.snapshot()
    assert after.selection == candidate and after.previous_selection == before.selection
    assert after.dataset.shares_pixels(before.dataset)
    def rows(snapshot):
        return [(r.name, r.selection, r.t, r.c, r.z, r.attributes) for r in snapshot.rois]
    assert rows(after) == rows(before)
    assert after.metadata == before.metadata and doc.undo_steps == history+1
    doc.undo(); assert doc.snapshot().shares_data(before)
    doc.redo(); assert doc.snapshot().shares_data(after)
    noop = ij.resolve_selection_creation_command(doc.snapshot(), request(candidate))
    assert not noop.changes_selection
    doc.commit(ij.prepare_selection_creation(noop))
    assert doc.undo_steps == history+1
    assert reads.counts().plane_reads == reads.counts().region_reads == 0


@pytest.mark.parametrize("mutation", ["version1", "version2", "version3", "result", "extra", "partial", "hidden_points", "boolean"])
def test_closed_versions_malformed_geometry_and_equation(mutation):
    doc, reads = owner(); before = doc.snapshot()
    record = ij.resolve_selection_creation_command(before, request()).record()
    if mutation.startswith("version"): record["version"] = int(mutation[-1])
    elif mutation == "result": record["result"]["path"][1] = .125
    elif mutation == "extra": record["candidate"]["extra"] = 1
    elif mutation == "partial": record["candidate"]["path"] = [0, 1]
    elif mutation == "hidden_points": record["candidate"]["points"] = [[1, 2]]
    else: record["operation"] = "add"
    with pytest.raises(ValueError): ij.validate_selection_creation_command_record(record)
    assert doc.snapshot().shares_data(before) and reads.counts().plane_reads == 0


@pytest.mark.parametrize("field", ["max_path_segments", "max_path_command_scalars", "max_candidate_vertices", "max_working_bytes"])
def test_path_limits_refuse_before_user_callbacks(field):
    doc, _ = owner(); limits = ij.SelectionCreationLimits(); setattr(limits, field, 1)
    calls = []
    with pytest.raises(ValueError):
        ij.resolve_selection_creation_command(doc.snapshot(), request(), limits,
            progress=lambda *v: calls.append(v), checkpoint=lambda: calls.append("poll"))
    assert not calls


def test_unrelated_path_storage_is_admitted_for_legacy_candidate():
    doc, _ = owner(); before = doc.snapshot()
    candidate = ij.SelectionCreationRequest(ij.Selection("rectangle", [(2, 2), (5, 5)]))
    baseline = ij.estimate_selection_creation(before, candidate).working_bytes
    tx = before.begin_edit(); tx.set_rois([]); tx.set_previous_selection(None); doc.commit(tx)
    assert baseline > ij.estimate_selection_creation(doc.snapshot(), candidate).working_bytes
    limits = ij.SelectionCreationLimits(); limits.max_path_segments = 1
    with pytest.raises(ValueError): ij.resolve_selection_creation_command(before, candidate, limits)


def test_exact_budget_and_finish_cancellation():
    doc, _ = owner(); before = doc.snapshot(); intent = request()
    limits = ij.SelectionCreationLimits()
    limits.max_working_bytes = ij.estimate_selection_creation(before, intent).working_bytes
    ij.resolve_selection_creation_command(before, intent, limits)
    limits.max_working_bytes -= 1
    with pytest.raises(ValueError): ij.resolve_selection_creation_command(before, intent, limits)
    token = ij.CancellationToken()
    with pytest.raises(ij.OperationCancelled):
        ij.resolve_selection_creation_command(before, intent, cancellation=token,
            progress=lambda done, total: token.cancel() if done == 1 else None)
    assert doc.snapshot().shares_data(before)


def test_v8_composition_previous_noop_and_original_only_extent():
    doc, reads = owner(); before = doc.snapshot(); history = doc.undo_steps
    capture = ij.resolve_selection_creation_command(before, request()).record()
    original_only = ij.NativeWorkflowProgram([step(original=capture, authored=False)])
    assert original_only.record()["version"] == 8
    assert original_only.record()["steps"][0]["selection_creation_extent"] is None
    ij.prepare_native_workflow(before, original_only.steps)
    program = ij.NativeWorkflowProgram([step(), step(path(1))])
    assert program.record()["version"] == 8
    assert program.record()["steps"][0]["selection_creation_extent"] == EXTENT
    restored = ij.NativeWorkflowProgram.from_json(program.json())
    assert restored.record() == program.record()
    result = ij.prepare_native_workflow(before, restored.steps)
    assert result.preview.selection == path(1) and result.preview.previous_selection == before.selection
    doc.commit(result.begin_edit())
    assert doc.undo_steps == history+1
    doc.undo(); assert doc.snapshot().shares_data(before)
    current = doc.snapshot()
    netnoop = ij.prepare_native_workflow(current, [step(), step(current.selection)])
    assert netnoop.preview.previous_selection == current.previous_selection
    doc.commit(netnoop.begin_edit())
    assert doc.undo_steps == history and doc.snapshot().shares_data(current)
    assert reads.counts().plane_reads == reads.counts().region_reads == 0


@pytest.mark.parametrize("version", range(1, 8))
def test_old_programs_cannot_smuggle_new_intent(version):
    record = ij.NativeWorkflowProgram([step()]).record()
    record["version"] = version
    record["steps"][0].pop("selection_creation_extent")
    with pytest.raises(ValueError): ij.NativeWorkflowProgram.from_record(record)


def test_extent_is_required_explicit_and_disabled_intent_stays_portable():
    doc, reads = owner(); before = doc.snapshot()
    with pytest.raises(ValueError): ij.NativeWorkflowProgram([step(extent=None)])
    mismatch = EXTENT.copy(); mismatch[4] += 1
    program = ij.NativeWorkflowProgram([step(extent=mismatch)])
    with pytest.raises(ValueError): ij.prepare_native_workflow(before, program.steps)
    disabled = ij.NativeWorkflowProgram([step(extent=mismatch, enabled=False)])
    assert ij.prepare_native_workflow(before, disabled.steps).completed_steps == 0
    capture = ij.resolve_selection_creation_command(before, request()).record()
    rebound = step(ij.Selection("rectangle", [(1, 1), (2, 3)]), original=capture)
    assert ij.NativeWorkflowProgram([rebound]).record()["version"] == 8
    assert reads.counts().plane_reads == 0 and doc.snapshot().shares_data(before)


def test_v4_strict_replay_rebind_stale_and_session(tmp_path):
    doc, _ = owner(); before = doc.snapshot()
    command = ij.resolve_selection_creation_command(before, request())
    replay = ij.selection_creation_command_from_record(before, command.record())
    assert replay.result.path.commands == COMMANDS
    assert struct.pack("d", replay.result.path.commands[1]) == struct.pack("d", -0.)
    tx = ij.prepare_selection_creation(command)
    doc.select(path(2))
    with pytest.raises(ij.RevisionConflict): doc.commit(tx)
    rebound = ij.rebind_selection_creation_command(doc.snapshot(), command.record(), request(path(1)))
    doc.commit(ij.prepare_selection_creation(rebound))
    archive = tmp_path / "authored.ijx"
    ij.save_session(str(archive), ij.SessionCapture([doc]))
    reopened = ij.load_session(str(archive)).documents[0].snapshot()
    assert archive.read_bytes().startswith(b"IMAGEJXX5\n")
    assert reopened.selection.path.commands == path(1).path.commands
    assert reopened.previous_selection.path.commands == path(2).path.commands


def test_v4_original_requires_explicit_native_mode_for_authored_ordinary_override():
    doc, reads = owner(); before = doc.snapshot()
    capture = ij.resolve_selection_creation_command(before, request()).record()
    ordinary = ij.Selection("rectangle", [(2, 2), (5, 6)])
    legacy = ij.NativeWorkflowOperation("selection_creation", {}, ij.TargetRequest(),
        original=capture, selection_creation=ij.SelectionCreationRequest(ordinary),
        selection_creation_extent=EXTENT)
    with pytest.raises(ValueError, match="NativePathV4"):
        ij.NativeWorkflowProgram([legacy])
    with pytest.raises(ValueError, match="NativePathV4"):
        ij.prepare_native_workflow(before, [legacy])
    explicit = step(ordinary, original=capture)
    assert ij.prepare_native_workflow(before, [explicit]).preview.selection == ordinary
    assert doc.snapshot().shares_data(before) and reads.counts().plane_reads == 0


@pytest.mark.parametrize("mutation", ["extent_mismatch", "extent_missing", "disabled_v4_original"])
@pytest.mark.parametrize("preceding_valid", [False, True])
def test_workflow_refusal_precedes_exposed_progress_and_reads(mutation, preceding_valid):
    doc, reads = owner()
    before = doc.snapshot()
    history = (doc.undo_steps, doc.redo_steps)
    if mutation == "extent_mismatch":
        extent = EXTENT.copy()
        extent[4] += 1
        invalid = step(extent=extent)
    elif mutation == "extent_missing":
        invalid = step(extent=None)
    else:
        record = ij.resolve_selection_creation_command(before, request()).record()
        record["result"]["path"][1] = .125
        invalid = step(original=record, enabled=False, authored=False)
    steps = ([step(path(1))] if preceding_valid else []) + [invalid]
    calls = []
    with pytest.raises(ValueError):
        ij.prepare_native_workflow(before, steps, progress=lambda *args: calls.append(args))
    # Python intentionally exposes workflow progress only. The paired native
    # matrix checks checkpoint/progress/stage callbacks independently.
    assert not calls
    assert doc.snapshot().shares_data(before)
    assert (doc.undo_steps, doc.redo_steps) == history
    assert reads.counts().plane_reads == reads.counts().region_reads == 0
