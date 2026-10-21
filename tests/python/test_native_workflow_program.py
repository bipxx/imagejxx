"""Portable native intent, closed admission, lifetime and independent execution."""
import copy
import gc
import io
import json
import zipfile
import numpy as np
import pytest
import imagejxx as ij
from test_imagej_bare_native_edits import fixture as edited_fixture
from test_imagej_annotation_export import fixture as export_fixture
from test_imagej_generated_annotation_export import fixture as fresh_fixture
from test_imagej_roi_zip_export import projection


def operation(target=None):
    return ij.NativeWorkflowOperation('add', {'value': 5}, target or ij.TargetRequest(
        ij.TargetScope.ALL_Z, roi=ij.RoiPolicy.WHOLE_PLANE))


@pytest.mark.parametrize('scope', ['current', 'all', 'ranges', 'positions'])
def test_owner_free_target_intent_roundtrips_and_resolves_on_new_owner(scope):
    kwargs = {'roi': ij.RoiPolicy.WHOLE_PLANE}
    expected = np.zeros((2, 3, 4, 3, 5), np.uint16)
    if scope == 'current':
        target = ij.TargetRequest(ij.TargetScope.CURRENT_PLANE, **kwargs)
        expected[0, 0, 0] = 5
    elif scope == 'all':
        target = ij.TargetRequest(ij.TargetScope.ALL_Z, **kwargs)
        expected[0, 0] = 5
    elif scope == 'ranges':
        target = ij.TargetRequest(ij.TargetScope.RANGES, t=ij.AxisRange(1, 2),
            c=ij.AxisRange(1, 3), z=ij.AxisRange(1, None, 2), **kwargs)
        expected[1, 1:3, 1::2] = 5
    else:
        target = ij.TargetRequest(ij.TargetScope.POSITIONS,
            positions=[ij.PlanePosition(1, 2, 3), ij.PlanePosition(0, 1, 2)], **kwargs)
        expected[1, 2, 3] = 5; expected[0, 1, 2] = 5
    program = ij.NativeWorkflowProgram([operation(target)])
    record = program.record(); intent = record['steps'][0]['target']
    assert set(intent) == {'scope', 'roi', 'mode', 'destination', 't', 'c', 'z', 'positions'}
    assert 'document_id' not in program.json() and 'source_revisions' not in program.json()
    loaded = ij.NativeWorkflowProgram.from_json(program.json())
    assert loaded.record() == record and loaded.json() == program.json()
    assert ij.NativeWorkflowProgram.from_record(record).json() == program.json()
    owner = ij.Document(ij.Dataset(np.zeros_like(expected), axes='TCZYX')); before = owner.snapshot()
    prepared = ij.prepare_native_workflow(before, loaded.steps)
    np.testing.assert_array_equal(prepared.preview.dataset.numpy('TCZYX'), expected)
    assert owner.snapshot().shares_data(before) and owner.undo_steps == 0


def test_v4_mixed_import_originals_precise_native_payload_and_outputs_survive():
    owner, reads, items, rows = edited_fixture(); before = owner.snapshot()
    original = ij.resolve_imagej_annotation_import(before, items).record()
    choice = ij.ImageJAnnotationExportItem('point.roi', 0, ij.ImageJAnnotationKind.SELECTION, 0, projection(rows[0]))
    program = ij.NativeWorkflowProgram([ij.NativeWorkflowImport(items, original=original),
        operation(), ij.NativeWorkflowExport('review.zip', [choice])])
    assert reads.counts().plane_reads == reads.counts().region_reads == 0
    record = program.record()
    assert record['steps'][0]['inputs']['version'] == 4
    assert record['steps'][0]['original'] == original
    assert set(record['steps'][0]) == {'kind', 'original', 'mode', 'limits', 'inputs'}
    loaded = ij.NativeWorkflowProgram.from_json(program.json())
    a = ij.prepare_native_workflow(before, program.steps)
    b = ij.prepare_native_workflow(before, loaded.steps)
    assert b.outputs[0]['data'] == a.outputs[0]['data']
    assert b.preview.metadata == a.preview.metadata
    np.testing.assert_array_equal(b.preview.dataset.numpy('TCZYX'), a.preview.dataset.numpy('TCZYX'))
    with zipfile.ZipFile(io.BytesIO(b.outputs[0]['data'])) as z:
        result = ij.decode_imagej_roi_native_envelope(z.read('point.roi'))
    assert result.annotation.selection.points == rows[0].selection.points
    assert result.annotation.attributes == rows[0].attributes
    assert owner.snapshot().shares_data(before) and owner.undo_steps == 0


