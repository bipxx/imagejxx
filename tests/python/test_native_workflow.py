"""Detached scientific workflows, explicit source choices and atomic proposals."""
import copy
import io
import zipfile
import numpy as np
import pytest
import imagejxx as ij
from test_imagej_bare_native_edits import fixture as edited_fixture
from test_imagej_annotation_export import fixture as export_fixture
from test_imagej_roi_zip_export import projection
from test_imagej_roi_import import owner, bare, SOURCES


def target():
    return ij.TargetRequest(ij.TargetScope.POSITIONS,
        positions=[ij.PlanePosition(1, 2, 5)], roi=ij.RoiPolicy.WHOLE_PLANE)


def test_v4_import_processing_exports_and_repeated_import_keep_scientific_axes():
    doc, reads, items, rows = edited_fixture()
    source = doc.snapshot()
    choice = ij.ImageJAnnotationExportItem('geometry.roi', 0,
        ij.ImageJAnnotationKind.SELECTION, 0, projection(rows[0]))
    steps = [ij.NativeWorkflowImport(items),
        ij.NativeWorkflowExport('before.zip', [choice]),
        ij.NativeWorkflowOperation('add', {'value': 9}, target()),
        ij.NativeWorkflowImport(items), ij.NativeWorkflowExport('after.zip', [choice])]
    prepared = ij.prepare_native_workflow(source, steps)
    assert doc.snapshot().shares_data(source) and doc.undo_steps == 0
    assert prepared.completed_steps == 5 and prepared.retained_bytes > 0
    assert prepared.preview.dataset.shape == source.dataset.shape
    expected = np.zeros((2, 3, 6, 8, 12), dtype=np.uint16)
    expected[1, 2, 5] = 9
    np.testing.assert_array_equal(prepared.preview.dataset.numpy('TCZYX'), expected)
    before, after = prepared.outputs
    assert [before['step_index'], after['step_index']] == [1, 4]
    assert len(before['source'].metadata['extensions'][SOURCES]['entries']) == 3
    assert len(after['source'].metadata['extensions'][SOURCES]['entries']) == 6
    assert before['source'].metadata['extensions'][SOURCES]['version'] == 4
    assert before['source'].dataset.numpy('TCZYX').sum() == 0
    np.testing.assert_array_equal(after['source'].dataset.numpy('TCZYX'), expected)
    for output in prepared.outputs:
        ij.validate_imagej_annotation_export_record(output['record'])
        with zipfile.ZipFile(io.BytesIO(output['data'])) as z:
            assert z.namelist() == ['geometry.roi']
    doc.commit(prepared.begin_edit())
    assert doc.undo_steps == 1
    final = doc.snapshot().metadata
    doc.undo(); assert doc.snapshot().shares_data(source)
    doc.redo(); assert doc.snapshot().metadata == final


@pytest.mark.parametrize('packaging', [False, True])
def test_mixed_exports_are_path_free_and_match_existing_codec(packaging, tmp_path):
    doc, reads, items = export_fixture()
    selected = items if packaging else items[:1]
    source = doc.snapshot(); counts = reads.counts()
    result = ij.prepare_native_workflow(source,
        [ij.NativeWorkflowExport('result.zip' if packaging else 'result.roi', selected, zip=packaging)])
    expected = (ij.resolve_imagej_annotation_zip_export(source, selected) if packaging
                else ij.resolve_imagej_annotation_export(source, selected[0]))
    assert result.outputs[0]['data'] == expected.data
    assert result.preview.dataset.shares_pixels(source.dataset)
    assert doc.snapshot().shares_data(source)
    assert reads.counts().plane_reads == counts.plane_reads
    assert reads.counts().region_reads == counts.region_reads
    assert list(tmp_path.iterdir()) == []


