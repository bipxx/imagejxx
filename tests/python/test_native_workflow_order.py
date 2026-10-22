"""Retained disabled intent, ordered execution and V3 compatibility."""
import copy
import json
import numpy as np
import pytest
import imagejxx as ij
from test_imagej_bare_native_edits import fixture as edited_fixture
from test_imagej_annotation_export import fixture as export_fixture
from test_imagej_roi_import import owner
from test_imagej_roi_zip_export import projection


def target():
    return ij.TargetRequest(ij.TargetScope.ALL_Z, roi=ij.RoiPolicy.WHOLE_PLANE)


def op(name='add', value=5, enabled=True):
    return ij.NativeWorkflowOperation(name, {'value': value}, target(), enabled=enabled)


@pytest.mark.parametrize('reverse, expected', [(False, 30), (True, 25)])
def test_order_is_native_intent_and_preserves_full_captured_audit(reverse, expected):
    doc = ij.Document(ij.Dataset(np.full((2, 3, 4), 10, np.uint16), axes='ZYX')); source = doc.snapshot()
    original = {'operation': 'add', 'parameters': {'value': 1}, 'all_slices': False}
    steps = [ij.NativeWorkflowOperation('add', {'value': 5}, target(), original=original), op('multiply', 2)]
    if reverse: steps.reverse()
    p = ij.NativeWorkflowProgram(steps); loaded = ij.NativeWorkflowProgram.from_json(p.json())
    result = ij.prepare_native_workflow(source, loaded.steps)
    np.testing.assert_array_equal(result.preview.dataset.numpy('ZYX'), np.full((2, 3, 4), expected))
    assert result.completed_steps == 2 and doc.snapshot().shares_data(source)
    assert next(s for s in loaded.record()['steps'] if s['operation'] == 'add')['original'] == original


def test_all_disabled_is_zero_execution_zero_reads_and_zero_undo():
    doc, reads = owner(); source = doc.snapshot(); counts = reads.counts()
    # A valid owner-free target can be incompatible with this destination.
    outside = ij.TargetRequest(ij.TargetScope.POSITIONS, positions=[ij.PlanePosition(999, 999, 999)])
    p = ij.NativeWorkflowProgram([ij.NativeWorkflowOperation('add', {'value': 9}, outside, enabled=False), op(enabled=False)])
    r = p.record(); assert r['version'] == 3 and all(s['enabled'] is False for s in r['steps'])
    loaded = ij.NativeWorkflowProgram.from_json(p.json()); assert all(not s.enabled for s in loaded.steps)
    result = ij.prepare_native_workflow(source, loaded.steps)
    assert result.completed_steps == 0 and result.outputs == [] and result.preview.dataset.shares_pixels(source.dataset)
    doc.commit(result.begin_edit())
    assert doc.snapshot().shares_data(source) and doc.undo_steps == 0
    assert reads.counts().plane_reads == counts.plane_reads and reads.counts().region_reads == counts.region_reads


@pytest.mark.parametrize('kind', ['import', 'export', 'rank'])
def test_disabled_steps_never_bind_incompatible_destinations(kind):
    old, _, imports, rows = edited_fixture()
    if kind == 'import':
        step = ij.NativeWorkflowImport(imports, enabled=False)
    elif kind == 'export':
        item = ij.ImageJAnnotationExportItem('unbound.roi', 999, ij.ImageJAnnotationKind.SELECTION, 999, projection(rows[0]))
        step = ij.NativeWorkflowExport('unbound.zip', [item], enabled=False)
    else:
        original = ij.resolve_rank_command(old.snapshot()).record()
        step = ij.NativeWorkflowOperation('rank_filter_imagej', {}, target(), original=original,
            filter_options=ij.RankFilterCommandOptions('mean', 0), enabled=False)
    # RGB is incompatible with rank; extent and missing archives differ too.
    doc = ij.Document(ij.Dataset(np.zeros((2, 3, 3), np.uint8), axes='YXS'))
    source = doc.snapshot(); p = ij.NativeWorkflowProgram([step])
    loaded = ij.NativeWorkflowProgram.from_record(p.record())
    result = ij.prepare_native_workflow(source, loaded.steps)
    assert result.completed_steps == 0 and result.outputs == [] and doc.snapshot().shares_data(source)


def test_disabled_export_retains_capture_and_original_program_output_indices():
    doc, reads, imports, rows = edited_fixture(); source = doc.snapshot()
    item = ij.ImageJAnnotationExportItem('point.roi', 0, ij.ImageJAnnotationKind.SELECTION, 0, projection(rows[0]))
    steps = [ij.NativeWorkflowImport(imports), ij.NativeWorkflowExport('off.zip', [item], enabled=False),
        op(), ij.NativeWorkflowExport('on.zip', [item])]
    p = ij.NativeWorkflowProgram(steps); result = ij.prepare_native_workflow(source, ij.NativeWorkflowProgram.from_json(p.json()).steps)
    assert result.completed_steps == 3 and [o['step_index'] for o in result.outputs] == [3]
    assert [o['name'] for o in result.outputs] == ['on.zip']
    assert p.record()['steps'][1]['choices'] == p.record()['steps'][3]['choices']
    assert doc.snapshot().shares_data(source)


