"""Exact pinned path-filter words across scientific target/publication scopes.

Each TCZ plane is a byte-for-byte clone of one Java input.  The numerical
oracle is the corresponding retained Java output, with original words outside
the independently projected Java mask.  Literal TCZ membership and ordered
output positions detect routing errors even for a numerically unchanged plane.

This file complements the exhaustive per-shape/option reference tests.  Its
canonical rank rows cover every admitted operation/type/sample-policy triple.
Supplemental typed Gaussian and WholePlane catalogs supply their own literal
words.  Quiet-NaN metadata cases reuse finite Float32 words without sample
conversion; IEEE Gaussian cases require refusal before provider reads/progress.
"""

import copy
import gzip
import itertools
import json
from pathlib import Path

import imagejxx as ij
import numpy as np
import pytest


FIXTURES = Path(__file__).resolve().parents[1] / "fixtures"
TCZ = (2, 3, 4)
CURRENT = (1, 2, 3)
NAVIGATED = (0, 1, 0)
SCOPES = ("current", "all-z", "stepped-tcz", "ordered-positions")
ROI_POLICIES = ("active", "positioned")
DESTINATIONS = ("in-place", "new-document")
SCOPE_POSITIONS = {
    "current": ((1, 2, 3),),
    "all-z": ((1, 2, 0), (1, 2, 1), (1, 2, 2), (1, 2, 3)),
    "stepped-tcz": ((0, 0, 1), (0, 0, 3), (0, 2, 1), (0, 2, 3)),
    "ordered-positions": ((1, 2, 3), (0, 0, 1), (1, 0, 0), (0, 1, 2)),
}
# These sets describe the authored ROI positions, not a product target query.
SAVED_MATCHES = frozenset((1, c, z) for c in range(3) for z in range(4)) | {(0, 0, 1)}
SUBSET_PATH = (0, 3, 2, 1, 5, 2, 1, 5, 3, 1, 3, 3, 4)


def _fixture(name):
    return json.loads(gzip.decompress((FIXTURES / name).read_bytes()))


RANK_FIXTURE = _fixture("path-rank-targets-imagej154r.json.gz")
GAUSSIAN_FIXTURE = _fixture("path-filter-targets-imagej154r.json.gz")
TYPED_GAUSSIAN_FIXTURE = _fixture("path-typed-gaussian-targets-imagej154r.json.gz")
RANK_SCOPE_FIXTURE = _fixture("path-rank-scope-targets-imagej154r.json.gz")


def _rank_rows():
    result = {}
    for row in RANK_FIXTURE["cases"]:
        request = row["request"]
        if not row["id"].endswith("-triangle"):
            continue
        key = (request["operation"], request["pixel_type"], request["input_pattern"])
        result.setdefault(key, row)
    required = {
        (operation, dtype, "finite")
        for operation in ("minimum", "maximum", "mean", "median", "variance",
                          "despeckle", "outliers", "top_hat")
        for dtype in ("u8", "u16", "f32")
    } | {("remove_nans", "f32", policy) for policy in ("finite", "quiet_nan", "ieee")}
    assert set(result) == required
    return tuple(("rank", result[key]) for key in sorted(result))


RANK_CASES = _rank_rows()
GAUSSIAN_CASES = tuple(
    ("gaussian", row) for row in GAUSSIAN_FIXTURE["cases"]
    if row["id"] in ("fractional-triangle-sigma1", "fractional-triangle-sigma12")
)
assert len(GAUSSIAN_CASES) == 2
CASES = RANK_CASES + GAUSSIAN_CASES


def _typed_gaussian_rows(shape):
    rows = tuple(row for row in TYPED_GAUSSIAN_FIXTURE["cases"]
                 if row["id"].endswith("-" + shape))
    assert len(rows) == 16
    assert {(row["request"]["pixel_type"], row["request"]["sigma_x"],
             row["request"]["sigma_y"]) for row in rows} == {
                 (dtype, sx, sy) for dtype in ("u8", "u16", "f32", "rgb")
                 for sx, sy in ((1, 1), (12, 12), (2, 1), (0, 0))}
    return (tuple(("gaussian-typed", row) for row in rows)
            + tuple(("gaussian-typed-quiet", row) for row in rows
                    if row["request"]["pixel_type"] == "f32"))


