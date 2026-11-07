"""Captured exports share ordered analysis jobs and atomic file publication."""
import copy
import json
import os
import zlib
from pathlib import Path

import numpy as np
import pytest
import imagejxx as ij
from imagejxx.ui.app import Editor
from test_mixed_replay import document, wait_for_step


def setup(tmp_path):
    owner = document()
    editor = Editor(preferences=tmp_path / "prefs.json")
    editor.add_native_document(owner)
    editor.update_settings(measurements=["Area", "mean", "Length"], profile_samples=3, persist=True)
    editor.recording = True
    png, pdf = tmp_path / "manual.png", tmp_path / "manual.pdf"
    for command, params in [("measure", {}), ("export_display", {"path": str(png)}),
                            ("profile", {}), ("export_pdf", {"path": str(pdf), "dpi": 96, "landscape": True}),
                            ("flatten", {})]:
        editor.action(command, **params)
        editor.wait()
    editor.recording = False
    editor.active_index = 0
    assert len(editor.workflow) == 5
    return owner, editor, png.read_bytes(), pdf.read_bytes()


def flat(editor):
    native = editor._editor
    info = json.loads(native.document_infos())[-1]
    return native.native_document(info["id"]).snapshot().dataset.numpy("YXS").copy()


def overrides(tmp_path):
    return {"2": {"path": str(tmp_path / "replay.png")}, "4": {"path": str(tmp_path / "replay.pdf")}}


@pytest.mark.parametrize("mode", ["default", "rebind", "reopen"])
def test_manual_outputs_intent_history_and_workspace(mode, tmp_path):
    owner, editor, png, pdf = setup(tmp_path)
    rows, steps, pixels = copy.deepcopy(editor.results), copy.deepcopy(editor.workflow), flat(editor)
    editor.action("export_plot_csv", path=str(tmp_path / "manual-profile.csv"))
    curve = (tmp_path / "manual-profile.csv").read_bytes()
    original, history = owner.snapshot(), owner.undo_steps
    editor.action("save_workflow", path=str(tmp_path / "workflow.json"))
    editor.workflow = []
    editor.action("load_workflow", path=str(tmp_path / "workflow.json"))
    if mode == "rebind":
        editor.add_native_document(document())
        editor.active_index = 2
    if mode == "reopen":
        editor.action("save_workspace", path=str(tmp_path / "workspace.ijx"))
        editor = Editor(preferences=tmp_path / "prefs.json", workspace=tmp_path / "workspace.ijx")
        editor.active_index = 0
    replay_source_index = editor.active_index
    editor.action("run_workflow", restore_settings=True, rebind=mode != "default", export_options=overrides(tmp_path))
    assert editor.busy
    editor.wait()
    assert editor.workflow_replay_info == dict(state="completed", completed=5, total=5, active_step=0, job_id=0, error="")
    assert editor.results == rows + rows and editor.workflow == steps
    assert (tmp_path / "replay.png").read_bytes() == png
    assert (tmp_path / "replay.pdf").read_bytes() == pdf
    # Flatten activates its new image. Live Profile actions require their source;
    # no GUI frame has discarded the deliberately source-bound live plot here.
    editor.active_index = replay_source_index
    editor.action("export_plot_csv", path=str(tmp_path / "replay-profile.csv"))
    assert (tmp_path / "replay-profile.csv").read_bytes() == curve
    np.testing.assert_array_equal(flat(editor), pixels)
    assert owner.snapshot().shares_data(original) and owner.undo_steps == history
    assert editor.render_memory_info["working_bytes"] == 0


def test_reviewed_recorded_destinations_and_export_only_sequence(tmp_path):
    _, editor, png, pdf = setup(tmp_path)
    steps = editor.workflow
    editor.workflow = [steps[1], steps[3]]
    frozen = copy.deepcopy(editor.workflow)
    (tmp_path / "manual.png").write_bytes(b"previous")
    (tmp_path / "manual.pdf").write_bytes(b"previous")
    editor.action("run_workflow", restore_settings=True)
    editor.wait()
    assert editor.workflow_replay_info["completed"] == 2 and editor.workflow == frozen
    assert (tmp_path / "manual.png").read_bytes() == png and (tmp_path / "manual.pdf").read_bytes() == pdf
    assert editor.render_memory_info["working_bytes"] == 0


def test_each_export_has_explicit_font_recovery_without_rewriting_intent(tmp_path):
    _, editor, png, pdf = setup(tmp_path)
    steps, options = editor.workflow, overrides(tmp_path)
    for index in [1, 3, 4]:
        key = str(index + 1)
        options.setdefault(key, {}).update(font_path=steps[index]["font"]["default_path"],
                                           font_directory=steps[index]["font"]["directory"])
        steps[index]["font"]["default_path"] = str(tmp_path / "unavailable.ttf")
    editor.workflow = steps
    editor.action("run_workflow", restore_settings=True, export_options=options)
    editor.wait()
    assert editor.workflow == steps and editor.workflow_replay_info["completed"] == 5
    assert (tmp_path / "replay.png").read_bytes() == png and (tmp_path / "replay.pdf").read_bytes() == pdf
    assert editor.render_memory_info["working_bytes"] == 0


