"""Ordered native analysis, portable intent, bounded batch CSV publication."""
import copy
import json
import numpy as np
import pytest
import imagejxx as ij


def fixture(dtype=np.uint16):
    reads = ij.DatasetReadObserver()
    doc = ij.Document(ij.Dataset(np.arange(12, dtype=dtype).reshape(3, 2, 2), axes='ZYX').observe_reads(reads))
    target = ij.TargetRequest(ij.TargetScope.ALL_Z, roi=ij.RoiPolicy.WHOLE_PLANE)
    options = ij.TargetHistogramOptions()
    options.binning = ij.HistogramBinning.UniformRange
    options.bins = 4
    options.range = (0., 20.)
    record = ij.resolve_histogram_command(doc.snapshot(), options, target, True).record()
    return doc, reads, target, record


@pytest.mark.parametrize('dtype', [np.uint8, np.uint16, np.float32])
def test_ordered_analysis_is_detached_and_does_not_edit_owner(dtype):
    doc, reads, target, record = fixture(dtype)
    before = doc.snapshot()
    steps = [ij.NativeWorkflowHistogram('before.csv', target, record),
             ij.NativeWorkflowOperation('add', {'value': 5}, target),
             ij.NativeWorkflowHistogram('after.csv', target, record)]
    program = ij.NativeWorkflowProgram(steps)
    assert reads.counts().plane_reads == 0
    wire = json.loads(program.json())
    assert wire['version'] == 4 and all(s['enabled'] for s in wire['steps'])
    assert wire['steps'][1]['filter_options'] is None
    replay = ij.NativeWorkflowProgram.from_json(json.dumps(wire))
    assert replay.record() == program.record()
    prepared = ij.prepare_native_workflow(before, steps)
    first, last = prepared.histograms
    assert first["result"] is not prepared.histograms[0]["result"]
    assert [first['step_index'], last['step_index']] == [0, 2]
    assert [first['name'], last['name']] == ['before.csv', 'after.csv']
    assert first['record']['log_counts'] and last['record']['log_counts']
    assert first['result'].histogram['counts'].tolist() == [5, 5, 2, 0]
    assert last['result'].histogram['counts'].tolist() == [0, 5, 5, 2]
    assert first['result'].sum == 66 and last['result'].sum == 126
    assert prepared.completed_steps == 3 and prepared.outputs == []
    assert doc.snapshot().shares_data(before) and doc.undo_steps == 0
    np.testing.assert_array_equal(prepared.preview.dataset.numpy('ZYX'), np.arange(12).reshape(3,2,2) + 5)
    doc.commit(prepared.begin_edit())
    assert doc.undo_steps == 1
    doc.undo()
    assert doc.snapshot().shares_data(before)
    # Previously returned analyses survive owner/history changes and plan release.
    del prepared
    assert first['result'].sum == 66 and last['result'].sum == 126


def test_analysis_only_is_noop_and_explicit_rebind_uses_destination():
    doc, _, target, record = fixture()
    destination = ij.Document(ij.Dataset(np.full((2,2), 19, np.uint16), axes='YX'))
    before = destination.snapshot()
    p = ij.prepare_native_workflow(before, [ij.NativeWorkflowHistogram('h.csv', ij.TargetRequest(roi=ij.RoiPolicy.WHOLE_PLANE), record)])
    assert p.histograms[0]['result'].histogram['counts'].tolist() == [0,0,0,4]
    assert p.preview.dataset.shares_pixels(before.dataset)
    destination.commit(p.begin_edit())
    assert destination.undo_steps == 0
    assert p.histograms[0]['record']['target']['document_id'] != record['target']['document_id']


