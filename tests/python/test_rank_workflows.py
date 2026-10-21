"""Typed rank workflow dispatch and detached proposal atomicity.

Expected pixels are owned literal radius-zero five-cell-cross examples plus
one radius-one calibration example. No native/Python filter computes an
expectation. These tests exercise the legacy
detached model's [Z,]YXS workflow adapter; they give no installed or GUI credit.
"""

from copy import deepcopy
import json

import imagejxx as ij
import numpy as np
import pytest

from imagejxx.ui import workflows
from imagejxx.ui.model import Calibration, Document, NamedROI, Selection


METHODS = ("minimum", "maximum", "median", "mean")
DTYPES = (("u8", np.uint8), ("u16", np.uint16), ("f32", np.float32))
NUMERICAL_CASES = tuple((method, name, dtype)
                        for method in METHODS for name, dtype in DTYPES)
NUMERICAL_IDS = tuple(f"{method}-{name}" for method, name, _ in NUMERICAL_CASES)

# These literal matrices describe one isolated 80-valued sample. Radius zero
# retains the cross of five cells; its mean is exactly16 in all three types.
SPIKE = (
    (0, 0, 0, 0, 0),
    (0, 0, 0, 0, 0),
    (0, 0, 80, 0, 0),
    (0, 0, 0, 0, 0),
    (0, 0, 0, 0, 0),
)
LOW = (
    (0, 0, 0, 0, 0),
    (0, 0, 0, 0, 0),
    (0, 0, 0, 0, 0),
    (0, 0, 0, 0, 0),
    (0, 0, 0, 0, 0),
)
MAXIMUM = (
    (0, 0, 0, 0, 0),
    (0, 0, 80, 0, 0),
    (0, 80, 80, 80, 0),
    (0, 0, 80, 0, 0),
    (0, 0, 0, 0, 0),
)
MAXIMUM_RADIUS1 = (
    (0, 0, 0, 0, 0),
    (0, 80, 80, 80, 0),
    (0, 80, 80, 80, 0),
    (0, 80, 80, 80, 0),
    (0, 0, 0, 0, 0),
)
MEAN = (
    (0, 0, 0, 0, 0),
    (0, 0, 16, 0, 0),
    (0, 16, 16, 16, 0),
    (0, 0, 16, 0, 0),
    (0, 0, 0, 0, 0),
)
RECTANGLE_MEAN = (
    (0, 0, 0, 0, 0),
    (0, 0, 16, 0, 0),
    (0, 16, 16, 0, 0),
    (0, 0, 16, 0, 0),
    (0, 0, 0, 0, 0),
)
HOLED_MEAN = (
    (0, 0, 0, 0, 0),
    (0, 0, 16, 0, 0),
    (0, 16, 80, 16, 0),
    (0, 0, 16, 0, 0),
    (0, 0, 0, 0, 0),
)
RING = (
    (0, 0, 0, 0, 0),
    (0, 255, 255, 255, 0),
    (0, 255, 0, 255, 0),
    (0, 255, 255, 255, 0),
    (0, 0, 0, 0, 0),
)


def spike(dtype=np.uint8, base=0):
    return np.array(SPIKE, dtype=dtype) + dtype(base)


def expected(method="mean", dtype=np.uint8, base=0):
    values = {"minimum": LOW, "maximum": MAXIMUM,
              "median": LOW, "mean": MEAN}[method]
    return np.array(values, dtype=dtype) + dtype(base)


def raw_record(method="median", radius=0):
    original = ij.Document(ij.Dataset(spike(), axes="YX"))
    return ij.resolve_rank_command(
        original.snapshot(), options=ij.RankFilterCommandOptions(method, radius)
    ).record()


def gaussian_record():
    original = ij.Document(ij.Dataset(spike(), axes="YX"))
    return ij.resolve_gaussian_command(
        original.snapshot(), options=ij.GaussianBlurCommandOptions(0, False)
    ).record()


