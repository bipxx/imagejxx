"""Pinned ImageJ processor arithmetic; scientific axis remaps remain separate."""
import json
from pathlib import Path

import numpy as np
import pytest
import imagejxx as ij

FIXTURE = Path(__file__).parents[1] / "fixtures/color-components-imagej154r.json"
CASES = json.loads(FIXTURE.read_text())["cases"]


@pytest.mark.parametrize("case", CASES, ids=lambda c: f"{c['kind']}-{c['space']}-{c['pattern']}-{c.get('inverse', False)}")
def test_reference(case):
    space = case["space"]
    dtype = np.float32 if space == "hsb32" else np.uint8
    h, w = case["shape"]
    if case["kind"] == "split":
        source = np.array(case["input"], dtype=np.uint8).reshape(h, w, 3)
        expected = np.array(case["output"], dtype=dtype).reshape(3, h, w)
        got = ij.split_color_components(source, space)
    else:
        source = np.array(case["input"], dtype=dtype).reshape(-1, h, w)
        expected = np.array(case["output"], dtype=np.uint8).reshape(h, w, 3)
        got = ij.merge_color_components(source, space, invert_rgb=case["inverse"])
    assert got.dtype == expected.dtype
    assert got.flags.owndata and not np.shares_memory(source, got)
    np.testing.assert_array_equal(got, expected)
    if got.dtype == np.float32:
        np.testing.assert_array_equal(got.view(np.uint32), expected.view(np.uint32))


@pytest.mark.parametrize("space", ["rgb", "hsb8", "hsb32"])
def test_strided_input(space):
    rgb = np.arange(6 * 8 * 3, dtype=np.uint8).reshape(6, 8, 3)[::-2, ::-2]
    parts = ij.split_color_components(rgb, space)
    np.testing.assert_array_equal(parts, ij.split_color_components(rgb.copy(), space))
    view = parts[:, ::-1, ::-1]
    np.testing.assert_array_equal(ij.merge_color_components(view, space), ij.merge_color_components(view.copy(), space))


@pytest.mark.parametrize("source", [np.zeros((2, 3), np.uint8), np.zeros((2, 3, 4), np.uint8), np.zeros((2, 3, 3), np.float32), np.zeros((0, 3, 3), np.uint8)])
def test_split_refusals(source):
    with pytest.raises(ValueError):
        ij.split_color_components(source)


@pytest.mark.parametrize("source,space", [(np.zeros((1, 2, 3), np.uint8), "rgb"), (np.zeros((4, 2, 3), np.uint8), "rgb"), (np.zeros((2, 2, 3), np.uint8), "hsb8"), (np.zeros((3, 2, 3), np.float64), "hsb32"), (np.zeros((3, 0, 3), np.uint8), "rgb")])
def test_merge_refusals(source, space):
    with pytest.raises(ValueError):
        ij.merge_color_components(source, space)


@pytest.mark.parametrize("value", [np.nan, np.inf, -np.inf])
def test_nonfinite_hsb(value):
    source = np.zeros((3, 1, 2), np.float32)
    source[1, 0, 0] = value
    with pytest.raises(ValueError):
        ij.merge_color_components(source, "hsb32")


@pytest.mark.parametrize("cap", [True, np.bool_(False), 1.5, "100000"])
def test_cap_type(cap):
    with pytest.raises(TypeError):
        ij.split_color_components(np.zeros((1, 1, 3), np.uint8), max_working_bytes=cap)


@pytest.mark.parametrize("merge", [False, True])
def test_cancel_budget_and_snapshot(merge):
    source = np.arange(18, dtype=np.uint8).reshape((3, 2, 3) if merge else (2, 3, 3))
    work = ij.merge_color_components if merge else ij.split_color_components
    expected = work(source.copy())
    visits = []
    def checkpoint():
        visits.append(1)
        source.fill(255)
    got = work(source, checkpoint=checkpoint)
    assert visits
    np.testing.assert_array_equal(got, expected)
    visits.clear()
    with pytest.raises(ValueError, match="byte limit"):
        work(source, max_working_bytes=65536, checkpoint=checkpoint)
    assert not visits
    token = ij.CancellationToken()
    token.cancel()
    with pytest.raises(ij.OperationCancelled):
        work(source, cancellation=token, checkpoint=checkpoint)
    assert not visits


def test_controls_and_inversion():
    source = np.zeros((2, 1, 3), np.uint8)
    inverted = ij.merge_color_components(source, invert_rgb=True)
    np.testing.assert_array_equal(inverted, 255)
    with pytest.raises(TypeError):
        ij.merge_color_components(source, invert_rgb=1)
    with pytest.raises(ValueError):
        ij.merge_color_components(np.zeros((3, 1, 3), np.uint8), "hsb8", invert_rgb=True)
    for function, image in [(ij.split_color_components, np.zeros((1, 1, 3), np.uint8)), (ij.merge_color_components, source)]:
        with pytest.raises(ValueError):
            function(image, "unknown")
        def fail():
            raise LookupError("owned callback error")
        with pytest.raises(LookupError, match="owned callback"):
            function(image, checkpoint=fail)
        progress = []
        function(image, progress=lambda done, total: progress.append((done, total)), max_working_bytes=np.int64(100000))
        assert progress == [(1, 1)]
