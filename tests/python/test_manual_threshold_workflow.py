"""Independent manual workflow samples, provenance, codecs and transactions."""
import copy

import imagejxx as ij
import numpy as np
import pytest

from test_path_filter_admission_ui import pinned_members
from test_path_filter_ui import PATH


def captured(version=3):
    owner = ij.Document(ij.Dataset(np.zeros((2, 17, 23), np.uint8), axes='ZYX'))
    owner.select(ij.Selection.from_path(ij.ImageJCompositePath(PATH)) if version == 3
                 else ij.Selection('rectangle', [(0, 0), (3, 3)]))
    target = ij.TargetRequest(ij.TargetScope.ALL_Z if version == 2 else
                             ij.TargetScope.CURRENT_PLANE)
    record = ij.resolve_manual_threshold_command(
        owner.snapshot(), ij.ManualThresholdRange(40, 120), target).record()
    assert record['schema_version'] == version
    return record


def destination(dtype):
    t, c, z, y, x = np.indices((2, 2, 2, 17, 23))
    pixels = ((19*t + 29*c + 37*z + 5*y + 3*x) % 180).astype(dtype)
    if dtype in (np.float32, np.float64):
        pixels[0, 0, 0, 0, 0] = -0.0
    owner = ij.Document(ij.Dataset(pixels, axes='TCZYX'))
    owner.select(ij.Selection.from_path(ij.ImageJCompositePath(PATH)))
    edit = owner.begin_edit()
    edit.set_title('destination, not captured owner')
    edit.set_rois([ij.ROI('destination rectangle',
        ij.Selection('rectangle', [(1, 2), (5, 6)]), t=1, c=1, z=0)])
    owner.commit(edit)
    view = owner.snapshot().display
    view.t, view.c, view.crosshair = 1, 1, [7, 8, 1]
    view.properties = {'lut': 'Fire', 'display_range': [3., 179.]}
    view = ij.set_manual_threshold_state(view, owner.snapshot().dataset,
                                        ij.ManualThresholdRange(13, 17))
    owner.set_display(view)
    return owner, pixels


def target(scope, roi):
    policy = {'whole': ij.RoiPolicy.WHOLE_PLANE,
              'active': ij.RoiPolicy.ACTIVE_SELECTION,
              'positioned': ij.RoiPolicy.POSITIONED_ROIS}[roi]
    if scope == 'current':
        return ij.TargetRequest(roi=policy), [(1, 1, 1)]
    if scope == 'all_z':
        return ij.TargetRequest(ij.TargetScope.ALL_Z, roi=policy), [(1, 1, 0), (1, 1, 1)]
    if scope == 'ranges':
        return ij.TargetRequest(ij.TargetScope.RANGES, roi=policy,
            t=ij.AxisRange(0, 2), c=ij.AxisRange(1, 2), z=ij.AxisRange(0, 2)), [
                (t, 1, z) for t in range(2) for z in range(2)]
    return ij.TargetRequest(ij.TargetScope.POSITIONS, roi=policy,
        positions=[ij.PlanePosition(0, 0, 1), ij.PlanePosition(1, 1, 0)]), [
            (0, 0, 1), (1, 1, 0)]


def expected_samples(pixels, positions, roi, lower, upper):
    result = pixels.copy()
    for position in positions:
        if roi == 'positioned' and position != (1, 1, 0):
            continue
        cells = ((x, y) for y in range(17) for x in range(23)) if roi == 'whole' else (
            pinned_members() if roi == 'active' else
            ((x, y) for y in range(2, 6) for x in range(1, 5)))
        for x, y in cells:
            value = pixels[position][y, x]
            result[position][y, x] = 255 if lower <= value <= upper else 0
    return result


