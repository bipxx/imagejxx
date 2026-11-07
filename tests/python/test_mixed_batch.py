"""One mixed sequence processes reviewed sources and preserves per-item output."""
import copy
import csv
import json
import os
from pathlib import Path
import time

import numpy as np
import pytest
import imagejxx as ij
from imagejxx.ui.app import Editor
from test_edit_analysis_chain import setup as edit_setup, pixels, TARGET
from test_workflow_editing import wrap


def setup(tmp_path):
    source, first, editor = edit_setup(tmp_path)
    second = ij.Document(ij.Dataset(np.array([[255, 0]], np.uint8), axes="YX"), "Reverse baseline")
    second.select(ij.Selection("line", [[0, 0], [1, 0]]))
    editor.add_native_document(second)
    original = editor.workflow
    editor.workflow = [wrap(original[4], False), wrap(original[0]), wrap(original[1], False),
                       wrap(original[2]), wrap(original[3]), wrap(original[4])]
    editor.active_index = 1
    sentinel = tmp_path / "manual.png"
    sentinel.write_bytes(b"disabled sentinel")
    choices = dict(rebind=True, restore_settings=True, gaussian_rebind=TARGET, items=[
        dict(document_id=second.id, export_options={"6": {"path": str(tmp_path / "second.png")}}),
        dict(document_id=first.id, export_options={"6": {"path": str(tmp_path / "first.png")}})])
    return source, first, second, editor, choices


def batch(editor):
    return editor.workflow_replay_info["batch"]


def wait_for_item(editor, ordinal):
    until = time.monotonic() + 5
    while time.monotonic() < until:
        if batch(editor)["active_item"] == ordinal:
            return
        editor.poll()
        time.sleep(.001)
    raise AssertionError("Batch did not reach requested item")


def test_ordered_batch_pixels_rows_plots_files_history_and_workspace(tmp_path):
    source, first, second, editor, choices = setup(tmp_path)
    original, before, old_rows = editor.workflow, source.snapshot(), editor.results
    editor.action("run_mixed_batch", **choices)
    assert batch(editor)["active_item"] == 1 and editor.workflow_replay_info["active_step"] == 2
    editor.wait()
    result = batch(editor)
    assert result["completed"] == result["total"] == 2 and result["active_item"] == 0
    assert [row["document_id"] for row in result["items"]] == [second.id, first.id]
    assert all(row["state"] == "completed" and row["completed"] == 4 for row in result["items"])
    np.testing.assert_array_equal(pixels(first), [[77, 178]])
    np.testing.assert_array_equal(pixels(second), [[178, 77]])
    assert first.undo_steps == second.undo_steps == 1 and editor.workflow == original
    assert source.snapshot().shares_data(before) and source.undo_steps == 2
    assert len(editor.results) == len(old_rows) + 2
    assert [row["Image"] for row in editor.results[-2:]] == ["Reverse baseline", "Edit clone"]
    assert (tmp_path / "manual.png").read_bytes() == b"disabled sentinel"
    assert (tmp_path / "first.png").is_file() and (tmp_path / "second.png").is_file()
    assert len(editor.plot_snapshots_info) == 2
    for item, expected in zip(result["items"], [[178, 77], [77, 178]]):
        assert len(item["profile_plots"]) == 1 and item["profile_plots"][0]["authored_step"] == 5
        path = tmp_path / (str(item["document_id"]) + ".csv")
        editor.action("export_plot_csv", plot_id=item["profile_plots"][0]["plot_id"], path=str(path))
        rows = list(csv.DictReader(path.read_text().splitlines()))
        assert [(float(r["x"]), float(r["y"])) for r in rows] == list(zip([0, 1], expected))
    editor.action("undo")
    np.testing.assert_array_equal(pixels(first), [[0, 255]])
    editor.action("redo")
    np.testing.assert_array_equal(pixels(first), [[77, 178]])
    editor.action("save_workspace", path=str(tmp_path / "batch.ijx"))
    reopened = Editor(preferences=tmp_path / "reopened.json", workspace=tmp_path / "batch.ijx")
    assert reopened.workflow == original and len(reopened.plot_snapshots_info) == 2
    assert editor.render_memory_info["working_bytes"] == 0


