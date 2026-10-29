"""Explicit Gaussian context/target replay; original numerical corpus stays separate."""
import copy
import json

import imagejxx as ij
import numpy as np
import pytest
from test_global_calibration_consumers import global_value


def setup(dtype=np.uint8, samples=1, policy="global"):
    a = np.broadcast_to(np.array([0, 255], dtype), (2, 2, 3, 1, 2)).copy()
    if samples == 3:
        a = np.stack((a, 255-a, a), axis=-1)
    observer = ij.DatasetReadObserver()
    owner = ij.Document(ij.Dataset(a, axes="TCZYXS" if samples == 3 else "TCZYX",
        calibration=ij.SpatialCalibration(spacing=[3, 4, 5], unit="um")).observe_reads(observer))
    display = owner.snapshot().display
    display.t = display.c = 1
    display.crosshair = [0, 0, 1]
    owner.set_display(display)
    app = ij.Application()
    app.adopt(owner)
    if policy != "local":
        app.set_global_calibration(global_value())
    if policy == "ignored":
        app.set_ignore_global_calibration(True, owner.id)
    return app, owner, observer, a


def capture(app, owner, request, detached=False):
    return (ij.resolve_target(owner.snapshot(), request) if detached else
            app.resolve_target(request, document_id=owner.id))


@pytest.mark.parametrize("dtype,samples", [(np.uint8, 1), (np.uint16, 1), (np.float32, 1), (np.uint8, 3)])
@pytest.mark.parametrize("policy", ["global", "local", "ignored", "detached"])
@pytest.mark.parametrize("derived", [False, True])
def test_context_record_pixels_target_and_local_history(dtype, samples, policy, derived):
    app, owner, reads, a = setup(dtype, samples, policy)
    request = ij.TargetRequest(ij.TargetScope.RANGES, z=ij.AxisRange(0, 3, 2),
        roi=ij.RoiPolicy.WHOLE_PLANE,
        destination=ij.OutputDestination.NEW_DOCUMENT if derived else ij.OutputDestination.IN_PLACE)
    target = capture(app, owner, request, policy == "detached")
    options = ij.GaussianBlurCommandOptions(.5 if policy == "global" else 3, True)
    command = ij.resolve_calibrated_command(target, "gaussian_blur_imagej", options)
    record = json.loads(json.dumps(command.record()))
    ij.validate_calibrated_command_record(record)
    assert record["source"]["calibration"]["spacing"] == [3, 4, 5]
    assert (record["effective"] is not None) == (policy == "global")
    assert record["bypass"] == (policy == "ignored")
    assert record["binding"] == ("detached" if policy == "detached" else "application")
    legacy = record["descriptor"]["command"]
    ij.validate_gaussian_command_record(legacy)
    assert legacy["effective_sigma_xy"] == [1, .4 if policy == "global" else .75]
    assert legacy["calibration"]["spacing"] == ([.5, 1.25, 2] if policy == "global" else [3, 4, 5])
    # Navigation and the caller's different default scope must not retarget replay.
    display = owner.snapshot().display
    display.t = display.c = 0
    display.crosshair = [0, 0, 0]
    owner.set_display(display)
    current = capture(app, owner, ij.TargetRequest(), policy == "detached")
    replay = ij.calibrated_command_from_record(current, record)
    assert replay.record() == record and reads.counts().plane_reads == 0
    assert replay.capture.position_count == 2
    prepared = ij.prepare_calibrated_command(replay)
    expected = a.copy()
    if samples == 1:
        expected[1, 1, 0] = expected[1, 1, 2] = (
            ij.gaussian_blur_imagej(np.array([[0, 255]], np.float32), (.4 if policy == "global" else .75, 1))
            if dtype == np.float32 else [[77, 178]])
    else:
        expected[1, 1, 0] = expected[1, 1, 2] = [[[77, 178, 77], [178, 77, 178]]]
    np.testing.assert_array_equal(prepared.preview.dataset.numpy("TCZYXS" if samples == 3 else "TCZYX"), expected)
    assert owner.undo_steps == 0
    result = app.publish(prepared)
    assert result.created == derived
    assert owner.snapshot().dataset.calibration.spacing == [3, 4, 5]
    assert owner.undo_steps == (0 if derived else 1)
    if derived:
        assert result.document.snapshot().dataset.calibration.spacing == ([.5, 1.25, 2] if policy == "global" else [3, 4, 5])
        if policy == "global":
            assert ij.read_imagej_intensity_calibration(result.document.snapshot()).record == app.effective_calibration(owner.id).intensity.record
    else:
        owner.undo()
        np.testing.assert_array_equal(owner.snapshot().dataset.numpy("TCZYXS" if samples == 3 else "TCZYX"), a)


