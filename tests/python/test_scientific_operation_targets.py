"""Installed typed scientific Gaussian editor API, with independent targets/pixels.

The literal step-response fixtures exercise owner routing and every native axis.
Configured upstream capture002 supplies the separate full/scaled/large-sigma
numeric checks. Neither product previews nor serializers supply expected pixels.
"""

import base64
import copy
import hashlib
import json
import os
from pathlib import Path
import struct
import zlib

import imagejxx as ij
import numpy as np
import pytest


REFERENCE_SHA = "8885c41ded23f51e56b1c42e28245bf4b9320108dacfa09da631e14fbe9bf762"
SHAPE = (2, 3, 4, 17, 23)
CALIBRATION = {
    "spacing": [0.5, 2.0, 3.0], "origin": [11.0, -7.0, 5.0],
    "direction": [0.0, -1.0, 0.0, 1.0, 0.0, 0.0, 0.0, 0.0, 1.0],
    "unit": "um", "frame_interval": 0.25, "time_origin": 8.0,
    "time_unit": "min",
}
CAMERAS = {
    "crosshair": [9, 6, 2], "zoom": 1.25, "linked_zoom": False,
    "plane_zoom": [1.25, 2.0, 3.0], "pan": [[3.0, -2.0], [1.0, 4.0], [-5.0, 2.0]],
}


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


def _fixture(dtype=np.uint8, samples=1):
    # Sigma-1 nearest-edge step response rounded to integer values. The
    # transition lies far from either edge; the omitted tails round to zero.
    # At x=9..12 the exact mathematical response is 5.8558,30.0529,
    # 69.9471,94.1442. Every TCZ/S receives a distinct constant offset.
    row = np.asarray([0] * 11 + [100] * 12, dtype=dtype)
    blurred = np.asarray([0] * 9 + [6, 30, 70, 94] + [100] * 10, dtype=dtype)
    shape = SHAPE + ((samples,) if samples > 1 else ())
    source, expected = np.empty(shape, dtype), np.empty(shape, dtype)
    for t in range(2):
        for c in range(3):
            for z in range(4):
                offset = 48 * t + 16 * c + 4 * z
                if samples == 1:
                    source[t, c, z] = row + offset
                    expected[t, c, z] = blurred + offset
                else:
                    for s in range(samples):
                        source[t, c, z, ..., s] = row + offset + 20 * s
                        expected[t, c, z, ..., s] = blurred + offset + 20 * s
    return source, expected


def _owner(editor, pixels=None, *, calibration=None, setup=None):
    if pixels is None:
        pixels, _ = _fixture()
    axes = "TCZYXS" if pixels.ndim == 6 else "TCZYX"
    owner = ij.Document(ij.Dataset(
        pixels, axes=axes,
        calibration=ij.SpatialCalibration(**(calibration or CALIBRATION))),
        "Scientific operation targets")
    tx = owner.begin_edit()
    tx.set_property("specimen", {"id": "owned literal", "nested": [None, True, 7]})
    tx.set_extension("future_annotation", {"version": 3, "retained": [1, 2]})
    if setup is not None:
        setup(tx)
    owner.commit(tx)
    owner.mark_saved()
    view = owner.snapshot().display
    view.t, view.c, view.crosshair = 1, 2, CAMERAS["crosshair"]
    view.zoom, view.linked_zoom = CAMERAS["zoom"], CAMERAS["linked_zoom"]
    view.plane_zoom, view.pan = CAMERAS["plane_zoom"], CAMERAS["pan"]
    view.orthogonal = True
    owner.set_display(view)
    editor.add_native_document(owner)
    return owner


def _all(roi=ij.RoiPolicy.WHOLE_PLANE):
    return ij.TargetRequest(ij.TargetScope.RANGES,
                            t=ij.AxisRange(0, 2), c=ij.AxisRange(0, 3),
                            z=ij.AxisRange(0, 4), roi=roi)


def _positions(target):
    return [(p.t, p.c, p.z) for p in
            (target.position(i) for i in range(target.position_count))]


def _pixels(owner):
    return owner.snapshot().dataset.numpy("TCZYXS" if owner.snapshot().dataset.shape[5] > 1 else "TCZYX")


