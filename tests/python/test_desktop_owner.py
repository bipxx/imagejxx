"""Shared native owners across headless controllers, desktop panels and Python."""

from concurrent.futures import ThreadPoolExecutor
import json

import imagejxx as ij
import numpy as np
import pytest


@pytest.fixture
def editor(tmp_path):
    pytest.importorskip("imagejxx._desktop", exc_type=ModuleNotFoundError)
    from imagejxx.ui.app import Editor
    from imagejxx.ui.persistence import Preferences

    return Editor(preferences=Preferences(tmp_path / "preferences.json"))


@pytest.mark.parametrize("dtype", [np.uint8, np.uint16, np.float32, np.float64])
def test_native_attachment_shares_data_history_and_display(editor, dtype):
    pixels = np.arange(60, dtype=dtype).reshape(3, 4, 5)
    owner = ij.Document(ij.Dataset(pixels, axes="ZYX"), "Shared")
    controller = ij.Application()
    controller.adopt(owner)
    live = editor.add_native_document(owner)
    assert live.id == owner.id and live.native_owner is owner
    captured = live.resolve_target(operation="add")
    assert captured.document_id == owner.id
    assert captured.source.shares_data(controller.snapshot())
    assert live.native_snapshot().shares_data(controller.snapshot())
    assert live.info["undo_steps"] == 0 and not live.info["dirty"]
    before = owner.snapshot()
    tx = controller.begin_edit()
    tx.replace_plane(np.full((4, 5), 99, dtype), z=1)
    tx.set_title("Headless edit")
    controller.commit(tx)
    assert live.info["title"] == "Headless edit"
    np.testing.assert_array_equal(live.plane(coordinate=1), 99)
    assert live.native_snapshot().shares_data(owner.snapshot())
    live.apply_operation("add", {"value": 2}, asynchronous=False)
    np.testing.assert_array_equal(
        owner.snapshot().dataset.plane(coordinate=0), pixels[0] + 2
    )
    assert owner.undo_steps == live.info["undo_steps"] == 2
    view = owner.snapshot().display
    view.crosshair = [3, 2, 2]
    view.properties = {"lut": "Ice", "display_range": [2, 100]}
    owner.set_display(view)
    assert live.view["crosshair"] == [3, 2, 2]
    owner.undo()
    assert live.view["crosshair"] == [3, 2, 2]
    assert live.native_snapshot().display.properties == view.properties
    np.testing.assert_array_equal(live.plane(coordinate=0), pixels[0])
    np.testing.assert_array_equal(before.dataset.plane(coordinate=1), pixels[1])


def test_ui_native_owner_and_transaction_handle_share_the_same_owner(editor):
    from imagejxx.ui.model import Document

    editor.add_document(Document(np.arange(20.0).reshape(4, 5)))
    live = editor.live_document()
    owner = live.native_owner
    source = live.native_snapshot()
    assert owner.id == live.id and owner.snapshot().shares_data(source)
    edit = live.begin_edit()
    edit.set_property("source", {"id": 2**64 - 1})
    owner.commit(edit)
    assert live.native_snapshot().metadata["properties"]["source"]["id"] == 2**64 - 1
    same = live.native_snapshot().begin_edit()
    live.commit(same)
    assert live.native_snapshot().shares_data(owner.snapshot())


def test_native_attachment_is_targeted_without_legacy_serialization(editor):
    owner = ij.Document(ij.Dataset(np.zeros((3, 4, 5)), axes="ZYX"))
    backend = editor._editor

    class TargetOnly:
        def documents(self):
            raise AssertionError("Native attachment must not serialize documents")

        def document_snapshot(self, *_):
            raise AssertionError("Native attachment must not serialize pixels")

        def __getattr__(self, name):
            return getattr(backend, name)

    editor._editor = TargetOnly()
    live = editor.add_native_document(owner)
    assert live.native_snapshot().shares_data(owner.snapshot())
    with pytest.raises(ValueError, match="already open"):
        editor.add_native_document(owner)
    assert len(editor.document_infos) == 1


def test_frontend_constraints_apply_to_headless_publication_until_detach(editor):
    owner = ij.Document(ij.Dataset(np.zeros((4, 5)), axes="YX"))
    live = editor.add_native_document(owner)
    before = live.info
    edit = owner.begin_edit()
    edit.set_extension("title", "Shadow")
    with pytest.raises(ValueError, match="Reserved desktop"):
        owner.commit(edit)
    assert edit.active and live.info == before
    view = owner.snapshot().display
    view.properties = {"unrecognized": True}
    with pytest.raises(ValueError, match="display property"):
        owner.set_display(view)
    assert live.info == before
    live.close(discard=True)
    owner.commit(edit)
    assert owner.snapshot().metadata["extensions"]["title"] == "Shadow"


