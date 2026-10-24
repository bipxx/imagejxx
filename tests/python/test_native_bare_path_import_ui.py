"""Actual shared ImGui import choices, native XYZ previews and atomic refusal."""
import base64
import json
import os
from pathlib import Path
import pytest
import imagejxx as ij
from test_planar_path_ui import COMMANDS


@pytest.fixture(scope='module', params=['imgui_cpp', 'imgui_python'])
def case(request, tmp_path_factory):
    if request.param == 'imgui_cpp':
        path = Path(os.environ['IMAGEJXX_NATIVE_BARE_CPP_REPORT'])
        return json.loads(path.read_bytes()), path.parent
    from imagejxx import _desktop
    root = tmp_path_factory.mktemp('native-bare-ui')
    editor = _desktop.Editor(str(root/'preferences.json'))
    return json.loads(editor._native_bare_import_ui_case(str(root/'capture'))), root/'capture'


def rows(state):
    return state['metadata'].get('imagejxx.planar_annotations', {}).get('rows', [])


def sources(state):
    return state['metadata'].get('imagejxx.imagej_roi_import_sources')


def test_actual_explicit_choice_and_review_do_not_publish(case):
    report, _ = case
    assert report['contract'] == 'p13c.native-bare-import.ui.001'
    assert report['state'] == 'observed' and report['error'] == ''
    assert not report['unmapped']['ready'] and report['unmapped']['error']
    assert report['review']['ready'] and not report['review']['error']
    assert [v['kind'] for v in report['review']['review']] == ['path']*3
    assert all(v['native_path'] for v in report['review']['entries'])
    assert not report['invalidated']['ready']
    for name in ['before', 'unmapped', 'review']:
        state = report['states'][name]
        assert state['undo'] == 0 and rows(state) == [] and sources(state) is None
    for frame in report['frames']:
        assert frame['events'] == frame['rendered']['input_frame']['raw_inputs']
    names = [v['name'] for v in report['controls']]
    assert all(name in names for name in ['format.2', 'plane.1', 'plane.2', 'plane.3', 'explicit_slice', 'apply native bare import'])


def test_actual_native_xyz_publication_and_original_transport(case):
    report, _ = case; state = report['states']['applied']
    assert state['undo'] == 1 and len(state['workflow']) == 1
    assert state['selection'] is None and state['legacy_roi_count'] == 0
    assert [(v['plane'], v['coordinate']) for v in rows(state)] == [('XY', 0), ('XZ', 0), ('YZ', 0)]
    assert all(v['selection']['path'] == COMMANDS for v in rows(state))
    archive = sources(state)
    assert archive['version'] == 5 and len(archive['entries']) == 3
    for entry, plane in zip(archive['entries'], ['xy', 'xz', 'yz']):
        assert entry['native_path'] is True and entry['mapping']['plane'] == plane
        raw = base64.b64decode(entry['data'])
        assert raw == bytes(report['source_bytes'])
        original = ij.decode_imagej_composite_roi(raw)
        assert original.roi.selection.path.commands == COMMANDS and original.group == 7


def test_actual_preview_draw_buffers_and_idle_cache_reuse(case):
    report, _ = case
    for plane in report['preview_render']:
        layers = plane['import_preview']['layers']
        assert len(layers) == 1 and layers[0]['geometry']['path'] == COMMANDS
        assert layers[0]['geometry']['path_version'] == 1
        assert layers[0]['draw_index_end'] > layers[0]['draw_index_begin']
    for before, after in zip(report['idle_before'], report['idle_after']):
        a, b = before['import_preview']['layers'], after['import_preview']['layers']
        assert [v['screen_builds'] for v in a] == [v['screen_builds'] for v in b]


def test_actual_history_atomic_bad_member_and_session_reopen(case):
    report, directory = case; states = report['states']
    assert rows(states['undone']) == [] and sources(states['undone']) is None and states['undone']['undo'] == 0
    assert rows(states['redone']) == rows(states['applied']) and states['redone']['undo'] == 1
    assert states['refused'] == states['redone']
    assert report['bad_review']['error'] and not report['bad_review']['ready']
    assert rows(states['reopened']) == rows(states['edited_import'])
    assert sources(states['reopened']) == sources(states['edited_import'])
    expected = [[100*z+10*y+x for y in range(10) for x in range(14)] for z in range(6)]
    assert all(state['pixels'] == expected for state in states.values())
    decoded = ij.decode_session((directory/'imports.ijx').read_bytes()).documents[0]
    assert all(row.selection.path.commands == COMMANDS for row in ij.planar_annotations(decoded.snapshot())[:3])
    assert all(entry.native_path for entry in ij.inspect_imagej_annotation_sources(decoded.snapshot()))


def test_actual_native_replacement_keeps_original_and_binary64_control(case):
    report, _ = case; states = report['states']
    assert states['edited_review'] == states['redone']
    assert report['native_editor']['kind'] == 'path' and report['native_editor']['error'] == ''
    state = states['edited_import']
    assert len(rows(state)) == 4 and state['undo'] == 2
    expected = COMMANDS.copy(); expected[4:6] = [3.7500000000000004, .8750000000000001]
    assert rows(state)[3]['selection']['path'] == expected
    archive = sources(state)
    assert archive['version'] == 6 and len(archive['entries']) == 4
    source = archive['entries'][3]['source']
    assert source['native_path'] is True and base64.b64decode(source['data']) == bytes(report['source_bytes'])
    assert source['mapping']['plane'] == 'xz'
    assert source['native_replacement']['annotation']['selection']['path'] == expected
