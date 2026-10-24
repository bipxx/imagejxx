"""Affine native path resizing with independently calculated operand values."""
import math
import struct

import pytest
import imagejxx as ij

COMMANDS = [0, 0, -0., 1, 10, 0, 2, 15, 5, 10, 10,
            3, 8, 12, 2, 12, 0, 10, 4, 0, 2, 2, 1, 3, 2, 1, 3, 3, 4]
BOUNDS = (0., 0., 12.5, 11.5)
DIRS = [(-1, -1), (0, -1), (1, -1), (1, 0),
        (1, 1), (0, 1), (-1, 1), (-1, 0)]


def geometry(commands=COMMANDS):
    return ij.Selection.from_path(ij.ImageJCompositePath(commands)).with_attributes(
        {"opaque": ["retained", 2**64-1]})


def transformed(commands, anchors, scales):
    out = commands.copy()
    pos = 0
    while pos < len(out):
        count = {0: 2, 1: 2, 2: 4, 3: 6, 4: 0}[out[pos]]
        pos += 1
        for k in range(count):
            axis = k % 2
            out[pos+k] = anchors[axis] + scales[axis] * (out[pos+k] - anchors[axis])
        pos += count
    return out


@pytest.mark.parametrize("handle", range(8))
@pytest.mark.parametrize("from_center,preserve_aspect", [(False, False), (False, True), (True, False), (True, True)])
@pytest.mark.parametrize("signs", [(1, 1), (-1, 1), (1, -1), (-1, -1)])
def test_all_handles_anchors_aspect_and_crossings(handle, from_center, preserve_aspect, signs):
    source = geometry()
    editor = ij.SelectionEditor(source, (32, 40))
    assert editor.bounds == list(BOUNDS)
    directions = DIRS[handle]
    centers = [6.25, 5.75]
    anchors = [centers[a] if from_center or not directions[a]
               else BOUNDS[a + (2 if directions[a] < 0 else 0)] for a in range(2)]
    start = [centers[a] if not directions[a]
             else BOUNDS[a + (2 if directions[a] > 0 else 0)] for a in range(2)]
    scales = [signs[a]*(a+2) if directions[a] else 1 for a in range(2)]
    pointer = [anchors[a] + scales[a]*(start[a]-anchors[a]) for a in range(2)]
    if preserve_aspect:
        magnitude = max(abs(scales[a]) for a in range(2) if directions[a])
        scales = [math.copysign(magnitude, scales[a]) if directions[a] else magnitude for a in range(2)]
    result = editor.resized(handle, pointer, from_center=from_center,
                            preserve_aspect=preserve_aspect, limits=ij.SelectionEditLimits(), mode=ij.SelectionHandleMode.BOUNDS)
    assert result.path.commands == pytest.approx(transformed(COMMANDS, anchors, scales), abs=1e-12)
    assert result.attributes == source.attributes and result.width == source.width
    assert source.path.commands == COMMANDS


def test_bounds_mode_does_not_replace_vertex_hit_contract():
    editor = ij.SelectionEditor(geometry(), (32, 40))
    assert all(h.kind == ij.SelectionHandleKind.VERTEX for h in editor.handles())
    handles = editor.handles(ij.SelectionHandleMode.BOUNDS)
    assert [h.index for h in handles] == list(range(8))
    assert handles[4].position == [12.5, 11.5]
    hit = editor.hit_test([0, 0], [.1, .1], mode=ij.SelectionHandleMode.BOUNDS)
    assert hit.kind == ij.SelectionHandleKind.RESIZE and hit.index == 0
    assert editor.hit_test([5, 5], [.1, .1], mode=ij.SelectionHandleMode.BOUNDS) is None
    assert editor.hit_test([0, 0], [.1, .1]).kind == ij.SelectionHandleKind.VERTEX


@pytest.mark.parametrize("commands,indices", [([0, 1, 2, 1, 5, 2], [3, 7]),
                                              ([0, 1, 2, 1, 1, 6], [1, 5]),
                                              ([0, 1, 2], [])])
def test_degenerate_source_axis_rules(commands, indices):
    editor = ij.SelectionEditor(geometry(commands), (12, 16))
    assert [h.index for h in editor.handles(ij.SelectionHandleMode.BOUNDS)] == indices
    with pytest.raises(ValueError):
        editor.resized(4, [8, 8], limits=ij.SelectionEditLimits(), mode=ij.SelectionHandleMode.BOUNDS)
    if indices:
        with pytest.raises(ValueError):
            editor.resized(indices[0], [8, 8], preserve_aspect=True, limits=ij.SelectionEditLimits(), mode=ij.SelectionHandleMode.BOUNDS)


def test_signed_zero_identity_and_uncontrolled_axis_are_exact():
    editor = ij.SelectionEditor(geometry(), (32, 40))
    identity = editor.resized(4, [12.5, 11.5], mode=ij.SelectionHandleMode.BOUNDS)
    side = editor.resized(3, [25, 5.75], mode=ij.SelectionHandleMode.BOUNDS)
    assert struct.pack("d", identity.path.commands[2]) == struct.pack("d", -0.)
    assert struct.pack("d", side.path.commands[2]) == struct.pack("d", -0.)


