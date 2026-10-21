"""Whole-dataset scientific conversion menu, native workflow and recording."""
import json
import os
from pathlib import Path

import imagejxx as ij
import numpy as np
import pytest
from imagejxx import _desktop
from imagejxx.ui.app import Editor
from test_scientific_process_recording import state, click
from test_scientific_binary_review import attach, apply_binary, parameters

REFERENCE = json.loads((Path(__file__).parent.parent / 'fixtures/conversion-imagej154r.json').read_bytes())
CASES = [r for r in REFERENCE['cases'] if r['operation'] == 'convert_image'
         and r['source_dtype'] == 'uint16' and r['source_samples'] == 1 and r['pattern'] == 0
         and r['dtype'] in ('uint8', 'float32') and r['parameters']['range_policy'] in ('data', 'dtype')]
assert len(CASES) == 8


def open_conversion(editor, **options):
    editor.action('show_operation', operation='convert_image', parameters=options)
    return click(editor, state(editor), 'confirm')


def target(stop=2):
    return ij.TargetRequest(ij.TargetScope.RANGES, t=ij.AxisRange(0, 2), c=ij.AxisRange(0, 2),
                            z=ij.AxisRange(0, stop), roi=ij.RoiPolicy.WHOLE_PLANE)


@pytest.mark.parametrize('case', CASES)
def test_captured_preferences_convert_every_plane_against_pinned_reference(tmp_path, case):
    editor = Editor(preferences=tmp_path / 'prefs.json'); editor.recording = True
    image = np.array(case['source'], np.uint16).reshape(case['shape'])
    pixels = np.broadcast_to(image, (2, 2, 2, *image.shape)).copy()
    owner, live, reads = attach(editor, pixels); live.set_view(t=1, c=1, crosshair=[0, 0, 1])
    opts = case['parameters']
    editor.update_settings({'conversion_' + k: v for k, v in opts.items() if k not in ('range_min', 'range_max')}, persist=False)
    before = reads.counts().plane_reads
    editor.action('show_operation', operation='convert_image', parameters=dict(
        dtype=case['dtype'], range_min=opts['range_min'], range_max=opts['range_max']))
    assert reads.counts().plane_reads == before
    panel = click(editor, state(editor), 'confirm')
    editor.update_settings(dict(conversion_scale=not opts['scale'], conversion_range_policy='display'), persist=False)
    apply_binary(editor, panel)
    expected = np.broadcast_to(np.array(case['pixels'], dtype=case['dtype']).reshape(image.shape), pixels.shape)
    np.testing.assert_array_equal(owner.snapshot().dataset.numpy('TCZYX'), expected)
    assert parameters(editor.workflow[-1]) == dict(dtype=case['dtype'], **opts)
    row = ij.NativeWorkflowProgram.from_json(json.dumps(editor.workflow[-1])).record()['steps'][0]
    for axis in ('t', 'c', 'z'): assert row['target'][axis] == dict(start=0, stop=2, step=1)
    assert row['target']['roi'] == 0 and owner.undo_steps == 1
    editor.action('cancel_native_workflow')
    editor.action('undo'); np.testing.assert_array_equal(owner.snapshot().dataset.numpy('TCZYX'), pixels)
    editor.action('redo'); np.testing.assert_array_equal(owner.snapshot().dataset.numpy('TCZYX'), expected)


def test_explicit_weighted_rgb_options_override_preferences(tmp_path):
    editor = Editor(preferences=tmp_path / 'prefs.json'); editor.recording = True
    pixels = np.zeros((2, 2, 2, 2, 3, 3), np.uint8); pixels[:] = [20, 40, 80]
    owner, _, _ = attach(editor, pixels, 'TCZYXS')
    opts = dict(dtype='float32', weighted_rgb=True, red_weight=.5, green_weight=.25, blue_weight=.25, scale=False)
    apply_binary(editor, open_conversion(editor, **opts))
    np.testing.assert_array_equal(owner.snapshot().dataset.numpy('TCZYX'), np.full((2, 2, 2, 2, 3), 40, np.float32))
    captured = parameters(editor.workflow[-1])
    for k, v in opts.items(): assert captured[k] == v