def _unchanged(owner, before, history, workflow, editor):
    now = owner.snapshot()
    assert now.shares_data(before)
    assert now.revisions.matches(before.revisions, ij.Change.ALL)
    assert now.dirty == before.dirty and owner.undo_steps == history
    assert editor.workflow == workflow and editor.results == []


def _calibration(snapshot):
    value = snapshot.dataset.calibration
    return {key: getattr(value, key) for key in CALIBRATION}


@pytest.mark.parametrize("selection", ["current", "mixed", "all", "positions"])
@pytest.mark.parametrize("dtype,samples", [(np.uint8, 1), (np.uint16, 1), (np.uint8, 3)])
def test_typed_current_range_all_and_ordered_positions(editor, selection, dtype, samples):
    pixels, blurred = _fixture(dtype, samples)
    owner = _owner(editor, pixels)
    before, history = owner.snapshot(), owner.undo_steps
    if selection == "current":
        request = ij.TargetRequest(roi=ij.RoiPolicy.WHOLE_PLANE)
        selected = [(1, 2, 2)]
    elif selection == "mixed":
        request = ij.TargetRequest(ij.TargetScope.RANGES,
                                  t=ij.AxisRange(0, 2), c=ij.AxisRange(0, 3, 2),
                                  z=ij.AxisRange(1, 4, 2), roi=ij.RoiPolicy.WHOLE_PLANE)
        selected = [(t, c, z) for t in (0, 1) for c in (0, 2) for z in (1, 3)]
    elif selection == "all":
        request, selected = _all(), [(t, c, z) for t in range(2) for c in range(3) for z in range(4)]
    else:
        selected = [(1, 2, 3), (0, 0, 0), (1, 1, 2)]
        request = ij.TargetRequest(ij.TargetScope.POSITIONS,
                                  positions=[ij.PlanePosition(*p) for p in selected],
                                  roi=ij.RoiPolicy.WHOLE_PLANE)
    editor.recording = True
    editor.apply_gaussian(request, ij.GaussianBlurCommandOptions(1), document_id=owner.id)
    # Navigation is display-only. It cannot recompute the accepted target.
    editor.live_document().set_view(t=0, c=1, crosshair=[4, 5, 0], zoom=2.5)
    editor.wait()
    expected = pixels.copy()
    for p in selected:
        expected[p] = blurred[p]
    np.testing.assert_array_equal(_pixels(owner), expected)
    np.testing.assert_array_equal(before.dataset.numpy("TCZYXS" if samples > 1 else "TCZYX"), pixels)
    assert owner.undo_steps == history + 1 and owner.snapshot().dirty
    assert _calibration(owner.snapshot()) == CALIBRATION
    assert owner.snapshot().metadata == before.metadata
    current_view = owner.snapshot().display
    assert (current_view.t, current_view.c, current_view.crosshair) == (0, 1, [4, 5, 0])
    assert current_view.zoom == 2.5 and current_view.pan == CAMERAS["pan"]
    record, = editor.workflow
    assert record["operation"] == "gaussian_blur_imagej" and record["schema_version"] == 1
    assert record["options"] == {"sigma": 1.0, "scaled_units": False}
    assert record["calibration"] == CALIBRATION and record["effective_sigma_xy"] == [1.0, 1.0]
    target = record["target"]
    assert target["document_id"] == owner.id and target["source_shape"] == list(SHAPE) + [samples]
    assert target["source_revisions"] == [before.revisions.pixels, before.revisions.metadata,
                                           before.revisions.selection]
    decoded = ij.gaussian_command_from_record(before, record)
    assert _positions(decoded.target) == selected
    editor.action("undo", document_id=owner.id)
    np.testing.assert_array_equal(_pixels(owner), pixels)
    assert owner.undo_steps == history and not owner.snapshot().dirty
    assert owner.snapshot().display.zoom == 2.5
    editor.action("redo", document_id=owner.id)
    np.testing.assert_array_equal(_pixels(owner), expected)
    assert owner.undo_steps == history + 1