def source_state(document):
    return {
        "shape": document.data.shape,
        "dtype": document.data.dtype,
        "pixels": document.data.tobytes(),
        "metadata": deepcopy(document.metadata()),
        "revision": document.revision,
        "dirty": document.dirty,
        "can_undo": document.can_undo,
        "can_redo": document.can_redo,
    }


def assert_unchanged(document, before, steps, frozen):
    assert source_state(document) == before
    assert steps == frozen


@pytest.mark.parametrize("case", NUMERICAL_CASES, ids=NUMERICAL_IDS)
def test_rw01_literal_rank_proposal_for_all_methods_and_canonical_types(case):
    method, _, dtype = case
    document = Document(spike(dtype))
    steps = [raw_record(method)]
    before, frozen = source_state(document), deepcopy(steps)
    result = workflows.run(document, steps,
                           rank_rebind=ij.TargetRequest(roi=ij.RoiPolicy.WHOLE_PLANE))
    wanted = expected(method, dtype)
    assert result.dtype == np.dtype(dtype) and result.shape == wanted.shape
    assert result.tobytes() == wanted.tobytes()
    assert not np.shares_memory(result, document.data)
    assert_unchanged(document, before, steps, frozen)


def test_rw02_rank_and_mixed_records_keep_the_exact_version1_envelope():
    steps = [gaussian_record(), raw_record("mean"),
             {"operation": "add", "parameters": {"value": 1}}]
    frozen = deepcopy(steps)
    assert workflows.validate_steps(steps) is steps
    assert ij.validate_rank_command_record(steps[1]) is None
    envelope = json.loads(workflows.dumps(steps))
    assert set(envelope) == {"format", "version", "steps"}
    assert envelope["format"] == "imagejxx-workflow"
    assert type(envelope["version"]) is int and envelope["version"] == 1
    assert envelope["steps"] == frozen
    assert workflows.loads(json.dumps(envelope)) == frozen and steps == frozen
    assert len(EXPECTED_COLLECTION_IDS) == 49


INVALID_REQUESTS = (None, True, {}, "current_plane", 0)
INVALID_REQUEST_IDS = ("missing", "boolean", "json-object", "text", "integer")


@pytest.mark.parametrize("rebind_request", INVALID_REQUESTS, ids=INVALID_REQUEST_IDS)
def test_rw03_rank_requires_its_own_actual_explicit_request(rebind_request):
    document = Document(spike())
    steps = [raw_record("mean")]
    before, frozen, progress = source_state(document), deepcopy(steps), []
    with pytest.raises(TypeError, match="explicit TargetRequest"):
        workflows.run(document, steps, gaussian_rebind=ij.TargetRequest(),
                      rank_rebind=rebind_request,
                      progress=lambda completed, total: progress.append((completed, total)))
    assert progress == []
    assert_unchanged(document, before, steps, frozen)


@pytest.mark.parametrize("present", ("gaussian", "rank"), ids=("only-gaussian", "only-rank"))
def test_rw04_mixed_typed_workflow_requires_each_operations_request(present):
    document = Document(spike())
    steps = [gaussian_record(), raw_record("mean")]
    before, frozen, progress = source_state(document), deepcopy(steps), []
    arguments = {present + "_rebind": ij.TargetRequest()}
    with pytest.raises(TypeError, match="explicit TargetRequest"):
        workflows.run(document, steps, **arguments,
                      progress=lambda completed, total: progress.append((completed, total)))
    assert progress == []
    assert_unchanged(document, before, steps, frozen)