@pytest.mark.parametrize('bad', ['original', 'mapping', 'record', 'operation', 'geometry', 'destination', 'target', 'limit', 'unknown'])
def test_late_invalid_inputs_are_preflighted_before_any_kernel(bad):
    doc, reads = owner(); source = doc.snapshot()
    first = ij.NativeWorkflowOperation('add', {'value': 9}, target())
    item = bare()
    if bad == 'original':
        item = ij.ImageJRoiImportItem(item.name, b'bad', item.mapping)
        last = ij.NativeWorkflowImport([item])
    elif bad == 'mapping':
        mapping = item.mapping; mapping.coordinate = 999
        last = ij.NativeWorkflowImport([ij.ImageJRoiImportItem(item.name, item.data, mapping)])
    elif bad == 'record':
        last = ij.NativeWorkflowImport([item], original={'schema': 'bad'})
    elif bad == 'operation':
        last = ij.NativeWorkflowOperation('unknown', {}, target())
    elif bad == 'geometry':
        last = ij.NativeWorkflowOperation('resize', {'width': 6, 'height': 4}, target())
    elif bad == 'destination':
        last = ij.NativeWorkflowOperation('add', {'value': 1},
            ij.TargetRequest(destination=ij.OutputDestination.NEW_DOCUMENT))
    elif bad == 'target':
        last = ij.NativeWorkflowOperation('add', {'value': 1},
            ij.TargetRequest(ij.TargetScope.POSITIONS, positions=[ij.PlanePosition(9, 0, 0)]))
    elif bad == 'limit':
        with pytest.raises(ValueError):
            ij.prepare_native_workflow(source, [first], ij.NativeWorkflowLimits(max_retained_bytes=1))
        assert reads.counts().plane_reads == reads.counts().region_reads == 0
        return
    else:
        last = object()
    with pytest.raises((ValueError, TypeError, IndexError)):
        ij.prepare_native_workflow(source, [first, last])
    assert doc.snapshot().shares_data(source) and doc.undo_steps == 0
    assert reads.counts().plane_reads == reads.counts().region_reads == 0


def test_stale_publication_and_fresh_display_navigation():
    doc, _ = owner(); source = doc.snapshot()
    result = ij.prepare_native_workflow(source, [ij.NativeWorkflowImport([bare()])])
    display = doc.snapshot().display; display.crosshair = [2, 3, 4]; doc.set_display(display)
    doc.commit(result.begin_edit()); assert doc.snapshot().display.crosshair == [2, 3, 4]
    with pytest.raises(ij.RevisionConflict): doc.commit(result.begin_edit())
    current = doc.snapshot(); stale = ij.prepare_native_workflow(current, [ij.NativeWorkflowImport([bare()])])
    edit = doc.begin_edit(); edit.set_title('concurrent change'); doc.commit(edit)
    with pytest.raises(ij.RevisionConflict): doc.commit(stale.begin_edit())
    assert len(doc.snapshot().metadata['extensions'][SOURCES]['entries']) == 1


def test_empty_workflow_and_metadata_only_import_share_pixels():
    doc, reads = owner(); source = doc.snapshot()
    empty = ij.prepare_native_workflow(source, [])
    doc.commit(empty.begin_edit()); assert doc.undo_steps == 0
    result = ij.prepare_native_workflow(source, [ij.NativeWorkflowImport([bare()])])
    assert result.preview.dataset.shares_pixels(source.dataset)
    assert reads.counts().plane_reads == reads.counts().region_reads == 0
    assert result.preview.dataset.calibration.spacing == source.dataset.calibration.spacing


@pytest.mark.parametrize('name', ['../bad.zip', 'a/b.zip', 'a\\b.zip', 'C:bad.zip', 'bad\0.zip', '', '..'])
def test_output_names_refuse_paths_before_processing(name):
    doc, _, items = export_fixture()
    with pytest.raises(ValueError):
        ij.prepare_native_workflow(doc.snapshot(), [ij.NativeWorkflowExport(name, items)])


def test_duplicate_output_names_and_bad_projection_refuse_before_processing():
    doc, reads, items = export_fixture(); source = doc.snapshot(); counts = reads.counts()
    output = ij.NativeWorkflowExport('same.zip', items)
    with pytest.raises(ValueError):
        ij.prepare_native_workflow(source, [ij.NativeWorkflowOperation('add', {'value': 1}, target()), output, output])
    assert reads.counts().plane_reads == counts.plane_reads and doc.snapshot().shares_data(source)