@pytest.mark.parametrize("transition", ["set", "clear", "bypass", "same_values"])
def test_stale_preparation_strict_replay_and_explicit_rebind(transition):
    app, owner, _, _ = setup()
    target = app.resolve_target(ij.TargetRequest(roi=ij.RoiPolicy.WHOLE_PLANE))
    options = ij.GaussianBlurCommandOptions(.5, True)
    command = ij.resolve_calibrated_command(target, "gaussian_blur_imagej", options)
    prepared = ij.prepare_calibrated_command(command)
    if transition in ("set", "same_values"):
        app.set_global_calibration(global_value(.75 if transition == "set" else .5))
    elif transition == "clear":
        app.clear_global_calibration()
    else:
        app.set_ignore_global_calibration(True, owner.id)
    with pytest.raises(ij.RevisionConflict):
        app.publish(prepared)
    current = app.resolve_target()
    if transition == "same_values":
        ij.calibrated_command_from_record(current, command.record())
    else:
        with pytest.raises(ValueError, match="rebind"):
            ij.calibrated_command_from_record(current, command.record())
    rebound = ij.rebind_calibrated_command(current, command.record(), "gaussian_blur_imagej",
                                           ij.GaussianBlurCommandOptions(3, True))
    app.publish(ij.prepare_calibrated_command(rebound))
    assert owner.undo_steps == 1 and owner.snapshot().dataset.calibration.spacing == [3, 4, 5]


@pytest.mark.parametrize("corruption", ["extra", "sigma", "effective", "source", "target", "dtype", "descriptor"])
def test_closed_descriptor_validates_original_before_rebinding(corruption):
    app, owner, reads, _ = setup()
    record = ij.resolve_calibrated_command(app.resolve_target(), "gaussian_blur_imagej",
                                          ij.GaussianBlurCommandOptions(.5, True)).record()
    bad = copy.deepcopy(record)
    d = bad["descriptor"]
    if corruption == "extra":
        d["extra"] = True
    elif corruption == "sigma":
        d["command"]["effective_sigma_xy"][0] += 1
    elif corruption == "effective":
        d["command"]["calibration"]["origin"][0] += 1
    elif corruption == "source":
        d["source"]["document_id"] += 1
    elif corruption == "target":
        d["command"]["target"]["document_id"] = 1
    elif corruption == "dtype":
        bad["source"]["pixel_type"] = d["source"]["pixel_type"] = "float64"
    else:
        d["operation"] = "project_imagej"
    with pytest.raises(ValueError):
        ij.validate_calibrated_command_record(bad)
    with pytest.raises(ValueError):
        ij.rebind_calibrated_command(app.resolve_target(), bad, "gaussian_blur_imagej",
                                      ij.GaussianBlurCommandOptions(1))
    assert owner.undo_steps == 0 and reads.counts().plane_reads == 0


