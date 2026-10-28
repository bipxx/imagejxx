"""Captured calibration in dedicated factories; existing kernels remain the oracle.

These integration checks do not replace pinned ImageJ numerical fixtures.
"""
import imagejxx as ij
import numpy as np
import pytest


KINDS = ("axis_plane_export", "axis_range_reslice", "axis_range_orientation",
         "axis_range_spacing", "axis_range_slicer_resize", "imagej_projection")


def global_value(width=.5):
    return ij.GlobalCalibration(
        ij.SpatialCalibration(spacing=[width, 1.25, 2], origin=[11, -7, 19],
                              unit="um", frame_interval=.25, time_origin=8, time_unit="min"),
        ij.ImageJIntensityCalibration.function("uint8", "straight_line", [10, 2], "OD"),
        "uint8")


def setup(dtype=np.uint8, samples=1):
    a = (np.arange(3 * 4 * 5 * samples) % 251).astype(dtype).reshape(3, 4, 5, samples)
    owner = ij.Document(ij.Dataset(a, axes="ZYXS",
                                  calibration=ij.SpatialCalibration(spacing=[3, 4, 5])))
    app = ij.Application()
    app.adopt(owner)
    app.set_global_calibration(global_value())
    return app, owner, a


def request(kind, plane="xz", flags=(False, False, False)):
    if kind == "imagej_projection":
        r = ij.imagej_projection_defaults()
        r.update(first=0, last=2, method="maximum")
        return r
    if kind == "axis_plane_export":
        return ij.AxisPlaneExportRequest(plane, 1)
    r = ij.AxisRangeResliceRequest(plane, 0, 2)
    if kind == "axis_range_reslice":
        return r
    oriented = ij.AxisRangeOrientationRequest(
        r, reverse_order=flags[0], flip_vertical=flags[1], transpose=flags[2])
    if kind == "axis_range_orientation":
        return oriented
    if kind == "axis_range_spacing":
        return ij.AxisRangeSpacingRequest(oriented, physical_vertical_spacing=.5,
                                         sampling=ij.AxisRangeSampling.Linear)
    return ij.AxisRangeSlicerResizeRequest(oriented)


def calibration_record(c):
    return (c.spacing, c.origin, c.direction, c.unit,
            c.frame_interval, c.time_origin, c.time_unit)


@pytest.mark.parametrize("kind", KINDS)
@pytest.mark.parametrize("dtype,samples", [(np.uint8, 1), (np.uint16, 1), (np.float32, 1), (np.uint8, 3)])
def test_effective_pixels_geometry_density_and_original_source(kind, dtype, samples):
    app, owner, a = setup(dtype, samples)
    before = owner.snapshot()
    effective = app.effective_calibration(owner.id)
    oracle_owner = ij.Document(ij.Dataset(a, axes="ZYXS", calibration=effective.spatial))
    ij.set_imagej_intensity_calibration(oracle_owner, effective.intensity)
    r = request(kind)
    if kind == "axis_range_slicer_resize" and samples != 1:
        # Existing finite-scalar Slicer contract: RGB support is a separate open gap.
        with pytest.raises(ValueError, match="finite scalar"):
            ij.prepare_axis_range_slicer_resize(oracle_owner.snapshot(), r)
        with pytest.raises(ValueError, match="finite scalar"):
            app.prepare_axis_range_slicer_resize(r)
        assert owner.snapshot().shares_data(before) and owner.undo_steps == 0
        return
    expected = getattr(ij, "prepare_" + kind)(oracle_owner.snapshot(), r).prepared_new_document().snapshot()
    prepared = getattr(app, "prepare_" + kind)(r)
    target = prepared.target
    candidate = prepared.prepared_new_document().snapshot()
    assert target.application_calibration_bound and target.calibration_context.uses_global
    assert target.source.id == owner.id and target.source.shares_data(before)
    assert candidate.metadata["title"] == expected.metadata["title"]
    assert calibration_record(candidate.dataset.calibration) == calibration_record(expected.dataset.calibration)
    np.testing.assert_array_equal(candidate.dataset.numpy("TCZYXS"), expected.dataset.numpy("TCZYXS"))
    density = ij.read_imagej_intensity_calibration(candidate)
    expected_density = ij.read_imagej_intensity_calibration(expected) if kind == "imagej_projection" else effective.intensity
    assert density.record == expected_density.record
    published = app.publish(prepared)
    assert published.created and published.document.id != owner.id
    assert owner.snapshot().shares_data(before) and owner.undo_steps == 0
    assert owner.snapshot().dataset.calibration.spacing == [3, 4, 5]


