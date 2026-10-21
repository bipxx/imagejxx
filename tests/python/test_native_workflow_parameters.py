"""Authored filter intent, immutable captures and versioned portable execution."""
import copy
import json
import numpy as np
import pytest
import imagejxx as ij
from test_imagej_roi_import import owner


def target():
    return ij.TargetRequest(ij.TargetScope.ALL_Z, roi=ij.RoiPolicy.WHOLE_PLANE)


@pytest.mark.parametrize('method', ['minimum', 'maximum', 'median', 'mean'])
@pytest.mark.parametrize('dtype', [np.uint8, np.uint16, np.float32])
def test_authored_rank_options_execute_new_literal_intent_and_preserve_original(method, dtype):
    pixels = np.zeros((5, 5), dtype=dtype); pixels[2, 2] = 80
    doc = ij.Document(ij.Dataset(pixels, axes='YX')); before = doc.snapshot()
    original = ij.resolve_rank_command(before, options=ij.RankFilterCommandOptions('median', 2)).record()
    options = ij.RankFilterCommandOptions(method, 0)
    step = ij.NativeWorkflowOperation('rank_filter_imagej', {}, target(), original=original, filter_options=options)
    program = ij.NativeWorkflowProgram([step]); saved = program.record()
    options.radius = 7
    assert saved['version'] == 2 and saved['steps'][0]['original'] == original
    assert saved['steps'][0]['filter_options'] == {'kind': 'rank', 'method': method, 'radius': 0.0}
    loaded = ij.NativeWorkflowProgram.from_json(program.json())
    assert loaded.json() == program.json()
    prepared = ij.prepare_native_workflow(before, loaded.steps)
    expected = np.zeros_like(pixels)
    if method in ('maximum', 'mean'):
        for y, x in [(2, 2), (1, 2), (3, 2), (2, 1), (2, 3)]:
            expected[y, x] = 80 if method == 'maximum' else 16
    np.testing.assert_array_equal(prepared.preview.dataset.numpy('YX'), expected)
    assert doc.snapshot().shares_data(before) and doc.undo_steps == 0 and program.record() == saved


@pytest.mark.parametrize('original_capture', [False, True])
def test_authored_gaussian_zero_sigma_overrides_old_capture_without_rewriting_it(original_capture):
    pixels = np.arange(50, dtype=np.uint16).reshape(2, 5, 5)
    doc = ij.Document(ij.Dataset(pixels, axes='ZYX')); before = doc.snapshot()
    original = ij.resolve_gaussian_command(before, options=ij.GaussianBlurCommandOptions(2, False)).record() if original_capture else None
    program = ij.NativeWorkflowProgram([ij.NativeWorkflowOperation('gaussian_blur_imagej', {}, target(),
        original=original, filter_options=ij.GaussianBlurCommandOptions(0, True))])
    saved = program.record(); assert saved['steps'][0]['original'] == original
    result = ij.prepare_native_workflow(before, ij.NativeWorkflowProgram.from_record(saved).steps)
    np.testing.assert_array_equal(result.preview.dataset.numpy('ZYX'), pixels)
    assert doc.snapshot().shares_data(before)


def test_authored_scaled_gaussian_uses_destination_calibration_and_requires_consent():
    pixels = np.zeros((9, 11), np.uint16); pixels[4, 5] = 1000
    doc = ij.Document(ij.Dataset(pixels, axes='YX')); old = doc.snapshot()
    original = ij.resolve_gaussian_command(old).record()
    edit = doc.begin_edit(); edit.set_calibration(ij.SpatialCalibration(spacing=(0.5, 2, 3), unit='um')); doc.commit(edit)
    source = doc.snapshot(); saved = copy.deepcopy(original)
    def step(consent):
        return ij.NativeWorkflowOperation('gaussian_blur_imagej', {}, target(), original=original,
            use_destination_calibration=consent, filter_options=ij.GaussianBlurCommandOptions(1, True))
    with pytest.raises(ValueError, match='calibration'):
        ij.prepare_native_workflow(source, [step(False)])
    program = ij.NativeWorkflowProgram([step(True)])
    result = ij.prepare_native_workflow(source, ij.NativeWorkflowProgram.from_json(program.json()).steps)
    expected = ij.prepare_native_workflow(source, [ij.NativeWorkflowOperation('gaussian_blur_imagej',
        {'sigma': np.array([0.5, 2.0])}, target())])
    np.testing.assert_array_equal(result.preview.dataset.numpy('YX'), expected.preview.dataset.numpy('YX'))
    assert original == saved and program.record()['steps'][0]['original'] == saved


@pytest.mark.parametrize('bad', ['version', 'missing', 'extra', 'kind', 'bool_sigma', 'negative_sigma',
    'rank_method', 'rank_radius', 'median_radius', 'wrong_operation', 'mixed_parameters', 'volume'])
