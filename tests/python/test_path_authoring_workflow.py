"""Independent final-publication and version/extent oracles for authored paths."""
import copy

import numpy as np
import pytest

import imagejxx as ij


COMMANDS = [0, 0, 0, 1, 8, 0, 2, 12, 4, 8, 8, 3, 6, 10, 2, 10, 0, 8, 4]
SECOND = [0, 1, 2, 1, 9, 2, 2, 13, 6, 9, 10, 3, 7, 12, 3, 12, 1, 10, 4]


def owner(dtype=np.uint16):
    pixels = np.arange(2 * 2 * 2 * 12 * 16, dtype=dtype).reshape(2, 2, 2, 12, 16, 1)
    reads = ij.DatasetReadObserver()
    document = ij.Document(ij.Dataset(pixels, axes="TCZYXS").observe_reads(reads))
    previous = ij.Selection("line", [(1, 2), (4, 5)])
    original = ij.Selection("rectangle", [(2, 3), (5, 6)])
    document.select(previous)
    document.select(original)
    return document, reads, pixels, original, previous


def step(candidate, shape):
    request = ij.SelectionCreationRequest(candidate, ij.SelectionCreationOperation.REPLACE,
                                         ij.SelectionCreationGeometry.NATIVE_PATH_V4)
    return ij.NativeWorkflowOperation("selection_creation", {}, ij.TargetRequest(),
                                      selection_creation=request, selection_creation_extent=shape)


@pytest.mark.parametrize("dtype", [np.uint8, np.uint16, np.float32, np.float64])
def test_composed_authored_paths_publish_original_previous_once(dtype):
    document, reads, pixels, original, previous = owner(dtype)
    before = document.snapshot()
    shape = before.dataset.shape
    a = ij.Selection.from_path(ij.ImageJCompositePath(COMMANDS))
    b = ij.Selection.from_path(ij.ImageJCompositePath(SECOND))
    program = ij.NativeWorkflowProgram([step(a, shape), step(b, shape)])
    record = program.record()
    assert record["version"] == 8
    assert record["steps"][0]["selection_creation_extent"] == list(shape)
    restored = ij.NativeWorkflowProgram.from_json(program.json())
    assert restored.record() == record
    plan = ij.prepare_native_workflow(before, restored.steps)
    assert document.snapshot().shares_data(before)
    document.commit(plan.begin_edit())
    after = document.snapshot()
    assert after.selection.path.commands == SECOND
    assert after.previous_selection == original
    assert document.undo_steps == 1 and document.redo_steps == 0
    assert reads.counts().plane_reads == reads.counts().region_reads == 0
    np.testing.assert_array_equal(after.dataset.numpy("TCZYXS"), pixels)
    document.undo()
    assert document.snapshot().selection == original
    assert document.snapshot().previous_selection == previous
    document.redo()
    assert document.snapshot().selection.path.commands == SECOND
    assert document.snapshot().previous_selection == original


def test_net_noop_authored_workflow_retains_previous_and_has_no_history():
    document, reads, _, original, previous = owner()
    before = document.snapshot()
    path = ij.Selection.from_path(ij.ImageJCompositePath(COMMANDS))
    plan = ij.prepare_native_workflow(before, [step(path, before.dataset.shape),
                                             step(original, before.dataset.shape)])
    document.commit(plan.begin_edit())
    assert document.snapshot().selection == original
    assert document.snapshot().previous_selection == previous
    assert document.undo_steps == document.redo_steps == 0
    assert reads.counts().plane_reads == reads.counts().region_reads == 0


def test_disabled_original_v4_uses_v8_without_destination_extent_match():
    document, _, _, _, _ = owner()
    source = document.snapshot()
    candidate = ij.Selection.from_path(ij.ImageJCompositePath(COMMANDS))
    capture = ij.resolve_selection_creation_command(source, step(candidate, source.dataset.shape).selection_creation)
    retained = ij.NativeWorkflowOperation("selection_creation", {}, ij.TargetRequest(),
                                         original=capture.record(), enabled=False)
    program = ij.NativeWorkflowProgram([retained])
    assert program.record()["version"] == 8
    other = ij.Document(ij.Dataset(np.zeros((4, 5), np.uint8), axes="YX"))
    prepared = ij.prepare_native_workflow(other.snapshot(), program.steps)
    assert prepared.completed_steps == 0
    assert other.undo_steps == 0


def test_authored_extent_mismatch_refuses_before_reads_or_publication():
    document, reads, _, _, _ = owner()
    before = document.snapshot()
    shape = list(before.dataset.shape)
    shape[4] += 1
    candidate = ij.Selection.from_path(ij.ImageJCompositePath(COMMANDS))
    program = ij.NativeWorkflowProgram([step(candidate, shape)])
    with pytest.raises(ValueError):
        ij.prepare_native_workflow(before, program.steps)
    assert document.snapshot().shares_data(before)
    assert reads.counts().plane_reads == reads.counts().region_reads == 0


@pytest.mark.parametrize("version", range(1, 8))
def test_old_program_versions_do_not_admit_new_extent_or_path_intent(version):
    document, _, _, _, _ = owner()
    candidate = ij.Selection.from_path(ij.ImageJCompositePath(COMMANDS))
    record = copy.deepcopy(ij.NativeWorkflowProgram([
        step(candidate, document.snapshot().dataset.shape)]).record())
    record["version"] = version
    with pytest.raises(ValueError):
        ij.NativeWorkflowProgram.from_record(record)


def test_v6_shaped_record_cannot_hide_path_intent_without_new_fields():
    document, _, _, _, _ = owner()
    candidate = ij.Selection.from_path(ij.ImageJCompositePath(COMMANDS))
    record = ij.NativeWorkflowProgram([step(candidate, document.snapshot().dataset.shape)]).record()
    record["version"] = 6
    record["steps"][0].pop("selection_creation_extent")
    record["steps"][0].pop("annotation_intent")
    with pytest.raises(ValueError, match="V8"):
        ij.NativeWorkflowProgram.from_record(record)
