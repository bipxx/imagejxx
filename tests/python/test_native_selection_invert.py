"""Scientific inverse uses metadata-only native algebra, including IEEE sources."""
import copy
import numpy as np
import pytest
import imagejxx as ij
from imagejxx.ui.app import Editor


KINDS = ('rectangle', 'ellipse', 'polygon', 'freehand', 'rotated_rectangle',
         'rotated_ellipse', 'rounded_rectangle', 'rotated_rounded_rectangle', 'mask', 'empty_mask', 'whole')
SOURCES = ('u8', 'u16', 'f32', 'rgb', 'quiet', 'ieee')


def area(kind, width=7, height=5):
    if kind in ('mask', 'empty_mask'):
        rows = ('0111110', '0100010', '0100010', '0100010', '0111110')
        return ij.Selection.from_mask(np.array([[255 if c == '1' and kind == 'mask' else 0
                                                for c in row] for row in rows], np.uint8))
    if kind == 'whole':
        return ij.Selection('rectangle', [(0, 0), (width, height)])
    if kind in ('polygon', 'freehand'):
        return ij.Selection(kind, [(-1, 1), (3, 0), (6, 3), (2, 5)])
    if kind.startswith('rotated_'):
        return ij.Selection(kind, [(3, 2), (5, 3), (2.5, 3)],
                            attributes={'corner_diameter': 2} if kind == 'rotated_rounded_rectangle' else {})
    if kind == 'rounded_rectangle':
        return ij.Selection(kind, [(1, 0), (6, 5)], attributes={'corner_diameter': 2})
    return ij.Selection(kind, [(1, 1), (6, 4)])


def source(kind, width=7, height=5):
    shape = (2, 2, 2, height, width, 3 if kind == 'rgb' else 1)
    policy = ij.FloatingSamplePolicy.FINITE_ONLY
    dtype = np.uint16 if kind == 'u16' else np.float32 if kind in ('f32', 'quiet', 'ieee') else np.uint8
    pixels = np.full(shape, 30, dtype)
    if kind in ('quiet', 'ieee'):
        policy = (ij.FloatingSamplePolicy.PRESERVE_QUIET_NAN_FLOAT32 if kind == 'quiet'
                  else ij.FloatingSamplePolicy.PRESERVE_IEEE_FLOAT32)
        words = [0x7fc12345, 0x80000000, 0x3f800000] if kind == 'quiet' else [
            0x7fc12345, 0x7f800000, 0xff800000, 0x80000000, 0x00000001, 0x3f800000]
        pixels.view(np.uint32).flat[:len(words)] = words
    reads = ij.DatasetReadObserver()
    owner = ij.Document(ij.Dataset(pixels, axes='TCZYXS', sample_policy=policy).observe_reads(reads))
    view = owner.snapshot().display
    view.t = view.c = 1
    view.crosshair = [0, 0, 1]
    owner.set_display(view)
    return owner, reads, pixels


def editor_for(owner, tmp_path):
    editor = Editor(preferences=tmp_path/'prefs.json')
    editor._editor.add_virtual_document(owner)
    return editor


@pytest.mark.parametrize('source_kind', SOURCES)
@pytest.mark.parametrize('kind', KINDS)
def test_inverse_complements_every_cell_preserving_source_and_restore(source_kind, kind, tmp_path):
    owner, reads, pixels = source(source_kind)
    initial = area(kind)
    owner.select(initial)
    before = owner.snapshot()
    original_mask = initial.mask((5, 7)) != 0
    editor = editor_for(owner, tmp_path)
    editor.action('invert_selection')
    after = owner.snapshot()
    np.testing.assert_array_equal(after.selection.mask((5, 7)) != 0, ~original_mask)
    assert after.previous_selection == initial
    assert after.dataset.shares_pixels(before.dataset) and after.dataset.sample_policy == before.dataset.sample_policy
    assert after.revisions.pixels == before.revisions.pixels and after.revisions.metadata == before.revisions.metadata
    assert after.display.t == after.display.c == after.display.crosshair[2] == 1
    assert not after.dirty and owner.undo_steps == 0 and owner.redo_steps == 0
    assert editor.workflow == [] and reads.counts().plane_reads == 0
    editor.action('restore_selection')
    assert owner.snapshot().selection == initial
    editor.action('invert_selection')
    editor.action('invert_selection')
    np.testing.assert_array_equal(owner.snapshot().selection.mask((5, 7)) != 0, original_mask)
    assert reads.counts().plane_reads == 0
    assert owner.snapshot().dataset.numpy('TCZYXS').tobytes() == pixels.tobytes()


@pytest.mark.parametrize('source_kind', SOURCES)
def test_inverse_without_selection_selects_whole_image(source_kind, tmp_path):
    owner, reads, _ = source(source_kind)
    editor = editor_for(owner, tmp_path)
    editor.action('invert_selection')
    assert owner.snapshot().selection == area('whole')
    assert reads.counts().plane_reads == 0


