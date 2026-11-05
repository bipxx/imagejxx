"""Aligned geometry edits and the shared point-properties draft lifecycle."""
import csv
import json
import os
from pathlib import Path

import imagejxx as ij
import numpy as np
import pytest

KEY = "imagejxx.point_annotations"
EXTENT = [2, 2, 2, 5, 7, 1]


def annotated(count=3):
    points = [[float(i), 1] for i in range(count)]
    plain = ij.Selection("point" if count == 1 else "multipoint", points,
        attributes={"opaque": {"keep": [True, "μ"]}})
    return ij.with_point_annotations(plain, [ij.PointAnnotation(i+1, ij.PlanePosition(1, 1, 1)) for i in range(count)])


@pytest.mark.parametrize("count", [1, 3])
@pytest.mark.parametrize("where", ["first", "last"])
def test_vertex_insert_delete_preserve_identity_and_default_new_entry(count, where):
    source = annotated(count)
    index = 0 if where == "first" else count
    editor = ij.SelectionEditor(source, (5, 7))
    inserted = editor.inserted_vertex(index, [4, 2])
    words = ij.imagej_point_counters(source, EXTENT)
    expected = words[:index] + [0] + words[index:]
    assert ij.imagej_point_counters(inserted, EXTENT) == expected
    assert inserted.attributes["opaque"] == source.attributes["opaque"]
    removed = ij.SelectionEditor(inserted, (5, 7)).removed_vertex(index)
    assert removed.points == source.points and removed.attributes == source.attributes
    assert editor.selection == source


def test_move_rotate_and_other_vertex_deletion_keep_annotations_aligned():
    source = annotated()
    removed = ij.SelectionEditor(source, (5, 7)).removed_vertex(1)
    assert [x.counter for x in ij.point_annotations(removed)] == [1, 3]
    moved = ij.SelectionEditor(source, (5, 7)).moved([1, 1])
    rotated = ij.SelectionEditor(moved, (5, 7)).rotated(90)
    assert ij.imagej_point_counters(rotated, EXTENT) == ij.imagej_point_counters(source, EXTENT)


def test_unknown_annotations_refuse_topology_change_without_touching_source():
    source = ij.Selection("multipoint", [[0, 0], [1, 1], [2, 2]],
        attributes={KEY: {"version": 2, "points": []}, "opaque": "kept"})
    editor = ij.SelectionEditor(source, (5, 7))
    with pytest.raises(ValueError):editor.inserted_vertex(1, [4, 2])
    with pytest.raises(ValueError):editor.removed_vertex(1)
    assert editor.selection == source


def setup(tmp_path, selection=None):
    from imagejxx import _desktop
    reads = ij.DatasetReadObserver()
    owner = ij.Document(ij.Dataset(np.zeros((2, 2, 2, 5, 7), np.uint8), axes="TCZYX").observe_reads(reads))
    owner.select(selection if selection is not None else annotated())
    editor = _desktop.Editor(str(tmp_path / "prefs.json"));editor.add_native_document(owner)
    return editor, owner, reads


def action(editor, command, **params):
    editor.action(command, json.dumps(params))


def test_scientific_point_properties_does_not_enable_legacy_area_route(tmp_path):
    editor, owner, reads = setup(tmp_path, ij.Selection("rectangle", [[0, 0], [2, 2]]))
    before = owner.snapshot();n = reads.counts().plane_reads
    with pytest.raises(ValueError, match="not supported"):
        action(editor, "show_selection_properties")
    assert owner.snapshot().shares_data(before) and reads.counts().plane_reads == n


def test_removed_selection_keeps_cancel_and_stale_apply_reachable(tmp_path):
    editor, owner, reads = setup(tmp_path)
    action(editor, "show_selection_properties")
    owner.select(None)
    before = owner.snapshot();n = reads.counts().plane_reads
    with pytest.raises(ij.RevisionConflict):
        action(editor, "apply_selection_properties")
    assert owner.snapshot().shares_data(before) and reads.counts().plane_reads == n
    owner.select(annotated())
    action(editor, "show_selection_properties")
    owner.select(None)
    action(editor, "cancel_selection_properties")
    assert owner.snapshot().selection is None