def test_rw05_mixed_gaussian_rank_and_legacy_dispatch_preserves_progress_order():
    document = Document(spike())
    document.select(Selection("rectangle", [(1, 1), (3, 4)]))
    steps = [gaussian_record(), raw_record("mean"),
             {"operation": "add", "parameters": {"value": 1}}]
    before, frozen, progress = source_state(document), deepcopy(steps), []
    result = workflows.run(
        document, workflows.loads(workflows.dumps(steps)),
        gaussian_rebind=ij.TargetRequest(roi=ij.RoiPolicy.WHOLE_PLANE,
                                       destination=ij.OutputDestination.NEW_DOCUMENT),
        rank_rebind=ij.TargetRequest(roi=ij.RoiPolicy.ACTIVE_SELECTION),
        progress=lambda completed, total: progress.append((completed, total)),
    )
    # Gaussian's WholePlane request must not replace rank's active rectangle.
    # The legacy Add then changes the same rectangle only. In particular the
    # excluded right cross cell (3,2) remains zero throughout both writes.
    wanted = np.array(((0, 0, 0, 0, 0),
                       (0, 1, 17, 0, 0),
                       (0, 17, 17, 0, 0),
                       (0, 1, 17, 0, 0),
                       (0, 0, 0, 0, 0)), np.uint8)
    assert result.dtype == np.uint8 and result.tobytes() == wanted.tobytes()
    assert progress == [(1, 3), (2, 3), (3, 3)]
    assert_unchanged(document, before, steps, frozen)


MALFORMED_RECORDS = (
    ("schema-boolean", ("schema_version",), True),
    ("schema-floating", ("schema_version",), 1.0),
    ("unsupported-typed-operation", ("operation",), "invert"),
    ("wrong-profile", ("profile",), "another_profile"),
    ("unknown-option", ("options", "unknown"), 7),
    ("boolean-radius", ("options", "radius"), True),
    ("nonfinite-radius", ("options", "radius"), float("inf")),
    ("original-rgb-shape", ("target", "source_shape"), [1, 1, 1, 5, 5, 3]),
    ("original-outside-range", ("target", "ranges"),
     [[0, 1, 1], [0, 1, 1], [0, 2, 1]]),
    ("boolean-original-revision", ("target", "source_revisions"), [True, 0, 0]),
)


def malformed_record(case):
    _, path, value = case
    record = raw_record()
    node = record
    for key in path[:-1]:
        node = node[key]
    node[path[-1]] = deepcopy(value)
    return record


@pytest.mark.parametrize("case", MALFORMED_RECORDS, ids=[case[0] for case in MALFORMED_RECORDS])
def test_rw06_every_original_is_validated_before_any_workflow_step(case):
    document = Document(spike())
    steps = [{"operation": "invert", "parameters": {}}, gaussian_record(), malformed_record(case)]
    before, frozen = source_state(document), deepcopy(steps)
    for entrypoint in ("validate", "dumps", "loads", "run"):
        progress = []
        with pytest.raises((ValueError, TypeError, OverflowError, IndexError)):
            if entrypoint == "validate":
                workflows.validate_steps(steps)
            elif entrypoint == "dumps":
                workflows.dumps(steps)
            elif entrypoint == "loads":
                workflows.loads(json.dumps({"format": "imagejxx-workflow", "version": 1,
                                            "steps": steps}))
            else:
                workflows.run(document, steps,
                              gaussian_rebind=ij.TargetRequest(), rank_rebind=ij.TargetRequest(),
                              progress=lambda completed, total: progress.append((completed, total)))
        assert progress == [], entrypoint
        assert_unchanged(document, before, steps, frozen)


STACK_SCOPES = ("current", "all-z", "range", "explicit-position")


