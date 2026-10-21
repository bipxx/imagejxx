"""Exact public axis exports: pinned pixels and separately owned affine/state rules.

Only typed pixels and scalar spacing are compared to ImageJ. Full world basis,
time, ownership and history assertions express the independently owned contract.
No reference process, GUI, fixture generator or target-generated oracle runs here.
"""

import hashlib
import json
from pathlib import Path

import imagejxx as ij
import numpy as np
import pytest


CASE_IDS = (
    "u8.xy.lower", "u8.xz.lower", "u8.yz.lower",
    "u16.xy.interior", "u16.xz.interior", "u16.yz.interior",
    "f32.xy.upper", "f32.xz.upper", "f32.yz.upper",
    "rgb.xy.interior", "rgb.xz.interior", "rgb.yz.interior",
)
FIXTURE_SHA = "05cb09b95b2dcfd2e1e492150d3883497c0bb5a203c8dde3b5b5b1830f3b5d08"
WRAPPERS = ("export", "snapshot_prepare", "application_prepare")
AXES = {"xy": (0, 1, 2), "xz": (0, 2, 1), "yz": (1, 2, 0)}


@pytest.fixture(scope="module")
def oracle():
    # Supports root-owned execution from the held stage and later live adoption.
    candidates = [parent / "tests/fixtures/axis-plane-imagej154r.json"
                  for parent in tuple(Path(__file__).resolve().parents)[:8]]
    paths = [path for path in candidates if path.is_file()]
    assert len(paths) == 1
    raw = paths[0].read_bytes()
    assert len(raw) == 1_307_528 and hashlib.sha256(raw).hexdigest() == FIXTURE_SHA
    corpus = json.loads(raw)
    assert corpus["schema_version"] == 1
    assert corpus["imagej_version"] == "1.54r"
    assert corpus["imagej_commit"] == "62db90a269cccc1ac79f658e4602043214ed055b"
    assert tuple(corpus["exact_ordered_case_ids"]) == CASE_IDS
    assert tuple(row["case_id"] for row in corpus["cases"]) == CASE_IDS
    return {row["case_id"]: row for row in corpus["cases"]}


def reference_source(kind):
    z, y, x = np.indices((9, 17, 32), dtype=np.int64)
    if kind == "u8":
        return ((37 * x + 61 * y + 13 * x * y + 43 * z) % 251).astype(np.uint8)
    if kind == "u16":
        return (6000 * z + 32 * y + x).astype(np.uint16)
    if kind == "f32":
        return (10000 * z + 100 * y + x + 0.25).astype(np.float32)
    assert kind == "rgb"
    return np.stack(((17 * x + 31 * y + 47 * z) % 256,
                     (71 * x + 11 * y + 29 * z) % 256,
                     (13 * x + 61 * y + 23 * z) % 256), axis=-1).astype(np.uint8)


def typed_bytes(array):
    value = np.asarray(array)
    return np.ascontiguousarray(value.astype(value.dtype.newbyteorder("<"))).tobytes()


def literal_slice(pixels, plane, coordinate):
    if plane == "xy":
        return pixels[coordinate, :, :].copy()
    if plane == "xz":
        return pixels[:, coordinate, :].copy()
    assert plane == "yz"
    return pixels[:, :, coordinate].copy()


def scientific_source(dtype=np.float64, samples=1, origin=(-2.0, 4.0, 9.0)):
    t, c, z, y, x, s = np.indices((2, 2, 3, 4, 5, samples), dtype=np.int64)
    values = 64 * t + 32 * c + 8 * z + 2 * y + x + 24 * s
    if dtype == np.uint16:
        values = values + 4096
    elif dtype == np.float32:
        values = values - 32.5
    elif dtype == np.float64:
        values = values - 32.375
    pixels = values.astype(dtype)
    calibration = ij.SpatialCalibration(
        spacing=(0.25, 1.5, 3.0), origin=origin,
        direction=(-1.0, 0.5, 0.25, 0.0, 1.0, 0.5, 0.5, 0.0, 1.0),
        unit="um", frame_interval=2.5, time_origin=-3.0, time_unit="min",
    )
    return pixels, ij.Dataset(pixels, axes="TCZYXS", calibration=calibration)


