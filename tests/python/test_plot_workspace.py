"""Installed workspace consumers for detached numeric plots and real controls."""
import copy
import json
import os
from pathlib import Path

import numpy as np
import pytest
import imagejxx as ij
from imagejxx.ui.app import Editor

KEY = "retained_numeric_plots"


def make_plots(tmp_path):
    owner = ij.Document(ij.Dataset(np.array([[0, 32768, 65535]], np.uint16), axes="YX"), "Workspace source")
    editor = Editor(preferences=tmp_path / "prefs.json")
    editor.add_native_document(owner)
    editor.action("set_intensity_calibration", record=ij.ImageJIntensityCalibration.signed16("HU workspace").record, show_plot=True)
    editor.action("keep_plot_snapshot")
    editor.action("keep_plot_snapshot", plot_id=editor.plot_snapshots_info[0]["plot_id"])
    second = editor.plot_snapshots_info[1]["plot_id"]
    editor.action("set_plot_limits", plot_id=second, limits=[100, 200, -50, 50], nice_ticks=True, minor_ticks=True)
    editor.action("set_plot_style", plot_id=second, line_width=3, curve_color="#00ff00")
    editor.action("fit_intensity_calibration", function="straight_line", measured=[0, 1], known=[2, 4], show_plot=True)
    editor.wait()
    editor.action("keep_plot_snapshot")
    editor.select({"kind": "line", "points": [[0, 0], [2, 0]]})
    editor.action("profile")
    editor.wait()
    editor.action("keep_plot_snapshot")
    editor.action("select_none")
    editor.action("histogram", bins=4)
    editor.action("keep_plot_snapshot")
    return owner, editor


def csvs(editor, tmp_path, name):
    output = []
    for index, info in enumerate(editor.plot_snapshots_info):
        path = tmp_path / f"{name}-{index}.csv"
        editor.action("export_plot_csv", plot_id=info["plot_id"], path=str(path))
        output.append(path.read_bytes())
    return output


PLOT_FIELDS = ("kind", "function", "value_unit", "measured", "known", "curve_x", "curve_y", "series_x", "series_y", "bin_edges", "bin_counts", "limits", "view_limits", "viewport", "log_x", "log_y", "grid", "ticks", "nice_ticks", "minor_ticks", "annotation", "style", "x_label", "y_label", "source_title", "context_caption")


def semantic_plots(rows):
    # IDs are new runtime identities; sample values, units and presentation are exact.
    return [{key: row["plot"][key] for key in PLOT_FIELDS if key in row["plot"]} for row in rows]


@pytest.mark.parametrize("startup", [False, True], ids=["open", "startup"])
@pytest.mark.parametrize("plot_only", [False, True], ids=["with-document", "plot-only"])
def test_all_kinds_share_and_reopen(startup, plot_only, tmp_path):
    owner, editor = make_plots(tmp_path)
    expected_csv = csvs(editor, tmp_path, "before")
    expected_plots = semantic_plots(editor.render_headless(4)["plot_snapshots"])
    if plot_only:
        editor.close_document(discard=True)
    before, history = owner.snapshot(), owner.undo_steps
    path = tmp_path / "plots.ijx"
    editor.action("save_workspace", path=str(path))
    assert owner.snapshot().shares_data(before) and owner.undo_steps == history
    saved = ij.load_session(str(path))
    assert len(saved.documents) == (0 if plot_only else 1)
    record = saved.extensions[KEY]
    assert record["schema"] == "imagejxx.retained-numeric-plots" and record["version"] == 1
    assert len(record["payloads"]) == 4 and len(record["windows"]) == 5
    assert record["windows"][0]["payload"] == record["windows"][1]["payload"]
    if startup:
        restored = Editor(preferences=tmp_path / "restored.json", workspace=path)
    else:
        restored = Editor(preferences=tmp_path / "restored.json")
        restored.open_file(path)
    assert csvs(restored, tmp_path, "after") == expected_csv
    observed = restored.render_headless(4)
    assert semantic_plots(observed["plot_snapshots"]) == expected_plots
    infos = restored.plot_snapshots_info
    assert [info["kind"] for info in infos] == ["calibration_table", "calibration_table", "fit", "profile", "histogram"]
    assert infos[0]["payload_id"] == infos[1]["payload_id"]
    assert KEY not in observed["session_extensions"]  # no duplicate runtime JSON plot store
    while restored.documents:
        restored.close_document(discard=True)
    assert csvs(restored, tmp_path, "detached") == expected_csv
    for info in reversed(restored.plot_snapshots_info):
        restored.action("close_plot_snapshot", plot_id=info["plot_id"])
    assert restored.plot_snapshots_info == []
    assert restored.render_memory_info["retained_bytes"] == 0
    assert restored.render_memory_info["working_bytes"] == 0


