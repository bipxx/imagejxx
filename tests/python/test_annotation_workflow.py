"""Native annotation workflow intent, stage ordering and closed V7 transport."""
import copy
import json
import numpy as np
import pytest
import imagejxx as ij

EXTENT = [1, 1, 3, 10, 12, 1]
COMMANDS = [0, 1.125, 1.25, 2, 3.25, -2, 7.125, 1.25,
            3, 9, 2, 9, 6, 7, 6, 1, 1.125, 6, 4]


def area():
    return ij.Selection.from_path(ij.ImageJCompositePath(COMMANDS)).with_attributes({'opaque': ['kept', 2**64-1]})


def owner(saved=False):
    reads = ij.DatasetReadObserver()
    doc = ij.Document(ij.Dataset(np.arange(360, dtype=np.uint16).reshape(3, 10, 12), axes='ZYX').observe_reads(reads))
    if saved:
        edit = doc.snapshot().begin_edit()
        edit.set_rois([ij.ROI('saved', area(), attributes={'row': ['kept']})])
        doc.commit(edit)
    return doc, reads


def operation(request, extent=EXTENT, original=None, enabled=True):
    kind = 'planar_annotation' if isinstance(request, ij.PlanarAnnotationRequest) else 'saved_roi_geometry'
    target = ij.TargetRequest(roi=ij.RoiPolicy.WHOLE_PLANE)
    intent = ij.NativeWorkflowAnnotationIntent(extent, request)
    return ij.NativeWorkflowOperation(kind, {}, target, original=original, enabled=enabled, annotation_intent=intent)


def store(plane='xy'):
    return ij.PlanarAnnotationRequest('store',
        annotation=ij.PlanarAnnotation('curve', area(), plane, 0, attributes={'row': ['kept']}))


@pytest.mark.parametrize('plane', ['xy', 'xz', 'yz'])
def test_planar_path_portable_stage_and_one_publication(plane):
    doc, reads = owner(); before = doc.snapshot()
    add = operation(store(plane))
    moved = ij.SelectionEditor(area(), (10, 12)).moved([.25, -.5])
    update = operation(ij.PlanarAnnotationRequest('update', index=0,
        annotation=ij.PlanarAnnotation('edited', moved, plane, 0, attributes={'row': ['kept']})))
    program = ij.NativeWorkflowProgram([add, update])
    record = program.record(); assert record['version'] == 7
    restored = ij.NativeWorkflowProgram.from_json(program.json())
    assert restored.record() == record
    result = ij.prepare_native_workflow(before, restored.steps)
    assert ij.planar_annotations(result.preview)[0].selection == moved
    assert result.preview.dataset.shares_pixels(before.dataset)
    assert doc.snapshot().shares_data(before) and doc.undo_steps == 0
    doc.commit(result.begin_edit()); assert doc.undo_steps == 1
    doc.undo(); assert doc.snapshot().shares_data(before)
    doc.redo(); assert ij.planar_annotations(doc.snapshot())[0].selection == moved
    assert reads.counts().plane_reads == reads.counts().region_reads == 0


def test_saved_native_path_preserves_rows_selection_pixels_and_history():
    doc, reads = owner(True); before = doc.snapshot(); history = doc.undo_steps
    moved = ij.SelectionEditor(area(), (10, 12)).moved_vertex(1, [3.7500000000000004, .8750000000000001])
    request = ij.SavedRoiGeometryRequest(0, moved)
    captured = ij.resolve_saved_roi_geometry(before, request).record()
    assert captured['version'] == 4
    decoded = ij.decode_saved_roi_geometry_request(captured)
    assert decoded.row_index == 0 and decoded.replacement == moved
    step = operation(request, original=captured)
    result = ij.prepare_native_workflow(before, ij.NativeWorkflowProgram([step]).steps)
    assert result.preview.rois[0].selection == moved
    assert result.preview.rois[0].attributes == {'row': ['kept']}
    assert result.preview.selection is None and result.preview.previous_selection is None
    doc.commit(result.begin_edit()); assert doc.undo_steps == history+1
    doc.undo(); assert doc.snapshot().shares_data(before)
    doc.redo(); assert doc.snapshot().rois[0].selection == moved
    assert reads.counts().plane_reads == reads.counts().region_reads == 0


@pytest.mark.parametrize('version', range(1, 7))
def test_older_programs_refuse_annotation_steps(version):
    record = ij.NativeWorkflowProgram([operation(store())]).record()
    record['version'] = version
    with pytest.raises(ValueError): ij.NativeWorkflowProgram.from_record(record)