@pytest.mark.parametrize('fresh', [False, True])
@pytest.mark.parametrize('zip_output', [False, True])
def test_mixed_projection_options_and_generated_or_archived_bases_are_portable(fresh, zip_output):
    if fresh: owner, reads, _, items = fresh_fixture()
    else: owner, reads, items = export_fixture()
    selected = items if zip_output else items[:1]
    steps = [ij.NativeWorkflowExport('out.zip' if zip_output else 'out.roi', selected,
        zip=zip_output, compress=False)]
    program = ij.NativeWorkflowProgram(steps)
    loaded = ij.NativeWorkflowProgram.from_json(program.json())
    assert loaded.record() == program.record()
    before = owner.snapshot(); count = reads.counts()
    a = ij.prepare_native_workflow(before, steps); b = ij.prepare_native_workflow(before, loaded.steps)
    assert b.outputs[0]['data'] == a.outputs[0]['data']
    assert reads.counts().plane_reads == count.plane_reads and reads.counts().region_reads == count.region_reads
    assert owner.snapshot().shares_data(before)


def test_parameter_array_types_and_borrowed_step_lifetime():
    assert {'NativeWorkflowProgram', 'NativeWorkflowProgramLimits', 'NativeWorkflowOperation',
            'prepare_native_workflow', 'prepare_native_workflow_batch'} <= set(ij.__all__)
    target = ij.TargetRequest(roi=ij.RoiPolicy.WHOLE_PLANE)
    p = ij.NativeWorkflowProgram([ij.NativeWorkflowOperation('convolve',
        {'kernel': np.array([[0., 0., 0.], [0., 1., 0.], [0., 0., 0.]]), 'normalize': False}, target)])
    r = p.record()['steps'][0]['parameters']
    assert r['kernel']['kind'] == 'array' and r['kernel']['value']['shape'] == [3, 3]
    assert r['normalize'] == {'kind': 'bool', 'value': False}
    loaded = ij.NativeWorkflowProgram.from_json(p.json()); steps = loaded.steps
    del p, loaded; gc.collect()
    owner = ij.Document(ij.Dataset(np.arange(20, dtype=np.uint16).reshape(4, 5), axes='YX'))
    prepared = ij.prepare_native_workflow(owner.snapshot(), steps)
    np.testing.assert_array_equal(prepared.preview.dataset.numpy('YX'), np.arange(20, dtype=np.uint16).reshape(4, 5))


    image_record = ij.NativeWorkflowProgram([operation()]).record()
    image_step = image_record['steps'][0]
    image_step['operation'] = 'calculate'
    image_step['parameters'] = {'other': {'kind': 'image', 'value': {
        'width': 2, 'height': 2, 'channels': 1, 'type': 1, 'values': [1., 2., 3., 4.]}}}
    image_program = ij.NativeWorkflowProgram.from_record(image_record)
    image_program = ij.NativeWorkflowProgram.from_json(image_program.json())
    other = ij.Document(ij.Dataset(np.full((2, 2), 10, np.uint16), axes='YX'))
    output = ij.prepare_native_workflow(other.snapshot(), image_program.steps)
    np.testing.assert_array_equal(output.preview.dataset.numpy('YX'), [[11, 12], [13, 14]])
    unsigned = ij.NativeWorkflowProgram([ij.NativeWorkflowOperation('add', {'value': 2**64 - 1}, target)])
    assert unsigned.record()['steps'][0]['parameters']['value'] == {'kind': 'uint64', 'value': 2**64 - 1}
    assert ij.NativeWorkflowProgram.from_json(unsigned.json()).record() == unsigned.record()


