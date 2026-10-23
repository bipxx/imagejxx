"""Native transition state; real panel transition widgets remain a separate gate."""
import json
import numpy as np
import pytest
import imagejxx as ij
from test_rgb_contrast_session import REFERENCE, PIXELS, sha
from test_contrast_apply import STACK_FIXTURE, STACK_PIXELS, rgb_stack_selection


def position(owner, at):
    view = owner.snapshot().display
    view.t, view.c = at[:2]
    view.crosshair = [0, 0, at[2]]
    owner.set_display(view)


@pytest.mark.parametrize("row", REFERENCE["cases"], ids=lambda r:f"a{r['axis']}-o{r['origin']}-c{r['channels']}-s{r['roi']}")
def test_native_transitions_match_every_actual_imagej_checkpoint(row):
    reads = ij.DatasetReadObserver()
    owner = ij.Document(ij.Dataset(PIXELS, axes="TCZYXS").observe_reads(reads))
    a = [row["origin"]]*3
    b = a.copy()
    b[row["axis"]] = 1-row["origin"]
    position(owner, a)
    owner.select(rgb_stack_selection(row["roi"]))
    editor = ij.RgbContrastSessionEditor(owner.snapshot())
    editor.set_channels(row["channels"])
    editor.set_entire_stack(True)

    def evaluate(key, at=a, full=False):
        start = reads.counts().plane_reads
        command = editor.preview_command()
        assert not command.plan.entire_stack
        assert reads.counts().plane_reads == start
        result = ij.execute_rgb_contrast_session_command(command).preview()
        values = result.dataset.numpy("TCZYXS")
        assert sha(values if full else values[tuple(at)]) == row[key+"_sha256"]
        assert result.selection == owner.snapshot().selection
        assert result.display.crosshair == owner.snapshot().display.crosshair

    editor.set_range(10.9, 40.1)
    evaluate("first_map_a")
    owner.select(rgb_stack_selection(row["changed_roi"]))
    assert editor.synchronize(owner.snapshot()) == ij.RgbContrastSessionTransition.Selection
    editor.set_range(20.25, 200.75)
    evaluate("changed_selection_map_a")
    editor.reset()
    evaluate("original_a")
    editor.set_range(20.25, 200.75)
    position(owner, b)
    assert editor.synchronize(owner.snapshot()) == ij.RgbContrastSessionTransition.Visit
    evaluate("original_b", b)
    editor.set_range(-30.25, 400.5)
    evaluate("mapped_b", b)
    position(owner, a)
    assert editor.synchronize(owner.snapshot()) == ij.RgbContrastSessionTransition.Visit
    editor.reset()
    evaluate("return_reset_a")
    evaluate("reset_stack", full=True)
    editor.set_range(-10.2, 160.9)
    evaluate("return_remap_a")
    evaluate("remapped_stack", full=True)
    assert len(editor.plan.visits) == 3
    assert all(len(v.stages) == 1 for v in editor.plan.visits)
    np.testing.assert_array_equal(owner.snapshot().dataset.numpy("TCZYXS"), PIXELS)


@pytest.mark.parametrize("row", STACK_FIXTURE["rows"], ids=lambda r:f"r{r['range_index']}-c{r['channels']}-s{r['roi']}-a{r['anchor']}-p{int(r['prefix'])}-w{int(r['whole_plane'])}")
def test_final_apply_policy_matches_all_actual_imagej_stack_cases(row):
    reads = ij.DatasetReadObserver()
    owner = ij.Document(ij.Dataset(STACK_PIXELS, axes="TCZYXS").observe_reads(reads))
    position(owner, STACK_FIXTURE["anchors"][row["anchor"]])
    owner.select(rgb_stack_selection(row["roi"]))
    editor = ij.RgbContrastSessionEditor(owner.snapshot())
    if row["prefix"]:
        editor.set_range(*STACK_FIXTURE["prefix_range"])
        editor.promote()
    editor.set_channels(row["channels"])
    editor.set_range(*STACK_FIXTURE["ranges"][row["range_index"]], whole_plane=row["whole_plane"])
    editor.set_entire_stack(True)
    saved = editor.command
    assert saved.plan.entire_stack and not editor.preview_command().plan.entire_stack
    assert reads.counts().plane_reads == 0
    result = ij.execute_rgb_contrast_session_command(saved).preview()
    assert sha(result.dataset.numpy("TCZYXS")) == row["stack_sha256"]
    assert len(saved.plan.visits[0].stages) == 1+int(row["prefix"])


def small():
    pixels = np.full((1, 1, 2, 2, 4, 3), 30, np.uint8)
    reads = ij.DatasetReadObserver()
    return ij.Document(ij.Dataset(pixels, axes="TCZYXS").observe_reads(reads)), reads