@pytest.mark.parametrize("extents", [(1, 3, 1), (2, 1, 4), (1, 1, 1)])
def test_singleton_axes_remain_explicit_for_all_targets(editor, extents):
    pixels, blurred = _fixture()
    nt, nc, nz = extents
    pixels, blurred = pixels[:nt, :nc, :nz].copy(), blurred[:nt, :nc, :nz].copy()
    owner = ij.Document(ij.Dataset(pixels, axes="TCZYX"), "Explicit singleton axes")
    editor.add_native_document(owner)
    request = ij.TargetRequest(ij.TargetScope.RANGES, t=ij.AxisRange(0, nt),
                              c=ij.AxisRange(0, nc), z=ij.AxisRange(0, nz),
                              roi=ij.RoiPolicy.WHOLE_PLANE)
    editor.apply_gaussian(request, ij.GaussianBlurCommandOptions(1), wait=True)
    assert owner.snapshot().dataset.shape == (*extents, 17, 23, 1)
    np.testing.assert_array_equal(_pixels(owner), blurred)
    assert owner.undo_steps == 1
    editor.action("undo")
    np.testing.assert_array_equal(_pixels(owner), pixels)


@pytest.mark.parametrize("policy", ["active", "positioned", "unmatched"])
def test_native_roi_consumption_union_zero_positions_holes_and_no_match(editor, policy):
    pixels, blurred = _fixture()
    active = np.zeros((17, 23), np.uint8)
    active[2:15, 7:17] = 255
    active[6:10, 10:13] = 0

    def setup(tx):
        tx.select(ij.Selection.from_mask(active))
        tx.set_rois([
            ij.ROI("explicit zero", ij.Selection("rectangle", [(7, 2), (12, 8)]), t=0, c=0, z=0),
            ij.ROI("overlap wildcard Z", ij.Selection("rectangle", [(10, 5), (17, 15)]), t=0, c=0),
            ij.ROI("positioned hole", ij.Selection.from_mask(active), t=1, c=2, z=2),
        ])

    owner = _owner(editor, pixels, setup=setup)
    history, before = owner.undo_steps, owner.snapshot()
    selected = [(0, 0, 0), (1, 2, 2), (1, 1, 3)] if policy != "unmatched" else [(1, 1, 3)]
    request = ij.TargetRequest(
        ij.TargetScope.POSITIONS, positions=[ij.PlanePosition(*p) for p in selected],
        roi=ij.RoiPolicy.ACTIVE_SELECTION if policy == "active" else ij.RoiPolicy.POSITIONED_ROIS)
    expected = pixels.copy()
    for p in selected:
        mask = np.zeros((17, 23), bool)
        if policy == "active" or p == (1, 2, 2):
            mask = active != 0
        elif p == (0, 0, 0):
            mask[2:8, 7:12] = mask[5:15, 10:17] = True
        expected[p][mask] = blurred[p][mask]
    editor.apply_gaussian(request, ij.GaussianBlurCommandOptions(1), wait=True)
    np.testing.assert_array_equal(_pixels(owner), expected)
    assert [(r.name, r.t, r.c, r.z, r.selection, r.attributes) for r in owner.snapshot().rois] == [
        (r.name, r.t, r.c, r.z, r.selection, r.attributes) for r in before.rois]
    assert owner.snapshot().selection == before.selection
    assert owner.undo_steps == history + (policy != "unmatched")
    assert owner.snapshot().dirty == (policy != "unmatched")
    if policy == "unmatched":
        assert owner.snapshot().shares_data(before)


@pytest.mark.parametrize("kind", ["sigma_zero", "empty_mask", "missing_active", "nonarea"])
def test_noop_recording_and_native_active_selection_rules(editor, kind):
    pixels, blurred = _fixture()

    def setup(tx):
        if kind == "empty_mask":
            tx.select(ij.Selection.from_mask(np.zeros((17, 23), np.uint8)))
        elif kind == "nonarea":
            tx.select(ij.Selection("line", [(2, 3), (19, 11)]))

    owner = _owner(editor, pixels, setup=setup)
    before, history = owner.snapshot(), owner.undo_steps
    editor.recording = True
    editor.apply_gaussian(options=ij.GaussianBlurCommandOptions(0 if kind == "sigma_zero" else 1), wait=True)
    expected = pixels.copy()
    changed = kind in {"missing_active", "nonarea"}
    if changed:
        expected[1, 2, 2] = blurred[1, 2, 2]
    np.testing.assert_array_equal(_pixels(owner), expected)
    assert owner.undo_steps == history + changed and owner.snapshot().dirty == changed
    assert len(editor.workflow) == 1
    assert editor.workflow[0]["target"]["roi_policy"] == "active_selection"
    if not changed:
        assert owner.snapshot().shares_data(before)


