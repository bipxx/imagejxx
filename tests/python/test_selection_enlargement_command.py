import copy
import json
from pathlib import Path
import numpy as np
import pytest
import imagejxx as ij
from imagejxx.ui.app import Editor
from test_selection_enlargement import geometry, rows, FIXTURE


def source(kind='u16', shape=(2, 2, 2, 40, 48, 1), spacing=.5):
    dtype = np.uint8 if kind in ('u8', 'rgb') else np.float32 if kind in ('float', 'quiet', 'ieee') else np.uint16
    if kind == 'rgb':
        shape = (*shape[:-1], 3)
    pixels = np.full(shape, 30, dtype)
    policy = ij.FloatingSamplePolicy.FINITE_ONLY
    if kind in ('quiet', 'ieee'):
        policy = ij.FloatingSamplePolicy.PRESERVE_QUIET_NAN_FLOAT32 if kind == 'quiet' else ij.FloatingSamplePolicy.PRESERVE_IEEE_FLOAT32
        words = [0x7fc12345, 0x80000000, 0x00000001] if kind == 'quiet' else [0x7fc12345, 0x7f800000, 0xff800000, 0x80000000, 0x00000001]
        pixels.view(np.uint32).flat[:len(words)] = words
    calibration = ij.SpatialCalibration(spacing=(spacing, 1.5, 2), origin=(11, -7, 19), unit='um')
    reads = ij.DatasetReadObserver()
    owner = ij.Document(ij.Dataset(pixels, axes='TCZYXS', calibration=calibration, sample_policy=policy).observe_reads(reads))
    display = owner.snapshot().display
    display.t = display.c = 1
    display.crosshair = [2, 2, 1]
    owner.set_display(display)
    return owner, reads, pixels


@pytest.mark.parametrize('unit', ['PIXELS', 'CALIBRATED'])
@pytest.mark.parametrize('row', FIXTURE['cases'], ids=lambda r: f"kind{r['kind']}-distance{r['distance']}")
def test_commands_match_original_imagej_rows_through_record_and_publication(row, unit):
    owner, reads, pixels = source()
    original = geometry(row)
    owner.select(original)
    before = owner.snapshot()
    distance = row['distance']*(.5 if unit == 'CALIBRATED' else 1)
    request = ij.SelectionEnlargementRequest(distance, getattr(ij.SelectionEnlargementUnits, unit))
    estimate = ij.estimate_selection_enlargement_command(before, request)
    command = ij.resolve_selection_enlargement_command(before, request)
    assert estimate.working_bytes == command.estimate.working_bytes and command.estimate.pixel_distance == row['distance']
    record = json.loads(json.dumps(command.record(), allow_nan=False))
    assert record['schema'] == 'imagejxx.selection-enlargement-command' and record['version'] == 1
    assert record['position'] == {'t': 1, 'c': 1, 'z': 1} and record['pixel_width'] == .5 and record['unit'] == 'um'
    ij.validate_selection_enlargement_command_record(record)
    replay = ij.selection_enlargement_command_from_record(before, record)
    np.testing.assert_array_equal(replay.result.mask((40, 48)), rows(row['after']))
    owner.commit(ij.prepare_selection_enlargement(replay))
    after = owner.snapshot()
    assert after.dataset.shares_pixels(before.dataset) and after.dataset.sample_policy == before.dataset.sample_policy
    assert after.display.t == after.display.c == after.display.crosshair[2] == 1
    assert not after.dirty and owner.undo_steps == owner.redo_steps == 0
    if replay.changes_selection:
        assert after.previous_selection == original
    else:
        assert after.shares_data(before)
    assert reads.counts().plane_reads == 0
    assert after.dataset.numpy('TCZYXS').tobytes() == pixels.tobytes()


