"""Captured Analyzer rows: independent samples, stored positions and ownership."""
import math
import numpy as np
import pytest
import imagejxx as ij
from test_global_calibration_execution import global_value


def fixture(dtype=np.uint8):
    pixels = np.empty((2, 2, 2, 3, 4), dtype=dtype)
    for t in range(2):
        for c in range(2):
            for z in range(2):
                raw = 100*t + 30*c + 10*z + np.arange(12).reshape(3, 4)
                pixels[t, c, z] = raw*257 if dtype == np.uint16 else raw*.25-8 if dtype == np.float32 else raw
    reads = ij.DatasetReadObserver()
    owner = ij.Document(ij.Dataset(pixels, axes="TCZYX", calibration=ij.SpatialCalibration(
        spacing=[3, 4, 5], unit="local")).observe_reads(reads))
    edit = owner.begin_edit()
    edit.select(ij.Selection("line", [(0, 0), (3, 2)]))
    edit.set_rois([
        ij.ROI("area A", ij.Selection("rectangle", [(0, 0), (2, 2)]), t=1, c=0, z=1),
        ij.ROI("line B", ij.Selection("line", [(0, 0), (3, 2)]), t=0, c=1, z=0),
        ij.ROI("same plane", ij.Selection("rectangle", [(2, 0), (4, 1)]), t=1, c=0, z=1),
        ij.ROI("wildcard", ij.Selection("rectangle", [(0, 0), (1, 1)]), c=1)])
    owner.commit(edit)
    view = owner.snapshot().display
    view.t, view.c, view.crosshair = 1, 0, [0, 0, 0]
    owner.set_display(view)
    app = ij.Application()
    app.adopt(owner)
    return app, owner, reads, pixels


def prepare(app, owner, indices=()):
    request = ij.roi_measurement_target_request(owner.snapshot(), list(indices))
    options = ij.TargetAnalysisOptions()
    options.measurement, options.roi_indices = ij.TargetMeasurementMode.ROI_MANAGER, list(indices)
    return ij.resolve_target_analysis(app.resolve_target(request), options)


@pytest.mark.parametrize("dtype", [np.uint8, np.uint16, np.float32])
@pytest.mark.parametrize("indices", [[], [2, 1, 0], [3]])
def test_manager_grouped_reads_preserve_row_order_and_frozen_axes(dtype, indices):
    app, owner, reads, pixels = fixture(dtype)
    n = reads.counts().plane_reads
    before = owner.snapshot()
    command = prepare(app, owner, indices)
    assert reads.counts().plane_reads == n and command.record["version"] == 3
    order = indices or [0, 1, 2, 3]
    positions = [(1, 0, 1), (0, 1, 0), (1, 0, 1), (1, 1, 0)]
    assert command.estimate.row_count == len(order)
    assert command.estimate.plane_count == len({positions[i] for i in order})
    exposed = command.options
    exposed.roi_indices = [0]
    owner.select(ij.Selection("rectangle", [(0, 0), (1, 1)]))
    view = owner.snapshot().display
    view.t, view.c, view.crosshair = 0, 0, [0, 0, 1]
    owner.set_display(view)
    current = owner.snapshot()
    undo = owner.undo_steps
    app.validate_target(command.target)  # Manager does not consume active selection/navigation.
    result = ij.execute_target_analysis(command)
    assert reads.counts().plane_reads == n + command.estimate.plane_count
    assert result.profile_distances.size == 0
    for row, index in zip(result.planes, order):
        assert row.roi_index == index
        assert (row.position.t, row.position.c, row.position.z) == positions[index]
        source = pixels[positions[index]]
        if index == 1:
            values = np.linspace(float(source[0, 0]), float(source[2, 3]), 5).astype(np.float32).astype(float)
            assert row.length == pytest.approx(math.sqrt(145))
            assert row.midpoint_world == [4.5, 4, 0] and row.bounds is None
        else:
            values = (source[:2, :2] if index == 0 else source[:1, 2:4] if index == 2 else source[:1, :1]).astype(float).ravel()
            assert row.length is None
        assert row.statistics["pixel_count"] == len(values)
        assert row.statistics["mean"] == pytest.approx(values.mean())
        assert row.statistics["area"] == len(values)*12
    assert owner.snapshot().shares_data(current) and owner.undo_steps == undo
    assert before.dataset.shape == owner.snapshot().dataset.shape