@pytest.mark.parametrize('kind', ['gaussian', 'rank'])
def test_captured_filter_rebind_uses_original_intent_without_rewriting_it(kind):
    doc, _ = owner(); source = doc.snapshot()
    original = (ij.resolve_gaussian_command(source, options=ij.GaussianBlurCommandOptions(1, False)).record()
        if kind == 'gaussian' else ij.resolve_rank_command(source).record())
    saved = copy.deepcopy(original)
    operation = original['operation']
    result = ij.prepare_native_workflow(source,
        [ij.NativeWorkflowOperation(operation, {}, target(), original=original)])
    assert result.completed_steps == 1 and original == saved
    with pytest.raises(ValueError):
        ij.prepare_native_workflow(source,
            [ij.NativeWorkflowOperation(operation, {'sigma': 9}, target(), original=original)])


@pytest.mark.parametrize('value', [True, 0, -1, 4097, 1.5])
def test_limits_require_positive_bounded_real_integers(value):
    with pytest.raises((ValueError, TypeError, OverflowError)):
        ij.NativeWorkflowLimits(max_steps=value)


@pytest.mark.parametrize('how', ['before', 'after_import', 'callback_error'])
def test_cancellation_or_callback_failure_discards_whole_proposal(how):
    doc, reads = owner(); source = doc.snapshot()
    token = ij.CancellationToken(); events = []
    if how == 'before': token.cancel()
    def progress(done, total):
        events.append((done, total))
        if how == 'callback_error': raise RuntimeError('deliberate callback failure')
        token.cancel()
    with pytest.raises((RuntimeError, ij.OperationCancelled)):
        ij.prepare_native_workflow(source, [ij.NativeWorkflowImport([bare()]),
            ij.NativeWorkflowOperation('add', {'value': 9}, target())],
            cancellation=token, progress=progress)
    assert events == ([] if how == 'before' else [(1, 2)])
    assert doc.snapshot().shares_data(source) and doc.undo_steps == 0
    assert reads.counts().plane_reads == reads.counts().region_reads == 0


def test_captured_import_original_is_validated_and_preserved():
    doc, _, items, _ = edited_fixture(); source = doc.snapshot()
    command = ij.resolve_imagej_annotation_import(source, items)
    original = command.record(); saved = copy.deepcopy(original)
    result = ij.prepare_native_workflow(source,
        [ij.NativeWorkflowImport(items, original=original)])
    assert result.preview.metadata['extensions'][SOURCES] == command.provenance()
    assert original == saved and doc.snapshot().shares_data(source)
    bad = copy.deepcopy(original); bad['after_sources']['version'] = 3
    with pytest.raises(ValueError):
        ij.prepare_native_workflow(source, [ij.NativeWorkflowImport(items, original=bad)])


def test_gaussian_destination_calibration_requires_explicit_consent():
    doc, _ = owner(); source = doc.snapshot()
    original = ij.resolve_gaussian_command(source).record()
    edit = doc.begin_edit(); calibration = ij.SpatialCalibration(spacing=(2, 3, 4), unit='um')
    edit.set_calibration(calibration); doc.commit(edit)
    step = lambda consent: ij.NativeWorkflowOperation('gaussian_blur_imagej', {},
        target(), original=original, use_destination_calibration=consent)
    with pytest.raises(ValueError): ij.prepare_native_workflow(doc.snapshot(), [step(False)])
    result = ij.prepare_native_workflow(doc.snapshot(), [step(True)])
    assert result.preview.dataset.calibration.spacing == [2, 3, 4]


def test_late_export_pairing_failure_keeps_live_owner_unchanged():
    doc, _, items = export_fixture(); source = doc.snapshot()
    bad = ij.ImageJAnnotationExportItem('bad.roi', 0, ij.ImageJAnnotationKind.SELECTION,
        999, items[0].standard_projection)
    with pytest.raises((ValueError, IndexError)):
        ij.prepare_native_workflow(source, [ij.NativeWorkflowOperation('add', {'value': 1}, target()),
            ij.NativeWorkflowExport('bad.zip', [bad])])
    assert doc.snapshot().shares_data(source)


def contrast_saved_record():
    saved = ij.Document(ij.Dataset(np.array([[0, 10, 20, 40]], np.uint16), axes='YX'))
    options = ij.ContrastApplyOptions()
    options.minimum, options.maximum, options.unsigned16_bits = 0., 60., 8
    return ij.resolve_contrast_apply_command(saved.snapshot(), options).record()


