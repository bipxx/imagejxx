"""Immutable native curve authoring, with independent Bernstein evaluations."""
import math
import struct

import numpy as np
import pytest
import imagejxx as ij


def curve(op):
    points = {1: [(1, 2), (11, 3)], 2: [(1, 2), (7, 8), (11, 3)],
              3: [(1, 2), (3, -4), (8, 15), (11, 3)]}[op]
    commands = [0, *points[0], op, *[v for p in points[1:] for v in p]]
    return ij.ImageJCompositePath(commands), np.array(points, dtype=float)


def bernstein(points, t):
    degree = len(points) - 1
    weights = [math.comb(degree, k) * (1-t)**(degree-k) * t**k for k in range(degree+1)]
    return np.array(weights) @ points


@pytest.mark.parametrize("op", [1, 2, 3])
@pytest.mark.parametrize("t", [.001, .125, .5, .875, .999])
def test_split_preserves_parametric_curve(op, t):
    source, controls = curve(op)
    prefix = [0, -8, -9, 1, -5, -6, 4]
    suffix = [0, 30, 40, 2, 35, 45, 50, 60, 4]
    path = ij.ImageJCompositePath(prefix + source.commands + suffix)
    result = ij.split_composite_path(path, 4, t)
    n = 2 * op
    assert result.segment_count == path.segment_count + 1
    assert result.commands[:len(prefix)] == prefix
    assert result.commands[-len(suffix):] == suffix
    divided = result.commands[len(prefix):-len(suffix)]
    assert divided[3] == divided[4+n] == op
    left = np.array(divided[1:3] + divided[4:4+n]).reshape(-1, 2)
    right = np.array(divided[2+n:4+n] + divided[5+n:5+2*n]).reshape(-1, 2)
    for u in np.linspace(0, 1, 101):
        actual = bernstein(left, u/t) if u <= t else bernstein(right, (u-t)/(1-t))
        assert actual == pytest.approx(bernstein(controls, u), abs=1e-12)
    assert source.commands == [0, *controls[0], op, *controls[1:].ravel()]


def test_edits_are_command_indexed_and_immutable():
    p, _ = curve(3)
    before = p.commands
    replaced = ij.edit_composite_path(p, ij.PathSegmentEdit.Replace, 1, [2, 7, 8, 11, 3])
    assert replaced.commands == [0, 1, 2, 2, 7, 8, 11, 3]
    appended = ij.edit_composite_path(replaced, ij.PathSegmentEdit.InsertBefore, 2, [4])
    assert appended.commands == replaced.commands + [4]
    inserted = ij.edit_composite_path(appended, ij.PathSegmentEdit.InsertBefore, 1, [0, -9, -8])
    assert inserted.commands == [0, 1, 2, 0, -9, -8, 2, 7, 8, 11, 3, 4]
    assert ij.edit_composite_path(inserted, ij.PathSegmentEdit.Remove, 1).commands == appended.commands
    assert p.commands == before


def test_close_split_retains_subpath_start_and_later_commands():
    p = ij.ImageJCompositePath([0, 1, 2, 1, 9, 6, 4, 1, 5, 5, 0, 20, 20, 4])
    split = ij.split_composite_path(p, 2, .25)
    assert split.commands == [0, 1, 2, 1, 9, 6, 1, 7, 5, 4, 1, 5, 5, 0, 20, 20, 4]


@pytest.mark.parametrize("op,index,segment", [
    (ij.PathSegmentEdit.Remove, 0, []),
    (ij.PathSegmentEdit.Remove, 1, [4]),
    (ij.PathSegmentEdit.Replace, 2, [4]),
    (ij.PathSegmentEdit.InsertBefore, 3, [4]),
    (ij.PathSegmentEdit.Replace, 1, [3, 1, 2]),
    (ij.PathSegmentEdit.Replace, 1, [1, 1, 2, 4]),
    (ij.PathSegmentEdit.Replace, 1, [1, math.inf, 2]),
    (ij.PathSegmentEdit.Replace, 1, [1, 1e9+1, 2]),
    (ij.PathSegmentEdit.Replace, 1, [1.5, 1, 2]),
])
def test_invalid_edits_do_not_call_progress(op, index, segment):
    p, _ = curve(3)
    calls = []
    with pytest.raises(ValueError):
        ij.edit_composite_path(p, op, index, segment, progress=lambda *args: calls.append(args))
    assert calls == []


@pytest.mark.parametrize("index,t", [(0, .5), (2, .5), (1, 0), (1, 1), (1, math.nan), (1, math.inf)])
def test_invalid_splits(index, t):
    with pytest.raises(ValueError):
        ij.split_composite_path(curve(3)[0], index, t)


@pytest.mark.parametrize("field,value", [("max_segments", 2), ("max_command_scalars", 10),
    ("max_working_bytes", 4096), ("max_segments", 0), ("max_command_scalars", 0)])
def test_admission_precedes_callbacks(field, value):
    limits = ij.PathEditLimits()
    setattr(limits, field, value)
    calls = []
    with pytest.raises(ValueError):
        ij.split_composite_path(curve(3)[0], 1, limits=limits, checkpoint=lambda: calls.append("checkpoint"))
    assert calls == []


def test_exact_working_admission_and_unmodified_signed_zero():
    p = ij.ImageJCompositePath([0, -0., 2, 1, 4, 5])
    limits = ij.PathEditLimits()
    output_scalars = len(p.commands) + 3
    limits.max_working_bytes = 4096 + 8 * (len(p.commands) + 2 * output_scalars)
    r = ij.split_composite_path(p, 1, limits=limits)
    assert struct.pack("d", r.commands[1]) == struct.pack("d", -0.)
    limits.max_working_bytes -= 1
    with pytest.raises(ValueError):
        ij.split_composite_path(p, 1, limits=limits)


def test_callback_mutation_and_finish_cancellation():
    source, _ = curve(3)
    expected = ij.split_composite_path(source, 1).commands
    limits = ij.PathEditLimits()
    calls = []
    def progress(done, total):
        nonlocal source
        calls.append((done, total))
        source = ij.ImageJCompositePath([0, 99, 99])
        limits.max_segments = 0
    assert ij.split_composite_path(source, 1, limits=limits, progress=progress).commands == expected
    assert calls == [(0, 1), (1, 1)]
    token = ij.CancellationToken()
    with pytest.raises(ij.OperationCancelled):
        ij.split_composite_path(curve(3)[0], 1, cancellation=token,
                                progress=lambda done, total: token.cancel() if done == 1 else None)
    with pytest.raises(ij.OperationCancelled):
        ij.split_composite_path(curve(3)[0], 1, cancellation=token)


@pytest.mark.parametrize("segment", [[True, 1, 2], [1, True, 2], "1,2,3", [1, 2+3j, 4]])
def test_python_numeric_command_types_are_strict(segment):
    with pytest.raises((TypeError, ValueError)):
        ij.edit_composite_path(curve(3)[0], ij.PathSegmentEdit.Replace, 1, segment)
