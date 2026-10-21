"""Shared native viewer: real widget input plus independent source/RGB checks."""
import json
import os
import struct
from pathlib import Path

import imagejxx as ij
import numpy as np
import pytest

IEEE = ij.FloatingSamplePolicy.PRESERVE_IEEE_FLOAT32
CASES = ['IEEE01', 'IEEE02', 'IEEE03', 'IEEE04']
CAPTURES = ['ordinary.auto', 'linked.auto', 'linked.stable', 'channels.auto',
            'channels.draft', 'channels.manual', 'channels.reset', 'channels.max',
            'channels.min', 'session.reopened']


def expected_words(case):
    t, c, z, y, x = np.indices((2, 2, 2, 4, 8))
    i = t*3 + c*5 + z*2 + y*8 + x
    if case == 'IEEE02':
        return np.array([0x7f800001, 0xff812345, 0x7fc12345, 0xffc00001], np.uint32)[i % 4]
    if case == 'IEEE03':
        return np.where(i % 2, np.uint32(0x7f800000), np.uint32(0xff800000))
    if case == 'IEEE04':
        result = np.where(i % 2, np.uint32(0x3f800000), np.uint32(0))
        result[:, :, :, 0, 0] = 0x7f800000
        result[:, :, :, 0, 1] = 0xff800000
        return result
    return np.array([0xff800000, 0x7f800000, 0x7f800001, 0xffc12345,
                     0x80000000, 0, 0x3f800000, 0x3f000000], np.uint32)[i % 8]


@pytest.fixture(scope='module')
def observations(tmp_path_factory):
    from imagejxx.ui import Editor
    from imagejxx.ui.persistence import Preferences
    cpp_root = Path(os.environ['IMAGEJXX_IEEE_CPP_DIR'])
    parent = tmp_path_factory.mktemp('ieee-input')
    results = {}
    for case in CASES:
        editor = Editor(preferences=Preferences(parent / f'{case}-preferences.json'))
        reply = editor._editor._ieee_viewing_ui_case(case, 'imgui_python', str(parent / case))
        results['python', case] = json.loads(reply)
        results['cpp', case] = json.loads((cpp_root / case / 'report.json').read_bytes())
    return results


def selected(volume, plane, coordinate):
    if plane == 'XY':
        return volume[coordinate]
    if plane == 'XZ':
        return volume[:, coordinate, :]
    return volume[:, :, coordinate]


def mapped(word, bounds):
    if (word & 0x7fffffff) > 0x7f800000 or isinstance(bounds, dict):
        return 0
    low, high = (float(np.float32(v)) for v in bounds)
    if word == 0xff800000:
        return 0
    if word == 0x7f800000:
        return 255
    value = struct.unpack('<f', struct.pack('<I', word))[0]
    if high == low:
        return 255 if value > low else 0
    return min(255, max(0, int(max(0, value-low)*255/(high-low)+0.5)))


def table(lut):
    if 'table' in lut:
        return np.array(lut['table'], np.uint16)
    active = {'Grays': (1, 1, 1), 'White': (1, 1, 1), 'Red': (1, 0, 0),
              'Green': (0, 1, 0), 'Blue': (0, 0, 1), 'Cyan': (0, 1, 1),
              'Magenta': (1, 0, 1), 'Yellow': (1, 1, 0)}[lut['name']]
    return np.array([[i*r, i*g, i*b] for i in range(256) for r, g, b in [active]], np.uint16)


