"""Headless native transactions and the same contract on targeted UI documents."""

from concurrent.futures import ThreadPoolExecutor
import json

import imagejxx as ij
import numpy as np
import pytest


@pytest.fixture
def core():
    pixels = np.arange(240, dtype=np.uint16).reshape(2, 2, 3, 4, 5)
    return ij.Document(ij.Dataset(pixels, axes="TCZYX"), title="Original")


@pytest.mark.parametrize("dtype", [np.uint8, np.uint16, np.float32, np.float64])
def test_atomic_preview_commit_and_snapshot_lifetime(dtype):
    pixels = np.arange(60).reshape(3, 4, 5).astype(dtype)
    document = ij.Document(ij.Dataset(pixels, axes="ZYX"), "Before")
    source = document.snapshot()
    transaction = source.begin_edit()
    transaction.replace_plane(np.full((4, 5), 99, dtype=dtype), z=1)
    transaction.set_title("After")
    transaction.set_calibration(ij.SpatialCalibration(spacing=(0.5, 2, 3), unit="um"))
    preview = transaction.preview()
    assert document.snapshot().metadata["title"] == "Before"
    assert not source.dirty and document.undo_steps == 0
    np.testing.assert_array_equal(source.dataset.numpy("ZYX"), pixels)
    np.testing.assert_array_equal(preview.dataset.plane(coordinate=1), 99)
    document.commit(transaction)
    committed = document.snapshot()
    assert not transaction.active and document.undo_steps == 1 and committed.dirty
    assert committed.revisions.pixels == committed.revisions.metadata == 1
    assert committed.revisions.selection == committed.revisions.display == 0
    assert committed.dataset.calibration.spacing == [0.5, 2, 3]
    detached = committed.dataset.plane(coordinate=1)
    detached[:] = 0
    np.testing.assert_array_equal(committed.dataset.plane(coordinate=1), 99)
    document.undo()
    np.testing.assert_array_equal(document.snapshot().dataset.numpy("ZYX"), pixels)
    assert not document.snapshot().dirty
    document.redo()
    document.mark_saved()
    assert not document.snapshot().dirty
    del document
    np.testing.assert_array_equal(committed.dataset.plane(coordinate=1), 99)


def test_transaction_rollback_and_noop(core):
    transaction = core.begin_edit()
    transaction.set_title("Discard")
    transaction.rollback()
    assert not transaction.active and core.undo_steps == 0
    with pytest.raises(ValueError, match="closed"):
        core.commit(transaction)
    with pytest.raises(ValueError, match="closed"):
        transaction.preview()
    assert core.snapshot().metadata["title"] == "Original"
    noop = core.begin_edit()
    noop.set_title("Original")
    core.commit(noop)
    assert not noop.active and core.undo_steps == 0 and not core.snapshot().dirty


def test_stale_selection_foreign_owner_and_display_dependencies(core):
    stale = core.begin_edit()
    core.select(ij.Selection("rectangle", [(1, 1), (4, 3)]))
    with pytest.raises(ij.RevisionConflict):
        core.commit(stale)
    assert stale.active and core.undo_steps == 0 and not core.snapshot().dirty
    other = ij.Document(core.snapshot().dataset)
    transaction = core.begin_edit()
    with pytest.raises(ValueError, match="another document"):
        other.commit(transaction)
    assert transaction.active
    with_display = core.begin_edit(ij.Change.ALL)
    view = core.snapshot().display
    view.t, view.c, view.crosshair, view.zoom = 1, 1, [4, 3, 2], 3
    core.set_display(view)
    core.commit(transaction)
    with pytest.raises(ij.RevisionConflict):
        core.commit(with_display)
    assert core.undo_steps == 0 and not core.snapshot().dirty
    assert core.snapshot().revisions.display == 1


