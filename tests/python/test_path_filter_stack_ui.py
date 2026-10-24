"""Pinned path pixels and captured-Z stack choices in both shared UI launches."""
import struct

import pytest

from test_path_filter_admission_ui import input_protocol, load_case
from test_path_filter_ui import PATH, expected_words, little_words, rank_expected, reference, retained


CASES = [f'stack-{operation}-{choice}' for operation in ('gaussian', 'rank-mean')
         for choice in ('yes', 'no', 'cancel')]
STATES = {'before', 'drawn', 'configured', 'prompt', 'display_moved', 'chosen', 'reopened', 'final_cancel'}


def plane_words(state, expected):
    assert state['shape'] == [1, 1, 3, 17, 23, 1]
    assert state['pixel_type'] == 2 and state['sample_policy'] == 0
    assert len(state['planes']) == 3
    assert [plane['position'] for plane in state['planes']] == [[0, 0, z] for z in range(3)]
    for plane in state['planes']:
        words = expected[plane['position'][2]]
        assert plane['declared_f32_hex'] == words
        assert plane['storage_f64_hex'] == ''.join(
            struct.pack('<d', value[0]).hex() for value in struct.iter_unpack('<f', bytes.fromhex(words)))


def captured_positions(target):
    if target['scope'] == 'positions':
        return target['positions']
    assert target['scope'] == 'ranges'
    axes = target['ranges']
    assert len(axes) == 3 and all(len(axis) == 3 and axis[2] > 0 for axis in axes)
    assert target['positions'] == []
    return [[t, c, z] for t in range(*axes[0]) for c in range(*axes[1]) for z in range(*axes[2])]


@pytest.mark.parametrize('case_id', CASES)
@pytest.mark.parametrize('context', ['imgui_cpp', 'imgui_python'])
def test_actual_path_stack_prompt_keeps_captured_plane_and_first_accepted_options(case_id, context, tmp_path):
    row = load_case(case_id, context, tmp_path)
    rank = case_id.startswith('stack-rank-mean-')
    choice = case_id.rsplit('-', 1)[1]
    assert row['recipe'] == {
        'operation': 'rank_filter_imagej' if rank else 'gaussian_blur_imagej',
        'method': 'mean' if rank else None, 'sigma': None if rank else 1,
        'radius': 2.5 if rank else None, 'destination': 'in_place',
        'roi_policy': 'active_selection', 'stack_choice': choice,
        'initial_position': [0, 0, 1], 'moved_position': [0, 0, 2],
        'input_pattern': 'identical-dyadic-grid-v1-each-z',
    }
    states, obs = row['states'], row['observations']
    assert set(states) == set(obs) == set(row['document_states']) == STATES
    original = little_words(reference(1)['input']['f32_words'])
    filtered = rank_expected('mean', 2.5) if rank else expected_words(1)
    assert original != filtered
    drawn = states['drawn']
    assert drawn['selection']['path'] == PATH
    panel_name = 'rank_panel' if rank else 'gaussian_panel'
    initial = row['initial_panel'][panel_name]
    assert initial['open'] and initial['draft_valid'] and not initial['scientific_targets']
    assert not initial['stack_prompt'] and not initial['preview']
    assert states['drawn']['display']['crosshair'][2] == 1
    if rank:
        assert initial['method'] == 'mean' and initial['remembered_radii']['mean'] == 2
    else:
        assert initial['sigma'] == 2
    after_choice = {'chosen', 'reopened', 'final_cancel'}
    for name, state in states.items():
        selected = (0, 1, 2) if choice == 'yes' else (1,) if choice == 'no' else ()
        expected = [filtered if name in after_choice and z in selected else original for z in range(3)]
        plane_words(state, expected)
        assert retained(state) == retained(states['before'])
        if name != 'before':
            assert state['selection'] == drawn['selection'] and state['previous'] == drawn['previous']
            published = name in after_choice and choice != 'cancel'
            assert state['undo'] == drawn['undo'] + int(published)
            assert state['redo'] == 0
            assert len(state['workflow']) == len(drawn['workflow']) + int(published)
            if not published:
                assert state['workflow'] == drawn['workflow']
                assert state['data_identity'] == drawn['data_identity']
        assert state['display']['crosshair'][2] == (2 if name in after_choice | {'display_moved'} else 1)
        assert state['display']['t'] == state['display']['c'] == 0
        assert obs[name]['preview'] is None
        for field in ('metadata', 'selection'):
            if name != 'before':
                assert state['revisions'][field] == drawn['revisions'][field]
        docs = row['document_states'][name]
        assert docs['document_ids'] == [row['source_document_id']]
        assert docs['active_document_id'] == state['document_id'] == row['source_document_id']
        assert len(docs['documents']) == 1
        owner = docs['documents'][0]
        plane_words(owner, expected)
        for field in ('selection', 'previous', 'rois', 'rows', 'overlays', 'calibration', 'workflow', 'undo', 'redo'):
            assert owner[field] == state[field]
    for name in ('configured', 'prompt', 'display_moved', 'reopened'):
        panel = obs[name][panel_name]
        assert panel['open'] and not panel['scientific_targets'] and not panel['preview']
        assert panel['stack_prompt'] is (name in {'prompt', 'display_moved'})
        if rank:
            assert panel['method'] == 'mean' and panel['radius'] == 2.5
            if name != 'configured':
                assert panel['remembered_radii']['mean'] == 2.5
        else:
            assert panel['sigma'] == 1 and panel['scaled_units'] is False
    assert not obs['chosen'][panel_name]['open'] and not obs['final_cancel'][panel_name]['open']
    for name in ('display_moved', 'chosen', 'reopened', 'final_cancel'):
        assert obs[name]['ordinary_pixels']['current_tcz'] == [0, 0, 2]
        assert obs[name]['ordinary_pixels']['pixels_source'] == 'live'
    if choice == 'cancel':
        assert 'output_document_id' not in row
    else:
        assert row['output_document_id'] == row['source_document_id']
        record = states['chosen']['workflow'][-1]
        assert record['operation'] == row['recipe']['operation']
        assert record['options'] == ({'method': 'mean', 'radius': 2.5} if rank else {'sigma': 1.0, 'scaled_units': False})
        target = record['target']
        assert target['source_shape'] == [1, 1, 3, 17, 23, 1]
        assert target['roi_policy'] == 'active_selection' and target['destination'] == 'in_place'
        assert captured_positions(target) == ([[0, 0, z] for z in range(3)] if choice == 'yes' else [[0, 0, 1]])
    assert len(row['display_injections']) == 1
    injection = row['display_injections'][0]
    assert injection['method'] == 'Window.setNativeDisplay'
    assert injection['purpose'] == 'external display-only stimulus during Process Stack'
    assert injection['ui_navigation_credit'] is False and injection['requested_position'] == [0, 0, 2]
    assert 0 < injection['frame_before'] < injection['frame_after'] <= len(row['frames'])
    before, after = injection['before'], injection['after']
    for field in ('document_id', 'data_identity', 'dirty', 'undo', 'redo', 'history'):
        assert before[field] == after[field]
    for field in ('pixels', 'metadata', 'selection'):
        assert before['revisions'][field] == after['revisions'][field]
    assert before['display']['crosshair'][2] == 1 and after['display']['crosshair'][2] == 2
    assert after['revisions']['display'] > before['revisions']['display']
    assert [hit['image'] for hit in row['canvas_hits']] == [[2.2, 1.2], [9.2, 1.2], [2.2, 8.2]]
    assert all(hit['hovered_id'] == hit['canvas_id'] != 0 for hit in row['canvas_hits'])
    input_protocol(row)