@pytest.mark.parametrize("mode", ["single-pixel", "repeated-profile"])
def test_batch_handles_last_pixel_and_readonly_profile_items(mode, tmp_path):
    _, first, second, editor, choices = setup(tmp_path)
    rows = editor.workflow
    editor.workflow = [rows[2] | {"enabled": True}] if mode == "single-pixel" else [rows[4], rows[4]]
    for item in choices["items"]: item.pop("export_options")
    editor.action("run_mixed_batch", **choices)
    editor.wait()
    assert editor.workflow_replay_info["state"] == "completed" and batch(editor)["completed"] == 2
    assert all(row["completed"] == (1 if mode == "single-pixel" else 2) for row in batch(editor)["items"])
    if mode == "single-pixel":
        np.testing.assert_array_equal(pixels(first), [[255, 0]])
        np.testing.assert_array_equal(pixels(second), [[0, 255]])
        assert first.undo_steps == second.undo_steps == 1
    else:
        assert first.undo_steps == second.undo_steps == 0
        assert len(editor.plot_snapshots_info) == 4
        assert all(len(row["profile_plots"]) == 2 for row in batch(editor)["items"])
    assert editor.render_memory_info["working_bytes"] == 0


def test_batch_flatten_publication_preserves_source_plan_and_profiles(tmp_path):
    _, first, second, editor, choices = setup(tmp_path)
    profile = editor.workflow[4]
    editor.recording = True
    editor.action("flatten")
    editor.wait()
    editor.recording = False
    flatten = editor.workflow[-1]
    editor.workflow = [profile, flatten]
    for item in choices["items"]: item.pop("export_options")
    count = len(editor.documents)
    editor.action("run_mixed_batch", **choices)
    editor.wait()
    assert editor.workflow_replay_info["state"] == "completed" and batch(editor)["completed"] == 2
    assert len(editor.documents) == count + 2 and len(editor.plot_snapshots_info) == 2
    assert first.undo_steps == second.undo_steps == 0 and editor.render_memory_info["working_bytes"] == 0
    np.testing.assert_array_equal(pixels(first), [[0, 255]])
    np.testing.assert_array_equal(pixels(second), [[255, 0]])


@pytest.mark.parametrize("mutation", ["empty", "duplicate", "id-bool", "missing-source", "unknown-root",
    "unknown-item", "no-consent", "consent-type", "missing-path", "collision", "relative",
    "missing-parent", "disabled-slot", "settings-type", "font-type"])
def test_batch_preflight_is_global_atomic_and_does_not_activate_sources(mutation, tmp_path):
    source, first, second, editor, choices = setup(tmp_path)
    choices = copy.deepcopy(choices)
    if mutation == "empty": choices["items"] = []
    if mutation == "duplicate": choices["items"][1]["document_id"] = second.id
    if mutation == "id-bool": choices["items"][1]["document_id"] = True
    if mutation == "missing-source": choices["items"][1]["document_id"] = 99999999
    if mutation == "unknown-root": choices["extra"] = 1
    if mutation == "unknown-item": choices["items"][0]["extra"] = 1
    if mutation == "no-consent": choices["rebind"] = False
    if mutation == "consent-type": choices["rebind"] = 1
    if mutation == "missing-path": choices["items"][1].pop("export_options")
    if mutation == "collision": choices["items"][1]["export_options"] = choices["items"][0]["export_options"]
    if mutation == "relative": choices["items"][1]["export_options"]["6"]["path"] = "relative.png"
    if mutation == "missing-parent": choices["items"][1]["export_options"]["6"]["path"] = str(tmp_path / "absent" / "out.png")
    if mutation == "disabled-slot": choices["items"][1]["export_options"]["1"] = {"path": str(tmp_path / "bad.png")}
    if mutation == "settings-type": choices["restore_settings"] = 1
    if mutation == "font-type": choices["items"][1]["export_options"]["6"]["font_path"] = False
    rows, steps = editor.results, editor.workflow
    errors = (IndexError,) if mutation == "missing-source" else (ValueError, RuntimeError)
    with pytest.raises(errors):
        editor.action("run_mixed_batch", **choices)
    assert editor.active_index == 1 and first.undo_steps == second.undo_steps == 0
    assert editor.results == rows and editor.workflow == steps and editor.render_memory_info["working_bytes"] == 0
    assert not (tmp_path / "second.png").exists() and not (tmp_path / "first.png").exists()


