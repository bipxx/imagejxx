"""Installed scientific extraction commands, desktop actions and native state.

Every expected sample and world landmark is authored here from literal indices.
No product extraction, serializer or UI report supplies an expected result.
The finite matrix contains exactly 72 rows and exercises both command/publication
and desktop direct-action interfaces. Physical dialog coverage lives separately.
"""

import copy
import json
import os

import imagejxx as ij
import numpy as np
import pytest


KINDS = ("plane", "range")
PLANES = ("xy", "xz", "yz")
DTYPES = (np.uint8, np.uint16, np.float32, np.float64)
SAMPLES = (1, 3, 4)
AXES = {"xy": (0, 1, 2), "xz": (0, 2, 1), "yz": (1, 2, 0)}
EXTENTS = (7, 5, 4)  # XYZ, deliberately asymmetric.
CALIBRATION = {
    "spacing": [.5, 1.25, 2.5], "origin": [11., -7., 19.],
    "direction": [-1., .25, .5, 0., 1., .5, .5, 0., 1.],
    "unit": "um", "frame_interval": .75, "time_origin": 2.25,
    "time_unit": "s",
}
DEFAULT_CAP = 512 * 1024 * 1024


@pytest.fixture
def editor(tmp_path):
    required = os.environ.get("IMAGEJXX_REQUIRE_RENDER_TESTS", "0")
    assert required in {"0", "1"}
    if required == "1":
        from imagejxx import _desktop  # noqa: F401
    else:
        pytest.importorskip("imagejxx._desktop", exc_type=ModuleNotFoundError)
    from imagejxx.ui.app import Editor
    from imagejxx.ui.persistence import Preferences

    return Editor(preferences=Preferences(tmp_path / "preferences.json"))


def _fixture(dtype=np.uint16, samples=1, *, shape=(2, 3, 4, 5, 7), calibration=None,
             policy=None):
    t, c, z, y, x, s = np.indices((*shape, samples), dtype=np.int64)
    pixels = (64*t + 24*c + 8*z + 3*y + x + 2*s).astype(dtype)
    keywords = {"axes": "TCZYXS", "calibration": ij.SpatialCalibration(
        **(calibration or CALIBRATION))}
    if policy is not None:
        keywords["sample_policy"] = policy
    return pixels, ij.Dataset(pixels, **keywords)


def _owner(dtype=np.uint16, samples=1, *, dataset=None, shape=(2, 3, 4, 5, 7),
           spectrum=False):
    if dataset is None:
        pixels, dataset = _fixture(dtype, samples, shape=shape)
    else:
        pixels = dataset.numpy("TCZYXS")
    owner = ij.Document(dataset, "Scientific slicing literal")
    tx = owner.begin_edit()
    tx.set_property("source_only", {"nested": [None, True, 7, "retained"]})
    tx.set_extension("future_annotation", {"version": 37, "opaque": [3, 1, 4]})
    tx.set_record_layout({"source_presence": True})
    tx.select(ij.Selection("rectangle", [(1, 1), (4, 3)]))
    tx.set_previous_selection(ij.Selection("line", [(0, 0), (2, 2)]))
    tx.set_rois([
        ij.ROI("explicit zero", ij.Selection("rectangle", [(1, 1), (3, 3)]),
               t=0, c=0, z=0, attributes={"label": "zero"}),
        ij.ROI("wildcard", ij.Selection("point", [(2, 2)]), z=1),
    ])
    tx.set_overlays([{"name": "source overlay", "points": [[1, 1], [2, 2]]}])
    if spectrum:
        tx.set_spectrum(ij.FourierState(np.ones((5, 7), np.complex128), original_shape=(5, 7)))
    owner.commit(tx)
    owner.mark_saved()
    tx = owner.begin_edit()
    tx.set_property("history_probe", True)
    owner.commit(tx)
    owner.undo()  # Preserve a real redo entry as well as source save identity.
    view = owner.snapshot().display
    view.t, view.c, view.crosshair = 1, 2, [3, 2, 2]
    view.zoom, view.linked_zoom = 1.25, False
    view.plane_zoom, view.pan = [1.25, 2., 3.], [[3., -2.], [1., 4.], [-5., 2.]]
    view.orthogonal = True
    owner.set_display(view)
    return pixels, owner


def _raw(value):
    value = np.asarray(value)
    return np.ascontiguousarray(value.astype(value.dtype.newbyteorder("<"))).tobytes()


def _state(owner):
    snap, history = owner.snapshot(), owner.history()
    view = snap.display
    return {
        "snapshot": snap, "data_identity": snap.data_identity, "dirty": snap.dirty,
        "revisions": (snap.revisions.pixels, snap.revisions.metadata,
                      snap.revisions.selection, snap.revisions.display),
        "pixels": _raw(snap.dataset.numpy("TCZYXS")), "metadata": snap.metadata,
        "record_layout": snap.record_layout, "selection": snap.selection,
        "previous_selection": snap.previous_selection,
        "rois": [(r.name, r.selection, r.t, r.c, r.z, r.attributes) for r in snap.rois],
        "overlays": snap.overlays,
        "spectrum": None if snap.spectrum is None else _raw(snap.spectrum.numpy()),
        "calibration": {key: getattr(snap.dataset.calibration, key) for key in CALIBRATION},
        "display": (view.t, view.c, view.crosshair, view.zoom, view.linked_zoom,
                    view.plane_zoom, view.pan, view.orthogonal, view.revision, view.properties),
        "history": (history.undo_steps, history.redo_steps, history.step_limit, history.pixel_budget),
    }


