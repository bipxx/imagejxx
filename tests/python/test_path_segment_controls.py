"""Path command staging through active/saved native desktop properties."""
import json
import numpy as np
import pytest
import imagejxx as ij
from imagejxx import _desktop

COMMANDS = [0, 1.25, 1.125, 2, 3.25, .25, 5.25, 1.125,
            3, 6.25, 2, 6.25, 4, 5.25, 4, 1, 1.25, 4, 4]


def setup(tmp_path, carrier):
    reads = ij.DatasetReadObserver()
    owner = ij.Document(ij.Dataset(np.arange(840, dtype=np.uint16).reshape(6,10,14), axes='ZYX').observe_reads(reads))
    path = ij.Selection.from_path(ij.ImageJCompositePath(COMMANDS)).with_attributes({'opaque': ['kept', 2**64-1]})
    tx = owner.snapshot().begin_edit()
    tx.select(path)
    tx.set_previous_selection(ij.Selection('line', [(0,0), (1,1)]))
    tx.set_rois([ij.ROI('curve', path, attributes={'row':'kept'})])
    owner.commit(tx)
    editor = _desktop.Editor(preferences_path=str(tmp_path/'preferences.json'))
    editor.add_native_document(owner)
    def action(name, **values):
        editor.action(name, json.dumps(values))
    if carrier == 'saved': action('edit_saved_roi_geometry', index=0)
    action('show_selection_properties')
    return editor, action, owner, reads


@pytest.mark.parametrize('carrier', ['active', 'saved'])
@pytest.mark.parametrize('operation,index,command', [
    ('split', 2, .25), ('replace', 1, [1,5.25,1.125]),
    ('insert', 4, [1,2,3]), ('remove', 1, None),
])
def test_command_stage_apply_history_and_v5(carrier, operation, index, command, tmp_path):
    editor, action, owner, reads = setup(tmp_path, carrier)
    before = owner.snapshot(); history = owner.undo_steps
    request = {'operation': operation, 'index': index}
    if operation == 'split': request['t'] = command
    elif operation != 'remove': request['command'] = command
    action('selection_properties_draft', path_edit=request)
    assert owner.snapshot().shares_data(before)
    action('apply_selection_properties')
    after = owner.snapshot()
    geometry = (before.selection if carrier == 'active' else before.rois[0].selection).path
    if operation == 'split': expected = ij.split_composite_path(geometry, index, command)
    else:
        op = {'replace': ij.PathSegmentEdit.Replace, 'insert': ij.PathSegmentEdit.InsertBefore,
              'remove': ij.PathSegmentEdit.Remove}[operation]
        expected = ij.edit_composite_path(geometry, op, index, [] if command is None else command)
    selected = after.selection if carrier == 'active' else after.rois[0].selection
    assert selected.path.commands == expected.commands
    assert selected.attributes == before.selection.attributes
    assert after.dataset.shares_pixels(before.dataset)
    assert after.rois[0].name == before.rois[0].name and after.rois[0].attributes == before.rois[0].attributes
    if carrier == 'active':
        assert after.previous_selection == before.selection and after.rois[0].selection == before.rois[0].selection
    else:
        assert after.selection == before.selection and after.previous_selection == before.previous_selection
    assert owner.undo_steps == history+1
    owner.undo(); assert owner.snapshot().shares_data(before)
    owner.redo()
    restored = owner.snapshot()
    assert (restored.selection if carrier == 'active' else restored.rois[0].selection).path.commands == expected.commands
    assert reads.counts().plane_reads == reads.counts().region_reads == 0
    archive = tmp_path/'authored.ijx'
    ij.save_session(str(archive), ij.SessionCapture([owner]))
    assert archive.read_bytes().startswith(b'IMAGEJXX5\n')
    reopened = ij.load_session(str(archive)).documents[0].snapshot()
    assert (reopened.selection if carrier == 'active' else reopened.rois[0].selection).path.commands == expected.commands