def revisions(snapshot):
    r = snapshot.revisions
    return r.pixels, r.metadata, r.selection, r.display


def state(owner):
    snap = owner.snapshot()
    view = snap.display
    history = owner.history()
    return {
        "snapshot": snap, "id": snap.id, "data_identity": snap.data_identity,
        "revisions": revisions(snap), "dirty": snap.dirty,
        "raw": typed_bytes(snap.dataset.numpy("TCZYXS")),
        "metadata": snap.metadata, "record_layout": snap.record_layout,
        "selection": snap.selection, "previous_selection": snap.previous_selection,
        "rois": [(row.name, row.selection, row.t, row.c, row.z, row.attributes)
                 for row in snap.rois],
        "overlays": snap.overlays,
        "spectrum": None if snap.spectrum is None else snap.spectrum.numpy().tobytes(),
        "display": (view.t, view.c, tuple(view.crosshair), view.orthogonal,
                    view.linked_zoom, view.zoom, tuple(view.plane_zoom),
                    tuple(tuple(row) for row in view.pan), view.revision, view.properties),
        "history": (history.undo_steps, history.redo_steps,
                    history.step_limit, history.pixel_budget),
    }


def unchanged(owner, before):
    after = state(owner)
    assert after.pop("snapshot").shares_data(before["snapshot"])
    assert after == {key: value for key, value in before.items() if key != "snapshot"}


def invoke(wrapper, app, owner, request, **controls):
    if wrapper == "export":
        return ij.export_axis_plane(owner.snapshot().dataset, request, **controls)
    if wrapper == "snapshot_prepare":
        return ij.prepare_axis_plane_export(owner.snapshot(), request, **controls)
    assert wrapper == "application_prepare"
    return app.prepare_axis_plane_export(request, owner.id, **controls)


@pytest.mark.parametrize("case_id", CASE_IDS)
def test_reference_integer_pixels_and_scalar_spacing(oracle, case_id):
    row = oracle[case_id]
    kind, plane, _ = case_id.split(".")
    pixels = reference_source(kind)
    assert typed_bytes(pixels).hex() == row["source_before"]["raw_hex"]
    assert row["source_before"]["raw_hex"] == row["source_after"]["raw_hex"]
    # Native world origins/time are separately owned; no ImageJ origin/time claim.
    spacing = (1.0, 1.0, 1.0) if kind in ("u8", "u16") else (0.4, 1.2, 3.0)
    source = ij.Dataset(pixels, axes="ZYXS" if kind == "rgb" else "ZYX",
                        calibration=ij.SpatialCalibration(spacing=spacing, unit="um"))
    before = typed_bytes(source.numpy("ZYXS" if kind == "rgb" else "ZYX"))
    request = ij.AxisPlaneExportRequest(plane, row["coordinate_zero_based"])
    result = ij.export_axis_plane(source, request)
    actual = result.dataset.numpy("YXS" if kind == "rgb" else "YX")
    expected = row["output"]
    samples = 3 if kind == "rgb" else 1
    assert result.dataset.shape == (1, 1, 1, expected["height"], expected["width"], samples)
    assert actual.dtype == pixels.dtype
    assert typed_bytes(actual).hex() == expected["raw_hex"]
    assert tuple(result.dataset.calibration.spacing) == tuple(expected["calibration"]["spacing"])
    assert result.dataset.calibration.unit == "um"
    assert result.dataset.sample_policy == source.sample_policy
    assert not result.dataset.shares_pixels(source)
    assert result.request.plane == plane and result.request.coordinate == request.coordinate
    assert result.request.t == result.request.c == 0
    actual[...] = 0  # Returned NumPy storage cannot mutate either immutable dataset.
    assert typed_bytes(result.dataset.numpy("YXS" if kind == "rgb" else "YX")).hex() == expected["raw_hex"]
    assert typed_bytes(source.numpy("ZYXS" if kind == "rgb" else "ZYX")) == before