@pytest.mark.parametrize("bypass", [False, True])
def test_global_values_are_frozen_and_original_manager_revision_is_guarded(bypass):
    app, owner, reads, _ = fixture()
    app.set_global_calibration(global_value())
    app.set_ignore_global_calibration(bypass, owner.id)
    command = prepare(app, owner)
    n = reads.counts().plane_reads
    def progress(done, total):
        if done == 0:
            app.set_global_calibration(global_value(.75))
    result = ij.execute_target_analysis(command, progress=progress)
    assert reads.counts().plane_reads == n + 3
    assert result.planes[0].statistics["mean"] == (112.5 if bypass else 235)
    assert result.planes[1].statistics["mean"] == (35.5 if bypass else 81)
    if bypass:
        app.validate_target(command.target)
    else:
        with pytest.raises(ij.RevisionConflict):
            app.validate_target(command.target)
    edit = owner.begin_edit()
    edit.set_rois([])
    owner.commit(edit)
    with pytest.raises(ij.RevisionConflict):
        app.validate_target(command.target)


@pytest.mark.parametrize("which", ["max_working_bytes", "max_pixels", "max_record_bytes", "max_measurement_rows"])
def test_manager_admission_refuses_before_reads_or_callbacks(which):
    app, owner, reads, _ = fixture()
    command = prepare(app, owner)
    limits = ij.TargetAnalysisLimits()
    setattr(limits, which, {"max_working_bytes":command.estimate.working_bytes-1,
        "max_pixels":command.estimate.total_pixels-1, "max_record_bytes":1, "max_measurement_rows":3}[which])
    n = reads.counts().plane_reads
    for fn in [ij.resolve_target_analysis, ij.estimate_target_analysis]:
        with pytest.raises(ValueError):
            fn(command.target, command.options, limits)
    calls = []
    with pytest.raises(ValueError):
        ij.execute_target_analysis(command, runtime_max_working_bytes=command.estimate.working_bytes-1,
                                   checkpoint=lambda:calls.append(1))
    assert not calls and reads.counts().plane_reads == n


@pytest.mark.parametrize("indices", [[0, 0], [99]])
def test_invalid_rows_refuse_without_reads(indices):
    _, owner, reads, _ = fixture()
    n = reads.counts().plane_reads
    with pytest.raises(ValueError):
        ij.roi_measurement_target_request(owner.snapshot(), indices)
    assert reads.counts().plane_reads == n


def test_cancellation_discards_partial_group_and_unsupported_rows_refuse():
    app, owner, reads, _ = fixture()
    command = prepare(app, owner)
    token = ij.CancellationToken()
    n = reads.counts().plane_reads
    def progress(done, total):
        if done == 1:
            token.cancel()
    with pytest.raises(ij.OperationCancelled):
        ij.execute_target_analysis(command, cancellation=token, progress=progress)
    assert reads.counts().plane_reads == n + 1
    edit = owner.begin_edit()
    edit.set_rois([ij.ROI("unsupported", ij.Selection("angle", [(0, 0), (1, 1), (2, 0)]))])
    owner.commit(edit)
    n = reads.counts().plane_reads
    with pytest.raises(ValueError):
        prepare(app, owner)
    assert reads.counts().plane_reads == n


def test_current_line_all_z_midpoints_and_profile_options_are_separate():
    app, owner, reads, _ = fixture()
    options = ij.TargetAnalysisOptions()
    options.measurement = ij.TargetMeasurementMode.SELECTION
    command = ij.resolve_target_analysis(app.resolve_target(ij.TargetRequest(ij.TargetScope.ALL_Z)), options)
    n = reads.counts().plane_reads
    result = ij.execute_target_analysis(command)
    assert reads.counts().plane_reads == n + 2
    assert [row.midpoint_world for row in result.planes] == [[4.5, 4, 0], [4.5, 4, 5]]
    assert [row.statistics["mean"] for row in result.planes] == [105.5, 115.5]
    options.path = [[0, 0]]
    with pytest.raises(ValueError):
        ij.resolve_target_analysis(command.target, options)
