"""Full command equations, independent ImageJ geometry and stage retention."""
import base64
import copy
import json
import numpy as np
import pytest
import imagejxx as ij
from test_selection_region import FIXTURE, SOURCES, sample
from test_selection_enlargement import geometry
from test_selection_enlargement_command import source


FULL = ij.SelectionEnlargementGeometry.FULL_REGION_V2


def request(distance=2, units='PIXELS'):
    return ij.SelectionEnlargementRequest(distance, getattr(ij.SelectionEnlargementUnits, units), FULL)


def corner(doc):
    mask = np.zeros((9, 9), np.uint8);mask[0, 0] = 255
    doc.select(ij.SelectionEditor(ij.Selection.from_mask(mask), (9, 9)).properties(
        3, {'origin': 'opaque origin', 'attributes': {'flags': [True, 2**64-1]}}))


@pytest.mark.parametrize('units', ['PIXELS', 'CALIBRATED'])
@pytest.mark.parametrize('kind,sequence', [(k, s) for k in range(9) for s in range(8)])
def test_recorded_full_commands_match_complete_imagej_sequences_through_reopen(kind, sequence, units):
    doc, reads, pixels = source()
    doc.select(geometry(SOURCES[kind]))
    for row in [r for r in FIXTURE['cases'] if r['kind'] == kind and r['sequence'] == sequence]:
        before = doc.snapshot()
        req = request(row['distance']*(.5 if units == 'CALIBRATED' else 1), units)
        estimate = ij.estimate_selection_enlargement_command(before, req)
        command = ij.resolve_selection_enlargement_command(before, req)
        assert command.estimate.working_bytes == estimate.working_bytes
        record = json.loads(json.dumps(command.record(), allow_nan=False))
        assert record['version'] == 2 and 'attributes' in record['base'] and 'attributes' in record['result']
        ij.validate_selection_enlargement_command_record(record)
        replay = ij.selection_enlargement_command_from_record(before, record)
        assert replay.result == command.result and replay.request.geometry == FULL
        region = ij.SelectionRegion(replay.result, (40, 48))
        _, _, width, height = row['sample']
        expected = np.unpackbits(np.frombuffer(base64.b64decode(row['mask']), np.uint8))[:width*height].reshape(height, width)
        np.testing.assert_array_equal(sample(region, row['sample']), expected)
        left, top, w, h = row['float_bounds']
        assert region.bounds == [left, top, left+w, top+h]
        doc.commit(ij.prepare_selection_enlargement(replay))
        assert doc.snapshot().dataset.shares_pixels(before.dataset)
        assert not doc.snapshot().dirty and doc.undo_steps == 0
        assert reads.counts().plane_reads == reads.counts().region_reads == 0
        # The next command captures the reopened canonical geometry, not a
        # projection and not the producer's original in-memory Selection.
        doc = ij.decode_session(ij.encode_session(ij.SessionCapture([doc.snapshot()]))).documents[0]
    assert doc.snapshot().dataset.numpy('TCZYXS').tobytes() == pixels.tobytes()


def test_explicit_v2_recovers_corner_while_v1_records_retain_original_clipping():
    full, _, _ = small()
    legacy, _, _ = small()
    for distance in (2, -2):
        a = ij.resolve_selection_enlargement_command(full.snapshot(), request(distance))
        b = ij.resolve_selection_enlargement_command(legacy.snapshot(), ij.SelectionEnlargementRequest(distance))
        assert a.record()['version'] == 2 and b.record()['version'] == 1
        full.commit(ij.prepare_selection_enlargement(a));legacy.commit(ij.prepare_selection_enlargement(b))
    assert np.count_nonzero(full.snapshot().selection.mask((9, 9))) == 1
    assert np.count_nonzero(legacy.snapshot().selection.mask((9, 9))) == 8


def small(kind='u16'):
    # source() positions its cursor at T=C=Z=1; keep those dimensions valid.
    doc, reads, pixels = source(kind, shape=(2, 2, 2, 9, 9, 1))
    corner(doc)
    return doc, reads, pixels


