"""Explicit independent XY annotation/selection/ROI copies and their UI consumers."""
import json
import os
from pathlib import Path

import numpy as np
import pytest
import imagejxx as ij

KEY = "imagejxx.point_annotations"
EXTENT = [2, 2, 2, 5, 7, 1]


def points():
    return ij.with_point_annotations(ij.Selection("multipoint", [(1, 1), (2, 2), (1, 1)],
        attributes={"opaque-selection": {"kept": [True, "μ"]}}),
        [ij.PointAnnotation(2, ij.PlanePosition(1, 1, 1)), ij.PointAnnotation(0), ij.PointAnnotation(2)])


def owner(selection=None, plane="xy", t=0, c=1):
    pixels = np.fromfunction(lambda t,c,z,y,x:80*t+30*c+10*z+x+10*y, (2,2,2,5,7), dtype=int).astype(np.uint8)
    reads = ij.DatasetReadObserver()
    doc = ij.Document(ij.Dataset(pixels, axes="TCZYX").observe_reads(reads))
    row = ij.PlanarAnnotation("imported", selection if selection is not None else points(), plane, 1,
        t=t, c=c, attributes={"opaque-row": {"keep": [None, 7]}})
    doc.commit(ij.prepare_store_planar_annotation(doc.snapshot(), row));doc.mark_saved()
    return doc, row, reads


def commit(doc, edit):
    doc.commit(edit.transaction, display=edit.display)


@pytest.mark.parametrize("t,c", [(0, 0), (None, 0), (1, None), (None, None)])
def test_selection_copy_preserves_values_and_parent_pose_without_history_or_metadata(t, c):
    doc, row, reads = owner(t=t, c=c)
    prior = ij.Selection("ellipse", [(0, 0), (2, 2)])
    doc.select(prior)
    view = doc.snapshot().display;view.t = 1;view.c = 1;view.crosshair = [3, 2, 0];doc.set_display(view)
    before = doc.snapshot();history = doc.undo_steps
    edit = ij.prepare_select_planar_annotation(before, 0)
    assert edit.row_index is None and doc.snapshot().shares_data(before)
    commit(doc, edit)
    current = doc.snapshot()
    assert current.selection == row.selection and current.previous_selection == prior
    assert (current.display.t, current.display.c, current.display.crosshair[2]) == (1 if t is None else t, 1 if c is None else c, 1)
    assert current.display.crosshair[:2] == [3, 2]
    assert current.metadata == before.metadata and current.dataset.shares_pixels(before.dataset)
    assert doc.undo_steps == history and not current.dirty
    assert reads.counts().plane_reads == reads.counts().region_reads == 0
    no_op = doc.snapshot();commit(doc, ij.prepare_select_planar_annotation(no_op, 0))
    assert doc.snapshot().shares_data(no_op) and doc.snapshot().revisions.display == no_op.revisions.display


def test_stored_roi_copy_preserves_wildcards_name_row_attributes_and_independent_edits():
    doc, row, reads = owner(t=None, c=None)
    active = ij.Selection("rectangle", [(0, 0), (2, 2)]);doc.select(active)
    before = doc.snapshot();history = doc.undo_steps
    edit = ij.prepare_store_planar_annotation_roi(before, 0)
    assert edit.row_index == 0 and edit.display is None and doc.snapshot().shares_data(before)
    commit(doc, edit)
    stored = doc.snapshot().rois[0]
    assert stored.name == row.name and stored.selection == row.selection and stored.attributes == row.attributes
    assert (stored.t, stored.c, stored.z) == (None, None, 1)
    assert doc.snapshot().selection == active and doc.undo_steps == history + 1
    assert doc.snapshot().display.crosshair == before.display.crosshair and reads.counts().plane_reads == 0
    doc.undo();assert doc.snapshot().shares_data(before)
    doc.redo();assert doc.snapshot().rois[0].selection == row.selection
    changed = ij.without_point_annotations(row.selection)
    doc.commit(ij.prepare_update_planar_annotation(doc.snapshot(), 0,
        ij.PlanarAnnotation(row.name, changed, "xy", 1, attributes=row.attributes)))
    assert doc.snapshot().rois[0].selection == stored.selection