@pytest.mark.parametrize('carrier', ['active', 'saved'])
def test_composed_numeric_and_segment_draft_cancel(carrier, tmp_path):
    editor, action, owner, reads = setup(tmp_path, carrier)
    before = owner.snapshot()
    action('selection_properties_draft', translation=[.25, -.125])
    action('selection_properties_draft', path_edit={'operation':'split','index':1,'t':.5})
    action('selection_properties_draft', path_edit={'operation':'insert','index':4,'command':[1,2,3]})
    action('cancel_selection_properties')
    assert owner.snapshot().shares_data(before)
    assert reads.counts().plane_reads == reads.counts().region_reads == 0


@pytest.mark.parametrize('intent', [
    {'operation':'split','index':1,'t':True}, {'operation':'split','index':True,'t':.5},
    {'operation':'remove','index':0}, {'operation':'replace','index':1,'command':[3,1,2]},
    {'operation':'split','index':1,'t':.5,'extra':1},
])
def test_invalid_segment_intent_never_publishes(intent, tmp_path):
    editor, action, owner, _ = setup(tmp_path, 'saved')
    before = owner.snapshot()
    with pytest.raises(ValueError): action('selection_properties_draft', path_edit=intent)
    assert owner.snapshot().shares_data(before)


def test_segment_operation_is_not_mixed_with_numeric_patch(tmp_path):
    editor, action, owner, _ = setup(tmp_path, 'active')
    before = owner.snapshot()
    with pytest.raises(ValueError):
        action('selection_properties_draft', path_edit={'operation':'split','index':1,'t':.5}, translation=[1,2])
    assert owner.snapshot().shares_data(before)


def test_core_selection_history_is_explicit_and_atomic():
    reads = ij.DatasetReadObserver()
    owner = ij.Document(ij.Dataset(np.zeros((6,8), dtype=np.uint8), axes='YX').observe_reads(reads))
    first = ij.Selection.from_path(ij.ImageJCompositePath([0,1,1,2,3,5,6,1,4]))
    owner.select(first)
    before = owner.snapshot()
    assert owner.undo_steps == 0 and not before.dirty
    tx = before.begin_edit()
    tx.select(ij.SelectionEditor(first, (6,8)).moved([1,0]))
    tx.retain_selection_history()
    owner.commit(tx)
    after = owner.snapshot()
    assert owner.undo_steps == 1 and after.dirty
    assert after.previous_selection == before.selection
    assert after.dataset.shares_pixels(before.dataset)
    owner.undo(); assert owner.snapshot().shares_data(before) and not owner.snapshot().dirty
    owner.redo(); assert owner.snapshot().shares_data(after)
    noop = owner.snapshot().begin_edit(); noop.retain_selection_history(); owner.commit(noop)
    assert owner.undo_steps == 1 and owner.snapshot().shares_data(after)
    stale = owner.snapshot().begin_edit(); stale.select(first); stale.retain_selection_history()
    owner.select(ij.Selection('rectangle', [(0,0),(2,2)]))
    changed = owner.snapshot()
    with pytest.raises(ij.RevisionConflict): owner.commit(stale)
    assert owner.snapshot().shares_data(changed) and owner.undo_steps == 1
    assert reads.counts().plane_reads == reads.counts().region_reads == 0


@pytest.mark.parametrize('carrier', ['active', 'saved'])
def test_refused_segment_stage_can_be_corrected_without_losing_prior_draft(carrier, tmp_path):
    editor, action, owner, _ = setup(tmp_path, carrier)
    before = owner.snapshot()
    action('selection_properties_draft', path_edit={'operation':'split','index':1,'t':.25})
    with pytest.raises(ValueError):
        action('selection_properties_draft', path_edit={'operation':'remove','index':0})
    with pytest.raises(ValueError): action('apply_selection_properties')
    assert owner.snapshot().shares_data(before)
    action('selection_properties_draft', path_edit={'operation':'replace','index':1,'command':[1,2.25,.796875]})
    action('apply_selection_properties')
    result = owner.snapshot().selection if carrier == 'active' else owner.snapshot().rois[0].selection
    assert result.path.commands[:6] == [0,1.25,1.125,1,2.25,.796875]
    assert result.path.commands[6:11] == [2,3.75,.46875,5.25,1.125]
