"""Independent words/history/control checks for installed manual workflow UI."""
import gzip
import hashlib
import json
import os
from pathlib import Path
import struct

import pytest

from manual_threshold_workflow_ui_runner import CASES
from test_settings_wand_ui import decode

ROOT = Path(__file__).resolve().parents[2]
REFERENCE = json.loads(gzip.decompress(
    (ROOT / 'tests/fixtures/manual-workflow-triangles-imagej154r.json.gz').read_bytes()))
NORMAL_STATES = ('before', 'drawn', 'recorded', 'input_restored', 'destination_ready',
                 'strict_refused', 'reviewed', 'preview_ready', 'cancelled', 'edited',
                 'prepared', 'applied', 'undone', 'redone', 'reopened')
RECOVERY_STATES = ('before', 'input_restored', 'recovered_applied', 'undone', 'redone')


@pytest.fixture(scope='session')
def python_reports(tmp_path_factory):
    reports = None
    def produce():
        nonlocal reports
        if reports is None:
            from manual_threshold_workflow_ui_runner import produce_python_reports
            reports = produce_python_reports(tmp_path_factory.mktemp('manual-workflow-python'))
        return reports
    return produce


def load(case, context, python_reports):
    supplied = os.environ.get('IMAGEJXX_MANUAL_WORKFLOW_UI_REPORTS')
    cpp = os.environ.get('IMAGEJXX_MANUAL_WORKFLOW_CPP_REPORTS')
    if supplied:
        path = json.loads(supplied)[context][case]
    elif context == 'imgui_cpp' and cpp:
        path = json.loads(cpp)[case]
    elif context == 'imgui_python' and os.environ.get('IMAGEJXX_REQUIRE_RENDER_TESTS') == '1':
        path = python_reports()[case]
    elif os.environ.get('IMAGEJXX_REQUIRE_RENDER_TESTS') == '1':
        pytest.fail('Required manual workflow producer missing: '+context+'/'+case)
    else:
        pytest.skip('Installed manual workflow reports were not supplied')
    row = decode(Path(path).read_bytes())
    assert row['schema'] == 'manual-threshold.workflow.ui.001'
    assert row['case_id'] == case and row['context'] == context
    assert row['status'] == 'observed' and row['acceptance_claim'] is False and row['error'] == ''
    assert row['bounds'] == {'frames': 750, 'seconds': 60, 'report_bytes': 16*1024**2}
    assert 0 < len(row['frames']) <= 750 and row['events'] and row['setup_events']
    for i, frame in enumerate(row['frames']):
        assert frame['ordinal'] == i
        assert frame['events'] == frame['rendered']['input_frame']['raw_inputs']
    for event in row['events']:
        assert 0 <= event['frame_before'] < event['frame_after'] < len(row['frames'])
        if event['kind'] == 'control_click':
            item = event['actual_control']
            x, y = event['actual_mouse']
            assert item['id'] == event['hovered_id'] != 0
            assert item.get('enabled', True) is True
            assert item['rect'][0] <= x < item['rect'][2] and item['rect'][1] <= y < item['rect'][3]
            clip = event['actual_clip']
            assert clip[0] <= x < clip[2] and clip[1] <= y < clip[3]
        elif event['kind'] == 'canvas_click':
            assert event['actual_mouse'] == event['requested_screen'] and event['canvas_id'] != 0
    return row, Path(path).parent


def members(name):
    observation = next(r['observation'] for r in REFERENCE['cases'] if r['id'] == name)
    mask = observation['processor_mask']
    width, height = mask['dimensions']
    x0, y0 = mask['image_origin']
    data = bytes.fromhex(mask['u8_hex'])
    cells = {(x0+x, y0+y) for y in range(height) for x in range(width) if data[y*width+x]}
    offset, end = (2, 10) if name == 'captured' else (1, 8)
    assert cells == {(x, y) for y in range(offset, offset+6)
                     for x in range(offset, offset+6) if x+y < end}
    assert len(cells) == 21
    return cells


def original(shape):
    _, _, depth, height, width, _ = shape
    return [bytes((31*z+7*y+3*x)%256 for y in range(height) for x in range(width))
            for z in range(depth)]


def thresholded(planes, shape, captured, all_z, lower, upper):
    result = [bytearray(p) for p in planes]
    width = shape[4]
    for z in range(len(planes)) if all_z else (1,):
        for x, y in members('captured' if captured else 'destination'):
            p = y*width+x
            result[z][p] = 255 if lower <= planes[z][p] <= upper else 0
    return list(map(bytes, result))