@pytest.mark.parametrize('kind', ['u8', 'u16', 'rgb', 'float', 'quiet', 'ieee'])
def test_desktop_recording_replay_rebind_and_session_keep_source_bits(kind, tmp_path):
    owner, reads, pixels = source(kind)
    owner.select(ij.Selection('rectangle', [(4, 5), (24, 22)]))
    editor = Editor(preferences=tmp_path/'prefs.json')
    editor._editor.add_virtual_document(owner)
    editor.recording = True
    before = owner.snapshot()
    editor.action('enlarge_selection', distance=1., units='calibrated')
    assert len(editor.workflow) == 1
    record = editor.workflow[0]
    assert owner.snapshot().selection.points == [[2., 3.], [26., 24.]]
    editor.action('replay_selection_enlargement', record=record)
    assert len(editor.workflow) == 1  # Replaying the same geometry is an exact no-op.
    editor.action('replay_selection_enlargement', record=record, rebind={'distance': 0., 'units': 'pixels'})
    assert len(editor.workflow) == 1
    owner.select(ij.Selection('rectangle', [(6, 7), (20, 18)]))
    editor.action('replay_selection_enlargement', record=record, rebind={'distance': 1., 'units': 'pixels'})
    assert len(editor.workflow) == 2 and owner.snapshot().selection.points == [[5., 6.], [21., 19.]]
    assert owner.snapshot().dataset.shares_pixels(before.dataset) and reads.counts().plane_reads == 0
    assert owner.snapshot().dataset.numpy('TCZYXS').tobytes() == pixels.tobytes()
    capture = ij.SessionCapture([owner], workflow=editor.workflow)
    reopened = ij.decode_session(ij.encode_session(capture))
    assert reopened.workflow == editor.workflow and reopened.documents[0].snapshot().selection == owner.snapshot().selection
    assert reopened.documents[0].snapshot().dataset.numpy('TCZYXS').tobytes() == pixels.tobytes()
    with pytest.raises(ValueError, match='owner'):
        ij.selection_enlargement_command_from_record(reopened.documents[0].snapshot(), record)
    rebound = ij.rebind_selection_enlargement_command(reopened.documents[0].snapshot(), record, ij.SelectionEnlargementRequest(0))
    assert rebound.record()['source']['id'] == reopened.documents[0].id


@pytest.mark.parametrize('mutation', ['distance', 'result', 'base', 'version', 'units', 'source', 'position'])
def test_malformed_original_refuses_before_rebind(mutation):
    owner, reads, _ = source()
    owner.select(ij.Selection('rectangle', [(4, 5), (24, 22)]))
    record = ij.resolve_selection_enlargement_command(owner.snapshot(), ij.SelectionEnlargementRequest(1)).record()
    if mutation == 'distance': record['distance'] = 2.
    elif mutation == 'result': record['result']['width'] = 3.
    elif mutation == 'base': record['base']['points'][0] = [7., 8.]
    elif mutation == 'version': record['version'] = True
    elif mutation == 'units': record['units'] = 'world'
    elif mutation == 'source': record['source']['unexpected'] = 0
    else: record['position']['z'] = -1
    before = owner.snapshot()
    with pytest.raises(ValueError):
        ij.rebind_selection_enlargement_command(before, record, ij.SelectionEnlargementRequest(0))
    assert owner.snapshot().shares_data(before) and reads.counts().plane_reads == 0


def test_resources_callbacks_freezing_stale_guards_and_explicit_rebind():
    owner, reads, _ = source()
    owner.select(ij.Selection('rectangle', [(4, 5), (24, 22)]))
    before = owner.snapshot()
    request = ij.SelectionEnlargementRequest(1.)
    command = ij.resolve_selection_enlargement_command(before, request)
    limits = ij.SelectionEnlargementCommandLimits()
    limits.max_working_bytes = command.estimate.working_bytes-1
    calls = []
    with pytest.raises(ValueError, match='working memory'):
        ij.resolve_selection_enlargement_command(before, request, limits, checkpoint=lambda: calls.append('forbidden'))
    assert calls == []
    limits.max_working_bytes += 1
    def mutate(a, b):
        calls.append((a, b))
        request.distance = 100
        limits.max_working_bytes = 1
    frozen = ij.resolve_selection_enlargement_command(before, request, limits, progress=mutate)
    assert calls == [(0, 1), (1, 1)] and frozen.request.distance == 1.
    with pytest.raises(ValueError):
        ij.prepare_selection_enlargement(command, execution_max_working_bytes=command.estimate.working_bytes-1)
    edit = ij.prepare_selection_enlargement(command)
    display = owner.snapshot().display
    display.t = 0
    owner.set_display(display)
    with pytest.raises(RuntimeError): owner.commit(edit)
    with pytest.raises(ValueError, match='T/C/Z'):
        ij.selection_enlargement_command_from_record(owner.snapshot(), command.record())
    rebound = ij.rebind_selection_enlargement_command(owner.snapshot(), command.record(), ij.SelectionEnlargementRequest(2))
    assert rebound.record()['position']['t'] == 0
    assert reads.counts().plane_reads == 0


