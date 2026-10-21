"""Installed desktop Python entry points; C++ owns creation and publication.

These checks cover the native binding and its optional Python facade against
one shared controller. They grant no physical-input or full-family parity credit.
"""

import copy
import importlib
import os
from pathlib import Path
import sysconfig

import numpy as np
import pytest


LAYERS = ("binding", "wrapper")
TEST_NAMES = (
    "test_apply_explicit_inactive_owner_preserves_other_state",
    "test_recording_exact_noop_and_redo",
    "test_strict_dict_replay_and_complete_rebind",
    "test_malformed_stale_closed_refusals_are_atomic",
    "test_scientific_and_incomplete_requests_are_atomic",
)
EXPECTED_COLLECTION_COUNT = 10
EXPECTED_COLLECTION_IDS = tuple(f"{name}[{layer}]" for name in TEST_NAMES for layer in LAYERS)
BASE_STYLE = {"label": "base", "future": {"version": 37, "retained": [True, 7, None]}}
CANDIDATE_STYLE = {"label": "complete", "future": {"version": 43}}
RIGHT_ROWS = ("0000000", "0001111", "0001111", "0001111", "0000000")
SUBTRACT_ROWS = ("0000000", "0111100", "0100100", "0111100", "0000000")


@pytest.fixture
def editor(tmp_path):
    global ij
    ij = importlib.import_module("imagejxx")
    required = os.environ.get("IMAGEJXX_REQUIRE_RENDER_TESTS", "0")
    assert required in {"0", "1"}
    desktop = (importlib.import_module("imagejxx._desktop") if required == "1" else
               pytest.importorskip("imagejxx._desktop", exc_type=ModuleNotFoundError))
    if required == "1":
        roots = {Path(sysconfig.get_path(key)).resolve() / "imagejxx" for key in ("purelib", "platlib")}
        for module in (desktop, importlib.import_module("imagejxx._native")):
            path = Path(module.__file__).resolve(strict=True)
            assert path.suffix in {".pyd", ".so"}
            assert any(path.is_relative_to(root) for root in roots), "Checkout extension import"
    from imagejxx.ui.app import Editor
    from imagejxx.ui.persistence import Preferences
    return Editor(preferences=Preferences(tmp_path / "preferences.json"))


def _base():
    return ij.Selection("rectangle", [(1, 1), (5, 4)], width=2, attributes=copy.deepcopy(BASE_STYLE))


def _candidate():
    return ij.Selection("rectangle", [(3, 1), (7, 4)], width=3, attributes=copy.deepcopy(CANDIDATE_STYLE))


def _request(candidate=None, operation="REPLACE"):
    return ij.SelectionCreationRequest(_candidate() if candidate is None else candidate,
                                      getattr(ij.SelectionCreationOperation, operation))


def _owner(editor, *, scientific=False):
    shape = (2, 2, 2, 5, 7, 1) if scientific else (1, 1, 2, 5, 7, 1)
    t, c, z, y, x, s = np.indices(shape, dtype=np.int64)
    pixels = (40*t + 20*c + 7*z + 3*y + x + s).astype(np.uint16)
    calibration = ij.SpatialCalibration(spacing=(.5, 1.5, 2), origin=(11, -7, 19),
        unit="um", frame_interval=.75, time_origin=2.25, time_unit="s")
    # Ordinary admission requires T1/C1 and no explicit T/C-positioned saved ROIs.
    # Declared ZYXS input retains the identical canonical Z2/Y5/X7/S1 source bytes.
    source = pixels if scientific else pixels[0, 0]
    axes = "TCZYXS" if scientific else "ZYXS"
    owner = ij.Document(ij.Dataset(source, axes=axes, calibration=calibration), "Desktop creation")
    edit = owner.begin_edit()
    edit.set_property("retained", {"nested": [True, 7, "kept"]})
    edit.set_extension("opaque", {"version": 37, "unsigned": 2**64-1})
    edit.set_record_layout({"source_presence": True})
    edit.set_rois([ij.ROI("stored", ij.Selection("rectangle", [(0, 0), (2, 2)]),
                          t=0 if scientific else None, c=0 if scientific else None,
                          z=1, attributes={"retained": True})])
    edit.set_overlays([{"future_annotation": {"version": 37, "values": [3, 1, 4]}}])
    edit.select(_base())
    edit.set_previous_selection(ij.Selection("line", [(0, 0), (2, 2)], width=2))
    owner.commit(edit)
    owner.mark_saved()
    edit = owner.begin_edit()
    edit.set_property("redo_probe", True)
    owner.commit(edit)
    owner.undo()
    view = owner.snapshot().display
    view.t, view.c, view.crosshair = shape[0]-1, shape[1]-1, [4, 3, 1]
    view.zoom, view.linked_zoom = 2.25, False
    view.plane_zoom, view.pan = [2.25, 3., 4.], [[1., -2.], [3., 4.], [-5., 2.]]
    # Desktop display keys are closed; opaque records remain in metadata/extensions.
    view.properties = {"display_range": [5., 90.], "lut": "Ice",
                       "custom_lut": [[i, (3*i) % 256, 255-i] for i in range(256)]}
    owner.set_display(view)
    editor.add_native_document(owner)
    return owner


