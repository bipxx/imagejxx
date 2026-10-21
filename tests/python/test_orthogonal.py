"""Shared native editor planes, physical display and actual ImGui gestures."""

from decimal import Decimal
import hashlib
import json
import os
from pathlib import Path
import struct
import subprocess

import imagejxx as ij
import numpy as np
import pytest
from imagejxx.ui.model import Calibration, Document, Overlay, Selection
from imagejxx.ui.persistence import Preferences, load_session, save_session


@pytest.fixture
def editor(tmp_path):
    pytest.importorskip("imagejxx._desktop", exc_type=ModuleNotFoundError)
    from imagejxx.ui.app import Editor

    return Editor(preferences=Preferences(tmp_path / "prefs.json"))


@pytest.mark.parametrize("dtype", [np.uint8, np.uint16, np.float32, np.float64])
def test_live_planes_navigation_and_undo(editor, dtype):
    source = np.arange(60).reshape(3, 4, 5).astype(dtype)
    editor.add_document(Document(source, is_stack=True))
    live = editor.live_document()
    before = live.info
    assert before["pixel_storage"] == {
        "logical_bytes": 60 * 8,
        "retained_bytes": 60 * 8,
        "plane_buffers": 3,
    }
    live.set_view(orthogonal=True, crosshair=[3, 2, 1])
    after = live.info
    assert after["id"] == before["id"]
    for key in ["revision", "dirty", "undo_steps", "redo_steps"]:
        assert after[key] == before[key]
    assert after["view_revision"] > before["view_revision"]
    np.testing.assert_array_equal(live.plane("xy"), source[1])
    np.testing.assert_array_equal(live.plane("xz"), source[:, 2, :])
    np.testing.assert_array_equal(live.plane("yz"), source[:, :, 3])
    live.apply_operation("add", {"value": 1}, asynchronous=False)
    assert live.info["pixel_storage"] == {
        "logical_bytes": 60 * 8,
        "retained_bytes": 80 * 8,
        "plane_buffers": 4,
    }
    np.testing.assert_array_equal(live.plane("xy"), source[1] + 1)
    live.set_view(crosshair=[1, 1, 2])
    editor.action("undo")
    assert live.view["crosshair"] == [1, 1, 2]
    np.testing.assert_array_equal(live.snapshot().data, source)
    editor.action("redo")
    assert live.view["crosshair"] == [1, 1, 2]
    expected = source.copy()
    expected[1] += 1
    np.testing.assert_array_equal(live.snapshot().data, expected)
    snapshot = live.snapshot()
    with pytest.raises(ValueError):
        snapshot.data[:] = 0
    snapshot.commit(np.zeros_like(snapshot.data))
    np.testing.assert_array_equal(live.snapshot().data, expected)


def test_targeted_access_does_not_serialize_other_documents(editor):
    editor.add_document(Document(np.zeros((3, 4), np.uint8), "first"))
    first = editor.live_document()
    editor.add_document(Document(np.full((3, 4), 10, np.uint8), "second"))
    second = editor.live_document()
    assert first.id != second.id
    backend = editor._editor

    class GuardedBackend:
        def documents(self):
            raise AssertionError("Targeted reads must not serialize every document")

        def __getattr__(self, name):
            return getattr(backend, name)

    editor._editor = GuardedBackend()
    assert editor.document.title == "second"
    assert all("pixels" not in info for info in editor.document_infos)
    assert first.info["title"] == "first"
    np.testing.assert_array_equal(first.plane(), 0)
    first.apply_operation("add", {"value": 5}, asynchronous=False)
    np.testing.assert_array_equal(first.plane(), 5)
    np.testing.assert_array_equal(second.plane(), 10)
    assert editor.active_index == 1
    first.set_view(crosshair=[1, 1, 0])
    assert first.view["crosshair"] == [1, 1, 0]
    with pytest.raises(IndexError):
        editor.live_document(max(first.id, second.id) + 1000)
    before = first.view
    with pytest.raises(IndexError):
        first.set_view(orthogonal=True, crosshair=[99, 0, 0])
    assert first.view == before
    with pytest.raises(ValueError, match="Unknown view parameter"):
        first.set_view(crossahir=[1, 1, 0])


def test_native_and_python_session_preserve_views(editor, tmp_path):
    editor.add_document(Document(np.zeros((3, 4, 5), np.uint16), is_stack=True))
    live = editor.live_document()
    live.set_view(
        orthogonal=True,
        crosshair=[3, 2, 1],
        zoom=7,
        linked_zoom=False,
        plane_zoom=[2, 3, 4],
        pan=[[1, 2], [3, 4], [5, 6]],
    )
    view = live.view
    path = tmp_path / "native.ijx"
    editor.action("save_workspace", path=str(path))
    extensions = {}
    documents, _, _ = load_session(path, extensions=extensions)
    assert documents[0].view_state == view
    py_path = tmp_path / "python.ijx"
    # The V2 writer transports documents; native root extensions remain in the
    # explicit destination above, outside this document-compatibility check.
    save_session(py_path, documents)
    from imagejxx.ui.app import Editor

    reopened = Editor(preferences=Preferences(tmp_path / "reopened.json"))
    reopened.open_file(py_path)
    assert reopened.live_document().view == view


def test_actual_orthogonal_mouse_gestures_and_cache(editor):
    document = Document(np.arange(60, dtype=np.uint16).reshape(3, 4, 5), is_stack=True)
    document.set_calibration(Calibration(0.5, 2, 6, "um"))
    editor.add_document(document)
    live = editor.live_document()
    live.set_view(orthogonal=True, crosshair=[3, 2, 1])
    before = live.info
    rendered = editor.render_headless(frames=4, exercise=True)
    assert rendered["vertices"] > 0
    assert live.view["crosshair"] == [1, 2, 1]
    assert live.info["revision"] == before["revision"]
    assert live.info["undo_steps"] == before["undo_steps"]
    panels = rendered["orthogonal_planes"]
    assert [(p["plane"], p["width"], p["height"]) for p in panels] == [
        ("XY", 5, 4),
        ("XZ", 5, 3),
        ("YZ", 4, 3),
    ]
    for panel, ratio in zip(panels, [4, 12, 3]):
        assert panel["scale"][1] / panel["scale"][0] == pytest.approx(ratio)
    repeated = editor.render_headless(frames=4)
    assert [p["builds"] for p in repeated["orthogonal_planes"]] == [
        p["builds"] for p in panels
    ]
    editor.apply_operation("add", {"value": 1}, asynchronous=False)
    refreshed = editor.render_headless(frames=3)
    assert all(
        p["builds"] > old["builds"]
        for p, old in zip(refreshed["orthogonal_planes"], panels)
    )


def test_calibrated_selection_gestures(editor):
    document = Document(np.zeros((32, 40), np.uint8))
    document.set_calibration(Calibration(pixel_width=0.5, pixel_height=2))
    editor.add_document(document)
    rendered = editor.render_headless(exercise=True)
    assert rendered["selection"]["points"] == [[3, 4], [13, 14]]


def test_physical_zoom_roi_outline_and_selective_plane_refresh(editor):
    from imagejxx.ui.model import Selection

    document = Document(np.arange(60, dtype=np.uint16).reshape(3, 4, 5), is_stack=True)
    document.set_calibration(Calibration(0.5, 2, 6, "um"))
    editor.add_document(document)
    live = editor.live_document()
    live.set_view(crosshair=[1, 2, 1])
    live.select(Selection("rectangle", [(1, 1), (4, 3)]))
    editor.action("add_roi")
    live.set_view(orthogonal=True, crosshair=[1, 2, 1], zoom=2)
    rendered = editor.render_headless(4)
    panels = rendered["orthogonal_planes"]
    np.testing.assert_allclose([p["scale"] for p in panels], [[1, 4], [1, 12], [4, 12]])
    assert panels[0]["selection_spans"] > 0 and panels[0]["roi_spans"] > 0
    live.set_view(crosshair=[2, 2, 1])
    moved = editor.render_headless(4)["orthogonal_planes"]
    assert [p["builds"] - old["builds"] for p, old in zip(moved, panels)] == [0, 0, 1]
    live.set_view(linked_zoom=False, plane_zoom=[2, 3, 4])
    independent = editor.render_headless(4)["orthogonal_planes"]
    np.testing.assert_allclose(
        [p["scale"] for p in independent], [[1, 4], [1.5, 18], [8, 24]]
    )


@pytest.mark.skipif(
    not os.environ.get("IMAGEJXX_RENDER_TESTS"),
    reason="Enable hidden OpenGL rendering checks",
)
@pytest.mark.parametrize("orthogonal", [False, True])
def test_hidden_opengl_orthogonal_views(editor, tmp_path, orthogonal):
    document = Document(
        np.arange(24 * 32 * 40, dtype=np.uint16).reshape(24, 32, 40), is_stack=True
    )
    document.set_calibration(Calibration(0.5, 1, 2, "um"))
    editor.add_document(document)
    editor.live_document().set_view(orthogonal=orthogonal, crosshair=[20, 16, 12])
    output = tmp_path / "xyz.png"
    assert editor.run(hidden=True, frames=6, screenshot=output) == 0
    from PIL import Image

    assert Image.open(output).size == (1250, 820)
    # Recreating a graphics context must recreate the panel textures.
    assert editor.run(hidden=True, frames=3) == 0


def test_ordinary_canvas_uses_persisted_physical_view(editor, tmp_path):
    document = Document(np.zeros((3, 4, 5), np.uint16), is_stack=True)
    document.set_calibration(Calibration(0.5, 2, 6, "um"))
    editor.add_document(document)
    live = editor.live_document()
    live.set_view(zoom=2, pan=[[7, -9], [1, 2], [3, 4]], crosshair=[3, 2, 1])
    before = live.info
    rendered = editor.render_headless()
    np.testing.assert_allclose(rendered["ordinary_canvas"]["scale"], [1, 4])
    assert live.view["pan"][0] == [7, -9]
    assert live.info["revision"] == before["revision"]
    assert live.info["undo_steps"] == before["undo_steps"]
    live.set_view(orthogonal=True)
    orthogonal = editor.render_headless()["orthogonal_planes"]
    np.testing.assert_allclose(orthogonal[0]["scale"], [1, 4])
    live.set_view(orthogonal=False)
    path = tmp_path / "camera.ijx"
    editor.action("save_workspace", path=str(path))
    extensions = {}
    documents, _, _ = load_session(path, extensions=extensions)
    assert documents[0].view_state == live.view
    from imagejxx.ui.app import Editor

    reopened = Editor(preferences=Preferences(tmp_path / "reopened.json"))
    reopened.open_file(path)
    assert reopened.live_document().view == live.view
    np.testing.assert_allclose(
        reopened.render_headless()["ordinary_canvas"]["scale"], [1, 4]
    )