@pytest.mark.parametrize("endpoint", ["cancel-first", "future-title", "workflow", "recording", "budget", "cancel-second", "later-title"])
def test_batch_stop_preserves_commits_and_never_starts_later_sources(endpoint, tmp_path):
    _, first, second, editor, choices = setup(tmp_path)
    editor.action("run_mixed_batch", **choices)
    later = endpoint in ["cancel-second", "later-title"]
    if later: wait_for_item(editor, 2)
    if endpoint.startswith("cancel"): editor.cancel()
    if endpoint in ["future-title", "later-title"]:
        tx = first.begin_edit()
        tx.set_title("Changed pending source")
        first.commit(tx)
    if endpoint == "workflow": editor.workflow = []
    if endpoint == "recording": editor.recording = True
    if endpoint == "budget": editor.update_settings(processing_memory_mb=128, persist=False)
    if endpoint.startswith("cancel"): editor.wait()
    else:
        with pytest.raises(RuntimeError): editor.wait()
    result = batch(editor)
    assert result["completed"] == int(later)
    assert result["items"][0]["state"] == ("completed" if later else "canceled" if endpoint.startswith("cancel") else "failed")
    assert result["items"][1]["state"] == ("canceled" if endpoint == "cancel-second" else "failed" if later else "not_started")
    assert second.undo_steps == int(later) and first.undo_steps == int(endpoint in ["future-title", "later-title"])
    np.testing.assert_array_equal(pixels(first), [[0, 255]])
    assert not (tmp_path / "first.png").exists() and editor.render_memory_info["working_bytes"] == 0


def assert_host(report, context):
    assert report["contract"] == "mixed-batch-ui.001" and report["context"] == context
    assert report["state"] == "observed" and report["cancel_preserved"]
    assert report["first_pixels"] == [[77, 178]] and report["second_pixels"] == [[178, 77]]
    result = report["status"]["batch"]
    assert result["completed"] == result["total"] == 2 and result["active_item"] == 0
    assert [row["title"] for row in result["items"]] == ["Reverse baseline", "Edit clone"]
    assert all(row["state"] == "completed" and row["completed"] == 4 for row in result["items"])
    assert report["history_roundtrip"] and report["disabled_destination_protected"]
    assert report["final_working_bytes"] == 0 and report["visible_batch"]["read_only"]
    assert report["visible_batch"]["report"] == result and len(report["snapshots"]) == 2
    assert [row["Image"] for row in report["rows"]] == ["Edit source", "Reverse baseline", "Edit clone"]
    directory = Path(report["workflow"][0]["record"]["file"]["path"]).parent
    assert (directory / "disabled.png").read_bytes() == b"disabled"
    for name, expected in [("first", [77, 178]), ("second", [178, 77])]:
        rows = list(csv.DictReader((directory / (name + ".csv")).read_text().splitlines()))
        assert [(float(r["x"]), float(r["y"])) for r in rows] == list(zip([0, 1], expected))
        reader = Editor(preferences=directory / (name + "-reader.json"))
        reader.open_file(directory / (name + ".png"))
        restored = reader._editor.native_document(json.loads(reader._editor.document_infos())[0]["id"])
        # Captured display includes the active line across this two-pixel image.
        np.testing.assert_array_equal(restored.snapshot().dataset.numpy("YXS"), [[[255, 213, 75]] * 2])
    saved = ij.load_session(str(directory / "batch.ijx"))
    assert saved.workflow == report["workflow"] and saved.results == report["rows"]
    names = {row["name"] for row in report["controls"]}
    assert {"workflow.batch", "dialog.rebind", "dialog.apply", "dialog.cancel", "workflow.save", "workflow.load"} <= names
    assert any(name.startswith("dialog.source.") and name.endswith(".up") for name in names)
    assert any(name.startswith("dialog.batch_") and name.endswith("_6_path") for name in names)


def test_python_actual_mixed_batch(tmp_path):
    from imagejxx import _desktop
    editor = _desktop.Editor(str(tmp_path / "prefs.json"))
    assert_host(json.loads(editor._mixed_batch_ui_case(str(tmp_path / "actual"))), "imgui_python")


def test_standalone_actual_mixed_batch():
    assert_host(json.loads(Path(os.environ["IMAGEJXX_MIXED_BATCH_CPP_REPORT"]).read_bytes()), "imgui_cpp")