def test_copyback_freezes_selection_values_preserves_scope_and_uses_existing_replay_record():
    doc, original, reads = owner();history = doc.undo_steps
    edited = ij.SelectionEditor(points(), (5, 7)).inserted_vertex(1, [3.125, 3])
    doc.select(edited);before = doc.snapshot()
    command = ij.resolve_planar_annotation_from_selection(before, 0)
    ij.validate_planar_annotation_command_record(command.record())
    assert doc.snapshot().shares_data(before)
    # The complete replacement is frozen, as for other planar commands; a later
    # independent active selection is neither consumed nor overwritten.
    other = ij.Selection("rectangle", [(1, 1), (4, 4)]);doc.select(other)
    doc.commit(ij.prepare_planar_annotation(command))
    row = ij.planar_annotations(doc.snapshot())[0]
    assert row.selection == edited and doc.snapshot().selection == other
    assert (row.name, row.t, row.c, row.coordinate, row.attributes) == (original.name, original.t, original.c, original.coordinate, original.attributes)
    assert doc.undo_steps == history + 1 and reads.counts().plane_reads == 0
    replay = ij.planar_annotation_command_from_record(before, command.record())
    assert replay.request.annotation.selection == edited


@pytest.mark.parametrize("selection", [
    ij.Selection("rectangle", [(0, 0), (2, 2)]),
    ij.Selection("line", [(0.25, 0.5), (3, 2)]),
    ij.Selection.from_mask(np.eye(5, 7, dtype=np.uint8)),
    ij.Selection.from_path(ij.ImageJCompositePath([0,1,1,1,3,1,1,3,3,4])),
])
def test_other_native_xy_geometry_is_copied_without_rasterization(selection):
    doc, row, reads = owner(selection)
    commit(doc, ij.prepare_select_planar_annotation(doc.snapshot(), 0))
    commit(doc, ij.prepare_store_planar_annotation_roi(doc.snapshot(), 0))
    assert doc.snapshot().selection == doc.snapshot().rois[0].selection == row.selection
    assert reads.counts().plane_reads == reads.counts().region_reads == 0


@pytest.mark.parametrize("plane", ["xz", "yz"])
def test_non_xy_rows_refuse_without_silent_projection(plane):
    doc, _, reads = owner(ij.Selection("point", [(1, 1)]), plane=plane)
    doc.select(ij.Selection("point", [(2, 2)]));before = doc.snapshot()
    for fn in [ij.prepare_select_planar_annotation, ij.prepare_store_planar_annotation_roi, ij.resolve_planar_annotation_from_selection]:
        with pytest.raises(ValueError, match="XY"):fn(before, 0)
    assert doc.snapshot().shares_data(before) and reads.counts().plane_reads == 0


@pytest.mark.parametrize("fn", [ij.prepare_select_planar_annotation, ij.prepare_store_planar_annotation_roi])
def test_prepared_manager_transfer_refuses_stale_source_and_display(fn):
    doc, _, _ = owner();edit = fn(doc.snapshot(), 0)
    view = doc.snapshot().display;view.t = 1;doc.set_display(view);current = doc.snapshot()
    with pytest.raises(ij.RevisionConflict):commit(doc, edit)
    assert doc.snapshot().shares_data(current)


@pytest.mark.parametrize("fn", [ij.prepare_select_planar_annotation, ij.prepare_store_planar_annotation_roi, ij.resolve_planar_annotation_from_selection])
@pytest.mark.parametrize("index", [True, -1, 1.5, "0", 2**80, 9])
def test_indices_are_strict_and_missing_rows_do_not_mutate(fn, index):
    doc, _, reads = owner();before = doc.snapshot()
    with pytest.raises((TypeError, ValueError, OverflowError, IndexError)):fn(before, index)
    assert doc.snapshot().shares_data(before) and reads.counts().plane_reads == 0


def test_working_limit_refuses_before_decode_or_publication_and_opaque_versions_are_preserved():
    selection = ij.Selection("multipoint", [(1, 1)]*1024)
    doc, _, reads = owner(selection);doc.select(points());before = doc.snapshot()
    for fn in [ij.prepare_select_planar_annotation, ij.prepare_store_planar_annotation_roi, ij.resolve_planar_annotation_from_selection]:
        with pytest.raises(ValueError, match="working"):fn(before, 0, max_working_bytes=1024*1024)
    with pytest.raises(ValueError, match="working"):ij.planar_annotations(before, max_working_bytes=8192)
    assert len(ij.planar_annotations(before)) == 1 and doc.snapshot().shares_data(before)
    assert reads.counts().plane_reads == 0
    opaque = ij.Selection("point", [(1, 1)], attributes={KEY: {"version": 99, "points": []}})
    doc, _, _ = owner(opaque)
    commit(doc, ij.prepare_select_planar_annotation(doc.snapshot(), 0))
    assert doc.snapshot().selection.attributes == opaque.attributes


def action(editor, command, **params):
    editor.action(command, json.dumps(params))


