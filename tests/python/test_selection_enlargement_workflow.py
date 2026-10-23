"""Stage-relative selection intent, atomic publication and real session batches."""
import copy
import json
import numpy as np
import pytest
import imagejxx as ij


def owner(spacing=.5, samples=1):
    reads = ij.DatasetReadObserver()
    calibration = ij.SpatialCalibration(spacing=(spacing, 1.5, 2), unit='um')
    doc = ij.Document(ij.Dataset(np.full((2, 2, 2, 9, 9, samples), 10, np.uint8),
        axes='TCZYXS', calibration=calibration).observe_reads(reads))
    doc.select(ij.Selection('rectangle', [(3, 3), (6, 6)]))
    display = doc.snapshot().display
    display.t = display.c = 1
    display.crosshair = [4, 4, 1]
    doc.set_display(display)
    return doc, reads


def step(distance=1., units='pixels', original=None, enabled=True, target=None):
    return ij.NativeWorkflowOperation('selection_enlargement',
        {'distance': distance, 'units': units}, target or ij.TargetRequest(),
        original=original, enabled=enabled)


@pytest.mark.parametrize('samples', [1, 3])
@pytest.mark.parametrize('units,distance,radius', [('pixels', 1., 1), ('calibrated', 1., 2),
    ('pixels', -1., -1), ('pixels', 0., 0)])
def test_roundtrip_advances_stage_mask_and_commits_once(samples, units, distance, radius):
    doc, reads = owner(samples=samples)
    before = doc.snapshot()
    program = ij.NativeWorkflowProgram([step(distance, units),
        ij.NativeWorkflowOperation('add', {'value': 7}, ij.TargetRequest())])
    portable = ij.NativeWorkflowProgram.from_json(program.json())
    assert portable.record() == program.record() and reads.counts().plane_reads == 0
    prepared = ij.prepare_native_workflow(before, portable.steps)
    mask = np.zeros((9, 9), bool)
    mask[3-radius:6+radius, 3-radius:6+radius] = True
    expected = np.full((2, 2, 2, 9, 9, samples), 10, np.uint8)
    expected[1, 1, 1][mask] = 17
    np.testing.assert_array_equal(prepared.preview.dataset.numpy('TCZYXS'), expected)
    np.testing.assert_array_equal(prepared.preview.selection.mask((9, 9)) != 0, mask)
    assert prepared.dependencies == ij.Change.ALL and prepared.completed_steps == 2
    assert doc.snapshot().shares_data(before) and doc.undo_steps == 0
    doc.commit(prepared.begin_edit())
    assert doc.undo_steps == 1
    doc.undo()
    assert doc.snapshot().shares_data(before)
    doc.redo()
    np.testing.assert_array_equal(doc.snapshot().dataset.numpy('TCZYXS'), expected)


@pytest.mark.parametrize('distance', [-100., -1., 0., 1., 2.])
def test_selection_only_never_reads_or_creates_pixel_history(distance):
    doc, reads = owner()
    before = doc.snapshot()
    prepared = ij.prepare_native_workflow(before, [step(distance)])
    assert reads.counts().plane_reads == reads.counts().region_reads == 0
    assert prepared.preview.dataset.shares_pixels(before.dataset)
    assert prepared.dependencies == ij.Change.ALL
    doc.commit(prepared.begin_edit())
    after = doc.snapshot()
    assert not after.dirty and doc.undo_steps == doc.redo_steps == 0
    assert after.display.crosshair == before.display.crosshair
    if distance in (0., -100.):
        assert after.shares_data(before)
    else:
        assert after.previous_selection == before.selection


def test_sequential_enlargements_use_stage_geometry_and_previous():
    doc, reads = owner()
    prepared = ij.prepare_native_workflow(doc.snapshot(), [step(1), step(1)])
    assert prepared.preview.selection == ij.Selection('rectangle', [(1, 1), (8, 8)])
    assert prepared.preview.previous_selection == ij.Selection('rectangle', [(2, 2), (7, 7)])
    assert reads.counts().plane_reads == 0


