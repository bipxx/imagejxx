"""Independent words/history/target checks for shared path admission consumers.

Mean expectations are the native RAW sum/count contract, not Java Mean parity.
Renderer/state reads do not establish the native quiet-admission boundary.
"""
import json
import os
import struct
from pathlib import Path

import pytest

from test_path_filter_ui import PATH, budget_file, reference, retained


U8_CASES = (
    'admission-legacy-invert-path',
    'admission-pooled-mask-mean-path',
    'admission-pooled-display-mean-path',
)
GENERIC_STATES = {
    'before', 'source_saved', 'drawn', 'configured', 'preview_ready',
    'cancelled', 'panel_reopened', 'applied', 'undone', 'redone',
    'filtered_saved', 'filtered_reopened',
}
MANUAL_STATES = GENERIC_STATES - {'preview_ready'} | {'automatic', 'automatic_reopened'}


def load_case(case_id, context, tmp_path):
    if context == 'imgui_cpp':
        path = Path(json.loads(os.environ['IMAGEJXX_PATH_FILTER_CPP_REPORTS'])[case_id])
        assert path.stat().st_size < 16 * 1024 * 1024
        row = json.loads(path.read_bytes())
    else:
        from imagejxx import _desktop
        editor = _desktop.Editor(preferences_path=str(tmp_path / 'preferences.json'))
        row = editor._path_filter_ui_case(case_id, str(tmp_path / 'capture'))
        if isinstance(row, str):
            row = json.loads(row)
        assert (tmp_path / 'capture/case.json').stat().st_size < 16 * 1024 * 1024
    assert row['contract'] == 'p13i.path-filter.ui.001'
    assert row['state'] == 'observed' and row['error'] == ''
    assert row['case_id'] == case_id and row['context'] == context
    assert row['acceptance_claim'] is False
    return row


def input_protocol(row):
    assert 0 < len(row['frames']) <= 750
    for index, frame in enumerate(row['frames'], 1):
        assert frame['ordinal'] == index
        assert frame['events'] == frame['rendered']['input_frame']['raw_inputs']
    assert row['controls']
    for control in row['controls']:
        assert control['hovered_id'] == control['item']['id'] != 0
        assert control['item']['enabled'] is True
    assert row['final_usage']['reserved_bytes'] == 0


def pinned_members():
    # Original Java processor-mask observation, independent of the native planner.
    mask = reference(1)['processor_mask']
    x0, y0 = mask['image_origin']
    width, height = mask['dimensions']
    cells = bytes.fromhex(mask['u8_hex'])
    assert len(cells) == width * height
    selected = {(x0 + x, y0 + y) for y in range(height) for x in range(width)
                if cells[y * width + x]}
    assert len(selected) == 28
    return selected


def originals(pooled):
    return {
        (0, 0, z): [100 + z if x < 11 and y < 11 else 240
                    for y in range(17) for x in range(23)]
        if pooled else [(37 * x + 53 * y + (x * y) % 17) % 256
                        for y in range(17) for x in range(23)]
        for z in range(2 if pooled else 1)
    }