def _unchanged(owner, before):
    after = _state(owner)
    assert after.pop("snapshot").shares_data(before["snapshot"])
    assert after == {key: value for key, value in before.items() if key != "snapshot"}


def _request(kind, plane, *, t=1, c=2, cap=DEFAULT_CAP):
    if kind == "plane":
        return ij.AxisPlaneExportRequest(plane, 2, t=t, c=c, max_working_bytes=cap)
    extent = EXTENTS[AXES[plane][2]]
    return ij.AxisRangeResliceRequest(plane, 1, extent - 1, 2,
                                      t=t, c=c, max_working_bytes=cap)


def _coordinates(request):
    if isinstance(request, ij.AxisPlaneExportRequest):
        return (request.coordinate,)
    return tuple(range(request.first, request.last + 1, request.step))


def _literal(pixels, request):
    volume = pixels[request.t, request.c]
    slices = []
    for fixed in _coordinates(request):
        if request.plane == "xy":
            slices.append(volume[fixed, :, :, :])
        elif request.plane == "xz":
            slices.append(volume[:, fixed, :, :])
        else:
            assert request.plane == "yz"
            slices.append(volume[:, :, fixed, :])
    return np.stack(slices)


def _geometry(dataset, request, layout=None):
    cal, order = dataset.calibration, AXES[request.plane]
    first = _coordinates(request)[0]
    step = 1 if isinstance(request, ij.AxisPlaneExportRequest) else request.step
    source_direction = np.asarray(CALIBRATION["direction"]).reshape(3, 3)
    source_spacing = CALIBRATION["spacing"]
    expected_origin = [CALIBRATION["origin"][r] + source_direction[r, order[2]] *
                       source_spacing[order[2]] * first for r in range(3)]
    assert cal.origin == expected_origin
    assert cal.spacing == [source_spacing[order[0]], source_spacing[order[1]],
                           source_spacing[order[2]] * step]
    assert cal.direction == [float(source_direction[r, a]) for r in range(3) for a in order]
    assert (cal.unit, cal.time_unit, cal.frame_interval, cal.time_origin) == (
        "um", "s", .75, 2.25 + .75*request.t)
    for k, fixed in enumerate(_coordinates(request)):
        for u, v in ((0, 0), (dataset.shape[4]-1, dataset.shape[3]-1)):
            voxel = [0, 0, 0]
            voxel[order[0]], voxel[order[1]], voxel[order[2]] = u, v, fixed
            expected = [CALIBRATION["origin"][r] + sum(source_direction[r, a] *
                        source_spacing[a] * voxel[a] for a in range(3)) for r in range(3)]
            assert cal.voxel_to_world((u, v, k)) == expected
    if layout is not None:
        assert tuple(layout.source_axes) == order and layout.plane_name == request.plane.upper()
        assert layout.geometry == {
            "origin": expected_origin,
            "horizontal_step": [float(source_direction[r, order[0]] * source_spacing[order[0]]) for r in range(3)],
            "vertical_step": [float(source_direction[r, order[1]] * source_spacing[order[1]]) for r in range(3)],
            "horizontal_axis": "XYZ"[order[0]], "vertical_axis": "XYZ"[order[1]], "unit": "um",
        }


def _output(owner, pixels, source, request, *, layout=None):
    snap = owner.snapshot()
    actual, expected = snap.dataset.numpy("ZYXS"), _literal(pixels, request)
    assert actual.dtype == pixels.dtype and _raw(actual) == _raw(expected)
    assert tuple(snap.dataset.shape) == (1, 1, *expected.shape)
    assert snap.dataset.sample_policy == source.dataset.sample_policy
    assert not snap.dataset.shares_pixels(source.dataset) and snap.id != source.id
    assert not snap.dirty and owner.undo_steps == owner.redo_steps == 0
    assert snap.selection is snap.previous_selection is snap.spectrum is None
    assert snap.rois == snap.overlays == [] and snap.record_layout == {}
    assert snap.metadata["properties"] == {}
    assert snap.display.t == snap.display.c == 0 and not snap.display.orthogonal
    assert snap.display.crosshair[2] == 0
    operation = "axis_plane_export" if isinstance(request, ij.AxisPlaneExportRequest) else "axis_range_reslice"
    order = AXES[request.plane]
    expected_provenance = {
        "schema_version": 1, "source_document_id": source.id,
        "plane": request.plane.upper(), "t": request.t, "c": request.c,
        "horizontal_axis": "XYZ"[order[0]], "vertical_axis": "XYZ"[order[1]],
        "fixed_axis": "XYZ"[order[2]], "index_convention": "zero_based_voxel_centers",
    }
    if operation == "axis_plane_export":
        expected_provenance["coordinate"] = request.coordinate
        title = f"{source.metadata['title']} {request.plane.upper()} {request.coordinate+1}"
    else:
        expected_provenance.update(first=request.first, last=request.last, step=request.step,
                                   count=len(_coordinates(request)))
        title = (f"{source.metadata['title']} {request.plane.upper()} "
                 f"{request.first+1}-{request.last+1} step {request.step}")
    assert snap.metadata["title"] == title
    assert snap.metadata["extensions"] == {operation: expected_provenance}
    _geometry(snap.dataset, request, layout)


def _activate(editor, owner):
    editor.active_index = next(i for i, row in enumerate(editor.document_infos) if row["id"] == owner.id)


