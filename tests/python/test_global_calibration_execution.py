"""Effective calibration in application processing; detached snapshots stay local."""
import imagejxx as ij
import numpy as np
import pytest


def setup():
    pixels = np.arange(17 * 23, dtype=np.uint8).reshape(17, 23)
    owner = ij.Document(ij.Dataset(pixels, axes="YX"))
    app = ij.Application()
    app.adopt(owner)
    return app, owner, pixels


def global_value(width=.5):
    return ij.GlobalCalibration(
        ij.SpatialCalibration(spacing=[width, 2, 3], unit="um"),
        ij.ImageJIntensityCalibration.function("uint8", "straight_line", [10, 2], "OD"),
        "uint8")


@pytest.mark.parametrize("queued", [False, True])
@pytest.mark.parametrize("derived", [False, True])
def test_changed_global_refuses_publication_without_source_mutation(queued, derived):
    app, owner, pixels = setup()
    app.set_global_calibration(global_value())
    request = ij.TargetRequest(destination=ij.OutputDestination.NEW_DOCUMENT if derived
                               else ij.OutputDestination.IN_PLACE)
    target = app.resolve_target(request)
    before = owner.snapshot()
    pending = (app.submit if queued else app.prepare)("add", target, {"value": 1})
    if queued:
        assert pending.wait(2000) and pending.info.state == ij.JobState.READY
    app.set_global_calibration(global_value(.75))
    with pytest.raises(ij.RevisionConflict):
        app.publish(pending)
    assert owner.snapshot().shares_data(before) and owner.undo_steps == 0
    assert app.active_document_id == owner.id
    np.testing.assert_array_equal(owner.snapshot().dataset.numpy("YX"), pixels)
    if queued:
        assert app.job_scheduler.usage.reserved_bytes == 0


@pytest.mark.parametrize("action", ["prepare", "submit"])
def test_enabling_global_invalidates_preexisting_local_application_target(action):
    app, owner, _ = setup()
    target = app.resolve_target()
    assert target.application_calibration_bound and target.calibration_context is None
    app.set_global_calibration(global_value())
    with pytest.raises(ij.RevisionConflict):
        getattr(app, action)("add", target, {"value": 1})
    assert owner.undo_steps == 0


def test_bypass_is_per_document_and_ignored_targets_survive_global_changes():
    app, owner, _ = setup()
    app.set_global_calibration(global_value())
    app.set_ignore_global_calibration(True, owner.id)
    target = app.resolve_target()
    pending = app.prepare("add", target, {"value": 1})
    other = app.create(ij.Dataset(np.zeros((1, 2), np.uint8), axes="YX"))
    app.set_ignore_global_calibration(True, other)
    app.set_global_calibration(global_value(.75))
    app.publish(pending)
    assert owner.undo_steps == 1 and target.calibration.spacing == [1, 1, 1]


def test_bypass_round_trip_invalidates_prepared_work():
    app, owner, _ = setup()
    app.set_global_calibration(global_value())
    pending = app.prepare("add", app.resolve_target(), {"value": 1})
    app.set_ignore_global_calibration(True, owner.id)
    app.set_ignore_global_calibration(False, owner.id)
    with pytest.raises(ij.RevisionConflict):
        app.publish(pending)
    assert owner.undo_steps == 0


def test_foreign_application_cannot_use_bound_target_but_detached_is_local():
    app, owner, _ = setup()
    app.set_global_calibration(global_value())
    target = app.resolve_target()
    other = ij.Application()
    other.adopt(owner)
    with pytest.raises(ValueError):
        other.prepare("add", target, {"value": 1})
    detached = ij.resolve_target(owner.snapshot(), ij.TargetRequest())
    assert not detached.application_calibration_bound
    assert detached.calibration.spacing == [1, 1, 1]
    app.publish(app.prepare("add", detached, {"value": 1}))
    assert owner.snapshot().dataset.calibration.spacing == [1, 1, 1]