@pytest.mark.parametrize('origin', ['cpp', 'python'])
@pytest.mark.parametrize('case', CASES)
@pytest.mark.parametrize('capture', CAPTURES)
def test_actual_view_bytes(observations, origin, case, capture):
    report = observations[origin, case]
    assert report['context'] == ('imgui_cpp' if origin == 'cpp' else 'imgui_python')
    root = report['captures'][capture]
    views = [dict(root['ordinary_pixels'], plane='XY', coordinate=root['ordinary_pixels']['current_tcz'][2])] if capture == 'ordinary.auto' else root['orthogonal_planes']
    words = expected_words(case)
    state = None if capture.startswith(('ordinary.', 'linked.')) else root['channel_display_panel']['state']
    for view in views:
        t, c, z = view['current_tcz'] if 'current_tcz' in view else view['range_tcz']
        plane = view['plane']
        coordinate = view['coordinate']
        source = selected(words[t, c], plane, coordinate)
        actual = view['readback']['source']
        assert actual['encoding'] == 'IEEE754 Float32LE'
        assert bytes(actual['bytes']) == source.astype('<u4').tobytes()
        channels = range(2) if state and state['mode'] == 'composite' else [c]
        colored = []
        for channel in channels:
            if state and state['mode'] == 'composite' and not state['channels'][channel]['visible']:
                continue
            samples = selected(words[t, channel], plane, coordinate)
            bounds = state['channels'][channel]['range'] if state else view['range']
            indices = np.array([mapped(int(w), bounds) for w in samples.ravel()], np.uint8).reshape(samples.shape)
            lut = table(state['channels'][channel]['lut']) if state else table({'name': 'Grays'})
            if state and state['mode'] == 'grayscale':
                lut = table({'name': 'Grays'})
            colored.append(lut[indices])
        assert colored
        projection = state['projection'] if state and state['mode'] == 'composite' else 'sum'
        expected = np.minimum(np.sum(colored, axis=0), 255) if projection == 'sum' else np.max(colored, axis=0) if projection == 'max' else np.min(colored, axis=0)
        rendered = view['readback']['rendered_rgb']
        assert rendered['dtype'] == 'uint8'
        assert rendered['pixels'] == expected.astype(np.uint8).ravel().tolist()
        if capture.startswith(('ordinary.', 'linked.')):
            assert view['range'] == {'float32_bits': [0x7fc00000, 0x7fc00000] if case == 'IEEE02' else [0xff800000, 0x7f800000]}


@pytest.mark.parametrize('origin', ['cpp', 'python'])
@pytest.mark.parametrize('case', CASES)
def test_input_cache_history_and_session(observations, origin, case):
    report = observations[origin, case]
    expected = expected_words(case).ravel().tolist()
    assert report['source_words'] == report['final_words'] == report['reopened_words'] == expected
    captures = report['captures']
    for before, after in zip(captures['linked.auto']['orthogonal_planes'], captures['linked.stable']['orthogonal_planes']):
        assert before['builds'] == after['builds']
    draft = captures['channels.draft']['channel_display_panel']
    assert draft['minimum_text'] == '0' and draft['maximum_text'] == '1' and draft['draft_valid']
    assert captures['channels.manual']['channel_display_panel']['state']['channels'][1]['range'] == [0, 1]
    for name in CAPTURES[3:]:
        assert captures[name]['channel_display_panel']['state']['version'] == 3
    assert captures['channels.max']['channel_display_panel']['state']['projection'] == 'max'
    assert captures['channels.min']['channel_display_panel']['state']['projection'] == 'min'
    assert len(report['workflow']) == 4
    assert all(record['version'] == 3 for record in report['workflow'])
    assert any(event['type'] == 'text' for frame in report['frames'] for event in frame['events'])
    assert any(event['type'] == 'button' for frame in report['frames'] for event in frame['events'])
    json.dumps(report, allow_nan=False)


@pytest.mark.parametrize('words', [[0, 0], [0, 0x3f800000], [0xff800000, 0x7f800000],
                                  [0x7fc00000, 0x7fc00000]])
def test_computed_range_override(words):
    options = ij.Float32DisplayOptions()
    options.computed_range_bits = words
    source = np.array([[.25, .5]], np.float32)
    actual = ij.render_float32_display(source, options, sample_policy=IEEE)
    expected = [255, 255] if words == [0, 0] else [64, 128] if words == [0, 0x3f800000] else [0, 0]
    assert actual.rgb[:, :, 0].ravel().tolist() == expected
    assert actual.range_bits == words