@pytest.mark.parametrize('kind', ['line', 'polyline', 'multipoint', 'point', 'angle'])
def test_non_area_inverse_is_atomic(kind, tmp_path):
    owner, reads, _ = source('u16')
    points = [(1, 1)] if kind == 'point' else [(0, 0), (2, 1), (4, 3)] if kind in ('angle', 'polyline', 'multipoint') else [(0, 0), (3, 3)]
    owner.select(ij.Selection(kind, points))
    editor = editor_for(owner, tmp_path)
    before = owner.snapshot()
    with pytest.raises(ValueError, match='area'):
        editor.action('invert_selection')
    assert owner.snapshot().shares_data(before) and reads.counts().plane_reads == 0


@pytest.mark.parametrize('params', [{'radius': 1}, {'extra': True}, {'document_id': 0, 'unexpected': False}])
def test_inverse_rejects_unknown_fields(params, tmp_path):
    owner, reads, _ = source('u8')
    owner.select(area('mask'))
    editor = editor_for(owner, tmp_path)
    before = owner.snapshot()
    with pytest.raises(ValueError):
        editor.action('invert_selection', **params)
    assert owner.snapshot().shares_data(before) and reads.counts().plane_reads == 0


def test_inverse_inactive_owner_and_live_budget_refusal_are_atomic(tmp_path):
    owner, reads, _ = source('u8', width=512, height=256)
    owner.select(ij.Selection('rectangle', [(1, 1), (10, 10)]))
    editor = editor_for(owner, tmp_path)
    other, _, _ = source('u16')
    editor._editor.add_virtual_document(other)
    before = owner.snapshot()
    with pytest.raises(ValueError):
        editor.action('invert_selection', document_id=owner.id)
    assert owner.snapshot().shares_data(before)
    editor.active_index = 0
    editor.update_settings(processing_memory_mb=1, persist=False)
    with pytest.raises(ValueError, match='working memory limit'):
        editor.action('invert_selection')
    assert owner.snapshot().shares_data(before) and reads.counts().plane_reads == 0
    editor.update_settings(processing_memory_mb=8, persist=False)
    editor.action('invert_selection')
    assert owner.snapshot().previous_selection == before.selection


@pytest.mark.parametrize('operation', ['REPLACE', 'ADD', 'SUBTRACT', 'INTERSECT', 'XOR'])
def test_ieee_core_command_record_replay_rebind_and_bit_preservation(operation):
    owner, reads, pixels = source('ieee')
    owner.select(area('mask'))
    before = owner.snapshot()
    request = ij.SelectionCreationRequest(area('rectangle'), getattr(ij.SelectionCreationOperation, operation))
    command = ij.resolve_selection_creation_command(before, request)
    record = command.record()
    assert record['source']['sample_policy'] == 'preserve_ieee_float32'
    ij.validate_selection_creation_command_record(record)
    replay = ij.selection_creation_command_from_record(before, record)
    owner.commit(ij.prepare_selection_creation(replay))
    assert owner.snapshot().dataset.shares_pixels(before.dataset)
    assert reads.counts().plane_reads == 0
    assert owner.snapshot().dataset.numpy('TCZYXS').tobytes() == pixels.tobytes()
    rebound = ij.rebind_selection_creation_command(owner.snapshot(), record, request)
    assert rebound.record()['source']['sample_policy'] == 'preserve_ieee_float32'
    bad = copy.deepcopy(record)
    bad['source']['type'] = 'uint16'
    with pytest.raises(ValueError, match='Float32'):
        ij.validate_selection_creation_command_record(bad)


@pytest.mark.parametrize('source_kind', SOURCES)
def test_inverse_records_complete_existing_command_and_preserves_redo(source_kind, tmp_path):
    owner, reads, _ = source(source_kind)
    edit = owner.begin_edit()
    edit.set_property('redo_probe', {'kept': [1, 2, 3]})
    owner.commit(edit)
    owner.undo()
    owner.select(area('mask'))
    before = owner.snapshot()
    editor = editor_for(owner, tmp_path)
    editor.recording = True
    editor.action('invert_selection')
    assert owner.redo_steps == 1 and owner.undo_steps == 0
    assert len(editor.workflow) == 1
    record = editor.workflow[0]
    assert record['operation'] == 'xor' and record['source']['id'] == owner.id
    assert record['position'] == {'t': 1, 'c': 1, 'z': 1}
    ij.validate_selection_creation_command_record(record)
    command = ij.selection_creation_command_from_record(before, record)
    np.testing.assert_array_equal(command.result.mask((5, 7)), owner.snapshot().selection.mask((5, 7)))
    assert reads.counts().plane_reads == 0
    # Desktop replay retains its established explicit scientific creation context.
    editor.action('create_roi')
    editor.replay_selection_creation(record)
    assert len(editor.workflow) == 1  # Exact replay is a no-op.
    owner.redo()
    assert owner.snapshot().metadata['properties']['redo_probe'] == {'kept': [1, 2, 3]}