def test_actual_parameter_form_can_change_output_type(tmp_path):
    editor = Editor(preferences=tmp_path / 'prefs.json'); editor.recording = True
    pixels = np.arange(48, dtype=np.uint16).reshape(2, 2, 2, 2, 3)
    owner, _, _ = attach(editor, pixels)
    click(editor, open_conversion(editor), 'edit_parameters'); editor.wait()
    panel = state(editor); assert panel['editor']['visible']
    combo_point = panel['editor']['controls']['dtype']['point']
    panel = click(editor, panel['editor'], 'dtype')
    choice_point = panel['editor']['controls']['dtype.choice.2']['point']
    # render_headless owns one ImGui context per call. Open and select within
    # that same input session; the native parameter draft outlives the context.
    events = []
    for point in (combo_point, choice_point):
        events.extend([dict(type='position', space='screen', point=point, frames=2),
                       dict(type='button', name='left', down=True),
                       dict(type='button', name='left', down=False, frames=2)])
    panel = editor.render_headless(frames=3, events=events)['native_workflow']
    assert next(f for f in panel['editor']['fields'] if f['name'] == 'dtype')['text'] == 'float32'
    panel = click(editor, panel['editor'], 'keep_parameters')
    apply_binary(editor, click(editor, panel, 'confirm'))
    actual = owner.snapshot().dataset.numpy('TCZYX')
    assert actual.dtype == np.float32 and parameters(editor.workflow[-1])['dtype'] == 'float32'
    np.testing.assert_array_equal(actual, pixels)


