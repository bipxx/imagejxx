"""Independent ImageJ combined promotion/navigation/Auto/stack oracle."""
import json
from pathlib import Path
import numpy as np
import pytest
import imagejxx as ij
from imagejxx.ui.app import Editor
from test_rgb_contrast_session import PIXELS, sha
from test_contrast_apply import rgb_stack_selection

FIXTURE = json.loads((Path(__file__).parents[1]/"fixtures/rgb-contrast-combined-imagej154r.json").read_bytes())


def case_id(row):
    return f"a{row['axis']}-o{row['origin']}-c{row['first_channels']}-{row['second_channels']}-{row['current_channels']}-r{row['roi']}-auto{row['automatic']}"


def position(owner, at):
    view = owner.snapshot().display
    view.t, view.c = at[:2]
    view.crosshair = [0, 0, at[2]]
    owner.set_display(view)


def setup(row):
    owner = ij.Document(ij.Dataset(PIXELS, axes="TCZYXS"))
    a = [row["origin"]]*3
    b = a.copy()
    b[row["axis"]] = 1-row["origin"]
    position(owner, a)
    owner.select(rgb_stack_selection(row["roi"]))
    return owner, a, b


@pytest.mark.parametrize("row", FIXTURE["cases"], ids=case_id)
def test_native_combined_history_auto_capture_workflow_worker_and_history(row):
    owner, a, b = setup(row)
    editor = ij.RgbContrastSessionEditor(owner.snapshot())
    editor.set_channels(row["first_channels"])

    def check(key, at=a, full=False):
        values = ij.execute_rgb_contrast_session_command(editor.preview_command()).preview().dataset.numpy("TCZYXS")
        assert sha(values if full else values[tuple(at)]) == row[key+"_sha256"]

    editor.set_range(*FIXTURE["ranges"]["first"])
    check("first_a")
    editor.set_channels(row["second_channels"])
    owner.select(rgb_stack_selection(row["changed_roi"]))
    editor.synchronize(owner.snapshot())
    editor.set_range(*FIXTURE["ranges"]["selected"])
    check("selected_a")
    editor.set_channels(row["current_channels"])
    position(owner, b)
    editor.synchronize(owner.snapshot())
    owner.select(rgb_stack_selection(row["other_roi"]))
    editor.synchronize(owner.snapshot())
    editor.set_range(*FIXTURE["ranges"]["other"])
    check("mapped_b", b)
    position(owner, a)
    editor.synchronize(owner.snapshot())
    editor.set_range(*FIXTURE["ranges"]["replaced_return_draft"])
    check("return_draft_a")
    editor.reset()
    check("return_reset_a")
    if row["automatic"]:
        baseline = ij.execute_rgb_contrast_session_command(editor.preview_command()).preview()
        request = ij.TargetRequest(roi=ij.RoiPolicy.ACTIVE_SELECTION if row["other_roi"] in (1, 2, 3, 8) else ij.RoiPolicy.WHOLE_PLANE)
        histogram = ij.accumulate_rgb_histogram(ij.resolve_target(baseline, request))
        decision = ij.rgb_auto_contrast_from_histogram(histogram)
        assert decision.range == [row["minimum"], row["maximum"]]
        assert decision.cycle.next == row["auto_threshold"]
    editor.set_range(row["minimum"], row["maximum"], whole_plane=bool(row["automatic"]))
    check("final_a")
    check("preview_stack", full=True)
    editor.set_entire_stack(True)
    command = editor.command
    assert len(command.plan.visits) == 3 and len(command.plan.visits[0].stages) == 3
    before = owner.snapshot()
    np.testing.assert_array_equal(before.dataset.numpy("TCZYXS"), PIXELS)
    record = json.loads(json.dumps(command.record()))
    strict = ij.rgb_contrast_session_command_from_record(before, record)
    program = ij.NativeWorkflowProgram.from_json(ij.NativeWorkflowProgram([ij.make_rgb_contrast_session_workflow_step(strict)]).json())
    prepared = ij.prepare_native_workflow(before, program.steps)
    assert sha(prepared.preview.dataset.numpy("TCZYXS")) == row["applied_stack_sha256"]
    app = ij.Application()
    app.adopt(owner)
    history = owner.undo_steps
    try:
        job = ij.submit_rgb_contrast_session(app.job_scheduler, strict)
        assert job.wait(5000)
        assert sha(job.preview.dataset.numpy("TCZYXS")) == row["applied_stack_sha256"]
        job.publish(app)
        assert owner.undo_steps == history+1
        assert owner.snapshot().selection == before.selection
        assert owner.snapshot().display.crosshair == before.display.crosshair
        owner.undo()
        assert owner.snapshot().shares_data(before)
        owner.redo()
        assert sha(owner.snapshot().dataset.numpy("TCZYXS")) == row["applied_stack_sha256"]
    finally:
        app.job_scheduler.shutdown()


