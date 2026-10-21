"""Thresholded variance: pinned runner observations and plane-scoped policy."""
import copy
import json
import os
from pathlib import Path

import imagejxx as ij
import numpy as np
import pytest

CORPUS = json.loads((Path(__file__).parents[1] / 'fixtures/variance-threshold-imagej154r.json').read_bytes())


def pixels(row, phase):
    return np.stack([np.frombuffer(bytes.fromhex(v), dtype=row['dtype']).reshape(20, 25)
                     for v in row[phase + '_hex']])


def threshold(z=1):
    return {'version': 1, 'lower': 4., 'upper': 8., 't': 0, 'c': 0, 'z': z}


def test_malformed_threshold_is_rejected_without_reads():
    reads = ij.DatasetReadObserver()
    owner = ij.Document(ij.Dataset(np.ones((3, 3), np.uint16), axes='YX').observe_reads(reads))
    display = owner.snapshot().display
    display.properties = {'manual_threshold': dict(threshold(0), version=999)}
    with pytest.raises(ValueError, match='threshold display schema'):
        owner.set_display(display)
    assert reads.counts().plane_reads == 0 and owner.undo_steps == 0
    assert owner.snapshot().display.properties == {}


def owner_for(row, opaque=True):
    reads = ij.DatasetReadObserver()
    owner = ij.Document(ij.Dataset(pixels(row, 'input'), axes='ZYX').observe_reads(reads))
    edit = owner.begin_edit()
    if row['selection'] == 1:
        edit.select(ij.Selection('rectangle', [[2, 3], [21, 16]]))
    elif row['selection'] == 2:
        edit.select(ij.Selection('polygon', [[2, 3], [20, 2], [18, 17], [5, 15]]))
    owner.commit(edit)
    display = owner.snapshot().display
    display.crosshair = [12, 10, 1]
    properties = {'display_range': row['initial_range'], 'manual_threshold': threshold(), 'lut': 'Inverted'}
    if opaque:
        properties['future'] = {'keep': True}
    display.properties = properties
    owner.set_display(display)
    return ij.Document(owner.snapshot()), reads


@pytest.mark.parametrize('legacy', [False, True], ids=['native', 'desktop-adapter'])
@pytest.mark.parametrize('row', CORPUS['cases'], ids=lambda r: f"{r['dtype']}-s{r['signal']}-m{r['mode']}-a{r['selection']}-l{r['lut_mode']}")
def test_runner_threshold_pixels_range_and_history(tmp_path, row, legacy):
    owner, reads = owner_for(row, opaque=not legacy)
    before = owner.snapshot()
    request = ij.TargetRequest(ij.TargetScope.ALL_Z if row['mode'] else ij.TargetScope.CURRENT_PLANE)
    command = ij.resolve_rank_command(before, request, ij.RankFilterCommandOptions('variance', row['radius']))
    record = copy.deepcopy(command.record())
    assert ij.rank_command_from_record(before, record).record() == record
    assert reads.counts().plane_reads == 0
    if legacy:
        from imagejxx.ui.app import Editor
        editor = Editor(preferences=tmp_path / 'prefs.json')
        editor.add_native_document(owner)
        editor.apply_operation('rank_filter_imagej', command.parameters(), all_slices=bool(row['mode']), asynchronous=False)
    else:
        prepared = ij.prepare_operation('rank_filter_imagej', command.target, command.parameters())
        assert owner.snapshot().display.properties == before.display.properties
        assert prepared.preview.dataset.numpy('ZYX').tobytes() == pixels(row, 'output').tobytes()
        owner.commit(prepared.begin_edit())
    after = owner.snapshot()
    expected = dict(before.display.properties, display_range=row['final_range'])
    if row['final_threshold'] is None:
        expected.pop('manual_threshold')
    else:
        assert row['final_threshold'] == [4., 8.]
    assert after.display.properties == expected
    assert after.dataset.numpy('ZYX').tobytes() == pixels(row, 'output').tobytes()
    assert after.display.crosshair == before.display.crosshair
    assert owner.undo_steps == 1
    owner.undo()
    assert owner.snapshot().display.properties == before.display.properties
    assert owner.snapshot().dataset.numpy('ZYX').tobytes() == pixels(row, 'input').tobytes()
    owner.redo()
    assert owner.snapshot().display.properties == expected
    assert owner.snapshot().dataset.numpy('ZYX').tobytes() == pixels(row, 'output').tobytes()


