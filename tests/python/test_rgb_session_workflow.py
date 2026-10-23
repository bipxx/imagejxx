"""Portable complete-history intent; exact samples come from the C++ service."""
import copy
import json

import numpy as np
import pytest
import imagejxx as ij

from test_rgb_contrast_session import REFERENCE, PIXELS, step, visit, plan, sha
from test_rgb_contrast_session_command import request
from test_contrast_apply import STACK_FIXTURE, STACK_PIXELS, rgb_stack_owner, rgb_stack_steps, rgb_stack_selection


def operation(command, anchor=None):
    value = ij.make_rgb_contrast_session_workflow_step(command)
    if anchor is None:
        return value
    record = ij.NativeWorkflowProgram([value]).record()["steps"][0]
    return ij.NativeWorkflowOperation("rgb_contrast_session", {},
        ij.TargetRequest(ij.TargetScope.POSITIONS, positions=[ij.PlanePosition(*anchor)], roi=ij.RoiPolicy.WHOLE_PLANE),
        original=record["original"])


@pytest.mark.parametrize("row", REFERENCE["cases"], ids=lambda r:f"a{r['axis']}-o{r['origin']}-c{r['channels']}-s{r['roi']}")
def test_full_history_default_anchor_matches_all_imagej_reset_and_remap_traces(row):
    for reset in [False, True]:
        reads = ij.DatasetReadObserver()
        owner = ij.Document(ij.Dataset(PIXELS, axes="TCZYXS").observe_reads(reads))
        before = owner.snapshot()
        command = ij.resolve_rgb_contrast_session_command(before, request(row, reset))
        program = ij.NativeWorkflowProgram.from_json(ij.NativeWorkflowProgram([operation(command)]).json())
        saved = program.record()
        assert saved["version"] == 1 and saved["steps"][0]["original"] == command.record()
        assert saved["steps"][0]["target"]["positions"] == [[row["origin"]]*3]
        assert reads.counts().plane_reads == 0
        prepared = ij.prepare_native_workflow(before, program.steps)
        assert prepared.dependencies == ij.Change.ALL and prepared.completed_steps == 1
        assert reads.counts().plane_reads <= 2 and owner.snapshot().shares_data(before) and owner.undo_steps == 0
        expected = row["reset_stack_sha256" if reset else "remapped_stack_sha256"]
        assert sha(prepared.preview.dataset.numpy("TCZYXS")) == expected
        owner.commit(prepared.begin_edit())
        assert sha(owner.snapshot().dataset.numpy("TCZYXS")) == expected
        assert owner.undo_steps == int(row["changed_roi"] != 3)
        if owner.undo_steps:
            owner.undo()
            assert owner.snapshot().shares_data(before)
            owner.redo()
            assert sha(owner.snapshot().dataset.numpy("TCZYXS")) == expected
        assert program.record() == saved


@pytest.mark.parametrize("row", REFERENCE["cases"], ids=lambda r:f"translated-a{r['axis']}-o{r['origin']}-c{r['channels']}-s{r['roi']}")
def test_translate_original_three_cube_to_smaller_destination_without_losing_visits_or_masks(row):
    source_reads = ij.DatasetReadObserver()
    source = ij.Document(ij.Dataset(STACK_PIXELS, axes="TCZYXS").observe_reads(source_reads))
    original_plan = request(row)
    shifted = []
    for v in original_plan.visits:
        shifted.append(visit([v.position.t+1, v.position.c+1, v.position.z+1], v.stages))
    original_plan.visits = shifted
    command = ij.resolve_rgb_contrast_session_command(source.snapshot(), original_plan)
    saved = command.record()
    # Some saved positions are outside this destination. Decode/validate the
    # complete ORIGINAL descriptor before translating; absolute rebind first
    # would incorrectly refuse this portable intent.
    reads = ij.DatasetReadObserver()
    owner = ij.Document(ij.Dataset(PIXELS, axes="TCZYXS").observe_reads(reads))
    owner.select(rgb_stack_selection(3))
    view = owner.snapshot().display
    view.t = view.c = 1-row["origin"]
    view.crosshair = [0, 0, 1-row["origin"]]
    view.zoom = 1.5
    view.properties = {"display_range": [12., 220.]}
    owner.set_display(view)
    before = owner.snapshot()
    program = ij.NativeWorkflowProgram.from_json(ij.NativeWorkflowProgram([operation(command, [row["origin"]]*3)]).json())
    prepared = ij.prepare_native_workflow(before, program.steps)
    assert source_reads.counts().plane_reads == 0 and reads.counts().plane_reads <= 2
    out = prepared.preview
    assert sha(out.dataset.numpy("TCZYXS")) == row["remapped_stack_sha256"]
    assert out.selection == before.selection
    assert (out.display.t, out.display.c, out.display.crosshair, out.display.zoom) == (view.t, view.c, view.crosshair, view.zoom)
    assert program.record()["steps"][0]["original"] == saved
    assert owner.snapshot().shares_data(before) and owner.undo_steps == 0


