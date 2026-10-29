"""Explicit source/effective provenance and current-capture replay of commands."""
import copy
import json
import imagejxx as ij
import numpy as np
import pytest
import test_global_calibration_consumers as fixtures


OPERATIONS = tuple("project_imagej" if k == "imagej_projection" else k for k in fixtures.KINDS)


def request(operation):
    return fixtures.request("imagej_projection" if operation == "project_imagej" else operation)


def capture(app, owner):
    return app.resolve_target(document_id=owner.id)


@pytest.mark.parametrize("operation", OPERATIONS)
@pytest.mark.parametrize("policy", ["global", "local", "ignored", "detached"])
def test_record_replay_geometry_pixels_and_binding_policy(operation, policy):
    app, owner, _ = fixtures.setup()
    if policy == "local":
        app.clear_global_calibration()
    elif policy == "ignored":
        app.set_ignore_global_calibration(True, owner.id)
    target = ij.resolve_target(owner.snapshot()) if policy == "detached" else capture(app, owner)
    command = ij.resolve_calibrated_command(target, operation, request(operation))
    record = json.loads(json.dumps(command.record()))
    assert record["schema"] == "imagejxx.calibrated-command"
    assert record["binding"] == ("detached" if policy == "detached" else "application")
    assert record["bypass"] == (policy == "ignored")
    assert (record["effective"] is not None) == (policy == "global")
    assert record["source"]["calibration"]["spacing"] == [3, 4, 5]
    assert record["descriptor"]["source"]["calibration"]["spacing"] == ([.5, 1.25, 2] if policy == "global" else [3, 4, 5])
    assert "global_generation" not in record and "application_id" not in record
    ij.validate_calibrated_command_record(record)
    replay = ij.calibrated_command_from_record(target, record)
    assert replay.record() == record and replay.working_bytes == command.working_bytes
    prepared = ij.prepare_calibrated_command(replay)
    kind = "imagej_projection" if operation == "project_imagej" else operation
    expected = (getattr(ij, "prepare_" + kind)(owner.snapshot(), request(operation)) if policy == "detached"
                else getattr(app, "prepare_" + kind)(request(operation)))
    actual = prepared.prepared_new_document().snapshot()
    oracle = expected.prepared_new_document().snapshot()
    assert fixtures.calibration_record(actual.dataset.calibration) == fixtures.calibration_record(oracle.dataset.calibration)
    assert list(actual.dataset.shape) == record["descriptor"]["derived"]["shape"]
    np.testing.assert_array_equal(actual.dataset.numpy("TCZYXS"), oracle.dataset.numpy("TCZYXS"))
    assert ij.read_imagej_intensity_calibration(actual).record == ij.read_imagej_intensity_calibration(oracle).record
    assert prepared.target.source.id == owner.id
    app.publish(prepared)
    assert owner.undo_steps == 0 and owner.snapshot().dataset.calibration.spacing == [3, 4, 5]


@pytest.mark.parametrize("operation", OPERATIONS)
@pytest.mark.parametrize("transition", ["global", "clear", "bypass"])
def test_preparation_uses_frozen_context_and_publication_refuses_stale_work(operation, transition):
    app, owner, _ = fixtures.setup()
    command = ij.resolve_calibrated_command(capture(app, owner), operation, request(operation))
    if transition == "global":
        app.set_global_calibration(fixtures.global_value(.75))
    elif transition == "clear":
        app.clear_global_calibration()
    else:
        app.set_ignore_global_calibration(True, owner.id)
    prepared = ij.prepare_calibrated_command(command)
    assert command.capture.calibration.spacing[0] == .5
    with pytest.raises(ij.RevisionConflict):
        app.publish(prepared)
    assert owner.undo_steps == 0 and app.active_document_id == owner.id