def pixels(owner):
    depth, height, width = owner['shape'][2:5]
    assert owner['type'] == 'uint8' and owner['sample_policy'] == 'finite_only'
    assert len(owner['planes']) == depth
    result = []
    for z, plane in enumerate(owner['planes']):
        assert plane['position'] == [0, 0, z] and plane['shape'] == [height, width, 1]
        assert plane['dtype'] == 'uint8'
        declared = bytes.fromhex(plane['declared_u8_hex'])
        assert len(declared) == width*height
        storage = bytes.fromhex(plane['storage_f64le_hex'])
        assert storage == b''.join(struct.pack('<d', float(v)) for v in declared)
        result.append(declared)
    return result


def active(state):
    return next(o for o in state['all_owners'] if o['document_id'] == state['active_id'])


@pytest.mark.parametrize('case', CASES)
@pytest.mark.parametrize('context', ['imgui_cpp', 'imgui_python'])
def test_actual_manual_workflow_pixels_original_history_and_recovery(case, context, python_reports):
    row, directory = load(case, context, python_reports)
    recovery = case == 'manual-workflow-refusal-recovery'
    required = RECOVERY_STATES if recovery else NORMAL_STATES
    assert set(row['states']) == set(required)
    states = row['states']
    assert [states[n]['frame'] for n in required] == sorted(states[n]['frame'] for n in required)
    shape = [1, 1, 2, 128, 256, 1] if recovery else [1, 1, 3, 11, 11, 1]
    before = active(states['before'])
    assert before['shape'] == shape and before['selection'] is None
    base = original(shape)
    all_z = case != 'manual-workflow-current-path'
    final = thresholded(base, shape, False, all_z, 45 if recovery else 50, 95 if recovery else 90)
    other_before = [o for o in states['before']['all_owners'] if o['document_id'] != before['document_id']]
    originals = []
    for name in required:
        state = states[name]
        owner = active(state)
        assert state['recording'] is (name == 'recorded')
        assert all(state['usage'][k] == 0 for k in ('running', 'queued', 'running_bytes'))
        retained = 64*1024**2 if name in ('preview_ready', 'prepared', 'applied', 'recovered_applied') else 0
        assert state['usage']['retained_bytes'] == state['usage']['reserved_bytes'] == retained
        expected = final if name in ('applied', 'recovered_applied', 'redone', 'reopened') else (
            thresholded(base, shape, True, False, 45, 95) if name == 'recorded' else base)
        assert pixels(owner) == expected
        assert owner['metadata'] == before['metadata'] and owner['calibration'] == before['calibration']
        assert owner['rois'] == before['rois'] and owner['overlays'] == before['overlays']
        for other in other_before:
            found = next(o for o in state['all_owners'] if o['document_id'] == other['document_id'])
            assert pixels(found) == pixels(other)
            assert found['metadata'] == other['metadata'] and found['undo'] == other['undo']
        if name not in ('before', 'drawn'):
            assert len(state['workflow']) == 1
            originals.append(state['workflow'][0])
            assert originals[-1]['schema_version'] == 3
            assert originals[-1]['lower'] == 45 and originals[-1]['upper'] == 95
        else:
            assert state['workflow'] == []
        if name in ('drawn', 'recorded', 'input_restored'):
            assert owner['selection']['path'] == [0,2,2,1,8,2,1,2,8,4]
        elif name != 'before':
            assert owner['selection']['path'] == [0,1,1,1,7,1,1,1,7,4]
    assert all(r == originals[0] for r in originals)
    applied = active(states['recovered_applied' if recovery else 'applied'])
    undone, redone = active(states['undone']), active(states['redone'])
    assert undone['undo']+1 == applied['undo'] == redone['undo']
    assert undone['redo'] == 1 and redone['redo'] == 0
    if recovery:
        milestones = row['milestones']
        assert {'low_configured','low_refused','restored','stale_prepared',
                'stale_invalidated','recovered_prepared'} <= set(milestones)
        low = milestones['low_refused']
        assert low['settings']['processing_memory_mb'] == 1
        assert low['observations']['native_workflow']['error']
        assert low['usage']['reserved_bytes'] == 0
        assert milestones['stale_invalidated']['active_id'] != states['before']['active_id']
        assert not milestones['stale_invalidated']['observations']['native_workflow']['visible']
        assert milestones['restored']['settings']['processing_memory_mb'] == 64
        destination = active(milestones['destination_ready'])
        for name in ('low_configured', 'low_refused', 'restored', 'stale_prepared', 'recovered_prepared'):
            owner = next(o for o in milestones[name]['all_owners'] if o['document_id'] == destination['document_id'])
            for key in ('data_identity', 'revisions', 'undo', 'redo', 'selection', 'previous', 'metadata', 'rois', 'overlays', 'display'):
                assert owner[key] == destination[key], (name, key)
        for name in ('low_refused', 'stale_invalidated'):
            assert all(milestones[name]['usage'][k] == 0 for k in
                       ('running', 'queued', 'running_bytes', 'retained_bytes', 'reserved_bytes'))
        tabs = [e for e in row['events'] if e['kind'] == 'control_click' and e['control'].startswith('document.')]
        assert len(tabs) == 2
        assert tabs[0]['actual_control']['editing_blocked'] is True
        assert tabs[0]['actual_control']['close_enabled'] is False
        assert tabs[1]['actual_control']['editing_blocked'] is False
        assert tabs[1]['actual_control']['close_enabled'] is True
    else:
        assert row['milestones']['invalid_bounds_refused']['observations']['native_workflow']['editor']['error']
        assert states['strict_refused']['error']
        assert not states['cancelled']['observations']['native_workflow']['visible']
        assert states['preview_ready']['observations']['native_workflow']['preview_only']
        assert applied['undo'] == active(states['prepared'])['undo']+1
        file = row['files']['session']
        assert file['member'] == 'threshold-workflow.ijx'
        raw = (directory/file['member']).read_bytes()
        assert len(raw) == file['bytes'] and hashlib.sha256(raw).hexdigest() == file['sha256']
        assert active(states['reopened'])['document_id'] != applied['document_id']
        destination = active(states['destination_ready'])
        for name in ('strict_refused', 'reviewed', 'preview_ready', 'cancelled', 'edited', 'prepared'):
            owner = active(states[name])
            for key in ('data_identity', 'revisions', 'undo', 'redo', 'selection', 'previous', 'metadata', 'rois', 'overlays', 'display'):
                assert owner[key] == destination[key], (name, key)
        invalid = active(row['milestones']['invalid_bounds_refused'])
        for key in ('data_identity', 'revisions', 'undo', 'redo', 'selection', 'previous', 'metadata', 'rois', 'overlays', 'display'):
            assert invalid[key] == destination[key], ('invalid_bounds_refused', key)
        for name in ('reviewed', 'preview_ready', 'edited', 'prepared', 'applied'):
            panel = states[name]['observations']['native_workflow']
            entry = panel['entries'][0]
            chosen = entry['chosen_target']
            assert chosen == {'scope': int(all_z), 'roi': 1, 'mode': 0,
                              't': None, 'c': None, 'z': None, 'positions_count': 0}
            authored = name in ('edited', 'prepared', 'applied')
            assert entry['manual_threshold'] == {'lower': 50 if authored else 45,
                'upper': 90 if authored else 95, 'authored': authored,
                'captured_lower': 45, 'captured_upper': 95, 'original_version': 3}
        viewport = states['preview_ready']['observations']['native_workflow']['viewport']
        assert viewport['document_id'] != destination['document_id']
        preview = thresholded(base, shape, False, all_z, 45, 95)
        cx, cy, cz = viewport['display']['crosshair']
        assert [cx, cy, cz] == destination['display']['crosshair'] == [3, 3, 1]
        width, height, depth = shape[4], shape[3], shape[2]
        expected_planes = [list(preview[cz]),
            [preview[z][cy*width+x] for z in range(depth) for x in range(width)],
            [preview[z][y*width+cx] for z in range(depth) for y in range(height)]]
        for plane, expected, label, coordinate, dimensions in zip(
                viewport['planes'], expected_planes, ('XY', 'XZ', 'YZ'), (cz, cy, cx),
                ([height, width, 1], [depth, width, 1], [depth, height, 1]), strict=True):
            assert plane['plane'] == label and plane['coordinate'] == coordinate
            assert plane['current_tcz'] == [0, 0, cz]
            readback = plane['readback']
            assert readback['available'] is True
            assert readback['source_tcz'] == [0, 0, cz]
            assert readback['source']['shape'] == dimensions
            assert readback['source']['dtype'] == 'uint8'
            assert readback['source']['pixels'] == expected
        controls = [e['control'] for e in row['events'] if e['kind'] == 'control_click']
        required_controls = ['host.workflow.record', 'host.menu.manual_threshold', 'threshold.apply',
            'host.workflow.record', 'host.workflow.manual_threshold.0.replay', 'error.ok',
            'host.workflow.run', 'program.confirm', 'program.prepare_preview', 'program.view_preview',
            'viewport.close', 'program.cancel', 'host.workflow.run', 'program.edit_parameters',
            'parameters.keep_parameters', 'parameters.keep_parameters', 'program.confirm',
            'program.prepare', 'program.apply', 'program.cancel', 'host.menu.Edit.Undo',
            'host.menu.Edit.Redo', 'host.menu.path.save_document', 'host.menu.file.open']
        cursor = iter(controls)
        for required_control in required_controls:
            assert any(control == required_control for control in cursor), required_control