@pytest.mark.parametrize('dtype', [np.uint8, np.uint16, np.float32, np.float64])
@pytest.mark.parametrize('scope', ['current', 'all_z', 'ranges', 'positions'])
@pytest.mark.parametrize('roi', ['whole', 'active', 'positioned'])
@pytest.mark.parametrize('authored', [False, True])
def test_manual_program_destination_pixels_original_and_atomic_history(dtype, scope, roi, authored):
    owner, pixels = destination(dtype)
    before, history = owner.snapshot(), owner.undo_steps
    request, positions = target(scope, roi)
    original = captured()
    frozen = copy.deepcopy(original)
    parameters = {'lower': 60., 'upper': 90.} if authored else {}
    program = ij.NativeWorkflowProgram([
        ij.NativeWorkflowOperation('threshold', parameters, request, original=original)])
    wire = program.record()
    assert wire['version'] == 9 and wire['steps'][0]['original'] == frozen
    assert ij.NativeWorkflowProgram.from_record(wire).record() == wire
    loaded = ij.NativeWorkflowProgram.from_json(program.json())
    assert loaded.record() == wire
    prepared = ij.prepare_native_workflow(before, loaded.steps)
    lower, upper = (60, 90) if authored else (40, 120)
    expected = expected_samples(pixels, positions, roi, lower, upper)
    np.testing.assert_array_equal(prepared.preview.dataset.numpy('TCZYX'), expected)
    assert prepared.preview.dataset.numpy('TCZYX').dtype == pixels.dtype
    assert prepared.preview.dataset.numpy('TCZYX').tobytes() == expected.tobytes()
    assert owner.snapshot().shares_data(before) and owner.undo_steps == history
    assert prepared.preview.selection == before.selection
    assert prepared.preview.metadata == before.metadata
    assert prepared.preview.display.properties == before.display.properties
    # Pixel publication merges with live camera state rather than restoring capture.
    view = owner.snapshot().display
    view.crosshair = [9, 10, 0]
    owner.set_display(view)
    owner.commit(prepared.begin_edit())
    np.testing.assert_array_equal(owner.snapshot().dataset.numpy('TCZYX'), expected)
    assert owner.snapshot().dataset.numpy('TCZYX').dtype == pixels.dtype
    assert owner.snapshot().dataset.numpy('TCZYX').tobytes() == expected.tobytes()
    assert owner.snapshot().display.crosshair == [9, 10, 0]
    assert owner.snapshot().display.properties == before.display.properties
    affected = not np.array_equal(expected, pixels)
    assert owner.undo_steps == history + int(affected)
    if affected:
        owner.undo()
        np.testing.assert_array_equal(owner.snapshot().dataset.numpy('TCZYX'), pixels)
        assert owner.snapshot().dataset.numpy('TCZYX').tobytes() == pixels.tobytes()
        owner.redo()
        np.testing.assert_array_equal(owner.snapshot().dataset.numpy('TCZYX'), expected)
        assert owner.snapshot().dataset.numpy('TCZYX').tobytes() == expected.tobytes()
    assert original == frozen and program.record() == wire


@pytest.mark.parametrize('version', [1, 2, 3])
def test_saved_manual_versions_rebind_without_restoring_original_selection(version):
    owner, pixels = destination(np.uint8)
    before = owner.snapshot()
    original = captured(version)
    with pytest.raises((ValueError, ij.RevisionConflict)):
        ij.manual_threshold_command_from_record(before, original)
    request, positions = target('all_z', 'active')
    step = ij.NativeWorkflowOperation('threshold', {}, request, original=original)
    program = ij.NativeWorkflowProgram([step])
    prepared = ij.prepare_native_workflow(before, program.steps)
    np.testing.assert_array_equal(prepared.preview.dataset.numpy('TCZYX'),
        expected_samples(pixels, positions, 'active', 40, 120))
    assert prepared.preview.selection == before.selection and program.record()['steps'][0]['original'] == original


@pytest.mark.parametrize('version', range(1, 9))
def test_manual_program_cannot_be_downgraded_into_old_grammar(version):
    step = ij.NativeWorkflowOperation('threshold', {}, ij.TargetRequest(), original=captured())
    wire = ij.NativeWorkflowProgram([step]).record()
    wire['version'] = version
    fields = {'kind', 'original', 'operation', 'parameters', 'target', 'use_destination_calibration'}
    if version >= 2:
        fields.add('filter_options')
    if version >= 3:
        fields.add('enabled')
    if version >= 6:
        fields.add('selection_creation')
    if version >= 7:
        fields.add('annotation_intent')
    if version >= 8:
        fields.add('selection_creation_extent')
    wire['steps'][0] = {k: v for k, v in wire['steps'][0].items() if k in fields}
    if version == 6:
        from test_creation_workflow import step as creation, request
        sentinel = ij.NativeWorkflowProgram([creation(request())]).record()
        assert sentinel['version'] == 6
        wire['steps'].extend(sentinel['steps'])
    if version == 7:
        from test_annotation_workflow import operation, store
        sentinel = ij.NativeWorkflowProgram([operation(store())]).record()
        assert sentinel['version'] == 7
        wire['steps'].extend(sentinel['steps'])
    with pytest.raises(ValueError, match='Typed manual threshold requires native program V9'):
        ij.NativeWorkflowProgram.from_record(wire)


