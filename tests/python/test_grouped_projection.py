"""Independent group-boundary, dtype, metadata and source-preservation oracles."""
import numpy as np
import pytest
import imagejxx as ij


@pytest.mark.parametrize("dtype", [np.uint8, np.uint16, np.float32, np.float64])
@pytest.mark.parametrize("method,expected", [
    ("maximum", [6., 14.]), ("minimum", [2., 10.]), ("mean", [4., 12.]),
    ("sum", [8., 24.]), ("median", [4., 12.]), ("stddev", [2., 2.]),
])
def test_complete_groups_exclude_outer_slices(dtype, method, expected):
    source = np.array([199, 2, 6, 10, 14, 188], dtype=dtype)[:, None, None]
    before = source.copy()
    out = ij.project_groups(source, 2, method, first=1, last=4)
    np.testing.assert_array_equal(out, np.array(expected)[:, None, None])
    assert out.dtype == (np.dtype(dtype) if method in ("maximum", "minimum") else np.float64)
    assert out.flags.c_contiguous
    np.testing.assert_array_equal(source, before)


@pytest.mark.parametrize("dtype", [np.uint8, np.uint16, np.float32, np.float64])
@pytest.mark.parametrize("size,expected", [(1, [2., 6., 10., 14.]), (2, [4., 12.]), (4, [8.])])
def test_one_unit_and_multiple_groups_keep_z_axis(dtype, size, expected):
    source = np.array([2, 6, 10, 14], dtype=dtype)[:, None, None]
    out = ij.project_groups(source, np.int64(size))
    assert out.shape == (len(expected), 1, 1)
    np.testing.assert_array_equal(out[:, 0, 0], expected)
    np.testing.assert_array_equal(out, ij.project_groups(source, size, first=0, last=3))


@pytest.mark.parametrize("parameter", ["first", "last", "group_size"])
@pytest.mark.parametrize("value", [True, False, np.bool_(True), np.bool_(False), 2.0, "2"])
def test_group_indices_refuse_coercion(parameter, value):
    arguments = dict(group_size=2, first=0, last=3)
    arguments[parameter] = value
    with pytest.raises(TypeError, match="integers"):
        ij.project_groups(np.zeros((4, 2, 3), np.uint8), **arguments)


@pytest.mark.parametrize("size", [0, -1, 3, 5])
def test_group_size_refuses_incomplete_or_invalid_groups(size):
    source = np.array([2, 6, 10, 14], np.uint16)[:, None, None]
    before = source.copy()
    with pytest.raises(ValueError, match="Projection"):
        ij.project_groups(source, size)
    np.testing.assert_array_equal(source, before)


@pytest.mark.parametrize("first,last", [(-1, 3), (3, 1), (0, 4), (4, None)])
def test_grouped_invalid_range_is_atomic(first, last):
    source = np.arange(24, dtype=np.uint16).reshape(4, 2, 3)
    before = source.copy()
    with pytest.raises(ValueError, match="Projection"):
        ij.project_groups(source, 2, first=first, last=last)
    np.testing.assert_array_equal(source, before)


def test_grouped_rgba_channels_stay_independent():
    source = np.zeros((4, 2, 3, 4), np.uint16)
    source[0] = [2, 10, 20, 100]
    source[1] = [6, 30, 40, 200]
    source[2] = [10, 50, 60, 300]
    source[3] = [14, 70, 80, 400]
    expected = np.broadcast_to(np.array([[4., 20., 30., 150.], [12., 60., 70., 350.]])[:, None, None, :], (2, 2, 3, 4))
    np.testing.assert_array_equal(ij.project_groups(source, 2), expected)


def editor_at(path):
    pytest.importorskip("imagejxx._desktop", exc_type=ModuleNotFoundError)
    from imagejxx.ui.app import Editor
    from imagejxx.ui.persistence import Preferences
    return Editor(preferences=Preferences(path))


