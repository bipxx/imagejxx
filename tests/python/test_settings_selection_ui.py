"""Independent literals for actual Preferences/enlargement/fresh-host runs."""
import gzip
import json
import os
from pathlib import Path
import struct

import pytest

from test_settings_wand_ui import decode, input_protocol, preferences_file, required_events, click, text

CASES = ('enlargement-consume-ordinary', 'enlargement-consume-scientific',
         'enlargement-persist', 'enlargement-reopen')
CONTEXTS = ('imgui_cpp', 'imgui_python')
CONSUME = ('before', 'pixels_applied', 'expand_opened', 'units_converted', 'cancelled',
           'expanded', 'restored', 'calibrated_applied', 'shrink_opened', 'shrunk',
           'restored_again', 'override_opened', 'override_applied', 'default_reopened', 'final_cancel')
PERSIST = ('before', 'draft', 'saved', 'closed')
REOPEN = ('loaded', 'ordinary_expand_opened', 'ordinary_expanded', 'ordinary_restored',
          'ordinary_shrink_opened', 'ordinary_shrunk', 'scientific_expand_opened',
          'scientific_expanded', 'scientific_restored', 'scientific_shrink_opened',
          'scientific_shrunk', 'shown', 'final_cancel')
R, E2, E3, S3 = ((4, 4, 12, 11), (2, 2, 14, 13), (1, 1, 15, 14), (7, 7, 9, 8))
LINE = {'kind': 'line', 'points': [[0, 16], [16, 0]], 'width': 1}
OPEN_PREFERENCES = [click('host.menu.Edit'), click('host.menu.Edit.Preferences'),
                    text('preferences.search', 'enlargement'), click('preferences.group'), click('group.Selection')]
GROW = [click('host.menu.Edit'), click('host.menu.Edit.Selection'), click('host.menu.form.grow_selection')]
SHRINK = [click('host.menu.Edit'), click('host.menu.Edit.Selection'), click('host.menu.form.shrink_selection')]
RESTORE = [click('host.menu.Edit'), click('host.menu.Edit.Selection'), click('host.menu.action.restore_selection')]


@pytest.fixture(scope='session')
def python_report_factory(tmp_path_factory):
    reports = None
    def produce():
        nonlocal reports
        if reports is None:
            from settings_selection_ui_runner import produce_python_reports
            reports = produce_python_reports(tmp_path_factory.mktemp('cfg2-python-ui'))
        return reports
    return produce


def load_case(case, context, python_reports=None):
    if os.environ.get('IMAGEJXX_SETTINGS_SELECTION_UI_REPORTS'):
        path = json.loads(os.environ['IMAGEJXX_SETTINGS_SELECTION_UI_REPORTS'])[context][case]
    elif context == 'imgui_cpp' and os.environ.get('IMAGEJXX_SETTINGS_SELECTION_CPP_REPORTS'):
        path = json.loads(os.environ['IMAGEJXX_SETTINGS_SELECTION_CPP_REPORTS'])[case]
    elif context == 'imgui_python' and python_reports and os.environ.get('IMAGEJXX_REQUIRE_RENDER_TESTS') == '1':
        path = python_reports()[case]
    else:
        if os.environ.get('IMAGEJXX_REQUIRE_RENDER_TESTS') == '1':
            pytest.fail('Required selection settings observation missing: '+context+'/'+case)
        pytest.skip('Installed selection settings UI observations not supplied')
    path = Path(path)
    if path.suffix == '.gz':
        with gzip.open(path, 'rb') as stream:
            raw = stream.read(16*1024**2+1)
    else:
        raw = path.read_bytes()
    row = decode(raw)
    assert row['schema'] == 'cfg2.settings-selection.ui.001'
    assert row['case_id'] == case and row['context'] == context
    assert row['status'] == 'observed' and row['error'] == '' and row['acceptance_claim'] is False
    assert row['bounds'] == {'frames': 750, 'seconds': 60, 'report_bytes_exclusive': 16*1024**2,
                             'preference_bytes': 1024**2}
    assert 0 < len(row['frames']) <= 750
    assert row['events'] and row['setup_events']
    for index, frame in enumerate(row['frames']):
        assert frame['ordinal'] == index
        assert frame['events'] == frame['rendered']['input_frame']['raw_inputs']
    input_protocol(row)
    names = PERSIST if case == 'enlargement-persist' else REOPEN if case == 'enlargement-reopen' else CONSUME
    assert set(row['states']) == set(names)
    indices = [row['states'][name]['frame'] for name in names]
    assert indices == sorted(set(indices)) and indices[0] >= 0 and indices[-1] < len(row['frames'])
    assert set(row['milestones']) == ({'negative_refused', 'negative_corrected'} if case == CASES[0] else set())
    for state in row['states'].values():
        assert state['error'] == '' and state['workflow'] == [] and state['results'] == []
        assert state['recording'] is False
        assert all(state['usage'][key] == 0 for key in ('running', 'queued', 'running_bytes', 'retained_bytes', 'reserved_bytes'))
        preferences_file(state['preferences_file'])
    return row