@pytest.mark.parametrize("operation", OPERATIONS)
def test_explicit_rebind_replaces_context_but_never_repairs_bad_original_claims(operation):
    app, owner, _ = fixtures.setup()
    command = ij.resolve_calibrated_command(capture(app, owner), operation, request(operation))
    record = command.record()
    app.set_global_calibration(fixtures.global_value(.75))
    current = capture(app, owner)
    with pytest.raises(ValueError, match="rebind"):
        ij.calibrated_command_from_record(current, record)
    rebound = ij.rebind_calibrated_command(current, record, operation, request(operation))
    assert rebound.capture.calibration.spacing[0] == .75
    assert rebound.record()["effective"]["spatial"]["spacing"][0] == .75
    bad = copy.deepcopy(record)
    bad["descriptor"]["derived"]["shape"][4] += 1
    with pytest.raises(ValueError):
        ij.rebind_calibrated_command(current, bad, operation, request(operation))
    app.publish(ij.prepare_calibrated_command(rebound))
    assert owner.undo_steps == 0


@pytest.mark.parametrize("operation", OPERATIONS)
@pytest.mark.parametrize("corruption", ["extra", "version", "binding", "bypass", "spatial", "source", "derived", "limits"])
def test_contradictory_records_are_rejected_without_mutation(operation, corruption):
    app, owner, _ = fixtures.setup()
    before = owner.snapshot()
    record = ij.resolve_calibrated_command(capture(app, owner), operation, request(operation)).record()
    if corruption == "extra":
        record["global_generation"] = 1
    elif corruption == "version":
        record["version"] = True
    elif corruption == "binding":
        record["binding"] = "detached"
    elif corruption == "bypass":
        record["bypass"] = True
    elif corruption == "spatial":
        record["effective"]["spatial"]["spacing"][0] = .75
    elif corruption == "source":
        record["source"]["document_id"] += 1
    elif corruption == "derived":
        record["descriptor"]["derived"]["shape"][3] += 1
    else:
        record["limits"]["max_record_bytes"] = 0
    with pytest.raises(ValueError):
        ij.validate_calibrated_command_record(record)
    assert owner.snapshot().shares_data(before) and owner.undo_steps == 0


@pytest.mark.parametrize("operation", OPERATIONS)
def test_metadata_admission_and_tight_runtime_caps_are_read_free(operation):
    app, _, a = fixtures.setup()
    reads = ij.DatasetReadObserver()
    owner = ij.Document(ij.Dataset(a, axes="ZYXS").observe_reads(reads))
    app.adopt(owner)
    target = capture(app, owner)
    with pytest.raises(ValueError, match="budget"):
        ij.resolve_calibrated_command(target, operation, request(operation),
                                      ij.CalibratedCommandLimits(max_working_bytes=1))
    command = ij.resolve_calibrated_command(target, operation, request(operation))
    ij.validate_calibrated_command_record(command.record())
    ij.calibrated_command_from_record(target, command.record())
    events = []
    with pytest.raises(ValueError, match="budget"):
        ij.prepare_calibrated_command(command, runtime_max_working_bytes=command.working_bytes - 1,
                                      progress=lambda done, total: events.append((done, total)))
    assert reads.counts().plane_reads == 0 and not events and owner.undo_steps == 0
    prepared = ij.prepare_calibrated_command(command, runtime_max_working_bytes=command.working_bytes)
    assert prepared.prepared_new_document() is not None


@pytest.mark.parametrize("operation", OPERATIONS)
def test_returned_requests_and_records_do_not_mutate_command(operation):
    app, owner, _ = fixtures.setup()
    r = request(operation)
    command = ij.resolve_calibrated_command(capture(app, owner), operation, r)
    before = command.record()
    returned = command.request
    if operation == "project_imagej":
        returned["first"] = r["first"] = 999
    elif operation == "axis_plane_export":
        returned.coordinate = r.coordinate = 999
    elif operation == "axis_range_reslice":
        returned.first = r.first = 999
    elif operation == "axis_range_orientation":
        returned.range.first = r.range.first = 999
    else:
        returned.orientation.range.first = r.orientation.range.first = 999
    detached_record = command.record()
    detached_record["effective"]["spatial"]["spacing"][0] = 999
    assert command.record() == before
    assert ij.prepare_calibrated_command(command).prepared_new_document() is not None


