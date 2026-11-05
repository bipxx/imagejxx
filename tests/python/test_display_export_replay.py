"""Accepted display export records, replay and explicit recovery."""
import copy
import json
import os
import time
from pathlib import Path
import numpy as np
import pytest
import imagejxx as ij
from imagejxx import _desktop
from test_roi_style import selection_record


def action(editor, name, **values):
    editor.action(name, json.dumps(values))


def fixture(tmp_path):
    pixels = np.arange(2*48*64, dtype=np.uint16).reshape(2, 48, 64) % 256
    pixels[1] = (pixels[1]+51) % 256
    reads = ij.DatasetReadObserver()
    owner = ij.Document(ij.Dataset(pixels.astype(np.uint8), axes="ZYX").observe_reads(reads))
    point = ij.Selection("multipoint", [(12, 12), (32, 24)])
    point = ij.with_point_annotations(point, [ij.PointAnnotation(0, ij.PlanePosition(0, 0, 1)), ij.PointAnnotation(2)])
    point = ij.with_point_style(point, ij.PointStyle())
    tx = owner.begin_edit();tx.set_overlays([{"kind":"text", "points":[[2,2]], "text":"A12", "font_size":9}, selection_record(point)]);owner.commit(tx)
    editor = _desktop.Editor(str(tmp_path/"preferences.json"));editor.add_native_document(owner)
    view = owner.snapshot().display;view.properties = {"display_range":[20,220], "lut":"Ice"};owner.set_display(view)
    return editor, owner, reads


def output(editor):
    info = json.loads(editor.document_infos())[-1]
    return editor.native_document(info["id"]).snapshot().dataset.numpy("YXS")


def capture(editor, name, tmp_path):
    editor.recording = True
    path = tmp_path/("captured.pdf" if name == "export_pdf" else "captured.png")
    action(editor, name, **({} if name == "flatten" else {"path":str(path)}))
    record = json.loads(editor.workflow)[-1]
    expected = output(editor).copy() if name == "flatten" else path.read_bytes()
    return record, expected


def ready(editor):
    deadline = time.monotonic()+5
    while editor.job_info.state in (ij.JobState.QUEUED, ij.JobState.RUNNING):
        assert time.monotonic()<deadline
        time.sleep(.001)
    assert editor.job_info.state == ij.JobState.READY


@pytest.mark.parametrize("name", ["flatten", "export_display", "export_pdf"])
def test_record_replay_restores_frozen_pose_and_options_without_changing_live_state(name, tmp_path):
    editor, owner, reads = fixture(tmp_path);record, expected = capture(editor, name, tmp_path)
    assert record["schema"] == "imagejxx.display-export-command" and record["version"] == 1
    assert record["source"]["document_id"] == owner.id and record["display"]["position"] == [0,0,0]
    assert record["display"]["properties"] == {"display_range":[20,220], "lut":"Ice"}
    assert record["font"]["policy"] == "reacquire" and record["file"]["publication"] == "replace"
    view = owner.snapshot().display;view.crosshair = [0,0,1];view.properties = {"display_range":[0,255], "lut":"Fire"};owner.set_display(view)
    editor.update_settings(json.dumps({"point_show_all":True, "show_rois":True, "print_dpi":300, "print_landscape":True}), False)
    before = reads.counts().plane_reads;original = owner.snapshot();n = len(json.loads(editor.workflow))
    replay_path = tmp_path/("replayed.pdf" if name == "export_pdf" else "replayed.png")
    action(editor, "replay_display_export", record=record, **({} if name=="flatten" else {"path":str(replay_path)}))
    actual = output(editor) if name=="flatten" else replay_path.read_bytes()
    if name=="flatten":np.testing.assert_array_equal(actual, expected)
    else:assert actual == expected
    assert not editor.busy and reads.counts().plane_reads == before+1
    assert len(json.loads(editor.workflow)) == n and owner.snapshot().shares_data(original)
    assert owner.snapshot().display.crosshair == [0,0,1] and owner.snapshot().display.properties == view.properties
    settings = json.loads(editor.settings())
    assert settings["point_show_all"] and settings["show_rois"]
    assert settings["print_dpi"] == 300 and settings["print_landscape"]
    assert json.loads(editor.render_memory_info())["working_bytes"] == 0