def _panel_trace(row, tmp_path, physical=False):
    owner, a, b = setup(row)
    editor = Editor(preferences=tmp_path/"prefs.json")
    editor._editor.add_virtual_document(owner)

    def click(name):
        editor.action("show_contrast")
        editor.action("wait_jobs")
        panel = editor.render_headless(frames=2)["contrast_panel"]
        control = panel["controls"][name]
        assert control["visible"] and control["enabled"]
        editor.render_headless(frames=2, events=[
            {"type": "action", "name": "show_contrast", "frames": 2},
            {"type": "position", "space": "screen", "point": control["point"]},
            {"type": "button", "name": "left", "down": True},
            {"type": "button", "name": "left", "down": False}])
        editor.action("wait_jobs")

    def channels(value):
        if physical:
            click(f"rgb_components_{value}")
            assert editor.render_headless()["contrast_panel"]["rgb"]["channels"] == value
        else:
            editor.action("contrast_rgb_channels", channels=value)

    def mapping(name):
        low, high = FIXTURE["ranges"][name]
        editor.action("contrast_range", minimum=low, maximum=high)
        editor.action("wait_jobs")

    def check(key):
        editor.action("show_channel_display")
        editor.action("wait_jobs")
        report = editor.render_headless(frames=2)
        readback = report["ordinary_pixels"]["readback"]
        assert readback["available"]
        assert sha(np.array(readback["source"]["pixels"], np.uint8)) == row[key+"_sha256"]
        return report["contrast_panel"]

    try:
        editor.action("show_contrast")
        if physical: click("rgb_stack")
        else: editor.action("contrast_rgb_stack", enabled=True)
        channels(row["first_channels"])
        mapping("first")
        check("first_a")
        channels(row["second_channels"])
        owner.select(rgb_stack_selection(row["changed_roi"]))
        editor.action("wait_jobs")
        mapping("selected")
        check("selected_a")
        channels(row["current_channels"])
        position(owner, b)
        editor.action("wait_jobs")
        owner.select(rgb_stack_selection(row["other_roi"]))
        editor.action("wait_jobs")
        mapping("other")
        check("mapped_b")
        position(owner, a)
        editor.action("wait_jobs")
        mapping("replaced_return_draft")
        check("return_draft_a")
        editor.action("contrast_reset")
        check("return_reset_a")
        if row["automatic"]:
            editor.action("contrast_auto")
            editor.action("wait_jobs")
        else:
            mapping("final")
        report = check("final_a")
        assert report["range"] == [row["minimum"], row["maximum"]]
        assert report["rgb"]["auto_divisor"] == row["auto_threshold"]
        np.testing.assert_array_equal(owner.snapshot().dataset.numpy("TCZYXS"), PIXELS)
        before = owner.snapshot()
        history = owner.undo_steps
        editor.recording = True
        if physical: click("apply")
        else: editor.action("contrast_apply")
        editor.action("wait_jobs")
        assert sha(owner.snapshot().dataset.numpy("TCZYXS")) == row["applied_stack_sha256"]
        record = editor.workflow[-1]
        assert record["schema"] == "imagejxx.rgb-contrast-session-command"
        assert len(record["visits"]) == 3 and len(record["visits"][0]["stages"]) == 3
        assert record["entire_stack"]
        assert record["visits"][-1]["stages"][-1]["whole_plane"] == bool(row["automatic"])
        assert owner.undo_steps == history+1 and owner.snapshot().selection == before.selection
        assert owner.snapshot().display.crosshair == before.display.crosshair
        owner.undo()
        assert owner.snapshot().shares_data(before)
        owner.redo()
        assert sha(owner.snapshot().dataset.numpy("TCZYXS")) == row["applied_stack_sha256"]
    finally:
        editor.action("close_contrast")


@pytest.mark.parametrize("row", FIXTURE["cases"], ids=case_id)
def test_shared_panel_combined_transitions_auto_and_final_stack_match_imagej(row, tmp_path):
    _panel_trace(row, tmp_path)


@pytest.mark.parametrize("row", [r for r in FIXTURE["cases"] if r["axis"] == 2 and r["origin"] == 0 and r["roi"] == 0], ids=case_id)
def test_component_stack_and_apply_widgets_match_combined_imagej(row, tmp_path):
    _panel_trace(row, tmp_path, physical=True)