TYPED_TRIANGLE_CASES = _typed_gaussian_rows("triangle")
WHOLE_PLANE_CASES = (tuple(("rank", row) for row in RANK_SCOPE_FIXTURE["cases"])
                    + _typed_gaussian_rows("covers-canvas"))
SUPPLEMENT_CASES = (tuple((case, roi) for case in TYPED_TRIANGLE_CASES for roi in ROI_POLICIES)
                    + tuple((case, "whole-plane") for case in WHOLE_PLANE_CASES))
IEEE_GAUSSIAN_CASES = tuple(case for case in TYPED_TRIANGLE_CASES
                          if case[0] == "gaussian-typed" and case[1]["request"]["pixel_type"] == "f32")


def _is_gaussian(case):
    return case[0].startswith("gaussian")


def _case_id(case):
    family, row = case
    request = row["request"]
    if family == "gaussian":
        return f"gaussian-f32-finite-sigma{request['sigma']}"
    if family.startswith("gaussian-typed"):
        policy = "quiet_nan" if family.endswith("-quiet") else "finite"
        return (f"typed-gaussian-{request['pixel_type']}-{policy}-"
                f"xy{request['sigma_x']}x{request['sigma_y']}")
    return "-".join((request["operation"], request["pixel_type"], request["input_pattern"]))


CASE_IDS = tuple(map(_case_id, CASES))
assert len(CASE_IDS) == len(set(CASE_IDS)) == 29
TYPED_TRIANGLE_IDS = tuple(map(_case_id, TYPED_TRIANGLE_CASES))
SUPPLEMENT_IDS = tuple(f"{roi}-{_case_id(case)}" for case, roi in SUPPLEMENT_CASES)
assert len(SUPPLEMENT_IDS) == len(set(SUPPLEMENT_IDS)) == 87


def _words(value, width, height, dtype):
    return np.array([int(word, 16) for word in value], dtype=dtype).reshape(height, width)


def _typed_words(value, width, height, pixel_type):
    dtype = {"u8": np.uint8, "u16": np.uint16, "f32": np.uint32, "rgb": np.uint32}[pixel_type]
    words = _words(value, width, height, dtype)
    if pixel_type == "rgb":
        # The observer serializes RRGGBB, while the public array has an S axis.
        # Unpack the observed bytes; no float conversion or filter is involved.
        return np.stack(tuple(((words >> shift) & 255).astype(np.uint8)
                              for shift in (16, 8, 0)), axis=-1)
    return words


def _reference(case):
    """Decode literal words and project a reference mask without native calls."""
    family, row = case
    request, observed = row["request"], row["observation"]
    width, height = request["width"], request["height"]
    if family == "gaussian":
        original = _words(observed["input"]["f32_words"], width, height, np.uint32)
        filtered = _words(observed["gaussian"]["after_mask_reset_f32_words"],
                          width, height, np.uint32)
        sample_dtype, policy = np.float32, "finite"
    else:
        pixel_type = request["pixel_type"]
        original = _typed_words(observed["input"]["sample_words"], width, height, pixel_type)
        filtered = _typed_words(observed["after_mask_reset"]["sample_words"],
                                width, height, pixel_type)
        sample_dtype = {"u8": np.uint8, "u16": np.uint16, "f32": np.float32, "rgb": np.uint8}[pixel_type]
        policy = ("quiet_nan" if family.endswith("-quiet") else
                  request.get("input_pattern", "finite"))
    mask = np.zeros((height, width), dtype=np.uint8)
    roi_mask = observed["roi_mask"]
    assert roi_mask is not None
    x0, y0 = roi_mask["image_origin"]
    mask_width, mask_height = roi_mask["dimensions"]
    raw = bytes.fromhex(roi_mask["u8_hex"])
    assert len(raw) == mask_width * mask_height
    for y in range(max(0, y0), min(height, y0 + mask_height)):
        for x in range(max(0, x0), min(width, x0 + mask_width)):
            mask[y, x] = raw[(y - y0) * mask_width + x - x0]
    # The additional saved contour has literal occupied cells (3,2), (4,2).
    # Both belong to the observed triangle; its union must leave that oracle
    # unchanged, while still exercising a non-identical overlapping contour.
    assert mask[2, 3] == mask[2, 4] == 255
    assert np.count_nonzero(mask) > 2
    expected = original.copy()
    expected[mask != 0] = filtered[mask != 0]
    pixels = original.view(sample_dtype).copy()
    return pixels, original, expected, mask, policy