def _desktop_action(editor, owner, request):
    if isinstance(request, ij.AxisPlaneExportRequest):
        return editor.action("export_axis_plane", document_id=owner.id,
                             plane=request.plane, coordinate=request.coordinate)
    return editor.action("reslice_axis_range", document_id=owner.id, plane=request.plane,
                         first=request.first, last=request.last, step=request.step)


@pytest.mark.parametrize("kind", KINDS)
@pytest.mark.parametrize("plane", PLANES)
@pytest.mark.parametrize("dtype", DTYPES)
@pytest.mark.parametrize("samples", SAMPLES)
def test_exact_72_native_command_and_desktop_action_rows(editor, kind, plane, dtype, samples):
    pixels, owner = _owner(dtype, samples)
    editor.add_native_document(owner)
    app = ij.Application()
    app.adopt(owner)
    before, request = _state(owner), _request(kind, plane)
    command = ij.resolve_axis_extraction_command(owner.snapshot(), request)
    record = command.record()
    assert record["schema"] == "imagejxx.axis-extraction-command" and record["version"] == 1
    assert record["source"]["shape"] == [2, 3, 4, 5, 7, samples]
    assert record["source"]["document_id"] == owner.id
    assert record["request"]["t"] == 1 and record["request"]["c"] == 2
    assert record["destination"] == "new_document"
    ij.validate_axis_extraction_command_record(json.loads(json.dumps(record)))
    prepared = ij.prepare_axis_extraction(command)
    candidate = prepared.prepared_new_document()
    assert candidate is not None and not app.contains(candidate.id)
    _output(candidate, pixels, before["snapshot"], request, layout=command.layout)
    assert tuple(command.layout.shape) == tuple(candidate.snapshot().dataset.shape)
    assert record["derived"]["working_bytes"] == command.layout.working_bytes > 0
    published = app.publish(prepared)
    assert published.created and published.document is candidate and not prepared.active
    assert app.document_ids == [owner.id, candidate.id]
    _unchanged(owner, before)
    app.close(candidate.id, discard=True)
    ids = [row["id"] for row in editor.document_infos]
    assert _desktop_action(editor, owner, request) is None
    output = editor.live_document().native_owner
    assert [row["id"] for row in editor.document_infos] == ids + [output.id]
    _output(output, pixels, before["snapshot"], request)
    assert editor.workflow == editor.results == []
    _unchanged(owner, before)
    editor.close_document(output.id, discard=True)


@pytest.mark.parametrize("kind", KINDS)
@pytest.mark.parametrize("plane", PLANES)
def test_quiet_nan_payload_signed_zero_subnormal_exact_bits(editor, kind, plane):
    pattern = np.array([0, 0x80000000, 1, 0x7FC12345, 0xFFC34567, 0x3F800000], np.uint32)
    bits = pattern[np.arange(2*3*4*5*7) % len(pattern)].reshape(2, 3, 4, 5, 7, 1)
    policy = ij.FloatingSamplePolicy.PRESERVE_QUIET_NAN_FLOAT32
    dataset = ij.Dataset(bits.view(np.float32), axes="TCZYXS", sample_policy=policy,
                         calibration=ij.SpatialCalibration(**CALIBRATION))
    _, owner = _owner(dataset=dataset)
    editor.add_native_document(owner)
    before, request = _state(owner), _request(kind, plane)
    command = ij.resolve_axis_extraction_command(owner.snapshot(), request)
    assert command.record()["source"]["sample_policy"] == "preserve_quiet_nan_float32"
    prepared = ij.prepare_axis_extraction(command)
    np.testing.assert_array_equal(prepared.prepared_new_document().snapshot().dataset.numpy("ZYXS").view(np.uint32),
                                  _literal(bits, request))
    _desktop_action(editor, owner, request)
    output = editor.live_document().native_owner
    assert output.snapshot().dataset.sample_policy == policy
    np.testing.assert_array_equal(output.snapshot().dataset.numpy("ZYXS").view(np.uint32), _literal(bits, request))
    _unchanged(owner, before)
    editor.close_document(output.id, discard=True)


@pytest.mark.parametrize("kind", KINDS)
def test_command_captured_values_and_defensive_python_copies(kind):
    pixels, owner = _owner()
    request = _request(kind, "xz")
    command = ij.resolve_axis_extraction_command(owner.snapshot(), request)
    original = copy.deepcopy(command.record())
    request.t, request.c, request.max_working_bytes = 0, 0, 0
    detached = command.request
    detached.t, detached.c, detached.max_working_bytes = 0, 0, 0
    layout = command.layout
    with pytest.raises(AttributeError):
        layout.calibration.origin = [999, 998, 997]
    detached_origin = layout.calibration.origin
    detached_origin[0] = 999
    assert layout.calibration.origin == original["derived"]["calibration"]["origin"]
    geometry = layout.geometry
    geometry["origin"][0] = 123
    changed = command.record()
    changed["source"]["shape"][0] = 99
    changed["derived"]["calibration"]["origin"][0] = 99
    assert command.record() == original and command.request.t == 1 and command.request.c == 2
    assert command.source.shares_data(owner.snapshot())
    assert command.operation == ("axis_plane_export" if kind == "plane" else "axis_range_reslice")
    for name in ("source", "request", "layout", "operation"):
        with pytest.raises(AttributeError):
            setattr(command, name, None)
    _output(ij.prepare_axis_extraction(command).prepared_new_document(), pixels,
            owner.snapshot(), _request(kind, "xz"), layout=command.layout)