def test_scientific_attachment_navigates_without_legacy_edits(editor):
    pixels = np.arange(2 * 3 * 4 * 5, dtype=np.uint16).reshape(2, 3, 4, 5)
    owner = ij.Document(ij.Dataset(np.zeros((3, 4, 5), np.uint16), axes="ZYX"))
    live = editor.add_native_document(owner)
    replacement = owner.begin_edit()
    replacement.replace_dataset(ij.Dataset(pixels, axes="CZYX"))
    owner.commit(replacement)
    assert not replacement.active
    assert live.info["axis_sizes"] == {"T": 1, "C": 2, "Z": 3, "Y": 4, "X": 5, "S": 1}
    assert live.info["scientific_read_only"] is False
    baseline = live.native_snapshot()
    live.set_view(t=0, c=1, crosshair=[2, 1, 1])
    assert live.view["t"] == 0 and live.view["c"] == 1
    np.testing.assert_array_equal(live.plane(), pixels[1, 1])
    np.testing.assert_array_equal(live.plane("xz", coordinate=1), pixels[1, :, 1, :])
    np.testing.assert_array_equal(live.plane("yz", coordinate=2), pixels[1, :, :, 2])
    assert live.native_snapshot().shares_data(baseline)
    assert owner.undo_steps == 1 and owner.redo_steps == 0
    guarded = live.info
    # Scientific threshold now has its own captured native command/UI contract
    # (P06l); these remaining actions still use the guarded legacy lane.
    for action in ("fill", "cut", "select_all", "measure"):
        with pytest.raises(ValueError, match="[Ss]cientific"):
            editor.action(action, document_id=live.id)
        assert live.info == guarded and live.native_snapshot().shares_data(baseline)
    with pytest.raises(ValueError, match="[Ss]cientific"):
        live.begin_edit()
    with pytest.raises(ValueError, match="[Ss]cientific"):
        live.apply_operation("add", {"value": 2}, asynchronous=False)
    with pytest.raises(ValueError, match="[Ss]cientific"):
        editor._editor.document_snapshot(live.id)
    assert live.info == guarded
    # The headless owner remains independently editable; the UI adapter must
    # refresh its selected C/Z pointer table without flattening the other C.
    external = owner.begin_edit()
    external.replace_plane(np.full((4, 5), 77, np.uint16), c=1, z=1)
    owner.commit(external)
    np.testing.assert_array_equal(live.plane(), 77)
    np.testing.assert_array_equal(live.native_snapshot().dataset.numpy("CZYX")[0], pixels[0])
    editor.action("undo", document_id=live.id)
    np.testing.assert_array_equal(live.plane(), pixels[1, 1])
    assert live.view["c"] == 1 and live.view["crosshair"] == [2, 1, 1]
    assert owner.undo_steps == 1 and owner.redo_steps == 1
    editor.action("redo", document_id=live.id)
    np.testing.assert_array_equal(live.plane(), 77)
    assert live.view["c"] == 1 and live.view["crosshair"] == [2, 1, 1]
    assert owner.undo_steps == 2 and owner.redo_steps == 0


def test_explicit_zero_positioned_roi_keeps_legacy_edit_guards(editor):
    owner = ij.Document(ij.Dataset(np.arange(20, dtype=np.uint16).reshape(4, 5), axes="YX"))
    edit = owner.begin_edit()
    edit.set_rois([ij.ROI("explicit zero", ij.Selection("rectangle", [(1, 1), (3, 3)]), t=0, c=0)])
    owner.commit(edit)
    live = editor.add_native_document(owner)
    assert live.info["axis_sizes"]["T"] == live.info["axis_sizes"]["C"] == 1
    assert live.info["scientific_read_only"] is False and live.view["t"] == live.view["c"] == 0
    before = live.info
    with pytest.raises(ValueError, match="[Ss]cientific"):
        live.begin_edit()
    with pytest.raises(ValueError, match="[Ss]cientific"):
        editor.action("fill", document_id=live.id)
    assert live.info == before
    roi = owner.snapshot().rois[0]
    assert roi.name == "explicit zero" and roi.t == roi.c == 0 and roi.z is None