def contrast_owner(dtype=np.uint16):
    pixels = np.tile(np.array([5, 10, 20, 30, 40, 60], dtype), (2, 2, 2, 2, 1))
    reads = ij.DatasetReadObserver()
    doc = ij.Document(ij.Dataset(pixels, axes='TCZYX').observe_reads(reads))
    mask = np.array([[0, 0, 1, 1, 0, 0]] * 2, np.uint8)
    doc.select(ij.Selection.from_mask(mask))
    edit = doc.begin_edit()
    edit.set_rois([ij.ROI('positioned', ij.Selection('rectangle', [(1, 0), (5, 2)]), t=1, c=1, z=0)])
    doc.commit(edit)
    view = doc.snapshot().display
    view.t, view.c, view.crosshair = 1, 1, [0, 0, 1]
    view.properties = {'display_range': [5., 60.], 'lut': 'Fire'}
    view = ij.set_manual_threshold_state(view, doc.snapshot().dataset, ij.ManualThresholdRange(15, 35))
    doc.set_display(view)
    return doc, pixels, reads


def contrast_target(scope, roi):
    policy = {'whole': ij.RoiPolicy.WHOLE_PLANE, 'active': ij.RoiPolicy.ACTIVE_SELECTION,
              'positioned': ij.RoiPolicy.POSITIONED_ROIS}[roi]
    if scope == 'current':
        return ij.TargetRequest(roi=policy), [(1, 1, 1)]
    if scope == 'all_z':
        return ij.TargetRequest(ij.TargetScope.ALL_Z, roi=policy), [(1, 1, 0), (1, 1, 1)]
    if scope == 'ranges':
        return ij.TargetRequest(ij.TargetScope.RANGES, t=ij.AxisRange(0, 2), c=ij.AxisRange(1, 2),
                                z=ij.AxisRange(0, 2), roi=policy), [(t, 1, z) for t in range(2) for z in range(2)]
    return ij.TargetRequest(ij.TargetScope.POSITIONS, roi=policy,
        positions=[ij.PlanePosition(0, 0, 1), ij.PlanePosition(1, 1, 0)]), [(0, 0, 1), (1, 1, 0)]


@pytest.mark.parametrize('dtype,bits,mapped', [
    (np.uint8, 0, [0, 0, 85, 170, 255, 255]),
    (np.uint16, 0, [0, 0, 21845, 43690, 65535, 65535]),
    (np.uint16, 12, [0, 0, 1365, 2730, 4094, 4094]),
])
@pytest.mark.parametrize('scope', ['current', 'all_z', 'ranges', 'positions'])
@pytest.mark.parametrize('roi', ['whole', 'active', 'positioned'])
def test_contrast_apply_ordered_targets_mapping_reset_and_history(dtype, bits, mapped, scope, roi):
    doc, pixels, reads = contrast_owner(dtype)
    source, undo = doc.snapshot(), doc.undo_steps
    request, positions = contrast_target(scope, roi)
    original = contrast_saved_record()
    choices = {'minimum': 10., 'maximum': 40., 'unsigned16_bits': bits}
    apply = ij.NativeWorkflowOperation('contrast_apply', choices, request, original=original)
    whole = ij.TargetRequest(ij.TargetScope.RANGES, t=ij.AxisRange(0, 2), c=ij.AxisRange(0, 2),
                            z=ij.AxisRange(0, 2), roi=ij.RoiPolicy.WHOLE_PLANE)
    histogram = lambda name: ij.NativeWorkflowHistogram(name, whole,
        authored_options=ij.NativeWorkflowHistogramOptions())
    program = ij.NativeWorkflowProgram([histogram('before.csv'), apply, histogram('after.csv')])
    wire = program.record()
    assert wire['steps'][1]['original'] == original
    replay = ij.NativeWorkflowProgram.from_json(program.json())
    assert replay.record() == wire and reads.counts().plane_reads == reads.counts().region_reads == 0
    prepared = ij.prepare_native_workflow(source, replay.steps)
    expected = pixels.copy()
    affected = []
    for position in positions:
        if roi == 'positioned' and position != (1, 1, 0):
            continue
        affected.append(position)
        if roi == 'whole':
            expected[position] = mapped
        elif roi == 'active':
            expected[position][:, 2:4] = mapped[2:4]
        else:
            expected[position][:, 1:5] = mapped[1:5]
    np.testing.assert_array_equal(prepared.preview.dataset.numpy('TCZYX'), expected)
    assert prepared.completed_steps == 3 and len(prepared.histograms) == 2
    assert [v['step_index'] for v in prepared.histograms] == [0, 2]
    assert [v['result'].sum for v in prepared.histograms] == [float(pixels.sum()), float(expected.sum())]
    props = prepared.preview.display.properties
    assert props['lut'] == source.display.properties['lut']
    if affected:
        assert ij.manual_threshold_state(prepared.preview.display, prepared.preview.dataset) is None
        if dtype == np.uint8:
            bounds = [0., 255.]
        elif bits:
            bounds = [0., 4095.]
        else:
            channel = 1 if any(p[1] == 1 for p in affected) else min(p[1] for p in affected)
            candidates = [p for p in affected if p[1] == channel]
            anchor = next((p for p in candidates if p[0] == 1 and p[2] == 1), candidates[0])
            plane = expected[anchor]
            bounds = [float(plane.min()), float(plane.max())]
        assert props['display_range'] == bounds
    else:
        assert props == source.display.properties
    assert doc.snapshot().shares_data(source) and doc.undo_steps == undo
    doc.commit(prepared.begin_edit())
    assert doc.undo_steps == undo + bool(affected)
    if affected:
        doc.undo()
        np.testing.assert_array_equal(doc.snapshot().dataset.numpy('TCZYX'), pixels)
        assert doc.snapshot().display.properties == source.display.properties
        doc.redo()
        np.testing.assert_array_equal(doc.snapshot().dataset.numpy('TCZYX'), expected)