@pytest.mark.parametrize('bad', ['record', 'missing', 'name', 'duplicate', 'target', 'mode', 'destination', 'pixels', 'work', 'wire', 'aggregate'])
def test_entire_workflow_admitted_before_processing_reads(bad):
    doc, reads, target, record = fixture()
    limits = ij.HistogramCommandLimits()
    name = 'h.csv'
    if bad == 'record': record['log_counts'] = 1
    if bad == 'missing': record = {}
    if bad == 'name': name = '../h.csv'
    if bad == 'target': target = ij.TargetRequest(ij.TargetScope.POSITIONS, positions=[ij.PlanePosition(0,0,99)])
    if bad == 'mode': target = ij.TargetRequest(mode=ij.ExecutionMode.VOLUME)
    if bad == 'destination': target = ij.TargetRequest(destination=ij.OutputDestination.NEW_DOCUMENT)
    if bad == 'pixels': limits.processing.max_pixels = 1
    if bad == 'work': limits.processing.max_working_bytes = 1
    if bad == 'wire': limits.max_record_bytes = 1
    if bad == 'wire':
        with pytest.raises(ValueError): ij.NativeWorkflowHistogram(name, target, record, limits)
        assert reads.counts().plane_reads == 0
        return
    step = ij.NativeWorkflowHistogram(name, target, record, limits)
    steps = [ij.NativeWorkflowOperation('add', {'value': 1}, ij.TargetRequest()), step]
    if bad == 'duplicate': steps.append(step)
    options = ij.NativeWorkflowLimits(max_retained_bytes=1) if bad == 'aggregate' else ij.NativeWorkflowLimits()
    with pytest.raises((ValueError, IndexError, RuntimeError)):
        ij.prepare_native_workflow(doc.snapshot(), steps, options)
    assert reads.counts().plane_reads == 0 and doc.undo_steps == 0


def test_disabled_analysis_validates_intent_but_not_destination_and_produces_no_result():
    doc, reads, _, record = fixture()
    bad_target = ij.TargetRequest(ij.TargetScope.POSITIONS, positions=[ij.PlanePosition(0,0,99)])
    step = ij.NativeWorkflowHistogram('skip.csv', bad_target, record, enabled=False)
    program = ij.NativeWorkflowProgram([step])
    assert program.record()['version'] == 4
    p = ij.prepare_native_workflow(doc.snapshot(), [step])
    assert p.completed_steps == 0 and p.histograms == [] and reads.counts().plane_reads == 0
    broken = copy.deepcopy(record); broken['profile'] = 'unknown'
    with pytest.raises(ValueError):
        ij.NativeWorkflowProgram([ij.NativeWorkflowHistogram('skip.csv', bad_target, broken, enabled=False)])


@pytest.mark.parametrize('mutate', [
    lambda r: r.update(version=3),
    lambda r: r['steps'][0].update(enabled=1),
    lambda r: r['steps'][0].update(extra=True),
    lambda r: r['steps'][0]['limits'].update(max_pixels=True),
    lambda r: r['steps'][0]['limits'].update(max_working_bytes=0),
    lambda r: r['steps'][0]['limits'].update(max_record_bytes=0),
    lambda r: r['steps'][0]['limits'].update(extra=1),
    lambda r: r['steps'][0].update(original=None),
])
def test_closed_v4_wire(mutate):
    doc, reads, target, record = fixture()
    wire = ij.NativeWorkflowProgram([ij.NativeWorkflowHistogram('h.csv', target, record)]).record()
    mutate(wire)
    with pytest.raises((ValueError, TypeError, IndexError)):
        ij.NativeWorkflowProgram.from_record(wire)
    assert reads.counts().plane_reads == 0


def test_cancellation_at_analysis_completion_returns_no_partial_workflow():
    doc, _, target, record = fixture()
    token = ij.CancellationToken()
    events = []
    def progress(done, total):
        events.append((done, total))
        if done == total: token.cancel()
    with pytest.raises(RuntimeError):
        ij.prepare_native_workflow(doc.snapshot(), [ij.NativeWorkflowHistogram('h.csv', target, record)], cancellation=token, progress=progress)
    assert events and doc.undo_steps == 0