@pytest.mark.parametrize('dtype', [np.uint8, np.uint16, np.float32, np.float64])
def test_whole_dataset_manual_result_is_uint8_and_undo_restores_original_type(dtype):
    owner, pixels = destination(dtype)
    request = ij.TargetRequest(ij.TargetScope.RANGES, roi=ij.RoiPolicy.WHOLE_PLANE,
        t=ij.AxisRange(0, 2), c=ij.AxisRange(0, 2), z=ij.AxisRange(0, 2))
    step = ij.NativeWorkflowOperation('threshold', {}, request, original=captured())
    prepared = ij.prepare_native_workflow(owner.snapshot(), [step])
    expected = np.where((pixels >= 40) & (pixels <= 120), 255, 0).astype(np.uint8)
    result = prepared.preview.dataset.numpy('TCZYX')
    assert result.dtype == np.dtype('uint8') and result.tobytes() == expected.tobytes()
    owner.commit(prepared.begin_edit())
    assert owner.snapshot().dataset.numpy('TCZYX').dtype == np.dtype('uint8')
    owner.undo()
    restored = owner.snapshot().dataset.numpy('TCZYX')
    assert restored.dtype == pixels.dtype and restored.tobytes() == pixels.tobytes()
    owner.redo()
    assert owner.snapshot().dataset.numpy('TCZYX').dtype == np.dtype('uint8')


@pytest.mark.parametrize('parameters', [
    {'lower': 1}, {'upper': 2}, {'lower': 1, 'upper': 2, 'extra': 3},
    {'lower': True, 'upper': 2}, {'lower': '1', 'upper': 2},
    {'lower': float('nan'), 'upper': 2}, {'lower': 1, 'upper': float('inf')},
    {'lower': 3, 'upper': 2}, {'lower': 2**53+1, 'upper': 2**54},
])
@pytest.mark.parametrize('enabled', [False, True])
def test_malformed_authored_bounds_refuse_even_when_disabled(parameters, enabled):
    owner, _ = destination(np.uint8)
    before, history = owner.snapshot(), owner.undo_steps
    step = ij.NativeWorkflowOperation('threshold', parameters, ij.TargetRequest(), original=captured(), enabled=enabled)
    for action in (lambda: ij.NativeWorkflowProgram([step]),
                   lambda: ij.prepare_native_workflow(before, [step])):
        with pytest.raises((ValueError, TypeError, OverflowError)):
            action()
    assert owner.snapshot().shares_data(before) and owner.undo_steps == history


def test_disabled_manual_is_v9_but_has_no_pixel_or_history_effect():
    owner, pixels = destination(np.uint16)
    before, history = owner.snapshot(), owner.undo_steps
    step = ij.NativeWorkflowOperation('threshold', {}, ij.TargetRequest(), original=captured(), enabled=False)
    program = ij.NativeWorkflowProgram([step])
    assert program.record()['version'] == 9
    prepared = ij.prepare_native_workflow(before, program.steps)
    owner.commit(prepared.begin_edit())
    np.testing.assert_array_equal(owner.snapshot().dataset.numpy('TCZYX'), pixels)
    assert owner.undo_steps == history


def test_manual_preceding_add_and_low_working_refusal_have_no_partial_publication():
    owner, pixels = destination(np.float64)
    before, history = owner.snapshot(), owner.undo_steps
    request, positions = target('all_z', 'whole')
    steps = [ij.NativeWorkflowOperation('add', {'value': 1}, request),
             ij.NativeWorkflowOperation('threshold', {'lower': 60, 'upper': 90},
                                         request, original=captured())]
    with pytest.raises(ValueError):
        ij.prepare_native_workflow(before, steps, ij.NativeWorkflowLimits(max_working_bytes=64))
    assert owner.snapshot().shares_data(before) and owner.undo_steps == history
    expected = pixels.copy()
    for p in positions:
        expected[p] += 1
    expected = expected_samples(expected, positions, 'whole', 60, 90)
    prepared = ij.prepare_native_workflow(before, steps)
    np.testing.assert_array_equal(prepared.preview.dataset.numpy('TCZYX'), expected)
    edit = owner.begin_edit()
    edit.set_title('concurrent change')
    owner.commit(edit)
    with pytest.raises(ij.RevisionConflict):
        owner.commit(prepared.begin_edit())
    np.testing.assert_array_equal(owner.snapshot().dataset.numpy('TCZYX'), pixels)