@pytest.mark.parametrize('parameters', [
    {}, {'minimum': 0, 'maximum': 1}, {'minimum': 0, 'maximum': 1, 'unsigned16_bits': 0, 'extra': 1},
    {'minimum': True, 'maximum': 1, 'unsigned16_bits': 0},
    {'minimum': 2**64 - 1, 'maximum': 1, 'unsigned16_bits': 0},
    {'minimum': 0, 'maximum': float('inf'), 'unsigned16_bits': 0},
    {'minimum': 2, 'maximum': 1, 'unsigned16_bits': 0},
    {'minimum': 0, 'maximum': 1, 'unsigned16_bits': True},
    {'minimum': 0, 'maximum': 1, 'unsigned16_bits': 12.},
    {'minimum': 0, 'maximum': 1, 'unsigned16_bits': 9},
])
def test_contrast_authored_grammar_rejects_late_invalid_before_reads(parameters):
    doc, _, reads = contrast_owner()
    source = doc.snapshot()
    first = ij.NativeWorkflowOperation('add', {'value': 1}, ij.TargetRequest())
    invalid = ij.NativeWorkflowOperation('contrast_apply', parameters, ij.TargetRequest())
    with pytest.raises((ValueError, TypeError, RuntimeError, OverflowError)):
        ij.NativeWorkflowProgram([first, invalid])
    with pytest.raises((ValueError, TypeError, RuntimeError, OverflowError)):
        ij.prepare_native_workflow(source, [first, invalid])
    assert reads.counts().plane_reads == reads.counts().region_reads == 0 and doc.snapshot().shares_data(source)


@pytest.mark.parametrize('mode', ['original', 'calibration', 'typed_options', 'volume', 'destination', 'target', 'budget'])
def test_contrast_workflow_rejects_invalid_intent_without_source_reads(mode):
    doc, _, reads = contrast_owner()
    source = doc.snapshot()
    original = contrast_saved_record()
    request, _ = contrast_target('all_z', 'whole')
    kwargs, limits = {}, ij.NativeWorkflowLimits()
    if mode == 'original': original['profile'] = 'future'
    elif mode == 'calibration': kwargs['use_destination_calibration'] = True
    elif mode == 'typed_options': kwargs['filter_options'] = ij.GaussianBlurCommandOptions()
    elif mode == 'volume': request = ij.TargetRequest(mode=ij.ExecutionMode.VOLUME)
    elif mode == 'destination': request = ij.TargetRequest(destination=ij.OutputDestination.NEW_DOCUMENT)
    elif mode == 'target': request = ij.TargetRequest(ij.TargetScope.POSITIONS, positions=[ij.PlanePosition(9, 0, 0)])
    else: limits = ij.NativeWorkflowLimits(max_retained_bytes=1)
    step = ij.NativeWorkflowOperation('contrast_apply', {'minimum': 10., 'maximum': 40., 'unsigned16_bits': 12}, request, original=original, **kwargs)
    with pytest.raises((ValueError, TypeError, RuntimeError, IndexError)):
        ij.prepare_native_workflow(source, [step], limits)
    assert reads.counts().plane_reads == reads.counts().region_reads == 0 and doc.snapshot().shares_data(source)