@pytest.mark.parametrize("mutation", ["version", "kind", "index", "boolean-index", "negative-count", "curve-length", "style", "view", "unknown-field"])
def test_malformed_workspace_keeps_current_publication(mutation, tmp_path):
    owner, editor = make_plots(tmp_path)
    valid = tmp_path / "valid.ijx"
    editor.action("save_workspace", path=str(valid))
    saved = ij.load_session(str(valid))
    extensions = copy.deepcopy(saved.extensions)
    record = extensions[KEY]
    if mutation == "version": record["version"] = 2
    if mutation == "kind": record["payloads"][0]["kind"] = "future"
    if mutation == "index": record["windows"][0]["payload"] = 99
    if mutation == "boolean-index": record["windows"][0]["payload"] = True
    if mutation == "negative-count": record["payloads"][3]["histogram"]["counts"][0] = -1
    if mutation == "curve-length": record["payloads"][0]["data"]["curve_y"].append(0)
    if mutation == "style": record["windows"][0]["style"]["line_width"] = 0
    if mutation == "view": record["windows"][0]["view"]["fractions"] = [1, 0, 0, 1]
    if mutation == "unknown-field": record["extra"] = True
    bad = tmp_path / "bad.ijx"
    ij.save_session(str(bad), ij.SessionCapture(saved.documents, results=saved.results, workflow=saved.workflow, extensions=extensions))
    source, steps = owner.snapshot(), owner.undo_steps
    infos, rows, workflow = editor.plot_snapshots_info, editor.results, editor.workflow
    memory, documents = editor.render_memory_info, len(editor.documents)
    with pytest.raises((ValueError, RuntimeError), match="plot workspace"):
        editor.open_file(bad)
    assert owner.snapshot().shares_data(source) and owner.undo_steps == steps
    assert editor.plot_snapshots_info == infos and len(editor.documents) == documents
    assert editor.results == rows and editor.workflow == workflow
    assert editor.render_memory_info["retained_bytes"] == memory["retained_bytes"]
    assert editor.render_memory_info["working_bytes"] == 0


def test_repeated_open_and_old_workspace_keep_existing_snapshots(tmp_path):
    _, editor = make_plots(tmp_path)
    path = tmp_path / "saved.ijx"
    editor.action("save_workspace", path=str(path))
    ids = [info["plot_id"] for info in editor.plot_snapshots_info]
    editor.open_file(path)
    first = editor.plot_snapshots_info
    assert len(first) == 10 and min(info["plot_id"] for info in first[5:]) > max(ids)
    editor.open_file(path)
    second = editor.plot_snapshots_info
    assert len(second) == 15 and min(info["plot_id"] for info in second[10:]) > max(info["plot_id"] for info in first)
    old = tmp_path / "old.ijx"
    ij.save_session(str(old), ij.SessionCapture([], extensions={"unrelated": "preserved"}))
    editor.open_file(old)
    assert editor.plot_snapshots_info == second
    again = tmp_path / "again.ijx"
    editor.action("save_workspace", path=str(again))
    assert ij.load_session(str(again)).extensions["unrelated"] == "preserved"


def assert_host_report(report, context):
    assert report["contract"] == "retained-plot-workspace-ui.001" and report["context"] == context
    assert report["state"] == "observed" and not report["acceptance_claim"] and not report["error"]
    assert report["source_released"] is True and report["read_only_save_source"] is True
    assert report["document_count"] == 0
    assert len(report["before_info"]) == len(report["after_info"]) == 5
    assert report["before_csv"] == report["after_csv"]
    assert semantic_plots(report["before_plots"]) == semantic_plots(report["after_plots"])
    assert report["after_info"][0]["payload_id"] == report["after_info"][1]["payload_id"]
    assert min(row["plot_id"] for row in report["after_info"]) > max(row["plot_id"] for row in report["before_info"])
    assert report["final_info"] == []
    for key in ["memory_closed", "memory_final"]:
        assert report[key]["retained_bytes"] == report[key]["working_bytes"] == 0
    for frame in report["frames"]:
        assert frame["events"] == frame["input_frame"]["raw_inputs"]
    names = {control["name"] for control in report["controls"]}
    assert {"keep_table", "duplicate_table", "table_style", "style_apply", "keep_fit", "keep_profile", "keep_histogram", "save_workspace", "open_workspace", "close_restored"} <= names


def test_actual_python_plot_workspace(tmp_path):
    from imagejxx import _desktop
    editor = _desktop.Editor(str(tmp_path / "prefs.json"))
    assert_host_report(json.loads(editor._plot_workspace_ui_case(str(tmp_path / "actual"))), "imgui_python")


def test_standalone_plot_workspace_report():
    assert_host_report(json.loads(Path(os.environ["IMAGEJXX_PLOT_WORKSPACE_CPP_REPORT"]).read_bytes()), "imgui_cpp")
