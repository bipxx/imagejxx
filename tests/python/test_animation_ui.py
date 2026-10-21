"""Independent positions, source bits and lifetime assertions for actual playback."""
import json
import os
from pathlib import Path
import struct

import pytest

CASES = ('z', 'c', 't', 'legacy', 'singleton', 'switch', 'edit', 'close',
         'stop', 'fps', 'ieee', 'rgb', 'busy')


def samples(case, shape):
    output = []
    special = (0x7F800001, 0xFFC01234, 0x80000000, 0x7F800000, 0xFF800000, 0x3F800000)
    nt, nc, nz, ny, nx, ns = shape
    for t in range(nt):
        for c in range(nc):
            for z in range(nz):
                for y in range(ny):
                    for x in range(nx):
                        for s in range(ns):
                            value = 1000*t + 100*c + 10*z + y + x
                            if case == 'ieee': value = special[(t+c+z+y+x) % 6]
                            elif case == 'rgb': value = (40*t+20*c+5*z+y+x+s) % 251
                            elif case not in {'legacy', 'busy'}:
                                value = struct.unpack('<I', struct.pack('<f', value))[0]
                            output.append(value)
    return output


@pytest.mark.parametrize('context', ['imgui_cpp', 'imgui_python'])
@pytest.mark.parametrize('case', CASES)
def test_actual_czt_playback(case, context, tmp_path):
    if context == 'imgui_cpp':
        configured = os.environ.get('IMAGEJXX_ANIMATION_CPP_ROOT')
        assert configured, 'Required installed C++ animation observations are missing'
        report = json.loads((Path(configured)/case/'case.json').read_bytes())
    else:
        from imagejxx import _desktop
        editor = _desktop.Editor(str(tmp_path/'preferences.json'))
        report = json.loads(editor._animation_ui_case(case, str(tmp_path/'capture')))
        assert report == json.loads((tmp_path/'capture/case.json').read_bytes())
    assert report['contract_id'] == 'p09s.animation-ui.001'
    assert (report['case_id'], report['context']) == (case, context)
    assert report['observation_state'] == 'observed' and report['acceptance_claim'] is False
    assert 0 < report['elapsed_seconds'] <= 15
    assert report['frame_count'] == len(report['frames']) <= 180
    assert report['lifecycle']['open'] is False
    legacy = case in {'legacy', 'busy'}
    shape = [1, 1, 4, 5, 7, 1] if legacy else [2, 3, 1 if case == 'singleton' else 4, 5, 7, 3 if case == 'rgb' else 1]
    assert report['shape'] == shape
    before, after = report['before'], report['after']
    expected = samples(case, shape)
    assert before['samples'] == expected
    edited = [x + int(1 <= i % 7 < 4 and 1 <= (i // 7) % 5 < 3)
              for i, x in enumerate(expected)] if case == 'busy' else expected
    assert after['samples'] == edited
    assert before['position'] == ([0, 0, 1] if legacy else [1, 1, 0 if case == 'singleton' else 1])
    assert before['crosshair'][:2] == [3, 2]
    assert before['shares_original'] and not before['dirty']
    assert before['data_identity'] == before['undo'] == before['redo'] == 0
    assert before['workflow'] == before['results'] == []
    axis = {'c': 1, 't': 0}.get(case, 2)
    previous = before
    ticks = 0
    last_tick_time = None
    for frame in report['frames']:
        rendered, source = frame['rendered'], frame['source']
        assert frame['events'] == rendered['input_frame']['raw_inputs']
        assert not rendered['window_error']
        animation = rendered['animation']
        chosen_axis = 'c' if case == 'c' else 't' if case == 't' else 'z'
        if animation['playing'] or animation['ticks']:
            assert animation['axis'] == chosen_axis
        else:
            assert animation['axis'] in {'z', chosen_axis}
        assert animation['clock'] == 'imgui_monotonic_seconds'
        assert animation['fps'] in ({15., 60.} if case == 'fps' else {60.})
        delta = animation['ticks'] - ticks
        assert delta in {0, 1}
        position = previous['position'].copy()
        if delta:
            position[axis] = (position[axis]+1) % shape[axis]
            if last_tick_time is not None:
                assert animation['last_tick'] - last_tick_time >= 1/animation['fps'] - 1e-9
            last_tick_time = animation['last_tick']
        assert source['position'] == position
        assert source['crosshair'][:2] == [3, 2]
        if source['document_ids'][source['active_index']] == source['source_id']:
            assert rendered['view']['crosshair'] == source['crosshair']
            assert rendered['view'].get('t', 0) == source['position'][0]
            assert rendered['view'].get('c', 0) == source['position'][1]
            if not legacy and animation['playing']:
                panes = rendered['orthogonal_planes']
                assert [p['plane'] for p in panes] == ['XY', 'XZ', 'YZ']
                for p, dimensions in zip(panes, [(7, 5), (7, shape[2]), (5, shape[2])]):
                    assert p['visible'] is True and p['pixels_source'] == 'live_document'
                    assert (p['width'], p['height']) == dimensions
                    assert p['current_tcz'] == source['position']
                    assert (p['t'], p['c']) == tuple(source['position'][:2])
        assert source['display_revision'] == previous['display_revision'] + delta
        assert source['results'] == []
        for field in ('selection', 'previous', 'roi', 'zoom', 'plane_zoom', 'linked_zoom',
                      'orthogonal', 'spacing', 'origin', 'title'):
            assert source[field] == before[field]
        if case not in {'edit', 'busy'}:
            assert source['shares_original'] and not source['dirty']
            assert source['data_identity'] == source['undo'] == source['redo'] == 0
            assert source['pixels_revision'] == source['metadata_revision'] == source['selection_revision'] == 0
            assert source['workflow'] == []
        if frame['label'] == 'after_stop':
            assert animation['playing'] is False and animation['source_id'] is None
        ticks, previous = animation['ticks'], source
    assert after['position'] == previous['position']
    assert report['animation']['playing'] is False
    controls = report['controls']
    assert all(c['control']['id'] > 0 and len(c['control']['rect']) == 4 for c in controls)
    for c in controls:
        assert c['control']['enabled'] is not c['disabled_attempt']
        point, rect = c['control']['point'], c['control']['rect']
        assert rect[0] <= point[0] < rect[2] and rect[1] <= point[1] < rect[3]
    assert any(c['name'] == 'menu.action.animation' for c in controls)
    if case in {'c', 't'}:
        assert any(c['name'] == 'menu.animation.axis.'+case for c in controls)
    apis = {x['label']: x for x in report['api']}
    if case == 'singleton':
        assert ticks == 0 and controls[-1]['disabled_attempt'] is True
        assert apis['singleton_refusal']['accepted'] is False
        assert apis['singleton_refusal']['before'] == apis['singleton_refusal']['after']
    else:
        assert ticks >= 3
        if case not in {'switch', 'edit', 'close', 'stop', 'busy'}:
            assert ticks >= shape[axis]+3
    if case == 'z':
        assert set(apis) == {'invalid_axis', 'invalid_playing', 'unknown_parameter'}
        for item in apis.values():
            assert item['accepted'] is False and item['error']
            assert item['before'] == item['after']
    if case == 'switch':
        assert after['active_index'] == 1 and after['document_ids'] == before['document_ids']
        assert report['animation']['reason'] == 'source_changed'
    if case == 'close':
        assert after['document_ids'] == before['document_ids'][1:]
    if case == 'edit':
        assert after['properties'] == {'changed': True}
        assert after['metadata_revision'] == 1 and after['pixels_revision'] == 0
        assert after['undo'] == 1 and after['redo'] == 0 and after['dirty']
        assert report['animation']['reason'] == 'source_edited'
    if case == 'busy':
        assert apis['submit']['accepted'] is True and apis['busy_refusal']['accepted'] is False
        assert after['pixels_revision'] == 1 and after['undo'] == 1
        assert len(after['workflow']) == 1 and after['workflow'][0]['operation'] == 'add'
    if case == 'fps':
        assert apis['fps']['accepted'] is True and report['animation']['fps'] == 15.
