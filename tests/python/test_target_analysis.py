"""Read-only captured analysis; independent membership/affine/lifecycle checks."""
import gc
import math
import numpy as np
import pytest
import imagejxx as ij
from test_target_histogram import fixture
from test_global_calibration_execution import global_value


@pytest.mark.parametrize("dtype", [np.uint8, np.uint16, np.float32])
@pytest.mark.parametrize("policy", ["whole", "active", "positioned"])
@pytest.mark.parametrize("use_global", [False, True])
def test_grid_membership_calibration_order_and_no_edits(dtype, policy, use_global):
    owner, reads, detached, pixels, _ = fixture(dtype, policy)
    app = ij.Application()
    app.adopt(owner)
    if use_global:
        app.set_global_calibration(global_value())
    request = ij.TargetRequest(ij.TargetScope.RANGES, t=ij.AxisRange(0, 2),
                              c=ij.AxisRange(1, 2), z=ij.AxisRange(1, 4, 2),
                              roi=detached.roi_policy)
    target = app.resolve_target(request)
    n = reads.counts().plane_reads
    before = owner.snapshot()
    estimate = ij.estimate_target_analysis(target)
    command = ij.resolve_target_analysis(target)
    assert reads.counts().plane_reads == n
    assert estimate.plane_count == 4 and estimate.total_pixels == 64
    assert estimate.plot_bytes >= estimate.result_bytes
    assert estimate.working_bytes == command.estimate.working_bytes
    view = owner.snapshot().display
    view.t, view.c, view.crosshair = 0, 0, [0, 0, 2]
    owner.set_display(view)
    result = ij.execute_target_analysis(command)
    assert reads.counts().plane_reads == n + 4
    for row, (t, z) in zip(result.planes, [(0, 1), (0, 3), (1, 1), (1, 3)]):
        assert (row.position.t, row.position.c, row.position.z) == (t, 1, z)
        members = pixels[t, 1, z]
        if policy == "active":
            members = members[:, :2]
        elif policy == "positioned" and z == 3:
            members = members[:, 2:] if t == 0 else members[:0]
        values = members.astype(np.float64).ravel()
        if use_global and dtype == np.uint8:
            values = 10 + 2 * values
        stats = row.statistics
        assert stats["pixel_count"] == values.size
        assert stats["area"] == values.size  # Global .5 * 2 = local 1 * 1.
        assert sum(stats["histogram"]) == values.size
        if values.size:
            assert stats["mean"] == pytest.approx(values.mean())
            assert stats["stddev"] == pytest.approx(values.std(ddof=1))
            assert stats["minimum"] == values.min() and stats["maximum"] == values.max()
        else:
            assert math.isnan(stats["mean"])
        stats["histogram"][:] = 0
        assert sum(row.statistics["histogram"]) == values.size
    app.validate_target(command.target)
    assert owner.snapshot().shares_data(before) and owner.undo_steps == 0
    assert result.record == command.record and result.record["target"] == target.record()


@pytest.mark.parametrize("what", ["max_pixels", "max_working_bytes", "max_record_bytes"])
def test_metadata_limits_and_runtime_cap_refuse_without_reads_or_callbacks(what):
    owner, reads, target, _, _ = fixture(np.uint16, "positioned")
    n = reads.counts().plane_reads
    estimate = ij.estimate_target_analysis(target)
    limits = ij.TargetAnalysisLimits()
    setattr(limits, what, {"max_pixels": estimate.total_pixels - 1,
                          "max_working_bytes": estimate.working_bytes - 1,
                          "max_record_bytes": 1}[what])
    for prepare in [ij.estimate_target_analysis, ij.resolve_target_analysis]:
        with pytest.raises(ValueError):
            prepare(target, limits=limits)
    assert reads.counts().plane_reads == n
    command = ij.resolve_target_analysis(target)
    calls = []
    with pytest.raises(ValueError, match="working byte"):
        ij.execute_target_analysis(command, checkpoint=lambda: calls.append(1),
                                   runtime_max_working_bytes=estimate.working_bytes - 1)
    assert not calls and reads.counts().plane_reads == n
    limits = ij.TargetAnalysisLimits()
    limits.max_working_bytes = estimate.working_bytes
    assert ij.resolve_target_analysis(target, limits=limits).estimate.working_bytes == estimate.working_bytes