# Exact standalone malformed membership. Boolean alternatives are deliberately
# tested in metadata, independently of legacy typed-request constructor coercion.
MALFORMED = (
    ("unknown-top", ("extra",), 1), ("schema", ("schema",), "other"),
    ("version-bool", ("version",), True), ("version-future", ("version",), 2),
    ("operation", ("operation",), "reslice"), ("destination", ("destination",), "in_place"),
    ("owner-bool", ("source", "document_id"), True), ("owner-zero", ("source", "document_id"), 0),
    ("owner-overflow", ("source", "document_id"), 2**64),
    ("shape-bool", ("source", "shape", 0), True), ("shape-zero", ("source", "shape", 2), 0),
    ("shape-overflow", ("source", "shape", 4), 2**31),
    ("pixel-type", ("source", "pixel_type"), "uint32"),
    ("sample-policy", ("source", "sample_policy"), "unchecked"),
    ("nan-policy-wrong-type", ("source", "sample_policy"), "preserve_quiet_nan_float32"),
    ("revision-bool", ("source", "revisions", "pixels"), False),
    ("revision-negative", ("source", "revisions", "metadata"), -1),
    ("revision-overflow", ("source", "revisions", "pixels"), 2**64),
    ("calibration-bool", ("source", "calibration", "spacing", 0), True),
    ("calibration-singular", ("source", "calibration", "direction"), [0.]*9),
    ("calibration-nonfinite", ("source", "calibration", "origin", 0), float("inf")),
    ("plane-case", ("request", "plane"), "xy"),
    ("coordinate-bool", ("request", "coordinate"), True),
    ("coordinate-negative", ("request", "coordinate"), -1),
    ("coordinate-end", ("request", "coordinate"), 4),
    ("coordinate-fraction", ("request", "coordinate"), 1.25),
    ("time-end", ("request", "t"), 2), ("channel-bool", ("request", "c"), True),
    ("cap-zero", ("request", "max_working_bytes"), 0),
    ("cap-bool", ("request", "max_working_bytes"), True),
    ("cap-overflow", ("request", "max_working_bytes"), 2**64),
    ("mixed-variant", ("request", "first"), 0),
    ("derived-shape", ("derived", "shape", 4), 8),
    ("derived-shape-bool", ("derived", "shape", 0), True),
    ("derived-axes", ("derived", "source_axes"), [0, 2, 1]),
    ("derived-plane", ("derived", "plane_name"), "XZ"),
    ("derived-working", ("derived", "working_bytes"), 1),
    ("derived-origin", ("derived", "geometry", "origin", 0), 123),
    ("derived-basis", ("derived", "geometry", "horizontal_step", 2), 9),
    ("derived-calibration", ("derived", "calibration", "frame_interval"), 2),
)


def _tampered(record, path, value):
    result = copy.deepcopy(record)
    target = result
    for key in path[:-1]:
        target = target[key]
    target[path[-1]] = value
    return result


@pytest.mark.parametrize("label,path,value", MALFORMED, ids=[row[0] for row in MALFORMED])
def test_standalone_record_validation_and_rebind_reject_malformed_original(label, path, value):
    _, owner = _owner()
    record = ij.resolve_axis_extraction_command(owner.snapshot(), _request("plane", "xy")).record()
    bad = _tampered(record, path, value)
    before = _state(owner)
    for operation in (lambda: ij.validate_axis_extraction_command_record(bad),
                      lambda: ij.axis_extraction_command_from_record(owner.snapshot(), bad),
                      lambda: ij.rebind_axis_extraction_command(owner.snapshot(), bad, _request("range", "yz"))):
        with pytest.raises((ValueError, TypeError, OverflowError, IndexError)):
            operation()
    _unchanged(owner, before)


@pytest.mark.parametrize("kind", KINDS)
def test_strict_replay_navigation_selection_and_stale_metadata_pixels(kind):
    pixels, owner = _owner()
    request = _request(kind, "yz")
    record = ij.resolve_axis_extraction_command(owner.snapshot(), request).record()
    view = owner.snapshot().display
    view.t, view.c, view.crosshair, view.zoom = 0, 0, [1, 1, 0], 2.5
    owner.set_display(view)
    owner.select(ij.Selection("point", [(3, 2)]))
    decoded = ij.axis_extraction_command_from_record(owner.snapshot(), json.loads(json.dumps(record)))
    assert decoded.record() == record and decoded.request.t == 1 and decoded.request.c == 2
    _output(ij.prepare_axis_extraction(decoded).prepared_new_document(), pixels, owner.snapshot(), request)
    edit = owner.begin_edit()
    edit.set_property("stale_metadata", True)
    owner.commit(edit)
    with pytest.raises(ij.RevisionConflict):
        ij.axis_extraction_command_from_record(owner.snapshot(), record)
    owner.undo()
    with pytest.raises(ij.RevisionConflict):
        ij.axis_extraction_command_from_record(owner.snapshot(), record)
    fresh = ij.resolve_axis_extraction_command(owner.snapshot(), request).record()
    edit = owner.begin_edit()
    edit.replace_plane(np.full((5, 7), 9, np.uint16), t=1, c=2, z=1)
    owner.commit(edit)
    with pytest.raises(ij.RevisionConflict):
        ij.axis_extraction_command_from_record(owner.snapshot(), fresh)


