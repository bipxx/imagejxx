"""Installed bindings to the C++ per-plane session evaluator, not Python kernels."""
import hashlib
import json
from pathlib import Path

import numpy as np
import pytest
import imagejxx as ij

from test_contrast_apply import rgb_options, rgb_stack_selection, STACK_FIXTURE, STACK_PIXELS


REFERENCE = json.loads((Path(__file__).parents[1] / "fixtures/rgb-contrast-navigation-imagej154r.json").read_bytes())
T, C, Z, Y, X = np.indices((2, 2, 2, 8, 256))
PIXELS = np.stack(((X+11*C+23*Z+41*T)&255,
                   (73*X+19*Y+17*C+31*Z+47*T)&255,
                   (255-X+53*C+59*Z+61*T)&255), axis=-1).astype(np.uint8)


def step(lo=0., hi=255., channels=7, selection=None, whole=False):
    value = ij.RgbContrastSessionStep()
    value.options = rgb_options(lo, hi, channels)
    value.selection = selection
    value.whole_plane = whole
    return value


def visit(position, stages):
    value = ij.RgbContrastSessionVisit()
    value.position = ij.PlanePosition(*position)
    value.stages = stages
    return value


def plan(visits, stack=False):
    value = ij.RgbContrastSessionPlan()
    value.visits = visits
    value.entire_stack = stack
    return value


def sha(array):
    return hashlib.sha256(array.tobytes()).hexdigest()


@pytest.mark.parametrize("row", STACK_FIXTURE["rows"], ids=lambda r:f"stack-r{r['range_index']}-c{r['channels']}-roi{r['roi']}-a{r['anchor']}-p{int(r['prefix'])}-w{int(r['whole_plane'])}")
def test_session_final_stack_matches_all_actual_imagej_single_visit_references(row):
    owner = ij.Document(ij.Dataset(STACK_PIXELS, axes="TCZYXS"))
    before = owner.snapshot()
    selected = rgb_stack_selection(row["roi"])
    stages = []
    if row["prefix"]:
        stages.append(step(*STACK_FIXTURE["prefix_range"], 7, selected))
    stages.append(step(*STACK_FIXTURE["ranges"][row["range_index"]], row["channels"], selected, row["whole_plane"]))
    anchor = STACK_FIXTURE["anchors"][row["anchor"]]
    request = plan([visit(anchor, stages)], stack=True)
    result = ij.prepare_rgb_contrast_session(before, request)
    values = result.preview().dataset.numpy("TCZYXS")
    assert sha(values) == row["stack_sha256"]
    assert sha(values[tuple(anchor)]) == row["active_sha256"]
    assert owner.snapshot().shares_data(before) and owner.undo_steps == 0


@pytest.mark.parametrize("row", REFERENCE["cases"], ids=lambda r:f"axis{r['axis']}-from{r['origin']}-c{r['channels']}-roi{r['roi']}")
def test_all_actual_imagej_visit_selection_reset_and_return_checkpoints(row):
    reads = ij.DatasetReadObserver()
    owner = ij.Document(ij.Dataset(PIXELS, axes="TCZYXS").observe_reads(reads))
    before = owner.snapshot()
    a = [row["origin"]]*3
    b = a.copy()
    b[row["axis"]] = 1-row["origin"]
    assert sha(PIXELS[tuple(a)]) == row["original_a_sha256"]
    assert sha(PIXELS[tuple(b)]) == row["original_b_sha256"]
    channels = row["channels"]
    first = rgb_stack_selection(row["roi"])
    selected = rgb_stack_selection(row["changed_roi"])
    identity = step(channels=channels, selection=selected)
    mapped_a = step(20.25, 200.75, channels, selected)
    mapped_b = step(-30.25, 400.5, channels, selected)

    def evaluate(visits, at, key, full=False):
        request = plan(visits)
        start = reads.counts().plane_reads
        estimate = ij.estimate_rgb_contrast_session(before, request)
        assert reads.counts().plane_reads == start
        assert estimate.visits == len(visits) and estimate.stages == len(visits)
        edit = ij.prepare_rgb_contrast_session(before, request)
        assert reads.counts().plane_reads-start <= len({tuple((v.position.t, v.position.c, v.position.z)) for v in visits})
        output = edit.preview().dataset.numpy("TCZYXS")
        assert sha(output if full else output[tuple(at)]) == row[key+"_sha256"]
        assert owner.snapshot().shares_data(before) and owner.undo_steps == 0
        assert edit.preview().selection == before.selection
        assert edit.preview().display.t == before.display.t and edit.preview().display.c == before.display.c
        assert edit.preview().display.crosshair == before.display.crosshair
        return edit

    evaluate([visit(a, [step(10.9, 40.1, channels, first)])], a, "first_map_a")
    evaluate([visit(a, [mapped_a])], a, "changed_selection_map_a")
    evaluate([visit(a, [identity])], a, "original_a")
    evaluate([visit(a, [mapped_a]), visit(b, [identity])], b, "original_b")
    evaluate([visit(a, [mapped_a]), visit(b, [mapped_b])], b, "mapped_b")
    previous = [visit(a, [mapped_a]), visit(b, [mapped_b])]
    evaluate(previous+[visit(a, [identity])], a, "return_reset_a")
    returned = previous+[visit(a, [step(-10.2, 160.9, channels, selected)])]
    evaluate(returned, a, "return_remap_a")
    evaluate(returned, a, "remapped_stack", full=True)
    edit = evaluate(previous+[visit(a, [identity])], a, "reset_stack", full=True)
    owner.commit(edit)
    assert sha(owner.snapshot().dataset.numpy("TCZYXS")) == row["reset_stack_sha256"]
    assert owner.undo_steps == int(row["changed_roi"] != 3)
    if owner.undo_steps:
        owner.undo()
        assert owner.snapshot().shares_data(before)
        owner.redo()
        assert sha(owner.snapshot().dataset.numpy("TCZYXS")) == row["reset_stack_sha256"]