def test_properties_stage_cancel_apply_clear_store_and_native_session(tmp_path):
    from imagejxx import _desktop
    editor, owner, reads = setup(tmp_path)
    before, undo = owner.snapshot(), owner.undo_steps
    entries = [{"counter": 7, "position": [1, 0, 1]}, {"counter": 0, "position": None},
               {"counter": 7, "position": None}]
    n = reads.counts().plane_reads
    action(editor, "show_selection_properties")
    action(editor, "selection_properties_draft", point_annotations=entries)
    assert owner.snapshot().shares_data(before)
    action(editor, "cancel_selection_properties")
    assert owner.snapshot().shares_data(before)
    action(editor, "show_selection_properties")
    action(editor, "selection_properties_draft", point_annotations=entries)
    action(editor, "apply_selection_properties")
    assert reads.counts().plane_reads == n and owner.undo_steps == undo
    assert owner.snapshot().dataset.shares_pixels(before.dataset)
    assert [x.counter for x in ij.point_annotations(owner.snapshot().selection)] == [7, 0, 7]
    action(editor, "add_roi", name="edited")
    stored = owner.snapshot().rois[0].selection
    path = tmp_path / "point.ijx";action(editor, "save_document", path=str(path))
    restored = _desktop.Editor(str(tmp_path / "reopen.json"));restored.open_file(str(path))
    snapshot = restored.native_document(json.loads(restored.document_infos())[0]["id"]).snapshot()
    assert snapshot.selection.attributes == stored.attributes == snapshot.rois[0].selection.attributes
    action(editor, "show_selection_properties")
    action(editor, "selection_properties_draft", point_annotations=None)
    action(editor, "apply_selection_properties")
    assert KEY not in owner.snapshot().selection.attributes and owner.snapshot().rois[0].selection == stored


@pytest.mark.parametrize("entries", [[], [{"counter": True, "position": None}]*3,
    [{"counter": 100, "position": None}]*3, [{"counter": 1, "position": [2, 0, 0]}]*3,
    [{"counter": 1, "position": [0, 0]}]*3,
    [{"counter": "x"*100000, "position": None}]*3])
def test_invalid_annotation_patch_is_atomic(entries, tmp_path):
    editor, owner, reads = setup(tmp_path)
    before = owner.snapshot();n = reads.counts().plane_reads
    action(editor, "show_selection_properties")
    with pytest.raises(ValueError):action(editor, "selection_properties_draft", point_annotations=entries)
    assert owner.snapshot().shares_data(before) and reads.counts().plane_reads == n
    action(editor, "cancel_selection_properties")


def test_bulk_geometry_requires_matching_annotations_or_explicit_clear(tmp_path):
    editor, owner, _ = setup(tmp_path);before = owner.snapshot()
    action(editor, "show_selection_properties")
    with pytest.raises(ValueError):action(editor, "selection_properties_draft", points=[[0, 0], [1, 1]])
    assert owner.snapshot().shares_data(before)
    action(editor, "selection_properties_draft", points=[[0, 0], [1, 1]],
        point_annotations=[{"counter": 8, "position": None}, {"counter": 0, "position": None}])
    action(editor, "apply_selection_properties")
    assert [x.counter for x in ij.point_annotations(owner.snapshot().selection)] == [8, 0]
    action(editor, "show_selection_properties")
    action(editor, "selection_properties_draft", points=[[2, 2]], point_annotations=None)
    action(editor, "apply_selection_properties")
    assert owner.snapshot().selection.points == [[2, 2]] and KEY not in owner.snapshot().selection.attributes


def test_navigation_or_selection_change_refuses_stale_apply(tmp_path):
    editor, owner, _ = setup(tmp_path)
    action(editor, "show_selection_properties")
    action(editor, "selection_properties_draft", point_annotations=None)
    view = owner.snapshot().display;view.t = 1;owner.set_display(view)
    before = owner.snapshot()
    with pytest.raises(ij.RevisionConflict):action(editor, "apply_selection_properties")
    assert owner.snapshot().shares_data(before)


def test_invalid_position_is_safe_to_show_and_explicitly_clear(tmp_path):
    selected = ij.with_point_annotations(ij.Selection("point", [[1, 1]]),
        [ij.PointAnnotation(1, ij.PlanePosition(2147483647, 0, 0))])
    editor, owner, _ = setup(tmp_path, selected)
    action(editor, "show_selection_properties")
    panel = json.loads(editor.render_headless())["selection_properties"]
    assert panel["point_annotation_error"] and panel["control_states"]["Counter"]["disabled"]
    action(editor, "selection_properties_draft", point_annotations=None)
    action(editor, "apply_selection_properties")
    assert KEY not in owner.snapshot().selection.attributes