def _policy(name):
    return {"finite": ij.FloatingSamplePolicy.FINITE_ONLY,
            "quiet_nan": ij.FloatingSamplePolicy.PRESERVE_QUIET_NAN_FLOAT32,
            "ieee": ij.FloatingSamplePolicy.PRESERVE_IEEE_FLOAT32}[name]


def _path(commands):
    return ij.Selection.from_path(ij.ImageJCompositePath(commands))


def _owner(case, roi_policy, *, sample_policy=None, reads=None):
    pixels, _, _, _, policy = _reference(case)
    volume = np.broadcast_to(pixels, TCZ + pixels.shape).copy()
    request = case[1]["request"]
    calibration = (ij.SpatialCalibration(spacing=(.5, 1, 3), origin=(7, -2, 5), unit="um")
                   if request.get("sigma_x") == 2 and request.get("sigma_y") == 1 else
                   ij.SpatialCalibration())
    dataset = ij.Dataset(volume, axes="TCZYXS" if volume.ndim == 6 else "TCZYX",
                         sample_policy=_policy(policy if sample_policy is None else sample_policy),
                         calibration=calibration)
    if reads is not None:
        dataset = dataset.observe_reads(reads)
    owner = ij.Document(dataset, "Pinned path scope source")
    path = _path(request["commands"])
    width, height = request["width"], request["height"]
    full = _path([0, 0, 0, 1, width, 0, 1, width, height, 1, 0, height, 4])
    edit = owner.begin_edit()
    edit.select(path if roi_policy == "active" else
                (_path(SUBSET_PATH) if roi_policy == "whole-plane" else full))
    edit.set_previous_selection(path)
    edit.set_rois([
        ij.ROI("T wildcard", path, t=1, attributes={"p13i": "wildcard"}),
        ij.ROI("Exact TCZ", path, t=0, c=0, z=1, attributes={"p13i": "exact"}),
        ij.ROI("Partial overlap", _path(SUBSET_PATH), t=1, c=2, z=3),
    ])
    edit.set_overlays([{"p13i": {"opaque": "preserve", "unsigned": 2**63 + 7}}])
    owner.commit(edit)
    view = owner.snapshot().display
    view.t, view.c, view.crosshair = CURRENT[0], CURRENT[1], [1, 2, CURRENT[2]]
    view.properties = {"lut": "Inverted" if request.get("inverted_lut", False) else "Grays",
                       "p13i_scope_keep": {"value": 17, "signed_zero": -0.0}}
    owner.set_display(view)
    # Seeded annotation state is the clean baseline, not part of filter history.
    return ij.Document(owner.snapshot()), volume


def _request(scope, roi_policy, destination):
    kwargs = {
        "roi": {"active": ij.RoiPolicy.ACTIVE_SELECTION,
                "positioned": ij.RoiPolicy.POSITIONED_ROIS,
                "whole-plane": ij.RoiPolicy.WHOLE_PLANE}[roi_policy],
        "destination": ij.OutputDestination.IN_PLACE if destination == "in-place"
                       else ij.OutputDestination.NEW_DOCUMENT,
    }
    if scope == "current":
        return ij.TargetRequest(ij.TargetScope.CURRENT_PLANE, **kwargs)
    if scope == "all-z":
        return ij.TargetRequest(ij.TargetScope.ALL_Z, **kwargs)
    if scope == "stepped-tcz":
        return ij.TargetRequest(ij.TargetScope.RANGES,
                                t=ij.AxisRange(0, 2, 2), c=ij.AxisRange(0, 3, 2),
                                z=ij.AxisRange(1, 4, 2), **kwargs)
    assert scope == "ordered-positions"
    return ij.TargetRequest(ij.TargetScope.POSITIONS,
                            positions=[ij.PlanePosition(*p) for p in SCOPE_POSITIONS[scope]],
                            **kwargs)