@pytest.mark.parametrize('words', [[0x7f800001, 0x7fc00000], [0x7fc12345, 0x7fc00000],
                                  [0x7fc00000, 0], [0x7f800000, 0xff800000],
                                  [False, 0], [0., 1], [-1, 1], [2**32, 0]])
def test_computed_bound_validation(words):
    with pytest.raises((ValueError, TypeError, OverflowError)):
        options = ij.Float32DisplayOptions()
        options.computed_range_bits = words
        ij.render_float32_display(np.zeros((2, 3), np.float32), options, sample_policy=IEEE)


@pytest.mark.parametrize('plane', ['xy', 'xz', 'yz'])
def test_python_plane_readback_preserves_signaling(plane, tmp_path):
    from imagejxx.ui import Editor
    from imagejxx.ui.persistence import Preferences
    data_words = expected_words('IEEE01')
    owner = ij.Document(ij.Dataset(data_words.view(np.float32), axes='TCZYX', sample_policy=IEEE))
    editor = Editor(preferences=Preferences(tmp_path / 'preferences.json'))
    editor.add_native_document(owner)
    view = owner.snapshot().display
    view.t, view.c, view.crosshair = 1, 1, [3, 2, 1]
    editor._editor.set_native_display(view)
    result = editor._editor.plane(plane, {'xy': 1, 'xz': 2, 'yz': 3}[plane])
    assert result.tobytes() == selected(data_words[1, 1], plane.upper(), {'xy': 1, 'xz': 2, 'yz': 3}[plane]).astype('<u4').tobytes()


@pytest.mark.parametrize('case', CASES)
def test_detached_python_render_uses_native_profile(case):
    from imagejxx.ui.rendering import display_pixels
    words = expected_words(case)[1, 1, 1]
    result = display_pixels(words.view(np.float32), sample_policy=IEEE)
    assert result.dtype == np.uint8 and not np.any(result)


@pytest.mark.parametrize('case', CASES)
def test_workspace_file_opens_through_editor(case, tmp_path):
    from imagejxx.ui import Editor
    from imagejxx.ui.persistence import Preferences
    editor = Editor(preferences=Preferences(tmp_path / 'preferences.json'))
    path = Path(os.environ['IMAGEJXX_IEEE_CPP_DIR']) / case / 'viewing.ijx'
    editor.open_file(path)
    snapshot = editor.live_document().native_snapshot()
    assert snapshot.dataset.sample_policy == IEEE
    assert snapshot.dataset.numpy('TCZYX').tobytes() == expected_words(case).astype('<u4').tobytes()
    assert ij.channel_display_state(snapshot.display, snapshot.dataset)['version'] == 3
    editor.action('show_channel_display')
    report = editor.render_headless(frames=3)
    assert len(report['orthogonal_planes']) == 3
    assert all(view['readback']['available'] for view in report['orthogonal_planes'])


@pytest.mark.parametrize('stack', [False, True], ids=['plane', 'stack'])
def test_detached_ieee_workspace_roundtrip(stack, tmp_path):
    from imagejxx.ui import Editor
    from imagejxx.ui.model import Document
    from imagejxx.ui.persistence import Preferences
    words = expected_words('IEEE01')[1, 1]
    if not stack:
        words = words[1]
    document = Document(words.view(np.float32), is_stack=stack, sample_policy=IEEE)
    editor = Editor(preferences=Preferences(tmp_path / 'preferences.json'))
    editor.add_document(document)
    saved = tmp_path / 'ieee-workspace.ijx'
    editor.action('save_workspace', path=str(saved))
    reopened = Editor(preferences=Preferences(tmp_path / 'reopened-preferences.json'))
    reopened.open_file(saved)
    snapshot = reopened.live_document().native_snapshot()
    assert snapshot.dataset.sample_policy == IEEE
    assert snapshot.dataset.numpy('ZYX' if stack else 'YX').tobytes() == words.astype('<u4').tobytes()
    assert reopened.document_snapshot().data.tobytes() == words.astype('<u4').tobytes()
    assert document.data.tobytes() == words.astype('<u4').tobytes()