def rectangle(box):
    a, b, c, d = box
    return {'kind': 'rectangle', 'points': [[a, b], [c, d]], 'width': 1}


def mask_hex(box):
    a, b, c, d = box
    return bytes(255 if a <= x < c and b <= y < d else 0 for y in range(17) for x in range(17)).hex()


def selection(owner, box, previous):
    assert owner['selection'] == rectangle(box)
    assert owner['selection_observation'] == {'mask_shape': [17, 17], 'mask_u8_hex': mask_hex(box), 'packed_mask_hex': None}
    assert owner['previous'] == (LINE if previous == 'line' else rectangle(previous))
    if previous != 'line':
        assert owner['previous_observation'] == {'mask_shape': [17, 17], 'mask_u8_hex': mask_hex(previous), 'packed_mask_hex': None}


def source(owner):
    n = 2 if owner['scientific'] else 1
    assert owner['shape'] == [n, n, n, 17, 17, 1]
    assert owner['dtype'] == 'uint8' and owner['sample_policy'] == 'finite_only'
    assert len(owner['planes']) == n**3
    for plane, (t, c, z) in zip(owner['planes'], ((t, c, z) for t in range(n) for c in range(n) for z in range(n))):
        values = [(19*t+31*c+47*z+3*y+x) % 256 for y in range(17) for x in range(17)]
        assert plane['position'] == [t, c, z] and plane['shape'] == [17, 17, 1]
        assert plane['declared_u8_hex'] == bytes(values).hex()
        assert plane['storage_f64_hex'] == b''.join(struct.pack('<d', v) for v in values).hex()
    assert owner['metadata'] == {'title': 'CFG2 '+('scientific' if n == 2 else 'ordinary'),
        'properties': {'cfg2': {'label': 'retain properties', 'value': 17}},
        'extensions': {'cfg2': 'retain extension'}, 'record_layout': {'cfg2': 'retain layout'}}
    assert owner['calibration'] == {'spacing': [.5, 1.5, 2], 'origin': [0, 0, 0],
        'direction': [1, 0, 0, 0, 1, 0, 0, 0, 1], 'unit': 'um', 'frame_interval': 1,
        'time_origin': 0, 'time_unit': 's'}
    assert owner['rois'] == [{'name': 'cfg2 retained ROI', 'selection': rectangle((0, 0, 1, 1)),
        't': None, 'c': None, 'z': None, 'attributes': {'cfg2': 'retain ROI'}}]
    assert owner['overlays'] == [{'kind': 'point', 'points': [[16, 0]], 'cfg2': 'retain overlay'}]
    assert owner['display']['t'] == owner['display']['c'] == n-1
    assert owner['display']['crosshair'] == [8, 8, n-1]
    assert owner['display']['properties']['display_range'] == [0, 255]
    assert owner['undo'] == owner['redo'] == 0 and owner['dirty'] is False


def invariant(state, baseline):
    assert len(state['owners']) == len(baseline['owners'])
    assert len(state['settings']) == 61
    assert set(state['settings']) == set(baseline['settings'])
    for key in baseline['settings']:
        if key not in ('selection_enlargement_distance', 'selection_enlargement_units'):
            assert state['settings'][key] == baseline['settings'][key], key
    for owner, initial in zip(state['owners'], baseline['owners']):
        for key in ('document_id', 'scientific', 'shape', 'dtype', 'sample_policy', 'planes',
                    'metadata', 'calibration', 'rois', 'overlays', 'display', 'dirty', 'undo', 'redo', 'history'):
            assert owner[key] == initial[key], key
        for key in ('pixels', 'metadata', 'display'):
            assert owner['revisions'][key] == initial['revisions'][key]


def pair(state):
    return state['settings']['selection_enlargement_distance'], state['settings']['selection_enlargement_units']


def dialog(state, distance, units):
    actual = state['observations']['dialog']
    assert actual['open'] and actual['title'] == 'Enlarge selection'
    assert actual['values'] == {'distance': distance, 'units': units}
    assert actual['controls']['apply']['enabled'] and actual['controls']['cancel']['enabled']
    if units == 'calibrated':
        assert 'anisotropy_warning' in actual['controls']


