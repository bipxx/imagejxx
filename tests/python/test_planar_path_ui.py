"""Actual shared ImGui control points and native planar path preservation."""
import json
import math
import os
from pathlib import Path

import numpy as np
import pytest
import imagejxx as ij

COMMANDS = [0, 1.25, 1.125, 2, 3.25, .25, 5.25, 1.125,
            3, 6.25, 2, 6.25, 4, 5.25, 4, 1, 1.25, 4, 4,
            0, 2.25, 2, 1, 4.25, 2, 1, 4.25, 3, 1, 2.25, 3, 4]


@pytest.fixture(scope='module', params=['imgui_cpp', 'imgui_python'])
def case(request, tmp_path_factory):
    if request.param == 'imgui_cpp':
        report = Path(os.environ['IMAGEJXX_PLANAR_PATH_CPP_REPORT'])
        return json.loads(report.read_bytes()), report.parent
    from imagejxx import _desktop
    root = tmp_path_factory.mktemp('path-ui')
    editor = _desktop.Editor(str(root/'prefs.json'))
    return json.loads(editor._planar_path_ui_case(str(root/'capture'))), root/'capture'


def test_actual_native_paths_render_on_all_xyz_canvases(case):
    report, _ = case
    assert report['contract'] == 'p13b.planar-path.ui.001'
    assert report['state'] == 'observed' and report['error'] == ''
    assert len(report['initial_render']) == 3
    for plane, rendered in zip(['XY', 'XZ', 'YZ'], report['initial_render']):
        panel = rendered['planar_annotations']
        assert not panel['error']
        layers = [row for row in panel['layers'] if row['plane'] == plane]
        assert layers and layers[0]['geometry']['path'] == COMMANDS
        assert layers[0]['geometry']['path_version'] == 1
        assert layers[0]['geometry']['points'] == []
        assert layers[0]['emitted_vertices'] > 0 and layers[0]['emitted_indices'] > 0
        assert layers[0]['path_visual']['contour_edges'] > 0
        assert layers[0]['path_visual']['fill_spans'] > 0
        assert layers[0]['path_visual']['device_spans'] > 0
    before = report['states']['before']
    assert len(before['rows']) == 3
    assert [row['plane'] for row in before['rows']] == [0, 1, 2]
    assert before['undo'] == 0


def test_actual_control_drag_preserves_every_other_curve_scalar(case):
    report, _ = case
    states = report['states']
    before, edited = states['before'], states['edited']
    for index in [0, 2]:
        assert before['rows'][index] == edited['rows'][index]
    path = edited['rows'][1]['selection']['path']
    # ImGui uses device-pixel mouse positions. Derive the delivered image point
    # from the actual input and captured transform, independently of output geometry.
    drag = report['drags'][0]
    expected = [(math.floor(drag['end_screen'][i])-drag['origin'][i])/drag['scale'][i]
                for i in range(2)]
    assert path[4:6] == pytest.approx(expected, abs=1e-6)
    assert path[:4] == COMMANDS[:4] and path[6:] == COMMANDS[6:]
    assert edited['undo'] == 1
    assert edited['rows'][1]['selection']['attributes'] == before['rows'][1]['selection']['attributes']
    assert edited['rows'][1]['attributes'] == before['rows'][1]['attributes']
    assert states['cancelled'] == edited
    assert report['drafts'][0]['active']
    assert report['drafts'][1]['active']


def test_actual_cancel_history_cache_and_source_pixels(case):
    report, _ = case
    states = report['states']
    assert states['undone']['rows'] == states['edited']['rows']
    assert states['undone']['undo'] == 1
    assert states['redone']['rows'] == states['numeric']['rows']
    assert states['redone']['undo'] == 2
    expected = (np.arange(6)[:, None, None]*100 +
                np.arange(10)[None, :, None]*10 + np.arange(14)[None, None, :])
    for phase, state in states.items():
        np.testing.assert_array_equal(np.array(state['pixels']).reshape(6, 10, 14), expected)
        assert state['workflow'] == []
    for before, after in zip(report['idle_before'], report['idle_after']):
        a, b = before['planar_annotations'], after['planar_annotations']
        assert a['visual_builds'] == b['visual_builds']
        assert not a['active'] and not b['active']


def test_actual_save_reopen_preserves_continuous_native_session(case):
    report, directory = case
    states = report['states']
    assert states['reopened']['rows'] == states['redone']['rows']
    blob = (directory/'curves.ijx').read_bytes()
    assert blob[:10] == b'IMAGEJXX3\n'
    capture = ij.decode_session(blob)
    rows = ij.planar_annotations(capture.documents[0].snapshot())
    assert all(row.selection.is_path for row in rows)
    assert rows[1].selection.path.commands[4:6] == [3.7500000000000004, .8750000000000001]
    assert rows[0].selection.path.commands == rows[2].selection.path.commands == COMMANDS


def test_actual_numeric_controls_preserve_binary64_and_refuse_domain_overflow(case):
    report, _ = case
    states = report['states']
    path = states['numeric']['rows'][1]['selection']['path']
    assert path[4:6] == [3.7500000000000004, .8750000000000001]
    assert path[:4] == COMMANDS[:4] and path[6:] == COMMANDS[6:]
    assert states['numeric']['undo'] == 2
    assert states['invalid'] == states['numeric'] == states['restored']
    assert report['invalid_error'] and report['restored_error'] == ''
    names = [control['name'] for control in report['controls']]
    assert 'vertex_index' in names and 'vertex_u' in names and 'vertex_v' in names


@pytest.mark.parametrize('plane', ['xy', 'xz', 'yz'])
def test_native_owner_entry_supports_planar_paths_without_changing_legacy_selection(plane, tmp_path):
    from imagejxx.ui.app import Editor
    from imagejxx.ui.persistence import Preferences
    owner = ij.Document(ij.Dataset(np.zeros((6, 10, 14), np.uint16), axes='ZYX'))
    area = ij.Selection.from_path(ij.ImageJCompositePath(COMMANDS))
    annotation = ij.PlanarAnnotation('native', area, plane, 0)
    owner.commit(ij.prepare_store_planar_annotation(owner.snapshot(), annotation))
    editor = Editor(preferences=Preferences(tmp_path/'preferences.json'))
    live = editor.add_native_document(owner)
    assert ij.planar_annotations(live.native_snapshot())[0].selection == area
    assert live.native_snapshot().selection is None and live.native_snapshot().rois == []