def test_captured_gaussian_keeps_historical_record_separate_from_new_target():
    owner = ij.Document(ij.Dataset(np.array([[0, 255]], np.uint8), axes='YX'))
    original = ij.resolve_gaussian_command(owner.snapshot(),
        options=ij.GaussianBlurCommandOptions(1, False)).record()
    target = ij.TargetRequest(roi=ij.RoiPolicy.WHOLE_PLANE)
    p = ij.NativeWorkflowProgram([ij.NativeWorkflowOperation('gaussian_blur_imagej', {}, target,
        original=original, use_destination_calibration=True)])
    r = p.record()['steps'][0]
    assert r['original'] == original and r['use_destination_calibration']
    assert 'document_id' not in r['target']
    loaded = ij.NativeWorkflowProgram.from_json(p.json())
    result = ij.prepare_native_workflow(owner.snapshot(), loaded.steps)
    np.testing.assert_array_equal(result.preview.dataset.numpy('YX'), [[77, 178]])


@pytest.mark.parametrize('bad', ['schema', 'version', 'extra', 'empty', 'step_field', 'kind',
    'parameter_kind', 'parameter_extra', 'target_field', 'target_scope', 'target_bool',
    'target_destination', 'unused_range', 'unused_positions', 'duplicate_positions', 'range_step',
    'operation', 'geometry', 'original', 'nonfinite'])
def test_closed_program_record_rejects_malformed_or_unsupported_intent(bad):
    r = ij.NativeWorkflowProgram([operation()]).record(); s = r['steps'][0]; t = s['target']
    if bad == 'schema': r['schema'] = 'future'
    elif bad == 'version': r['version'] = 99
    elif bad == 'extra': r['output_path'] = 'forbidden.zip'
    elif bad == 'empty': r['steps'] = []
    elif bad == 'step_field': s['consent_to_write'] = True
    elif bad == 'kind': s['kind'] = 'future'
    elif bad == 'parameter_kind': s['parameters']['value']['kind'] = 'future'
    elif bad == 'parameter_extra': s['parameters']['value']['owner'] = 1
    elif bad == 'target_field': t['document_id'] = 1
    elif bad == 'target_scope': t['scope'] = 9
    elif bad == 'target_bool': t['scope'] = True
    elif bad == 'target_destination': t['destination'] = 'new_document'
    elif bad == 'unused_range': t['t'] = {'start': 0, 'stop': 1, 'step': 1}
    elif bad == 'unused_positions': t['positions'] = [[0, 0, 0]]
    elif bad == 'duplicate_positions': t['scope'] = 3; t['positions'] = [[0, 0, 0]] * 2
    elif bad == 'range_step': t['scope'] = 2; t['t'] = {'start': 0, 'stop': 2, 'step': 0}
    elif bad == 'operation': s['operation'] = 'unknown'
    elif bad == 'geometry': s['operation'] = 'rotate90'; s['parameters'] = {}
    elif bad == 'original': s['original'] = {'schema': 'bad'}
    elif bad == 'nonfinite': s['parameters']['value']['value'] = float('inf')
    with pytest.raises((ValueError, RuntimeError, IndexError)):
        ij.NativeWorkflowProgram.from_record(r)


@pytest.mark.parametrize('text', ['{}', '[]', '{"schema":1,"schema":2}',
    '{"schema":"imagejxx.native-workflow-program","version":1,"steps":[],"steps":[]}',
    '{"nested":' + '[' * 130 + '0' + ']' * 130 + '}'])
def test_json_transport_rejects_duplicate_keys_and_deep_or_invalid_roots(text):
    with pytest.raises((ValueError, RuntimeError, IndexError)):
        ij.NativeWorkflowProgram.from_json(text)


def test_record_and_transport_admission_and_cycles_are_bounded():
    limits = ij.NativeWorkflowProgramLimits(max_record_bytes=64)
    with pytest.raises(ValueError): ij.NativeWorkflowProgram([operation()], limits)
    with pytest.raises(ValueError): ij.NativeWorkflowProgram.from_json(' ' * 65, limits)
    r = ij.NativeWorkflowProgram([operation()]).record(); r['extra'] = 'x' * 128
    with pytest.raises(ValueError): ij.NativeWorkflowProgram.from_record(r, limits)
    r['cycle'] = r
    with pytest.raises(ValueError): ij.NativeWorkflowProgram.from_record(r)
    with pytest.raises(ValueError): ij.NativeWorkflowProgram([], ij.NativeWorkflowProgramLimits())
    with pytest.raises(ValueError): ij.NativeWorkflowProgram([operation(), operation()], ij.NativeWorkflowProgramLimits(max_steps=1))