def test_narrow_dependencies_merge_disjoint_planes_and_metadata(core):
    first = core.begin_edit(ij.Change.METADATA)
    second = core.begin_edit(ij.Change.METADATA)
    first.replace_plane(np.full((4, 5), 101, np.uint16), z=1)
    second.replace_plane(np.full((4, 5), 202, np.uint16), t=1, c=1, z=1)
    core.commit(first)
    core.commit(second)
    np.testing.assert_array_equal(core.snapshot().dataset.plane(coordinate=1), 101)
    np.testing.assert_array_equal(
        core.snapshot().dataset.plane(coordinate=1, t=1, c=1), 202
    )
    a = core.begin_edit(ij.Change.PIXELS)
    b = core.begin_edit(ij.Change.PIXELS)
    a.set_property("first", 1)
    b.set_property("second", False)
    core.commit(a)
    core.commit(b)
    assert core.snapshot().metadata["properties"] == {"first": 1, "second": False}
    assert core.snapshot().revisions.pixels == core.snapshot().revisions.metadata == 2


@pytest.mark.parametrize(
    "value",
    [
        None,
        True,
        False,
        12,
        -(2**63),
        2**63 - 1,
        2**64 - 1,
        1.25,
        "text",
        ["a", "b"],
        [],
        [1, None, False, 2.5],
        {"": [], "nested": {"values": [-(2**63), 2**64 - 1, "µm"]}},
    ],
)
def test_typed_metadata(core, value):
    transaction = core.begin_edit()
    transaction.set_property("field", value)
    core.commit(transaction)
    properties = core.snapshot().metadata["properties"]
    assert properties["field"] == value and type(properties["field"]) is type(value)
    properties["field"] = "detached"
    assert core.snapshot().metadata["properties"]["field"] == value
    remove = core.begin_edit()
    remove.remove_property("field")
    core.commit(remove)
    assert core.snapshot().metadata["properties"] == {}


@pytest.mark.parametrize(
    "value",
    [
        np.nan,
        np.inf,
        2**80,
        -(2**63) - 1,
        {"nested": np.nan},
        [np.inf],
        {1: "bad key"},
        {"x": object()},
        object(),
    ],
)
def test_invalid_metadata_atomicity(core, value):
    transaction = core.begin_edit()
    with pytest.raises((ValueError, OverflowError, TypeError)):
        transaction.set_property("invalid", value)
    assert transaction.preview().metadata["properties"] == {}
    assert core.snapshot().metadata["properties"] == {} and core.undo_steps == 0


def test_positioned_rois_masks_and_selection_revisions(core):
    rectangle = ij.Selection("rectangle", [(1, 1), (4, 3)])
    mask = rectangle.mask((4, 5))
    np.testing.assert_array_equal(ij.Selection.from_mask(mask).mask((4, 5)), mask)
    transaction = core.begin_edit()
    transaction.select(rectangle)
    transaction.set_rois([ij.ROI("sample", rectangle, t=1, c=1, z=2)])
    core.commit(transaction)
    snapshot = core.snapshot()
    roi = snapshot.rois[0]
    assert (roi.t, roi.c, roi.z) == (1, 1, 2)
    assert snapshot.revisions.metadata == snapshot.revisions.selection == 1
    assert snapshot.revisions.pixels == 0 and core.undo_steps == 1
    with pytest.raises(IndexError):
        core.begin_edit().set_rois([ij.ROI("outside", rectangle, t=2)])

    # P05e admits valid off-canvas geometry; only its raster is clipped.
    outside = ij.Selection("rectangle", [(10, 10), (12, 12)])
    outside_edit = core.begin_edit()
    outside_edit.select(outside)
    preview = outside_edit.preview()
    assert preview.selection.points == outside.points
    np.testing.assert_array_equal(preview.selection.mask((4, 5)), np.zeros((4, 5)))
    assert core.snapshot().selection.points == rectangle.points
    core.commit(outside_edit)
    selected = core.snapshot()
    assert selected.selection.points == outside.points
    assert selected.previous_selection.points == rectangle.points
    assert selected.revisions.selection == snapshot.revisions.selection + 1
    assert selected.revisions.pixels == snapshot.revisions.pixels
    assert selected.revisions.metadata == snapshot.revisions.metadata
    assert selected.revisions.display == snapshot.revisions.display
    assert selected.data_identity == snapshot.data_identity
    assert selected.dirty == snapshot.dirty and core.undo_steps == 1 and core.redo_steps == 0
    np.testing.assert_array_equal(selected.dataset.numpy("TCZYX"), snapshot.dataset.numpy("TCZYX"))

    for points in ([(1, 1)], [(1, 1), (1, 3)],
                   [(10, 10), (np.inf, 12)], [(10, np.nan), (12, 12)]):
        invalid = core.begin_edit()
        with pytest.raises(ValueError):
            invalid.select(ij.Selection("rectangle", points))
        assert invalid.active and invalid.preview().selection.points == outside.points
    assert core.snapshot().revisions.selection == selected.revisions.selection
    assert core.undo_steps == 1 and core.redo_steps == 0
    np.testing.assert_array_equal(
        ij.Selection("rectangle", [(-1e100, -1e100), (1e100, 1e100)]).mask((4, 5)), 255
    )