@pytest.mark.parametrize("invalid", ["bounds", "zero_step", "reversed", "duplicate", "empty", "volume", "new_document"])
def test_invalid_targets_refuse_before_lane_or_publication(editor, invalid):
    owner = _owner(editor)
    before, history = owner.snapshot(), owner.undo_steps
    request = _all()
    if invalid == "bounds":
        request.c = ij.AxisRange(0, 4)
    elif invalid == "zero_step":
        request.z = ij.AxisRange(0, 4, 0)
    elif invalid == "reversed":
        request.t = ij.AxisRange(1, 0)
    elif invalid in {"duplicate", "empty"}:
        request = ij.TargetRequest(ij.TargetScope.POSITIONS,
                                  positions=[ij.PlanePosition(), ij.PlanePosition()] if invalid == "duplicate" else [])
    elif invalid == "volume":
        request.mode = ij.ExecutionMode.VOLUME
    else:
        request.destination = ij.OutputDestination.NEW_DOCUMENT
    with pytest.raises((ValueError, IndexError)):
        editor.apply_gaussian(request, ij.GaussianBlurCommandOptions(1))
    assert not editor.busy and editor.job_info is None
    _unchanged(owner, before, history, [], editor)


@pytest.mark.parametrize("dtype,samples", [(np.float64, 1), (np.uint8, 4), (np.uint16, 3), (np.float32, 3)])
def test_unsupported_type_or_sample_layout_is_atomic(editor, dtype, samples):
    shape = SHAPE + ((samples,) if samples > 1 else ())
    owner = _owner(editor, np.zeros(shape, dtype))
    before, history = owner.snapshot(), owner.undo_steps
    with pytest.raises(ValueError, match="supports|sample|Gaussian"):
        editor.apply_gaussian(_all(), ij.GaussianBlurCommandOptions(1))
    _unchanged(owner, before, history, [], editor)
    assert not editor.busy


def test_foreign_and_inactive_owner_ids_do_not_change_active_owner(editor):
    first = _owner(editor)
    second = _owner(editor)
    active = editor.active_index
    first_before, second_before = first.snapshot(), second.snapshot()
    outsider = ij.Document(ij.Dataset(_fixture()[0], axes="TCZYX"))
    for document_id in (first.id, outsider.id, 2**63):
        with pytest.raises((ValueError, IndexError, ij.DocumentUnavailable)):
            editor.apply_gaussian(_all(), ij.GaussianBlurCommandOptions(1), document_id=document_id)
        assert editor.active_index == active and not editor.busy
        assert first.snapshot().shares_data(first_before) and second.snapshot().shares_data(second_before)
        assert editor.workflow == [] and editor.results == []


def test_explicit_inactive_history_preserves_active_scientific_preview(editor):
    pixels, blurred = _fixture()
    first = _owner(editor, pixels)
    editor.apply_gaussian(options=ij.GaussianBlurCommandOptions(1), wait=True)
    first_history = first.undo_steps
    first_view = first.snapshot().display
    expected_first = pixels.copy()
    expected_first[1, 2, 2] = blurred[1, 2, 2]
    second = _owner(editor, pixels)
    editor.live_document(second.id).set_view(orthogonal=False)
    active_before = second.snapshot()
    active_history, active_index = second.undo_steps, editor.active_index
    editor.action("show_operation", operation="gaussian_blur_imagej", document_id=second.id)
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
    installed = editor.render_headless(frames=2)
    baseline_lane = installed["gaussian_jobs"]
    assert baseline_lane["preview"]["exit"] == "installed"
    assert baseline_lane["preview"]["source_id"] == second.id
    assert baseline_lane["active_installed_generation"] is not None
    assert installed["ordinary_pixels"]["pixels_source"] == "preview"
    workflow = copy.deepcopy(editor.workflow)
    for action, expected, history in (("undo", pixels, first_history - 1),
                                      ("redo", expected_first, first_history)):
        editor.action(action, document_id=first.id)
        observed = editor.render_headless(frames=2)
        np.testing.assert_array_equal(_pixels(first), expected)
        assert first.undo_steps == history
        assert (first.snapshot().display.t, first.snapshot().display.c,
                first.snapshot().display.crosshair) == (first_view.t, first_view.c, first_view.crosshair)
        assert editor.active_index == active_index and editor.live_document().id == second.id
        assert second.snapshot().shares_data(active_before) and second.undo_steps == active_history
        assert second.snapshot().revisions.matches(active_before.revisions, ij.Change.ALL)
        lane = observed["gaussian_jobs"]
        assert lane["active_installed_generation"] == baseline_lane["active_installed_generation"]
        assert lane["preview"]["job"]["id"] == baseline_lane["preview"]["job"]["id"]
        assert lane["preview"]["target"] == baseline_lane["preview"]["target"]
        assert lane["preview"]["exit"] == "installed"
        assert observed["ordinary_pixels"]["pixels_source"] == "preview"
        assert observed["ordinary_pixels"]["current_tcz"] == [1, 2, 2]
        assert editor.workflow == workflow and editor.results == []
    editor.cancel()
    editor.poll()


