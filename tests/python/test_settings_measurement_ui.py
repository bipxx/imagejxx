"""Independent source, particle and CSV literals for actual installed UI routes."""
import csv
import gzip
import hashlib
import io
import json
import os
from pathlib import Path
import struct

import pytest

from test_settings_wand_ui import decode, input_protocol, preferences_file, required_events, click, text

CASES = ('measurement-names-edit-persist', 'measurement-names-reopen')
CONTEXTS = ('imgui_cpp', 'imgui_python')
NAMES = ['min', 'max', 'bbox', 'pixels']
VALUES = ['2.0', '7.0', '[1,1,3,2]', '6']
EXTRA = ['Add', 'Column name', 'All available columns', 'custom##literal']
EDIT = ('before', 'measured', 'names_draft', 'applied', 'table', 'csv_saved',
        'changed_draft', 'closed_cancel', 'defaults_draft', 'reverted', 'final_close')
REOPEN = ('loaded', 'measured', 'preferences_opened', 'table', 'csv_saved', 'final_close')
OPEN = [click('host.menu.Edit'), click('host.menu.Edit.Preferences'),
        text('preferences.search', 'measurements'), click('preferences.group'), click('group.Measurements')]
PARTICLES = [click('host.menu.Analyze'), click('host.menu.form.particles'), click('dialog.apply')]
REFUSALS = ('blank_refused', 'duplicate_refused', 'overlong_refused')
REMOVALS = ('choice_add_removed', 'choice_column_name_removed', 'choice_all_removed', 'choice_literal_removed')


@pytest.fixture(scope='session')
def python_report_factory(tmp_path_factory):
    reports = None
    def produce():
        nonlocal reports
        if reports is None:
            from settings_measurement_ui_runner import produce_python_reports
            reports = produce_python_reports(tmp_path_factory.mktemp('cfg3-python-ui'))
        return reports
    return produce


def load_case(case, context, python_reports=None):
    if os.environ.get('IMAGEJXX_SETTINGS_MEASUREMENT_UI_REPORTS'):
        path = json.loads(os.environ['IMAGEJXX_SETTINGS_MEASUREMENT_UI_REPORTS'])[context][case]
    elif context == 'imgui_cpp' and os.environ.get('IMAGEJXX_SETTINGS_MEASUREMENT_CPP_REPORTS'):
        path = json.loads(os.environ['IMAGEJXX_SETTINGS_MEASUREMENT_CPP_REPORTS'])[case]
    elif context == 'imgui_python' and python_reports and os.environ.get('IMAGEJXX_REQUIRE_RENDER_TESTS') == '1':
        path = python_reports()[case]
    else:
        if os.environ.get('IMAGEJXX_REQUIRE_RENDER_TESTS') == '1':
            pytest.fail('Required measurement observation missing: '+context+'/'+case)
        pytest.skip('Installed measurement UI observations not supplied')
    path = Path(path)
    if path.suffix == '.gz':
        with gzip.open(path, 'rb') as stream:
            raw = stream.read(16*1024**2+1)
    else:
        raw = path.read_bytes()
    row = decode(raw)
    assert row['schema'] == 'cfg3.measurement-names.ui.001'
    assert row['case_id'] == case and row['context'] == context
    assert row['status'] == 'observed' and row['error'] == '' and row['acceptance_claim'] is False
    assert row['bounds'] == {'frames': 750, 'seconds': 60, 'report_bytes_exclusive': 16*1024**2,
                             'preference_bytes': 1024**2, 'csv_bytes': 1024**2}
    assert 0 < len(row['frames']) <= 750 and row['events'] and row['setup_events']
    for index, frame in enumerate(row['frames']):
        assert frame['ordinal'] == index
        assert frame['events'] == frame['rendered']['input_frame']['raw_inputs']
    input_protocol(row)
    for event in row['events']:
        if event['kind'] != 'text_input':
            continue
        interval = row['frames'][event['frame_before']:event['frame_after']+1]
        enter = [atom for frame in interval for atom in frame['events']
                 if atom.get('type') == 'key' and atom.get('name') == 'Enter']
        assert [atom['down'] for atom in enter] == ([True,False] if event['enter'] else [])
        if event['control'] == 'names.pending':
            delivered = next(frame for frame in interval if any(
                atom.get('type') == 'text' and atom.get('value') == event['text'] for atom in frame['events']))
            control = delivered['rendered']['preferences_panel']['fields']['measurements']['controls']['pending']
            assert control['id'] == event['input_id'] and control['enabled'] and control['visible']
    names = EDIT if case == CASES[0] else REOPEN
    assert set(row['states']) == set(names)
    indices = [row['states'][name]['frame'] for name in names]
    assert indices == sorted(set(indices)) and indices[0] >= 0 and indices[-1] < len(row['frames'])
    assert set(row['milestones']) == (set(REFUSALS + ('choice_ids_ready',) + REMOVALS) if case == CASES[0] else set())
    if case == CASES[0]:
        milestone_frames = [row['states']['measured']['frame'],
            *(row['milestones'][name]['frame'] for name in REFUSALS+('choice_ids_ready',)+REMOVALS),
            row['states']['names_draft']['frame']]
        assert milestone_frames == sorted(set(milestone_frames))
    runtime = row['fixture']['runtime']
    assert type(runtime['process_id']) is int and runtime['process_id'] > 0
    assert type(runtime['parent_process_id']) is int and runtime['parent_process_id'] > 0
    return row