def test_explicit_eager_source_commands_work_while_scientific_document_is_active(editor):
    eager_pixels = np.arange(60, dtype=np.uint16).reshape(3, 4, 5)
    eager_owner = ij.Document(ij.Dataset(eager_pixels, axes="ZYX"), "Targeted eager source")
    eager = editor.add_native_document(eager_owner)
    scientific_owner = ij.Document(ij.Dataset(np.arange(40, dtype=np.uint16).reshape(2, 4, 5), axes="CYX"))
    scientific = editor.add_native_document(scientific_owner)
    before = scientific.info
    scientific_snapshot, eager_snapshot = scientific.native_snapshot(), eager.native_snapshot()
    assert editor.live_document().id == scientific.id
    eager.show_threshold()
    rendered = editor.render_headless(frames=2)
    assert rendered["threshold_panel"]["open"] is True
    assert rendered["threshold_panel"]["source_id"] == eager.id
    assert editor.live_document().id == scientific.id
    assert scientific.info == before and scientific.native_snapshot().shares_data(scientific_snapshot)
    assert eager.native_snapshot().shares_data(eager_snapshot) and eager_owner.undo_steps == 0
    exported = eager.export_plane("xz", coordinate=2)
    np.testing.assert_array_equal(exported.plane(), eager_pixels[:, 2, :])
    assert exported.id not in {eager.id, scientific.id}
    assert len(editor.document_infos) == 3
    assert scientific.info == before and scientific.native_snapshot().shares_data(scientific_snapshot)
    assert eager.native_snapshot().shares_data(eager_snapshot) and eager_owner.undo_steps == 0


def test_attached_native_edits_obey_creator_thread_but_reads_are_headless(editor):
    owner = ij.Document(ij.Dataset(np.zeros((4, 5)), axes="YX"))
    live = editor.add_native_document(owner)
    before = live.native_snapshot()
    edit = owner.begin_edit()
    edit.set_title("Background")
    with ThreadPoolExecutor(max_workers=1) as pool:
        snapshot = pool.submit(owner.snapshot).result()
        assert snapshot.shares_data(before)
        with pytest.raises(RuntimeError, match="creator thread"):
            pool.submit(owner.commit, edit).result()
    assert edit.active and owner.snapshot().shares_data(before)
    live.close()
    with ThreadPoolExecutor(max_workers=1) as pool:
        pool.submit(owner.commit, edit).result()
    assert owner.snapshot().metadata["title"] == "Background"


def test_close_preserves_active_identity_and_retained_shared_records(editor):
    first = ij.Document(ij.Dataset(np.arange(20.0).reshape(4, 5), axes="YX"), "First")
    second = ij.Document(ij.Dataset(np.ones((4, 5)), axes="YX"), "Second")
    live = editor.add_native_document(first)
    source = live.native_snapshot()
    editor.add_native_document(second)
    old_edit = live.begin_edit()
    old_edit.set_title("Late")
    live.close()
    assert editor.live_document().id == second.id and editor.active_index == 0
    with pytest.raises(IndexError):
        live.commit(old_edit)
    with pytest.raises(IndexError):
        live.native_owner
    assert old_edit.active
    np.testing.assert_array_equal(source.dataset.plane(), np.arange(20.0).reshape(4, 5))
    first.commit(old_edit)
    assert first.snapshot().metadata["title"] == "Late"


def test_legacy_record_presence_survives_core_title_edits_and_sessions(
    editor, tmp_path
):
    from imagejxx.ui.model import Document, Selection

    initial = Document(np.ones((4, 5)), "Legacy")
    initial.selection = Selection("rectangle", [(0, 0), (2, 2)])
    editor.add_document(initial)
    live = editor.live_document()
    vendor = live.begin_edit()
    vendor.set_extension("vendor", {"flags": [True, None, 2**64 - 1]})
    live.commit(vendor)
    saved = json.loads(editor._editor.document_snapshot(live.id))
    edit = live.native_owner.begin_edit()
    edit.set_title("Renamed")
    live.native_owner.commit(edit)
    current = json.loads(editor._editor.document_snapshot(live.id))
    assert current["calibration"] == saved["calibration"]
    assert current["vendor"] == saved["vendor"] == {"flags": [True, None, 2**64 - 1]}
    assert current["selection"] == saved["selection"]
    path = tmp_path / "legacy.ijx"
    editor.action("save_workspace", path=str(path))
    editor.open_file(path)
    reopened = json.loads(editor._editor.document_snapshot(editor.live_document().id))
    assert reopened["calibration"] == current["calibration"]
    assert reopened["vendor"] == current["vendor"]
    assert reopened["selection"] == current["selection"]


def test_native_stack_legacy_session_preserves_all_planes(editor, tmp_path):
    pixels = np.arange(60, dtype=np.uint16).reshape(3, 4, 5)
    owner = ij.Document(ij.Dataset(pixels, axes="ZYX"), "Native stack")
    live = editor.add_native_document(owner)
    source = live.native_snapshot()
    path = tmp_path / "native-stack.ijx"
    editor.action("save_workspace", path=str(path))
    assert live.native_snapshot().shares_data(source)
    editor.open_file(path)
    reopened = editor.live_document().native_snapshot()
    np.testing.assert_array_equal(reopened.dataset.numpy("ZYX"), pixels)
    assert reopened.metadata["title"] == "Native stack"
