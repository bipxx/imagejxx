"""Headless application ownership, publication and live membership contracts."""

from concurrent.futures import ThreadPoolExecutor

import imagejxx as ij
import numpy as np
import pytest


def dataset(dtype=np.uint16):
    return ij.Dataset(np.arange(240).reshape(2, 2, 3, 4, 5).astype(dtype), axes="TCZYX")


def test_empty_registration_activation_and_close():
    app = ij.Application()
    assert app.document_ids == [] and app.active_document_id == 0
    assert not app.contains(0)
    for action in (app.snapshot, app.begin_edit, app.history, app.close):
        with pytest.raises(ij.DocumentUnavailable):
            action()
    first, second, third = [app.create(dataset(), name) for name in ("a", "b", "c")]
    assert app.document_ids == [first, second, third]
    app.close(first)
    assert app.active_document_id == third
    app.activate(second)
    app.close(second)
    assert app.active_document_id == third
    app.close(third)
    assert app.active_document_id == 0 and app.document_ids == []
    assert not app.contains(third)
    with pytest.raises(ij.DocumentUnavailable):
        app.activate(third)


@pytest.mark.parametrize("dtype", [np.uint8, np.uint16, np.float32, np.float64])
def test_adoption_shares_owner_and_survives_closed_membership(dtype):
    document = ij.Document(dataset(dtype), "External")
    app = ij.Application()
    identity = app.adopt(document)
    source = document.snapshot()
    assert app.snapshot(identity).shares_data(source)
    with pytest.raises(ValueError, match="already registered"):
        app.adopt(document)
    tx = document.begin_edit()
    tx.replace_plane(np.full((4, 5), 77, dtype=dtype), t=1, c=1, z=2)
    app.commit(tx)
    np.testing.assert_array_equal(
        document.snapshot().dataset.plane(coordinate=2, t=1, c=1), 77
    )
    assert app.history(identity).undo_steps == document.undo_steps == 1
    assert document.snapshot().dirty
    with pytest.raises(ValueError, match="unsaved"):
        app.close(identity)
    retained = app.snapshot(identity)
    pending = app.begin_edit(identity)
    app.close(identity, discard=True)
    assert not app.contains(identity)
    for action in (
        lambda: app.snapshot(identity),
        lambda: app.commit(pending),
        lambda: app.adopt(document),
    ):
        with pytest.raises(ij.DocumentUnavailable):
            action()
    assert pending.active
    assert document.snapshot().shares_data(retained)
    np.testing.assert_array_equal(retained.dataset.plane(coordinate=2, t=1, c=1), 77)
    document.undo()
    np.testing.assert_array_equal(
        document.snapshot().dataset.numpy("TCZYX"), source.dataset.numpy("TCZYX")
    )


@pytest.mark.parametrize("make_active", [None, False, True], ids=["default", "false", "true"])
def test_adopt_all_order_activation_keywords_and_empty_batch(make_active):
    app = ij.Application()
    owners = [ij.Document(ij.Dataset(np.full((2, 3), i, np.uint8), axes="YX"), str(i))
              for i in range(4)]
    options = {} if make_active is None else {"make_active": make_active}
    ids = app.adopt_all(documents=owners[:2], **options)
    assert ids == [owner.snapshot().id for owner in owners[:2]]
    assert app.document_ids == ids
    expected_active = ids[0] if make_active is False else ids[-1]
    assert app.active_document_id == expected_active
    appended = app.adopt_all(owners[2:], make_active=False)
    assert appended == [owner.snapshot().id for owner in owners[2:]]
    assert app.document_ids == ids + appended and app.active_document_id == expected_active
    assert app.adopt_all([], make_active=True) == []
    assert app.document_ids == ids + appended and app.active_document_id == expected_active
    for owner in owners:
        snapshot = owner.snapshot()
        assert app.snapshot(snapshot.id).shares_data(snapshot)
        assert not snapshot.dirty and owner.undo_steps == owner.redo_steps == 0


@pytest.mark.parametrize("invalid", ["none", "conversion", "duplicate", "existing", "closed"])
def test_adopt_all_python_conversion_and_late_invalid_member_are_atomic(invalid):
    def owner(title):
        return ij.Document(ij.Dataset(np.arange(6, dtype=np.uint8).reshape(2, 3), axes="YX"), title)

    app = ij.Application()
    existing, anchor, closed = [owner(title) for title in ("existing", "active", "closed")]
    existing_id, anchor_id = app.adopt_all([existing, anchor])
    closed_id = app.adopt(closed)
    app.close(closed_id)
    app.activate(anchor_id)
    incoming = owner("incoming")
    before_incoming = incoming.snapshot()
    before_ids, before_active = app.document_ids, app.active_document_id
    before_existing, before_anchor = app.snapshot(existing_id), app.snapshot(anchor_id)
    invalid_member = {"none": None, "conversion": object(), "duplicate": incoming,
                      "existing": existing, "closed": closed}[invalid]
    error = (TypeError if invalid == "conversion" else ij.DocumentUnavailable
             if invalid == "closed" else ValueError)
    with pytest.raises(error):
        app.adopt_all(documents=[incoming, invalid_member], make_active=True)
    assert app.document_ids == before_ids and app.active_document_id == before_active
    assert not app.contains(before_incoming.id) and not app.contains(closed_id)
    assert app.snapshot(existing_id).shares_data(before_existing)
    assert app.snapshot(anchor_id).shares_data(before_anchor)
    assert incoming.snapshot().shares_data(before_incoming)
    assert not incoming.snapshot().dirty and incoming.undo_steps == incoming.redo_steps == 0
    # Conversion/native rejection must leave the independent owner usable and
    # eligible for a later successful call through the same binding.
    edit = incoming.begin_edit()
    edit.set_title("usable after rejected batch")
    incoming.commit(edit)
    admitted = app.adopt_all([incoming], make_active=False)
    assert admitted == [before_incoming.id]
    assert app.document_ids == before_ids + admitted and app.active_document_id == before_active
    assert app.snapshot(admitted[0]).metadata["title"] == "usable after rejected batch"
    assert app.history(admitted[0]).undo_steps == incoming.undo_steps == 1