@pytest.mark.parametrize('kind', ['u8', 'u16', 'float', 'rgb', 'quiet', 'ieee'])
def test_explicit_editor_recording_replay_rebind_and_v4_keep_source_words(kind, tmp_path):
    from imagejxx.ui.app import Editor
    doc, reads, pixels = small(kind)
    editor = Editor(preferences=tmp_path/'prefs.json');editor._editor.add_virtual_document(doc)
    editor.recording = True
    editor.action('enlarge_selection', distance=2, geometry='full_region_v2')
    assert len(editor.workflow) == 1 and editor.workflow[0]['version'] == 2
    grown = doc.snapshot().selection
    assert grown.is_region and grown.mask_origin == [-3, -3]
    assert grown.attributes['origin'] == 'opaque origin'
    editor.action('replay_selection_enlargement', record=editor.workflow[0])
    assert doc.snapshot().selection == grown
    editor.action('replay_selection_enlargement', record=editor.workflow[0],
                  rebind={'distance': -2, 'units': 'pixels', 'geometry': 'full_region_v2'})
    assert np.count_nonzero(doc.snapshot().selection.mask((9, 9))) == 1
    assert reads.counts().plane_reads == reads.counts().region_reads == 0
    assert doc.undo_steps == 0 and not doc.snapshot().dirty
    path = tmp_path/'full.ijx';ij.save_session(str(path), ij.SessionCapture([doc.snapshot()]))
    assert path.read_bytes()[:10] == b'IMAGEJXX4\n'  # Previous retains the full grown region.
    restored = ij.load_session(str(path)).documents[0].snapshot()
    assert restored.previous_selection == grown
    assert restored.dataset.numpy('TCZYXS').tobytes() == pixels.tobytes()


@pytest.mark.parametrize('field', ['base', 'result'])
@pytest.mark.parametrize('bad', ['origin', 'origin_float', 'origin_bool', 'origin_large', 'shape', 'points', 'extra', 'attributes', 'padding'])
def test_closed_v2_values_and_full_frame_equation_refuse_malformed_records(field, bad):
    doc, reads, _ = small()
    grown = ij.resolve_selection_enlargement_command(doc.snapshot(), request())
    doc.commit(ij.prepare_selection_enlargement(grown))
    command = ij.resolve_selection_enlargement_command(doc.snapshot(), request(1))
    record = command.record();value = record[field]
    assert value['kind'] == 'region'
    if bad == 'origin': value['origin'][0] += 1
    elif bad == 'origin_float': value['origin'][0] = float(value['origin'][0])
    elif bad == 'origin_bool': value['origin'][0] = False
    elif bad == 'origin_large': value['origin'][0] = 2**31
    elif bad == 'shape': value['shape'][0] = 0
    elif bad == 'points': value['points'] = [[0, 0]]
    elif bad == 'extra': value['future'] = 1
    elif bad == 'attributes': value['attributes']['kind'] = 'mask'
    else:
        raw = bytearray(base64.b64decode(value['mask']));raw[-1] |= 1
        value['mask'] = base64.b64encode(raw).decode()
    before = doc.snapshot()
    with pytest.raises(ValueError): ij.validate_selection_enlargement_command_record(record)
    assert doc.snapshot().shares_data(before) and reads.counts().plane_reads == 0


def test_estimate_includes_off_image_output_and_exact_command_cap():
    doc, reads, _ = small();before = doc.snapshot()
    estimate = ij.estimate_selection_enlargement_command(before, request(10))
    limits = ij.SelectionEnlargementCommandLimits();limits.max_working_bytes = estimate.working_bytes
    command = ij.resolve_selection_enlargement_command(before, request(10), limits)
    assert command.result.mask_origin[0] < 0 and command.result.to_dict()['shape'][0] > 9
    limits.max_working_bytes -= 1;calls = []
    with pytest.raises((ValueError, OverflowError)):
        ij.resolve_selection_enlargement_command(before, request(10), limits, checkpoint=lambda: calls.append(1))
    assert calls == [] and doc.snapshot().shares_data(before) and reads.counts().plane_reads == 0
    limits = ij.SelectionEnlargementCommandLimits();limits.max_pixels = 100
    with pytest.raises((ValueError, OverflowError)): ij.resolve_selection_enlargement_command(before, request(10), limits)