@pytest.mark.parametrize("first,last,expected", [(2, 5, [4., 12.]), (1, 6, [500.5, 8., 451.])])
def test_installed_editor_groups_calibration_and_session(tmp_path, first, last, expected):
    from imagejxx.ui.model import Document, Calibration
    from imagejxx.ui.persistence import save_session
    editor = editor_at(tmp_path / "prefs.json")
    source = np.broadcast_to(np.array([999, 2, 6, 10, 14, 888], np.uint16)[:, None, None], (6, 2, 3)).copy()
    document = Document(source, is_stack=True)
    document.set_calibration(Calibration(.5, 2., 3., "um", .125, "s", origin=(10., 20., 30.),
        direction=(0.,0.,1., 0.,1.,0., -1.,0.,0.), time_origin=4.))
    editor.add_document(document)
    original = editor.live_document(); before = original.native_snapshot(); info = original.info
    for parameters in ({"group_size": 0}, {"group_size": True}, {"group_size": 2.0},
                       {"group_size": 3, "first": 2, "last": 5}, {"group_size": 4},
                       {"group_size": 2, "first": 1.0}, {"group_size": 2, "last": 7},
                       {"group_size": 2, "unknown": 1}):
        with pytest.raises((ValueError, RuntimeError)):
            editor.action("project_groups", **parameters)
        assert len(editor.document_infos) == 1 and not editor.busy
        assert original.native_snapshot().shares_data(before)
    editor.action("project_groups", group_size=2, first=first, last=last)
    editor.wait()
    out = editor.document
    expected_pixels = np.broadcast_to(np.array(expected)[:, None, None], (len(expected), 2, 3))
    np.testing.assert_array_equal(out.data, expected_pixels)
    assert out.is_stack and out.calibration.pixel_depth == 6.
    np.testing.assert_array_equal(out.calibration.origin, (10. + (first - 1) * 3., 20., 30.))
    assert out.calibration.pixel_width == .5 and out.calibration.pixel_height == 2.
    assert out.calibration.frame_interval == .125 and out.calibration.time_origin == 4.
    assert out.calibration.unit == "um" and out.calibration.time_unit == "s"
    path = tmp_path / "grouped.ijx"
    save_session(path, [out])
    reopened = editor_at(tmp_path / "reopened.json"); reopened.open_file(path)
    np.testing.assert_array_equal(reopened.document.data, expected_pixels)
    assert reopened.document.calibration == out.calibration
    editor.active_index = 0
    np.testing.assert_array_equal(editor.document.data, source)
    assert original.native_snapshot().shares_data(before)
    for key in ("revision", "dirty", "undo_steps", "redo_steps"):
        assert original.info[key] == info[key]
    editor.action("project_groups", group_size=6, method="sum"); editor.wait()
    assert not editor.document.is_stack
    np.testing.assert_array_equal(editor.document.data, np.full((2, 3), 1919.))


@pytest.mark.parametrize("action", ["project_groups", "show_grouped_z_projection"])
def test_grouped_does_not_flatten_scientific_tc_axes(tmp_path, action):
    editor = editor_at(tmp_path / "prefs.json")
    owner = ij.Document(ij.Dataset(np.zeros((2, 2, 4, 2, 3), np.uint16), axes="TCZYX"))
    editor.add_native_document(owner); before = owner.snapshot()
    with pytest.raises((ValueError, RuntimeError), match="scientific"):
        editor.action(action, **({"group_size": 2} if action == "project_groups" else {}))
    assert owner.snapshot().shares_data(before) and len(editor.document_infos) == 1


def test_grouped_refuses_overflowed_calibration_before_job(tmp_path):
    from imagejxx.ui.model import Document, Calibration
    editor = editor_at(tmp_path / "prefs.json")
    document = Document(np.zeros((4, 2, 3), np.uint16), is_stack=True)
    document.set_calibration(Calibration(pixel_depth=1e308)); editor.add_document(document)
    before = editor.live_document().native_snapshot()
    with pytest.raises((ValueError, RuntimeError)):
        editor.action("project_groups", group_size=2)
    assert len(editor.document_infos) == 1 and not editor.busy
    assert editor.live_document().native_snapshot().shares_data(before)


def test_grouped_output_is_reserved_before_worker_allocation(tmp_path):
    from imagejxx.ui.model import Document
    editor = editor_at(tmp_path / "prefs.json")
    editor.update_settings(processing_memory_mb=3, persist=False)
    source = np.zeros((64, 64, 64), np.uint8)
    editor.add_document(Document(source, is_stack=True))
    original = editor.live_document(); before = original.native_snapshot()
    # Legacy base is80 double planes=2.5MiB, but64 new output planes add2MiB.
    with pytest.raises((ValueError, RuntimeError), match="(?i)memory|budget|resource"):
        editor.action("project_groups", group_size=1)
    assert not editor.busy and len(editor.document_infos) == 1
    assert original.native_snapshot().shares_data(before)


@pytest.mark.parametrize("action", ["project_groups", "show_grouped_z_projection"])
def test_grouped_editor_requires_a_stack(tmp_path, action):
    editor = editor_at(tmp_path / "prefs.json")
    editor.add_document(np.zeros((2, 3), np.uint16))
    before = editor.live_document().native_snapshot()
    with pytest.raises((ValueError, RuntimeError), match="requires a stack"):
        editor.action(action, **({"group_size": 1} if action == "project_groups" else {}))
    assert len(editor.document_infos) == 1 and not editor.busy
    assert editor.live_document().native_snapshot().shares_data(before)