@pytest.mark.parametrize("row", STACK_FIXTURE["rows"], ids=lambda r:f"stack-r{r['range_index']}-c{r['channels']}-s{r['roi']}-a{r['anchor']}-p{int(r['prefix'])}-w{int(r['whole_plane'])}")
def test_portable_session_final_stack_matches_all_actual_imagej_single_visit_references(row):
    reads = ij.DatasetReadObserver()
    owner = rgb_stack_owner(row, reads)
    before = owner.snapshot()
    selected = rgb_stack_selection(row["roi"])
    stages = [step(v.options.minimum, v.options.maximum, v.options.channels, selected, v.whole_plane) for v in rgb_stack_steps(row)]
    command = ij.resolve_rgb_contrast_session_command(before, plan([visit(STACK_FIXTURE["anchors"][row["anchor"]], stages)], stack=True))
    program = ij.NativeWorkflowProgram.from_json(ij.NativeWorkflowProgram([operation(command)]).json())
    assert reads.counts().plane_reads == 0
    prepared = ij.prepare_native_workflow(before, program.steps)
    expected_reads = 0 if row["roi"] == 3 and not row["whole_plane"] else 1 if row["roi"] == 3 else 27
    assert reads.counts().plane_reads == expected_reads and prepared.dependencies == ij.Change.ALL
    assert sha(prepared.preview.dataset.numpy("TCZYXS")) == row["stack_sha256"]
    assert prepared.preview.selection == before.selection
    assert prepared.preview.display.crosshair == before.display.crosshair
    owner.commit(prepared.begin_edit())
    assert owner.undo_steps == int(expected_reads != 0)
    if owner.undo_steps:
        owner.undo()
        assert owner.snapshot().shares_data(before)
        owner.redo()
        assert sha(owner.snapshot().dataset.numpy("TCZYXS")) == row["stack_sha256"]


def small():
    owner = ij.Document(ij.Dataset(PIXELS, axes="TCZYXS"))
    command = ij.resolve_rgb_contrast_session_command(owner.snapshot(), request(REFERENCE["cases"][0]))
    return owner, command, operation(command)


@pytest.mark.parametrize("bad", ["missing", "parameters", "filter", "calibration", "current", "ranges", "empty", "multiple", "selection", "positioned", "volume", "new_document", "unused_range", "negative"])
def test_portable_session_rejects_ignored_or_ambiguous_choices_before_io(bad):
    owner, command, value = small()
    record = ij.NativeWorkflowProgram([value]).record()
    row = record["steps"][0]
    target = row["target"]
    if bad == "missing": row["original"] = None
    elif bad == "parameters": row["parameters"] = {"minimum": {"kind": "real", "value": 10.}}
    elif bad == "filter":
        invalid = ij.NativeWorkflowOperation("rgb_contrast_session", {},
            ij.TargetRequest(ij.TargetScope.POSITIONS, positions=[ij.PlanePosition(0, 0, 0)], roi=ij.RoiPolicy.WHOLE_PLANE),
            original=command.record(), filter_options=ij.GaussianBlurCommandOptions())
        with pytest.raises(ValueError): ij.NativeWorkflowProgram([invalid])
        return
    elif bad == "calibration": row["use_destination_calibration"] = True
    elif bad in ["current", "ranges"]: target["scope"] = 0 if bad == "current" else 2; target["positions"] = []
    elif bad == "empty": target["positions"] = []
    elif bad == "multiple": target["positions"] = [[0, 0, 0], [0, 0, 1]]
    elif bad == "selection": target["roi"] = 1
    elif bad == "positioned": target["roi"] = 2
    elif bad == "volume": target["mode"] = 1
    elif bad == "new_document": target["destination"] = 1
    elif bad == "unused_range": target["t"] = [0, 1, 1]
    else: target["positions"] = [[-1, 0, 0]]
    with pytest.raises((ValueError, TypeError, IndexError)): ij.NativeWorkflowProgram.from_record(record)
    assert owner.undo_steps == 0