def _resolve(owner, case, target_request):
    family, row = case
    request, source = row["request"], owner.snapshot()
    if family == "gaussian":
        return ij.resolve_gaussian_command(source, target_request,
                                            ij.GaussianBlurCommandOptions(request["sigma"], False))
    if family.startswith("gaussian-typed"):
        sx, sy = request["sigma_x"], request["sigma_y"]
        scaled = sx != sy
        assert not scaled or (sx, sy) == (2, 1)
        command = ij.resolve_gaussian_command(source, target_request,
            ij.GaussianBlurCommandOptions(1 if scaled else sx, scaled))
        assert (command.sigma_x, command.sigma_y) == (sx, sy)
        assert command.parameters() == {"sigma": [float(sy), float(sx)]}
        return command
    operation, radius = request["operation"], request["radius"]
    if operation == "remove_nans":
        return ij.resolve_remove_nans_command(source, target_request,
                                              ij.RemoveNaNsCommandOptions(radius))
    if operation == "outliers":
        return ij.resolve_remove_outliers_command(source, target_request,
            ij.RemoveOutliersCommandOptions(radius, request["threshold"],
                                            "bright" if request["which_outliers"] == 0 else "dark"))
    if operation == "top_hat":
        return ij.resolve_top_hat_command(source, target_request,
            ij.TopHatCommandOptions(radius, request["light_background"], request["dont_subtract"]))
    if operation == "despeckle":
        assert radius == 1
        return ij.resolve_despeckle_command(source, target_request)
    return ij.resolve_rank_command(source, target_request,
                                   ij.RankFilterCommandOptions(operation, radius))


def _positions(target):
    return tuple((p.t, p.c, p.z) for p in
                 (target.position(index) for index in range(target.position_count)))


def _operation(case, command):
    return "gaussian_blur_imagej" if _is_gaussian(case) else command.operation


def _restore(source, case, record):
    decode = ij.gaussian_command_from_record if _is_gaussian(case) else ij.rank_command_from_record
    return decode(source, record)


def _rebind(source, case, record, request):
    rebind = ij.rebind_gaussian_command if _is_gaussian(case) else ij.rebind_rank_command
    return rebind(source, record, request)


def _navigate(owner, position=NAVIGATED):
    view = owner.snapshot().display
    view.t, view.c, view.crosshair = position[0], position[1], [1, 2, position[2]]
    view.zoom = 2.5
    owner.set_display(view)


def _camera(snapshot):
    view = snapshot.display
    return view.t, view.c, tuple(view.crosshair), view.zoom


def _calibration(snapshot):
    value = snapshot.dataset.calibration
    return (tuple(value.spacing), tuple(value.origin), tuple(value.direction), value.unit,
            value.frame_interval, value.time_origin, value.time_unit)


def _annotations(snapshot):
    def selection(value):
        return None if value is None else (value.path.commands, value.attributes)
    return (selection(snapshot.selection), selection(snapshot.previous_selection),
            [(row.name, row.t, row.c, row.z, selection(row.selection), row.attributes)
             for row in snapshot.rois], snapshot.overlays)


def _expected_volume(case, scope, roi_policy):
    _, original, filtered, _, _ = _reference(case)
    expected = np.broadcast_to(original, TCZ + original.shape).copy()
    for position in SCOPE_POSITIONS[scope]:
        if roi_policy != "positioned" or position in SAVED_MATCHES:
            expected[position] = filtered
    return expected


def _dataset_pixels(snapshot):
    return snapshot.dataset.numpy("TCZYXS" if snapshot.dataset.shape[5] != 1 else "TCZYX")


def _assert_words(snapshot, expected):
    actual = _dataset_pixels(snapshot)
    assert actual.shape == expected.shape
    np.testing.assert_array_equal(actual.view(expected.dtype), expected)