def test_selection_promotion_reset_refresh_and_detached_values():
    owner, reads = small()
    owner.select(ij.Selection("rectangle", [(0, 0), (1, 1)]))
    editor = ij.RgbContrastSessionEditor(owner.snapshot())
    editor.set_range(10, 40)
    editor.promote()
    old = editor.command
    owner.select(ij.Selection("rectangle", [(1, 0), (2, 1)]))
    editor.synchronize(owner.snapshot())
    assert editor.plan.visits[0].stages[0].selection == old.plan.visits[0].stages[0].selection
    assert editor.plan.visits[0].stages[1].selection == owner.snapshot().selection
    editor.set_range(20, 60, whole_plane=True)
    view = owner.snapshot().display
    view.zoom = 2
    owner.set_display(view)
    assert editor.synchronize(owner.snapshot()) == ij.RgbContrastSessionTransition.Refreshed
    assert editor.plan.visits[0].stages[1].whole_plane
    editor.reset()
    assert not editor.plan.visits[0].stages[1].whole_plane
    detached = editor.plan
    detached.visits = []
    limits = editor.limits
    limits.max_record_bytes = 1
    assert len(editor.plan.visits) == 1 and editor.limits.max_record_bytes > 1
    assert reads.counts().plane_reads == 0


@pytest.mark.parametrize("kind", ["pixels", "calibration", "owner"])
def test_external_incompatible_source_refuses_without_discarding_history(kind):
    owner, reads = small()
    editor = ij.RgbContrastSessionEditor(owner.snapshot())
    editor.set_range(10, 40)
    position(owner, [0, 0, 1])
    editor.synchronize(owner.snapshot())
    saved = editor.command.record()
    if kind == "owner":
        other = ij.Document(owner.snapshot().dataset)
        changed = other.snapshot()
    else:
        edit = owner.snapshot().begin_edit()
        if kind == "pixels":
            edit.replace_dataset(ij.Dataset(np.zeros((1, 1, 2, 2, 4, 3), np.uint8), axes="TCZYXS"))
        else:
            cal = ij.SpatialCalibration(spacing=(2, 1, 1))
            edit.set_calibration(cal)
        owner.commit(edit)
        changed = owner.snapshot()
    with pytest.raises(ValueError, match="start a new session explicitly"):
        editor.synchronize(changed)
    assert editor.command.record() == saved and reads.counts().plane_reads == 0


@pytest.mark.parametrize("operation", ["nan", "order", "channels", "record", "work"])
def test_failed_edit_or_limit_readmission_has_strong_state_guarantee(operation):
    owner, reads = small()
    editor = ij.RgbContrastSessionEditor(owner.snapshot())
    editor.set_range(10, 40)
    saved = editor.command.record()
    with pytest.raises(ValueError):
        if operation == "nan": editor.set_range(float("nan"), 40)
        elif operation == "order": editor.set_range(40, 10)
        elif operation == "channels": editor.set_channels(8)
        else:
            limits = editor.limits
            if operation == "record": limits.max_record_bytes = 1
            else: limits.processing.max_working_bytes = 1
            editor.set_limits(limits)
    assert editor.command.record() == saved and reads.counts().plane_reads == 0


def test_navigation_refusal_recovery_and_complete_worker_capture():
    owner, reads = small()
    editor = ij.RgbContrastSessionEditor(owner.snapshot())
    editor.set_range(10, 40)
    limits = editor.limits
    estimate = ij.estimate_rgb_contrast_session(editor.source, editor.plan)
    limits.processing.max_pixels = estimate.pixels
    editor.set_limits(limits)
    saved = editor.command.record()
    position(owner, [0, 0, 1])
    with pytest.raises(ValueError): editor.synchronize(owner.snapshot())
    assert editor.command.record() == saved and reads.counts().plane_reads == 0
    editor.set_limits(ij.ContrastApplyCommandLimits())
    editor.synchronize(owner.snapshot())
    editor.set_range(20, 60)
    record = json.loads(json.dumps(editor.command.record()))
    replay = ij.rgb_contrast_session_command_from_record(owner.snapshot(), record)
    app = ij.Application()
    app.adopt(owner)
    history = owner.undo_steps
    job = ij.submit_rgb_contrast_session(app.job_scheduler, replay)
    assert job.wait(5000)
    expected = sha(job.preview.dataset.numpy("TCZYXS"))
    job.publish(app)
    assert owner.undo_steps == history+1
    assert sha(owner.snapshot().dataset.numpy("TCZYXS")) == expected
    owner.undo()
    owner.redo()
    assert sha(owner.snapshot().dataset.numpy("TCZYXS")) == expected
    app.job_scheduler.shutdown()


def test_public_exports():
    assert {"RgbContrastSessionEditor", "RgbContrastSessionTransition"} <= set(ij.__all__)