@pytest.mark.parametrize("bad", ["anchor", "translated_visit", "late_original", "scalar_prefix", "memory"])
def test_session_late_preflight_failures_do_not_read_earlier_prefix_samples(bad):
    reads = ij.DatasetReadObserver()
    owner = ij.Document(ij.Dataset(PIXELS, axes="TCZYXS").observe_reads(reads))
    source = owner.snapshot()
    command = ij.resolve_rgb_contrast_session_command(source, request(REFERENCE["cases"][0]))
    value = operation(command)
    record = ij.NativeWorkflowProgram([value]).record()
    prefix = ij.NativeWorkflowOperation("add", {"value": 1}, ij.TargetRequest(ij.TargetScope.ALL_Z, roi=ij.RoiPolicy.WHOLE_PLANE))
    limits = ij.NativeWorkflowLimits()
    if bad == "anchor": value = operation(command, [2, 0, 0])
    elif bad == "translated_visit": value = operation(command, [1, 0, 0])  # T visit at 1 would become 2
    elif bad == "late_original":
        original = copy.deepcopy(record["steps"][0]["original"])
        original["visits"][-1]["stages"][0]["whole_plane"] = 1
        value = ij.NativeWorkflowOperation("rgb_contrast_session", {},
            ij.TargetRequest(ij.TargetScope.POSITIONS, positions=[ij.PlanePosition(0, 0, 0)], roi=ij.RoiPolicy.WHOLE_PLANE), original=original)
    elif bad == "scalar_prefix":
        prefix = ij.NativeWorkflowOperation("convert_image", {"dtype": "uint8", "scale": False, "weighted_rgb": False},
            ij.TargetRequest(ij.TargetScope.RANGES, t=ij.AxisRange(0, 2), c=ij.AxisRange(0, 2), z=ij.AxisRange(0, 2), roi=ij.RoiPolicy.WHOLE_PLANE))
    else: limits = ij.NativeWorkflowLimits(max_retained_bytes=1)
    with pytest.raises((ValueError, TypeError, IndexError)):
        ij.prepare_native_workflow(source, [prefix, value], limits)
    assert reads.counts().plane_reads == 0 and owner.snapshot().shares_data(source) and owner.undo_steps == 0


@pytest.mark.parametrize("change", ["pixels", "selection", "display", "metadata"])
def test_session_workflow_publication_guards_every_source_revision(change):
    owner, _, value = small()
    source = owner.snapshot()
    prepared = ij.prepare_native_workflow(source, [value])
    if change == "pixels": owner.commit(prepared.begin_edit())
    elif change == "selection": owner.select(rgb_stack_selection(1))
    elif change == "display":
        view = source.display; view.zoom = 2; owner.set_display(view)
    else:
        edit = owner.begin_edit(); edit.set_title("newer"); owner.commit(edit)
    stable = owner.snapshot()
    with pytest.raises(ij.RevisionConflict): owner.commit(prepared.begin_edit())
    assert owner.snapshot().shares_data(stable)


@pytest.mark.parametrize("failure", ["pre_cancel", "late_cancel", "callback"])
def test_session_workflow_cancellation_or_callback_failure_never_publish(failure):
    owner, _, value = small()
    source = owner.snapshot()
    token = ij.CancellationToken()
    if failure == "pre_cancel": token.cancel()
    def progress(*_):
        if failure == "callback": raise RuntimeError("session workflow callback")
        token.cancel()
    with pytest.raises((ij.OperationCancelled, RuntimeError)):
        ij.prepare_native_workflow(source, [value], cancellation=token, progress=progress)
    assert owner.snapshot().shares_data(source) and owner.undo_steps == 0


def test_disabled_session_validates_original_without_resolving_destination():
    owner, command, _ = small()
    value = ij.NativeWorkflowOperation("rgb_contrast_session", {},
        ij.TargetRequest(ij.TargetScope.POSITIONS, positions=[ij.PlanePosition(999, 0, 0)], roi=ij.RoiPolicy.WHOLE_PLANE),
        original=command.record(), enabled=False)
    program = ij.NativeWorkflowProgram.from_json(ij.NativeWorkflowProgram([value]).json())
    source = owner.snapshot()
    prepared = ij.prepare_native_workflow(source, program.steps)
    owner.commit(prepared.begin_edit())
    assert prepared.completed_steps == 0 and owner.snapshot().shares_data(source) and owner.undo_steps == 0


@pytest.mark.parametrize("all_documents", [False, True])
def test_session_batch_preserves_input_tables_navigation_and_unselected_documents(tmp_path, all_documents):
    first, command, _ = small()
    second = ij.Document(ij.Dataset(PIXELS, axes="TCZYXS"))
    expected = ij.execute_rgb_contrast_session_command(command).preview().dataset.numpy("TCZYXS")
    value = operation(command)
    capture = ij.SessionCapture([first.snapshot(), second.snapshot()])
    capture.results = [{"kept": True}]
    capture.workflow = [{"schema": "kept"}]
    source = tmp_path/"input.ijx"
    ij.save_session(str(source), capture)
    original = source.read_bytes()
    out = tmp_path/"out"
    out.mkdir()
    batch = ij.prepare_native_workflow_batch([ij.NativeWorkflowBatchInput(str(source), "result")], [value], str(out),
        ij.NativeWorkflowBatchOptions(all_documents=all_documents))
    batch.publish()
    result = ij.load_session(str(out/"result.ijx"))
    np.testing.assert_array_equal(result.documents[0].snapshot().dataset.numpy("TCZYXS"), expected)
    np.testing.assert_array_equal(result.documents[1].snapshot().dataset.numpy("TCZYXS"), expected if all_documents else PIXELS)
    assert result.results == capture.results and result.workflow == capture.workflow and source.read_bytes() == original
    assert first.undo_steps == second.undo_steps == 0