@pytest.mark.parametrize("plane,coordinate,dtype,samples", [
    ("xy", 2, np.float64, 4), ("xz", 2, np.uint16, 1), ("yz", 3, np.float32, 3),
], ids=("xy-f64-rgba", "xz-u16-scientific", "yz-f32-rgb"))
def test_owned_scientific_channels_samples_full_basis_and_time(plane, coordinate, dtype, samples):
    pixels, source = scientific_source(dtype, samples)
    result = ij.export_axis_plane(source, ij.AxisPlaneExportRequest(plane, coordinate, t=1, c=1))
    expected = literal_slice(pixels[1, 1], plane, coordinate)
    actual = result.dataset.numpy("YXS")
    assert actual.dtype == expected.dtype and typed_bytes(actual) == typed_bytes(expected)
    assert result.dataset.shape == (1, 1, 1, expected.shape[0], expected.shape[1], samples)
    assert not result.dataset.shares_pixels(source)
    axes = AXES[plane]
    direction = ((-1.0, 0.5, 0.25), (0.0, 1.0, 0.5), (0.5, 0.0, 1.0))
    spacing = (0.25, 1.5, 3.0)
    original_origin = (-2.0, 4.0, 9.0)
    expected_origin = tuple(original_origin[r] + direction[r][axes[2]] *
                            spacing[axes[2]] * coordinate for r in range(3))
    output = result.dataset.calibration
    assert tuple(output.origin) == expected_origin
    assert tuple(output.spacing) == tuple(spacing[a] for a in axes)
    assert tuple(output.direction) == tuple(direction[r][a] for r in range(3) for a in axes)
    assert output.time_origin == -0.5 and output.frame_interval == 2.5
    assert output.time_unit == "min" and output.unit == "um"
    for v in range(expected.shape[0]):
        for u in range(expected.shape[1]):
            source_voxel = [0, 0, 0]
            source_voxel[axes[0]], source_voxel[axes[1]], source_voxel[axes[2]] = u, v, coordinate
            expected_world = tuple(original_origin[r] + sum(
                direction[r][a] * spacing[a] * source_voxel[a] for a in range(3))
                for r in range(3))
            assert tuple(output.voxel_to_world((u, v, 0))) == expected_world
    geometry = result.source_geometry
    assert tuple(geometry["origin"]) == expected_origin
    assert tuple(geometry["horizontal_step"]) == tuple(direction[r][axes[0]] * spacing[axes[0]] for r in range(3))
    assert tuple(geometry["vertical_step"]) == tuple(direction[r][axes[1]] * spacing[axes[1]] for r in range(3))
    assert (geometry["horizontal_axis"], geometry["vertical_axis"], geometry["unit"]) == (
        "XYZ"[axes[0]], "XYZ"[axes[1]], "um")
    output_bytes = expected.size * 8
    strip_bytes = 0 if plane == "xy" else expected.shape[1] * samples * 8
    assert result.working_bytes == output_bytes + strip_bytes + 4 * 5 * samples * 8
    assert typed_bytes(source.numpy("TCZYXS")) == typed_bytes(pixels)


def test_owned_large_origin_keeps_nonzero_small_basis_steps():
    _, source = scientific_source(origin=(1e100, -1e100, 1e100))
    result = ij.export_axis_plane(source, ij.AxisPlaneExportRequest("xz", 2, t=1, c=1))
    assert result.source_geometry["horizontal_step"] == [-0.25, 0.0, 0.125]
    assert result.source_geometry["vertical_step"] == [0.75, 1.5, 3.0]


@pytest.mark.parametrize("plane", ("xy", "xz", "yz"))
def test_owned_float32_quiet_nan_subnormal_signed_zero_bits(plane):
    literal_bits = np.array([0, 0x80000000, 1, 0x7FC12345, 0xFFC34567, 0x3F800000], dtype=np.uint32)
    bits = literal_bits[np.arange(60) % 6].reshape(3, 4, 5)
    policy = ij.FloatingSamplePolicy.PRESERVE_QUIET_NAN_FLOAT32
    source = ij.Dataset(bits.view(np.float32), axes="ZYX", sample_policy=policy)
    result = ij.export_axis_plane(source, ij.AxisPlaneExportRequest(plane, 2))
    expected = literal_slice(bits, plane, 2)
    assert result.dataset.sample_policy == policy
    actual = result.dataset.numpy("YX")
    assert actual.dtype == np.dtype(np.float32)
    assert np.array_equal(actual.view(np.uint32), expected)
    assert np.array_equal(source.numpy("ZYX").view(np.uint32), bits)