@pytest.mark.parametrize('method', ['resolve', 'validate', 'replay', 'rebind', 'prepare'])
def test_finish_failure_never_publishes_full_command(method):
    doc, reads, _ = small();before = doc.snapshot()
    req = request();command = ij.resolve_selection_enlargement_command(before, req)
    def finish(done, total):
        if done == total: raise RuntimeError('regional finish failure')
    calls = dict(resolve=lambda: ij.resolve_selection_enlargement_command(before, req, progress=finish),
        validate=lambda: ij.validate_selection_enlargement_command_record(command.record(), progress=finish),
        replay=lambda: ij.selection_enlargement_command_from_record(before, command.record(), progress=finish),
        rebind=lambda: ij.rebind_selection_enlargement_command(before, command.record(), req, progress=finish),
        prepare=lambda: ij.prepare_selection_enlargement(command, progress=finish))
    with pytest.raises(RuntimeError, match='regional finish failure'): calls[method]()
    assert doc.snapshot().shares_data(before) and reads.counts().plane_reads == 0


def test_request_and_limits_are_frozen_before_callbacks():
    doc, _, _ = small();req = request();limits = ij.SelectionEnlargementCommandLimits()
    def progress(done, total):
        req.distance = -100;req.geometry = ij.SelectionEnlargementGeometry.CANVAS_V1;limits.max_working_bytes = 1
    command = ij.resolve_selection_enlargement_command(doc.snapshot(), req, limits, progress=progress)
    assert command.record()['version'] == 2 and command.request.distance == 2
    assert command.result.mask_origin == [-3, -3]


def step(distance, units='pixels', original=None, explicit=True):
    params = {'distance': distance, 'units': units}
    if explicit: params['geometry'] = 'full_region_v2'
    return ij.NativeWorkflowOperation('selection_enlargement', params, ij.TargetRequest(), original=original)


@pytest.mark.parametrize('original_mode', ['authored', 'captured_explicit', 'captured_inferred'])
@pytest.mark.parametrize('filter_pixels', [False, True])
def test_portable_workflow_retains_full_stage_geometry_and_one_pixel_commit(original_mode, filter_pixels):
    doc, reads, pixels = small();before = doc.snapshot()
    record = ij.resolve_selection_enlargement_command(before, request()).record()
    steps = [step(2, original=record if original_mode != 'authored' else None, explicit=original_mode != 'captured_inferred')]
    if filter_pixels: steps.append(ij.NativeWorkflowOperation('add', {'value': 7}, ij.TargetRequest()))
    steps.append(step(-2))
    portable = ij.NativeWorkflowProgram.from_json(ij.NativeWorkflowProgram(steps).json())
    prepared = ij.prepare_native_workflow(before, portable.steps)
    assert np.count_nonzero(prepared.preview.selection.mask((9, 9))) == 1
    assert prepared.preview.previous_selection.is_region
    assert doc.snapshot().shares_data(before)
    doc.commit(prepared.begin_edit())
    assert doc.undo_steps == int(filter_pixels)
    expected = pixels.copy()
    if filter_pixels:
        mask = ij.SelectionRegion(before.selection, (9, 9)).enlarged(2).project((9, 9)).mask((9, 9)) != 0
        expected[1, 1, 1][mask] += 7
    else: assert reads.counts().plane_reads == 0
    np.testing.assert_array_equal(doc.snapshot().dataset.numpy('TCZYXS'), expected)


def test_session_batch_retains_regions_across_selection_steps_and_reopen(tmp_path):
    inputs = [];originals = {}
    for index in range(2):
        doc, _, _ = small()
        path = tmp_path/f'input{index}.ijx';ij.save_session(str(path), ij.SessionCapture([doc.snapshot()]))
        originals[path] = path.read_bytes();inputs.append(ij.NativeWorkflowBatchInput(str(path), f'result{index}'))
    output = tmp_path/'output';output.mkdir()
    prepared = ij.prepare_native_workflow_batch(inputs, [step(2), step(-2)], str(output))
    assert all(not (output/f'result{index}.ijx').exists() for index in range(2))
    prepared.publish()
    for index in range(2):
        path = output/f'result{index}.ijx';assert path.read_bytes()[:10] == b'IMAGEJXX4\n'
        restored = ij.load_session(str(path)).documents[0].snapshot()
        assert restored.previous_selection.is_region
        assert np.count_nonzero(restored.selection.mask((9, 9))) == 1
    assert all(path.read_bytes() == raw for path, raw in originals.items())