@pytest.mark.parametrize("orthogonal", [False, True])
@pytest.mark.parametrize("linked", [False, True])
def test_selection_fit_is_calibrated_and_keeps_pixel_history(
    editor, orthogonal, linked
):
    document = Document(np.zeros((3, 32, 40), np.uint16), is_stack=True)
    document.set_calibration(Calibration(0.5, 2, 6, "um"))
    editor.add_document(document)
    live = editor.live_document()
    live.select(Selection("rectangle", [(4, 3), (12, 11)]))
    live.set_view(
        orthogonal=orthogonal, linked_zoom=linked, zoom=2, plane_zoom=[2, 3, 4]
    )
    before = live.info
    live.fit_selection(extent=(160, 100))
    assert (live.view["zoom"] if linked else live.view["plane_zoom"][0]) == 6
    assert live.view["pan"][0] == [36, 108]
    if not linked:
        assert live.view["plane_zoom"][1:] == [3, 4]
    rendered = editor.render_headless()
    live.fit_selection()  # The same path used by the toolbar, with its real extent.
    extent = (
        rendered["orthogonal_planes"][0]["extent"]
        if orthogonal
        else rendered["ordinary_canvas"]["extent"]
    )
    zoom = live.view["zoom"] if linked else live.view["plane_zoom"][0]
    assert zoom == pytest.approx(0.96 * min(extent[0] / 4, extent[1] / 16))
    for key in ["revision", "undo_steps", "redo_steps", "dirty", "pixel_storage"]:
        assert live.info[key] == before[key]
    unchanged = live.view
    with pytest.raises(ValueError):
        live.fit_selection(extent=(160, 0))
    assert live.view == unchanged


def test_saved_fit_view_is_preserved_when_default_zoom_is_configured(editor, tmp_path):
    editor.update_settings(default_zoom=2)
    editor.add_document(Document(np.zeros((32, 40), np.uint8)))
    live = editor.live_document()
    assert editor.render_headless()["ordinary_zoom"] == 2
    live.set_view(zoom=0)
    path = tmp_path / "fit.ijx"
    editor.action("save_workspace", path=str(path))
    from imagejxx.ui.app import Editor

    reopened = Editor(preferences=Preferences(tmp_path / "prefs.json"))
    reopened.open_file(path)
    assert reopened.live_document().view["zoom"] == 0
    reopened.render_headless()
    assert reopened.live_document().view["zoom"] == 0


@pytest.mark.parametrize("orthogonal", [False, True])
def test_actual_camera_gestures_persist_without_pixel_edits(editor, orthogonal, tmp_path):
    document = Document(np.zeros((3, 32, 40), np.uint16), is_stack=True)
    document.set_calibration(Calibration(0.5, 2, 6, "um"))
    editor.add_document(document)
    live = editor.live_document()
    live.set_view(orthogonal=orthogonal, zoom=2)
    before = live.info
    layout = editor.render_headless(frames=4)
    if orthogonal:
        pane = layout["orthogonal_planes"][0]
        requested_pivot = np.asarray(pane["origin"]) + np.asarray(pane["scale"]) * [20.5, 16.5]
        canvas_center = np.asarray(pane["canvas_point"])
    else:
        pane = layout["ordinary_canvas"]
        requested_pivot = np.asarray(pane["origin"]) + np.asarray(pane["scale"]) * [20, 16]
        canvas_center = np.asarray(layout["orthogonal_navigation"]["ordinary_canvas_point"])
    # Pinned ImGui floors MousePos to integer raster coordinates before the
    # cursor-anchored 1.2x wheel zoom; the middle drag is exactly (17, -11).
    old_pan = np.asarray(before["view"]["pan"][0])
    expected_pan = old_pan - (np.floor(requested_pivot) - canvas_center - old_pan) * 0.2 + [17, -11]
    rendered = editor.render_headless(frames=4, camera=True)
    _xyz_diagnostic(tmp_path, "legacy-camera", str(orthogonal), before, rendered, [], live.info)
    assert live.view["zoom"] == pytest.approx(2.4)
    np.testing.assert_allclose(live.view["pan"][0], expected_pan, rtol=0, atol=1e-6)
    for key in ["revision", "undo_steps", "redo_steps", "dirty"]:
        assert live.info[key] == before[key]
    camera = live.view
    live.apply_operation("add", {"value": 1}, asynchronous=False)
    editor.action("undo")
    assert live.view == camera


# P05b uses project-owned scientific data. These expectations deliberately do
# not call the fixture generator, native extraction or either UI producer.
_XYZ_SHAPE = (9, 17, 32)
_XYZ_INITIAL = (7, 5, 3)
_XYZ_SOURCE_SHA = "b3e0f65e2f86dd2790085a5e7023ed9a336258c7bfb73e5a1820509ecc1ae51c"
_XYZ_INITIAL_PLANE_SHA = {
    "xy": "00e642b096d4464a9be44cf649d867331f59532ef0aff46244ef65614f2f2091",
    "xz": "28de57c30d6e32e46829805f59e7a3d211c8678100aeff5b9bdffa0753052b68",
    "yz": "ae1c1a123840804ff5fe4e860eec68f7d45b48d72eb5b086e690a6e5ffa51add",
}
_XYZ_PLANES = ("xy", "xz", "yz")
_XYZ_FIXED_AXIS = {"xy": 2, "xz": 1, "yz": 0}
_XYZ_LIMITS = (31, 16, 8)
_XYZ_CASES = (
    "activation-keyboard", "tab-order-and-exit", "enter-space-plane",
    "click-activation", "hover-is-not-focus", "plain-direction-all-planes",
    "shift-reference-direction-all-planes", "edge-clamp-plain-and-shift",
    "combined-keys-and-release", "repeat-bounded",
    "ctrl-alt-super-and-key-ownership", "text-focus-suppression",
    "numeric-control-ownership", "blocked-hidden-and-busy", "document-switch",
    "exact-scientific-planes-and-cache", "state-invariants",
    "session-and-pixel-undo", "escape-exits-orthogonal", "installed-two-contexts",
)


def _xyz_bytes(values):
    return b"".join(struct.pack("<f", value) for value in values)


def _xyz_source():
    raw = _xyz_bytes(
        10000 * z + 100 * y + x
        for z in range(9) for y in range(17) for x in range(32)
    )
    assert hashlib.sha256(raw).hexdigest() == _XYZ_SOURCE_SHA
    return np.frombuffer(raw, dtype="<f4").reshape(_XYZ_SHAPE).copy()


def _xyz_expected(xyz):
    x, y, z = xyz
    assert all(type(value) is int for value in xyz)
    assert all(0 <= value <= bound for value, bound in zip(xyz, _XYZ_LIMITS))
    pixels = {
        "xy": (10000 * z + 100 * row + col for row in range(17) for col in range(32)),
        "xz": (10000 * row + 100 * y + col for row in range(9) for col in range(32)),
        "yz": (10000 * row + 100 * col + x for row in range(9) for col in range(17)),
    }
    shapes = {"xy": (17, 32), "xz": (9, 32), "yz": (9, 17)}
    planes = {name: _xyz_bytes(values) for name, values in pixels.items()}
    world = (
        float(Decimal(10) - Decimal("1.2") * y),
        float(Decimal(-5) + Decimal("0.4") * x),
        float(Decimal(2) + Decimal(3) * z),
    )
    return {
        "crosshair_xyz": list(xyz), "world_xyz": list(world),
        "landmark": 10000 * z + 100 * y + x,
        "planes": {
            name: {"shape": shapes[name], "bytes": raw,
                   "sha256": hashlib.sha256(raw).hexdigest()}
            for name, raw in planes.items()
        },
    }


def _xyz_step(xyz, plane, key, shift=False):
    result = list(xyz)
    if shift:
        axis, delta = {"Left": (0, -1), "Right": (0, 1),
                       "Up": (1, -1), "Down": (1, 1)}[key]
    else:
        axis = _XYZ_FIXED_AXIS[plane]
        delta = 1 if key in ("Up", "PageUp") else -1
    result[axis] = min(_XYZ_LIMITS[axis], max(0, result[axis] + delta))
    return tuple(result)


def _xyz_calibration():
    return ij.SpatialCalibration(
        spacing=(0.4, 1.2, 3), origin=(10, -5, 2),
        direction=(0, -1, 0, 1, 0, 0, 0, 0, 1), unit="um",
    )


def _xyz_core_owner(title="P05b scientific XYZ"):
    owner = ij.Document(ij.Dataset(_xyz_source(), axes="ZYX", calibration=_xyz_calibration()), title)
    tx = owner.begin_edit()
    selection = ij.Selection("rectangle", [(2, 2), (5, 4)])
    tx.select(selection)
    tx.set_rois([ij.ROI("preserved", selection, z=3, attributes={"group": 2})])
    overlay = Overlay.from_dict({"kind": "text", "text": "retained", "x": 2, "y": 2}).to_dict()
    assert overlay == {"kind": "text", "text": "retained", "x": 2, "y": 2,
                       "color": "#ffcc00", "font_path": None, "font_size": 16,
                       "points": [], "slice_index": None, "width": 1}
    tx.set_overlays([overlay])
    tx.set_extension("p05b", {"preserved": True})
    owner.commit(tx)
    display = owner.snapshot().display
    display.crosshair = list(_XYZ_INITIAL)
    display.orthogonal = True
    # Keep every voxel wide enough to contain an actual raster mouse pixel.
    display.zoom = 4
    display.properties = {"display_range": [0, 81631]}
    owner.set_display(display)
    return owner


