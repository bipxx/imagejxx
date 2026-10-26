"""Independent inclusive-Z-range oracles; no ImageJ numeric parity claim."""
import numpy as np
import pytest
import imagejxx as ij


@pytest.mark.parametrize("method, expected", [
    ("minimum", 2.), ("maximum", 6.), ("mean", 4.),
    ("sum", 8.), ("median", 4.), ("stddev", 2.),
])
@pytest.mark.parametrize("dtype", [np.uint8, np.uint16, np.float32, np.float64])
def test_inclusive_projection_excludes_both_outer_slices(method, expected, dtype):
    source = np.array([199, 2, 6, 188], dtype=dtype)[:, None, None]
    original = source.copy()
    out = ij.project(source, method, first=1, last=2)
    np.testing.assert_array_equal(out, [[expected]])
    np.testing.assert_array_equal(source, original)
    assert out.dtype == (np.dtype(dtype) if method in ("minimum", "maximum") else np.float64)


@pytest.mark.parametrize("method, expected", [
    ("minimum", 2.), ("maximum", 6.), ("mean", 4.),
    ("sum", 12.), ("median", 4.), ("stddev", np.sqrt(8 / 3)),
])
def test_projection_default_last_and_singleton(method, expected):
    source = np.array([199., 2., 4., 6.])[:, None, None]
    np.testing.assert_allclose(ij.project(source, method, first=np.int64(1)), [[expected]])
    np.testing.assert_array_equal(ij.project(source, method, first=2, last=2),
                                  [[0. if method == "stddev" else 4.]])
    np.testing.assert_array_equal(ij.project(source, method),
                                  ij.project(source, method, first=0, last=3))


@pytest.mark.parametrize("first,last", [(-1, 2), (2, 1), (0, 4), (4, None)])
def test_projection_invalid_bounds_are_atomic(first, last):
    source = np.arange(24, dtype=np.uint16).reshape(4, 2, 3)
    original = source.copy()
    with pytest.raises(ValueError, match="Projection"):
        ij.project(source, first=first, last=last)
    np.testing.assert_array_equal(source, original)


@pytest.mark.parametrize("value", [True, False, np.bool_(True), np.bool_(False), 1.0, "1"])
@pytest.mark.parametrize("parameter", ["first", "last"])
def test_projection_indices_refuse_coercion(value, parameter):
    with pytest.raises(TypeError, match="integers"):
        ij.project(np.zeros((3, 2, 2), np.uint8), **{parameter: value})


def test_projection_selected_rgba_samples_are_independent():
    source = np.zeros((4, 2, 3, 4), np.uint16)
    source[0] = 999
    source[1] = [2, 10, 20, 100]
    source[2] = [6, 30, 40, 200]
    source[3] = 888
    expected = np.broadcast_to([4., 20., 30., 150.], (2, 3, 4))
    np.testing.assert_array_equal(ij.project(source, "mean", first=1, last=2), expected)


def test_installed_editor_projection_range_and_rejection(tmp_path):
    pytest.importorskip("imagejxx._desktop", exc_type=ModuleNotFoundError)
    from imagejxx.ui.app import Editor
    from imagejxx.ui.model import Document
    from imagejxx.ui.persistence import Preferences
    editor = Editor(preferences=Preferences(tmp_path / "prefs.json"))
    source = np.array([999, 2, 6, 888], np.uint16)[:, None, None]
    editor.add_document(Document(source, is_stack=True))
    before = editor.live_document().native_snapshot()
    for parameters in ({"first": 0}, {"first": 3, "last": 2}, {"last": 5},
                       {"first": True}, {"last": 2.5}, {"unknown": 1}):
        with pytest.raises((ValueError, RuntimeError)):
            editor.action("project", **parameters)
        assert len(editor.documents) == 1
        assert editor.live_document().native_snapshot().shares_data(before)
    editor.action("project", method="mean", first=2, last=3)
    editor.wait()
    assert len(editor.documents) == 2
    np.testing.assert_array_equal(editor.document.data, [[4.]])
    editor.active_index = 0
    np.testing.assert_array_equal(editor.document.data, source)
    after = editor.live_document().native_snapshot()
    assert after.shares_data(before)