@pytest.mark.parametrize("name", ["flatten", "export_display", "export_pdf"])
@pytest.mark.parametrize("finish", ["publish", "cancel", "stale"])
def test_recorder_appends_only_at_successful_creator_publication(name, finish, tmp_path):
    editor, owner, reads = fixture(tmp_path);editor.recording = True
    destination = tmp_path/("out.pdf" if name=="export_pdf" else "out.png");destination.write_bytes(b"keep")
    action(editor, name, path=str(destination), asynchronous=True);ready(editor)
    assert json.loads(editor.workflow) == []
    editor.recording = False
    if finish=="cancel":assert editor.cancel_job(editor.job_info.id)
    elif finish=="stale":
        tx = owner.begin_edit();tx.set_title("changed");owner.commit(tx)
    editor.wait()
    assert len(json.loads(editor.workflow)) == (1 if finish=="publish" else 0)
    if finish!="publish":assert destination.read_bytes() == b"keep"
    assert not editor.busy and json.loads(editor.render_memory_info())["working_bytes"] == 0


def test_saved_workflow_and_session_rebind_use_current_admission(tmp_path):
    editor, owner, reads = fixture(tmp_path);record, expected = capture(editor, "flatten", tmp_path)
    workflow_path = tmp_path/"workflow.json";action(editor, "save_workflow", path=str(workflow_path))
    editor.workflow = "[]";action(editor, "load_workflow", path=str(workflow_path))
    assert json.loads(editor.workflow) == [record]
    action(editor, "run_workflow");editor.wait();np.testing.assert_array_equal(output(editor), expected)
    session = tmp_path/"workspace.ijx";action(editor, "save_workspace", path=str(session))
    reopened = _desktop.Editor(str(tmp_path/"reopened.json"));reopened.open_file(str(session))
    assert json.loads(reopened.workflow) == [record]
    with pytest.raises(ValueError, match="rebind"):action(reopened, "replay_display_export", record=record)
    reopened.active_index = 0
    action(reopened, "replay_display_export", record=record, rebind=True)
    np.testing.assert_array_equal(output(reopened), expected)
    reopened.active_index = 0;reopened.update_settings(json.dumps({"processing_memory_mb":1}),False)
    huge = copy.deepcopy(record);huge["font"]["default_path"] = "x"*131064
    before = len(json.loads(reopened.document_infos()))
    with pytest.raises((ValueError,RuntimeError), match="memory budget"):
        action(reopened, "replay_display_export", record=huge, rebind=True)
    assert len(json.loads(reopened.document_infos())) == before and not reopened.busy


def test_changed_source_requires_explicit_compatible_rebind(tmp_path):
    editor, owner, reads = fixture(tmp_path);record, expected = capture(editor, "flatten", tmp_path)
    tx = owner.begin_edit();tx.set_title("changed");owner.commit(tx);n = reads.counts().plane_reads
    with pytest.raises(ValueError, match="explicitly rebind"):action(editor, "replay_display_export", record=record)
    assert reads.counts().plane_reads == n
    action(editor, "replay_display_export", record=record, rebind=True, document_id=owner.id)
    np.testing.assert_array_equal(output(editor), expected)
    other = ij.Document(ij.Dataset(np.zeros((8,9),np.uint8), axes="YX"));editor.add_native_document(other)
    with pytest.raises(ValueError, match="shape/type"):
        action(editor, "replay_display_export", record=record, rebind=True)


@pytest.mark.parametrize("name", ["flatten", "export_display", "export_pdf"])
def test_missing_default_font_and_destination_can_recover_without_a_recorded_partial_step(name, tmp_path):
    editor, owner, reads = fixture(tmp_path);record, expected = capture(editor, name, tmp_path)
    missing = copy.deepcopy(record);missing["font"]["default_path"] = str(tmp_path/"missing.ttf")
    destination = tmp_path/("recovery.pdf" if name=="export_pdf" else "recovery.png");destination.write_bytes(b"keep")
    before = reads.counts().plane_reads;count = len(json.loads(editor.workflow))
    with pytest.raises((ValueError, RuntimeError)):
        action(editor, "replay_display_export", record=missing, **({} if name=="flatten" else {"path":str(destination)}))
    assert reads.counts().plane_reads == before and destination.read_bytes() == b"keep"
    assert len(json.loads(editor.workflow)) == count and json.loads(editor.render_memory_info())["working_bytes"] == 0
    editor.clear_error()
    action(editor, "replay_display_export", record=missing, font_path=record["font"]["default_path"], **({} if name=="flatten" else {"path":str(destination)}))
    if name=="flatten":np.testing.assert_array_equal(output(editor), expected)
    else:assert destination.read_bytes() == expected
    assert len(json.loads(editor.workflow)) == count