def _xyz_assert_dataset(dataset, xyz, *, require_float32=True):
    expected = _xyz_expected(tuple(xyz))
    if require_float32:
        assert dataset.dtype == np.dtype(np.float32)
    assert dataset.axis_sizes == {"T": 1, "C": 1, "Z": 9, "Y": 17, "X": 32, "S": 1}
    assert hashlib.sha256(np.asarray(dataset.numpy("ZYX"), dtype="<f4").tobytes()).hexdigest() == _XYZ_SOURCE_SHA
    for name, axis in _XYZ_FIXED_AXIS.items():
        observed = dataset.plane(name, xyz[axis])
        assert observed.shape == expected["planes"][name]["shape"]
        assert np.asarray(observed, dtype="<f4").tobytes() == expected["planes"][name]["bytes"]
        geometry = dataset.plane_geometry(name, xyz[axis])
        assert geometry["horizontal_axis"] == {"xy": "X", "xz": "X", "yz": "Y"}[name]
        assert geometry["vertical_axis"] == ("Y" if name == "xy" else "Z")
        np.testing.assert_allclose(
            geometry["horizontal_step"], (0, 0.4, 0) if name != "yz" else (-1.2, 0, 0),
            rtol=0, atol=1e-12,
        )
        np.testing.assert_allclose(geometry["vertical_step"], (-1.2, 0, 0) if name == "xy" else (0, 0, 3), rtol=0, atol=1e-12)
    np.testing.assert_allclose(dataset.calibration.voxel_to_world(xyz), expected["world_xyz"], rtol=0, atol=1e-12)
    np.testing.assert_allclose(dataset.calibration.world_to_voxel(expected["world_xyz"]), xyz, rtol=0, atol=1e-12)


def test_xyz_independent_float32_fixture_and_initial_hashes():
    source = _xyz_source()
    assert source.shape == (9, 17, 32) and source.size == 4896
    assert source[3, 5, 7] == 30507
    expected = _xyz_expected(_XYZ_INITIAL)
    assert expected["world_xyz"] == [4.0, -2.2, 11.0]
    assert {name: value["sha256"] for name, value in expected["planes"].items()} == _XYZ_INITIAL_PLANE_SHA
    _xyz_assert_dataset(ij.Dataset(source, axes="ZYX", calibration=_xyz_calibration()), _XYZ_INITIAL)


_XYZ_DIRECTIONS = [
    (plane, key, shift)
    for plane in _XYZ_PLANES for shift in (False, True)
    for key in (("Up", "PageUp", "Down", "PageDown") if not shift else ("Left", "Right", "Up", "Down"))
]


@pytest.mark.parametrize("plane,key,shift", _XYZ_DIRECTIONS)
def test_xyz_headless_application_display_and_scientific_state(plane, key, shift):
    """Public core state publication; UI dispatch is proved by separate tests."""
    owner = _xyz_core_owner()
    app = ij.Application()
    identity = app.adopt(owner)
    before = app.snapshot(identity)
    history = app.history(identity)
    display = before.display
    expected = _xyz_step(_XYZ_INITIAL, plane, key, shift)
    display.crosshair = list(expected)
    app.set_display(display, identity)
    after = app.snapshot(identity)
    assert list(after.display.crosshair) == list(expected)
    assert after.display.orthogonal and after.shares_data(before)
    assert after.data_identity == before.data_identity
    assert after.id == before.id and after.metadata == before.metadata
    assert after.dirty == before.dirty
    assert (after.revisions.pixels, after.revisions.metadata, after.revisions.selection) == (
        before.revisions.pixels, before.revisions.metadata, before.revisions.selection)
    assert after.revisions.display > before.revisions.display
    assert (app.history(identity).undo_steps, app.history(identity).redo_steps) == (history.undo_steps, history.redo_steps)
    assert (after.display.t, after.display.c, after.display.zoom, after.display.pan) == (
        before.display.t, before.display.c, before.display.zoom, before.display.pan)
    _xyz_assert_dataset(after.dataset, expected)


@pytest.mark.parametrize("axis,bound", list(enumerate(_XYZ_LIMITS)))
def test_xyz_headless_bounds_and_invalid_publication_are_atomic(axis, bound):
    owner = _xyz_core_owner()
    app = ij.Application()
    identity = app.adopt(owner)
    for endpoint in (0, bound):
        display = app.snapshot(identity).display
        xyz = list(_XYZ_INITIAL)
        xyz[axis] = endpoint
        display.crosshair = xyz
        app.set_display(display, identity)
        _xyz_assert_dataset(app.snapshot(identity).dataset, xyz)
        before = app.snapshot(identity)
        history = app.history(identity)
        invalid = before.display
        invalid.crosshair = [(-1 if endpoint == 0 else bound + 1) if index == axis else value
                             for index, value in enumerate(xyz)]
        with pytest.raises((ValueError, IndexError)):
            app.set_display(invalid, identity)
        after = app.snapshot(identity)
        assert after.shares_data(before) and list(after.display.crosshair) == xyz
        assert after.revisions.display == before.revisions.display
        assert (app.history(identity).undo_steps, app.history(identity).redo_steps) == (history.undo_steps, history.redo_steps)


@pytest.fixture
def xyz_editor(editor):
    live = editor.add_native_document(_xyz_core_owner())
    assert live.native_snapshot().dataset.dtype == np.dtype(np.float32)
    return editor, live


def _xyz_keys(*names, down=True, frames=1):
    return {"type": "keys", "keys": [{"name": name, "down": down} for name in names], "frames": frames}


def _xyz_press(*names, frames=1):
    return [_xyz_keys(*names, frames=frames), _xyz_keys(*names, down=False)]


def _xyz_position(point):
    return {"type": "position", "point": list(point), "space": "screen"}


def _xyz_click(point):
    return [_xyz_position(point), {"type": "button", "name": "left", "down": True},
            {"type": "button", "name": "left", "down": False}]


def _xyz_panel(rendered, plane):
    rows = rendered["orthogonal_planes"]
    assert len(rows) == 3
    return next(row for row in rows if row["plane"].lower() == plane)


def _xyz_focused_plane(observed):
    value = observed["orthogonal_navigation"]["focused_plane"]
    assert type(value) is int and -1 <= value <= 2
    return None if value == -1 else _XYZ_PLANES[value]


def _xyz_crosshair_point(rendered, plane, xyz):
    row = _xyz_panel(rendered, plane)
    horizontal, vertical = {"xy": (xyz[0], xyz[1]), "xz": (xyz[0], xyz[2]),
                            "yz": (xyz[1], xyz[2])}[plane]
    point = []
    for axis, coordinate in enumerate((horizontal, vertical)):
        minimum = row["origin"][axis] + coordinate * row["scale"][axis]
        maximum = row["origin"][axis] + (coordinate + 1) * row["scale"][axis]
        assert np.isfinite(minimum) and np.isfinite(maximum) and minimum < maximum
        first_pixel = int(np.ceil(minimum))
        last_pixel = int(np.ceil(maximum)) - 1
        assert first_pixel <= last_pixel, "intended voxel has no raster mouse pixel"
        pixel = min(last_pixel, max(first_pixel, int(np.floor((minimum + maximum) / 2 + 0.5))))
        assert minimum <= pixel < maximum
        canvas_minimum = row["canvas_point"][axis] - row["extent"][axis] / 2
        canvas_maximum = row["canvas_point"][axis] + row["extent"][axis] / 2
        assert canvas_minimum <= pixel < canvas_maximum
        point.append(pixel)
    return point


def _xyz_event_frames(rendered, index):
    rows = [row for row in rendered["input_frames"] if row["event_index"] == index]
    assert rows, f"actual event {index} has no frame observations"
    return rows


def _xyz_assert_trace(rendered, events):
    required = {"document_info", "crosshair_xyz", "world_xyz", "orthogonal_navigation",
                "orthogonal_planes", "focus", "keys_down", "modifiers", "busy",
                "job_state", "frame", "event_index", "phase", "delta_time", "raw_inputs",
                "repeat_delay", "repeat_rate"}
    rows = rendered["input_frames"]
    assert len(rows) <= 360 and rows
    assert [row["frame"] for row in rows] == sorted({row["frame"] for row in rows})
    for row in rows:
        assert required <= row.keys()
        assert row["delta_time"] == pytest.approx(1 / 60, abs=1e-9, rel=0)
        assert row["repeat_delay"] == pytest.approx(0.275, abs=1e-8, rel=0)
        assert row["repeat_rate"] == pytest.approx(0.05, abs=1e-8, rel=0)
        assert {"shift", "ctrl", "alt", "super"} == row["modifiers"].keys()
        assert all(type(value) is bool for value in row["modifiers"].values())
        assert type(row["busy"]) is bool
        xyz = row["crosshair_xyz"]
        assert xyz == row["document_info"]["view"]["crosshair"]
        expected = _xyz_expected(tuple(xyz))
        np.testing.assert_allclose(row["world_xyz"], expected["world_xyz"], rtol=0, atol=1e-12)
        if row["document_info"]["view"]["orthogonal"]:
            for plane in _XYZ_PLANES:
                panel = _xyz_panel(row, plane)
                assert {"focused", "slice_focused", "visible", "canvas_id", "slice_id",
                        "canvas_point", "slice_point", "keyboard_admitted", "key_owners"} <= panel.keys()
                assert type(panel["visible"]) is bool
                if panel["visible"]:
                    # Undrawn warmup rows can still describe an unbuilt cache.
                    assert panel["coordinate"] == xyz[_XYZ_FIXED_AXIS[plane]]
                    assert (panel["height"], panel["width"]) == expected["planes"][plane]["shape"]
    for index, event in enumerate(events):
        frames = _xyz_event_frames(rendered, index)
        assert len(frames) == event.get("frames", 1)
        assert all(row["phase"] == "input" for row in frames)
        assert frames[0]["raw_inputs"] == [event]
        assert all(row["raw_inputs"] == [] for row in frames[1:])
        if event["type"] in ("keys", "key"):
            first = frames[0]
            atoms = event["keys"] if event["type"] == "keys" else [event]
            for atom in atoms:
                name = atom["name"]
                if name in ("Shift", "Ctrl", "Alt", "Super"):
                    assert first["modifiers"][name.lower()] is atom["down"]
                else:
                    assert (name in first["keys_down"]) is atom["down"]


def _xyz_assert_unchanged(before, after):
    """Compare observed data/history; metadata setup already made one edit."""
    for key in ("id", "revision", "dirty", "undo_steps", "redo_steps", "calibration",
                "selection", "axis_sizes", "history_pixel_bytes", "pixel_storage"):
        assert after[key] == before[key], key
    for key in ("pixels", "metadata", "selection"):
        assert after["revisions"][key] == before["revisions"][key], key
    for key in ("zoom", "plane_zoom", "pan", "linked_zoom"):
        assert after["view"][key] == before["view"][key], key
    assert (after["axis_sizes"]["T"], after["axis_sizes"]["C"]) == (1, 1)