@pytest.mark.parametrize("wrapper", WRAPPERS)
def test_python_request_is_copied_before_callbacks(wrapper):
    pixels, source = scientific_source(np.uint16)
    owner = ij.Document(source, "Source")
    app = ij.Application()
    app.adopt(owner)
    before = state(owner)
    request = ij.AxisPlaneExportRequest("xy", 0, t=1, c=0)
    calls = []

    def mutate_original(done, total):
        calls.append((done, total))
        request.plane, request.coordinate, request.t, request.c = "xz", 2, 0, 1
        request.max_working_bytes = 0

    value = invoke(wrapper, app, owner, request, progress=mutate_original)
    assert calls and calls[0][0] == 0
    assert request.plane == "xz" and request.max_working_bytes == 0
    if wrapper == "export":
        output = value.dataset
        assert (value.request.plane, value.request.coordinate, value.request.t, value.request.c) == ("xy", 0, 1, 0)
        assert value.request.max_working_bytes == 512 * 1024 * 1024
    else:
        candidate = value.prepared_new_document()
        assert not app.contains(candidate.id)
        assert candidate.snapshot().metadata["title"] == "Source XY 1"
        published = app.publish(value)
        assert published.document is candidate and not value.active
        output = candidate.snapshot().dataset
    assert typed_bytes(output.numpy("YXS")) == typed_bytes(pixels[1, 0, 0])
    unchanged(owner, before)


@pytest.mark.parametrize("wrapper", WRAPPERS)
@pytest.mark.parametrize("phase", ("before", "positive_work"))
def test_cancellation_preserves_source_and_membership(wrapper, phase):
    _, source = scientific_source()
    owner = ij.Document(source)
    app = ij.Application()
    app.adopt(owner)
    before, ids, active = state(owner), app.document_ids, app.active_document_id
    token, calls = ij.CancellationToken(), []
    if phase == "before":
        token.cancel()

    def cancel_after_strip(done, total):
        calls.append((done, total))
        if done > 0:
            token.cancel()

    with pytest.raises(ij.OperationCancelled):
        invoke(wrapper, app, owner, ij.AxisPlaneExportRequest("xz", 2, t=1, c=1),
               cancellation=token, progress=cancel_after_strip)
    assert token.canceled
    assert not calls if phase == "before" else any(done > 0 for done, _ in calls)
    assert app.document_ids == ids and app.active_document_id == active
    unchanged(owner, before)


@pytest.mark.parametrize("plane,coordinate,t,c,cap,error", [
    ("xy", -1, 0, 0, 512, IndexError), ("xy", 3, 0, 0, 512, IndexError),
    ("xz", -1, 0, 0, 512, IndexError), ("xz", 4, 0, 0, 512, IndexError),
    ("yz", -1, 0, 0, 512, IndexError), ("yz", 5, 0, 0, 512, IndexError),
    ("xy", 0, -1, 0, 512, IndexError), ("xy", 0, 2, 0, 512, IndexError),
    ("xy", 0, 0, -1, 512, IndexError), ("xy", 0, 0, 2, 512, IndexError),
    ("xz", 2, 1, 1, 319, ValueError), ("xy", 0, 0, 0, 0, ValueError),
], ids=("xy-negative", "xy-end", "xz-negative", "xz-end", "yz-negative", "yz-end",
        "t-negative", "t-end", "c-negative", "c-end", "cap-below-estimate", "zero-cap"))
def test_invalid_bounds_or_cap_reject_before_progress(plane, coordinate, t, c, cap, error):
    pixels, source = scientific_source()
    calls = []
    with pytest.raises(error):
        ij.export_axis_plane(source, ij.AxisPlaneExportRequest(plane, coordinate, t=t, c=c,
                             max_working_bytes=cap), progress=lambda *args: calls.append(args))
    assert calls == []  # Admission precedes progress; reader count is a native test.
    assert typed_bytes(source.numpy("TCZYXS")) == typed_bytes(pixels)


def test_invalid_plane_constructor_rejects():
    with pytest.raises(ValueError, match="Plane must be"):
        ij.AxisPlaneExportRequest("oblique", 0)