@pytest.mark.parametrize("kind", KINDS)
def test_explicit_rebind_uses_new_owner_request_geometry_and_session_binding(kind):
    pixels, owner = _owner()
    original = ij.resolve_axis_extraction_command(owner.snapshot(), _request(kind, "xy")).record()
    limits = ij.SessionLimits()
    limits.max_documents = 16
    limits.max_encoded_bytes = limits.max_json_bytes = 32*1024*1024
    limits.max_working_bytes = 128*1024*1024
    legacy = {"operation": "add", "parameters": {"value": 2}}
    decoded = ij.decode_session(ij.encode_session(ij.SessionCapture(
        [owner.snapshot()], workflow=[legacy, original]), limits), limits)
    reopened, = decoded.documents
    assert decoded.workflow == [legacy, original] and reopened.id != owner.id
    with pytest.raises(ValueError):
        ij.axis_extraction_command_from_record(reopened.snapshot(), original)
    calibration = ij.SpatialCalibration(**{**CALIBRATION, "origin": [-13., 5., 7.],
                                           "spacing": [2., 3., 4.], "time_origin": 31.})
    edit = reopened.begin_edit()
    edit.set_calibration(calibration)
    reopened.commit(edit)
    request = _request(kind, "xz", t=0, c=1)
    rebound = ij.rebind_axis_extraction_command(reopened.snapshot(), original, request)
    assert rebound.record()["source"]["document_id"] == reopened.id
    assert rebound.record()["source"]["calibration"]["origin"] == [-13., 5., 7.]
    assert rebound.record()["request"]["t"] == 0 and rebound.record()["request"]["c"] == 1
    output = ij.prepare_axis_extraction(rebound).prepared_new_document().snapshot().dataset
    np.testing.assert_array_equal(output.numpy("ZYXS"), _literal(pixels, request))
    # New origin uses the complete sheared Y column, not old derived geometry.
    first = _coordinates(request)[0]
    assert output.calibration.origin == [-13.+.25*3*first, 5.+3*first, 7.]
    assert output.calibration.spacing == [2., 4., 3.*(1 if kind == "plane" else 2)]
    assert output.calibration.time_origin == 31.
    assert original["source"]["document_id"] == owner.id and original["request"]["plane"] == "XY"
    for invalid in ({}, True):
        with pytest.raises(TypeError):
            ij.rebind_axis_extraction_command(reopened.snapshot(), original, invalid)


@pytest.mark.parametrize("kind", KINDS)
@pytest.mark.parametrize("phase", ("before", "positive_work"))
def test_command_cancellation_never_publishes_or_changes_source(kind, phase):
    _, owner = _owner()
    app = ij.Application()
    app.adopt(owner)
    command = ij.resolve_axis_extraction_command(owner.snapshot(), _request(kind, "xz"))
    before, members = _state(owner), app.document_ids
    token, calls = ij.CancellationToken(), []
    if phase == "before":
        token.cancel()

    def progress(done, total):
        calls.append((done, total))
        if done > 0:
            token.cancel()

    with pytest.raises(ij.OperationCancelled):
        ij.prepare_axis_extraction(command, cancellation=token, progress=progress)
    assert token.canceled and (not calls if phase == "before" else any(n > 0 for n, _ in calls))
    assert app.document_ids == members and app.active_document_id == owner.id
    _unchanged(owner, before)


@pytest.mark.parametrize("kind", KINDS)
def test_execution_cap_only_tightens_and_preserves_captured_record(kind):
    pixels, owner = _owner()
    command = ij.resolve_axis_extraction_command(owner.snapshot(), _request(kind, "xz"))
    record, calls = command.record(), []
    for cap in (0, command.layout.working_bytes-1):
        with pytest.raises(ValueError):
            ij.prepare_axis_extraction(command, execution_max_working_bytes=cap,
                                       progress=lambda *args: calls.append(args))
    assert not calls and command.record() == record
    prepared = ij.prepare_axis_extraction(command, execution_max_working_bytes=command.layout.working_bytes)
    _output(prepared.prepared_new_document(), pixels, owner.snapshot(), _request(kind, "xz"))
    assert command.record() == record


@pytest.mark.parametrize("kind", KINDS)
@pytest.mark.parametrize("callback", ("progress", "checkpoint"))
def test_bad_or_throwing_callbacks_abort_without_membership(kind, callback):
    _, owner = _owner()
    command = ij.resolve_axis_extraction_command(owner.snapshot(), _request(kind, "xz"))
    before = _state(owner)
    with pytest.raises(ValueError, match="callbacks must be callable"):
        ij.prepare_axis_extraction(command, **{callback: []})

    def fail(*_):
        raise RuntimeError("independent extraction callback failure")

    with pytest.raises(RuntimeError, match="independent extraction callback failure"):
        ij.prepare_axis_extraction(command, **{callback: fail})
    _unchanged(owner, before)