@pytest.mark.parametrize('field', ['name', 'category', 'source', 'projection', 'member_extra', 'packaging'])
def test_export_choices_are_closed_and_independently_admitted(field):
    _, _, items = export_fixture()
    r = ij.NativeWorkflowProgram([ij.NativeWorkflowExport('out.zip', items)]).record()
    choices = r['steps'][0]['choices']; member = choices['members'][0]
    if field == 'name': r['steps'][0]['name'] = '../out.zip'
    elif field == 'category': member['annotation_kind'] = 'display'
    elif field == 'source': member['source_kind'] = 'generated'
    elif field == 'projection': member['projection'] = 'AAAA'
    elif field == 'member_extra': member['path'] = 'not-consent.roi'
    elif field == 'packaging': choices['packaging'] = 'unknown'
    with pytest.raises((ValueError, RuntimeError, IndexError)):
        ij.NativeWorkflowProgram.from_record(r)


@pytest.mark.parametrize('cap', [4096, 8192])
def test_same_small_record_budget_accepts_own_record_and_json(cap):
    limits = ij.NativeWorkflowProgramLimits(max_record_bytes=cap)
    p = ij.NativeWorkflowProgram([operation()], limits)
    assert ij.NativeWorkflowProgram.from_record(p.record(), limits).json() == p.json()
    assert ij.NativeWorkflowProgram.from_json(p.json(), limits).record() == p.record()


def test_escaped_wire_budget_is_checked_separately_from_valid_record():
    owner, _, items, rows = edited_fixture()
    row = rows[0]
    edited = ij.PlanarAnnotation(row.name, row.selection, row.plane, row.coordinate,
        t=row.t, c=row.c, attributes={'escaped': '\x01' * 8192})
    item = items[0]
    changed = ij.ImageJRoiImportItem(item.name, item.data, item.mapping,
        ij.ImageJRoiNativeReplacement(owner.snapshot().dataset.shape, edited))
    limits = ij.NativeWorkflowProgramLimits(max_record_bytes=40000)
    p = ij.NativeWorkflowProgram([ij.NativeWorkflowImport([changed])], limits)
    assert len(p.record()['steps'][0]['inputs']['entries'][0]['source']['native_replacement']['annotation']['attributes']['escaped']) == 8192
    with pytest.raises(ValueError, match='JSON transport'):
        p.json()


def test_legacy_registry_original_is_audit_while_explicit_parameters_and_target_execute():
    original = {'operation': 'add', 'parameters': {'value': 99},
                'selection': None, 'all_slices': False, 'slice_index': 0}
    p = ij.NativeWorkflowProgram([ij.NativeWorkflowOperation('add', {'value': 7},
        ij.TargetRequest(ij.TargetScope.ALL_Z, roi=ij.RoiPolicy.WHOLE_PLANE), original=original)])
    loaded = ij.NativeWorkflowProgram.from_json(p.json())
    assert loaded.record()['steps'][0]['original'] == original
    owner = ij.Document(ij.Dataset(np.zeros((2, 2, 3), np.uint16), axes='ZYX'))
    result = ij.prepare_native_workflow(owner.snapshot(), loaded.steps)
    np.testing.assert_array_equal(result.preview.dataset.numpy('ZYX'), np.full((2, 2, 3), 7))


def test_legacy_geometric_import_original_survives_new_v4_replacement_choices():
    from test_imagej_roi_import import owner, bare, EXTENT, ROWS
    doc, reads = owner(); item = bare()
    old = ij.resolve_imagej_roi_import(doc.snapshot(), [item]).record()
    row = ij.PlanarAnnotation('portable replacement', ij.Selection('point', [(1.250000000001, .5)]),
        'xz', 2, attributes={'opaque': 2**64 - 1})
    changed = ij.ImageJRoiImportItem(item.name, item.data, item.mapping, ij.ImageJRoiNativeReplacement(EXTENT, row))
    program = ij.NativeWorkflowProgram([ij.NativeWorkflowImport([changed], original=old)])
    loaded = ij.NativeWorkflowProgram.from_json(program.json())
    assert loaded.record()['steps'][0]['original'] == old
    assert loaded.record()['steps'][0]['inputs']['version'] == 4
    prepared = ij.prepare_native_workflow(doc.snapshot(), loaded.steps)
    current = prepared.preview.metadata['extensions'][ROWS]['rows'][0]
    assert current['name'] == row.name and current['selection']['points'] == row.selection.points
    assert current['attributes'] == row.attributes
    assert reads.counts().plane_reads == reads.counts().region_reads == 0