def test_unsaved_close_snapshot_save_and_newer_edits():
    app = ij.Application()
    identity = app.create(dataset())
    original = app.snapshot()
    tx = app.begin_edit()
    tx.set_title("First edit")
    app.commit(tx)
    saved = app.snapshot()
    assert saved.data_identity != original.data_identity
    assert not tx.active
    app.mark_saved(original)
    assert app.snapshot().dirty
    app.mark_saved(saved)
    assert not app.snapshot().dirty
    pending = app.begin_edit()
    pending.set_property("newer", True)
    app.commit(pending)
    app.mark_saved(saved)
    assert app.snapshot().dirty
    app.undo()
    assert not app.snapshot().dirty
    with pytest.raises(ValueError, match="committed snapshot"):
        app.mark_saved(app.begin_edit().preview())
    other = ij.Document(dataset()).snapshot()
    with pytest.raises(ij.DocumentUnavailable):
        app.mark_saved(other)
    app.close(identity)


@pytest.mark.parametrize("preserve", [False, True])
def test_atomic_data_display_publication_and_concurrent_navigation(preserve):
    app = ij.Application()
    identity = app.create(dataset())
    original = app.snapshot()
    tx = app.begin_edit()
    tx.set_title("Applied")
    invalid = original.display
    invalid.zoom = np.nan
    with pytest.raises(ValueError):
        app.commit(tx, display=invalid)
    assert tx.active and app.snapshot().shares_data(original)
    assert app.history().undo_steps == 0
    requested = original.display
    requested.t, requested.c, requested.crosshair, requested.zoom = 1, 1, [4, 3, 2], 3
    concurrent = original.display
    concurrent.zoom = 7
    app.set_display(concurrent, identity)
    app.commit(tx, display=requested, preserve_concurrent_display=preserve)
    committed = app.snapshot()
    assert committed.metadata["title"] == "Applied"
    assert committed.display.zoom == (7 if preserve else 3)
    assert app.history().undo_steps == 1
    app.undo()
    assert app.snapshot().display.zoom == committed.display.zoom
    assert not app.snapshot().dirty


def test_display_only_publication_keeps_data_storage_and_identity():
    document = ij.Document(dataset())
    original = document.snapshot()
    view = original.display
    view.zoom = 3
    view.properties = {"lut": "Fire", "range": [10, 20]}
    tx = document.begin_edit()
    document.commit(tx, display=view)
    snapshot = document.snapshot()
    assert snapshot.shares_data(original)
    assert snapshot.data_identity == original.data_identity
    assert snapshot.revisions.display == 1 and snapshot.revisions.pixels == 0
    assert not snapshot.dirty and document.history().undo_steps == 0


def test_foreign_stale_and_disjoint_edits():
    app = ij.Application()
    identity = app.create(dataset())
    other = ij.Document(dataset())
    foreign = other.begin_edit()
    with pytest.raises(ij.DocumentUnavailable):
        app.commit(foreign)
    assert foreign.active
    stale = app.begin_edit(identity)
    app.select(ij.Selection("rectangle", [(1, 1), (4, 3)]), identity)
    with pytest.raises(ij.RevisionConflict):
        app.commit(stale)
    assert stale.active
    first = app.begin_edit(identity, ij.Change.PIXELS)
    second = app.begin_edit(identity, ij.Change.PIXELS)
    first.set_extension("left", {"value": 1})
    second.set_extension("right", {"value": 2})
    app.commit(first)
    app.commit(second)
    assert app.snapshot().metadata["extensions"] == {
        "left": {"value": 1},
        "right": {"value": 2},
    }