@pytest.mark.parametrize("interpolate", [False, True])
def test_profile_only_nonarea_selection_affine_axis_and_frozen_inputs(interpolate):
    spatial = ij.SpatialCalibration(spacing=[.5, 2, 3], unit="um",
                                    direction=[1, .25, 0, 0, 1, 0, 0, 0, 1])
    reads = ij.DatasetReadObserver()
    owner = ij.Document(ij.Dataset(np.array([[0, 1, 2], [3, 4, 5]], np.uint8),
                                  axes="YX", calibration=spatial).observe_reads(reads))
    edit = owner.begin_edit()
    edit.select(ij.Selection("line", [(0, 0), (2, 1)]))
    owner.commit(edit)
    app = ij.Application()
    app.adopt(owner)
    options = ij.TargetAnalysisOptions()
    options.statistics, options.line, options.interpolate = False, [0, 0, 2, 1], interpolate
    n = reads.counts().plane_reads
    command = ij.resolve_target_analysis(app.resolve_target(), options)
    assert command.estimate.mask_bytes == 0 and reads.counts().plane_reads == n
    options.line = [0, 0, 0, 0]
    exposed = command.spatial
    spacing_copy = exposed.spacing
    spacing_copy[0] = 99
    assert command.spatial.spacing[0] == .5
    result = ij.execute_target_analysis(command)
    assert result.planes[0].statistics is None
    np.testing.assert_array_equal(result.planes[0].profile_values, [0, 2.5 if interpolate else 4, 5])
    # Spatial affine vector = [2*.5 + .25*1*2, 1*2, 0].
    np.testing.assert_allclose(result.profile_distances, [0, 1.25, 2.5])
    result.profile_distances[:] = 999
    assert result.profile_distances[-1] == 2.5
    result.planes[0].profile_values[:] = 999
    assert result.planes[0].profile_values[0] == 0
    edit = owner.begin_edit()
    edit.select(ij.Selection("line", [(0, 0), (1, 0)]))
    owner.commit(edit)
    with pytest.raises(ij.RevisionConflict):
        app.validate_target(command.target)


@pytest.mark.parametrize("bypass", [False, True])
def test_global_change_during_execution_and_adoption_guard(bypass):
    owner, reads, _, _, _ = fixture()
    app = ij.Application()
    app.adopt(owner)
    app.set_global_calibration(global_value(.25))
    if bypass:
        app.set_ignore_global_calibration(True, owner.id)
    request = ij.TargetRequest(roi=ij.RoiPolicy.WHOLE_PLANE)
    command = ij.resolve_target_analysis(app.resolve_target(request))
    before = owner.snapshot()
    def progress(done, total):
        if done == 0:
            app.set_global_calibration(global_value(.75))
    result = ij.execute_target_analysis(command, progress=progress)
    assert result.spatial.spacing[0] == (1 if bypass else .25)
    assert result.planes[0].statistics["area"] == 16 * (1 if bypass else .5)
    if bypass:
        app.validate_target(command.target)
    else:
        with pytest.raises(ij.RevisionConflict):
            app.validate_target(command.target)
    assert owner.snapshot().shares_data(before) and owner.undo_steps == 0


@pytest.mark.parametrize("point", [[-1, 0, 1, 1], [0, 0, 99, 1], [0, 0, float("nan"), 1]])
def test_bad_profile_admission(point):
    _, reads, target, _, _ = fixture()
    n = reads.counts().plane_reads
    options = ij.TargetAnalysisOptions()
    options.line = point
    with pytest.raises(ValueError, match="endpoints"):
        ij.resolve_target_analysis(target, options)
    assert reads.counts().plane_reads == n


def test_cancellation_callbacks_and_no_partial_result():
    owner, reads, target, _, _ = fixture()
    command = ij.resolve_target_analysis(target)
    n = reads.counts().plane_reads
    token = ij.CancellationToken()
    token.cancel()
    with pytest.raises(ij.OperationCancelled):
        ij.execute_target_analysis(command, cancellation=token,
                                   checkpoint=lambda: pytest.fail("pre-cancel callback"))
    assert reads.counts().plane_reads == n
    token = ij.CancellationToken()
    def progress(done, total):
        if done == 1:
            token.cancel()
    with pytest.raises(ij.OperationCancelled):
        ij.execute_target_analysis(command, cancellation=token, progress=progress)
    assert reads.counts().plane_reads == n + 1
    def fail():
        raise RuntimeError("analysis checkpoint")
    with pytest.raises(RuntimeError, match="analysis checkpoint"):
        ij.execute_target_analysis(command, checkpoint=fail)
    assert reads.counts().plane_reads == n + 1 and owner.undo_steps == 0


def test_result_survives_owner_release_and_is_detached():
    owner, _, target, _, _ = fixture()
    command = ij.resolve_target_analysis(target)
    result = ij.execute_target_analysis(command)
    expected = result.planes[0].statistics["mean"]
    del command, target, owner
    gc.collect()
    assert result.planes[0].statistics["mean"] == expected
    record = result.record
    record["target"]["ranges"][0][0] = 99
    assert result.record["target"]["ranges"][0][0] == 0