def test_display_detached_validation_and_undo_survival(core):
    view = core.snapshot().display
    view.crosshair, view.zoom = [4, 3, 2], 2
    assert core.snapshot().display.zoom == 0
    core.set_display(view)
    transaction = core.begin_edit()
    transaction.set_title("changed")
    core.commit(transaction)
    core.undo()
    assert core.snapshot().display.crosshair == [4, 3, 2]
    assert core.snapshot().display.zoom == 2
    view.zoom = np.nan
    with pytest.raises(ValueError):
        core.set_display(view)
    assert core.snapshot().display.zoom == 2


def test_concurrent_snapshots_and_disjoint_metadata_commits(core):
    def update(index):
        edit = core.begin_edit(ij.Change.PIXELS)
        edit.set_property(f"field{index}", index)
        core.commit(edit)
        return core.snapshot().dataset.plane(coordinate=2, t=1, c=1)[0, 0]

    with ThreadPoolExecutor(max_workers=4) as pool:
        result = list(pool.map(update, range(40)))
    assert len(core.snapshot().metadata["properties"]) == 40
    assert result == [220] * 40 and core.undo_steps == 8
    core.set_history_limit(2)
    assert core.undo_steps == 2
    with pytest.raises(ValueError):
        core.set_history_limit(0)


@pytest.fixture
def editor(tmp_path):
    pytest.importorskip("imagejxx._desktop", exc_type=ModuleNotFoundError)
    from imagejxx.ui.app import Editor
    from imagejxx.ui.persistence import Preferences

    return Editor(preferences=Preferences(tmp_path / "preferences.json"))


def add_stack(editor):
    from imagejxx.ui.model import Document

    pixels = np.arange(60, dtype=np.uint16).reshape(3, 4, 5)
    editor.add_document(Document(pixels, "First", is_stack=True))
    return editor.live_document(), pixels


def test_ui_transactions_target_one_document_without_serialization(editor):
    from imagejxx.ui.model import Document

    first, pixels = add_stack(editor)
    editor.add_document(Document(np.zeros((4, 5), np.uint16), "Other"))
    second = editor.live_document()
    backend = editor._editor

    class TargetOnly:
        def documents(self):
            raise AssertionError("Must not serialize all documents")

        def document_snapshot(self, *_):
            raise AssertionError("Native snapshots must not serialize pixels")

        def __getattr__(self, name):
            return getattr(backend, name)

    editor._editor = TargetOnly()
    snapshot = first.native_snapshot()
    assert isinstance(snapshot, ij.DocumentSnapshot) and snapshot.id == first.id
    transaction = first.begin_edit()
    transaction.replace_plane(np.full((4, 5), 99, np.uint16), z=1)
    transaction.set_title("Edited")
    before = first.info
    first.commit(transaction)
    assert first.info["undo_steps"] == before["undo_steps"] + 1
    assert first.info["title"] == "Edited" and second.info["title"] == "Other"
    assert editor.active_index == 1
    np.testing.assert_array_equal(snapshot.dataset.numpy("ZYX"), pixels)
    np.testing.assert_array_equal(
        first.native_snapshot().dataset.plane(coordinate=1), 99
    )
    assert first.info["pixel_storage"]["plane_buffers"] == 4
    foreign = second.begin_edit()
    with pytest.raises(ValueError, match="another document"):
        first.commit(foreign)