def test_following_histogram_uses_enlarged_selection():
    doc, _ = owner()
    options = ij.TargetHistogramOptions()
    options.binning = ij.HistogramBinning.UniformRange
    options.bins = 2
    options.range = (0., 20.)
    choices = ij.NativeWorkflowHistogramOptions(options, False)
    histogram = ij.NativeWorkflowHistogram('area.csv', ij.TargetRequest(), authored_options=choices)
    prepared = ij.prepare_native_workflow(doc.snapshot(), [step(1), histogram])
    result = prepared.histograms[0]['result']
    assert result.finite_count == 25 and result.sum == 250
    np.testing.assert_array_equal(result.histogram['counts'], [0, 25])


def test_retained_limit_refuses_before_pixel_reads():
    doc, reads = owner()
    before = doc.snapshot()
    measured = ij.prepare_native_workflow(before, [step(1)])
    with pytest.raises(ValueError):
        ij.prepare_native_workflow(before, [step(1)],
            ij.NativeWorkflowLimits(max_retained_bytes=measured.retained_bytes-1))
    assert reads.counts().plane_reads == reads.counts().region_reads == 0
    assert doc.snapshot().shares_data(before)


@pytest.mark.parametrize('change', ['navigation', 'selection', 'title'])
def test_stale_publication_preserves_concurrent_state(change):
    doc, _ = owner()
    prepared = ij.prepare_native_workflow(doc.snapshot(), [step()])
    if change == 'navigation':
        display = doc.snapshot().display
        display.crosshair = [4, 4, 0]
        doc.set_display(display)
    elif change == 'selection':
        doc.select(ij.Selection('rectangle', [(0, 0), (2, 2)]))
    else:
        edit = doc.begin_edit()
        edit.set_title('concurrent')
        doc.commit(edit)
    current = doc.snapshot()
    history = doc.undo_steps
    with pytest.raises(ij.RevisionConflict):
        doc.commit(prepared.begin_edit())
    assert doc.snapshot().shares_data(current) and doc.undo_steps == history


@pytest.mark.parametrize('bad', ['distance_bool', 'distance_text', 'distance_lossy', 'units',
    'extra', 'absent', 'all_z', 'whole_plane', 'range', 'position', 'volume', 'destination',
    'original_equation', 'original_schema'])
@pytest.mark.parametrize('enabled', [True, False])
def test_bad_late_intent_refused_before_prior_pixel_reads(bad, enabled):
    doc, reads = owner()
    original = ij.resolve_selection_enlargement_command(doc.snapshot(), ij.SelectionEnlargementRequest(1)).record()
    params = {'distance': 1., 'units': 'pixels'}
    target = ij.TargetRequest()
    if bad == 'distance_bool': params['distance'] = True
    elif bad == 'distance_text': params['distance'] = '1'
    elif bad == 'distance_lossy': params['distance'] = 2**53+1
    elif bad == 'units': params['units'] = 'um'
    elif bad == 'extra': params['radius'] = 1
    elif bad == 'absent': params.pop('distance')
    elif bad == 'all_z': target = ij.TargetRequest(ij.TargetScope.ALL_Z)
    elif bad == 'whole_plane': target = ij.TargetRequest(roi=ij.RoiPolicy.WHOLE_PLANE)
    elif bad == 'range': target = ij.TargetRequest(t=ij.AxisRange(0, 1))
    elif bad == 'position': target = ij.TargetRequest(ij.TargetScope.POSITIONS, positions=[ij.PlanePosition(0, 0, 0)])
    elif bad == 'volume': target = ij.TargetRequest(mode=ij.ExecutionMode.VOLUME)
    elif bad == 'destination': target = ij.TargetRequest(destination=ij.OutputDestination.NEW_DOCUMENT)
    elif bad == 'original_equation': original['distance'] = 2.
    else: original['schema'] = 'future'
    before = doc.snapshot()
    with pytest.raises((ValueError, TypeError)):
        last = ij.NativeWorkflowOperation('selection_enlargement', params, target, original=original, enabled=enabled)
        ij.prepare_native_workflow(before, [ij.NativeWorkflowOperation('add', {'value': 1}, ij.TargetRequest()), last])
    assert reads.counts().plane_reads == reads.counts().region_reads == 0
    assert doc.snapshot().shares_data(before)