def _xyz_assert_live(live, xyz, before=None, snapshot=None):
    assert live.view["crosshair"] == list(xyz)
    if before is not None:
        _xyz_assert_unchanged(before, live.info)
    current = live.native_snapshot()
    _xyz_assert_dataset(current.dataset, xyz)
    if snapshot is not None:
        assert current.data_identity == snapshot.data_identity
        assert current.shares_data(snapshot) and current.metadata == snapshot.metadata
        assert current.overlays == snapshot.overlays
        assert len(current.rois) == len(snapshot.rois)
        assert (current.display.t, current.display.c) == (snapshot.display.t, snapshot.display.c)
    expected = _xyz_expected(tuple(xyz))
    for name in _XYZ_PLANES:
        assert np.asarray(live.plane(name), dtype="<f4").tobytes() == expected["planes"][name]["bytes"]


def _xyz_native_state(snapshot):
    def selection(value):
        if value is None:
            return None
        return {"kind": value.kind, "points": value.points, "width": value.width,
                "attributes": value.attributes}

    cal = snapshot.dataset.calibration
    display = snapshot.display
    return {
        "id": snapshot.id, "data_identity": snapshot.data_identity,
        "metadata": snapshot.metadata, "selection": selection(snapshot.selection),
        "rois": [{"name": roi.name, "selection": selection(roi.selection),
                  "t": roi.t, "c": roi.c, "z": roi.z, "attributes": roi.attributes}
                 for roi in snapshot.rois],
        "overlays": snapshot.overlays,
        "calibration": {"spacing": cal.spacing, "origin": cal.origin,
                        "direction": cal.direction, "unit": cal.unit},
        "t": display.t, "c": display.c,
    }


def _xyz_record(tmp_path, case, scenario, live, before, rendered, events, *, extra=None):
    directory = Path(os.environ.get("IMAGEJXX_ORTHOGONAL_REPORT_DIR", tmp_path / "xyz-reports"))
    directory.mkdir(parents=True, exist_ok=True)
    stem = f"{case}--{scenario}"
    expected = _xyz_expected(tuple(live.view["crosshair"]))
    artifacts = {}
    actual = {"source": live.native_snapshot().dataset.numpy("ZYX")}
    actual.update({name: live.plane(name) for name in _XYZ_PLANES})
    for name, values in actual.items():
        raw = np.asarray(values, dtype="<f4").tobytes()
        path = directory / f"{stem}-{name}.float32le"
        path.write_bytes(raw)
        artifacts[name] = {"path": str(path.resolve()), "sha256": hashlib.sha256(raw).hexdigest(),
                           "shape": list(values.shape), "dtype": "Float32LE", "bytes": len(raw)}
    expected_json = {key: value for key, value in expected.items() if key != "planes"}
    expected_json["planes"] = {name: {"shape": list(value["shape"]), "sha256": value["sha256"]}
                               for name, value in expected["planes"].items()}
    after = live.info
    after["source_snapshot"] = _xyz_native_state(live.native_snapshot())
    report = {"schema_version": 1, "context": "imgui_python", "case": case, "scenario": scenario,
              "input_events": events, "before": before, "after": after,
              "rendered": rendered, "expected": expected_json, "artifacts": artifacts}
    if extra is not None:
        report["observations"] = extra
    (directory / f"{stem}.json").write_text(json.dumps(report, allow_nan=False, indent=2) + "\n", encoding="utf-8")
    return report


def _xyz_diagnostic(tmp_path, case, scenario, before, rendered, events, after):
    directory = Path(os.environ.get("IMAGEJXX_ORTHOGONAL_REPORT_DIR", tmp_path / "xyz-reports"))
    directory.mkdir(parents=True, exist_ok=True)
    attempt = 1
    while True:
        path = directory / f"diagnostic-{case}--{scenario}-{attempt:03d}.json"
        if not path.exists():
            break
        attempt += 1
    report = {"verification_status": "unverified", "purpose": "pre-assert input diagnostics",
              "case": case, "scenario": scenario, "input_events": events,
              "before": before, "after": after, "rendered": rendered}
    with path.open("x", encoding="utf-8") as stream:
        stream.write(json.dumps(report, allow_nan=False, indent=2) + "\n")


def _xyz_run(xyz_editor, plane, events, *, prefix=True, busy=False, diagnostic=None):
    editor, live = xyz_editor
    before, snapshot = live.info, live.native_snapshot()
    before["source_snapshot"] = _xyz_native_state(snapshot)
    layout = editor.render_headless(frames=3)
    sequence = (_xyz_click(_xyz_crosshair_point(layout, plane, before["view"]["crosshair"]))
                if prefix else []) + events
    rendered = editor.render_headless(frames=3, events=sequence, exercise_orthogonal_busy=busy)
    if diagnostic is not None:
        _xyz_diagnostic(*diagnostic, before, rendered, sequence, live.info)
    assert not editor.error
    _xyz_assert_trace(rendered, sequence)
    return before, snapshot, rendered, sequence


@pytest.mark.parametrize("plane,key,shift", _XYZ_DIRECTIONS)
def test_xyz_actual_plain_and_shift_all_planes(xyz_editor, tmp_path, plane, key, shift):
    keys = ("Shift", key) if shift else (key,)
    before, snapshot, rendered, events = _xyz_run(xyz_editor, plane, _xyz_press(*keys))
    live = xyz_editor[1]
    expected = _xyz_step(_XYZ_INITIAL, plane, key, shift)
    _xyz_assert_live(live, expected, before, snapshot)
    pressed = _xyz_event_frames(rendered, 3)[0]
    assert _xyz_focused_plane(pressed) == plane
    assert pressed["orthogonal_navigation"]["keyboard_admitted"] is True
    assert pressed["crosshair_xyz"] == list(expected)
    released = _xyz_event_frames(rendered, 4)[0]
    assert released["crosshair_xyz"] == pressed["crosshair_xyz"]
    assert all(value is False for value in released["modifiers"].values())
    focused = [row["plane"].lower() for row in rendered["orthogonal_planes"] if row["focused"]]
    assert focused == [plane]
    case = "shift-reference-direction-all-planes" if shift else "plain-direction-all-planes"
    _xyz_record(tmp_path, case, f"{plane}-{key}", live, before, rendered, events)


_XYZ_BOUNDS = [
    (plane, key, shift, axis, endpoint)
    for plane in _XYZ_PLANES
    for key, shift, axis, endpoint in (
        ("Up", False, _XYZ_FIXED_AXIS[plane], _XYZ_LIMITS[_XYZ_FIXED_AXIS[plane]]),
        ("Down", False, _XYZ_FIXED_AXIS[plane], 0),
        ("Left", True, 0, 0), ("Right", True, 0, 31),
        ("Up", True, 1, 0), ("Down", True, 1, 16),
    )
]


@pytest.mark.parametrize("plane,key,shift,axis,endpoint", _XYZ_BOUNDS)
def test_xyz_actual_endpoint_clamps(xyz_editor, tmp_path, plane, key, shift, axis, endpoint):
    live = xyz_editor[1]
    xyz = list(_XYZ_INITIAL)
    xyz[axis] = endpoint
    live.set_view(crosshair=xyz)
    keys = ("Shift", key) if shift else (key,)
    inputs = _xyz_press(*keys, frames=40) + _xyz_press(*keys)
    before, snapshot, rendered, events = _xyz_run(xyz_editor, plane, inputs)
    _xyz_assert_live(live, xyz, before, snapshot)
    assert all(row["crosshair_xyz"] == xyz for row in rendered["input_frames"])
    _xyz_record(tmp_path, "edge-clamp-plain-and-shift", f"{plane}-{key}-{'shift' if shift else 'plain'}", live, before, rendered, events)


@pytest.mark.parametrize("keys,expected", [
    (("Up", "PageUp"), (7, 5, 4)), (("Down", "PageDown"), (7, 5, 2)),
    (("Up", "Down"), _XYZ_INITIAL), (("PageUp", "PageDown"), _XYZ_INITIAL),
    (("Shift", "Left", "Right"), _XYZ_INITIAL),
    (("Shift", "Up", "Down"), _XYZ_INITIAL),
    (("Shift", "Right", "Up"), (8, 4, 3)),
    (("Shift", "Right", "Up", "Down"), (8, 5, 3)),
])
def test_xyz_actual_combined_keys_are_one_frame(xyz_editor, tmp_path, keys, expected):
    before, snapshot, rendered, events = _xyz_run(xyz_editor, "xy", _xyz_press(*keys))
    live = xyz_editor[1]
    _xyz_assert_live(live, expected, before, snapshot)
    assert _xyz_event_frames(rendered, 3)[0]["crosshair_xyz"] == list(expected)
    assert _xyz_event_frames(rendered, 4)[0]["crosshair_xyz"] == list(expected)
    _xyz_record(tmp_path, "combined-keys-and-release", "-".join(keys), live, before, rendered, events)


@pytest.mark.parametrize("plane,keys,start,expected", [
    ("yz", ("Up",), (7, 5, 3), (16, 5, 3)),
    ("xz", ("Shift", "Right"), (7, 5, 3), (16, 5, 3)),
    ("xy", ("Shift", "Down"), (7, 5, 3), (7, 14, 3)),
])
def test_xyz_actual_repeat_has_nine_bounded_steps(xyz_editor, tmp_path, plane, keys, start, expected):
    live = xyz_editor[1]
    live.set_view(crosshair=list(start))
    inputs = [_xyz_keys(*keys, frames=40), _xyz_keys(*keys, down=False, frames=12)]
    before, snapshot, rendered, events = _xyz_run(xyz_editor, plane, inputs)
    held = _xyz_event_frames(rendered, 3)
    released = _xyz_event_frames(rendered, 4)
    positions = [tuple(row["crosshair_xyz"]) for row in held]
    deltas = np.diff(np.asarray([start] + positions), axis=0)
    moving_axis = 1 if "Down" in keys else 0
    assert np.count_nonzero(deltas[:, moving_axis]) == 9
    assert deltas[:, moving_axis].sum() == 9
    assert set(deltas[:, moving_axis]) <= {0, 1}
    assert np.all(np.delete(deltas, moving_axis, axis=1) == 0)
    assert all(row["crosshair_xyz"] == list(expected) for row in released)
    _xyz_assert_live(live, expected, before, snapshot)
    _xyz_record(tmp_path, "repeat-bounded", f"{plane}-{'-'.join(keys)}", live, before, rendered, events)