@pytest.mark.parametrize("scope", STACK_SCOPES, ids=STACK_SCOPES)
def test_rw07_explicit_request_retains_destination_stack_positions(scope):
    pixels = np.stack([spike(np.uint16, base) for base in (0, 10, 20)])
    document = Document(pixels, is_stack=True)
    document.slice_index = 2
    requests = {
        "current": ij.TargetRequest(),
        "all-z": ij.TargetRequest(ij.TargetScope.ALL_Z),
        "range": ij.TargetRequest(ij.TargetScope.RANGES, z=ij.AxisRange(0, 1)),
        "explicit-position": ij.TargetRequest(ij.TargetScope.POSITIONS,
                                              positions=[ij.PlanePosition(z=1)]),
    }
    included = {"current": (2,), "all-z": (0, 1, 2), "range": (0,), "explicit-position": (1,)}[scope]
    wanted = np.stack([expected("mean", np.uint16, 10 * index) if index in included
                       else spike(np.uint16, 10 * index) for index in range(3)])
    steps = [raw_record("mean")]
    before, frozen = source_state(document), deepcopy(steps)
    result = workflows.run(document, steps, rank_rebind=requests[scope])
    assert result.dtype == np.uint16 and result.tobytes() == wanted.tobytes()
    assert_unchanged(document, before, steps, frozen)


ROI_CASES = ("rectangle", "holed-mask", "nonarea", "empty-mask")


@pytest.mark.parametrize("roi", ROI_CASES, ids=ROI_CASES)
def test_rw08_active_area_holes_nonarea_and_empty_use_native_rank_geometry(roi):
    document = Document(spike())
    selections = {
        "rectangle": Selection("rectangle", [(1, 1), (3, 4)]),
        "holed-mask": Selection("mask", bitmap=np.array(RING, np.uint8)),
        "nonarea": Selection("point", [(2, 2)]),
        "empty-mask": Selection("mask", bitmap=np.zeros((5, 5), np.uint8)),
    }
    wanted = np.array({"rectangle": RECTANGLE_MEAN, "holed-mask": HOLED_MEAN,
                       "nonarea": MEAN, "empty-mask": SPIKE}[roi], np.uint8)
    document.select(selections[roi])
    steps = [raw_record("mean")]
    before, frozen = source_state(document), deepcopy(steps)
    result = workflows.run(document, steps, rank_rebind=ij.TargetRequest())
    assert result.dtype == np.uint8 and result.tobytes() == wanted.tobytes()
    assert_unchanged(document, before, steps, frozen)


def test_rw09_positioned_roi_hole_keeps_unmatched_stack_members_exact():
    pixels = np.stack([spike(np.uint16, base) for base in (0, 10, 20)])
    document = Document(pixels, is_stack=True)
    document.rois = [NamedROI("Only middle Z", Selection("mask", bitmap=np.array(RING, np.uint8)),
                              slice_index=1, attributes={"owned_group": 7})]
    steps = [raw_record("mean")]
    before, frozen = source_state(document), deepcopy(steps)
    result = workflows.run(document, steps, rank_rebind=ij.TargetRequest(
        ij.TargetScope.ALL_Z, roi=ij.RoiPolicy.POSITIONED_ROIS))
    wanted = np.stack([spike(np.uint16), np.array(HOLED_MEAN, np.uint16) + 10,
                       spike(np.uint16, 20)])
    assert result.dtype == np.uint16 and result.tobytes() == wanted.tobytes()
    assert_unchanged(document, before, steps, frozen)


DESTINATIONS = (ij.OutputDestination.IN_PLACE, ij.OutputDestination.NEW_DOCUMENT)


@pytest.mark.parametrize("destination", DESTINATIONS, ids=("in-place-scratch", "new-document-scratch"))
def test_rw10_both_destinations_return_independent_proposals_without_source_publication(destination):
    document = Document(spike(np.float32), "Owned rank destination")
    steps = [raw_record("maximum")]
    before, frozen = source_state(document), deepcopy(steps)
    result = workflows.run(document, steps, rank_rebind=ij.TargetRequest(
        roi=ij.RoiPolicy.WHOLE_PLANE, destination=destination))
    wanted = expected("maximum", np.float32)
    assert result.dtype == np.float32 and result.tobytes() == wanted.tobytes()
    assert not np.shares_memory(result, document.data)
    result[0, 0] = 123
    assert_unchanged(document, before, steps, frozen)