def test_desktop_guards_and_complete_selection_properties_measure_handoff(tmp_path):
    from imagejxx import _desktop
    doc, row, reads = owner();editor = _desktop.Editor(str(tmp_path / "prefs.json"));editor.add_native_document(doc)
    revision = doc.snapshot().revisions.metadata
    before = doc.snapshot()
    with pytest.raises(ij.RevisionConflict):action(editor, "select_planar_annotation", index=0, metadata_revision=revision+1)
    assert doc.snapshot().shares_data(before)
    action(editor, "select_planar_annotation", index=0, metadata_revision=revision)
    action(editor, "show_selection_properties")
    action(editor, "selection_properties_draft", point_annotations=[{"counter":7,"position":[1,1,1]}, {"counter":0,"position":None}, {"counter":7,"position":None}])
    action(editor, "apply_selection_properties")
    assert ij.planar_annotations(doc.snapshot())[0].selection == row.selection
    action(editor, "update_planar_annotation_selection", index=0)
    action(editor, "store_planar_annotation_roi", index=0)
    assert reads.counts().plane_reads == 0
    assert doc.snapshot().rois[0].selection == ij.planar_annotations(doc.snapshot())[0].selection
    action(editor, "measure_rois")
    result = json.loads(editor.results)
    assert len(result) == 3
    assert doc.snapshot().dataset.shares_pixels(before.dataset)


def test_desktop_working_limit_is_current_and_recovers_without_source_reads(tmp_path):
    from imagejxx import _desktop
    doc, _, reads = owner(ij.Selection("multipoint", [(1, 1)]*1024))
    editor = _desktop.Editor(str(tmp_path / "prefs.json"));editor.add_native_document(doc)
    editor.update_settings('{"processing_memory_mb":1}', False);before = doc.snapshot()
    with pytest.raises(ValueError, match="working"):
        action(editor, "select_planar_annotation", index=0)
    assert doc.snapshot().shares_data(before) and reads.counts().plane_reads == 0
    editor.update_settings('{"processing_memory_mb":16}', False)
    action(editor, "select_planar_annotation", index=0)
    assert len(doc.snapshot().selection.points) == 1024 and reads.counts().plane_reads == 0


@pytest.mark.parametrize("context", ["imgui_cpp", "imgui_python"])
@pytest.mark.parametrize("mode", ["handoff", "zip"])
def test_actual_imported_point_bridge_and_zip_controls(mode, context, tmp_path):
    from imagejxx import _desktop
    env = "IMAGEJXX_POINT_ROI_HANDOFF_CPP_REPORT" if mode == "handoff" else "IMAGEJXX_POINT_ROI_ZIP_CPP_REPORT"
    if context == "imgui_cpp":
        path = Path(os.environ[env]);report = json.loads(path.read_bytes())
    else:
        path = tmp_path / "capture/case.json";editor = _desktop.Editor(str(tmp_path / "prefs.json"))
        fn = editor._point_roi_handoff_ui_case if mode == "handoff" else editor._point_roi_zip_interchange_ui_case
        report = json.loads(fn(str(path.parent)))
    assert report["state"] == "observed" and not report["acceptance_claim"]
    assert all(f["events"] == f["input_frame"]["raw_inputs"] for f in report["frames"])
    assert 0 < len(report["frames"]) < 700 and report["context"] == context
    assert report["original_retained"] and report["export_preserved_source"] and report["pixels_shared"]
    assert all(c["control"]["enabled"] and c["control"]["id"] for c in report["controls"])
    names = {c["name"] for c in report["controls"]}
    if mode == "handoff":
        assert report["contract"] == "g066.point-roi-handoff.ui.001" and report["catalog_reused"]
        assert {"select_planar_annotation", "update_planar_annotation_selection", "store_planar_annotation_roi", "measure_rois"} <= names
        assert report["adopted_words"] == [2050,0,258,0]
        assert report["handoff_words"] == report["annotation_words"] == report["outer_words"] == [2055,0,258,0]
        rows = report["measurement_rows"]
        assert [r["mean"] for r in rows] == [133,22,11,11]
        assert [r["Counter"] for r in rows] == [7,0,2,0] and [r["Count"] for r in rows] == [1,2,1,2]
        assert report["handoff_undo"] == report["workflow_count"] == 3
        wires = [path.with_name("edited.roi").read_bytes()]
    else:
        assert report["contract"] == "g066.point-roi-interchange.zip.ui.001"
        assert {"compose", "member_name", "add_member", "validate_zip", "export"} <= names
        assert report["zip_members"] == ["edited.roi", "duplicate.roi"] and report["workflow_count"] == 2
        archive = ij.decode_imagej_roi_zip(path.with_name("edited.zip").read_bytes())
        assert [e.name for e in archive] == report["zip_members"]
        wires = [e.data for e in archive]
    for wire in wires:
        reopened = ij.decode_imagej_roi_envelope(wire)
        assert ij.imagej_point_counters(reopened.annotation.selection, EXTENT) == report["outer_words"]
        assert ij.encode_imagej_roi(reopened.source_record) == path.with_name("source.roi").read_bytes()