@pytest.mark.parametrize('zip_output', [False, True])
def test_legacy_roi_or_zip_export_original_is_retained_and_admitted(zip_output):
    if zip_output:
        from test_imagej_roi_zip_export import fixture
        doc, reads, _, items = fixture()
        old = ij.resolve_imagej_roi_zip_export(doc.snapshot(), items).record()
        choices = [ij.ImageJAnnotationExportItem(i.name, i.source_index,
            ij.ImageJAnnotationKind.SELECTION, i.annotation_index, i.standard_projection) for i in items]
    else:
        from test_imagej_roi_export_command import resolved
        doc, reads, pair, standard, command = resolved(); old = command.record()
        choices = [ij.ImageJAnnotationExportItem('annotation.roi', pair.source_index,
            ij.ImageJAnnotationKind.SELECTION, pair.annotation_index, standard)]
    step = ij.NativeWorkflowExport('out.zip' if zip_output else 'out.roi', choices,
        zip=zip_output, compress=zip_output, original=old)
    program = ij.NativeWorkflowProgram([step]); loaded = ij.NativeWorkflowProgram.from_json(program.json())
    assert loaded.record()['steps'][0]['original'] == old
    a = ij.prepare_native_workflow(doc.snapshot(), [step]); b = ij.prepare_native_workflow(doc.snapshot(), loaded.steps)
    assert a.outputs[0]['data'] == b.outputs[0]['data']
    assert reads.counts().plane_reads == reads.counts().region_reads == 0


@pytest.mark.parametrize('kind', ['registry', 'import', 'roi', 'zip'])
def test_corrupt_legacy_original_refuses_before_any_processing_read(kind):
    from test_imagej_roi_import import owner, bare
    doc, reads = owner(); item = bare()
    if kind == 'registry':
        last = ij.NativeWorkflowOperation('add', {'value': 1}, ij.TargetRequest(roi=ij.RoiPolicy.WHOLE_PLANE),
            original={'operation': 'add', 'parameters': {'unknown': 1}})
    elif kind == 'import':
        old = ij.resolve_imagej_roi_import(doc.snapshot(), [item]).record(); old['after_sources'] = None
        last = ij.NativeWorkflowImport([item], original=old)
    else:
        from test_imagej_roi_export_command import resolved
        _, _, pair, standard, command = resolved(); old = command.record(); old['output'] = 'AAAA'
        if kind == 'zip':
            from test_imagej_roi_zip_export import fixture
            other, _, _, items = fixture(); old = ij.resolve_imagej_roi_zip_export(other.snapshot(), items).record(); old['output'] = 'AAAA'
        choice = ij.ImageJAnnotationExportItem('point.roi', pair.source_index, ij.ImageJAnnotationKind.SELECTION,
            pair.annotation_index, standard)
        last = ij.NativeWorkflowExport('out.zip', [choice], original=old)
    with pytest.raises((ValueError, RuntimeError, IndexError)):
        ij.prepare_native_workflow(doc.snapshot(), [operation(), last])
    assert reads.counts().plane_reads == reads.counts().region_reads == 0


def test_decoded_projection_retention_refuses_before_decoding_a_later_bad_member():
    image = ij.ImageJImageRoi(); image.pixels = np.zeros((128, 128, 3), np.uint8)
    item = ij.ImageJAnnotationExportItem('image.roi', None, ij.ImageJAnnotationKind.DISPLAY,
        0, image, fresh_source=True)
    r = ij.NativeWorkflowProgram([ij.NativeWorkflowExport('a.zip', [item])]).record()
    later = copy.deepcopy(r['steps'][0]); later['name'] = 'b.zip'
    later['choices']['members'][0]['projection'] = 'AAAA'; r['steps'].append(later)
    with pytest.raises(ValueError, match='record-byte limit'):
        ij.NativeWorkflowProgram.from_record(r, ij.NativeWorkflowProgramLimits(max_record_bytes=100000))