@pytest.mark.parametrize("plane", _XYZ_PLANES)
def test_xyz_actual_click_focus_survives_pointer_departure(xyz_editor, tmp_path, plane):
    inputs = [_xyz_position((1240, 800))] + _xyz_press("Up")
    before, snapshot, rendered, events = _xyz_run(xyz_editor, plane, inputs)
    live = xyz_editor[1]
    expected = _xyz_step(_XYZ_INITIAL, plane, "Up")
    _xyz_assert_live(live, expected, before, snapshot)
    pressed = _xyz_event_frames(rendered, 4)[0]
    assert _xyz_focused_plane(pressed) == plane
    assert pressed["crosshair_xyz"] == list(expected)
    _xyz_record(tmp_path, "click-activation", plane, live, before, rendered, events)


def test_xyz_actual_hover_does_not_transfer_keyboard_focus(xyz_editor, tmp_path):
    editor, live = xyz_editor
    layout = editor.render_headless()
    inputs = [_xyz_position(_xyz_crosshair_point(layout, "yz", _XYZ_INITIAL))]
    inputs += _xyz_press("Up") + _xyz_press("PageUp") + _xyz_press("Shift", "Right")
    before, snapshot, rendered, events = _xyz_run(xyz_editor, "xz", inputs)
    _xyz_assert_live(live, (8, 7, 3), before, snapshot)
    assert all(_xyz_focused_plane(row) == "xz"
               for row in rendered["input_frames"] if row["event_index"] >= 3)
    _xyz_record(tmp_path, "hover-is-not-focus", "xz-focused-yz-hovered", live, before, rendered, events)


@pytest.mark.parametrize("plane,key", [(plane, key) for plane in _XYZ_PLANES for key in ("Enter", "Space")])
def test_xyz_actual_canvas_enter_space_do_not_place_crosshair(xyz_editor, tmp_path, plane, key):
    inputs = [_xyz_position((1240, 800))] + _xyz_press(key)
    before, snapshot, rendered, events = _xyz_run(xyz_editor, plane, inputs)
    live = xyz_editor[1]
    _xyz_assert_live(live, _XYZ_INITIAL, before, snapshot)
    assert _xyz_focused_plane(_xyz_event_frames(rendered, 4)[0]) == plane
    _xyz_record(tmp_path, "enter-space-plane", f"{plane}-{key}", live, before, rendered, events)


@pytest.mark.parametrize("reverse", [False, True])
def test_xyz_actual_tab_order_and_group_exit(xyz_editor, tmp_path, reverse):
    order = [("xz", "slice"), ("xz", "canvas"), ("yz", "slice"), ("yz", "canvas"), None]
    start = "xy"
    if reverse:
        order = [("yz", "slice"), ("xz", "canvas"), ("xz", "slice"), ("xy", "canvas"), ("xy", "slice"), None]
        start = "yz"
    inputs = []
    preparation_events = 0
    if not reverse:
        # Use auto-focused XY and actual keyboard preparation, as the native
        # producer does. A first Tab after mouse focus may only activate the
        # current canvas, so this forward case has no preceding pointer click.
        for names in (("Shift", "Tab"), ("Tab",)):
            pulse = _xyz_press(*names)
            pulse[-1]["frames"] = 2
            inputs += pulse
        preparation_events = len(inputs)
    for _ in order:
        pulse = _xyz_press(*(("Shift", "Tab") if reverse else ("Tab",)))
        pulse[-1]["frames"] = 2  # Observe the applied, settled ImGui Tab result.
        inputs += pulse
    before, snapshot, rendered, events = _xyz_run(
        xyz_editor, start, inputs, prefix=reverse,
        diagnostic=(tmp_path, "tab-order-and-exit", "reverse" if reverse else "forward"))
    live = xyz_editor[1]
    _xyz_assert_live(live, _XYZ_INITIAL, before, snapshot)
    event_prefix = 3 if reverse else 0
    baseline = _xyz_event_frames(rendered, 2 if reverse else -1)[-1]
    builds = [panel["builds"] for panel in baseline["orthogonal_planes"]]
    if not reverse:
        for event_index, target in ((1, "slice"), (3, "canvas")):
            prepared = _xyz_event_frames(rendered, event_index)[-1]
            panel = _xyz_panel(prepared, "xy")
            assert prepared["focus"]["nav_id"] == panel[f"{target}_id"]
            assert panel["slice_focused" if target == "slice" else "focused"] is True
            assert prepared["crosshair_xyz"] == list(_XYZ_INITIAL)
            assert [panel["builds"] for panel in prepared["orthogonal_planes"]] == builds
    for index, expected in enumerate(order):
        observed = _xyz_event_frames(rendered, event_prefix + 1 + preparation_events + index * 2)[-1]
        ids = [panel[key] for panel in observed["orthogonal_planes"] for key in ("canvas_id", "slice_id")]
        if expected is None:
            assert observed["focus"]["nav_id"] != 0
            assert observed["focus"]["nav_id"] not in ids
        else:
            plane, target = expected
            panel = _xyz_panel(observed, plane)
            assert observed["focus"]["nav_id"] == panel[f"{target}_id"]
            assert panel["focused" if target == "canvas" else "slice_focused"] is True
        assert observed["crosshair_xyz"] == list(_XYZ_INITIAL)
        assert [panel["builds"] for panel in observed["orthogonal_planes"]] == builds
    _xyz_record(tmp_path, "tab-order-and-exit", "reverse" if reverse else "forward", live, before, rendered, events)


_XYZ_MODIFIER_CASES = [
    (modifier, shift, key)
    for modifier in (("Ctrl",), ("Alt",), ("Super",), ("Ctrl", "Alt"),
                     ("Ctrl", "Super"), ("Alt", "Super"), ("Ctrl", "Alt", "Super"))
    for shift in (False, True)
    for key in ("Left", "Right", "Up", "Down", "PageUp", "PageDown", "Escape")
]


@pytest.mark.parametrize("modifier,shift,key", _XYZ_MODIFIER_CASES)
def test_xyz_actual_modified_keys_have_no_local_dispatch(xyz_editor, tmp_path, modifier, shift, key):
    names = modifier + (("Shift", key) if shift else (key,))
    before, snapshot, rendered, events = _xyz_run(xyz_editor, "xz", _xyz_press(*names))
    live = xyz_editor[1]
    _xyz_assert_live(live, _XYZ_INITIAL, before, snapshot)
    assert live.view["orthogonal"] is True
    pressed = _xyz_event_frames(rendered, 3)[0]
    assert pressed["orthogonal_navigation"]["keyboard_admitted"] is False
    assert pressed["crosshair_xyz"] == list(_XYZ_INITIAL)
    _xyz_record(tmp_path, "ctrl-alt-super-and-key-ownership", "-".join(names), live, before, rendered, events)


@pytest.mark.parametrize("names", [("Left",), ("Right",), ("Home",), ("End",),
                                    ("Shift", "PageUp"), ("Shift", "PageDown"), ("Shift", "Escape")])
def test_xyz_actual_unassigned_navigation_keys_do_not_change_state(xyz_editor, tmp_path, names):
    before, snapshot, rendered, events = _xyz_run(xyz_editor, "yz", _xyz_press(*names))
    live = xyz_editor[1]
    _xyz_assert_live(live, _XYZ_INITIAL, before, snapshot)
    assert live.view["orthogonal"] is True
    _xyz_record(tmp_path, "ctrl-alt-super-and-key-ownership", "-".join(names), live, before, rendered, events)


@pytest.mark.parametrize("plane", _XYZ_PLANES)
def test_xyz_actual_slice_control_owns_its_keys(xyz_editor, tmp_path, plane):
    editor, live = xyz_editor
    layout = editor.render_headless()
    point = _xyz_panel(layout, plane)["slice_point"]
    # A plain pointer click leaves an inactive slider with only NavId focus.
    # Ctrl-click enters the actual numeric editor so it owns these arrows.
    events = [_xyz_keys("Ctrl")] + _xyz_click(point) + [{"type": "wait", "frames": 2}]
    events += [_xyz_keys("Ctrl", down=False)] + _xyz_press("Shift", "Right") + _xyz_press("Right")
    before, snapshot, rendered, events = _xyz_run(
        xyz_editor, plane, events, prefix=False, diagnostic=(tmp_path, "numeric-control-ownership", plane))
    after_click = _xyz_event_frames(rendered, 4)[-1]
    assert _xyz_panel(after_click, plane)["slice_focused"] is True
    assert after_click["focus"]["active_id"] == _xyz_panel(after_click, plane)["slice_id"] != 0
    axis = _XYZ_FIXED_AXIS[plane]
    for index in (6, 7, 8, 9):
        observed = _xyz_event_frames(rendered, index)[0]
        assert observed["orthogonal_navigation"]["keyboard_admitted"] is False
        for other in range(3):
            if other != axis:
                assert observed["crosshair_xyz"][other] == after_click["crosshair_xyz"][other]
        assert _xyz_panel(observed, plane)["slice_focused"] is True
    _xyz_assert_live(live, tuple(live.view["crosshair"]), before, snapshot)
    _xyz_record(tmp_path, "numeric-control-ownership", f"{plane}-slice", live, before, rendered, events)