@pytest.mark.parametrize("kind", KINDS)
@pytest.mark.parametrize("transition", ["set", "clear", "bypass_roundtrip"])
def test_stale_prepared_factory_cannot_adopt_candidate(kind, transition):
    app, owner, _ = setup()
    before = owner.snapshot()
    prepared = getattr(app, "prepare_" + kind)(request(kind))
    if transition == "set":
        app.set_global_calibration(global_value(.75))
    elif transition == "clear":
        app.clear_global_calibration()
    else:
        app.set_ignore_global_calibration(True, owner.id)
        app.set_ignore_global_calibration(False, owner.id)
    with pytest.raises(ij.RevisionConflict):
        app.publish(prepared)
    assert app.active_document_id == owner.id
    assert owner.snapshot().shares_data(before) and owner.undo_steps == 0


@pytest.mark.parametrize("kind", KINDS)
def test_context_is_frozen_before_processing_callbacks(kind):
    app, owner, _ = setup()
    changed = []

    def progress(done, total):
        if not changed:
            app.set_global_calibration(global_value(.75))
            changed.append(True)

    prepared = getattr(app, "prepare_" + kind)(request(kind), progress=progress)
    assert changed and prepared.target.calibration.spacing[0] == .5
    with pytest.raises(ij.RevisionConflict):
        app.publish(prepared)
    assert owner.undo_steps == 0


@pytest.mark.parametrize("kind", KINDS)
def test_bypass_restores_local_geometry_and_ignores_global_updates(kind):
    app, owner, _ = setup()
    app.set_ignore_global_calibration(True, owner.id)
    r = request(kind)
    expected = getattr(ij, "prepare_" + kind)(owner.snapshot(), r).prepared_new_document().snapshot()
    prepared = getattr(app, "prepare_" + kind)(r)
    assert prepared.target.application_calibration_bound and prepared.target.calibration_context is None
    app.set_global_calibration(global_value(.75))
    result = app.publish(prepared).document.snapshot()
    assert calibration_record(result.dataset.calibration) == calibration_record(expected.dataset.calibration)
    np.testing.assert_array_equal(result.dataset.numpy("TCZYXS"), expected.dataset.numpy("TCZYXS"))


@pytest.mark.parametrize("plane", ["xy", "xz", "yz"])
@pytest.mark.parametrize("flags", [(False, False, False), (True, True, True)])
def test_oriented_effective_world_mapping_uses_existing_axis_remapper(plane, flags):
    app, owner, a = setup()
    r = request("axis_range_orientation", plane, flags)
    expected = ij.reslice_oriented_axis_range(
        ij.Dataset(a, axes="ZYXS", calibration=global_value().spatial), r)
    actual = app.prepare_axis_range_orientation(r).prepared_new_document().snapshot()
    assert calibration_record(actual.dataset.calibration) == calibration_record(expected.dataset.calibration)
    np.testing.assert_array_equal(actual.dataset.numpy("TCZYXS"), expected.dataset.numpy("TCZYXS"))


def test_projection_charges_global_density_before_callbacks_or_plane_reads():
    reads = ij.DatasetReadObserver()
    owner = ij.Document(ij.Dataset(np.zeros((3, 2, 3), np.uint16), axes="ZYX").observe_reads(reads))
    app = ij.Application()
    app.adopt(owner)
    density = ij.ImageJIntensityCalibration.function("uint16", "straight_line", [10, 2], "OD")
    app.set_global_calibration(ij.GlobalCalibration(ij.SpatialCalibration(), density, "uint16"))
    r = request("imagej_projection")
    r["max_record_bytes"] = 65536
    progress = []
    with pytest.raises(ValueError, match="budget"):
        app.prepare_imagej_projection(r, progress=lambda done, total: progress.append((done, total)))
    assert not progress and reads.counts().plane_reads == 0 and owner.undo_steps == 0


@pytest.mark.parametrize("axis", ["z", "time", "channels"])
@pytest.mark.parametrize("group_size", [0, 1])
def test_projection_captures_effective_metadata_across_scientific_axes(axis, group_size):
    a = (np.arange(3 * 3 * 3 * 2 * 3) % 251).astype(np.uint8).reshape(3, 3, 3, 2, 3)
    owner = ij.Document(ij.Dataset(a, axes="TCZYX"))
    app = ij.Application()
    app.adopt(owner)
    g = global_value()
    app.set_global_calibration(g)
    oracle_owner = ij.Document(ij.Dataset(a, axes="TCZYX", calibration=g.spatial))
    ij.set_imagej_intensity_calibration(oracle_owner, g.intensity)
    r = request("imagej_projection")
    r.update(axis=axis, group_size=group_size)
    expected = ij.prepare_imagej_projection(oracle_owner.snapshot(), r).prepared_new_document().snapshot()
    before = owner.snapshot()
    prepared = app.prepare_imagej_projection(r)
    actual = app.publish(prepared).document.snapshot()
    assert calibration_record(actual.dataset.calibration) == calibration_record(expected.dataset.calibration)
    np.testing.assert_array_equal(actual.dataset.numpy("TCZYXS"), expected.dataset.numpy("TCZYXS"))
    assert ij.read_imagej_intensity_calibration(actual).record == ij.read_imagej_intensity_calibration(expected).record
    assert owner.snapshot().shares_data(before) and owner.undo_steps == 0