@pytest.mark.parametrize('dtype', [np.uint16, np.float32])
@pytest.mark.parametrize('off_current_target', [False, True])
def test_other_plane_threshold_is_retained(dtype, off_current_target):
    data = np.arange(100, dtype=dtype).reshape(2, 5, 10)
    owner = ij.Document(ij.Dataset(data, axes='ZYX'))
    display = owner.snapshot().display
    display.properties = {'manual_threshold': threshold(), 'display_range': [1., 127.]}
    owner.set_display(display)
    request = ij.TargetRequest(ij.TargetScope.POSITIONS, positions=[ij.PlanePosition(0, 0, int(off_current_target))])
    prepared = ij.prepare_operation('rank_filter_imagej', ij.resolve_target(owner.snapshot(), request), {'method': 'variance', 'radius': 0})
    assert prepared.preview.display.properties['manual_threshold'] == threshold()
    owner.commit(prepared.begin_edit())
    owner.undo()
    assert owner.snapshot().display.properties == display.properties


@pytest.mark.parametrize('dtype', [np.uint16, np.float32])
def test_stretch_clears_threshold_even_when_bounds_equal_initial(dtype):
    row = next(r for r in CORPUS['cases'] if r['dtype'] == np.dtype(dtype).name and r['signal'] == 0 and r['selection'] == 0 and r['mode'] == 0)
    owner, _ = owner_for(row)
    display = owner.snapshot().display
    display.properties = dict(display.properties, display_range=row['final_range'])
    owner.set_display(display)
    prepared = ij.prepare_operation('rank_filter_imagej', ij.resolve_target(owner.snapshot()), {'method': 'variance', 'radius': row['radius']})
    assert prepared.preview.display.properties['display_range'] == row['final_range']
    assert 'manual_threshold' not in prepared.preview.display.properties


@pytest.mark.parametrize('dtype', [np.uint16, np.float32])
def test_empty_and_cancelled_variance_keep_threshold(dtype):
    reads = ij.DatasetReadObserver()
    owner = ij.Document(ij.Dataset(np.arange(25, dtype=dtype).reshape(5, 5), axes='YX').observe_reads(reads))
    edit = owner.begin_edit()
    edit.select(ij.Selection.from_mask(np.zeros((5, 5), np.uint8)))
    owner.commit(edit)
    display = owner.snapshot().display
    display.properties = {'manual_threshold': threshold(0)}
    owner.set_display(display)
    owner = ij.Document(owner.snapshot())
    prepared = ij.prepare_operation('rank_filter_imagej', ij.resolve_target(owner.snapshot()), {'method': 'variance', 'radius': 0})
    owner.commit(prepared.begin_edit())
    assert reads.counts().plane_reads == 0 and owner.undo_steps == 0
    assert owner.snapshot().display.properties == display.properties
    token = ij.CancellationToken()
    token.cancel()
    with pytest.raises(ij.OperationCancelled):
        ij.prepare_operation('rank_filter_imagej', ij.resolve_target(owner.snapshot()), {'method': 'variance', 'radius': 0}, cancellation=token)
    assert owner.snapshot().display.properties == display.properties