def test_complete_snapshot_import_and_duplication():
    app = ij.Application()
    identity = app.create(dataset())
    tx = app.begin_edit()
    tx.select(ij.Selection("point", [(2, 1)], attributes={"color": [1, 2, 3]}))
    tx.set_rois(
        [
            ij.ROI(
                "r",
                ij.Selection("point", [(1, 1)]),
                t=1,
                c=1,
                z=2,
                attributes={"group": 2},
            )
        ]
    )
    tx.set_extension("unknown", {"nested": [2**63, None, "value"]})
    tx.set_overlays([{"kind": "text", "text": "retained"}])
    tx.set_spectrum(
        ij.FourierState(np.zeros((4, 8), np.complex128), original_shape=(4, 5))
    )
    app.commit(tx)
    view = app.snapshot().display
    view.zoom = 4
    view.properties = {"lut": "Fire", "range": [10, 20]}
    app.set_display(view)
    source = app.snapshot()
    duplicate_id = app.duplicate(identity)
    clones = [app.snapshot(duplicate_id), ij.Document(source).snapshot()]
    for clone in clones:
        assert clone.id != source.id
        assert clone.dataset.shares_pixels(source.dataset)
        assert clone.metadata == source.metadata and clone.overlays == source.overlays
        assert clone.selection.attributes == source.selection.attributes
        assert clone.rois[0].attributes == source.rois[0].attributes
        assert (clone.rois[0].t, clone.rois[0].c, clone.rois[0].z) == (1, 1, 2)
        assert clone.spectrum.shares_storage(source.spectrum)
        assert clone.display.properties == view.properties and clone.display.zoom == 4
        assert not clone.dirty and clone.data_identity == 0
    assert app.history(duplicate_id).undo_steps == 0


def test_eager_history_budget_shares_planes_and_excludes_external_snapshots():
    document = ij.Document(ij.Dataset(np.zeros((2, 4, 5), np.uint16), axes="ZYX"))
    app = ij.Application()
    identity = app.adopt(document)
    external = app.snapshot()
    app.set_history_policy(8, 160, identity)
    for value in range(1, 4):
        tx = app.begin_edit()
        tx.replace_plane(np.full((4, 5), value, np.uint16), z=1)
        app.commit(tx)
    history = app.history()
    assert history.undo_steps == 1 and history.redo_steps == 0
    assert history.pixels.exact
    assert history.pixels.logical_bytes == history.pixels.current_bytes == 320
    assert history.pixels.retained_bytes == 480 and history.pixels.obsolete_bytes == 160
    assert history.pixels.retained_planes == 3
    with pytest.raises(ValueError):
        app.set_history_policy(0, 1)
    assert app.history().step_limit == 8 and app.history().pixel_budget == 160
    app.set_history_policy(8, 159)
    assert document.undo_steps == 0 and app.history().pixels.obsolete_bytes == 0
    np.testing.assert_array_equal(external.dataset.numpy("ZYX"), 0)


def test_parallel_application_patches_preserve_unrelated_fields():
    app = ij.Application()
    identity = app.create(dataset())

    def worker(number):
        for index in range(40):
            tx = app.begin_edit(identity, ij.Change.PIXELS)
            tx.set_property(f"{number}:{index}", index)
            app.commit(tx)
            assert app.snapshot(identity).id == identity

    with ThreadPoolExecutor(max_workers=4) as pool:
        list(pool.map(worker, range(4)))
    assert len(app.snapshot().metadata["properties"]) == 160
    assert app.history().pixels.retained_planes == 12


def test_import_of_desktop_snapshot_retains_records_without_sharing_live_owner(
    tmp_path,
):
    pytest.importorskip("imagejxx._desktop", exc_type=ModuleNotFoundError)
    from imagejxx.ui.app import Editor
    from imagejxx.ui.model import Document
    from imagejxx.ui.persistence import Preferences

    editor = Editor(preferences=Preferences(tmp_path / "preferences.json"))
    editor.add_document(
        Document(np.zeros((2, 4, 5), np.uint16), "Desktop", is_stack=True)
    )
    live = editor.live_document()
    tx = live.begin_edit()
    tx.set_extension("unknown", {"values": [2**63, None, 1.5]})
    tx.set_rois(
        [
            ij.ROI(
                "styled",
                ij.Selection("point", [(2, 1)]),
                z=1,
                attributes={"color": [1, 2, 3]},
            )
        ]
    )
    tx.set_overlays([{"kind": "text", "text": "retained", "position": [1, 2]}])
    tx.set_spectrum(
        ij.FourierState(np.zeros((4, 8), np.complex128), original_shape=(4, 5))
    )
    live.commit(tx)
    view = live.native_snapshot().display
    view.crosshair, view.zoom = [2, 1, 1], 3
    live.set_display(view)
    source = live.native_snapshot()
    imported = ij.Document(source)
    app = ij.Application()
    identity = app.adopt(imported)
    result = app.snapshot(identity)
    assert result.id != source.id and result.dataset.shares_pixels(source.dataset)
    assert result.metadata == source.metadata and result.overlays == source.overlays
    assert result.rois[0].attributes == source.rois[0].attributes
    assert result.spectrum.shares_storage(source.spectrum)
    assert (
        result.display.properties == source.display.properties
        and result.display.zoom == 3
    )
    edit = app.begin_edit(identity)
    edit.set_title("Independent import")
    app.commit(edit)
    assert imported.snapshot().metadata["title"] == "Independent import"
    assert live.native_snapshot().metadata["title"] == "Desktop"