@pytest.mark.parametrize('authored', [False, True])
def test_v9_session_and_batch_rebind_and_preserve_program_without_double_execution(tmp_path, authored):
    owner, pixels = destination(np.uint16)
    before = owner.snapshot()
    other = ij.Document(ij.Dataset(np.full((2, 3), 177, np.uint8), axes='YX'))
    request, positions = target('all_z', 'active')
    parameters = {'lower': 60, 'upper': 90} if authored else {}
    program = ij.NativeWorkflowProgram([
        ij.NativeWorkflowOperation('threshold', parameters, request, original=captured())])
    capture = ij.SessionCapture([before, other.snapshot()], workflow=[program.record()])
    capture.results = [{'retained': 17}]
    capture.extensions = {'manual_workflow': ['retain', 2**64-1]}
    path = tmp_path/'source.ijx'
    ij.save_session(str(path), capture)
    saved = path.read_bytes()
    reopened = ij.load_session(str(path))
    assert reopened.workflow == [program.record()]
    assert len(reopened.documents) == 2
    reopened_pixels = reopened.documents[0].snapshot().dataset.numpy('TCZYX')
    assert reopened_pixels.dtype == pixels.dtype and reopened_pixels.tobytes() == pixels.tobytes()
    output = tmp_path/'output'
    output.mkdir()
    plan = ij.prepare_native_workflow_batch([
        ij.NativeWorkflowBatchInput(str(path), 'result')], program.steps, str(output))
    assert not (output/'result.ijx').exists()
    plan.publish()
    result = ij.load_session(str(output/'result.ijx'))
    assert len(result.documents) == 2
    assert result.workflow == [program.record()]
    assert result.results == capture.results and result.extensions == capture.extensions
    lower, upper = (60, 90) if authored else (40, 120)
    expected = expected_samples(pixels, positions, 'active', lower, upper)
    actual = result.documents[0].snapshot().dataset.numpy('TCZYX')
    assert actual.dtype == pixels.dtype and actual.tobytes() == expected.tobytes()
    other_pixels = result.documents[1].snapshot().dataset.numpy('YX')
    other_expected = np.full((2, 3), 177, np.uint8)
    assert other_pixels.dtype == other_expected.dtype and other_pixels.tobytes() == other_expected.tobytes()
    np.testing.assert_array_equal(other_pixels, other_expected)
    assert path.read_bytes() == saved and owner.snapshot().shares_data(before)


@pytest.mark.parametrize('dtype,lower,upper', [(np.uint8, 80, 90), (np.uint16, 20000, 30000)])
def test_contrast_apply_prefix_feeds_manual_threshold_without_losing_eager_stage(dtype, lower, upper):
    pixels = np.array([[[10, 20, 30, 40]], [[40, 30, 20, 10]]], dtype=dtype)
    owner = ij.Document(ij.Dataset(pixels, axes='ZYX'))
    before = owner.snapshot()
    request = ij.TargetRequest(ij.TargetScope.ALL_Z, roi=ij.RoiPolicy.WHOLE_PLANE)
    steps = [ij.NativeWorkflowOperation('contrast_apply',
        {'minimum': 10, 'maximum': 40, 'unsigned16_bits': 0}, request),
        ij.NativeWorkflowOperation('threshold', {'lower': lower, 'upper': upper},
            request, original=captured())]
    program = ij.NativeWorkflowProgram(steps)
    prepared = ij.prepare_native_workflow(before, program.steps)
    expected = np.array([[[0, 255, 0, 0]], [[0, 0, 255, 0]]], dtype=np.uint8)
    actual = prepared.preview.dataset.numpy('ZYX')
    assert actual.dtype == expected.dtype and actual.tobytes() == expected.tobytes()
    assert prepared.completed_steps == 2 and owner.snapshot().shares_data(before)
    owner.commit(prepared.begin_edit())
    assert owner.undo_steps == 1
    owner.undo()
    restored = owner.snapshot().dataset.numpy('ZYX')
    assert restored.dtype == pixels.dtype and restored.tobytes() == pixels.tobytes()
    owner.redo()
    assert owner.snapshot().dataset.numpy('ZYX').tobytes() == expected.tobytes()