def test_rw11_pixel_radius_intent_does_not_scale_with_destination_calibration():
    document = Document(spike(np.uint16))
    document.set_calibration(Calibration(
        pixel_width=0.125, pixel_height=3, pixel_depth=7, unit="um",
        origin=(11, -7, 5), direction=(0, -1, 0, 1, 0, 0, 0, 0, 1),
        frame_interval=0.25, time_origin=8, time_unit="min"))
    steps = [raw_record("maximum", 1)]
    assert "calibration" not in steps[0] and steps[0]["options"]["radius"] == 1
    before, frozen = source_state(document), deepcopy(steps)
    result = workflows.run(document, steps, rank_rebind=ij.TargetRequest(roi=ij.RoiPolicy.WHOLE_PLANE))
    # Radius1's owned nine-cell square differs from the radius-zero cross;
    # strong anisotropic physical spacing must not stretch this pixel kernel.
    assert result.tobytes() == np.array(MAXIMUM_RADIUS1, np.uint16).tobytes()
    assert_unchanged(document, before, steps, frozen)


def test_rw12_cancellation_between_rank_steps_discards_the_working_proposal():
    document = Document(spike())
    steps = [raw_record("mean"), raw_record("maximum")]
    before, frozen, progress = source_state(document), deepcopy(steps), []
    with pytest.raises(InterruptedError, match="cancelled"):
        workflows.run(document, steps, rank_rebind=ij.TargetRequest(),
                      cancelled=lambda: bool(progress),
                      progress=lambda completed, total: progress.append((completed, total)))
    assert progress == [(1, 2)]
    assert_unchanged(document, before, steps, frozen)


def test_rw13_precancel_stops_before_any_rank_step_or_progress():
    document = Document(spike())
    steps = [raw_record("mean")]
    before, frozen, progress = source_state(document), deepcopy(steps), []
    with pytest.raises(InterruptedError, match="cancelled"):
        workflows.run(document, steps, rank_rebind=ij.TargetRequest(), cancelled=lambda: True,
                      progress=lambda completed, total: progress.append((completed, total)))
    assert progress == []
    assert_unchanged(document, before, steps, frozen)


def test_rw14_failing_progress_never_publishes_an_already_computed_rank_proposal():
    document = Document(spike())
    steps = [raw_record("mean"), {"operation": "invert", "parameters": {}}]
    before, frozen, progress = source_state(document), deepcopy(steps), []

    def fail(completed, total):
        progress.append((completed, total))
        raise RuntimeError("Owned rank workflow progress failure")

    with pytest.raises(RuntimeError, match="Owned rank workflow progress failure"):
        workflows.run(document, steps, rank_rebind=ij.TargetRequest(), progress=fail)
    assert progress == [(1, 2)]
    assert_unchanged(document, before, steps, frozen)


def test_rw15_later_legacy_failure_discards_an_already_computed_rank_proposal():
    document = Document(spike())
    steps = [raw_record("mean"), {"operation": "add", "parameters": {"value": 1},
                                 "slice_index": 99}]
    before, frozen, progress = source_state(document), deepcopy(steps), []
    with pytest.raises(ValueError, match="outside"):
        workflows.run(document, steps, rank_rebind=ij.TargetRequest(),
                      progress=lambda completed, total: progress.append((completed, total)))
    assert progress == [(1, 2)]
    assert_unchanged(document, before, steps, frozen)