@pytest.mark.parametrize("kind", KINDS)
@pytest.mark.parametrize("reason", ("source_pixels", "source_metadata", "candidate_edit", "closed", "foreign", "cancelled"))
def test_atomic_native_publication_refusal(kind, reason):
    _, owner = _owner()
    app = ij.Application()
    app.adopt(owner)
    token = ij.CancellationToken()
    prepared = ij.prepare_axis_extraction(ij.resolve_axis_extraction_command(owner.snapshot(),
        _request(kind, "xz")), cancellation=token)
    candidate = prepared.prepared_new_document()
    publisher, error = app, ij.RevisionConflict
    if reason == "source_pixels":
        edit = owner.begin_edit()
        edit.replace_plane(np.full((5, 7), 7, np.uint16), t=1, c=2, z=0)
        owner.commit(edit)
    elif reason == "source_metadata":
        edit = owner.begin_edit()
        edit.set_property("publication_conflict", True)
        owner.commit(edit)
    elif reason == "candidate_edit":
        edit = candidate.begin_edit()
        edit.set_property("candidate_conflict", True)
        candidate.commit(edit)
    elif reason == "closed":
        app.close(owner.id, discard=True)
        error = ij.DocumentUnavailable
    elif reason == "foreign":
        publisher, error = ij.Application(), ij.DocumentUnavailable
    else:
        token.cancel()
        error = ij.OperationCancelled
    before, candidate_before = _state(owner), _state(candidate)
    members, publisher_members = app.document_ids, publisher.document_ids
    with pytest.raises(error):
        publisher.publish(prepared)
    assert prepared.active and not app.contains(candidate.id)
    assert app.document_ids == members and publisher.document_ids == publisher_members
    _unchanged(owner, before)
    _unchanged(candidate, candidate_before)


def test_headless_spectrum_and_all_source_annotation_storage_preserved():
    pixels, owner = _owner(np.float64, spectrum=True)
    before = _state(owner)
    command = ij.resolve_axis_extraction_command(owner.snapshot(), _request("range", "yz"))
    _output(ij.prepare_axis_extraction(command).prepared_new_document(), pixels,
            owner.snapshot(), _request("range", "yz"))
    _unchanged(owner, before)


@pytest.mark.parametrize("kind", KINDS)
def test_singleton_explicit_zero_positioned_roi_retains_scientific_admission(editor, kind):
    pixels, dataset = _fixture(np.uint16, shape=(1, 1, 1, 5, 7))
    owner = ij.Document(dataset, "Singleton positioned ROI")
    edit = owner.begin_edit()
    edit.set_rois([ij.ROI("explicit zero wildcard Z", ij.Selection("point", [(2, 2)]), t=0, c=0)])
    owner.commit(edit)
    owner.mark_saved()
    editor.add_native_document(owner)
    request = _request(kind, "xz", t=0, c=0)
    before = _state(owner)
    _desktop_action(editor, owner, request)
    _output(editor.live_document().native_owner, pixels, before["snapshot"], request)
    _unchanged(owner, before)


@pytest.mark.parametrize("kind", KINDS)
def test_desktop_inactive_foreign_closed_and_required_id_refuse_atomically(editor, kind):
    _, first = _owner()
    _, second = _owner()
    editor.add_native_document(first)
    editor.add_native_document(second)
    before, members, workflow = _state(second), editor.document_infos, editor.workflow
    _, foreign = _owner()
    action = "export_axis_plane" if kind == "plane" else "reslice_axis_range"
    fields = {"plane": "xy", "coordinate": 1} if kind == "plane" else {
        "plane": "xz", "first": 1, "last": 4, "step": 2}
    for document_id in (first.id, foreign.id, 2**63):
        with pytest.raises((ValueError, IndexError, ij.DocumentUnavailable)):
            editor.action(action, document_id=document_id, **fields)
        assert editor.document_infos == members and editor.workflow == workflow and not editor.busy
        _unchanged(second, before)
    editor.close_document(first.id, discard=True)
    with pytest.raises((ValueError, IndexError, ij.DocumentUnavailable)):
        editor.action(action, document_id=first.id, **fields)
    if kind == "range":
        with pytest.raises(ValueError):
            editor.action(action, **fields)
    _unchanged(second, before)


DESKTOP_INVALID = (
    ("plane-bool", "export_axis_plane", {"plane": True, "coordinate": 1}),
    ("coordinate-bool", "export_axis_plane", {"plane": "xy", "coordinate": True}),
    ("coordinate-negative", "export_axis_plane", {"plane": "xy", "coordinate": -1}),
    ("coordinate-end", "export_axis_plane", {"plane": "xy", "coordinate": 4}),
    ("coordinate-overflow", "export_axis_plane", {"plane": "xy", "coordinate": 2**31}),
    ("unknown-plane-field", "export_axis_plane", {"plane": "xy", "coordinate": 1, "t": 0}),
    ("range-bool", "reslice_axis_range", {"plane": "xz", "first": True, "last": 4, "step": 1}),
    ("range-reversed", "reslice_axis_range", {"plane": "xz", "first": 4, "last": 1, "step": 1}),
    ("range-end", "reslice_axis_range", {"plane": "xz", "first": 1, "last": 5, "step": 1}),
    ("range-zero-step", "reslice_axis_range", {"plane": "xz", "first": 1, "last": 4, "step": 0}),
    ("range-negative-step", "reslice_axis_range", {"plane": "xz", "first": 1, "last": 4, "step": -1}),
    ("range-step-bool", "reslice_axis_range", {"plane": "xz", "first": 1, "last": 4, "step": True}),
    ("range-fraction", "reslice_axis_range", {"plane": "xz", "first": 1.5, "last": 4, "step": 1}),
    ("range-overflow", "reslice_axis_range", {"plane": "xz", "first": 1, "last": 4, "step": 2**31}),
)


@pytest.mark.parametrize("label,action,fields", DESKTOP_INVALID, ids=[row[0] for row in DESKTOP_INVALID])
def test_desktop_invalid_requests_have_no_output_or_record(editor, label, action, fields):
    _, owner = _owner()
    editor.add_native_document(owner)
    editor.recording = True
    before, members = _state(owner), editor.document_infos
    with pytest.raises((ValueError, IndexError, OverflowError)):
        editor.action(action, document_id=owner.id, **fields)
    assert editor.document_infos == members and editor.workflow == editor.results == []
    _unchanged(owner, before)