@pytest.mark.parametrize("callback", ("progress", "checkpoint"))
def test_noncallable_control_rejects_before_preparation(callback):
    _, source = scientific_source()
    owner = ij.Document(source)
    before = state(owner)
    with pytest.raises(ValueError, match="callbacks must be callable"):
        ij.prepare_axis_plane_export(owner.snapshot(), **{callback: []})
    unchanged(owner, before)


@pytest.mark.parametrize("overflow", ("world", "time"))
def test_nonfinite_selected_calibration_rejects_before_progress(overflow):
    pixels = np.zeros((2, 1, 3, 4, 5), dtype=np.float64)
    calibration = (ij.SpatialCalibration(spacing=(1, 1, 1e308), origin=(0, 0, 1e308))
                   if overflow == "world" else
                   ij.SpatialCalibration(frame_interval=1e308, time_origin=1e308))
    source = ij.Dataset(pixels, axes="TCZYX", calibration=calibration)
    calls = []
    with pytest.raises(ValueError):
        ij.export_axis_plane(source, ij.AxisPlaneExportRequest("xy", 2, t=1),
                             progress=lambda *args: calls.append(args))
    assert calls == []
    assert typed_bytes(source.numpy("TCZYX")) == typed_bytes(pixels)


def annotated_owner():
    pixels, source = scientific_source()
    owner = ij.Document(source, "Source")
    edit = owner.begin_edit()
    edit.set_property("source-only", {"nested": [7, "preserved"]})
    edit.set_extension("opaque", {"bytes": [3, 1, 4]})
    edit.set_record_layout({"presence": {"title": True}})
    edit.select(ij.Selection("rectangle", [(1, 1), (4, 3)], attributes={"label": "selected"}))
    edit.set_previous_selection(ij.Selection("line", [(0, 0), (2, 2)]))
    edit.set_rois([ij.ROI("stored", ij.Selection("rectangle", [(1, 1), (3, 3)]),
                         t=1, c=0, z=2, attributes={"color": "red"})])
    edit.set_overlays([{"name": "source overlay", "points": [[1, 1], [2, 2]]}])
    edit.set_spectrum(ij.FourierState(np.ones((4, 5), dtype=np.complex128), original_shape=(4, 5)))
    owner.commit(edit)
    owner.mark_saved()
    edit = owner.begin_edit()
    edit.set_property("history", True)
    owner.commit(edit)
    owner.undo()
    display = owner.snapshot().display
    display.t, display.c, display.crosshair = 1, 1, (3, 2, 1)
    display.orthogonal, display.zoom, display.plane_zoom = True, 2.0, (2.0, 2.0, 2.0)
    display.properties = {"lut": "Ice"}
    owner.set_display(display)
    return pixels, owner


def test_prepared_exact_fresh_owner_publication_and_source_preservation():
    pixels, owner = annotated_owner()
    app = ij.Application()
    app.adopt(owner)
    before = state(owner)
    prepared = app.prepare_axis_plane_export(ij.AxisPlaneExportRequest("xz", 2, t=1, c=1), owner.id)
    unchanged(owner, before)
    candidate = prepared.prepared_new_document()
    assert candidate.id != owner.id and not app.contains(candidate.id)
    snap = candidate.snapshot()
    assert not snap.dirty and candidate.undo_steps == candidate.redo_steps == 0
    assert snap.metadata == {
        "title": "Source XZ 3", "properties": {},
        "extensions": {"axis_plane_export": {
            "schema_version": 1, "source_document_id": owner.id, "plane": "XZ",
            "coordinate": 2, "t": 1, "c": 1, "horizontal_axis": "X",
            "vertical_axis": "Z", "fixed_axis": "Y",
            "index_convention": "zero_based_voxel_centers",
        }},
    }
    assert snap.selection is snap.previous_selection is snap.spectrum is None
    assert snap.rois == snap.overlays == [] and snap.record_layout == {}
    np.testing.assert_array_equal(snap.dataset.numpy("YXS"), pixels[1, 1, :, 2, :])
    candidate.set_history_policy(3, 4096)
    display = snap.display
    display.zoom, display.plane_zoom = 4.0, (4.0, 4.0, 4.0)
    candidate.set_display(display)
    # Selection/display are independent of the captured pixels/metadata/request.
    owner.select(ij.Selection("point", [(2, 2)]))
    source_display = owner.snapshot().display
    source_display.t, source_display.c, source_display.crosshair = 0, 0, (1, 1, 0)
    source_display.zoom = 3.0
    owner.set_display(source_display)
    before = state(owner)
    other = ij.Document(ij.Dataset(np.zeros((2, 3), dtype=np.uint8), axes="YX"), "Other")
    app.adopt(other)
    other_before = state(other)
    published = app.publish(prepared, title="Source XZ 3")
    assert published.created and published.document is candidate
    assert published.document_id == candidate.id and app.active_document_id == candidate.id
    assert app.document_ids == [owner.id, other.id, candidate.id]
    assert candidate.snapshot().display.zoom == 4.0
    assert candidate.history().step_limit == 3 and candidate.history().pixel_budget == 4096
    assert candidate.undo_steps == candidate.redo_steps == 0 and not candidate.snapshot().dirty
    assert not prepared.active
    with pytest.raises(ValueError, match="already published"):
        app.publish(prepared)
    unchanged(owner, before)
    unchanged(other, other_before)