def test_rw16_valid_scientific_original_is_rebound_only_with_the_complete_new_request():
    native = ij.Document(ij.Dataset(np.zeros((2, 2, 3, 2, 2), np.uint16), axes="TCZYX"))
    record = ij.resolve_rank_command(native.snapshot(), ij.TargetRequest(
        ij.TargetScope.RANGES, t=ij.AxisRange(), c=ij.AxisRange(), z=ij.AxisRange()),
        ij.RankFilterCommandOptions("maximum", 0)).record()
    assert record["target"]["source_shape"] == [2, 2, 3, 2, 2, 1]
    document = Document(np.stack([spike(np.uint16, base) for base in (0, 10, 20)]), is_stack=True)
    document.slice_index = 2
    steps = [record]
    before, frozen = source_state(document), deepcopy(steps)
    result = workflows.run(document, steps, rank_rebind=ij.TargetRequest(
        ij.TargetScope.POSITIONS, positions=[ij.PlanePosition(z=1)],
        roi=ij.RoiPolicy.WHOLE_PLANE, destination=ij.OutputDestination.NEW_DOCUMENT))
    wanted = np.stack([spike(np.uint16), expected("maximum", np.uint16, 10), spike(np.uint16, 20)])
    assert result.shape == (3, 5, 5) and result.tobytes() == wanted.tobytes()
    # The original TCZ graph remains a validated immutable original; no source
    # axes/identity are copied into the legacy destination model's ZYX graph.
    assert_unchanged(document, before, steps, frozen)


def test_rw17_actual_but_invalid_full_rebind_request_fails_without_progress_or_source_edit():
    document = Document(spike())
    steps = [raw_record("mean")]
    before, frozen, progress = source_state(document), deepcopy(steps), []
    with pytest.raises(ValueError):
        workflows.run(document, steps,
                      rank_rebind=ij.TargetRequest(mode=ij.ExecutionMode.VOLUME),
                      progress=lambda completed, total: progress.append((completed, total)))
    assert progress == []
    assert_unchanged(document, before, steps, frozen)


# Exact ordered pytest item.name membership for the root's source/installed
# supervisors. This module performs no collection, subprocess or runtime work.
EXPECTED_COLLECTION_IDS = (
    *("test_rw01_literal_rank_proposal_for_all_methods_and_canonical_types[" + case_id + "]"
      for case_id in NUMERICAL_IDS),
    "test_rw02_rank_and_mixed_records_keep_the_exact_version1_envelope",
    *("test_rw03_rank_requires_its_own_actual_explicit_request[" + case_id + "]"
      for case_id in INVALID_REQUEST_IDS),
    "test_rw04_mixed_typed_workflow_requires_each_operations_request[only-gaussian]",
    "test_rw04_mixed_typed_workflow_requires_each_operations_request[only-rank]",
    "test_rw05_mixed_gaussian_rank_and_legacy_dispatch_preserves_progress_order",
    *("test_rw06_every_original_is_validated_before_any_workflow_step[" + case[0] + "]"
      for case in MALFORMED_RECORDS),
    *("test_rw07_explicit_request_retains_destination_stack_positions[" + case_id + "]"
      for case_id in STACK_SCOPES),
    *("test_rw08_active_area_holes_nonarea_and_empty_use_native_rank_geometry[" + case_id + "]"
      for case_id in ROI_CASES),
    "test_rw09_positioned_roi_hole_keeps_unmatched_stack_members_exact",
    "test_rw10_both_destinations_return_independent_proposals_without_source_publication[in-place-scratch]",
    "test_rw10_both_destinations_return_independent_proposals_without_source_publication[new-document-scratch]",
    "test_rw11_pixel_radius_intent_does_not_scale_with_destination_calibration",
    "test_rw12_cancellation_between_rank_steps_discards_the_working_proposal",
    "test_rw13_precancel_stops_before_any_rank_step_or_progress",
    "test_rw14_failing_progress_never_publishes_an_already_computed_rank_proposal",
    "test_rw15_later_legacy_failure_discards_an_already_computed_rank_proposal",
    "test_rw16_valid_scientific_original_is_rebound_only_with_the_complete_new_request",
    "test_rw17_actual_but_invalid_full_rebind_request_fails_without_progress_or_source_edit",
)