@pytest.mark.parametrize("kind", KINDS)
def test_desktop_recording_strict_replay_explicit_rebind_and_native_session(editor, tmp_path, kind):
    pixels, owner = _owner()
    editor.add_native_document(owner)
    request = _request(kind, "xz")
    editor.recording = True
    source_before = _state(owner)
    _desktop_action(editor, owner, request)
    first_output = editor.live_document().native_owner
    original, = copy.deepcopy(editor.workflow)
    assert original == ij.resolve_axis_extraction_command(source_before["snapshot"], request).record()
    _output(first_output, pixels, source_before["snapshot"], request)
    editor.close_document(first_output.id, discard=True)
    _activate(editor, owner)
    editor.live_document().set_view(t=0, c=0, crosshair=[1, 1, 0], zoom=2.5)
    source_navigated = _state(owner)
    output_id = editor.replay_axis_extraction(original, document_id=owner.id)
    assert type(output_id) is int and output_id == editor.live_document().id
    _output(editor.live_document().native_owner, pixels, source_navigated["snapshot"], request)
    assert editor.workflow == [original, original] and editor.results == []
    _unchanged(owner, source_navigated)
    editor.close_document(output_id, discard=True)
    _activate(editor, owner)
    editor.recording = False
    editor.action("replay_axis_extraction", record=original, document_id=owner.id)
    assert editor.workflow == [original, original]
    editor.close_document(editor.live_document().id, discard=True)
    _activate(editor, owner)
    path = tmp_path / (kind + "-scientific-extraction.ijx")
    editor.action("save_document", path=str(path), document_id=owner.id)
    editor.open_file(path)
    reopened = editor.live_document().native_owner
    assert reopened.id != owner.id and editor.workflow == [original, original]
    members = editor.document_infos
    with pytest.raises(ValueError):
        editor.replay_axis_extraction(original, document_id=reopened.id)
    assert editor.document_infos == members
    new_request = _request(kind, "yz", t=0, c=0)
    edit = reopened.begin_edit()
    edit.set_calibration(ij.SpatialCalibration(**{**CALIBRATION, "origin": [-3., 2., 7.]}))
    reopened.commit(edit)
    before_rebind = _state(reopened)
    editor.recording = True
    rebound_id = editor.replay_axis_extraction(original, rebind_request=new_request,
                                               document_id=reopened.id)
    assert rebound_id == editor.live_document().id
    rebound = editor.workflow[-1]
    assert editor.workflow[:-1] == [original, original]
    assert rebound["source"]["document_id"] == reopened.id and rebound["request"]["plane"] == "YZ"
    assert rebound["source"]["calibration"]["origin"] == [-3., 2., 7.]
    np.testing.assert_array_equal(editor.live_document().native_snapshot().dataset.numpy("ZYXS"),
                                  _literal(pixels, new_request))
    _unchanged(reopened, before_rebind)
    assert original["source"]["document_id"] == owner.id


@pytest.mark.parametrize("kind", KINDS)
def test_desktop_replay_malformed_stale_and_rebind_atomicity(editor, kind):
    _, owner = _owner()
    editor.add_native_document(owner)
    record = ij.resolve_axis_extraction_command(owner.snapshot(), _request(kind, "xy")).record()
    malformed = _tampered(record, ("derived", "geometry", "origin", 0), 123)
    editor.recording = True
    before, members = _state(owner), editor.document_infos
    for rebind in (None, _request(kind, "yz")):
        with pytest.raises(ValueError):
            editor.replay_axis_extraction(malformed, rebind_request=rebind)
    for bad in ({}, True):
        with pytest.raises((TypeError, ValueError)):
            editor.replay_axis_extraction(record, rebind_request=bad)
    for parameters in ({}, {"record": record, "rebind_request": {}},
                       {"record": record, "rebind_request": True}):
        with pytest.raises(ValueError):
            editor.action("replay_axis_extraction", **parameters)
    assert editor.document_infos == members and editor.workflow == editor.results == []
    _unchanged(owner, before)
    edit = owner.begin_edit()
    edit.set_property("stale_extraction", True)
    owner.commit(edit)
    stale_before = _state(owner)
    members = editor.document_infos
    with pytest.raises(ij.RevisionConflict):
        editor.replay_axis_extraction(record)
    assert editor.document_infos == members and editor.workflow == editor.results == []
    _unchanged(owner, stale_before)


@pytest.mark.parametrize("scientific", (False, True))
def test_mixed_workflow_roundtrip_and_unsupported_generic_runner_is_atomic(editor, scientific):
    if scientific:
        _, owner = _owner()
        request = _request("plane", "xy")
    else:
        owner = ij.Document(ij.Dataset(np.arange(140, dtype=np.uint16).reshape(4, 5, 7), axes="ZYX"))
        request = ij.AxisPlaneExportRequest("xz", 2)
    editor.add_native_document(owner)
    record = ij.resolve_axis_extraction_command(owner.snapshot(), request).record()
    legacy = {"operation": "add", "parameters": {"value": 2}, "all_slices": False}
    editor.workflow = [legacy, record]
    editor.recording = True
    before, members = _state(owner), editor.document_infos
    with pytest.raises((ValueError, RuntimeError)):
        editor.action("run_workflow")
        editor.wait()
    assert not editor.busy and editor.document_infos == members
    assert editor.workflow == [legacy, record] and editor.results == []
    _unchanged(owner, before)