@pytest.mark.parametrize('mutation', ['kind', 'extent', 'unused', 'empty-v7', 'missing-intent', 'target'])
def test_malformed_claims_refuse_before_publication(mutation):
    doc, _ = owner(); before = doc.snapshot()
    record = copy.deepcopy(ij.NativeWorkflowProgram([operation(store())]).record())
    step = record['steps'][0]
    if mutation == 'kind': step['annotation_intent']['kind'] = 'saved_roi'
    elif mutation == 'extent': step['annotation_intent']['extent'][3] = True
    elif mutation == 'unused': step['annotation_intent']['request']['ignored'] = 1
    elif mutation == 'empty-v7': step['operation'] = 'invert'
    elif mutation == 'missing-intent': step['annotation_intent'] = None
    else: step['target']['roi'] = int(ij.RoiPolicy.ACTIVE_SELECTION)
    with pytest.raises(ValueError): ij.NativeWorkflowProgram.from_record(record)
    assert doc.snapshot().shares_data(before)


def test_disabled_annotation_retains_intent_without_destination_compatibility():
    doc, _ = owner(); before = doc.snapshot()
    step = operation(store(), extent=[1, 1, 3, 11, 12, 1], enabled=False)
    result = ij.prepare_native_workflow(before, ij.NativeWorkflowProgram([step]).steps)
    assert result.completed_steps == 0 and result.preview.dataset.shares_pixels(before.dataset)
    assert result.preview.metadata == before.metadata and result.preview.rois == []
    enabled = operation(store(), extent=[1, 1, 3, 11, 12, 1])
    with pytest.raises(ValueError, match='extent'): ij.prepare_native_workflow(before, [enabled])
    assert doc.snapshot().shares_data(before)


def test_saved_noop_and_view_revision_guard():
    doc, reads = owner(True); before = doc.snapshot(); history = doc.undo_steps
    noop = ij.prepare_native_workflow(before, [operation(ij.SavedRoiGeometryRequest(0, area()))])
    doc.commit(noop.begin_edit()); assert doc.undo_steps == history
    moved = ij.SelectionEditor(area(), (10, 12)).moved([.5, .25])
    result = ij.prepare_native_workflow(doc.snapshot(), [operation(ij.SavedRoiGeometryRequest(0, moved))])
    view = doc.snapshot().display; view.crosshair = [0, 0, 1]; doc.set_display(view)
    with pytest.raises(ij.RevisionConflict): doc.commit(result.begin_edit())
    assert doc.snapshot().rois[0].selection == area() and doc.undo_steps == history
    assert reads.counts().plane_reads == reads.counts().region_reads == 0


def test_planar_metadata_publication_keeps_fresh_navigation():
    doc, _ = owner(); result = ij.prepare_native_workflow(doc.snapshot(), [operation(store())])
    view = doc.snapshot().display; view.crosshair = [0, 0, 1]; doc.set_display(view)
    doc.commit(result.begin_edit())
    assert doc.snapshot().display.crosshair == [0, 0, 1]
    assert ij.planar_annotations(doc.snapshot())[0].selection.path.commands == COMMANDS


def test_invalid_original_and_small_retention_refuse_before_callbacks_or_samples():
    doc, reads = owner(); before = doc.snapshot()
    captured = ij.resolve_planar_annotation_command(before, store()).record(); captured['ignored'] = True
    step = operation(store(), original=captured); calls = []
    with pytest.raises(ValueError): ij.prepare_native_workflow(before, [step], progress=lambda *_: calls.append(1))
    assert calls == []
    with pytest.raises(ValueError): ij.NativeWorkflowProgram([operation(store())], ij.NativeWorkflowProgramLimits(max_record_bytes=1000))
    with pytest.raises(ValueError): ij.prepare_native_workflow(before, [operation(store())],
        ij.NativeWorkflowLimits(max_retained_bytes=1000), progress=lambda *_: calls.append(1))
    assert calls == [] and doc.snapshot().shares_data(before)
    assert reads.counts().plane_reads == reads.counts().region_reads == 0


def test_unrelated_saved_geometry_is_charged_before_detached_owner_or_callbacks():
    doc, reads = owner()
    geometry = ij.Selection('polyline', [[float(i % 12), float(i % 10)] for i in range(10000)])
    edit = doc.snapshot().begin_edit()
    edit.set_rois([ij.ROI('unrelated large row', geometry)])
    doc.commit(edit)
    before = doc.snapshot(); calls = []
    with pytest.raises(ValueError, match='retained-byte'):
        ij.prepare_native_workflow(before, [operation(store())],
            ij.NativeWorkflowLimits(max_retained_bytes=80000), progress=lambda *_: calls.append(1))
    assert calls == [] and doc.snapshot().shares_data(before)
    assert reads.counts().plane_reads == reads.counts().region_reads == 0