def test_ui_stale_commits_and_scientific_transition_are_atomic(editor):
    live, pixels = add_stack(editor)
    stale = live.begin_edit()
    live.apply_operation("add", {"value": 1}, asynchronous=False)
    with pytest.raises(ij.RevisionConflict):
        live.commit(stale)
    assert stale.active
    before = live.info
    invalid = live.begin_edit()
    with pytest.raises(ValueError, match="Sample axis|samples|components"):
        invalid.replace_dataset(ij.Dataset(np.zeros((4, 5, 2), np.uint16), axes="YXS"))
    assert invalid.active and live.info == before
    expected = pixels.copy()
    expected[0] += 1
    np.testing.assert_array_equal(live.native_snapshot().dataset.numpy("ZYX"), expected)
    scientific = live.begin_edit()
    scientific_pixels = np.arange(120, dtype=np.uint16).reshape(2, 3, 4, 5)
    scientific.replace_dataset(ij.Dataset(scientific_pixels, axes="CZYX"))
    live.commit(scientific)
    assert not scientific.active and live.info["scientific_read_only"] is False
    np.testing.assert_array_equal(live.native_snapshot().dataset.numpy("CZYX"), scientific_pixels)
    read_only = live.info
    with pytest.raises(ValueError, match="[Ss]cientific"):
        live.begin_edit()
    assert live.info == read_only
    # Native undo returns the original eager-Z data exactly. An incoming
    # native positioned ROI is admitted without erasing its optional zero.
    live.native_owner.undo()
    np.testing.assert_array_equal(live.native_snapshot().dataset.numpy("ZYX"), expected)
    roi = live.begin_edit()
    roi.set_rois([ij.ROI("positioned", ij.Selection("point", [(1, 1)]), t=0, c=0)])
    live.commit(roi)
    assert not roi.active and live.info["scientific_read_only"] is False
    stored = live.native_snapshot().rois[0]
    assert stored.t == stored.c == 0 and stored.z is None
    np.testing.assert_array_equal(live.native_snapshot().dataset.numpy("ZYX"), expected)


def test_ui_metadata_roi_extensions_and_session_roundtrip(editor, tmp_path):
    live, _ = add_stack(editor)
    path = tmp_path / "rois.json"
    path.write_text(
        json.dumps(
            {
                "format": "imagejxx-rois",
                "version": 1,
                "rois": [
                    {
                        "name": "same",
                        "selection": {"kind": "point", "points": [[1, 1]], "width": 1},
                        "slice_index": 1,
                        "color": "red",
                        "extension": {"a": 1},
                    },
                    {
                        "name": "same",
                        "selection": {"kind": "point", "points": [[2, 2]], "width": 1},
                        "slice_index": 2,
                        "color": "blue",
                        "extension": {"b": 2},
                    },
                ],
            }
        )
    )
    editor.action("load_rois", path=str(path))
    snapshot = live.native_snapshot()
    edit = live.begin_edit()
    edit.set_rois(list(reversed(snapshot.rois)))
    edit.set_property("comment", "retained")
    live.commit(edit)
    saved = live.snapshot()
    assert [roi.to_dict()["name"] for roi in saved.rois] == ["same", "same"]
    editor.action("save_rois", path=str(path))
    rois = json.loads(path.read_text())["rois"]
    assert [item["color"] for item in rois] == ["blue", "red"]
    assert rois[0]["extension"] == {"b": 2}
    session = tmp_path / "document.ijx"
    editor.action("save_workspace", path=str(session))
    editor.open_file(session)
    restored = editor.live_document().native_snapshot()
    assert restored.metadata["properties"] == {"comment": "retained"}
    assert [roi.z for roi in restored.rois] == [2, 1]