def check_case(row):
    states, case = row['states'], row['case_id']
    names = PERSIST if case == 'enlargement-persist' else REOPEN if case == 'enlargement-reopen' else CONSUME
    baseline = states[names[0]]
    def events(start, end, required):
        required_events(row, states[start]['frame'], states[end]['frame'], required)
    for owner in baseline['owners']:
        source(owner)
        selection(owner, R, 'line')
    for state in states.values():
        invariant(state, baseline)
    if case.startswith('enlargement-consume-'):
        assert len(baseline['owners']) == 1 and baseline['owners'][0]['scientific'] == (case == CASES[1])
        expected = {'expanded': (E2, R), 'restored': (R, E2), 'shrunk': (S3, R),
                    'restored_again': (R, S3), 'override_applied': (E2, R),
                    'default_reopened': (E2, R), 'final_cancel': (E2, R)}
        current = (R, 'line')
        for name in CONSUME:
            current = expected.get(name, current)
            selection(states[name]['owners'][0], *current)
            assert states[name]['active_id'] == baseline['owners'][0]['document_id']
            assert pair(states[name]) == ((15., 'calibrated') if name == 'before' else
                (2., 'pixels') if CONSUME.index(name) < CONSUME.index('calibrated_applied') else (3., 'calibrated'))
        dialog(states['expand_opened'], 2., 'pixels')
        dialog(states['units_converted'], 1., 'calibrated')
        dialog(states['shrink_opened'], -1.5, 'calibrated')
        dialog(states['override_opened'], 1.5, 'calibrated')
        dialog(states['default_reopened'], 1.5, 'calibrated')
        assert not states['cancelled']['observations']['dialog']['open']
        for name in ('pixels_applied', 'expand_opened', 'units_converted', 'cancelled'):
            assert states[name]['owners'][0]['data_identity'] == baseline['owners'][0]['data_identity']
            assert states[name]['owners'][0]['revisions'] == baseline['owners'][0]['revisions']
        assert states['cancelled']['preferences_file'] == states['pixels_applied']['preferences_file']
        assert all(states[name]['preferences_file'] == states['calibrated_applied']['preferences_file']
                   for name in CONSUME[CONSUME.index('calibrated_applied'):])
        assert not states['final_cancel']['observations']['dialog']['open']
        assert states['final_cancel']['owners'] == states['default_reopened']['owners']
        assert not states['before']['preferences_file']['exists']
        authored = ([text('field.selection_enlargement_distance', '-1'), click('units.combo'), click('units.choice.pixels'),
                     click('preferences.apply'), text('field.selection_enlargement_distance', '2')]
                    if case == CASES[0] else [text('field.selection_enlargement_distance', '2'),
                                            click('units.combo'), click('units.choice.pixels')])
        events('before', 'pixels_applied', OPEN_PREFERENCES+authored+[click('preferences.apply'), click('preferences.close_button')])
        events('pixels_applied', 'expand_opened', GROW)
        events('cancelled', 'expanded', GROW+[click('dialog.apply')])
        events('expanded', 'restored', RESTORE)
        events('restored', 'calibrated_applied', OPEN_PREFERENCES+[text('field.selection_enlargement_distance', '3'),
               click('units.combo'), click('units.choice.calibrated'), click('preferences.apply'), click('preferences.close_button')])
        events('calibrated_applied', 'shrink_opened', SHRINK)
        events('shrink_opened', 'shrunk', [click('dialog.apply')])
        events('shrunk', 'restored_again', RESTORE)
        events('restored_again', 'override_opened', GROW)
        events('override_applied', 'default_reopened', GROW)
        events('default_reopened', 'final_cancel', [click('dialog.cancel')])
        required_events(row, states['expand_opened']['frame'], states['cancelled']['frame'],
                        [click('dialog.units'), click('dialog.units.choice.calibrated'), click('dialog.cancel')])
        required_events(row, states['override_opened']['frame'], states['override_applied']['frame'],
                        [text('dialog.distance', '1'), click('dialog.apply')])
        if case == CASES[0]:
            refused, corrected = (row['milestones'][key] for key in ('negative_refused', 'negative_corrected'))
            assert pair(refused) == pair(corrected) == (15., 'calibrated')
            assert not corrected['preferences_file']['exists']
            assert not refused['preferences_file']['exists'] and refused['observations']['preferences_panel']['error']
            for state, value in ((refused, -1.), (corrected, 2.)):
                assert state['settings'] == baseline['settings']
                fields = state['observations']['preferences_panel']['fields']
                assert fields['selection_enlargement_distance']['draft'] == value
                assert fields['selection_enlargement_units']['draft'] == 'pixels'
                assert state['owners'][0]['selection'] == rectangle(R)
                assert state['owners'][0]['data_identity'] == baseline['owners'][0]['data_identity']
                assert state['owners'][0]['revisions'] == baseline['owners'][0]['revisions']
                assert state['owners'][0]['history'] == baseline['owners'][0]['history']
    elif case == 'enlargement-persist':
        assert len(baseline['owners']) == 1 and not baseline['owners'][0]['scientific']
        assert not states['before']['preferences_file']['exists'] and not states['draft']['preferences_file']['exists']
        assert pair(states['before']) == pair(states['draft']) == (15., 'calibrated')
        for name in PERSIST:
            selection(states[name]['owners'][0], R, 'line')
            assert states[name]['owners'][0]['data_identity'] == baseline['owners'][0]['data_identity']
            assert states[name]['owners'][0]['revisions'] == baseline['owners'][0]['revisions']
        assert pair(states['saved']) == pair(states['closed']) == (3., 'calibrated')
        assert states['saved']['preferences_file'] == states['closed']['preferences_file']
        assert not states['closed']['observations']['preferences_panel']['open']
        events('before', 'draft', OPEN_PREFERENCES+[text('field.selection_enlargement_distance', '3'),
               click('units.combo'), click('units.choice.calibrated')])
        events('draft', 'saved', [click('preferences.apply')])
        events('saved', 'closed', [click('preferences.close_button')])
    else:
        assert [owner['scientific'] for owner in baseline['owners']] == [False, True]
        expected = [(R, 'line'), (R, 'line')]
        for name in REOPEN:
            for index, prefix in enumerate(('ordinary', 'scientific')):
                if name == prefix+'_expanded': expected[index] = (E3, R)
                if name == prefix+'_restored': expected[index] = (R, E3)
                if name == prefix+'_shrunk': expected[index] = (S3, R)
            for owner, geometry in zip(states[name]['owners'], expected):
                selection(owner, *geometry)
            assert pair(states[name]) == (3., 'calibrated')
            assert states[name]['preferences_file'] == baseline['preferences_file']
        for prefix in ('ordinary', 'scientific'):
            dialog(states[prefix+'_expand_opened'], 1.5, 'calibrated')
            dialog(states[prefix+'_shrink_opened'], -1.5, 'calibrated')
            index = int(prefix == 'scientific')
            assert states[prefix+'_expanded']['active_id'] == baseline['owners'][index]['document_id']
            events(prefix+'_expand_opened', prefix+'_expanded', [click('dialog.apply')])
            events(prefix+'_expanded', prefix+'_restored', RESTORE)
            events(prefix+'_restored', prefix+'_shrink_opened', SHRINK)
            events(prefix+'_shrink_opened', prefix+'_shrunk', [click('dialog.apply')])
        events('loaded', 'ordinary_expand_opened', GROW)
        events('ordinary_shrunk', 'scientific_expand_opened',
               [click('document.'+str(baseline['owners'][1]['document_id']))]+GROW)
        events('scientific_shrunk', 'shown', OPEN_PREFERENCES)
        events('shown', 'final_cancel', [click('preferences.close_button')])
        assert not states['final_cancel']['observations']['preferences_panel']['open']
        assert states['final_cancel']['owners'] == states['shown']['owners']
        fields = states['shown']['observations']['preferences_panel']['fields']
        assert fields['selection_enlargement_distance']['draft'] == fields['selection_enlargement_distance']['applied'] == 3.
        assert fields['selection_enlargement_units']['draft'] == fields['selection_enlargement_units']['applied'] == 'calibrated'
        assert any(event['control'].startswith('document.') for event in row['events'])
    for name, state in states.items():
        if state['preferences_file']['exists']:
            payload = preferences_file(state['preferences_file'])
            assert all(payload['values'][key] == value for key, value in state['settings'].items())


@pytest.mark.parametrize('context', CONTEXTS)
@pytest.mark.parametrize('case_id', CASES)
def test_installed_preferences_enlargement_consumer(case_id, context, python_report_factory):
    check_case(load_case(case_id, context, python_report_factory))


@pytest.mark.parametrize('context', CONTEXTS)
def test_enlargement_persistence_is_consumed_by_a_distinct_fresh_process(context, python_report_factory):
    persisted = load_case('enlargement-persist', context, python_report_factory)
    reopened = load_case('enlargement-reopen', context, python_report_factory)
    before, after = persisted['states']['closed'], reopened['states']['loaded']
    assert before['preferences_file'] == after['preferences_file']
    assert pair(before) == pair(after) == (3., 'calibrated')
    assert persisted['fixture']['runtime']['process_id'] != reopened['fixture']['runtime']['process_id']
    check_case(persisted)
    check_case(reopened)