def test_xyz_actual_preferences_text_owner_suppresses_plane_keys(xyz_editor, tmp_path):
    editor, live = xyz_editor
    editor.action("show_preferences", search="")  # Dialog preparation only.
    layout = editor.render_headless()
    events = _xyz_click(layout["preferences_panel"]["search_point"])
    events += [{"type": "text", "value": "orthogonal-owner"}]
    events += [_xyz_position(_xyz_crosshair_point(layout, "yz", _XYZ_INITIAL))]
    events += _xyz_press("Up") + _xyz_press("Shift", "Right")
    before, snapshot, rendered, events = _xyz_run(
        xyz_editor, "xy", events, prefix=False,
        diagnostic=(tmp_path, "text-focus-suppression", "preferences-search"))
    assert rendered["preferences_panel"]["search"] == "orthogonal-owner"
    _xyz_assert_live(live, _XYZ_INITIAL, before, snapshot)
    assert live.view["orthogonal"] is True
    for index in (5, 7):
        observed = _xyz_event_frames(rendered, index)[0]
        assert observed["orthogonal_navigation"]["keyboard_admitted"] is False
        assert observed["focus"]["active_id"] != 0
        assert observed["focus"]["active_id"] != _xyz_panel(observed, "yz")["canvas_id"]
    _xyz_record(tmp_path, "text-focus-suppression", "preferences-search", live, before, rendered, events)
    # Escape reverts the current InputText edit. Verify the typed value first,
    # then explicitly reacquire Search in a fresh context whose initial value
    # is that text; no focus continuity across contexts is assumed.
    escape_events = _xyz_click(rendered["preferences_panel"]["search_point"]) + _xyz_press("Escape")
    escape_before, escape_snapshot, escaped, escape_events = _xyz_run(
        xyz_editor, "xy", escape_events, prefix=False,
        diagnostic=(tmp_path, "text-focus-suppression", "preferences-search-escape"))
    owner = _xyz_event_frames(escaped, 2)[-1]
    assert owner["focus"]["active_id"] != 0
    assert owner["orthogonal_navigation"]["keyboard_admitted"] is False
    assert _xyz_event_frames(escaped, 3)[0]["orthogonal_navigation"]["keyboard_admitted"] is False
    assert escaped["preferences_panel"]["search"] == "orthogonal-owner"
    assert live.view["orthogonal"] is True
    _xyz_assert_live(live, _XYZ_INITIAL, escape_before, escape_snapshot)
    _xyz_record(tmp_path, "text-focus-suppression", "preferences-search-escape", live,
                escape_before, escaped, escape_events)
    recovery = _xyz_click(escaped["preferences_panel"]["close_point"])
    recovery += _xyz_click(_xyz_crosshair_point(escaped, "xz", _XYZ_INITIAL))
    recovery += _xyz_press("Up")
    recovered_before, recovered_snapshot, recovered, recovery = _xyz_run(
        xyz_editor, "xz", recovery, prefix=False,
        diagnostic=(tmp_path, "text-focus-suppression", "preferences-search-recovery"))
    assert recovered["preferences_panel"]["open"] is False
    assert _xyz_focused_plane(recovered) == "xz"
    _xyz_assert_live(live, (7, 6, 3), recovered_before, recovered_snapshot)
    _xyz_record(tmp_path, "text-focus-suppression", "preferences-search-recovery", live,
                recovered_before, recovered, recovery)


@pytest.mark.parametrize("suppression", ["hidden", "modal", "busy"])
def test_xyz_actual_hidden_modal_busy_suppression(xyz_editor, tmp_path, suppression):
    editor, live = xyz_editor
    if suppression == "hidden":
        live.set_view(orthogonal=False)  # State preparation, never gesture proof.
    elif suppression == "modal":
        editor.action("show_operation", operation="add", parameters={"value": 1})
    inputs = _xyz_press("Up") + _xyz_press("Shift", "Right") + _xyz_press("Escape")
    before, snapshot, rendered, events = _xyz_run(xyz_editor, "xy", inputs, prefix=False, busy=suppression == "busy")
    _xyz_assert_live(live, _XYZ_INITIAL, before, snapshot)
    assert live.view["orthogonal"] is (suppression != "hidden")
    for index in range(len(events)):
        for observed in _xyz_event_frames(rendered, index):
            assert observed["orthogonal_navigation"]["keyboard_admitted"] is False
            if suppression == "busy":
                assert observed["busy"] is True and observed["job_state"] == "running"
    assert not editor.busy  # Real diagnostic worker must always leave its barrier.
    _xyz_record(tmp_path, "blocked-hidden-and-busy", suppression, live, before, rendered, events)
    if suppression == "busy":
        restored_before, restored_snapshot, restored, recovery = _xyz_run(xyz_editor, "yz", _xyz_press("Up"))
        _xyz_assert_live(live, (8, 5, 3), restored_before, restored_snapshot)
        assert _xyz_focused_plane(restored) == "yz"
        assert all(row["busy"] is False for row in restored["input_frames"])
        _xyz_record(tmp_path, "blocked-hidden-and-busy", "busy-idle-recovery", live,
                    restored_before, restored, recovery)
    elif suppression == "hidden":
        recovery = _xyz_click(rendered["orthogonal_navigation"]["toggle_point"])
        recovery += [{"type": "wait", "frames": 3}] + _xyz_press("Up")
        restored_before, restored_snapshot, restored, recovery = _xyz_run(
            xyz_editor, "xy", recovery, prefix=False)
        _xyz_assert_live(live, (7, 5, 4), restored_before, restored_snapshot)
        assert live.view["orthogonal"] and _xyz_focused_plane(restored) == "xy"
        _xyz_record(tmp_path, "blocked-hidden-and-busy", "hidden-visible-recovery", live,
                    restored_before, restored, recovery)


@pytest.mark.parametrize("plane", _XYZ_PLANES)
def test_xyz_actual_escape_returns_to_ordinary_canvas_and_reenters(xyz_editor, tmp_path, plane):
    editor, live = xyz_editor
    layout = editor.render_headless()
    inputs = _xyz_press("Escape")
    # The mode toggle's position is stable when the plane group disappears.
    inputs += _xyz_click(layout["orthogonal_navigation"]["toggle_point"])
    inputs += [{"type": "wait", "frames": 3}] + _xyz_press("Up")
    before, snapshot, rendered, events = _xyz_run(xyz_editor, plane, inputs)
    escaped = _xyz_event_frames(rendered, 4)[-1]
    assert escaped["document_info"]["view"]["orthogonal"] is False
    assert _xyz_focused_plane(escaped) is None
    assert escaped["orthogonal_navigation"]["ordinary_canvas_focused"] is True
    assert escaped["focus"]["nav_id"] == escaped["orthogonal_navigation"]["ordinary_canvas_id"] != 0
    assert escaped["document_info"]["id"] == before["id"]
    reentered = _xyz_event_frames(rendered, 8)[-1]
    assert reentered["document_info"]["view"]["orthogonal"] is True
    assert _xyz_panel(reentered, "xy")["focused"] is True
    assert reentered["crosshair_xyz"] == list(_XYZ_INITIAL)
    _xyz_assert_live(live, (7, 5, 4), before, snapshot)
    assert len(editor.document_infos) == 1
    _xyz_record(tmp_path, "escape-exits-orthogonal", plane, live, before, rendered, events)


@pytest.mark.parametrize("keys,plane,changed", [
    (("Up",), "xy", "xy"), (("Up",), "xz", "xz"), (("Up",), "yz", "yz"),
    (("Shift", "Right"), "xy", "yz"), (("Shift", "Down"), "yz", "xz"),
])
def test_xyz_actual_scientific_planes_and_selective_cache(xyz_editor, tmp_path, keys, plane, changed):
    inputs = _xyz_press(*keys) + [{"type": "wait", "frames": 8}]
    before, snapshot, rendered, events = _xyz_run(xyz_editor, plane, inputs)
    before_key = _xyz_event_frames(rendered, 2)[-1]
    pressed = _xyz_event_frames(rendered, 3)[0]
    for name in _XYZ_PLANES:
        delta = _xyz_panel(pressed, name)["builds"] - _xyz_panel(before_key, name)["builds"]
        assert delta == (1 if name == changed else 0)
    final_builds = [panel["builds"] for panel in pressed["orthogonal_planes"]]
    for observed in _xyz_event_frames(rendered, 5):
        assert [panel["builds"] for panel in observed["orthogonal_planes"]] == final_builds
    expected = _xyz_step(_XYZ_INITIAL, plane, keys[-1], "Shift" in keys)
    live = xyz_editor[1]
    _xyz_assert_live(live, expected, before, snapshot)
    _xyz_record(tmp_path, "exact-scientific-planes-and-cache", f"{plane}-{'-'.join(keys)}", live, before, rendered, events)


def test_xyz_actual_keyboard_toggle_activation_preserves_crosshair(xyz_editor, tmp_path):
    # Ordinary canvas focus is explicitly granted by Escape. Its previous
    # focusable sibling is the real mode checkbox, reached by Shift+Tab.
    inputs = _xyz_press("Escape") + [{"type": "wait", "frames": 2}]
    inputs += _xyz_press("Shift", "Tab") + _xyz_press("Space", frames=1)
    inputs += [{"type": "wait", "frames": 3}]
    before, snapshot, rendered, events = _xyz_run(xyz_editor, "xy", inputs, prefix=False)
    ordinary = _xyz_event_frames(rendered, 2)[-1]
    assert ordinary["orthogonal_navigation"]["mode"] is False
    assert ordinary["orthogonal_navigation"]["ordinary_canvas_focused"] is True
    assert ordinary["focus"]["nav_id"] == ordinary["orthogonal_navigation"]["ordinary_canvas_id"] != 0
    toggle = _xyz_event_frames(rendered, 4)[-1]
    assert toggle["orthogonal_navigation"]["toggle_focused"] is True
    assert toggle["focus"]["nav_id"] == toggle["orthogonal_navigation"]["toggle_id"] != 0
    assert toggle["orthogonal_navigation"]["mode"] is False
    activated = _xyz_event_frames(rendered, 7)[-1]
    assert activated["orthogonal_navigation"]["mode"] is True
    assert _xyz_focused_plane(activated) == "xy"
    assert _xyz_panel(activated, "xy")["focused"] is True
    assert all(row["crosshair_xyz"] == list(_XYZ_INITIAL) for row in rendered["input_frames"])
    live = xyz_editor[1]
    _xyz_assert_live(live, _XYZ_INITIAL, before, snapshot)
    _xyz_record(tmp_path, "activation-keyboard", "escape-shift-tab-space", live, before, rendered, events)


def test_xyz_actual_staggered_opposing_holds_cancel(xyz_editor, tmp_path):
    inputs = [_xyz_keys("Up"), _xyz_keys("Down", frames=40),
              _xyz_keys("Up", "Down", down=False, frames=8)]
    before, snapshot, rendered, events = _xyz_run(xyz_editor, "xy", inputs)
    assert _xyz_event_frames(rendered, 3)[0]["crosshair_xyz"] == [7, 5, 4]
    assert all(row["crosshair_xyz"] == [7, 5, 4]
               for index in (4, 5) for row in _xyz_event_frames(rendered, index))
    live = xyz_editor[1]
    _xyz_assert_live(live, (7, 5, 4), before, snapshot)
    _xyz_record(tmp_path, "combined-keys-and-release", "staggered-opposing-holds", live, before, rendered, events)