@pytest.mark.parametrize('dtype', [np.uint16, np.float32])
def test_threshold_workflow_new_document_session_and_conflict(tmp_path, dtype):
    row = next(r for r in CORPUS['cases'] if r['dtype'] == np.dtype(dtype).name and r['signal'] == 0 and r['selection'] == 0 and r['mode'] == 0)
    owner, _ = owner_for(row)
    before = owner.snapshot()
    program = ij.NativeWorkflowProgram([ij.NativeWorkflowOperation('rank_filter_imagej', {'method': 'variance', 'radius': row['radius']}, ij.TargetRequest())])
    prepared = ij.prepare_native_workflow(before, ij.NativeWorkflowProgram.from_json(program.json()).steps)
    owner.commit(prepared.begin_edit())
    assert 'manual_threshold' not in owner.snapshot().display.properties
    owner.undo()
    assert owner.snapshot().display.properties == before.display.properties
    app = ij.Application()
    app.adopt(owner)
    command = ij.resolve_rank_command(owner.snapshot(), ij.TargetRequest(destination=ij.OutputDestination.NEW_DOCUMENT), ij.RankFilterCommandOptions('variance', row['radius']))
    proposal = app.prepare('rank_filter_imagej', command.target, command.parameters())
    proposal.stage_new_document()
    result = app.publish(proposal)
    assert result.created and 'manual_threshold' not in result.document.snapshot().display.properties
    assert owner.snapshot().display.properties == before.display.properties
    path = tmp_path / 'threshold.ijx'
    ij.save_session(str(path), ij.SessionCapture([owner.snapshot(), result.document.snapshot()]))
    loaded = ij.load_session(str(path))
    assert loaded.documents[0].snapshot().display.properties == before.display.properties
    assert 'manual_threshold' not in loaded.documents[1].snapshot().display.properties
    pending = ij.prepare_operation('rank_filter_imagej', ij.resolve_target(owner.snapshot()), command.parameters())
    display = owner.snapshot().display
    display.properties = dict(display.properties, manual_threshold=dict(threshold(), upper=9.))
    owner.set_display(display)
    with pytest.raises(ij.RevisionConflict):
        owner.commit(pending.begin_edit())
    assert owner.snapshot().display.properties == display.properties


@pytest.mark.parametrize('case_id', ['R19', 'R20', 'R21', 'R22'])
def test_actual_thresholded_variance_both_hosts(tmp_path, case_id):
    if os.environ.get('IMAGEJXX_RENDER_TESTS') != '1':
        pytest.skip('render checks are opt in')
    from imagejxx import _desktop
    own = json.loads(_desktop.Editor(str(tmp_path / 'prefs.json'))._rank_ui_case(case_id, 'imgui_python', str(tmp_path / 'capture001')))
    cpp = json.loads((Path(os.environ['IMAGEJXX_THRESHOLD_CPP_DIR']) / case_id / 'case.json').read_bytes())
    for report in [own, cpp]:
        assert report['observation_state'] == 'observed'
        assert all(f['requested_events'] == f['rendered']['input_frame']['raw_inputs'] for f in report['frames'])
        before = report['fixture_sources']['source']
        states = report['states']
        after = states['variance.applied']['documents'][0]
        constant = case_id in ['R21', 'R22']
        props = after['display']['properties']
        assert ('manual_threshold' in props) == constant
        assert before['display']['properties']['manual_threshold'] == threshold()
        assert props['display_range'] == ([7., 7.] if constant else [0., 1024. if case_id == 'R19' else 1020.])
        dtype = np.dtype('uint16' if case_id in ['R19', 'R21'] else 'float32')
        expected = np.zeros((5, 5), dtype) if constant else np.array([[30,30,30,30,30],[30,0,1024,0,30],[30,1024,1024,1024,30],[30,0,1024,0,30],[30,30,30,30,30]], dtype)
        assert bytes.fromhex(after['planes'][1]['declared_hex']) == expected.tobytes()
        preview = next(f['rendered']['ordinary_pixels']['readback']['source'] for f in report['frames'] if f['label'] == 'variance.preview')
        assert np.asarray(preview['pixels'], dtype=dtype).tobytes() == expected.tobytes()
        undone = states['variance.undo']['documents'][0]
        redone = states['variance.redo']['documents'][0]
        assert undone['display']['properties'] == before['display']['properties']
        assert redone['display']['properties'] == props
        assert [p['declared_hex'] for p in undone['planes']] == [p['declared_hex'] for p in before['planes']]
        assert [p['declared_hex'] for p in redone['planes']] == [p['declared_hex'] for p in after['planes']]
        assert after['undo_steps'] == 1 and states['variance.applied']['workflow'][-1]['options']['method'] == 'variance'
