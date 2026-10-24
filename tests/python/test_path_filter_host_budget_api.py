"""Exposed synchronous Editor processing obeys the same finite host admission.

These API calls receive no actual-input UI credit. Sources are eager; no lazy
provider/read or complete-host callback claim is made.
"""
import imagejxx as ij
import numpy as np
import pytest

from test_path_filter_admission_ui import pinned_members
from test_path_filter_ui import PATH


def attach(tmp_path, selected):
    from imagejxx.ui.app import Editor
    from imagejxx.ui.persistence import Preferences

    y, x = np.indices((77, 100))
    pixels = ((37*x + 53*y + (x*y) % 17) % 256).astype(np.uint8)
    owner = ij.Document(ij.Dataset(pixels, axes='YX'))
    if selected:
        owner.select(ij.Selection.from_path(ij.ImageJCompositePath(PATH)))
    editor = Editor(preferences=Preferences(tmp_path / 'preferences.json'))
    editor.update_settings(processing_memory_mb=1, processing_workers=1, persist=False)
    live = editor.add_native_document(owner)
    editor.recording = True
    return editor, live, owner, pixels


@pytest.mark.parametrize('asynchronous', [False, True])
def test_exposed_editor_path_refusal_is_atomic_for_both_execution_modes(tmp_path, asynchronous):
    editor, live, owner, pixels = attach(tmp_path, True)
    before = owner.snapshot()
    steps = owner.undo_steps, owner.redo_steps
    table = editor.workflow
    # Literal fixture: P=100*77*8; B=P*17=1,047,200, below 1 MiB.
    # Even fixed raster planning scratch (4,096) exceeds the remaining 1,376.
    assert 100*77*8*17 < 1024*1024 < 100*77*8*17 + 4096
    with pytest.raises(ValueError, match='^ImageJ shape exceeds working-byte limit$'):
        live.apply_operation('invert', asynchronous=asynchronous)
    after = owner.snapshot()
    assert after.shares_data(before)
    assert (owner.undo_steps, owner.redo_steps) == steps
    assert editor.workflow == table and not editor.busy
    assert editor.jobs == []
    assert after.selection == before.selection
    assert after.display.properties == before.display.properties
    assert after.metadata == before.metadata and after.record_layout == before.record_layout
    np.testing.assert_array_equal(after.dataset.numpy('YX'), pixels)

    editor.update_settings(processing_memory_mb=64, persist=False)
    live.apply_operation('invert', asynchronous=False)
    expected = pixels.copy()
    for x, y in pinned_members():
        expected[y, x] = 255 - int(pixels[y, x])
    np.testing.assert_array_equal(owner.snapshot().dataset.numpy('YX'), expected)
    assert owner.undo_steps == steps[0] + 1 and owner.redo_steps == 0
    assert len(editor.workflow) == len(table) + 1
    record = editor.workflow[-1]
    assert record['operation'] == 'invert' and record['parameters'] == {}
    assert record['all_slices'] is False and record['slice_index'] == 0
    assert record['selection']['path'] == PATH
    assert owner.snapshot().selection == before.selection
    owner.undo()
    np.testing.assert_array_equal(owner.snapshot().dataset.numpy('YX'), pixels)
    owner.redo()
    np.testing.assert_array_equal(owner.snapshot().dataset.numpy('YX'), expected)


def test_exposed_editor_whole_plane_control_fits_low_host_cap(tmp_path):
    editor, live, owner, pixels = attach(tmp_path, False)
    before = owner.undo_steps
    live.apply_operation('invert', asynchronous=False)
    np.testing.assert_array_equal(owner.snapshot().dataset.numpy('YX'), 255 - pixels)
    assert owner.undo_steps == before + 1 and len(editor.workflow) == 1
    assert not editor.busy and editor.jobs == []


def test_ordinary_singleplane_path_manual_threshold_records_and_rebinds_v3(tmp_path):
    # This is the ordinary non-pooled route; the actual-input pooled route has
    # separate coverage. These actions are public API calls, not UI gestures.
    editor, live, owner, pixels = attach(tmp_path, True)
    editor.update_settings(processing_memory_mb=64, persist=False)
    editor.action('show_manual_threshold', document_id=live.id)
    editor.action('set_manual_threshold', lower=101, upper=255, document_id=live.id)
    editor.action('apply_manual_threshold', lower=101, upper=255, document_id=live.id)
    editor.wait()
    expected = pixels.copy()
    for x, y in pinned_members():
        expected[y, x] = 255 if 101 <= pixels[y, x] <= 255 else 0
    np.testing.assert_array_equal(owner.snapshot().dataset.numpy('YX'), expected)
    assert owner.undo_steps == 1 and len(editor.workflow) == 1
    record = editor.workflow[0]
    assert record['schema_version'] == 3 and record['profile'] == 'manual_raw_inclusive_v3'
    assert record['selection']['path'] == PATH
    assert record['lower'] == 101 and record['upper'] == 255
    owner.undo()
    np.testing.assert_array_equal(owner.snapshot().dataset.numpy('YX'), pixels)
    with pytest.raises((ValueError, ij.RevisionConflict)):
        editor.action('replay_manual_threshold', record=record, document_id=live.id)
    editor.action('replay_manual_threshold', record=record, document_id=live.id,
                  threshold_rebind={'scope': 'positions', 'positions': [[0, 0, 0]], 'roi': 'active_selection'})
    editor.wait()
    np.testing.assert_array_equal(owner.snapshot().dataset.numpy('YX'), expected)
    assert editor.workflow[-1]['schema_version'] == 3