def _assert_source(owner, before, original):
    now = owner.snapshot()
    assert now.shares_data(before)
    assert now.revisions.matches(before.revisions, ij.Change.ALL)
    assert now.dirty == before.dirty
    assert owner.undo_steps == owner.redo_steps == 0
    assert _dataset_pixels(now).tobytes() == original.tobytes()
    assert _annotations(now) == _annotations(before)


def _assert_regions(command, case, scope, roi_policy):
    _, _, _, mask, _ = _reference(case)
    observed = case[1]["observation"]["processor_roi_before_mask"]
    extent = tuple(observed["image_origin"] + observed["dimensions"])
    for index, position in enumerate(SCOPE_POSITIONS[scope]):
        region = (ij.gaussian_command_region(command.target, index) if _is_gaussian(case)
                  else command.region(index))
        if roi_policy == "whole-plane":
            assert region == {"extent": None, "mask": None}
            assert np.all(mask == 255)
            continue
        matched = roi_policy == "active" or position in SAVED_MATCHES
        np.testing.assert_array_equal(region["mask"], mask if matched else np.zeros_like(mask))
        assert region["extent"] == (extent if matched else (0, 0, 0, 0))


@pytest.mark.parametrize("case", CASES, ids=CASE_IDS)
@pytest.mark.parametrize("scope", SCOPES)
@pytest.mark.parametrize("roi_policy", ROI_POLICIES)
@pytest.mark.parametrize("destination", DESTINATIONS)
def test_sc01_pinned_words_target_isolation_and_atomic_publication(case, scope, roi_policy, destination):
    _check_publication(case, scope, roi_policy, destination)


def _check_publication(case, scope, roi_policy, destination):
    owner, original = _owner(case, roi_policy)
    request = _request(scope, roi_policy, destination)
    command = _resolve(owner, case, request)
    positions = SCOPE_POSITIONS[scope]
    assert _positions(command.target) == positions
    assert command.target.source.dataset.shape[:3] == tuple(TCZ)
    captured = json.loads(json.dumps(command.record()))
    _navigate(owner)
    before = owner.snapshot()
    # Strict same-owner decode binds the current immutable snapshot, retaining
    # the captured positions rather than resolving Current/AllZ at navigation.
    replay = _restore(before, case, captured)
    assert replay.record() == captured and _positions(replay.target) == positions
    assert _positions(command.target) == positions
    _assert_regions(replay, case, scope, roi_policy)
    expected = _expected_volume(case, scope, roi_policy)
    app = ij.Application()
    app.adopt(owner)
    try:
        prepared = app.prepare(_operation(case, replay), replay.target, replay.parameters())
        assert tuple((row.position.t, row.position.c, row.position.z)
                     for row in prepared.outputs) == positions
        assert [row.skipped for row in prepared.outputs] == [
            roi_policy == "positioned" and position not in SAVED_MATCHES for position in positions]
        _assert_words(prepared.preview, expected)
        assert prepared.preview.dataset.sample_policy == before.dataset.sample_policy
        assert _calibration(prepared.preview) == _calibration(before)
        assert _annotations(prepared.preview) == _annotations(before)
        _assert_source(owner, before, original)
        result = app.publish(prepared)
        assert not prepared.active
        output = result.document
        created = destination == "new-document"
        assert result.created is created
        assert (output.id != owner.id) is created
        _assert_words(output.snapshot(), expected)
        assert _calibration(output.snapshot()) == _calibration(before)
        assert _annotations(output.snapshot()) == _annotations(before)
        assert _camera(output.snapshot()) == _camera(before)
        assert output.snapshot().display.properties["p13i_scope_keep"] == before.display.properties["p13i_scope_keep"]
        if created:
            _assert_source(owner, before, original)
        changed = expected.tobytes() != original.tobytes()
        expected_history = int(created or changed)
        assert output.undo_steps == expected_history and output.redo_steps == 0
        if expected_history:
            output.undo()
            assert _dataset_pixels(output.snapshot()).tobytes() == original.tobytes()
            assert _annotations(output.snapshot()) == _annotations(before)
            assert output.undo_steps == 0 and output.redo_steps == 1
            output.redo()
            _assert_words(output.snapshot(), expected)
            assert output.undo_steps == 1 and output.redo_steps == 0
    finally:
        app.job_scheduler.shutdown()