def test_disabled_step_accepts_missing_destination_roi_but_validates_original():
    doc, reads = owner()
    original = ij.resolve_selection_enlargement_command(doc.snapshot(), ij.SelectionEnlargementRequest(1)).record()
    other = ij.Document(ij.Dataset(np.zeros((9, 9), np.uint8), axes='YX'))
    skipped = step(1, original=original, enabled=False)
    portable = ij.NativeWorkflowProgram.from_json(ij.NativeWorkflowProgram([skipped]).json())
    result = ij.prepare_native_workflow(other.snapshot(), portable.steps)
    assert result.completed_steps == 0 and result.preview.selection is None
    assert reads.counts().plane_reads == 0
    with pytest.raises(ValueError):
        ij.prepare_native_workflow(other.snapshot(), [step(1, original=original)])


def test_capture_is_provenance_and_authored_request_uses_destination_calibration():
    original_owner, _ = owner()
    original = ij.resolve_selection_enlargement_command(original_owner.snapshot(), ij.SelectionEnlargementRequest(2)).record()
    original_copy = copy.deepcopy(original)
    doc, reads = owner(spacing=2.)
    doc.select(ij.Selection('rectangle', [(2, 2), (5, 5)]))
    result = ij.prepare_native_workflow(doc.snapshot(), [step(2., 'calibrated', original)])
    assert result.preview.selection == ij.Selection('rectangle', [(1, 1), (6, 6)])
    assert original == original_copy and reads.counts().plane_reads == 0


@pytest.mark.parametrize('when', ['before', 'during', 'finish'])
def test_cancellation_and_callback_failure_do_not_publish(when):
    doc, reads = owner()
    before = doc.snapshot()
    token = ij.CancellationToken()
    if when == 'before': token.cancel()
    def progress(done, total):
        if when == 'during': token.cancel()
        elif when == 'finish' and done == total: raise RuntimeError('deliberate finish failure')
    with pytest.raises(RuntimeError):
        ij.prepare_native_workflow(before, [step()], cancellation=token, progress=progress)
    assert doc.snapshot().shares_data(before) and reads.counts().plane_reads == 0


def test_batch_inputs_use_each_selection_and_x_spacing(tmp_path):
    inputs = []
    raw = {}
    for index, spacing in enumerate((.5, 1., 2.)):
        doc, _ = owner(spacing=spacing)
        path = tmp_path/f'input{index}.ijx'
        ij.save_session(str(path), ij.SessionCapture([doc.snapshot()]))
        raw[path] = path.read_bytes()
        inputs.append(ij.NativeWorkflowBatchInput(str(path), f'result{index}'))
    output = tmp_path/'output'
    output.mkdir()
    prepared = ij.prepare_native_workflow_batch(inputs, [step(1., 'calibrated'),
        ij.NativeWorkflowOperation('add', {'value': 7}, ij.TargetRequest())], str(output))
    assert all(not (output/f'result{index}.ijx').exists() for index in range(3))
    prepared.publish()
    for index, radius in enumerate((2, 1, 1)):
        decoded = ij.load_session(str(output/f'result{index}.ijx'))
        actual = decoded.documents[0].snapshot()
        mask = np.zeros((9, 9), bool)
        mask[3-radius:6+radius, 3-radius:6+radius] = True
        np.testing.assert_array_equal(actual.selection.mask((9, 9)) != 0, mask)
        expected = np.full((2, 2, 2, 9, 9, 1), 10, np.uint8)
        expected[1, 1, 1][mask] = 17
        np.testing.assert_array_equal(actual.dataset.numpy('TCZYXS'), expected)
    assert all(path.read_bytes() == contents for path, contents in raw.items())
