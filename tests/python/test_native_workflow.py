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