@pytest.mark.parametrize("case", CASES, ids=CASE_IDS)
@pytest.mark.parametrize("roi_policy", ROI_POLICIES)
def test_sc02_session_explicit_rebind_and_portable_workflow(case, roi_policy):
    _check_session_replay(case, roi_policy)


def _check_session_replay(case, roi_policy):
    owner, original = _owner(case, roi_policy)
    scope = "ordered-positions"
    request = _request(scope, roi_policy, "in-place")
    command = _resolve(owner, case, request)
    captured = copy.deepcopy(command.record())
    program = ij.NativeWorkflowProgram([
        ij.NativeWorkflowOperation(_operation(case, command), {}, request, original=captured)
    ])
    record = program.record()
    capture = ij.SessionCapture([owner.snapshot()])
    capture.workflow = [record]
    encoded = ij.encode_session(capture)
    reopened = ij.decode_session(encoded)
    restored = reopened.documents[0]
    source = restored.snapshot()
    assert restored.id != owner.id and restored.undo_steps == restored.redo_steps == 0
    assert _dataset_pixels(source).tobytes() == original.tobytes()
    assert _annotations(source) == _annotations(owner.snapshot())
    assert source.dataset.sample_policy == owner.snapshot().dataset.sample_policy
    assert _calibration(source) == _calibration(owner.snapshot())
    assert reopened.workflow == [record]
    _navigate(restored)
    before = restored.snapshot()
    with pytest.raises(ValueError):
        _restore(before, case, captured)
    rebound = _rebind(before, case, captured, request)
    assert _positions(rebound.target) == SCOPE_POSITIONS[scope]
    assert rebound.target.document_id == restored.id
    expected = _expected_volume(case, scope, roi_policy)
    direct = ij.prepare_operation(_operation(case, rebound), rebound.target, rebound.parameters())
    _assert_words(direct.preview, expected)
    _assert_source(restored, before, original)
    loaded = ij.NativeWorkflowProgram.from_record(reopened.workflow[0])
    loaded = ij.NativeWorkflowProgram.from_json(loaded.json())
    assert loaded.record() == record
    proposal = ij.prepare_native_workflow(before, loaded.steps)
    assert proposal.completed_steps == 1
    _assert_words(proposal.preview, expected)
    _assert_source(restored, before, original)
    restored.commit(proposal.begin_edit())
    _assert_words(restored.snapshot(), expected)
    assert _annotations(restored.snapshot()) == _annotations(before)
    assert _camera(restored.snapshot()) == _camera(before)
    changed = expected.tobytes() != original.tobytes()
    assert restored.undo_steps == int(changed)
    if changed:
        restored.undo()
        assert _dataset_pixels(restored.snapshot()).tobytes() == original.tobytes()
        restored.redo()
        _assert_words(restored.snapshot(), expected)
    final = ij.decode_session(ij.encode_session(ij.SessionCapture([restored.snapshot()])))
    _assert_words(final.documents[0].snapshot(), expected)
    assert _annotations(final.documents[0].snapshot()) == _annotations(before)
    assert final.documents[0].snapshot().dataset.sample_policy == before.dataset.sample_policy
    assert _calibration(final.documents[0].snapshot()) == _calibration(before)
    assert final.documents[0].undo_steps == final.documents[0].redo_steps == 0
    assert command.record() == captured and program.record() == record


@pytest.mark.parametrize("case", CASES, ids=CASE_IDS)
def test_sc03_unmatched_saved_path_is_empty_and_keeps_clean_history(case):
    _check_unmatched(case)


def _check_unmatched(case):
    owner, original = _owner(case, "positioned")
    _navigate(owner, (0, 1, 2))
    before = owner.snapshot()
    request = _request("current", "positioned", "in-place")
    command = _resolve(owner, case, request)
    assert _positions(command.target) == ((0, 1, 2),)
    region = (ij.gaussian_command_region(command.target) if _is_gaussian(case)
              else command.region())
    assert region["extent"] == (0, 0, 0, 0)
    assert np.count_nonzero(region["mask"]) == 0
    prepared = ij.prepare_operation(_operation(case, command), command.target, command.parameters())
    assert len(prepared.outputs) == 1 and prepared.outputs[0].skipped
    assert _dataset_pixels(prepared.preview).tobytes() == original.tobytes()
    owner.commit(prepared.begin_edit())
    _assert_source(owner, before, original)