@pytest.mark.parametrize('mode', ['legacy', 'channel', 'unsigned_bits'])
def test_display_range_is_captured_without_processing_reads(tmp_path, mode):
    editor = Editor(preferences=tmp_path / 'prefs.json'); editor.recording = True
    pixels = (np.arange(128, dtype=np.uint16) * 10).reshape(2, 2, 2, 2, 8)
    owner, live, reads = attach(editor, pixels); live.set_view(t=1, c=1, crosshair=[0, 0, 1])
    display = owner.snapshot().display
    if mode == 'legacy': display.properties = dict(display_range=[0., 4095.])
    elif mode == 'channel':
        channels = ij.default_channel_display(owner.snapshot().dataset, 1, 1)
        channels['channels'][0]['range'] = [0., 255.]; channels['channels'][1]['range'] = [0., 4095.]
        display.properties = dict(channel_display=channels)
    owner.set_display(display)
    original = owner.snapshot().display.properties
    editor.update_settings(dict(conversion_scale=True, conversion_range_policy='display', conversion_unsigned16_range='12'), persist=False)
    count = reads.counts().plane_reads
    editor.action('show_operation', operation='convert_image')
    assert reads.counts().plane_reads == count
    apply_binary(editor, click(editor, state(editor), 'confirm'))
    np.testing.assert_array_equal(owner.snapshot().dataset.numpy('TCZYX'), (pixels.astype(np.uint32) + 8) // 16)
    assert owner.snapshot().display.properties == {}
    opts = parameters(editor.workflow[-1]); assert opts['range_policy'] == 'explicit' and opts['range_min'] == 0 and opts['range_max'] == 4095
    editor.action('cancel_native_workflow'); editor.action('undo')
    assert owner.snapshot().display.properties == original


@pytest.mark.parametrize('opts', [dict(dtype=['uint8']), dict(red_weight=True), dict(scale=0), dict(unknown=1), dict(range_policy='invalid')])
def test_invalid_options_refuse_without_processing_reads(tmp_path, opts):
    editor = Editor(preferences=tmp_path / 'prefs.json')
    owner, _, reads = attach(editor, np.zeros((2, 2, 2, 2, 3), np.uint16))
    count = reads.counts().plane_reads
    with pytest.raises(ValueError): editor.action('show_operation', operation='convert_image', parameters=opts)
    assert reads.counts().plane_reads == count and owner.undo_steps == 0


@pytest.mark.parametrize('kind', ['current', 'selection', 'partial'])
def test_portable_conversion_refuses_incomplete_targets_before_pixels(kind):
    reads = ij.DatasetReadObserver()
    owner = ij.Document(ij.Dataset(np.zeros((2, 2, 2, 2, 3), np.uint16), axes='TCZYX').observe_reads(reads))
    request = target(1 if kind == 'partial' else 2)
    if kind == 'current': request.scope = ij.TargetScope.CURRENT_PLANE
    if kind == 'selection': request.roi = ij.RoiPolicy.ACTIVE_SELECTION
    with pytest.raises(ValueError):
        program = ij.NativeWorkflowProgram([ij.NativeWorkflowOperation('convert_image', {}, request)])
        ij.prepare_native_workflow(owner.snapshot(), program.steps)
    assert reads.counts().plane_reads == 0 and owner.undo_steps == 0


def test_output_budget_refuses_at_opening_before_processing_reads(tmp_path):
    editor = Editor(preferences=tmp_path / 'prefs.json')
    owner, _, reads = attach(editor, np.zeros((2, 2, 2, 64, 64), np.uint16))
    editor.update_settings(dict(processing_memory_mb=1), persist=False)
    count = reads.counts().plane_reads
    with pytest.raises(ValueError, match='limit'): editor.action('show_operation', operation='convert_image')
    assert reads.counts().plane_reads == count and owner.undo_steps == 0


def test_stale_display_apply_refuses_without_recording(tmp_path):
    editor = Editor(preferences=tmp_path / 'prefs.json'); editor.recording = True
    pixels = np.arange(48, dtype=np.uint16).reshape(2, 2, 2, 2, 3)
    owner, _, _ = attach(editor, pixels)
    click(editor, open_conversion(editor), 'prepare'); editor.wait()
    display = owner.snapshot().display; display.t = 1; owner.set_display(display)
    panel = click(editor, state(editor), 'apply')
    assert panel['error'] and owner.undo_steps == 0 and not editor.workflow
    np.testing.assert_array_equal(owner.snapshot().dataset.numpy('TCZYX'), pixels)


def test_preview_cancel_does_not_change_owner_or_recorder(tmp_path):
    editor = Editor(preferences=tmp_path / 'prefs.json'); editor.recording = True
    pixels = np.arange(48, dtype=np.uint16).reshape(2, 2, 2, 2, 3)
    owner, _, _ = attach(editor, pixels)
    click(editor, open_conversion(editor), 'prepare_preview'); editor.wait()
    panel = state(editor); assert panel['ready'] and panel['preview_only']
    click(editor, panel, 'cancel')
    np.testing.assert_array_equal(owner.snapshot().dataset.numpy('TCZYX'), pixels)
    assert owner.undo_steps == 0 and not editor.workflow


def test_recording_off_conversion_does_not_append(tmp_path):
    editor = Editor(preferences=tmp_path / 'prefs.json')
    owner, _, _ = attach(editor, np.full((2, 2, 2, 2, 3), 1000, np.uint16))
    apply_binary(editor, open_conversion(editor, scale=False))
    assert owner.undo_steps == 1 and not editor.workflow


def test_successful_noop_records_without_pixel_history(tmp_path):
    editor = Editor(preferences=tmp_path / 'prefs.json'); editor.recording = True
    owner, _, _ = attach(editor, np.full((2, 2, 2, 2, 3), 7, np.uint8))
    apply_binary(editor, open_conversion(editor))
    assert owner.undo_steps == 0 and len(editor.workflow) == 1


def test_portable_conversion_then_filter_replay_pairs_display_history():
    pixels = np.arange(48, dtype=np.uint16).reshape(2, 2, 2, 2, 3)
    owner = ij.Document(ij.Dataset(pixels, axes='TCZYX'))
    display = owner.snapshot().display; display.properties = dict(display_range=[0., 4095.]); owner.set_display(display)
    conversion = ij.NativeWorkflowOperation('convert_image', dict(dtype='float32', scale=False), target())
    addition = ij.NativeWorkflowOperation('add', dict(value=2), target())
    program = ij.NativeWorkflowProgram.from_json(ij.NativeWorkflowProgram([conversion, addition]).json())
    plan = ij.prepare_native_workflow(owner.snapshot(), program.steps)
    owner.commit(plan.begin_edit())
    np.testing.assert_array_equal(owner.snapshot().dataset.numpy('TCZYX'), pixels + 2)
    assert owner.snapshot().display.properties == {} and owner.undo_steps == 1
    owner.undo(); assert owner.snapshot().display.properties == display.properties


@pytest.mark.parametrize('dtype', [np.uint16, np.float32])
def test_unsupported_color_source_refuses_before_pixels(dtype):
    reads = ij.DatasetReadObserver()
    owner = ij.Document(ij.Dataset(np.zeros((2, 2, 2, 2, 3, 3), dtype), axes='TCZYXS').observe_reads(reads))
    step = ij.NativeWorkflowOperation('convert_image', {}, target())
    with pytest.raises(ValueError): ij.prepare_native_workflow(owner.snapshot(), [step])
    assert reads.counts().plane_reads == 0


def test_other_sample_changing_operations_remain_guarded():
    with pytest.raises(ValueError): ij.NativeWorkflowProgram([ij.NativeWorkflowOperation('grayscale', {}, target())])


def test_actual_conversion_menu_in_both_hosts(tmp_path):
    cpp = json.loads(Path(os.environ['IMAGEJXX_SCIENTIFIC_PROCESS_CPP_REPORT']).read_bytes())
    window = _desktop.Editor(str(tmp_path / 'prefs.json'))
    python = json.loads(window._scientific_process_ui_case(str(tmp_path / 'capture001')))
    for report in (cpp, python):
        row = report['conversion']; before = np.array(report['invert16']['redone'])
        after = np.floor(before / 16 + .5)
        for field, expected in [('before', before), ('after', after), ('undone', before), ('redone', after)]:
            np.testing.assert_array_equal(row[field], expected)
        assert row['display_after'] == row['display_redone'] == {}
        assert row['display_undone'] == row['display_before']
        assert row['undo_steps'] == 5 and row['workflow'][:-1] == row['recorder_before']
        opts = parameters(row['workflow'][-1]); assert opts['scale'] and opts['range_policy'] == 'explicit' and opts['range_max'] == 4095