def test_metadata_admission_runtime_cap_cancel_and_legacy_boundary():
    app, owner, reads, _ = setup()
    target = app.resolve_target()
    options = ij.GaussianBlurCommandOptions(.5, True)
    with pytest.raises(ValueError, match="budget"):
        ij.resolve_calibrated_command(target, "gaussian_blur_imagej", options,
                                      ij.CalibratedCommandLimits(max_working_bytes=1))
    command = ij.resolve_calibrated_command(target, "gaussian_blur_imagej", options)
    events = []
    with pytest.raises(ValueError, match="budget"):
        ij.prepare_calibrated_command(command, runtime_max_working_bytes=command.working_bytes-1,
                                      checkpoint=lambda: events.append(True))
    assert reads.counts().plane_reads == 0 and not events
    token = ij.CancellationToken()
    token.cancel()
    with pytest.raises(ij.OperationCancelled):
        ij.prepare_calibrated_command(command, cancellation=token)
    assert reads.counts().plane_reads == 0 and owner.undo_steps == 0
    legacy = ij.resolve_gaussian_command(owner.snapshot(), options=options).record()
    assert "schema" not in legacy and legacy["schema_version"] == 1
    restored = ij.gaussian_command_from_record(owner.snapshot(), legacy)
    assert restored.calibration.spacing == [3, 4, 5]
    assert not restored.target.application_calibration_bound


@pytest.mark.parametrize("whole_plane", [False, True])
def test_replay_selection_dependencies_match_frozen_roi_policy(whole_plane):
    app, owner, reads, _ = setup()
    request = ij.TargetRequest(roi=ij.RoiPolicy.WHOLE_PLANE if whole_plane else ij.RoiPolicy.ACTIVE_SELECTION)
    command = ij.resolve_calibrated_command(app.resolve_target(request), "gaussian_blur_imagej",
                                          ij.GaussianBlurCommandOptions(.5, True))
    owner.select(ij.Selection("rectangle", [(0, 0), (1, 1)]))
    if whole_plane:
        replay = ij.calibrated_command_from_record(app.resolve_target(), command.record())
        assert replay.record() == command.record()
        assert replay.record()["descriptor"]["command"]["target"]["source_revisions"][2] == 0
    else:
        with pytest.raises(ij.RevisionConflict):
            ij.calibrated_command_from_record(app.resolve_target(), command.record())
    assert reads.counts().plane_reads == 0


@pytest.mark.parametrize("policy", ["local", "ignored"])
def test_raw_local_gaussian_does_not_decode_future_density(policy):
    app, owner, reads, _ = setup(policy=policy)
    future = {"schema": "imagejxx.intensity-calibration", "version": 999,
              "mode": "none", "value_unit": "future"}
    edit = owner.begin_edit()
    edit.set_property("imagejxx.intensity_calibration", future)
    owner.commit(edit)
    command = ij.resolve_calibrated_command(app.resolve_target(), "gaussian_blur_imagej",
                                          ij.GaussianBlurCommandOptions(3, True))
    ij.validate_calibrated_command_record(command.record())
    assert command.record()["effective"] is None and reads.counts().plane_reads == 0
    app.publish(ij.prepare_calibrated_command(command))
    assert owner.snapshot().metadata["properties"]["imagejxx.intensity_calibration"] == future


def test_gaussian_request_and_record_are_independent_values():
    app, _, _, _ = setup()
    options = ij.GaussianBlurCommandOptions(.5, True)
    command = ij.resolve_calibrated_command(app.resolve_target(), "gaussian_blur_imagej", options)
    original = command.record()
    options.sigma = 9
    returned = command.request
    returned.sigma = 7
    changed = command.record()
    changed["descriptor"]["command"]["options"]["sigma"] = 42
    assert command.request.sigma == .5 and command.record() == original


@pytest.mark.parametrize("unit", ["px", "pixel", "pixels"])
def test_effective_pixel_units_preserve_unscaled_raw_sigma(unit):
    app, owner, reads, _ = setup()
    app.set_global_calibration(ij.GlobalCalibration(ij.SpatialCalibration(
        spacing=[.5, 1.25, 2], unit=unit), ij.ImageJIntensityCalibration.uncalibrated("OD"), "uint8"))
    command = ij.resolve_calibrated_command(app.resolve_target(), "gaussian_blur_imagej",
                                          ij.GaussianBlurCommandOptions(2, True))
    record = command.record()
    assert record["descriptor"]["command"]["options"] == {"sigma": 2, "scaled_units": True}
    assert record["descriptor"]["command"]["effective_sigma_xy"] == [2, 2]
    assert record["source"]["calibration"]["spacing"] == [3, 4, 5]
    ij.validate_calibrated_command_record(record)
    assert owner.undo_steps == 0 and reads.counts().plane_reads == 0