@pytest.mark.parametrize('all_documents', [False, True])
def test_batch_csv_and_session_preservation(tmp_path, all_documents):
    doc, _, target, record = fixture()
    source = tmp_path/'input.ijx'
    other = ij.Document(ij.Dataset(np.full((2,2), 19, np.uint16), axes='YX'))
    capture = ij.SessionCapture([doc.snapshot(), other.snapshot()])
    capture.results = [{'keep': 42}]
    ij.save_session(str(source), capture)
    original = source.read_bytes()
    output = tmp_path/'out'; output.mkdir()
    steps = [ij.NativeWorkflowHistogram('h.csv', target, record)]
    program = ij.NativeWorkflowProgram(steps)
    inputs = [ij.NativeWorkflowBatchInput(str(source), 'result', program=program)]
    batch = ij.prepare_native_workflow_batch(inputs, [], str(output), ij.NativeWorkflowBatchOptions(all_documents=all_documents))
    assert not (output/'result.d1.h.csv').exists()
    batch.publish()
    assert (output/'result.d1.h.csv').read_text() == ij.target_histogram_csv(ij.prepare_native_workflow(doc.snapshot(), steps).histograms[0]['result'])
    assert (output/'result.d2.h.csv').exists() == all_documents
    decoded = ij.load_session(str(output/'result.ijx'))
    assert decoded.results == [{'keep': 42}]
    np.testing.assert_array_equal(decoded.documents[0].snapshot().dataset.numpy('ZYX'), np.arange(12).reshape(3,2,2))
    assert source.read_bytes() == original


@pytest.mark.parametrize('race', [False, True])
def test_batch_output_conflict_preserves_foreign_file(tmp_path, race):
    doc, _, target, record = fixture()
    source = tmp_path/'input.ijx'; ij.save_session(str(source), ij.SessionCapture([doc.snapshot()]))
    output = tmp_path/'out'; output.mkdir()
    path = output/'result.d1.h.csv'
    def prepare():
        return ij.prepare_native_workflow_batch([ij.NativeWorkflowBatchInput(str(source), 'result')], [ij.NativeWorkflowHistogram('h.csv', target, record)], str(output))
    if race: batch = prepare()
    path.write_bytes(b'foreign winner')
    with pytest.raises((ValueError, RuntimeError)):
        if race: batch.publish()
        else: prepare()
    assert path.read_bytes() == b'foreign winner' and not (output/'result.ijx').exists()


def test_destination_selection_and_late_unsupported_dtype_are_preflighted():
    doc, reads, target, record = fixture()
    edit = doc.begin_edit(); edit.select(ij.Selection('rectangle', [(0,0),(1,2)])); doc.commit(edit)
    active = ij.TargetRequest(ij.TargetScope.ALL_Z, roi=ij.RoiPolicy.ACTIVE_SELECTION)
    p = ij.prepare_native_workflow(doc.snapshot(), [ij.NativeWorkflowHistogram('roi.csv', active, record)])
    result = p.histograms[0]['result']
    assert result.finite_count == 6 and result.sum == 30
    assert result.histogram['counts'].tolist() == [3,2,1,0]
    before = reads.counts().plane_reads; history = doc.undo_steps
    ranges = ij.TargetRequest(ij.TargetScope.RANGES, t=ij.AxisRange(0,1), c=ij.AxisRange(0,1), z=ij.AxisRange(0,3), roi=ij.RoiPolicy.WHOLE_PLANE)
    convert = ij.NativeWorkflowOperation('convert_image', {'dtype': 'float64', 'scale': False}, ranges)
    with pytest.raises(ValueError):
        ij.prepare_native_workflow(doc.snapshot(), [convert, ij.NativeWorkflowHistogram('unsupported.csv', target, record)])
    assert reads.counts().plane_reads == before and doc.undo_steps == history