def source(owner):
    words = [0,0,0,0,0,0, 0,2,3,4,0,0, 0,5,6,7,0,0, 0,0,0,0,0,0, 0,0,0,0,0,0]
    assert owner['shape'] == [1,1,1,5,6,1] and owner['dtype'] == 'uint8'
    assert owner['scientific'] is False and owner['sample_policy'] == 'finite_only'
    assert owner['planes'] == [{'position': [0,0,0], 'shape': [5,6,1],
        'declared_u8_hex': bytes(words).hex(), 'storage_f64_hex': b''.join(struct.pack('<d', v) for v in words).hex()}]
    assert owner['selection'] is None and owner['selection_observation'] is None
    assert owner['previous'] == {'kind': 'line', 'points': [[0,4],[5,0]], 'width': 1}
    assert owner['metadata'] == {'title': 'CFG3 measurements',
        'properties': {'cfg3': {'label': 'retain properties', 'value': 17}},
        'extensions': {'cfg3': 'retain extension'}, 'record_layout': {'cfg3': 'retain layout'}}
    assert owner['calibration'] == {'spacing': [1,1,1], 'origin': [0,0,0],
        'direction': [1,0,0,0,1,0,0,0,1], 'unit': 'px', 'frame_interval': 1, 'time_origin': 0, 'time_unit': 's'}
    assert owner['rois'] == [{'name': 'cfg3 retained ROI',
        'selection': {'kind': 'rectangle', 'points': [[0,0],[1,1]], 'width': 1},
        't': None, 'c': None, 'z': None, 'attributes': {'cfg3': 'retain ROI'}}]
    assert owner['overlays'] == [{'kind': 'point', 'points': [[5,4]], 'cfg3': 'retain overlay'}]
    assert owner['display']['t'] == owner['display']['c'] == 0
    assert owner['display']['crosshair'] == [2,2,0] and owner['display']['zoom'] == 32
    assert owner['display']['properties']['display_range'] == [0,255]
    assert owner['undo'] == owner['redo'] == 0 and owner['dirty'] is False


def field(state):
    return state['observations']['preferences_panel']['fields']['measurements']


def check_csv(file):
    assert file['exists'] and Path(file['path']).is_absolute()
    raw = file['utf8'].encode('utf-8')
    assert 0 < len(raw) == file['byte_count'] <= 1024**2
    assert hashlib.sha256(raw).hexdigest() == file['sha256']
    assert list(csv.reader(io.StringIO(file['utf8'], newline=''))) == [NAMES, VALUES]


def table(state):
    assert state['observations']['results_columns'] == NAMES
    actual = state['observations']['results_table']
    assert actual['begin_table'] and actual['visible'] and not actual['truncated']
    assert (actual['page'], actual['page_count'], actual['page_size']) == (0,1,64)
    assert (actual['first_column'], actual['end_column_exclusive'], actual['column_count'], actual['row_count']) == (0,4,4,1)
    assert [(h['column'], h['name'], h['text'], h['text_source']) for h in actual['headers']] == [
        (i, name, name, 'TableHeadersRow') for i, name in enumerate(NAMES)]
    assert [(c['row'], c['column'], c['name'], c['text']) for c in actual['cells']] == [
        (0, i, name, value) for i, (name,value) in enumerate(zip(NAMES, VALUES))]
    assert all(h['visible'] for h in actual['headers']) and all(c['visible'] for c in actual['cells'])