@pytest.mark.parametrize("failure", ["empty", "empty_visit", "position", "channels", "pixels", "memory", "metadata", "late_geometry", "pre_cancel"])
def test_session_admission_and_late_geometry_fail_before_source_reads(failure):
    reads = ij.DatasetReadObserver()
    owner = ij.Document(ij.Dataset(PIXELS, axes="TCZYXS").observe_reads(reads))
    before = owner.snapshot()
    request = plan([visit([0, 0, 0], [step(10., 40.)]), visit([1, 1, 1], [step(20., 80.)])])
    limits = ij.ContrastApplyCommandLimits()
    token = ij.CancellationToken()
    if failure == "empty": request.visits = []
    elif failure == "empty_visit": request.visits = [visit([0, 0, 0], [])]
    elif failure == "position": request.visits = [visit([2, 0, 0], [step()])]
    elif failure == "channels": request.visits = [visit([0, 0, 0], [step(channels=0)])]
    elif failure == "pixels": limits.processing.max_pixels = 4095
    elif failure == "memory": limits.processing.max_working_bytes = 1
    elif failure == "metadata": limits.max_record_bytes = 1
    elif failure == "late_geometry":
        request.visits = [request.visits[0], visit([1, 1, 1], [step(selection=ij.Selection("line", [(-2e10, 0), (2e10, 1)]))])]
    elif failure == "pre_cancel": token.cancel()
    with pytest.raises((ValueError, ij.OperationCancelled)):
        ij.prepare_rgb_contrast_session(before, request, limits, cancellation=token)
    assert reads.counts().plane_reads == 0 and owner.snapshot().shares_data(before) and owner.undo_steps == 0


@pytest.mark.parametrize("field", ["step", "plan"])
@pytest.mark.parametrize("bad", [0, 1, "yes", None])
def test_session_boolean_controls_refuse_coercion(field, bad):
    with pytest.raises(TypeError):
        if field == "step": step(whole=bad)
        else: plan([], stack=bad)


def test_session_callback_cannot_mutate_captured_intent_and_cancel_is_private():
    owner = ij.Document(ij.Dataset(PIXELS, axes="TCZYXS"))
    before = owner.snapshot()
    request = plan([visit([0, 0, 0], [step(10.9, 40.1)])])
    expected = ij.prepare_rgb_contrast_session(before, request).preview().dataset.numpy("TCZYXS")
    limits = ij.ContrastApplyCommandLimits()
    called = []

    def mutate():
        called.append(True)
        request.visits = []
        limits.processing.max_pixels = 0

    result = ij.prepare_rgb_contrast_session(before, request, limits, checkpoint=mutate)
    assert called
    np.testing.assert_array_equal(result.preview().dataset.numpy("TCZYXS"), expected)
    token = ij.CancellationToken()
    with pytest.raises(ij.OperationCancelled):
        ij.prepare_rgb_contrast_session(before, plan([visit([0, 0, 0], [step(10., 40.)])]),
                                        cancellation=token, progress=lambda *_: token.cancel())
    assert owner.snapshot().shares_data(before) and owner.undo_steps == 0


def test_session_publication_rejects_stale_navigation():
    owner = ij.Document(ij.Dataset(PIXELS, axes="TCZYXS"))
    before = owner.snapshot()
    edit = ij.prepare_rgb_contrast_session(before, plan([visit([0, 0, 0], [step(10., 40.)])]))
    view = before.display
    view.crosshair = [0, 0, 1]
    owner.set_display(view)
    stable = owner.snapshot()
    with pytest.raises(ij.RevisionConflict): owner.commit(edit)
    assert owner.snapshot().shares_data(stable) and owner.undo_steps == 0