def test_sc04_matrix_catalog_and_literal_scope_partition():
    assert len(RANK_FIXTURE["cases"]) == 253
    assert len(GAUSSIAN_FIXTURE["cases"]) + len(GAUSSIAN_FIXTURE["degenerate_cases"]) == 125
    assert len(RANK_CASES) == 27
    all_positions = set(itertools.product(range(2), range(3), range(4)))
    for scope in SCOPES:
        positions = SCOPE_POSITIONS[scope]
        assert len(positions) == len(set(positions))
        assert set(positions) < all_positions
    assert SCOPE_POSITIONS["ordered-positions"] != tuple(sorted(SCOPE_POSITIONS["ordered-positions"]))
    assert set(SCOPE_POSITIONS["stepped-tcz"]) & SAVED_MATCHES == {(0, 0, 1)}
    assert set(SCOPE_POSITIONS["ordered-positions"]) - SAVED_MATCHES == {(0, 1, 2)}
    assert len(TYPED_GAUSSIAN_FIXTURE["cases"]) == 80
    assert len(RANK_SCOPE_FIXTURE["cases"]) == 27
    assert len(TYPED_TRIANGLE_CASES) == 20 and len(WHOLE_PLANE_CASES) == 47
    assert {(row["request"]["operation"], row["request"]["pixel_type"],
             row["request"]["input_pattern"]) for row in RANK_SCOPE_FIXTURE["cases"]} == {
        (row["request"]["operation"], row["request"]["pixel_type"],
         row["request"]["input_pattern"]) for _, row in RANK_CASES}


@pytest.mark.parametrize("case,roi_policy", SUPPLEMENT_CASES, ids=SUPPLEMENT_IDS)
@pytest.mark.parametrize("scope", SCOPES)
@pytest.mark.parametrize("destination", DESTINATIONS)
def test_sc05_typed_gaussian_and_whole_plane_scope_cells(case, roi_policy, scope, destination):
    _check_publication(case, scope, roi_policy, destination)


@pytest.mark.parametrize("case,roi_policy", SUPPLEMENT_CASES, ids=SUPPLEMENT_IDS)
def test_sc06_typed_gaussian_and_whole_plane_session_replay(case, roi_policy):
    _check_session_replay(case, roi_policy)


@pytest.mark.parametrize("case", TYPED_TRIANGLE_CASES, ids=TYPED_TRIANGLE_IDS)
def test_sc07_typed_gaussian_unmatched_saved_path(case):
    _check_unmatched(case)


@pytest.mark.parametrize("case", IEEE_GAUSSIAN_CASES, ids=tuple(map(_case_id, IEEE_GAUSSIAN_CASES)))
@pytest.mark.parametrize("scope", SCOPES)
@pytest.mark.parametrize("roi_policy", ROI_POLICIES + ("whole-plane",))
@pytest.mark.parametrize("destination", DESTINATIONS)
def test_sc08_gaussian_ieee_metadata_refuses_before_reads_and_progress(case, scope, roi_policy, destination):
    reads = ij.DatasetReadObserver()
    owner, original = _owner(case, roi_policy, sample_policy="ieee", reads=reads)
    before = owner.snapshot()
    command = _resolve(owner, case, _request(scope, roi_policy, destination))
    assert _positions(command.target) == SCOPE_POSITIONS[scope]
    assert reads.counts().plane_reads == reads.counts().region_reads == 0
    progress = []
    with pytest.raises(ValueError, match="IEEE Float32"):
        ij.prepare_operation("gaussian_blur_imagej", command.target, command.parameters(),
                             progress=lambda done, total: progress.append((done, total)))
    assert progress == []
    assert reads.counts().plane_reads == reads.counts().region_reads == 0
    _assert_source(owner, before, original)