@pytest.mark.parametrize('saved', [False, True])
def test_capture_only_replay_derives_complete_intent_and_keeps_original(saved):
    doc, reads = owner(saved); before = doc.snapshot()
    if saved:
        moved = ij.SelectionEditor(area(), (10, 12)).moved([.25, .5])
        request = ij.SavedRoiGeometryRequest(0, moved)
        captured = ij.resolve_saved_roi_geometry(before, request).record()
    else:
        captured = ij.resolve_planar_annotation_command(before, store()).record()
    step = ij.NativeWorkflowOperation('saved_roi_geometry' if saved else 'planar_annotation', {},
        ij.TargetRequest(roi=ij.RoiPolicy.WHOLE_PLANE), original=captured)
    restored = ij.NativeWorkflowProgram.from_json(ij.NativeWorkflowProgram([step]).json())
    assert restored.record()['steps'][0]['original'] == captured
    result = ij.prepare_native_workflow(before, restored.steps)
    if saved: assert result.preview.rois[0].selection == moved
    else: assert ij.planar_annotations(result.preview)[0].selection == area()
    assert doc.snapshot().shares_data(before)
    assert reads.counts().plane_reads == reads.counts().region_reads == 0


def test_explicit_extent_rebind_preserves_capture_and_authors_new_geometry():
    original_doc, _ = owner()
    captured = ij.resolve_planar_annotation_command(original_doc.snapshot(), store()).record()
    destination = ij.Document(ij.Dataset(np.zeros((4, 11, 13), dtype=np.uint16), axes='ZYX'))
    changed = ij.PlanarAnnotationRequest('store', annotation=ij.PlanarAnnotation('rebound', area(), 'xz', 2))
    step = operation(changed, extent=[1, 1, 4, 11, 13, 1], original=captured)
    program = ij.NativeWorkflowProgram.from_json(ij.NativeWorkflowProgram([step]).json())
    result = ij.prepare_native_workflow(destination.snapshot(), program.steps)
    row = ij.planar_annotations(result.preview)[0]
    assert row.name == 'rebound' and row.plane == 'xz' and row.coordinate == 2
    assert program.record()['steps'][0]['original'] == captured


def test_invalid_late_capture_rejects_before_earlier_filter_reads():
    doc, reads = owner(); before = doc.snapshot()
    captured = ij.resolve_planar_annotation_command(before, store()).record()
    captured['ignored'] = True
    early = ij.NativeWorkflowOperation('add', {'value': 5}, ij.TargetRequest())
    calls = []
    with pytest.raises(ValueError):
        ij.prepare_native_workflow(before, [early, operation(store(), original=captured)],
            progress=lambda *_: calls.append(1))
    assert calls == [] and doc.snapshot().shares_data(before)
    assert reads.counts().plane_reads == reads.counts().region_reads == 0


def test_v7_session_batch_replays_each_private_annotation_stage(tmp_path):
    doc, _ = owner(); program = ij.NativeWorkflowProgram([operation(store('xz')),
        operation(ij.PlanarAnnotationRequest('delete', index=0)), operation(store('yz'))])
    source = tmp_path/'source.ijx'
    ij.save_session(str(source), ij.SessionCapture([doc], workflow=[program.record()]))
    assert ij.load_session(str(source)).workflow == [program.record()]
    output = tmp_path/'output'; output.mkdir()
    plan = ij.prepare_native_workflow_batch([ij.NativeWorkflowBatchInput(str(source), 'result')],
        program.steps, str(output))
    assert not (output/'result.ijx').exists()
    plan.publish()
    restored = ij.load_session(str(output/'result.ijx')).documents[0]
    rows = ij.planar_annotations(restored.snapshot())
    assert len(rows) == 1 and rows[0].plane == 'yz' and rows[0].selection == area()


def test_saved_batch_missing_row_never_publishes_an_earlier_input(tmp_path):
    valid, _ = owner(True); missing, _ = owner()
    sources = []
    for name, doc in [('valid', valid), ('missing', missing)]:
        path = tmp_path/(name+'.ijx'); ij.save_session(str(path), ij.SessionCapture([doc]))
        sources.append(ij.NativeWorkflowBatchInput(str(path), name))
    output = tmp_path/'output'; output.mkdir()
    moved = ij.SelectionEditor(area(), (10, 12)).moved([.25, .5])
    with pytest.raises(ValueError):
        ij.prepare_native_workflow_batch(sources, [operation(ij.SavedRoiGeometryRequest(0, moved))], str(output))
    assert list(output.iterdir()) == []