def test_full_record_strict_replay_rebind_and_native_session(editor, tmp_path):
    pixels, blurred = _fixture()
    owner = _owner(editor, pixels)
    original, history = owner.snapshot(), owner.undo_steps
    request = ij.TargetRequest(ij.TargetScope.RANGES, t=ij.AxisRange(0, 2),
                              c=ij.AxisRange(1, 3), z=ij.AxisRange(0, 4, 2),
                              roi=ij.RoiPolicy.WHOLE_PLANE)
    selected = [(t, c, z) for t in (0, 1) for c in (1, 2) for z in (0, 2)]
    expected = pixels.copy()
    for p in selected:
        expected[p] = blurred[p]
    editor.recording = True
    editor.apply_gaussian(request, ij.GaussianBlurCommandOptions(1), wait=True)
    record = copy.deepcopy(editor.workflow[0])
    assert record["target"]["ranges"] == [[0, 2, 1], [1, 3, 1], [0, 4, 2]]
    editor.live_document().set_view(t=0, c=0, crosshair=[2, 4, 1])
    assert _positions(ij.gaussian_command_from_record(original.with_display(owner.snapshot().display), record).target) == selected
    with pytest.raises(ij.RevisionConflict):
        editor.action("gaussian_replay", record=record)
    assert owner.undo_steps == history + 1 and len(editor.workflow) == 1
    # A complete malformed original must fail even with an explicit destination.
    malformed = copy.deepcopy(record)
    malformed["effective_sigma_xy"] = [999, 1]
    with pytest.raises(ValueError):
        editor.action("gaussian_replay", record=malformed,
                      gaussian_rebind={"scope": "current_plane", "roi": "whole_plane",
                                       "mode": "planes", "destination": "in_place"})
    assert len(editor.workflow) == 1
    path = tmp_path / "scientific-targets.ijx"
    editor.action("save_document", path=str(path))
    assert not owner.snapshot().dirty
    raw = path.read_bytes()
    assert len(raw) <= 32 * 1024 * 1024 and raw[:10] == b"IMAGEJXX3\n"
    size, = struct.unpack("<Q", raw[10:18])
    assert 0 < size <= 32 * 1024 * 1024
    decoder = zlib.decompressobj()
    payload = decoder.decompress(raw[18:], size + 1)
    assert len(payload) == size and decoder.eof and not decoder.unused_data
    session = json.loads(payload)
    document, = session["documents"]
    assert document["axes"] == "TCZYXS" and document["shape"] == list(SHAPE) + [1]
    stored = np.asarray([np.frombuffer(base64.b64decode(value, validate=True), dtype="<f8")
                         for value in document["planes"]]).reshape(SHAPE)
    np.testing.assert_array_equal(stored, expected)
    assert document["calibration"] == CALIBRATION
    assert session["workflow"] == [record]
    editor.open_file(path)
    reopened = editor.live_document().native_owner
    assert reopened.id != owner.id and reopened.undo_steps == 0 and not reopened.snapshot().dirty
    np.testing.assert_array_equal(_pixels(reopened), expected)
    with pytest.raises((ValueError, ij.RevisionConflict)):
        editor.action("gaussian_replay", record=record)
    editor.recording = False
    # Rebind deliberately selects a formerly untouched plane. It never rewrites
    # the old source identity or silently reuses the old full Apply ranges.
    editor.action("gaussian_replay", record=record,
                  gaussian_rebind={"scope": "positions", "positions": [[0, 0, 1]],
                                   "roi": "whole_plane", "mode": "planes", "destination": "in_place"})
    editor.wait()
    rebound_expected = expected.copy()
    rebound_expected[0, 0, 1] = blurred[0, 0, 1]
    np.testing.assert_array_equal(_pixels(reopened), rebound_expected)
    assert reopened.undo_steps == 1
    assert record["target"]["document_id"] == owner.id