def test_xyz_actual_document_tabs_reset_the_keyboard_target(xyz_editor, tmp_path):
    editor, first = xyz_editor
    second = editor.add_native_document(_xyz_core_owner("Second independent XYZ"))
    second_initial = (11, 8, 4)
    second.set_view(crosshair=list(second_initial))
    second_layout = editor.render_headless(frames=4)
    editor.active_index = 0  # API preparation of the initial visible document.
    layout = editor.render_headless(frames=4)
    tabs = {row["document_id"]: row["point"] for row in layout["document_tabs"]}
    assert set(tabs) == {first.id, second.id}
    before_first, snapshot_first = first.info, first.native_snapshot()
    before_second, snapshot_second = second.info, second.native_snapshot()
    events = _xyz_click(_xyz_crosshair_point(layout, "yz", _XYZ_INITIAL))
    events += _xyz_press("Shift", "Right")  # First owner's X becomes 8.
    events += _xyz_click(tabs[second.id]) + [{"type": "wait", "frames": 3}]
    second_ready_index = len(events) - 1
    events += _xyz_press("Up")  # New document reacquires XY, so only its Z changes.
    events += _xyz_click(_xyz_crosshair_point(second_layout, "xz", (11, 8, 5)))
    events += _xyz_press("Shift", "Right")
    events += _xyz_click(tabs[first.id]) + [{"type": "wait", "frames": 3}]
    first_ready_index = len(events) - 1
    events += _xyz_press("Up")
    rendered = editor.render_headless(frames=4, events=events)
    _xyz_assert_trace(rendered, events)
    assert not editor.error and editor.active_index == 0
    second_ready = _xyz_event_frames(rendered, second_ready_index)[-1]
    first_ready = _xyz_event_frames(rendered, first_ready_index)[-1]
    assert second_ready["document_info"]["id"] == second.id
    assert second_ready["crosshair_xyz"] == list(second_initial)
    assert _xyz_focused_plane(second_ready) == "xy"
    assert first_ready["document_info"]["id"] == first.id
    assert first_ready["crosshair_xyz"] == [8, 5, 3]
    assert _xyz_focused_plane(first_ready) == "xy"
    _xyz_assert_live(first, (8, 5, 4), before_first, snapshot_first)
    _xyz_assert_live(second, (12, 8, 5), before_second, snapshot_second)
    before_first["source_snapshot"] = _xyz_native_state(snapshot_first)
    _xyz_record(tmp_path, "document-switch", "actual-tabs-two-distinct-owners", first,
                before_first, rendered, events, extra={
                    "second_before": before_second, "second_after": second.info,
                    "second_source_snapshot": _xyz_native_state(second.native_snapshot()),
                    "api_preparation": [{"method": "add_native_document", "id": second.id},
                                        {"method": "set_view", "crosshair": list(second_initial)},
                                        {"method": "active_index", "value": 0}],
                })


def test_xyz_actual_state_invariants_retain_annotations_camera_and_redo(xyz_editor, tmp_path):
    editor, live = xyz_editor
    source = _xyz_source()
    edit = live.begin_edit()
    edit.replace_plane(source[3] + np.float32(1), z=3)
    live.commit(edit)
    editor.action("undo")  # Independently prepare a nonempty redo history.
    view = live.native_snapshot().display
    view.linked_zoom = False
    view.zoom = 2
    view.plane_zoom = [2, 3, 4]
    view.pan = [[2, 1], [-3, 2], [1, -2]]
    live.set_display(view)
    assert live.info["undo_steps"] > 0 and live.info["redo_steps"] > 0
    inputs = _xyz_press("Shift", "Right") + _xyz_press("Up")
    inputs += _xyz_press("Shift", "Tab") + _xyz_press("Tab") + _xyz_press("Space")
    before, snapshot, rendered, events = _xyz_run(xyz_editor, "xz", inputs)
    _xyz_assert_live(live, (8, 6, 3), before, snapshot)
    final = _xyz_native_state(live.native_snapshot())
    assert final == before["source_snapshot"]
    for row in rendered["input_frames"]:
        _xyz_assert_unchanged(before, row["document_info"])
    _xyz_record(tmp_path, "state-invariants", "annotations-camera-nonempty-redo", live,
                before, rendered, events, extra={"preserved_native_state": final})


def test_xyz_actual_session_and_pixel_undo_preserve_navigation(xyz_editor, tmp_path):
    from imagejxx.ui.app import Editor

    editor, live = xyz_editor
    source = _xyz_source()
    prepared = source.copy()
    prepared[3] += np.float32(1)
    initial = live.info
    edit = live.begin_edit()
    edit.replace_plane(prepared[3], z=3)
    live.commit(edit)
    after_edit = live.info
    assert after_edit["undo_steps"] == initial["undo_steps"] + 1
    np.testing.assert_array_equal(live.native_snapshot().dataset.numpy("ZYX"), prepared)
    before, snapshot, rendered, events = _xyz_run(xyz_editor, "yz", _xyz_press("Up"))
    after_navigation = live.info
    assert after_navigation["view"]["crosshair"] == [8, 5, 3]
    _xyz_assert_unchanged(before, after_navigation)
    np.testing.assert_array_equal(live.native_snapshot().dataset.numpy("ZYX"), prepared)
    assert live.native_snapshot().shares_data(snapshot)
    navigated = live.view
    editor.action("undo")  # Pixel history API, separately recorded from keys.
    after_undo = live.info
    assert after_undo["undo_steps"] == initial["undo_steps"]
    assert after_undo["redo_steps"] == initial["redo_steps"] + 1
    assert live.view == navigated
    _xyz_assert_dataset(live.native_snapshot().dataset, (8, 5, 3))
    native_path = tmp_path / "rotated-native.ijx"
    editor.action("save_workspace", path=str(native_path))
    extensions = {}
    documents, results, steps = load_session(native_path, extensions=extensions)
    assert len(documents) == 1
    np.testing.assert_array_equal(documents[0].data, source)
    assert documents[0].view_state["crosshair"] == [8, 5, 3]
    python_path = tmp_path / "rotated-python.ijx"
    save_session(python_path, documents, results=results, workflow=steps)
    reopened = Editor(preferences=Preferences(tmp_path / "reopened-xyz.json"))
    reopened.open_file(python_path)
    fresh = reopened.live_document()
    assert fresh.id != live.id
    assert fresh.view == navigated
    # Existing v2 interchange is a compatibility path. Typed v3 session
    # migration remains separate; the canonical numerical bytes still agree.
    _xyz_assert_dataset(fresh.native_snapshot().dataset, (8, 5, 3), require_float32=False)
    state = _xyz_native_state(fresh.native_snapshot())
    original_state = _xyz_native_state(live.native_snapshot())
    for key in ("calibration", "selection", "rois", "overlays", "t", "c"):
        assert state[key] == original_state[key]
    fresh_render = reopened.render_headless(frames=4)
    assert _xyz_focused_plane(fresh_render) == "xy"  # Old YZ focus was transient.
    assert fresh_render["orthogonal_navigation"]["keyboard_admitted"] is True
    _xyz_assert_trace(fresh_render, [])
    _xyz_record(tmp_path, "session-and-pixel-undo", "native-python-roundtrip", live,
                before, rendered, events, extra={
                    "initial": initial, "after_pixel_edit": after_edit,
                    "after_navigation": after_navigation, "after_pixel_undo": after_undo,
                    "independent_edited_sha256": hashlib.sha256(prepared.astype("<f4").tobytes()).hexdigest(),
                    "reopened": fresh.info, "reopened_source_snapshot": state,
                    "reopened_rendered": fresh_render,
                    "sessions": [{"path": str(path.resolve()), "sha256": hashlib.sha256(path.read_bytes()).hexdigest()}
                                 for path in (native_path, python_path)],
                    "api_inputs": [{"method": "begin_edit/replace_plane/commit", "z": 3, "added": 1},
                                   {"method": "undo"}, {"method": "save_workspace"},
                                   {"method": "load_session/save_session/open_file"}],
                })


_XYZ_VALID_KEYS = ("Escape", "Delete", "Left", "Right", "Up", "Down", "PageUp",
                   "PageDown", "Home", "End", "Space", "Shift", "Ctrl", "Alt",
                   "Super", "A", "W", "Enter", "Tab", "Backspace")
_XYZ_BAD_BATCHES = [
    {"type": "keys", "keys": []},
    {"type": "keys", "keys": {}},
    {"type": "keys", "keys": [{"name": "Up", "down": True}] * 2},
    {"type": "keys", "keys": [{"name": "Up", "down": True}, {"name": "Up", "down": False}]},
    {"type": "keys", "keys": [{"name": "NotAnImGuiKey", "down": True}]},
    {"type": "keys", "keys": [{"name": 4, "down": True}]},
    {"type": "keys", "keys": [{"name": "Up", "down": 1}]},
    {"type": "keys", "keys": [{"name": "Up", "down": "true"}]},
    {"type": "keys", "keys": [{"name": "Up"}]},
    {"type": "keys", "keys": [{"name": "Up", "down": True, "extra": 1}]},
    {"type": "keys", "keys": [None]},
    _xyz_keys(*_XYZ_VALID_KEYS[:17], down=False),
    {**_xyz_keys("Up"), "extra": 1},
    {**_xyz_keys("Up"), "frames": True},
    {**_xyz_keys("Up"), "frames": 0},
    {**_xyz_keys("Up"), "frames": 121},
    {"type": "key", "name": "NotAnImGuiKey", "down": True},
    {"type": "position", "point": [1, 2], "space": "image"},
]


@pytest.mark.parametrize("bad", _XYZ_BAD_BATCHES, ids=[f"invalid-{index}" for index in range(len(_XYZ_BAD_BATCHES))])
def test_xyz_input_preflight_is_atomic_after_a_valid_first_event(xyz_editor, bad):
    editor, live = xyz_editor
    before, snapshot = live.info, live.native_snapshot()
    with pytest.raises((ValueError, TypeError)):
        editor.render_headless(events=[_xyz_keys("Up"), bad])
    assert live.info == before
    assert live.native_snapshot().data_identity == snapshot.data_identity
    assert live.native_snapshot().shares_data(snapshot)
    _xyz_assert_dataset(live.native_snapshot().dataset, _XYZ_INITIAL)
    assert not editor.busy