@pytest.mark.parametrize("reason", (
    "source_pixels", "source_metadata", "candidate_metadata", "candidate_selection",
    "candidate_reverted", "closed", "foreign", "title", "cancelled",
))
def test_rejected_publication_preserves_preparation_and_current_owners(reason):
    _, owner = annotated_owner()
    app = ij.Application()
    app.adopt(owner)
    token = ij.CancellationToken()
    prepared = app.prepare_axis_plane_export(ij.AxisPlaneExportRequest("xy", 1, t=1, c=1),
                                           owner.id, cancellation=token)
    candidate = prepared.prepared_new_document()
    publisher, expected_error, title = app, ij.RevisionConflict, None
    if reason == "source_pixels":
        edit = owner.begin_edit()
        edit.replace_plane(np.full((4, 5), 7.0), t=1, c=1, z=1)
        owner.commit(edit)
    elif reason == "source_metadata":
        edit = owner.begin_edit()
        edit.set_property("new metadata", True)
        owner.commit(edit)
    elif reason in ("candidate_metadata", "candidate_selection", "candidate_reverted"):
        candidate_baseline = candidate.snapshot()
        edit = candidate.begin_edit()
        if reason == "candidate_selection":
            edit.select(ij.Selection("point", [(1, 1)]))
        else:
            edit.set_property("modified output", True)
            if reason == "candidate_reverted":
                edit.replace_plane(np.full((4, 5), 7.0))
        candidate.commit(edit)
        if reason == "candidate_reverted":
            candidate.undo()
            # A positive one-byte obsolete-pixel budget removes the undone
            # pixel edit's redo entry without altering the restored Data.
            candidate.set_history_policy(1, 1)
            reverted = candidate.snapshot()
            assert reverted.shares_data(candidate_baseline)
            assert reverted.data_identity == candidate_baseline.data_identity
            assert not reverted.dirty and candidate.undo_steps == candidate.redo_steps == 0
            assert revisions(reverted) != revisions(candidate_baseline)
    elif reason == "closed":
        app.close(owner.id, discard=True)
        expected_error = ij.DocumentUnavailable
    elif reason == "foreign":
        publisher = ij.Application()
        publisher.create(ij.Dataset(np.zeros((2, 3), dtype=np.uint8), axes="YX"))
        expected_error = ij.DocumentUnavailable
    elif reason == "title":
        title, expected_error = "Different title", ValueError
    else:
        assert reason == "cancelled"
        token.cancel()
        expected_error = ij.OperationCancelled
    owner_before, candidate_before = state(owner), state(candidate)
    ids, active = app.document_ids, app.active_document_id
    publisher_ids, publisher_active = publisher.document_ids, publisher.active_document_id
    with pytest.raises(expected_error):
        publisher.publish(prepared, title=title)
    assert prepared.active and not app.contains(candidate.id)
    assert app.document_ids == ids and app.active_document_id == active
    assert publisher.document_ids == publisher_ids and publisher.active_document_id == publisher_active
    unchanged(owner, owner_before)
    unchanged(candidate, candidate_before)