def test_simultaneous_statistics_profile_and_exact_admission():
    owner, reads, target, pixels, _ = fixture(np.uint16, "positioned")
    options = ij.TargetAnalysisOptions()
    options.line = [0, 0, 3, 0]
    ij.set_imagej_intensity_calibration(owner, ij.ImageJIntensityCalibration.signed16("signed"))
    # Recapture after metadata replacement; preserve the same explicit target.
    target = ij.resolve_target(owner.snapshot(),
        ij.TargetRequest(ij.TargetScope.POSITIONS, positions=[ij.PlanePosition(0, 1, 1),
            ij.PlanePosition(1, 1, 3)], roi=ij.RoiPolicy.POSITIONED_ROIS))
    estimate = ij.estimate_target_analysis(target, options)
    limits = ij.TargetAnalysisLimits()
    limits.max_working_bytes = estimate.working_bytes
    result = ij.execute_target_analysis(ij.resolve_target_analysis(target, options, limits))
    assert result.planes[0].statistics["pixel_count"] == 16
    assert result.planes[1].statistics["pixel_count"] == 0
    for row in result.planes:
        np.testing.assert_array_equal(row.profile_values,
            pixels[row.position.t, 1, row.position.z, 0].astype(np.float64) - 32768)
    assert estimate.result_bytes > 2 * 65536 * 8
    assert estimate.plot_bytes >= estimate.result_bytes


def test_cancellation_inside_large_plane_validation():
    reads = ij.DatasetReadObserver()
    owner = ij.Document(ij.Dataset(np.ones((1024, 1024), np.float32), axes="YX").observe_reads(reads))
    target = ij.resolve_target(owner.snapshot(), ij.TargetRequest(roi=ij.RoiPolicy.WHOLE_PLANE))
    command = ij.resolve_target_analysis(target)
    n = reads.counts().plane_reads
    token = ij.CancellationToken()
    after_read = []
    def checkpoint():
        if reads.counts().plane_reads > n:
            after_read.append(1)
            if len(after_read) == 2:
                token.cancel()
    with pytest.raises(ij.OperationCancelled):
        ij.execute_target_analysis(command, cancellation=token, checkpoint=checkpoint)
    assert len(after_read) == 2 and reads.counts().plane_reads == n + 1
    assert owner.undo_steps == 0


@pytest.mark.parametrize("change", ["metadata", "selection", "close"])
def test_adoption_requires_current_source_and_membership(change):
    owner, _, _, _, _ = fixture(policy="active")
    app = ij.Application()
    app.adopt(owner)
    command = ij.resolve_target_analysis(app.resolve_target())
    result = ij.execute_target_analysis(command)
    if change == "close":
        app.close(owner.id, discard=True)
    else:
        edit = owner.begin_edit()
        if change == "metadata":
            edit.set_property("external_analysis_change", True)
        else:
            edit.select(ij.Selection("rectangle", [(0, 0), (1, 1)]))
        owner.commit(edit)
    with pytest.raises(ij.DocumentUnavailable if change == "close" else ij.RevisionConflict):
        app.validate_target(command.target)
    assert result.planes[0].statistics["pixel_count"] > 0


@pytest.mark.parametrize("dtype,shape,axes,policy", [
    (np.float64, (2, 3), "YX", ij.FloatingSamplePolicy.FINITE_ONLY),
    (np.uint8, (2, 3, 3), "YXS", ij.FloatingSamplePolicy.FINITE_ONLY),
    (np.float32, (2, 3), "YX", ij.FloatingSamplePolicy.PRESERVE_QUIET_NAN_FLOAT32),
])
def test_unsupported_source_policy_rejects_before_acquisition(dtype, shape, axes, policy):
    reads = ij.DatasetReadObserver()
    owner = ij.Document(ij.Dataset(np.zeros(shape, dtype), axes=axes, sample_policy=policy).observe_reads(reads))
    n = reads.counts().plane_reads
    with pytest.raises(ValueError):
        ij.resolve_target_analysis(ij.resolve_target(owner.snapshot()))
    assert reads.counts().plane_reads == n


def test_effective_metadata_budget_does_not_copy_displaced_local_units():
    spatial = ij.SpatialCalibration(unit="local-" + "u" * 262144)
    owner = ij.Document(ij.Dataset(np.zeros((2, 3), np.uint8), axes="YX", calibration=spatial))
    app = ij.Application()
    app.adopt(owner)
    app.set_global_calibration(ij.GlobalCalibration(ij.SpatialCalibration(unit="um"),
        ij.ImageJIntensityCalibration.uncalibrated(), "uint8"))
    target = app.resolve_target()
    estimate = ij.estimate_target_analysis(target)
    # Shared sampling admission also conservatively charges the local unit once.
    assert estimate.working_bytes < 512 * 1024
    limits = ij.TargetAnalysisLimits()
    limits.max_working_bytes = estimate.working_bytes
    command = ij.resolve_target_analysis(target, limits=limits)
    assert command.spatial.unit == "um" and command.record["spatial"]["unit"] == "um"
    with pytest.raises(ValueError):
        ij.resolve_target_analysis(ij.resolve_target(owner.snapshot()), limits=limits)