def mutate(record, key):
    if key=="version":record["version"] = 2
    elif key=="unknown":record["extra"] = True
    elif key=="id_bool":record["source"]["document_id"] = True
    elif key=="revision_float":record["source"]["revisions"][0] = 1.5
    elif key=="shape":record["source"]["shape"][4] = 0
    elif key=="position":record["display"]["position"][2] = 2
    elif key=="pixel_type":record["source"]["pixel_type"] = 5
    elif key=="property":record["display"]["properties"]["opaque"] = "x"
    elif key=="range":record["display"]["properties"]["display_range"] = [2,1]
    elif key=="lut":record["display"]["properties"]["custom_lut"] = [[1,2,3]]
    elif key=="boolean":record["render"]["show_rois"] = 1
    elif key=="dpi":record["file"]["dpi"] = 0
    elif key=="policy":record["font"]["policy"] = "embedded"
    elif key=="nul":record["font"]["default_path"] = "a\0b"
    elif key=="huge":record["font"]["default_path"] = "x"*131065


@pytest.mark.parametrize("bad", ["version","unknown","id_bool","revision_float","shape","position","pixel_type","property","range","lut","boolean","dpi","policy","nul","huge"])
def test_malformed_capture_refuses_before_a_job_or_source_read(bad, tmp_path):
    editor, owner, reads = fixture(tmp_path);record, _ = capture(editor, "flatten", tmp_path)
    mutate(record,bad);before = reads.counts().plane_reads;count = len(editor.jobs)
    with pytest.raises((ValueError,RuntimeError)):
        action(editor, "replay_display_export", record=record)
    assert reads.counts().plane_reads == before and not editor.busy and len(editor.jobs) == count
    assert len(json.loads(editor.workflow)) == 1 and json.loads(editor.render_memory_info())["working_bytes"] == 0


@pytest.mark.parametrize("host", ["cpp", "python"])
def test_actual_export_record_replay_and_rebind_controls(host):
    path = os.environ.get("IMAGEJXX_POINT_ANNOTATIONS_"+host.upper()+"_REPORT")
    if not path:pytest.skip("Requires actual export workflow input capture")
    report = json.loads(Path(path).read_bytes());state = report["export_replay"]
    assert state["record_checkbox"] and state["record_count"] == 1
    assert state["saved_loaded_equal"] and state["source_unchanged"]
    assert state["run_equal"] and state["review_equal"] and state["review_submitted"]
    assert state["record"]["schema"] == "imagejxx.display-export-command"
    assert len(state["controls"]) >= 4 and len(report["frames"]) < 180


def test_replay_resolves_relative_annotation_fonts_from_the_accepted_directory(tmp_path, monkeypatch):
    editor, owner, reads = fixture(tmp_path);record, _ = capture(editor, "flatten", tmp_path)
    font = Path(record["font"]["default_path"])
    if not font.is_absolute():font = Path(record["font"]["directory"])/font
    rows = owner.snapshot().overlays;rows[0]["font_path"] = os.path.relpath(font, Path.cwd())
    tx = owner.begin_edit();tx.set_overlays(rows);owner.commit(tx)
    editor.active_index = 0;editor.workflow = "[]"
    recorded, expected = capture(editor, "flatten", tmp_path)
    other = tmp_path/"other-cwd";other.mkdir();monkeypatch.chdir(other)
    action(editor, "replay_display_export", record=recorded)
    np.testing.assert_array_equal(output(editor), expected)
    assert len(json.loads(editor.workflow)) == 1 and json.loads(editor.render_memory_info())["working_bytes"] == 0


@pytest.mark.parametrize("name", ["export_display", "export_pdf"])
def test_replay_destination_failure_is_recoverable_without_history_or_recorder_changes(name, tmp_path):
    editor, owner, reads = fixture(tmp_path);record, expected = capture(editor, name, tmp_path)
    before = owner.snapshot();undo = owner.undo_steps;count = len(json.loads(editor.workflow))
    missing = tmp_path/"missing"/("out.pdf" if name=="export_pdf" else "out.png")
    with pytest.raises((ValueError, RuntimeError)):
        action(editor, "replay_display_export", record=record, path=str(missing))
    assert not missing.exists() and not list(tmp_path.rglob("*.tmp.*")) and owner.snapshot().shares_data(before)
    assert owner.undo_steps == undo and len(json.loads(editor.workflow)) == count
    assert json.loads(editor.render_memory_info())["working_bytes"] == 0
    editor.clear_error();destination = tmp_path/("recovered.pdf" if name=="export_pdf" else "recovered.png")
    action(editor, "replay_display_export", record=record, path=str(destination))
    assert destination.read_bytes() == expected and len(json.loads(editor.workflow)) == count