def test_same_value_global_reset_stales_old_work_but_fresh_capture_can_replay():
    app, owner, _ = fixtures.setup()
    command = ij.resolve_calibrated_command(capture(app, owner), "axis_plane_export", request("axis_plane_export"))
    prepared = ij.prepare_calibrated_command(command)
    app.set_global_calibration(fixtures.global_value())
    with pytest.raises(ij.RevisionConflict):
        app.publish(prepared)
    replay = ij.calibrated_command_from_record(capture(app, owner), command.record())
    assert replay.record() == command.record()
    app.publish(ij.prepare_calibrated_command(replay))


def test_application_identity_is_minted_from_explicit_capture_not_serialized():
    app, owner, _ = fixtures.setup()
    command = ij.resolve_calibrated_command(capture(app, owner), "axis_plane_export", request("axis_plane_export"))
    other = ij.Application()
    other.adopt(owner)
    other.set_global_calibration(fixtures.global_value())
    with pytest.raises(ValueError):
        other.publish(ij.prepare_calibrated_command(command))
    replay = ij.calibrated_command_from_record(capture(other, owner), command.record())
    other.publish(ij.prepare_calibrated_command(replay))
    with pytest.raises(ValueError):
        ij.calibrated_command_from_record(ij.resolve_target(owner.snapshot()), command.record())


def test_lookup_record_keeps_ieee_payload_words_through_json_replay():
    app, owner, _ = fixtures.setup()
    words = [0x7fc12345, 0x80000000, 0x7f800000, 0x3f800000] * 64
    density = ij.ImageJIntensityCalibration.from_record(dict(schema="imagejxx.intensity-calibration",
        version=1, mode="lookup", value_unit="raw", bit_depth=8,
        table_f32_hex="".join(f"{word:08x}" for word in words), source_parameters={}, signed16=False))
    app.set_global_calibration(ij.GlobalCalibration(fixtures.global_value().spatial, density, "uint8"))
    command = ij.resolve_calibrated_command(capture(app, owner), "axis_plane_export", request("axis_plane_export"))
    record = json.loads(json.dumps(command.record()))
    replay = ij.calibrated_command_from_record(capture(app, owner), record)
    result = ij.prepare_calibrated_command(replay).prepared_new_document().snapshot()
    assert record["effective"]["intensity"] == density.record
    assert ij.read_imagej_intensity_calibration(result).table_words() == words


@pytest.mark.parametrize("name", ["max_record_bytes", "max_working_bytes"])
def test_boolean_limits_are_rejected(name):
    with pytest.raises(TypeError, match="Boolean"):
        ij.CalibratedCommandLimits(**{name: True})


@pytest.mark.parametrize("operation", OPERATIONS[:-1])
def test_raw_local_axis_command_does_not_decode_unknown_density(operation):
    app, owner, _ = fixtures.setup()
    app.clear_global_calibration()
    edit = owner.begin_edit()
    future = {"schema": "imagejxx.intensity-calibration", "version": 999, "mode": "future"}
    edit.set_property("imagejxx.intensity_calibration", future)
    owner.commit(edit)
    before, steps = owner.snapshot(), owner.undo_steps
    command = ij.resolve_calibrated_command(capture(app, owner), operation, request(operation))
    assert command.record()["effective"] is None
    replay = ij.calibrated_command_from_record(capture(app, owner), command.record())
    app.publish(ij.prepare_calibrated_command(replay))
    assert owner.snapshot().shares_data(before) and owner.undo_steps == steps


def test_rebind_cannot_change_operation_family():
    app, owner, _ = fixtures.setup()
    command = ij.resolve_calibrated_command(capture(app, owner), "axis_plane_export", request("axis_plane_export"))
    with pytest.raises(ValueError, match="family"):
        ij.rebind_calibrated_command(capture(app, owner), command.record(), "axis_range_reslice", request("axis_range_reslice"))