@pytest.mark.parametrize('kind', ['operation', 'import', 'export'])
def test_disabled_invalid_originals_are_refused_before_an_earlier_kernel(kind):
    doc, reads, imports, rows = edited_fixture(); source = doc.snapshot(); counts = reads.counts()
    if kind == 'operation':
        original = ij.resolve_gaussian_command(source).record(); original['effective_sigma_xy'] = [9, 9]
        last = ij.NativeWorkflowOperation('gaussian_blur_imagej', {}, target(), original=original, enabled=False)
    elif kind == 'import':
        original = ij.resolve_imagej_annotation_import(source, imports).record(); original['version'] = 999
        last = ij.NativeWorkflowImport(imports, original=original, enabled=False)
    else:
        _, _, items = export_fixture()
        original = {'schema': 'unsupported.disabled.export'}
        last = ij.NativeWorkflowExport('disabled.zip', items, original=original, enabled=False)
    with pytest.raises((ValueError, KeyError)):
        ij.prepare_native_workflow(source, [op(), last])
    assert reads.counts().plane_reads == counts.plane_reads and reads.counts().region_reads == counts.region_reads
    assert doc.snapshot().shares_data(source)


@pytest.mark.parametrize('bad', ['missing', 'integer', 'null', 'text', 'extra', 'v1', 'v2', 'future', 'target', 'parameter'])
def test_closed_v3_requires_explicit_bool_and_retains_disabled_validation(bad):
    r = ij.NativeWorkflowProgram([op(enabled=False)]).record(); s = r['steps'][0]
    if bad == 'missing': del s['enabled']
    elif bad == 'integer': s['enabled'] = 0
    elif bad == 'null': s['enabled'] = None
    elif bad == 'text': s['enabled'] = 'false'
    elif bad == 'extra': s['skip'] = True
    elif bad == 'v1': r['version'] = 1
    elif bad == 'v2': r['version'] = 2
    elif bad == 'future': r['version'] = 6
    elif bad == 'target': s['target']['positions'] = [[1,2,3]]
    else: s['parameters']['value']['kind'] = 'future'
    with pytest.raises((ValueError, KeyError)): ij.NativeWorkflowProgram.from_record(r)
    with pytest.raises((ValueError, KeyError)): ij.NativeWorkflowProgram.from_json(json.dumps(r))


@pytest.mark.parametrize('kind', ['import', 'export'])
def test_closed_v3_enabled_field_is_required_for_non_operation_steps(kind):
    doc, _, imports, rows = edited_fixture()
    step = ij.NativeWorkflowImport(imports, enabled=False) if kind == 'import' else ij.NativeWorkflowExport('off.zip',
        [ij.ImageJAnnotationExportItem('point.roi', 0, ij.ImageJAnnotationKind.SELECTION, 0, projection(rows[0]))], enabled=False)
    r = ij.NativeWorkflowProgram([step]).record(); del r['steps'][0]['enabled']
    with pytest.raises(ValueError): ij.NativeWorkflowProgram.from_record(r)


def test_older_versions_remain_canonical_and_all_true_v3_downgrades():
    for step in [op(), ij.NativeWorkflowOperation('gaussian_blur_imagej', {}, target(), filter_options=ij.GaussianBlurCommandOptions(0, False))]:
        p = ij.NativeWorkflowProgram([step]); r = p.record(); version = r['version']
        assert version in [1,2] and 'enabled' not in r['steps'][0]
        r['version'] = 3; r['steps'][0]['enabled'] = True
        r['steps'][0].setdefault('filter_options', None)
        assert ij.NativeWorkflowProgram.from_record(r).json() == p.json()


@pytest.mark.parametrize('kind', ['operation', 'import', 'export'])
def test_enabled_python_flags_are_strict_and_read_only(kind):
    doc, _, imports, rows = edited_fixture()
    if kind == 'operation': make = lambda flag: ij.NativeWorkflowOperation('add', {'value': 1}, target(), enabled=flag)
    elif kind == 'import': make = lambda flag: ij.NativeWorkflowImport(imports, enabled=flag)
    else: make = lambda flag: ij.NativeWorkflowExport('off.zip', [ij.ImageJAnnotationExportItem('p.roi', 0, ij.ImageJAnnotationKind.SELECTION, 0, projection(rows[0]))], enabled=flag)
    with pytest.raises(TypeError): make(0)
    step = make(False); assert step.enabled is False
    with pytest.raises(AttributeError): step.enabled = True


def test_disabled_steps_count_against_model_and_planner_limits():
    doc, reads = owner(); counts = reads.counts(); steps = [op(enabled=False), op(enabled=False)]
    with pytest.raises(ValueError): ij.NativeWorkflowProgram(steps, ij.NativeWorkflowProgramLimits(max_steps=1))
    with pytest.raises(ValueError): ij.prepare_native_workflow(doc.snapshot(), steps, ij.NativeWorkflowLimits(max_steps=1))
    with pytest.raises(ValueError): ij.prepare_native_workflow(doc.snapshot(), steps, ij.NativeWorkflowLimits(max_retained_bytes=1))
    assert reads.counts().plane_reads == counts.plane_reads


def test_all_disabled_session_batch_preserves_input_tables_and_outputs_only_sessions(tmp_path):
    doc = ij.Document(ij.Dataset(np.arange(20, dtype=np.uint16).reshape(4,5), axes='YX'))
    source = tmp_path/'input.ijx'; out = tmp_path/'out'; out.mkdir()
    capture = ij.SessionCapture([doc.snapshot()]); capture.workflow = [{'unexecuted': True}]
    ij.save_session(str(source), capture); raw = source.read_bytes()
    p = ij.NativeWorkflowProgram([op(enabled=False)])
    result = ij.prepare_native_workflow_batch([ij.NativeWorkflowBatchInput(str(source),'copy')],
        ij.NativeWorkflowProgram.from_json(p.json()).steps,str(out))
    assert len(result.outputs) == 1 and result.outputs[0]['session']
    result.publish(); loaded = ij.load_session(str(out/'copy.ijx'))
    np.testing.assert_array_equal(loaded.documents[0].snapshot().dataset.numpy('YX'), np.arange(20).reshape(4,5))
    assert loaded.workflow == capture.workflow and source.read_bytes() == raw
