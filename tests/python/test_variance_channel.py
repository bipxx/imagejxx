"""Channel variance: runner pixels/visible ranges, canonical persistent state."""
import copy
import json
import os
from pathlib import Path

import imagejxx as ij
import numpy as np
import pytest

CORPUS = json.loads((Path(__file__).parents[1] / 'fixtures/variance-channel-imagej154r.json').read_bytes())
MODES = {1: 'composite', 2: 'color', 3: 'grayscale'}


def decode(row, phase):
    # ImageJ stack order is Z,C; native storage is T,C,Z,Y,X.
    return np.stack([np.frombuffer(bytes.fromhex(v), dtype=row['dtype']).reshape(5, 5)
                     for v in row[phase + '_hex']]).reshape(2, 2, 5, 5).transpose(1, 0, 2, 3)[None].copy()


def state(mode='composite', ranges=((1.,127.), (2.,254.))):
    return {'schema': 'imagejxx.channel-display', 'version': 1, 'mode': mode,
            'channels': [{'visible': True, 'range': list(r), 'lut': {'name': name}}
                         for r, name in zip(ranges, ['Red', 'Green'])]}


def fixture(row):
    reads = ij.DatasetReadObserver()
    owner = ij.Document(ij.Dataset(decode(row, 'input'), axes='TCZYX').observe_reads(reads))
    display = owner.snapshot().display
    display.c = 1
    display.crosshair = [2, 2, 1]
    display.properties = {'display_range': [-3., 700.], 'lut': 'Inverted',
                          'manual_threshold': {'version': 1, 'lower': 4., 'upper': 8., 't': 0, 'c': 1, 'z': 1}}
    display = ij.with_channel_display(display, owner.snapshot().dataset, state(MODES[row['mode']], row['initial_lut_ranges']))
    owner.set_display(display)
    request = ij.TargetRequest(ij.TargetScope.RANGES, t=ij.AxisRange(), c=ij.AxisRange(), z=ij.AxisRange()) if row['all'] else ij.TargetRequest()
    return owner, reads, request


@pytest.mark.parametrize('row', CORPUS['cases'], ids=lambda r: f"{r['dtype']}-m{r['mode']}-s{r['signal']}-a{r['all']}-h{r['hosted']}")
def test_runner_channel_pixels_rgb_and_paired_history(row):
    owner, reads, request = fixture(row)
    before = owner.snapshot()
    command = ij.resolve_rank_command(before, request, ij.RankFilterCommandOptions('variance', 0))
    assert ij.rank_command_from_record(before, command.record()).record() == command.record()
    assert reads.counts().plane_reads == 0
    prepared = ij.prepare_operation('rank_filter_imagej', command.target, command.parameters())
    after = prepared.preview
    assert after.dataset.numpy('TCZYX').tobytes() == decode(row, 'output').tobytes()
    expected = copy.deepcopy(before.display.properties)
    expected['channel_display']['channels'][1]['range'] = row['final_processor_range']
    if row['final_threshold'] is None:
        expected.pop('manual_threshold')
    assert after.display.properties == expected
    rgb = ij.compose_channel_display(after.dataset, expected['channel_display'], 'xy', 1, 0, 1)
    assert rgb.tobytes() == bytes.fromhex(row['rgb_hex'])
    # ImageJ's stored LUT range can lag the visible processor. Our canonical
    # record deliberately retains the visible result across navigation/session.
    assert row['final_lut_ranges'] == row['initial_lut_ranges']
    assert owner.snapshot().display.properties == before.display.properties
    owner.commit(prepared.begin_edit())
    assert owner.undo_steps == 1
    owner.undo()
    assert owner.snapshot().display.properties == before.display.properties
    assert owner.snapshot().dataset.numpy('TCZYX').tobytes() == decode(row, 'input').tobytes()
    owner.redo()
    assert owner.snapshot().display.properties == expected
    assert owner.snapshot().dataset.numpy('TCZYX').tobytes() == decode(row, 'output').tobytes()


