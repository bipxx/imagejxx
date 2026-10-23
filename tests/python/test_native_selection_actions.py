"""Native selection menu actions must not enter legacy pixel materialization."""
import numpy as np
import pytest
import imagejxx as ij
from imagejxx.ui.app import Editor


@pytest.mark.parametrize("dtype,samples", [(np.uint8, 3), (np.uint8, 1), (np.uint16, 1), (np.float32, 1)])
def test_select_all_deselect_restore_keep_native_pixels_navigation_and_history(dtype, samples, tmp_path):
    reads = ij.DatasetReadObserver()
    owner = ij.Document(ij.Dataset(np.full((2, 2, 2, 4, 5, samples), 30, dtype), axes="TCZYXS").observe_reads(reads))
    original = ij.Selection("ellipse", [(1, 0), (4, 3)])
    owner.select(original)
    view = owner.snapshot().display
    view.t = view.c = 1
    view.crosshair = [2, 1, 1]
    owner.set_display(view)
    before = owner.snapshot()
    editor = Editor(preferences=tmp_path/"prefs.json")
    editor._editor.add_virtual_document(owner)
    editor.action("select_all")
    all_area = owner.snapshot().selection
    assert all_area.kind == "rectangle" and all_area.points == [[0., 0.], [5., 4.]]
    assert owner.snapshot().previous_selection == original
    editor.action("select_none")
    assert owner.snapshot().selection is None
    editor.action("restore_selection")
    assert owner.snapshot().selection == all_area
    after = owner.snapshot()
    assert after.dataset.shares_pixels(before.dataset)
    assert after.revisions.pixels == before.revisions.pixels
    assert after.display.t == before.display.t and after.display.c == before.display.c
    assert after.display.crosshair == before.display.crosshair
    assert owner.undo_steps == 0 and editor.workflow == []
    assert reads.counts().plane_reads == 0


@pytest.mark.parametrize("action", ["select_all", "select_none", "restore_selection"])
def test_native_selection_actions_reject_extra_fields_and_inactive_owners(action, tmp_path):
    a = ij.Document(ij.Dataset(np.full((2, 1, 1, 2, 4, 3), 30, np.uint8), axes="TCZYXS"))
    b = ij.Document(ij.Dataset(np.full((2, 1, 1, 2, 4, 3), 40, np.uint8), axes="TCZYXS"))
    editor = Editor(preferences=tmp_path/"prefs.json")
    editor._editor.add_virtual_document(a)
    editor._editor.add_virtual_document(b)
    before_a, before_b = a.snapshot(), b.snapshot()
    with pytest.raises((ValueError, RuntimeError)):
        editor.action(action, extra=True)
    with pytest.raises((ValueError, RuntimeError)):
        editor.action(action, document_id=a.id)
    assert a.snapshot().shares_data(before_a) and b.snapshot().shares_data(before_b)