@pytest.mark.parametrize('dtype,samples', [(np.float32, 1), (np.float64, 1), (np.uint8, 3), (np.uint8, 4)])
def test_disabled_apply_keeps_portable_intent_without_destination_compatibility(dtype, samples):
    reads = ij.DatasetReadObserver()
    doc = ij.Document(ij.Dataset(np.zeros((2, 4, samples), dtype), axes='YXS').observe_reads(reads))
    original = contrast_saved_record()
    target = ij.TargetRequest(ij.TargetScope.POSITIONS, positions=[ij.PlanePosition(0, 0, 999)])
    step = ij.NativeWorkflowOperation('contrast_apply', {}, target, original=original, enabled=False)
    program = ij.NativeWorkflowProgram.from_json(ij.NativeWorkflowProgram([step]).json())
    prepared = ij.prepare_native_workflow(doc.snapshot(), program.steps)
    assert prepared.completed_steps == 0 and prepared.preview.dataset.shares_pixels(doc.snapshot().dataset)
    assert reads.counts().plane_reads == reads.counts().region_reads == 0


@pytest.mark.parametrize('phase', ['pre', 'between', 'callback', 'stale'])
def test_apply_workflow_cancellation_failure_and_stale_commit_are_atomic(phase):
    doc, _, _ = contrast_owner()
    source, steps = doc.snapshot(), doc.undo_steps
    apply = ij.NativeWorkflowOperation('contrast_apply', {}, ij.TargetRequest(), original=contrast_saved_record())
    token = ij.CancellationToken()
    if phase == 'pre': token.cancel()
    def progress(done, total):
        if done == 1:
            if phase == 'between': token.cancel()
            if phase == 'callback': raise RuntimeError('controlled callback failure')
    if phase == 'stale':
        prepared = ij.prepare_native_workflow(source, [apply])
        view = source.display
        view.zoom = 2.
        doc.set_display(view)
        with pytest.raises(ij.RevisionConflict): doc.commit(prepared.begin_edit())
    else:
        with pytest.raises((RuntimeError, ij.OperationCancelled)):
            ij.prepare_native_workflow(source, [apply], cancellation=token, progress=progress)
    assert doc.snapshot().shares_data(source) and doc.undo_steps == steps


def test_conversion_after_apply_consumes_observed_reset_range():
    doc = ij.Document(ij.Dataset(np.array([[10, 20, 30, 40]], np.uint16), axes='YX'))
    doc.select(ij.Selection.from_mask(np.array([[0, 1, 0, 0]], np.uint8)))
    view = doc.snapshot().display
    view.properties = {'display_range': [10., 40.]}
    doc.set_display(view)
    before = doc.snapshot()
    apply = ij.NativeWorkflowOperation('contrast_apply',
        {'minimum': 10., 'maximum': 40., 'unsigned16_bits': 0}, ij.TargetRequest())
    whole = ij.TargetRequest(ij.TargetScope.RANGES, t=ij.AxisRange(0, 1), c=ij.AxisRange(0, 1),
                            z=ij.AxisRange(0, 1), roi=ij.RoiPolicy.WHOLE_PLANE)
    first = ij.prepare_native_workflow(before, [apply])
    assert first.preview.display.properties['display_range'] == [10., 21845.]
    prepared = ij.prepare_native_workflow(before, [apply,
        ij.NativeWorkflowOperation('convert_image', {'dtype': 'uint8', 'scale': True}, whole)])
    np.testing.assert_array_equal(prepared.preview.dataset.numpy('YX'), [[0, 255, 0, 0]])
    doc.commit(prepared.begin_edit())
    assert doc.undo_steps == 1
    doc.undo()
    np.testing.assert_array_equal(doc.snapshot().dataset.numpy('YX'), [[10, 20, 30, 40]])
    assert doc.snapshot().display.properties == before.display.properties