@pytest.mark.parametrize('refusal', ['missing_area', 'target_budget'])
def test_original_equation_is_checked_before_rejected_rebind_target(refusal):
    original, _, _ = source()
    original.select(ij.Selection('rectangle', [(4, 5), (24, 22)]))
    record = ij.resolve_selection_enlargement_command(original.snapshot(), ij.SelectionEnlargementRequest(1)).record()
    record = copy.deepcopy(record)
    record['distance'] = 2.
    target, reads, _ = source()
    if refusal == 'target_budget':
        target.select(ij.Selection('rectangle', [(4, 5), (24, 22)]))
        edit = target.begin_edit()
        edit.set_property('large_argument', 'x'*(2*1024*1024))
        target.commit(edit)
    limits = ij.SelectionEnlargementCommandLimits()
    limits.max_working_bytes = 1024*1024
    before = target.snapshot()
    calls = []
    with pytest.raises(ValueError, match='original operation'):
        ij.rebind_selection_enlargement_command(before, record, ij.SelectionEnlargementRequest(1), limits,
                                              checkpoint=lambda: calls.append('forbidden'))
    assert calls == [] and target.snapshot().shares_data(before) and reads.counts().plane_reads == 0


@pytest.mark.parametrize('method', ['resolve', 'validate', 'replay', 'rebind', 'prepare'])
def test_finish_callback_exception_never_publishes(method):
    owner, reads, _ = source()
    owner.select(ij.Selection('rectangle', [(4, 5), (24, 22)]))
    before = owner.snapshot()
    request = ij.SelectionEnlargementRequest(1)
    command = ij.resolve_selection_enlargement_command(before, request)
    calls = []
    def finish(a, b):
        calls.append((a, b))
        if a == 1:
            raise RuntimeError('finish refusal')
    operations = {
        'resolve': lambda: ij.resolve_selection_enlargement_command(before, request, progress=finish),
        'validate': lambda: ij.validate_selection_enlargement_command_record(command.record(), progress=finish),
        'replay': lambda: ij.selection_enlargement_command_from_record(before, command.record(), progress=finish),
        'rebind': lambda: ij.rebind_selection_enlargement_command(before, command.record(), request, progress=finish),
        'prepare': lambda: ij.prepare_selection_enlargement(command, progress=finish),
    }
    with pytest.raises(RuntimeError, match='finish refusal'):
        operations[method]()
    assert calls == [(0, 1), (1, 1)] and owner.snapshot().shares_data(before) and reads.counts().plane_reads == 0


@pytest.mark.parametrize('params', [{'distance': True}, {'distance': float('nan')}, {'distance': 1, 'units': 'invalid'}, {'distance': 1, 'extra': 3}])
def test_desktop_bad_parameters_are_atomic(params, tmp_path):
    owner, reads, _ = source()
    owner.select(ij.Selection('rectangle', [(4, 5), (24, 22)]))
    editor = Editor(preferences=tmp_path/'prefs.json')
    editor._editor.add_virtual_document(owner)
    before = owner.snapshot()
    with pytest.raises((ValueError, RuntimeError, TypeError)):
        editor.action('enlarge_selection', **params)
    assert owner.snapshot().shares_data(before) and reads.counts().plane_reads == 0 and editor.workflow == []


def test_enlargement_preferences_defaults_validation_and_reopen(tmp_path):
    path = tmp_path/'prefs.json'
    editor = Editor(preferences=path)
    assert editor.settings['selection_enlargement_distance'] == 15.
    assert editor.settings['selection_enlargement_units'] == 'calibrated'
    editor.update_settings(selection_enlargement_distance=2.5, selection_enlargement_units='pixels')
    reopened = Editor(preferences=path)
    assert reopened.settings['selection_enlargement_distance'] == 2.5 and reopened.settings['selection_enlargement_units'] == 'pixels'
    before = reopened.settings.to_dict()
    with pytest.raises(ValueError): reopened.update_settings(selection_enlargement_distance=-1)
    assert reopened.settings.to_dict() == before


@pytest.mark.parametrize('action', ['enlarge_selection', 'grow_selection', 'shrink_selection'])
def test_ordinary_document_aliases_use_metadata_only_native_enlargement(action, tmp_path):
    reads = ij.DatasetReadObserver()
    owner = ij.Document(ij.Dataset(np.full((9, 9), 30, np.uint8), axes='YX').observe_reads(reads))
    original = ij.Selection('rectangle', [(2, 2), (7, 7)])
    owner.select(original)
    editor = Editor(preferences=tmp_path/'prefs.json')
    editor._editor.add_virtual_document(owner)
    before = owner.snapshot()
    initial_reads = reads.counts().plane_reads
    editor.action(action, **({'distance': 1.} if action == 'enlarge_selection' else {'radius': 1.}))
    expected = [[3., 3.], [6., 6.]] if action == 'shrink_selection' else [[1., 1.], [8., 8.]]
    assert owner.snapshot().selection.points == expected
    assert owner.snapshot().dataset.shares_pixels(before.dataset) and owner.snapshot().previous_selection == original
    assert reads.counts().plane_reads == initial_reads and owner.undo_steps == 0 and not owner.snapshot().dirty