def expected_pixels(pooled):
    # Keep the mask branch outside the value branch, so pooled outside words
    # cannot accidentally become binary in the independent expectation.
    selected = pinned_members()
    result = {}
    for position, values in originals(pooled).items():
        result[position] = [((255 if value > 100.5 else 0) if pooled else 255 - value)
                            if (index % 23, index // 23) in selected else value
                            for index, value in enumerate(values)]
    return result


def u8_words(state, expected):
    assert state['sample_schema'] == 'p13i.scalar-u8.001'
    assert state['pixel_type'] == 0 and state['sample_policy'] == 0
    assert state['shape'] == [1, 1, len(expected), 17, 23, 1]
    assert len(state['planes']) == len(expected)
    assert {tuple(plane['position']) for plane in state['planes']} == set(expected)
    for plane in state['planes']:
        values = expected[tuple(plane['position'])]
        assert set(plane) == {'position', 'declared_u8_hex', 'storage_f64_hex'}
        assert plane['declared_u8_hex'] == bytes(values).hex()
        assert plane['storage_f64_hex'] == ''.join(struct.pack('<d', value).hex() for value in values)


@pytest.mark.parametrize('case_id', U8_CASES)
@pytest.mark.parametrize('context', ['imgui_cpp', 'imgui_python'])
def test_actual_generic_and_pooled_path_consumers(case_id, context, tmp_path):
    row = load_case(case_id, context, tmp_path)
    pooled = case_id != 'admission-legacy-invert-path'
    manual = case_id == 'admission-pooled-display-mean-path'
    operation = 'threshold' if manual else 'auto_mask' if pooled else 'invert'
    assert row['recipe'] == {
        'operation': operation, 'method': 'mean' if pooled else None,
        'destination': 'in_place', 'roi_policy': 'active_selection',
        'initial_position': [0, 0, 1 if pooled else 0], 'all_slices': pooled,
        'bright': True if pooled else None, 'ignore_black': False if pooled else None,
        'ignore_white': False if pooled else None, 'stack_histogram': pooled,
        'sample_schema': 'p13i.scalar-u8.001',
    }
    required = MANUAL_STATES if manual else GENERIC_STATES
    states, obs = row['states'], row['observations']
    assert set(states) == set(obs) == set(row['document_states']) == required
    drawn = states['drawn']
    assert drawn['selection']['path'] == PATH
    changed = {'applied', 'redone', 'filtered_saved', 'filtered_reopened'}
    recorded = changed | {'undone'}
    for name, state in states.items():
        values = expected_pixels(pooled) if name in changed else originals(pooled)
        u8_words(state, values)
        assert retained(state) == retained(states['before']), name
        after_draw = obs[name]['frame'] >= obs['drawn']['frame']
        if after_draw:
            assert state['selection'] == drawn['selection']
            assert state['previous'] == drawn['previous']
        if name not in {'before', 'source_saved', 'filtered_reopened'}:
            assert state['undo'] == drawn['undo'] + int(name in changed), name
            assert state['redo'] == int(name == 'undone'), name
        if name in recorded:
            assert len(state['workflow']) == len(drawn['workflow']) + 1
            record = state['workflow'][-1]
            assert record['operation'] == operation
            if manual:
                target = record['target']
                assert target['scope'] == 'ranges' and target['ranges'] == [[0, 1, 1], [0, 1, 1], [0, 2, 1]]
                assert target['roi_policy'] == 'active_selection'
                assert record['schema_version'] == 3 and record['profile'] == 'manual_raw_inclusive_v3'
                assert record['lower'] == 101.0 and record['upper'] == 255.0
                assert record['selection'] == drawn['selection']
            else:
                assert record['parameters'] == obs['configured']['operation_dialog']['parameters']
                assert record['all_slices'] is pooled and record['slice_index'] == (1 if pooled else 0)
                assert record['selection'] == drawn['selection']
        else:
            assert state['workflow'] == (drawn['workflow'] if after_draw else states['before']['workflow'])
        owners = row['document_states'][name]
        ids = owners['document_ids']
        assert len(ids) == len(set(ids)) == len(owners['documents']) == (2 if name == 'filtered_reopened' else 1)
        assert set(ids) == {owner['document_id'] for owner in owners['documents']}
        assert row['source_document_id'] in ids and owners['active_document_id'] == state['document_id']
        for owner in owners['documents']:
            u8_words(owner, values)
            assert retained(owner) == retained(states['before'])
            for field in ('selection', 'previous', 'workflow'):
                assert owner[field] == state[field]
            original = owner['document_id'] == row['source_document_id']
            history = states['filtered_saved'] if name == 'filtered_reopened' and original else state
            assert owner['undo'] == (history['undo'] if original else 0)
            assert owner['redo'] == (history['redo'] if original else 0)
    if manual:
        for name in ('automatic', 'automatic_reopened'):
            panel = obs[name]['threshold_panel']
            assert panel['scientific_target'] and panel['histogram_roi'] and panel['target_valid']
            assert panel['automatic'] == {'method': 'mean', 'bright': True, 'ignore_black': False,
                                          'ignore_white': False, 'stack_histogram': True,
                                          'level': 100.5, 'selected_samples': 56, 'matched_count': 28}
            assert panel['lower'] == 101 and panel['upper'] == 255
            target = panel['target_panel']['target']
            assert target['scope'] == 'ranges' and target['ranges'] == [[0, 1, 1], [0, 1, 1], [0, 2, 1]]
            assert target['roi_policy'] == 'active_selection'
            assert 'manual_threshold' in states[name]['display']['properties']
        assert 'manual_threshold' not in states['cancelled']['display']['properties']
        assert 'manual_threshold' not in states['applied']['display']['properties']
    else:
        for name in ('configured', 'panel_reopened', 'preview_ready'):
            panel = obs[name]['operation_dialog']
            assert panel['id'] == operation and panel['open']
            assert panel['controls']['all_slices']['value'] is pooled
            if pooled:
                assert panel['controls']['all_slices']['enabled'] is False
                assert panel['parameters']['method'] == 'mean'
                assert all(panel['parameters'][key] is value for key, value in
                           {'bright': True, 'ignore_black': False, 'ignore_white': False, 'stack_histogram': True}.items())
            else:
                assert panel['parameters'] == {}
        preview = obs['preview_ready']['operation_dialog']
        assert preview['has_preview'] is True
        assert preview['preview_image'] == {'width': 23, 'height': 17, 'channels': 1,
                                           'pixels': expected_pixels(pooled)[(0, 0, 1 if pooled else 0)]}
    assert [hit['image'] for hit in row['canvas_hits']] == [[2.2, 1.2], [9.2, 1.2], [2.2, 8.2]]
    assert all(hit['hovered_id'] == hit['canvas_id'] != 0 for hit in row['canvas_hits'])
    input_protocol(row)


def constant_f32(state, value):
    assert state['pixel_type'] == 2 and state['sample_policy'] == 0
    assert state['shape'] == [1, 1, 1, 207, 209, 1]
    assert len(state['planes']) == 1
    plane = state['planes'][0]
    assert plane['position'] == [0, 0, 0]
    assert plane['declared_f32_hex'] == struct.pack('<f', value).hex() * (207 * 209)
    assert plane['storage_f64_hex'] == struct.pack('<d', value).hex() * (207 * 209)


@pytest.mark.parametrize('context', ['imgui_cpp', 'imgui_python'])
def test_actual_later_workflow_mask_refusal_and_budget_recovery(context, tmp_path):
    row = load_case('admission-workflow-later-path-refusal', context, tmp_path)
    assert row['recipe'] == {'operation': 'native_workflow', 'step_operations': ['add', 'gaussian_blur_imagej'],
                             'destination': 'in_place', 'roi_policies': ['whole_plane', 'active_selection'],
                             'initial_memory_mib': 64, 'requested_memory_mib': [1, 64],
                             'rendered_rgb_encoding': 'uint8_hex', 'fixture_geometry_authoring_credit': False}
    states, obs = row['states'], row['observations']
    required = {'before', 'low_refused', 'recovered_applied', 'undone', 'redone'}
    assert set(states) == set(obs) == set(row['document_states']) == required
    before = states['before']
    assert before['selection']['path'] == PATH
    assert before['previous'] is None and before['rois'] == before['rows'] == before['overlays'] == []
    assert before['metadata_all'] == {'title': 'Untitled', 'properties': {}, 'extensions': {}, 'record_layout': {}}
    assert before['display']['properties'] == {'display_range': [0.0, 20.0]}
    assert len(before['workflow']) == 1
    program = before['workflow'][0]
    assert program['schema'] == 'imagejxx.native-workflow-program' and program['version'] == 2
    assert len(program['steps']) == 2
    for index, step in enumerate(program['steps']):
        assert step['kind'] == 'operation'
        assert step['operation'] == ('add' if index == 0 else 'gaussian_blur_imagej')
        assert step['original'] is None and step['use_destination_calibration'] is False
        assert step['target'] == {'scope': 0, 'roi': index, 'mode': 0, 't': None, 'c': None,
                                  'z': None, 'positions': [], 'destination': 'in_place'}
        assert step['parameters'] == ({'value': {'kind': 'real', 'value': 1.0}} if index == 0 else {})
        assert step['filter_options'] == (None if index == 0 else {'kind': 'gaussian', 'scaled_units': False, 'sigma': 0.0})
    for name, state in states.items():
        constant_f32(state, 11 if name in {'recovered_applied', 'redone'} else 10)
        assert state['workflow'] == before['workflow']
        for field in ('selection', 'previous', 'rois', 'rows', 'overlays', 'calibration', 'metadata_all'):
            assert state[field] == before[field], (name, field)
        assert state['undo'] == int(name in {'recovered_applied', 'redone'})
        assert state['redo'] == int(name == 'undone')
        assert len(row['document_states'][name]['documents']) == 1
        owner = row['document_states'][name]['documents'][0]
        constant_f32(owner, 11 if name in {'recovered_applied', 'redone'} else 10)
        for field in ('selection', 'previous', 'rois', 'rows', 'overlays', 'calibration', 'metadata_all', 'workflow', 'undo', 'redo'):
            assert owner[field] == state[field]
        budget_file(obs[name])
        rgb = obs[name]['ordinary_pixels']['readback']['rendered_rgb']
        assert rgb['dtype'] == 'uint8' and rgb['shape'] == [207, 209, 3]
        assert rgb['encoding'] == 'uint8_hex' and 'pixels' not in rgb
        gray = 140 if name in {'recovered_applied', 'redone'} else 128
        assert bytes.fromhex(rgb['hex']) == bytes([gray]) * (207 * 209 * 3)
    denied = obs['low_refused']['native_workflow']
    assert denied['working'] is False and denied['ready'] is False
    assert denied['completed_steps'] == 0 and denied['prefix_ready'] is False
    assert denied['error'] in {'ImageJ shape exceeds working-byte limit',
                               'Native workflow retained plus target-mask working-byte limit'}
    assert states['low_refused']['data_identity'] == before['data_identity']
    assert states['low_refused']['revisions'] == before['revisions']
    assert states['low_refused']['history'] == before['history']
    assert set(row['milestones']) == {'low_configured', 'restored', 'review_reopened', 'recovered_prepared'}
    for name, milestone in row['milestones'].items():
        assert 0 < milestone['frame'] <= len(row['frames'])
        assert milestone['settings']['processing_memory_mb'] == (1 if name == 'low_configured' else 64)
        assert milestone['limits']['memory_budget'] == (1 if name == 'low_configured' else 64) * 1024 * 1024
        budget_file(milestone)
        # Scalar milestones omit full carrier/calibration snapshots; the five
        # full checkpoints above validate them, and revisions stay unchanged here.
        for field in ('selection', 'previous', 'overlays', 'metadata_all', 'workflow', 'undo', 'redo'):
            assert milestone['state'][field] == before[field], (name, field)
        assert milestone['state']['data_identity'] == before['data_identity']
        assert milestone['state']['revisions'] == before['revisions']
        if name in {'low_configured', 'review_reopened', 'recovered_prepared'}:
            entries = milestone['native_workflow']['entries']
            assert len(entries) == 2
            for index, entry in enumerate(entries):
                assert entry['enabled'] is True and entry['confirmed'] is True
                assert entry['chosen_target'] == {'scope': 0, 'roi': index, 'mode': 0,
                                                   't': None, 'c': None, 'z': None, 'positions_count': 0}
    ready = row['milestones']['recovered_prepared']['native_workflow']
    assert ready['ready'] is True and ready['working'] is False and ready['completed_steps'] == 2
    assert row['files']['workflow']['hex']
    input_protocol(row)