@pytest.fixture(scope="module")
def pinned_reference():
    path = os.environ.get("IMAGEJXX_GAUSSIAN_REFERENCE_JSONL")
    required = os.environ.get("IMAGEJXX_GAUSSIAN_REQUIRE_REFERENCE", "0")
    assert required in {"0", "1"}
    if not path and required != "1":
        pytest.skip("Configure the pinned Gaussian reference for numerical qualification")
    assert path and os.environ.get("IMAGEJXX_GAUSSIAN_REFERENCE_SHA256") == REFERENCE_SHA
    raw = Path(path).resolve(strict=True).read_bytes()
    assert hashlib.sha256(raw).hexdigest() == REFERENCE_SHA
    rows = [json.loads(line) for line in raw.splitlines() if line]
    assert len(rows) == 332 and len({row["id"] for row in rows}) == 332
    return {row["id"]: row for row in rows}


@pytest.mark.parametrize("case_id", ["u8-full-s2", "u16-full-s2", "f32-full-s2", "rgb-full-s2",
                                     "u8-full-s12", "u8-scaled-s2-px0p5-py2-run-scaled-harness"])
def test_native_lane_matches_pinned_full_scaled_and_large_sigma(editor, pinned_reference, case_id):
    row = pinned_reference[case_id]
    dtype = {"U8": np.dtype("u1"), "U16": np.dtype("<u2"),
             "F32": np.dtype("<f4"), "RGB": np.dtype("u1")}[row["dtype"]]
    shape = tuple(row["shape"])
    assert shape[:2] == (17, 23) and row["raw_reference_output_unmodified"] is True
    source = np.frombuffer(bytes.fromhex(row["input_raw_hex"]), dtype=dtype).reshape(shape)
    expected_plane = np.frombuffer(bytes.fromhex(row["output_raw_hex"]), dtype=dtype).reshape(shape)
    recipe = []
    for y in range(17):
        for x in range(23):
            base = (37*x + 61*y + 13*x*y + 19*(x % 3) + 23*(y % 5)) % 256
            if row["dtype"] == "U8":
                recipe.append((base + (47 if x == 0 else 0) + (83 if y == 16 else 0)) % 256)
            elif row["dtype"] == "U16":
                recipe.append((257*base + 97*x + 211*y + 331*(x % 7)) % 65536)
            elif row["dtype"] == "F32":
                recipe.append((base - 127)*0.125 + (x - 2*y)*0.0625 + ((x*y) % 17)*0.00390625)
            else:
                recipe.extend(((base + 3*x) % 256, (5*base + 11*y + 17) % 256,
                               (255 - base + 7*x + 19*y) % 256))
    assert source.tobytes() == np.asarray(recipe, dtype=dtype).reshape(shape).tobytes()
    # Every untouched TCZ plane has its own recipe, independently distinguishable
    # from both the selected source and expected reference output.
    pixels = np.empty(SHAPE + shape[2:], dtype)
    for t in range(2):
        for c in range(3):
            for z in range(4):
                pixels[t, c, z] = t * 48 + c * 16 + z * 4
    pixels[1, 2, 2] = source
    expected = pixels.copy()
    expected[1, 2, 2] = expected_plane
    calibration = copy.deepcopy(CALIBRATION)
    calibration["spacing"][:2] = row["pixel_spacing_xy"]
    calibration["unit"] = row["calibration_unit"]
    owner = _owner(editor, pixels, calibration=calibration)
    if case_id == "u8-full-s2":
        # Omitted request/options exercise the public defaults, including the
        # no-active-selection whole-plane rule. Sigma defaults to two pixels.
        editor.apply_gaussian(wait=True)
    else:
        editor.apply_gaussian(ij.TargetRequest(roi=ij.RoiPolicy.WHOLE_PLANE),
                              ij.GaussianBlurCommandOptions(row["scalar_sigma"], row["scaled_units"]), wait=True)
    actual = _pixels(owner)
    assert actual.dtype == expected.dtype and actual.tobytes() == expected.tobytes()