def check_case(row):
    states, case = row['states'], row['case_id']
    order = EDIT if case == CASES[0] else REOPEN
    baseline = states[order[0]]
    assert len(baseline['owners']) == 1 and len(baseline['settings']) == 61
    source(baseline['owners'][0])
    measured = states['measured']['results']
    assert len(measured) == 1
    expected = {'min': 2., 'max': 7., 'bbox': [1,1,3,2], 'pixels': 6,
                'area': 6., 'mean': 4.5, 'integrated_density': 27., 'label': 1, 'touches_border': False}
    assert all(measured[0][key] == value for key,value in expected.items())
    assert baseline['results'] == []
    for name, state in states.items():
        assert state['owners'] == baseline['owners'], name
        assert state['active_id'] == baseline['owners'][0]['document_id']
        assert state['workflow'] == [] and state['recording'] is False and state['error'] == ''
        assert all(value == 0 for value in state['usage'].values())
        assert {k:v for k,v in state['settings'].items() if k != 'measurements'} == {
            k:v for k,v in baseline['settings'].items() if k != 'measurements'}
        assert state['results'] == ([] if name == order[0] else measured)
        payload = preferences_file(state['preferences_file'])
        if payload:
            assert all(payload['values'][k] == v for k,v in state['settings'].items())
        if state['csv_file']['exists']:
            check_csv(state['csv_file'])
    def events(start,end,needed):
        required_events(row,states[start]['frame'],states[end]['frame'],needed)
    events(order[0], 'measured', PARTICLES)
    if case == CASES[0]:
        for name in EDIT[:3]:
            assert states[name]['settings']['measurements'] == [] and not states[name]['preferences_file']['exists']
        for name in EDIT[3:]:
            assert states[name]['settings']['measurements'] == NAMES
            assert states[name]['preferences_file'] == states['applied']['preferences_file']
        assert field(states['names_draft'])['draft'] == NAMES
        assert field(states['names_draft'])['pending_name'] == 'do not add on Apply'
        assert field(states['applied'])['draft'] == NAMES
        assert field(states['applied'])['pending_name'] == 'do not add on Apply'
        assert field(states['changed_draft'])['draft'] == ['min','max','pixels']
        assert field(states['changed_draft'])['pending_name'] == 'do not save'
        assert field(states['defaults_draft'])['draft'] == []
        assert field(states['defaults_draft'])['pending_name'] == ''
        assert field(states['reverted'])['draft'] == NAMES and field(states['reverted'])['pending_name'] == ''
        for name in ('table','closed_cancel','final_close'):
            panel = states[name]['observations']['preferences_panel']
            assert not panel['open'] and panel['measurements_pending_name'] == ''
        for name,draft,pending in zip(REFUSALS, ([],['min'],['min','max']), ('   ','min','a'*257)):
            state = row['milestones'][name]
            panel = state['observations']['preferences_panel']
            assert panel['error'] and field(state)['draft'] == draft and field(state)['pending_name'] == pending
            assert panel['draft'] == {**baseline['settings'], 'measurements': draft}
            assert state['settings'] == baseline['settings'] and not state['preferences_file']['exists']
            assert state['results'] == measured
            required_events(row, states['measured']['frame'], state['frame'],
                            [text('names.pending',pending),click('names.add')])
            refused_inputs = [e for e in row['events'] if e['kind'] == 'text_input'
                              and e['control'] == 'names.pending' and e['text'] == pending
                              and e['frame_after'] <= state['frame']]
            assert refused_inputs and refused_inputs[-1]['enter'] is False
        ready = row['milestones']['choice_ids_ready']
        choices = field(ready)
        assert choices['draft'] == NAMES+EXTRA
        required_events(row, states['measured']['frame'], ready['frame'],
                        [step for name in EXTRA for step in (text('names.pending',name),click('names.add'))])
        controls = [*choices['controls'].values(), *(choices['choices'][name] for name in EXTRA)]
        ids = [control['id'] for control in controls]
        assert all(value > 0 for value in ids) and len(ids) == len(set(ids))
        for i,(name,removed) in enumerate(zip(EXTRA,REMOVALS)):
            observation = choices['choices'][name]
            assert observation['label_text'] == name and observation['selected']
            assert observation['label_text_source'] == ('TextUnformatted' if '##' in name else 'Checkbox')
            state = row['milestones'][removed]
            assert field(state)['draft'] == NAMES+EXTRA[i+1:]
            required_events(row, ready['frame'], state['frame'], [click('names.choice.'+n) for n in EXTRA[:i+1]])
        for state in row['milestones'].values():
            assert state['settings'] == baseline['settings'] and state['results'] == measured
            assert state['preferences_file'] == baseline['preferences_file']
            assert state['csv_file'] == baseline['csv_file'] and not state['csv_file']['exists']
            assert state['active_id'] == baseline['active_id']
            assert state['workflow'] == [] and state['recording'] is False and state['error'] == ''
            assert all(value == 0 for value in state['usage'].values())
            assert state['owners'] == [{k:v for k,v in baseline['owners'][0].items()
                                      if k not in ('planes','selection_observation','previous_observation')}]
        events('measured','names_draft',OPEN+[text('names.pending','min'),text('names.pending','max'),
            text('names.pending','bbox'),click('names.choice.pixels')])
        events('names_draft','applied',[click('preferences.apply')])
        events('applied','table',[click('preferences.close_button')])
        events('csv_saved','changed_draft',OPEN+[click('names.choice.bbox'),text('names.pending','do not save')])
        events('changed_draft','closed_cancel',[click('preferences.close_button')])
        events('closed_cancel','defaults_draft',OPEN+[
            text('names.pending','discard on Defaults'),
            text('preferences.search','hidden-settings-filter'),text('preferences.search','measurements'),
            click('preferences.group'),click('group.Drawing'),click('preferences.defaults'),
            click('preferences.group'),click('group.Measurements'),click('preferences.defaults')])
        drawing_defaults = next(e for e in row['events'] if e['kind'] == 'control_click'
            and e['control'] == 'preferences.defaults'
            and states['closed_cancel']['frame'] < e['frame_before'] < states['defaults_draft']['frame'])
        panel = row['frames'][drawing_defaults['frame_after']]['rendered']['preferences_panel']
        assert panel['group'] == 'Drawing' and panel['measurements_pending_name'] == 'discard on Defaults'
        events('defaults_draft','reverted',[text('names.pending','discard on Revert'),click('preferences.revert')])
        inputs = [e for e in row['events'] if e['kind'] == 'text_input' and e['control'] == 'names.pending']
        assert any(e['text'] == 'min' and e['enter'] is True for e in inputs)
        assert any(e['text'] == 'bbox' and e['enter'] is True for e in inputs)
        assert any(e['text'] == 'do not add on Apply' and e['enter'] is False for e in inputs)
    else:
        for state in states.values():
            assert state['settings']['measurements'] == NAMES
            assert state['preferences_file'] == baseline['preferences_file']
        shown = field(states['preferences_opened'])
        assert shown['draft'] == shown['applied'] == NAMES and shown['pending_name'] == ''
        events('measured','preferences_opened',OPEN)
        events('preferences_opened','table',[click('preferences.close_button')])
    table(states['table'])
    check_csv(states['csv_saved']['csv_file'])
    events('table','csv_saved',[click('host.results.save_csv'),
        text('host.browser.path',states['csv_saved']['csv_file']['path']),click('host.browser.submit')])


@pytest.mark.parametrize('context',CONTEXTS)
@pytest.mark.parametrize('case_id',CASES)
def test_installed_measurement_names(case_id,context,python_report_factory):
    check_case(load_case(case_id,context,python_report_factory))


@pytest.mark.parametrize('context',CONTEXTS)
def test_saved_names_consumed_in_distinct_process(context,python_report_factory):
    edit = load_case(CASES[0],context,python_report_factory)
    reopen = load_case(CASES[1],context,python_report_factory)
    assert edit['fixture']['runtime']['process_id'] != reopen['fixture']['runtime']['process_id']
    assert edit['states']['final_close']['preferences_file'] == reopen['states']['loaded']['preferences_file']
    check_case(edit)
    check_case(reopen)
