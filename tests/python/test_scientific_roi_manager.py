"""Scientific manager behavior through public native and desktop entry points."""
import json

import numpy as np
import pytest

import imagejxx as ij


def owner():
    pixels = np.arange(2*2*3*5*7, dtype=np.uint16).reshape(2, 2, 3, 5, 7)
    native = ij.Document(ij.Dataset(pixels, axes="TCZYX"), "Scientific manager")
    edit = native.begin_edit()
    edit.set_extension("future", {"kept": [True, None, 7]})
    edit.set_overlays([{"future_overlay": {"kept": 7}}])
    native.commit(edit)
    native.mark_saved()
    return native, pixels.tobytes()


def commit(native, edit):
    native.commit(edit.transaction, display=edit.display)


def action(editor, name, parameters=None):
    if hasattr(editor, "_editor"):
        editor.action(name, **(parameters or {}))
    else:
        editor.action(name, json.dumps(parameters or {}))


def test_native_manager_positions_history_and_noop():
    native, pixels = owner()
    native.select(ij.Selection("rectangle", [(1, 1), (5, 4)]))
    start = native.history().undo_steps
    commit(native, ij.prepare_store_roi(native.snapshot(), "zero"))
    roi = native.snapshot().rois[0]
    assert (roi.t, roi.c, roi.z) == (0, 0, 0)
    assert native.history().undo_steps == start + 1
    view = native.snapshot().display
    view.t, view.c, view.crosshair = 1, 1, [2, 2, 2]
    native.set_display(view)
    native.select(ij.Selection("ellipse", [(0, 0), (2, 2)]))
    commit(native, ij.prepare_select_roi(native.snapshot(), 0))
    current = native.snapshot()
    assert (current.display.t, current.display.c, current.display.crosshair[2]) == (0, 0, 0)
    assert current.selection.kind == "rectangle" and current.previous_selection.kind == "ellipse"
    assert native.history().undo_steps == start + 1
    revisions = (current.revisions.selection, current.revisions.display)
    commit(native, ij.prepare_select_roi(current, 0))
    assert (native.snapshot().revisions.selection, native.snapshot().revisions.display) == revisions
    assert native.snapshot().dataset.numpy("TCZYX").tobytes() == pixels


@pytest.mark.parametrize("index", [True, -1, 1.5, "0", 2**80])
def test_native_indices_are_strict(index):
    native, _ = owner()
    with pytest.raises((TypeError, ValueError, OverflowError, IndexError)):
        ij.prepare_select_roi(native.snapshot(), index)


@pytest.mark.parametrize("layer", ["binding", "wrapper"])
def test_scientific_manager_desktop_workflow(tmp_path, layer):
    from imagejxx import _desktop
    from imagejxx.ui.app import Editor
    from imagejxx.ui.persistence import Preferences

    wrapper = Editor(preferences=Preferences(tmp_path / "preferences.json"))
    editor = wrapper if layer == "wrapper" else wrapper._editor
    native, pixels = owner()
    editor.add_native_document(native)
    view = native.snapshot().display
    view.t, view.c, view.crosshair = 1, 1, [3, 2, 2]
    native.set_display(view)
    action(editor, "create_roi")
    request = ij.SelectionCreationRequest(ij.Selection("rectangle", [(1, 1), (5, 4)]))
    editor.apply_selection_creation(request)
    history = native.history().undo_steps
    action(editor, "add_roi", {"name": "user area"})
    assert native.history().undo_steps == history + 1
    assert (native.snapshot().rois[0].t, native.snapshot().rois[0].c, native.snapshot().rois[0].z) == (1, 1, 2)
    action(editor, "roi_properties", {"index": 0, "name": "styled", "t": 0, "c": None,
        "style": {"stroke_color": "red", "fill_color": "#8000ff00", "stroke_width": 2.5}})
    roi = native.snapshot().rois[0]
    assert (roi.t, roi.c, roi.z) == (0, None, 2)
    assert ij.resolve_roi_style(roi.selection).stroke_width == 2.5
    action(editor, "select_roi", {"index": 0})
    current = native.snapshot()
    assert (current.display.t, current.display.c, current.display.crosshair[2]) == (0, 1, 2)
    assert current.selection.attributes == roi.selection.attributes
    before = native.history().undo_steps
    action(editor, "update_roi", {"index": 0})
    assert native.history().undo_steps == before
    action(editor, "rename_roi", {"index": 0, "name": "renamed"})
    action(editor, "undo")
    assert native.snapshot().rois[0].name == "styled"
    action(editor, "redo")
    assert native.snapshot().rois[0].name == "renamed"
    session = tmp_path / "saved.ijx"
    action(editor, "save_document", {"path": str(session)})
    reopened = _desktop.Editor(str(tmp_path / "reopened-preferences.json"))
    reopened.open_file(str(session))
    restored = reopened.native_document(json.loads(reopened.document_infos())[0]["id"]).snapshot()
    assert (restored.rois[0].t, restored.rois[0].c, restored.rois[0].z) == (0, None, 2)
    assert restored.rois[0].selection.attributes == native.snapshot().rois[0].selection.attributes
    assert restored.dataset.numpy("TCZYX").tobytes() == pixels
    assert restored.metadata["extensions"] == native.snapshot().metadata["extensions"]
    assert restored.overlays == native.snapshot().overlays


def test_manager_stale_navigation_and_invalid_properties_are_atomic(tmp_path):
    from imagejxx import _desktop
    editor = _desktop.Editor(str(tmp_path / "prefs.json"))
    native, pixels = owner()
    editor.add_native_document(native)
    action(editor, "create_roi")
    editor.apply_selection_creation(ij.SelectionCreationRequest(ij.Selection("rectangle", [(1, 1), (5, 4)])))
    action(editor, "add_roi")
    captured = native.snapshot()
    pending = ij.prepare_rename_roi(captured, 0, "stale")
    view = captured.display
    view.c = 1
    native.set_display(view)
    with pytest.raises(Exception, match="[Rr]evision|changed|conflict"):
        commit(native, pending)
    before = native.snapshot()
    for params in ({"index": True}, {"index": 0, "t": 2}, {"index": 0, "z": -1},
                   {"index": 0, "style": {"stroke_width": -1}}):
        with pytest.raises((ValueError, IndexError)):
            action(editor, "roi_properties", params)
        assert native.snapshot().data_identity == before.data_identity
        assert native.snapshot().rois[0].name == before.rois[0].name
    assert native.snapshot().dataset.numpy("TCZYX").tobytes() == pixels