def test_display_changes_do_not_dirty_or_undo_pixels_and_survive_undo(editor, tmp_path):
    live, pixels = add_stack(editor)
    editor.action("save_workspace", path=str(tmp_path / "clean.ijx"))
    before = live.info
    editor.action("display_range", minimum=2, maximum=50)
    editor.action("lut", name="Fire")
    after = live.info
    assert not after["dirty"] and after["undo_steps"] == before["undo_steps"]
    for category in ["pixels", "metadata", "selection"]:
        assert after["revisions"][category] == before["revisions"][category]
    assert after["revisions"]["display"] == before["revisions"]["display"] + 2
    np.testing.assert_array_equal(live.native_snapshot().dataset.numpy("ZYX"), pixels)
    live.apply_operation("add", {"value": 1}, asynchronous=False)
    editor.action("display_range", minimum=3, maximum=40)
    editor.action("lut", name="Ice")
    editor.action("undo")
    assert live.snapshot().display_range == (3, 40)
    assert live.snapshot().lut == "Ice"
    editor.action("redo")
    assert live.snapshot().display_range == (3, 40) and live.snapshot().lut == "Ice"


def test_display_and_navigation_during_async_edit_preserve_target(editor):
    live, pixels = add_stack(editor)
    live.set_view(crosshair=[2, 1, 1])
    live.apply_operation("add", {"value": 5}, asynchronous=True)
    assert editor.busy
    live.set_view(crosshair=[3, 2, 2])
    editor.action("display_range", minimum=3, maximum=40)
    editor.action("lut", name="Fire")
    editor.wait()
    expected = pixels.copy()
    expected[1] += 5
    np.testing.assert_array_equal(live.native_snapshot().dataset.numpy("ZYX"), expected)
    assert live.view["crosshair"] == [3, 2, 2]
    assert live.snapshot().display_range == (3, 40) and live.snapshot().lut == "Fire"


def test_legacy_action_keyword_and_separate_name_parameter(editor):
    live, _ = add_stack(editor)
    editor.action(name="select_all")
    assert live.native_snapshot().selection.kind == "rectangle"
    editor.action("add_roi", name="Named sample")
    assert live.native_snapshot().rois[0].name == "Named sample"
    with pytest.raises(TypeError, match="requires an action name"):
        editor.action()


def test_ui_creator_thread_and_closed_document_transactions(editor):
    live, pixels = add_stack(editor)
    snapshot = live.native_snapshot()
    transaction = live.begin_edit()
    with ThreadPoolExecutor(max_workers=1) as pool:
        with pytest.raises(RuntimeError, match="thread that created"):
            pool.submit(live.native_snapshot).result()
        # Immutable native snapshots can be consumed outside the editor thread.
        result = pool.submit(snapshot.dataset.numpy, "ZYX").result()
    np.testing.assert_array_equal(result, pixels)
    live.apply_operation("add", {"value": 1}, asynchronous=False)
    with pytest.raises(ValueError, match="explicitly discard"):
        live.close()
    assert live.info["title"] == "First"
    live.close(discard=True)
    with pytest.raises(IndexError):
        live.commit(transaction)
    assert transaction.active
    np.testing.assert_array_equal(snapshot.dataset.numpy("ZYX"), pixels)


def test_close_nonactive_document_preserves_active_tab_and_other_handles(editor):
    from imagejxx.ui.model import Document

    first, _ = add_stack(editor)
    editor.add_document(Document(np.zeros((4, 5), np.uint16), "Second"))
    second = editor.live_document()
    editor.add_document(Document(np.zeros((4, 5), np.uint16), "Third"))
    third = editor.live_document()
    editor.active_index = 1
    first.close(discard=True)
    assert editor.live_document().id == second.id and editor.active_index == 0
    assert third.info["title"] == "Third"
    with pytest.raises(IndexError):
        first.native_snapshot()