def test_closed_v2_filter_options_admit_no_ambiguous_or_malformed_intent(bad):
    program = ij.NativeWorkflowProgram([ij.NativeWorkflowOperation('gaussian_blur_imagej', {}, target(),
        filter_options=ij.GaussianBlurCommandOptions(0, False))])
    r = program.record(); s = r['steps'][0]; o = s['filter_options']
    if bad == 'version': r['version'] = 99
    elif bad == 'missing': del s['filter_options']
    elif bad == 'extra': o['extra'] = 1
    elif bad == 'kind': o['kind'] = 'future'
    elif bad == 'bool_sigma': o['sigma'] = True
    elif bad == 'negative_sigma': o['sigma'] = -1
    elif bad in ('rank_method', 'rank_radius', 'median_radius'):
        s['operation'] = 'rank_filter_imagej'
        s['filter_options'] = {'kind': 'rank', 'method': 'median', 'radius': 0}
        if bad == 'rank_method': s['filter_options']['method'] = 'future'
        elif bad == 'rank_radius': s['filter_options']['radius'] = 1001
        else: s['filter_options']['radius'] = 101
    elif bad == 'wrong_operation': s['operation'] = 'add'
    elif bad == 'mixed_parameters': s['parameters'] = {'sigma': {'kind': 'real', 'value': 2}}
    elif bad == 'volume': s['target']['mode'] = 1
    with pytest.raises((ValueError, KeyError)):
        ij.NativeWorkflowProgram.from_record(r)
    with pytest.raises((ValueError, KeyError)):
        ij.NativeWorkflowProgram.from_json(json.dumps(r))


@pytest.mark.parametrize('kind', ['gaussian', 'rank'])
def test_corrupt_late_original_still_refused_before_any_processing_reads(kind):
    doc, reads = owner(); source = doc.snapshot()
    if kind == 'gaussian':
        record = ij.resolve_gaussian_command(source).record(); record['effective_sigma_xy'] = [9, 9]
        options = ij.GaussianBlurCommandOptions(0, False); name = 'gaussian_blur_imagej'
    else:
        record = ij.resolve_rank_command(source).record(); record['profile'] = 'future'
        options = ij.RankFilterCommandOptions('mean', 0); name = 'rank_filter_imagej'
    steps = [ij.NativeWorkflowOperation('add', {'value': 7}, target()),
        ij.NativeWorkflowOperation(name, {}, target(), original=record, filter_options=options)]
    counts = reads.counts()
    with pytest.raises((ValueError, KeyError)):
        ij.prepare_native_workflow(source, steps)
    assert reads.counts().plane_reads == counts.plane_reads and reads.counts().region_reads == counts.region_reads
    assert doc.snapshot().shares_data(source)


def test_v1_stays_canonical_and_v2_requires_explicit_nullable_filter_field():
    p = ij.NativeWorkflowProgram([ij.NativeWorkflowOperation('add', {'value': 7}, target())])
    r = p.record(); assert r['version'] == 1 and 'filter_options' not in r['steps'][0]
    assert ij.NativeWorkflowProgram.from_json(p.json()).json() == p.json()
    r['steps'][0]['filter_options'] = None
    with pytest.raises(ValueError): ij.NativeWorkflowProgram.from_record(r)
    r['version'] = 2
    assert ij.NativeWorkflowProgram.from_record(r).json() == p.json()


def test_python_filter_options_refuse_untyped_or_mismatched_payloads():
    with pytest.raises(TypeError): ij.NativeWorkflowOperation('gaussian_blur_imagej', {}, target(), filter_options={'sigma': 0})
    with pytest.raises(ValueError): ij.NativeWorkflowProgram([ij.NativeWorkflowOperation('rank_filter_imagej', {}, target(), filter_options=ij.GaussianBlurCommandOptions(0, False))])


def test_authored_filter_program_reuses_native_session_batch_and_preserves_input(tmp_path):
    pixels = np.arange(25, dtype=np.uint16).reshape(5, 5)
    doc = ij.Document(ij.Dataset(pixels, axes='YX'))
    source = tmp_path / 'input.ijx'; out = tmp_path / 'out'; out.mkdir()
    capture = ij.SessionCapture([doc.snapshot()]); capture.workflow = [{'retained': True}]
    ij.save_session(str(source), capture); raw = source.read_bytes()
    original = ij.resolve_gaussian_command(doc.snapshot()).record()
    p = ij.NativeWorkflowProgram([ij.NativeWorkflowOperation('gaussian_blur_imagej', {}, target(), original=original,
        filter_options=ij.GaussianBlurCommandOptions(0, False))])
    p = ij.NativeWorkflowProgram.from_json(p.json())
    plan = ij.prepare_native_workflow_batch([ij.NativeWorkflowBatchInput(str(source), 'result')], p.steps, str(out))
    assert not (out / 'result.ijx').exists(); plan.publish()
    loaded = ij.load_session(str(out / 'result.ijx'))
    np.testing.assert_array_equal(loaded.documents[0].snapshot().dataset.numpy('YX'), pixels)
    assert loaded.workflow == [{'retained': True}] and source.read_bytes() == raw