def _preview(editor, owner):
    editor.live_document(owner.id).set_view(orthogonal=False)
    editor.action("show_operation", operation="gaussian_blur_imagej", document_id=owner.id)
    panel = editor.render_headless(frames=2)["gaussian_panel"]
    point = panel["controls"]["preview"]
    assert len(point) == 2 and all(value >= 0 for value in point)
    clicked = editor.render_headless(frames=2, events=[
        {"type": "position", "space": "screen", "point": point},
        {"type": "button", "name": "left", "down": True},
        {"type": "button", "name": "left", "down": False},
    ])
    assert clicked["gaussian_panel"]["preview"] is True
    editor.wait()
    observed = editor.render_headless(frames=2)
    lane = observed["gaussian_jobs"]
    assert lane["preview"]["exit"] == "installed" and lane["active_installed_generation"] is not None
    assert lane["resource_usage"]["retained_reserved_bytes"] > 0
    assert observed["ordinary_pixels"]["pixels_source"] == "preview"
    return lane


def _same_preview(editor, baseline):
    lane = editor.render_headless(frames=2)["gaussian_jobs"]
    for key in ("requested_generation", "installed_generation", "active_installed_generation"):
        assert lane[key] == baseline[key]
    assert lane["preview"]["job"]["id"] == baseline["preview"]["job"]["id"]
    assert lane["preview"]["target"] == baseline["preview"]["target"]
    assert lane["preview"]["exit"] == "installed"
    assert lane["resource_usage"]["retained_reserved_bytes"] == baseline["resource_usage"]["retained_reserved_bytes"]


@pytest.mark.parametrize("kind", KINDS)
def test_ready_preview_cancel_refusal_and_committed_output_activation(editor, kind):
    pixels, owner = _owner(np.float32)
    editor.add_native_document(owner)
    baseline = _preview(editor, owner)
    before, members = _state(owner), editor.document_infos
    record = ij.resolve_axis_extraction_command(owner.snapshot(), _request(kind, "xz")).record()
    malformed = _tampered(record, ("derived", "geometry", "origin", 0), 123)
    with pytest.raises(ValueError):
        editor.replay_axis_extraction(malformed)
    _same_preview(editor, baseline)
    action = "show_axis_plane_export" if kind == "plane" else "show_axis_range_reslice"
    editor.action(action, document_id=owner.id)
    dialog = editor.render_headless(frames=2)["dialog"]
    assert dialog["open"]
    cancel = dialog["controls"]["cancel"]
    assert cancel["enabled"] and cancel["id"] > 0
    point = [int(value) for value in cancel["point"]]
    assert cancel["rect"][0] <= point[0] < cancel["rect"][2]
    assert cancel["rect"][1] <= point[1] < cancel["rect"][3]
    editor.render_headless(frames=2, events=[
        {"type": "position", "space": "screen", "point": point},
        {"type": "button", "name": "left", "down": True},
        {"type": "button", "name": "left", "down": False},
    ])
    assert not editor.render_headless(frames=2)["dialog"]["open"]
    _same_preview(editor, baseline)
    assert editor.document_infos == members and editor.workflow == editor.results == []
    _unchanged(owner, before)
    _desktop_action(editor, owner, _request(kind, "xz"))
    _output(editor.live_document().native_owner, pixels, before["snapshot"], _request(kind, "xz"))
    for _ in range(8):
        editor.poll()
        lane = editor.render_headless(frames=2)["gaussian_jobs"]
        if lane["resource_usage"]["reserved_bytes"] == 0:
            break
    assert lane["active_installed_generation"] is None
    assert lane["resource_usage"]["reserved_bytes"] == 0
    _unchanged(owner, before)


def test_processing_cap_failure_preserves_ready_preview_and_no_record(editor):
    # The preview reserves one small XY plane; the exact full-Z output exceeds
    # one MiB. Set the limit while idle, before any retained reservation exists.
    budget = 1024*1024
    depth, height, width = 256, 33, 33
    editor.update_settings(processing_memory_mb=1, persist=False)
    _, owner = _owner(np.float32, shape=(2, 3, depth, height, width))
    editor.add_native_document(owner)
    baseline = _preview(editor, owner)
    usage = baseline["resource_usage"]
    assert 0 < usage["retained_reserved_bytes"] <= usage["reserved_bytes"] <= budget
    assert baseline["preview"]["job"]["resource_bytes"] <= budget
    # Existing XY extraction's exact output plus one XY fallback plane; the
    # already retained preview is a separate scheduler reservation.
    assert (depth+1)*height*width*8 > budget
    before, members = _state(owner), editor.document_infos
    editor.recording = True
    with pytest.raises(ValueError, match="working-memory estimate exceeds"):
        editor.action("reslice_axis_range", document_id=owner.id,
                      plane="xy", first=0, last=depth-1, step=1)
    assert editor.document_infos == members and editor.workflow == editor.results == []
    _same_preview(editor, baseline)
    _unchanged(owner, before)


@pytest.mark.parametrize("kind", KINDS)
def test_scientific_apply_busy_refuses_slicing_before_preparation(editor, kind):
    _, owner = _owner(np.float32)
    editor.add_native_document(owner)
    before, members = _state(owner), editor.document_infos
    editor.apply_gaussian(options=ij.GaussianBlurCommandOptions(1))
    assert editor.busy
    with pytest.raises(ValueError):
        _desktop_action(editor, owner, _request(kind, "xz"))
    assert editor.document_infos == members and editor.workflow == editor.results == []
    _unchanged(owner, before)
    editor.wait()