@pytest.mark.parametrize("mutation", ["index", "leading-zero", "non-object", "unknown", "boolean", "relative", "empty", "pdf-suffix", "flatten-path", "duplicate", "version", "font-folder"])
def test_known_invalid_choices_refuse_before_any_output(mutation, tmp_path):
    owner, editor, png, pdf = setup(tmp_path)
    options, steps = overrides(tmp_path), editor.workflow
    if mutation == "index": options["1"] = {"path": str(tmp_path / "wrong.png")}
    if mutation == "leading-zero": options["02"] = options.pop("2")
    if mutation == "non-object": options = []
    if mutation == "unknown": options["2"]["asynchronous"] = False
    if mutation == "boolean": options["2"]["path"] = True
    if mutation == "relative": options["2"]["path"] = "relative.png"
    if mutation == "empty": options["2"]["path"] = ""
    if mutation == "pdf-suffix": options["4"]["path"] = str(tmp_path / "wrong.png")
    if mutation == "flatten-path": options["5"] = {"path": str(tmp_path / "flat.png")}
    if mutation == "duplicate": options["4"]["path"] = options["2"]["path"]; steps[3] = copy.deepcopy(steps[1])
    if mutation == "version": steps[3]["version"] = 99
    if mutation == "font-folder": options["2"]["font_directory"] = "relative"
    editor.workflow = steps
    before, rows, history = owner.snapshot(), editor.results, owner.undo_steps
    with pytest.raises((ValueError, RuntimeError)):
        editor.action("run_workflow", restore_settings=True, export_options=options)
    assert editor.results == rows and editor.workflow == steps and not editor.busy
    assert owner.snapshot().shares_data(before) and owner.undo_steps == history
    assert not (tmp_path / "replay.png").exists() and not (tmp_path / "replay.pdf").exists()
    assert editor.render_memory_info["working_bytes"] == 0


@pytest.mark.parametrize("endpoint", ["cancel", "close", "workflow", "visibility", "later-io"])
def test_export_terminal_preserves_committed_analysis_and_destination(endpoint, tmp_path):
    owner, editor, png, pdf = setup(tmp_path)
    rows, steps = copy.deepcopy(editor.results), copy.deepcopy(editor.workflow)
    options = overrides(tmp_path)
    destination = tmp_path / "replay.png"
    destination.write_bytes(b"keep")
    if endpoint == "later-io": options["2"]["path"] = str(tmp_path / "absent" / "no.png")
    editor.action("run_workflow", restore_settings=True, export_options=options)
    wait_for_step(editor, 1)
    if endpoint == "cancel": editor.cancel()
    if endpoint == "close": editor._editor.close_document(owner.id, True)
    if endpoint == "workflow": editor.workflow = []
    if endpoint == "visibility": editor.update_settings(show_rois=not editor.settings["show_rois"], persist=False)
    if endpoint in ["workflow", "visibility", "later-io"]:
        with pytest.raises(RuntimeError): editor.wait()
    else: editor.wait()
    status = editor.workflow_replay_info
    assert status["state"] in ["failed", "canceled"] and status["completed"] == 1 and status["active_step"] == 2
    assert editor.results == rows + rows and destination.read_bytes() == b"keep"
    assert editor.workflow == ([] if endpoint == "workflow" else steps)
    assert not (tmp_path / "replay.pdf").exists() and editor.render_memory_info["working_bytes"] == 0


def assert_host(report, context):
    assert report["contract"] == "analysis-export-chain-ui.001" and report["context"] == context
    assert report["state"] == "observed" and not report["acceptance_claim"]
    assert report["status"]["state"] == "completed" and report["status"]["completed"] == 5
    assert report["png_equal"] and report["pdf_equal"] and report["flatten_equal"] and report["intent_protected"]
    rebound = copy.deepcopy(report["manual_rows"])
    for row in rebound: row["Image"] = "Chain clone"
    assert report["rows"] == report["manual_rows"] * 2 + rebound
    assert report["final_working_bytes"] == 0
    assert report["review"]["read_only"] and "export_pdf" in report["review"]["text"]
    assert report["chosen_outputs"]["2"]["path"] != report["workflow"][1]["file"]["path"]
    names = {x["name"] for x in report["controls"]}
    assert {"workflow.record", "workflow.save", "workflow.load", "workflow.clear", "workflow.run", "dialog.export_2_path", "dialog.export_4_path", "dialog.apply", "dialog.cancel"} <= names
    assert all(x["control"]["enabled"] and x["control"]["id"] for x in report["controls"])
    from imagejxx import _desktop
    path = Path(report["chosen_outputs"]["2"]["path"])
    reader = _desktop.Editor(str(path.parent / "reader-prefs.json"))
    reader.open_file(str(path))
    identity = json.loads(reader.document_infos())[0]["id"]
    pixels = reader.native_document(identity).snapshot().dataset.numpy("YXS")
    assert pixels.shape == (12, 16, 3)
    np.testing.assert_array_equal(pixels[0, 0], [0, 0, 0])
    np.testing.assert_array_equal(pixels[-1, -1], [191, 191, 191])
    pdf = Path(report["chosen_outputs"]["4"]["path"]).read_bytes()
    start = pdf.index(b"stream\n") + 7
    end = pdf.index(b"\nendstream", start)
    np.testing.assert_array_equal(np.frombuffer(zlib.decompress(pdf[start:end]), dtype=np.uint8).reshape(pixels.shape), pixels)
    assert b"/MediaBox [0 0 792.000000 612.000000]" in pdf


def test_python_actual_chain(tmp_path):
    from imagejxx import _desktop
    editor = _desktop.Editor(str(tmp_path / "prefs.json"))
    assert_host(json.loads(editor._analysis_export_chain_ui_case(str(tmp_path / "actual"))), "imgui_python")


def test_standalone_actual_chain():
    assert_host(json.loads(Path(os.environ["IMAGEJXX_ANALYSIS_EXPORT_CPP_REPORT"]).read_bytes()), "imgui_cpp")