@pytest.mark.parametrize("derived", [False, True])
def test_output_metadata_is_effective_only_for_derived_images(derived):
    app, owner, _ = setup()
    value = global_value()
    app.set_global_calibration(value)
    request = ij.TargetRequest(destination=ij.OutputDestination.NEW_DOCUMENT if derived
                               else ij.OutputDestination.IN_PLACE)
    target = app.resolve_target(request)
    assert target.calibration_context.uses_global
    assert target.intensity_calibration().record == value.intensity.record
    published = app.publish(app.prepare("add", target, {"value": 1}))
    result = published.document.snapshot()
    assert result.dataset.calibration.spacing == ([.5, 2, 3] if derived else [1, 1, 1])
    density = ij.read_imagej_intensity_calibration(result)
    assert density.has_density == derived
    if derived:
        assert density.record == value.intensity.record
        assert owner.undo_steps == 0


def test_scaled_gaussian_uses_effective_spacing_and_executes_matching_pixels():
    app, owner, pixels = setup()
    app.set_global_calibration(global_value())
    target = app.resolve_target(operation="gaussian_blur")
    command = ij.resolve_gaussian_command(target, options=ij.GaussianBlurCommandOptions(2, True))
    assert command.parameters() == {"sigma": [1.0, 4.0]}
    calibrated = ij.Document(ij.Dataset(pixels, axes="YX", calibration=global_value().spatial))
    oracle = ij.resolve_gaussian_command(calibrated.snapshot(), options=ij.GaussianBlurCommandOptions(2, True))
    oracle_app = ij.Application()
    oracle_app.adopt(calibrated)
    expected = oracle_app.prepare("gaussian_blur", oracle.target, oracle.parameters())
    actual = app.prepare("gaussian_blur", command.target, command.parameters())
    np.testing.assert_array_equal(actual.preview.dataset.numpy("YX"), expected.preview.dataset.numpy("YX"))
    app.publish(actual)
    assert owner.snapshot().dataset.calibration.spacing == [1, 1, 1]


@pytest.mark.parametrize("dtype,unit", [("uint16", "Gray Value"), ("float32", "OD"), ("float64", "OD")])
def test_derived_storage_conversion_adapts_density(dtype, unit):
    app, owner, _ = setup()
    app.set_global_calibration(global_value())
    target = app.resolve_target(ij.TargetRequest(destination=ij.OutputDestination.NEW_DOCUMENT))
    result = app.publish(app.prepare("convert", target, {"dtype": dtype})).document.snapshot()
    density = ij.read_imagej_intensity_calibration(result)
    assert not density.has_density and density.value_unit == unit
    assert result.dataset.calibration.spacing == [.5, 2, 3]
    assert owner.undo_steps == 0


def test_closed_source_refuses_prepared_global_publication():
    app, owner, _ = setup()
    app.set_global_calibration(global_value())
    prepared = app.prepare("add", app.resolve_target(), {"value": 1})
    app.close(owner.id, discard=True)
    with pytest.raises(ij.DocumentUnavailable):
        app.publish(prepared)
    assert owner.undo_steps == 0


@pytest.mark.parametrize("ignored", [False, True])
def test_raw_local_processing_preserves_unknown_density_metadata(ignored):
    app, owner, _ = setup()
    future = {"schema": "imagejxx.intensity-calibration", "version": 999,
              "mode": "none", "value_unit": "future"}
    edit = owner.begin_edit()
    edit.set_property("imagejxx.intensity_calibration", future)
    owner.commit(edit)
    if ignored:
        app.set_global_calibration(global_value())
        app.set_ignore_global_calibration(True, owner.id)
    target = app.resolve_target()
    assert target.calibration_context is None
    app.publish(app.prepare("add", target, {"value": 1}))
    assert owner.snapshot().metadata["properties"]["imagejxx.intensity_calibration"] == future


def test_target_record_roundtrip_is_explicitly_detached_and_local():
    app, owner, _ = setup()
    app.set_global_calibration(global_value())
    target = app.resolve_target()
    restored = ij.target_from_record(owner.snapshot(), target.record())
    assert restored.record() == target.record()
    assert not restored.application_calibration_bound and restored.calibration_context is None
    assert restored.calibration.spacing == [1, 1, 1]
    assert target.calibration.spacing == [.5, 2, 3]