def _selection(value):
    if value is None:
        return None
    return (value.kind, tuple(map(tuple, value.points)), value.width, copy.deepcopy(value.attributes),
            value.mask((5, 7)).tobytes())


def _request_state(request):
    return request.operation, _selection(request.candidate)


def _state(owner):
    snapshot, history = owner.snapshot(), owner.history()
    view, cal = snapshot.display, snapshot.dataset.calibration
    return {
        "identity": snapshot.data_identity, "dirty": snapshot.dirty,
        "revisions": (snapshot.revisions.pixels, snapshot.revisions.metadata,
                      snapshot.revisions.selection, snapshot.revisions.display),
        "selection": _selection(snapshot.selection), "previous": _selection(snapshot.previous_selection),
        "pixels": snapshot.dataset.numpy("TCZYXS").tobytes(),
        "shape": snapshot.dataset.shape, "dtype": snapshot.dataset.dtype.str,
        "sample_policy": snapshot.dataset.sample_policy,
        "metadata": snapshot.metadata, "record_layout": snapshot.record_layout,
        "rois": [(roi.name, _selection(roi.selection), roi.t, roi.c, roi.z, roi.attributes) for roi in snapshot.rois],
        "overlays": snapshot.overlays, "spectrum_present": snapshot.spectrum is not None,
        "calibration": (tuple(cal.spacing), tuple(cal.origin), tuple(cal.direction), cal.unit,
                        cal.frame_interval, cal.time_origin, cal.time_unit),
        "display": (view.t, view.c, tuple(view.crosshair), view.orthogonal, view.linked_zoom,
                    view.zoom, tuple(view.plane_zoom), tuple(map(tuple, view.pan)), view.revision, view.properties),
        "history": (history.undo_steps, history.redo_steps, history.step_limit, history.pixel_budget),
    }


def _collection(editor):
    return (editor.active_index, editor.recording, editor.busy, editor.results, editor.workflow,
            [(info["id"], _state(editor._editor.native_document(info["id"]))) for info in editor.document_infos])


def _call(editor, layer, name, *args, **kwargs):
    return getattr(editor if layer == "wrapper" else editor._editor, name)(*args, **kwargs)


def _preserved(before, owner):
    after = _state(owner)
    for key in before.keys() - {"selection", "previous", "revisions"}:
        assert after[key] == before[key], key
    assert after["revisions"][:2] == before["revisions"][:2]
    assert after["revisions"][3] == before["revisions"][3]
    return after


def _changed(before, owner, expected):
    after = _preserved(before, owner)
    assert after["selection"] == _selection(expected)
    assert after["previous"] == before["selection"]
    assert after["revisions"][2] == before["revisions"][2] + 1
    return after


def _literal_mask(rows):
    # Test-owned literal input/result membership; no product composer supplies it.
    return np.array([[255 if cell == "1" else 0 for cell in row] for row in rows], np.uint8)


def _same_controller(editor, owner):
    assert editor.live_document(owner.id).native_owner.id == owner.id
    assert _state(editor.live_document(owner.id).native_owner) == _state(owner)
    assert _state(editor._editor.native_document(owner.id)) == _state(owner)


@pytest.mark.parametrize("layer", LAYERS)
def test_apply_explicit_inactive_owner_preserves_other_state(editor, layer):
    source, active = _owner(editor), _owner(editor)
    assert editor.active_index == 1
    editor.recording, editor.results = True, [{"retained": 7}]
    before, untouched, snapshot = _state(source), _state(active), source.snapshot()
    request = _request()
    request_before = _request_state(request)
    assert _call(editor, layer, "apply_selection_creation", request, document_id=source.id) is None
    _changed(before, source, _candidate())
    assert source.snapshot().dataset.shares_pixels(snapshot.dataset)
    np.testing.assert_array_equal(source.snapshot().selection.mask((5, 7)), _literal_mask(RIGHT_ROWS))
    assert _request_state(request) == request_before and _state(active) == untouched
    assert editor.active_index == 1 and editor.results == [{"retained": 7}]
    assert len(editor.workflow) == 1
    record = editor.workflow[0]
    assert record["schema"] == "imagejxx.selection-creation-command" and record["version"] == 1
    assert record["source"]["id"] == source.id and record["operation"] == "replace"
    assert record["position"] == {"t": 0, "c": 0, "z": 1}
    _same_controller(editor, source)


@pytest.mark.parametrize("layer", LAYERS)
def test_recording_exact_noop_and_redo(editor, layer):
    owner = _owner(editor)
    editor.recording, editor.results = True, [{"retained": 11}]
    assert owner.history().redo_steps == 1
    before = _collection(editor)
    _call(editor, layer, "apply_selection_creation", _request(_base()), document_id=owner.id)
    assert _collection(editor) == before
    before = _state(owner)
    _call(editor, layer, "apply_selection_creation", _request(), document_id=owner.id)
    _changed(before, owner, _candidate())
    assert len(editor.workflow) == 1
    recorded = copy.deepcopy(editor.workflow)
    editor.recording = False
    before = _state(owner)
    small = ij.Selection("rectangle", [(0, 0), (2, 2)], width=1, attributes={"off": True})
    _call(editor, layer, "apply_selection_creation", _request(small), document_id=owner.id)
    _changed(before, owner, small)
    assert editor.workflow == recorded and editor.results == [{"retained": 11}]
    before = _collection(editor)
    _call(editor, layer, "apply_selection_creation", _request(small), document_id=owner.id)
    assert _collection(editor) == before
    _same_controller(editor, owner)