def test_actual_imgui_dialogs_and_workflow_adoption_preserve_off_image_geometry(tmp_path):
    from imagejxx.ui.app import Editor
    from test_imgui_roi_style import click
    import time
    doc, _, _ = small()
    seed = doc.snapshot().selection
    editor = Editor(preferences=tmp_path/'prefs.json');editor._editor.add_virtual_document(doc)
    editor.recording = True
    editor.update_settings(selection_enlargement_distance=2., selection_enlargement_units='pixels')
    for shrink in [False, True]:
        editor.action('show_selection_enlargement', shrink=shrink)
        frame = editor.render_headless(frames=3)
        assert frame['dialog']['values'] == {'distance': -2. if shrink else 2., 'units': 'pixels'}
        editor.render_headless(events=click(frame['dialog']['controls']['apply']['point']))
        assert editor.workflow[-1]['version'] == 2
        if not shrink:
            assert doc.snapshot().selection.is_region
            assert doc.snapshot().selection.mask_origin == [-3, -3]
        else: assert np.count_nonzero(doc.snapshot().selection.mask((9, 9))) == 1
    assert len(editor.workflow) == 2
    doc.select(seed)
    editor.action('run_workflow')
    frame = editor.render_headless(frames=3)
    def hit(name):
        nonlocal frame
        control = frame['native_workflow']['controls'][name]
        assert control['enabled'] and control['visible'], control
        frame = editor.render_headless(events=click(control['point']))
    def confirm_all():
        for index in range(len(frame['native_workflow']['entries'])):
            hit(f'step.{index}');hit('confirm')
    def settle():
        nonlocal frame
        for _ in range(3000):
            frame = editor.render_headless()
            if not frame['native_workflow']['working']: return
            time.sleep(.001)
        raise AssertionError('Workflow worker did not settle')
    confirm_all();hit('prepare_program');settle()
    (tmp_path/'program-observation.json').write_text(json.dumps(frame))
    assert frame['native_workflow']['program_ready'], frame['native_workflow']
    hit('keep_program')
    assert editor.workflow[0]['schema'] == 'imagejxx.native-workflow-program'
    editor.recording = False
    editor.action('run_workflow');frame = editor.render_headless(frames=3)
    confirm_all();hit('prepare');settle()
    assert frame['native_workflow']['ready']
    hit('apply')
    (tmp_path/'final-observation.json').write_text(json.dumps(frame))
    assert np.count_nonzero(doc.snapshot().selection.mask((9, 9))) == 1
    assert doc.snapshot().previous_selection.is_region and doc.undo_steps == 0 and not doc.snapshot().dirty


@pytest.mark.parametrize('leaf', ['scalar', 'strings'])
@pytest.mark.parametrize('depth,accepted', [(124, True), (125, True), (126, False), (127, False)])
def test_nested_v2_attributes_admit_the_complete_record_before_callbacks(depth, accepted, leaf):
    doc, _, _ = small()
    nested = 0 if leaf == 'scalar' else ['text']
    for _ in range(depth-int(leaf == 'strings')): nested = [nested]
    styled = ij.SelectionEditor(doc.snapshot().selection, (9, 9)).properties(3, {'deep': nested})
    doc.select(styled)
    calls = []
    if accepted:
        command = ij.resolve_selection_enlargement_command(doc.snapshot(), request(), progress=lambda a,b:calls.append(a))
        ij.validate_selection_enlargement_command_record(command.record())
    else:
        with pytest.raises(ValueError, match='nesting'):
            ij.resolve_selection_enlargement_command(doc.snapshot(), request(), progress=lambda a,b:calls.append(a))
        assert calls == []


@pytest.mark.parametrize('regional', [False, True])
@pytest.mark.parametrize('empty,distance', [(True, 2), (False, -100)])
def test_kernel_noops_keep_exact_storage_previous_and_selection_revision(regional, empty, distance):
    doc, reads, _ = small()
    if empty: doc.select(ij.Selection.from_mask(np.zeros((9, 9), np.uint8)))
    if regional: doc.select(ij.SelectionRegion(doc.snapshot().selection, (9, 9)).as_selection())
    before = doc.snapshot()
    command = ij.resolve_selection_enlargement_command(before, request(distance))
    assert not command.changes_selection and command.result == before.selection
    ij.validate_selection_enlargement_command_record(command.record())
    doc.commit(ij.prepare_selection_enlargement(command))
    assert doc.snapshot().shares_data(before) and doc.undo_steps == 0
    assert reads.counts().plane_reads == 0