def test_properties_refuses_annotation_storage_over_current_budget_before_reads(tmp_path):
    editor, owner, reads = setup(tmp_path, ij.Selection("multipoint", [[1, 1]]*1024))
    editor.update_settings('{"processing_memory_mb":1}', False)
    n = reads.counts().plane_reads;before = owner.snapshot()
    with pytest.raises(ValueError, match="working"):
        action(editor, "show_selection_properties")
    assert reads.counts().plane_reads == n and owner.snapshot().shares_data(before)


def test_lowered_budget_refuses_cached_point_draft_at_apply(tmp_path):
    editor, owner, reads = setup(tmp_path, ij.Selection("multipoint", [[1, 1]]*1024))
    action(editor, "show_selection_properties")
    action(editor, "selection_properties_draft", point_annotations=[{"counter": 1, "position": None}]*1024)
    before = owner.snapshot();n = reads.counts().plane_reads
    editor.update_settings('{"processing_memory_mb":1}', False)
    with pytest.raises(ValueError, match="working"):
        action(editor, "apply_selection_properties")
    assert owner.snapshot().shares_data(before) and reads.counts().plane_reads == n


@pytest.mark.parametrize("context", ["imgui_cpp", "imgui_python"])
def test_actual_point_properties_controls_and_measure_export(context, tmp_path):
    from imagejxx import _desktop
    if context == "imgui_cpp":
        path = Path(os.environ["IMAGEJXX_POINT_ANNOTATIONS_CPP_REPORT"]);report = json.loads(path.read_bytes())
    elif os.environ.get("IMAGEJXX_POINT_ANNOTATIONS_PYTHON_REPORT"):
        path = Path(os.environ["IMAGEJXX_POINT_ANNOTATIONS_PYTHON_REPORT"]);report = json.loads(path.read_bytes())
    else:
        editor = _desktop.Editor(str(tmp_path / "prefs.json"));path = tmp_path / "capture/case.json"
        report = json.loads(editor._point_annotations_ui_case(str(path.parent)))
    assert report["contract"] == "g066.point-annotation-edit.ui.001" and report["context"] == context
    assert report["state"] == "observed" and not report["acceptance_claim"]
    assert 0 < len(report["frames"]) < 180
    for frame in report["frames"]:assert frame["events"] == frame["input_frame"]["raw_inputs"]
    names = [x["name"] for x in report["controls"]]
    for key in ["Counter", "Positioned point", "Frame (1-based)", "Channel (1-based)", "Slice (1-based)", "Apply", "Cancel", "Clear point annotations"]:
        assert key in names
    assert report["unpublished"] and report["undo_after_apply"] == 0
    first = report["applied"][KEY]["points"][0]
    assert first == {"counter": 2, "position": [1, 1, 1]}
    assert report["stored"][KEY] == report["applied"][KEY]
    rows = report["rows"]
    assert [r["mean"] for r in rows] == [131, 22, 11]
    assert [r["Counter"] for r in rows] == [2, 0, 0] and [r["Count"] for r in rows] == [1, 2, 2]
    assert [(r["T"], r["C"], r["Z"]) for r in rows] == [(2, 2, 2), (1, 1, 1), (1, 1, 1)]
    assert report["cancel_preserved"] and KEY not in report["cleared_draft"] and KEY not in report["cleared"]
    assert report["stored_after_clear"][KEY] == report["stored"][KEY]
    assert report["workflow_count"] == 0
    with path.with_name("points.csv").open(newline="", encoding="utf-8") as f:
        exported = list(csv.DictReader(f))
    assert len(exported) == 3 and exported[0]["Counter"] == "2" and float(exported[0]["mean"]) == 131
    reopened = _desktop.Editor(str(tmp_path / "reopened.json"));reopened.open_file(str(path.with_name("points.ijx")))
    restored = reopened.native_document(json.loads(reopened.document_infos())[0]["id"]).snapshot()
    assert restored.selection.attributes[KEY] == report["applied"][KEY]