@pytest.mark.parametrize("invalid", [
    [_xyz_keys("Up")] + [{"type": "wait"}] * 120,
    [_xyz_keys(*_XYZ_VALID_KEYS[:16], down=False)] * 8,  # 128 logical atoms, only 8 events.
    [_xyz_keys("Up")] + [{"type": "wait", "frames": 120}] * 2,  # 241 event frames.
])
def test_xyz_input_counts_are_bounded_before_dispatch(xyz_editor, invalid):
    editor, live = xyz_editor
    before = live.info
    with pytest.raises(ValueError):
        editor.render_headless(events=invalid)
    assert live.info == before and not editor.busy
    _xyz_assert_dataset(live.native_snapshot().dataset, _XYZ_INITIAL)


@pytest.mark.parametrize("events", [
    [_xyz_keys(*_XYZ_VALID_KEYS[:16], down=False)],
    [{"type": "key", "name": "Up", "down": False}] * 120,
    [{"type": "wait", "frames": 120}, {"type": "wait", "frames": 120}],
])
def test_xyz_input_exact_limits_are_accepted_without_movement(xyz_editor, events):
    editor, live = xyz_editor
    before, snapshot = live.info, live.native_snapshot()
    rendered = editor.render_headless(frames=3, events=events)
    _xyz_assert_trace(rendered, events)
    _xyz_assert_live(live, _XYZ_INITIAL, before, snapshot)
    assert len(rendered["input_frames"]) == 3 + sum(event.get("frames", 1) for event in events)
    assert not editor.error


@pytest.mark.parametrize("flags", [
    {"exercise": True, "camera": True},
    {"exercise": True, "exercise_preferences": True},
    {"camera": True, "exercise_orthogonal_busy": True},
    {"exercise_preferences": True, "exercise_orthogonal_busy": True},
])
def test_xyz_diagnostic_exercises_are_exclusive_and_atomic(xyz_editor, flags):
    editor, live = xyz_editor
    before = live.info
    with pytest.raises(ValueError):
        editor.render_headless(**flags)
    assert live.info == before and not editor.busy


def _xyz_assert_cpp_science(science, report_directory):
    xyz = tuple(science["crosshair_xyz"])
    expected = _xyz_expected(xyz)
    np.testing.assert_allclose(science["world_xyz"], expected["world_xyz"], rtol=0, atol=1e-12)
    assert science["sample"] == expected["landmark"]
    assert len(science["planes"]) == 3
    assert [row["plane"].lower() for row in science["planes"]] == list(_XYZ_PLANES)
    for row in science["planes"]:
        plane = row["plane"].lower()
        assert row["shape"] == list(expected["planes"][plane]["shape"])
        assert row["coordinate"] == xyz[_XYZ_FIXED_AXIS[plane]]
        artifact = Path(row["raw_path"]).resolve(strict=True)
        assert artifact.is_relative_to(report_directory)
        actual = artifact.read_bytes()
        assert actual == expected["planes"][plane]["bytes"]
        assert hashlib.sha256(actual).hexdigest() == expected["planes"][plane]["sha256"]
        assert row["landmark_value"] == expected["landmark"]


def _xyz_assert_cpp_report(path):
    report = json.loads(path.read_text(encoding="utf-8"))
    assert report["schema_version"] == 1 and report["context"] == "imgui_cpp"
    assert report["requirement_id"] == "G027.xyz_keyboard_navigation"
    assert report["source"]["shape_zyx"] == [9, 17, 32]
    assert report["source"]["expected_sha256"] == _XYZ_SOURCE_SHA
    source_path = Path(report["source"]["raw_path"]).resolve(strict=True)
    assert source_path.is_relative_to(path.parent.resolve())
    raw_source = source_path.read_bytes()
    assert raw_source == _xyz_source().astype("<f4").tobytes()
    assert hashlib.sha256(raw_source).hexdigest() == _XYZ_SOURCE_SHA
    assert report["world_atol"] == 1e-12 and report["world_rtol"] == 0
    assert report["repeat"] == {"delta_time": 1 / 60, "delay": 0.275, "rate": 0.05,
                                "hold_frames": 40, "expected_steps": 9}
    assert [case["id"] for case in report["cases"]] == list(_XYZ_CASES)
    cases = {case["id"]: case["scenarios"] for case in report["cases"]}
    for case, rows in cases.items():
        assert rows and len({row["id"] for row in rows}) == len(rows)
        for row in rows:
            assert row["input_events"], f"{case}/{row['id']} has no actual input"
            _xyz_assert_trace(row["rendered"], row["input_events"])
            if "science" in row:
                _xyz_assert_cpp_science(row["science"], path.parent.resolve())
                assert row["science"]["crosshair_xyz"] == row["after"]["view"]["crosshair"]
            if "reopened_science" in row:
                _xyz_assert_cpp_science(row["reopened_science"], path.parent.resolve())
            if case not in ("document-switch", "session-and-pixel-undo"):
                _xyz_assert_unchanged(row["before"], row["after"])
            for frame in row["rendered"]["input_frames"]:
                if frame["phase"] == "input" and frame["busy"]:
                    assert frame["job_state"] == "running"
                    assert frame["orthogonal_navigation"]["keyboard_admitted"] is False
    for case, shift in (("plain-direction-all-planes", False), ("shift-reference-direction-all-planes", True)):
        prefix = "shift" if shift else "plain"
        rows = {row["id"]: row for row in cases[case]}
        keys = ("Left", "Right", "Up", "Down") if shift else ("Up", "PageUp", "Down", "PageDown")
        assert set(rows) == {f"{prefix}-{index}-{key}" for index in range(3) for key in keys}
        for index, plane in enumerate(_XYZ_PLANES):
            for key in keys:
                row = rows[f"{prefix}-{index}-{key}"]
                expected = list(_xyz_step(_XYZ_INITIAL, plane, key, shift))
                assert row["before"]["view"]["crosshair"] == list(_XYZ_INITIAL)
                assert row["after"]["view"]["crosshair"] == expected
                assert _xyz_focused_plane(row["rendered"]) == plane
                assert row["expected"]["crosshair_xyz"] == expected
    bounds = cases["edge-clamp-plain-and-shift"]
    assert len(bounds) == 18
    for row in bounds:
        xyz = row["before"]["view"]["crosshair"]
        assert row["after"]["view"]["crosshair"] == xyz
        assert all(frame["crosshair_xyz"] == xyz for frame in row["rendered"]["input_frames"])
    for row in cases["repeat-bounded"]:
        xyz = [16, 14, 3] if "shift-diagonal" in row["id"] else [16, 5, 3]
        assert row["after"]["view"]["crosshair"] == xyz
        positions = [list(_XYZ_INITIAL)] + [frame["crosshair_xyz"] for frame in row["rendered"]["input_frames"]]
        deltas = np.diff(np.asarray(positions), axis=0)
        assert set(deltas[:, 0]) <= {0, 1} and deltas[:, 0].sum() == 9
        assert np.count_nonzero(deltas[:, 0]) == 9 and np.all(deltas[:, 2] == 0)
        if "shift-diagonal" in row["id"]:
            assert set(deltas[:, 1]) <= {0, 1} and deltas[:, 1].sum() == 9
        else:
            assert np.all(deltas[:, 1] == 0)
    activation = cases["activation-keyboard"]
    assert any(frame["orthogonal_navigation"]["toggle_focused"]
               for row in activation for frame in row["rendered"]["input_frames"])
    assert any(event["type"] == "key" and event["name"] == "Space" and event["down"] is True
               or event["type"] == "keys" and {"name": "Space", "down": True} in event["keys"]
               for row in activation for event in row["input_events"])
    return report


@pytest.mark.skipif(
    not (os.environ.get("IMAGEJXX_ORTHOGONAL_CPP_REPORT") or os.environ.get("IMAGEJXX_CPP_EXE")),
    reason="Set IMAGEJXX_ORTHOGONAL_CPP_REPORT or IMAGEJXX_CPP_EXE to the installed standalone producer",
)
def test_xyz_installed_two_contexts_have_independent_input_and_science(xyz_editor, tmp_path):
    from imagejxx import _desktop, _native

    configured = os.environ.get("IMAGEJXX_ORTHOGONAL_CPP_REPORT")
    executable = os.environ.get("IMAGEJXX_CPP_EXE")
    if configured:
        cpp_path = Path(configured).resolve(strict=True)
    else:
        output = tmp_path / "installed-cpp-xyz"
        result = subprocess.run(
            [str(Path(executable).resolve(strict=True)), "--orthogonal-keyboard-test", "--keyboard-dir", str(output)],
            capture_output=True, text=True, encoding="utf-8", errors="replace", timeout=180,
        )
        (tmp_path / "installed-cpp-stdout.txt").write_text(result.stdout, encoding="utf-8")
        (tmp_path / "installed-cpp-stderr.txt").write_text(result.stderr, encoding="utf-8")
        assert result.returncode == 0, result.stdout + result.stderr
        cpp_path = output / "imgui-cpp.json"
    cpp = _xyz_assert_cpp_report(cpp_path)
    assert len(cpp["cases"]) == 20
    before, snapshot, rendered, events = _xyz_run(
        xyz_editor, "yz", _xyz_press("Shift", "Right") + _xyz_press("PageUp") + _xyz_press("Escape"))
    live = xyz_editor[1]
    _xyz_assert_live(live, (9, 5, 3), before, snapshot)
    assert live.view["orthogonal"] is False
    assert rendered["orthogonal_navigation"]["ordinary_canvas_focused"] is True
    identities = {}
    paths = {"installed_python_core": Path(_native.__file__), "installed_python_ui": Path(_desktop.__file__),
             "cpp_report": cpp_path}
    if executable:
        paths["installed_cpp_executable"] = Path(executable)
    for role, path in paths.items():
        identities[role] = {"path": str(path.resolve(strict=True)), "sha256": hashlib.sha256(path.read_bytes()).hexdigest()}
    assert identities["installed_python_core"]["path"] != identities["installed_python_ui"]["path"]
    _xyz_record(tmp_path, "installed-two-contexts", "independent-installed-producers", live,
                before, rendered, events, extra={"identities": identities, "cpp_cases": list(_XYZ_CASES),
                    "python_complete_matrix": "Other P05b tests supply all 20 Python UI case groups; root binds their separate raw reports."})