@pytest.mark.parametrize("point", [[0, 20], [20, 0], [math.inf, 20], [1e30, 20]])
def test_invalid_resize_precedes_callbacks(point):
    calls = []
    with pytest.raises(ValueError):
        ij.SelectionEditor(geometry(), (32, 40)).resized(4, point,
            preserve_aspect=True, limits=ij.SelectionEditLimits(),
            progress=lambda *v: calls.append(v), checkpoint=lambda: calls.append("checkpoint"), mode=ij.SelectionHandleMode.BOUNDS)
    assert calls == []


def test_resource_finish_cancel_exception_and_callback_reassignment():
    original = geometry()
    editor = ij.SelectionEditor(original, (32, 40))
    limits = ij.SelectionEditLimits()
    limits.max_working_bytes = 1
    calls = []
    with pytest.raises((ValueError, RuntimeError)):
        editor.resized(4, [25, 23], limits=limits, progress=lambda *v: calls.append(v), mode=ij.SelectionHandleMode.BOUNDS)
    assert calls == []
    limits = ij.SelectionEditLimits()
    def progress(done, total):
        nonlocal editor
        calls.append((done, total))
        editor = ij.SelectionEditor(geometry([0, 1, 1]), (32, 40))
        limits.max_working_bytes = 1
    result = editor.resized(4, [25, 23], limits=limits, progress=progress, mode=ij.SelectionHandleMode.BOUNDS)
    assert result.path.commands == transformed(COMMANDS, [0, 0], [2, 2])
    assert calls == [(0, 1), (1, 1)]
    token = ij.CancellationToken()
    with pytest.raises(ij.OperationCancelled):
        ij.SelectionEditor(original, (32, 40)).resized(4, [25, 23], cancellation=token,
            progress=lambda done, total: token.cancel() if done == 1 else None, mode=ij.SelectionHandleMode.BOUNDS)
    def fail(done, total):
        raise RuntimeError("callback failed")
    with pytest.raises(RuntimeError, match="callback failed"):
        ij.SelectionEditor(original, (32, 40)).resized(4, [25, 23], progress=fail, mode=ij.SelectionHandleMode.BOUNDS)
    assert original.path.commands == COMMANDS


def test_tiny_nonzero_resize_keeps_representable_path_operands():
    commands = [0, 0, 0, 1, 1, 0, 1, 1, 1, 1, 0, 1, 4]
    original = ij.Selection.from_path(ij.ImageJCompositePath(commands))
    result = ij.SelectionEditor(original, (24, 32)).resized(4, [1e-200, 1e-200], mode=ij.SelectionHandleMode.BOUNDS)
    assert result.path.commands == [0, 0, 0, 1, 1e-200, 0,
                                    1, 1e-200, 1e-200, 1, 0, 1e-200, 4]
    assert original.path.commands == commands


@pytest.mark.parametrize("matrix", [[0, 0, 0, 0, 1, 0], [1, 0, 0, 0, 0, 0],
                                     [1e-200, 1e-200, 0, 1e-200, 1e-200, 0]])
def test_tiny_resize_repair_keeps_singular_transform_refusal(matrix):
    path = ij.ImageJCompositePath([0, 0, 0, 1, 1, 0, 1, 1, 1, 1, 0, 1, 4])
    with pytest.raises(ValueError, match="invertible"):
        path.transformed(matrix)


def test_tiny_resize_repair_keeps_general_affine_near_collinear_contract():
    path = ij.ImageJCompositePath([0, 0, 0, 1, 1, 0, 1, 1, 1, 1, 0, 1, 4])
    near = math.nextafter(1., 2.)
    result = path.transformed([1, 1, 0, 1, near, 0])
    assert result.commands == [0, 0, 0, 1, 1, 1, 1, 2, 2, 1, 1, near, 4]


@pytest.mark.parametrize("controlled", [False, True])
@pytest.mark.parametrize("explicit_mode", [False, True])
def test_historical_path_resize_refusal_remains_before_callbacks(controlled, explicit_mode):
    original = geometry()
    editor = ij.SelectionEditor(original, (32, 40))
    calls = []
    options = {}
    if explicit_mode:
        options["mode"] = ij.SelectionHandleMode.CONTROL_POINTS
    if controlled:
        options.update(limits=ij.SelectionEditLimits(),
                       progress=lambda *values: calls.append(values),
                       checkpoint=lambda: calls.append("checkpoint"))
    with pytest.raises(ValueError):
        editor.resized(0, [1, 1], **options)
    assert calls == []
    assert editor.selection == original
    assert editor.selection.path.commands == COMMANDS
    assert all(handle.kind == ij.SelectionHandleKind.VERTEX for handle in editor.handles())


@pytest.mark.parametrize("kind", ["rectangle", "ellipse"])
def test_explicit_control_points_keeps_ordinary_resize_and_bounds_refuses(kind):
    original = ij.Selection(kind, [[2, 3], [6, 5]], width=3)
    editor = ij.SelectionEditor(original, (24, 32))
    expected = [[2, 3], [8, 7]]
    assert editor.resized(4, [8, 7]).points == expected
    assert editor.resized(4, [8, 7], mode=ij.SelectionHandleMode.CONTROL_POINTS).points == expected
    assert editor.resized(4, [8, 7], mode=ij.SelectionHandleMode.CONTROL_POINTS,
                          limits=ij.SelectionEditLimits()).points == expected
    calls = []
    with pytest.raises(ValueError):
        editor.resized(4, [8, 7], mode=ij.SelectionHandleMode.BOUNDS,
                       progress=lambda *values: calls.append(values),
                       checkpoint=lambda: calls.append("checkpoint"))
    assert calls == []
    assert editor.selection == original