@pytest.mark.parametrize("layer", LAYERS)
def test_strict_dict_replay_and_complete_rebind(editor, layer):
    source, destination = _owner(editor), _owner(editor)
    editor.recording = True
    _call(editor, layer, "apply_selection_creation", _request(), document_id=source.id)
    record = copy.deepcopy(editor.workflow[0])
    original = copy.deepcopy(record)
    fresh = ij.Selection("rectangle", [(0, 0), (2, 2)], attributes={"fresh": True})
    source.select(fresh)
    view = source.snapshot().display
    view.crosshair = [2, 1, 0]
    source.set_display(view)
    before = _state(source)
    assert _call(editor, layer, "replay_selection_creation", record, document_id=source.id) is None
    _changed(before, source, _candidate())
    assert _selection(source.snapshot().previous_selection) == _selection(fresh)
    assert record == original and editor.workflow[-1]["position"] == record["position"]
    rejected = _collection(editor)
    with pytest.raises(ValueError):
        _call(editor, layer, "replay_selection_creation", record, document_id=destination.id)
    assert _collection(editor) == rejected
    before = _state(destination)
    request = _request(ij.Selection("rectangle", [(2, 2), (4, 3)]), "SUBTRACT")
    request_before, count = _request_state(request), len(editor.workflow)
    _call(editor, layer, "replay_selection_creation", record, rebind_request=request, document_id=destination.id)
    after = _preserved(before, destination)
    selection = destination.snapshot().selection
    assert selection.kind == "mask" and selection.width == 1 and selection.attributes == {} and selection.points == []
    np.testing.assert_array_equal(selection.mask((5, 7)), _literal_mask(SUBTRACT_ROWS))
    assert after["previous"] == before["selection"] and after["revisions"][2] == before["revisions"][2] + 1
    assert len(editor.workflow) == count + 1 and editor.workflow[-1]["source"]["id"] == destination.id
    assert editor.workflow[-1]["operation"] == "subtract"
    assert _request_state(request) == request_before and record == original
    _same_controller(editor, destination)


@pytest.mark.parametrize("layer", LAYERS)
def test_malformed_stale_closed_refusals_are_atomic(editor, layer):
    source, destination = _owner(editor), _owner(editor)
    editor.recording, editor.results = True, [{"retained": 13}]
    _call(editor, layer, "apply_selection_creation", _request(), document_id=source.id)
    record = copy.deepcopy(editor.workflow[0])
    malformed = copy.deepcopy(record)
    malformed["result"]["points"] = [[0, 0], [1, 1]]  # Valid geometry, false original Replace equation.
    for explicit in (None, _request(_base())):
        before = _collection(editor)
        with pytest.raises(ValueError):
            _call(editor, layer, "replay_selection_creation", malformed,
                  rebind_request=explicit, document_id=destination.id if explicit else source.id)
        assert _collection(editor) == before
    edit = source.begin_edit()
    edit.set_property("metadata_changed_after_recording", True)
    source.commit(edit)
    before = _collection(editor)
    with pytest.raises(ij.RevisionConflict):
        _call(editor, layer, "replay_selection_creation", record, document_id=source.id)
    assert _collection(editor) == before
    editor.close_document(source.id, discard=True)
    closed = _state(source)
    before = _collection(editor)
    for method, args in (("apply_selection_creation", (_request(),)), ("replay_selection_creation", (record,))):
        with pytest.raises(IndexError):
            _call(editor, layer, method, *args, document_id=source.id)
        assert _collection(editor) == before and _state(source) == closed
    _same_controller(editor, destination)


@pytest.mark.parametrize("layer", LAYERS)
def test_scientific_and_incomplete_requests_are_atomic(editor, layer):
    eager, scientific = _owner(editor), _owner(editor, scientific=True)
    editor.recording = True
    _call(editor, layer, "apply_selection_creation", _request(), document_id=eager.id)
    record = copy.deepcopy(editor.workflow[0])
    before = _collection(editor)
    for method, args in (("apply_selection_creation", (_request(),)), ("replay_selection_creation", (record,))):
        with pytest.raises(ValueError):
            _call(editor, layer, method, *args, document_id=scientific.id)
        assert _collection(editor) == before
    # Complete typed requests are required at both Python desktop entry points.
    for name, args, kwargs in (
        ("apply_selection_creation", ({"operation": "replace"},), {}),
        ("replay_selection_creation", (record,), {"rebind_request": {"candidate": None}}),
    ):
        with pytest.raises(TypeError):
            _call(editor, layer, name, *args, document_id=eager.id, **kwargs)
        assert _collection(editor) == before
    _same_controller(editor, scientific)