@pytest.mark.parametrize('dtype', [np.uint16, np.float32])
@pytest.mark.parametrize('mode', ['composite', 'color', 'grayscale'])
@pytest.mark.parametrize('projection', ['sum', 'max', 'min'])
def test_v2_custom_luts_visibility_and_other_ranges_are_exact(dtype, mode, projection):
    pixels = np.arange(200, dtype=dtype).reshape(2, 2, 5, 10)
    owner = ij.Document(ij.Dataset(pixels, axes='CZYX'))
    channel = state(mode)
    channel.update(version=2, projection=projection)
    channel['channels'][0]['visible'] = False
    channel['channels'][0]['range'] = [7.25, 7.25]
    channel['channels'][1]['lut'] = {'table': [[i, 255-i, i//2] for i in range(256)]}
    display = owner.snapshot().display
    display.c = 1
    display.properties = {'display_range': [100., 200.]}
    owner.set_display(ij.with_channel_display(display, owner.snapshot().dataset, channel))
    prepared = ij.prepare_operation('rank_filter_imagej', ij.resolve_target(owner.snapshot()), {'method': 'variance', 'radius': 0})
    actual = prepared.preview.display.properties['channel_display']
    assert actual['version'] == 2 and actual['projection'] == projection and actual['mode'] == mode
    assert actual['channels'][0] == channel['channels'][0]
    assert actual['channels'][1]['lut'] == channel['channels'][1]['lut']
    assert actual['channels'][1]['visible'] == channel['channels'][1]['visible']
    assert prepared.preview.display.properties['display_range'] == [100., 200.]
    assert prepared.preview.dataset.numpy('CZYX')[0].tobytes() == pixels[0].tobytes()


@pytest.mark.parametrize('dtype', [np.uint16, np.float32])
@pytest.mark.parametrize('bounds', [[0.,0.], [7.5,7.5], [.1,.3]])
def test_collapsed_histogram_preserves_requested_channel_range(dtype, bounds):
    reads = ij.DatasetReadObserver()
    owner = ij.Document(ij.Dataset(np.zeros((3,3), dtype), axes='YX').observe_reads(reads))
    channel = state('color', [bounds])
    display = ij.with_channel_display(owner.snapshot().display, owner.snapshot().dataset, channel)
    owner.set_display(display)
    prepared = ij.prepare_operation('rank_filter_imagej', ij.resolve_target(owner.snapshot()), {'method': 'variance', 'radius': 0})
    assert prepared.preview.display.properties == display.properties
    owner.commit(prepared.begin_edit())
    assert owner.undo_steps == 0
    # Literal channel [0,0] requires no implicit-source-range acquisition.
    assert reads.counts().plane_reads == 2


@pytest.mark.parametrize('dtype', [np.uint16, np.float32])
def test_empty_cancel_and_stale_channel_state_do_not_publish(dtype):
    reads = ij.DatasetReadObserver()
    owner = ij.Document(ij.Dataset(np.arange(25, dtype=dtype).reshape(5,5), axes='YX').observe_reads(reads))
    owner.set_display(ij.with_channel_display(owner.snapshot().display, owner.snapshot().dataset, state('grayscale', [[0.,255.]])))
    edit = owner.begin_edit()
    edit.select(ij.Selection.from_mask(np.zeros((5,5), np.uint8)))
    owner.commit(edit)
    owner = ij.Document(owner.snapshot())
    before = owner.snapshot()
    prepared = ij.prepare_operation('rank_filter_imagej', ij.resolve_target(before), {'method': 'variance', 'radius': 0})
    owner.commit(prepared.begin_edit())
    assert owner.undo_steps == 0 and reads.counts().plane_reads == 0
    assert owner.snapshot().display.properties == before.display.properties
    token = ij.CancellationToken()
    token.cancel()
    with pytest.raises(ij.OperationCancelled):
        ij.prepare_operation('rank_filter_imagej', ij.resolve_target(before), {'method': 'variance', 'radius': 0}, cancellation=token)
    display = owner.snapshot().display
    channel = copy.deepcopy(display.properties['channel_display'])
    channel['channels'][0]['range'] = [1.,254.]
    owner.set_display(ij.with_channel_display(display, owner.snapshot().dataset, channel))
    with pytest.raises(ij.RevisionConflict):
        owner.commit(prepared.begin_edit())


@pytest.mark.parametrize('dtype', [np.uint16, np.float32])
def test_workflow_new_document_session_and_channel_switch_keep_result(tmp_path, dtype):
    row = next(r for r in CORPUS['cases'] if r['dtype'] == np.dtype(dtype).name and r['mode'] == 2 and r['signal'] == 0 and not r['all'])
    owner, _, request = fixture(row)
    source = owner.snapshot()
    program = ij.NativeWorkflowProgram([ij.NativeWorkflowOperation('rank_filter_imagej', {'method': 'variance', 'radius': 0}, request)])
    prepared = ij.prepare_native_workflow(source, ij.NativeWorkflowProgram.from_json(program.json()).steps)
    owner.commit(prepared.begin_edit())
    expected = owner.snapshot().display.properties
    assert expected['channel_display']['channels'][1]['range'] == row['final_processor_range']
    assert row['switched_processor_range'] != row['final_processor_range']
    for c in [0,1]:
        display = owner.snapshot().display
        display.c = c
        owner.set_display(display)
    assert owner.snapshot().display.properties == expected
    path = tmp_path / 'channel-variance.ijx'
    ij.save_session(str(path), ij.SessionCapture([owner.snapshot()]))
    restored = ij.load_session(str(path)).documents[0]
    assert restored.snapshot().display.properties == expected
    assert ij.compose_channel_display(restored.snapshot().dataset, expected['channel_display'], 'xy', 1, 0, 1).tobytes() == bytes.fromhex(row['rgb_hex'])
    owner.undo()
    assert owner.snapshot().display.properties == source.display.properties
    app = ij.Application()
    app.adopt(owner)
    request = ij.TargetRequest(destination=ij.OutputDestination.NEW_DOCUMENT)
    command = ij.resolve_rank_command(owner.snapshot(), request, ij.RankFilterCommandOptions('variance', 0))
    proposal = app.prepare('rank_filter_imagej', command.target, command.parameters())
    proposal.stage_new_document()
    result = app.publish(proposal)
    assert result.created and result.document.snapshot().display.properties == expected
    assert owner.snapshot().display.properties == source.display.properties


@pytest.mark.parametrize('dtype,bounds', [(np.uint16,[1.e30,2.e30]), (np.float32,[1.e39,2.e39])])
def test_unqualified_active_channel_bounds_refuse_before_reads(dtype, bounds):
    reads = ij.DatasetReadObserver()
    owner = ij.Document(ij.Dataset(np.ones((3,3),dtype), axes='YX').observe_reads(reads))
    owner.set_display(ij.with_channel_display(owner.snapshot().display, owner.snapshot().dataset, state('color',[bounds])))
    with pytest.raises(ValueError):
        ij.resolve_rank_command(owner.snapshot(), options=ij.RankFilterCommandOptions('variance',0))
    with pytest.raises(ValueError):
        ij.prepare_operation('rank_filter_imagej',ij.resolve_target(owner.snapshot()),{'method':'variance','radius':0})
    assert reads.counts().plane_reads == 0


@pytest.mark.parametrize('case_id', ['R23','R24','R25','R26'])
def test_actual_channel_variance_both_imgui_hosts(tmp_path, case_id):
    if os.environ.get('IMAGEJXX_RENDER_TESTS') != '1':
        pytest.skip('render checks are opt in')
    from imagejxx import _desktop
    own = json.loads(_desktop.Editor(str(tmp_path/'prefs.json'))._rank_ui_case(case_id,'imgui_python',str(tmp_path/'capture001')))
    cpp = json.loads((Path(os.environ['IMAGEJXX_CHANNEL_VARIANCE_CPP_DIR'])/case_id/'case.json').read_bytes())
    for report in [own, cpp]:
        assert report['observation_state'] == 'observed'
        assert all(f['requested_events'] == f['rendered']['input_frame']['raw_inputs'] for f in report['frames'])
        before = report['fixture_sources']['source']
        after = report['states']['variance.applied']['documents'][0]
        expected = copy.deepcopy(before['display']['properties'])
        constant = case_id in ['R25','R26']
        if not constant:
            expected['channel_display']['channels'][1]['range'] = [0.,1024. if case_id == 'R23' else 1020.]
            expected.pop('manual_threshold')
        assert after['display']['properties'] == expected
        dtype = np.dtype('uint16' if case_id in ['R23','R25'] else 'float32')
        pixels = np.zeros((5,5),dtype) if constant else np.array([[60,60,60,60,60],[60,0,1024,0,60],[60,1024,1024,1024,60],[60,0,1024,0,60],[60,60,60,60,60]],dtype)
        assert bytes.fromhex(after['planes'][10]['declared_hex']) == pixels.tobytes()
        assert all(p['declared_hex'] == before['planes'][i]['declared_hex'] for i,p in enumerate(after['planes']) if i != 10)
        preview = next(f['rendered']['ordinary_pixels']['readback']['source'] for f in report['frames'] if f['label']=='variance.preview')
        assert np.asarray(preview['pixels'],dtype=dtype).tobytes() == pixels.tobytes()
        undone = report['states']['variance.undo']['documents'][0]
        redone = report['states']['variance.redo']['documents'][0]
        assert undone['display']['properties'] == before['display']['properties']
        assert [p['declared_hex'] for p in undone['planes']] == [p['declared_hex'] for p in before['planes']]
        assert redone['display']['properties'] == expected
        assert after['undo_steps'] == 1 and report['states']['variance.applied']['workflow'][-1]['options']['method'] == 'variance'
