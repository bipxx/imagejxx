"""Atomic captured-owner display publication through both native owners."""

from concurrent.futures import ThreadPoolExecutor

import imagejxx as ij
import numpy as np
import pytest

EXPECTED_COLLECTION_COUNT = 13


@pytest.fixture(params=["document", "application"])
def owner(request):
    document = ij.Document(ij.Dataset(np.arange(20, dtype=np.uint16).reshape(4, 5), axes="YX"))
    if request.param == "application":
        application = ij.Application()
        application.adopt(document)
        return document, application
    return document, document


def revisions(snapshot):
    r = snapshot.revisions
    return r.pixels, r.metadata, r.selection, r.display


def test_equal_and_changed_guarded_display_preserve_history(owner):
    document, publisher = owner
    edit = document.begin_edit()
    edit.set_title("Redo remains available")
    document.commit(edit)
    document.undo()
    source = document.snapshot()
    display = source.display
    display.zoom = 2
    publisher.set_display_if_current(source, display)
    after = document.snapshot()
    assert revisions(after) == (*revisions(source)[:3], source.revisions.display + 1)
    assert after.display.revision == source.display.revision + 1
    assert after.data_identity == source.data_identity and after.dirty == source.dirty
    assert document.undo_steps == 0 and document.redo_steps == 1
    np.testing.assert_array_equal(after.dataset.numpy("YX"), source.dataset.numpy("YX"))
    publisher.set_display_if_current(after, after.display)
    assert document.snapshot().revisions.display == after.revisions.display + 1
    with pytest.raises(ij.RevisionConflict):
        publisher.set_display_if_current(source, source.display)


@pytest.mark.parametrize("change", ["pixels", "metadata", "selection", "display"])
def test_every_changed_dependency_refuses_without_publication(owner, change):
    document, publisher = owner
    source = document.snapshot()
    if change == "selection":
        document.select(ij.Selection("rectangle", [[0, 0], [2, 2]]))
    elif change == "display":
        view = source.display
        view.zoom = 3
        document.set_display(view)
    else:
        edit = document.begin_edit()
        if change == "metadata":
            edit.set_title("New metadata")
        else:
            edit.replace_plane(np.full((4, 5), 7, dtype=np.uint16))
        document.commit(edit)
    current = document.snapshot()
    history = document.undo_steps, document.redo_steps, current.data_identity, current.dirty
    with pytest.raises(ij.RevisionConflict):
        publisher.set_display_if_current(source, source.display)
    after = document.snapshot()
    assert revisions(after) == revisions(current)
    assert (document.undo_steps, document.redo_steps, after.data_identity, after.dirty) == history
    assert after.display.zoom == current.display.zoom


def test_only_one_concurrent_publisher_consumes_a_capture(owner):
    document, publisher = owner
    source = document.snapshot()

    def publish(_):
        try:
            publisher.set_display_if_current(source, source.display)
            return "published"
        except ij.RevisionConflict:
            return "stale"

    with ThreadPoolExecutor(max_workers=2) as pool:
        assert sorted(pool.map(publish, range(2))) == ["published", "stale"]
    assert document.snapshot().revisions.display == source.revisions.display + 1


def test_guarded_application_keeps_captured_membership():
    application = ij.Application()
    first = application.create(ij.Dataset(np.zeros((2, 3), dtype=np.uint8), axes="YX"))
    source = application.snapshot(first)
    other = application.create(ij.Dataset(np.ones((2, 3), dtype=np.uint8), axes="YX"))
    display = source.display
    display.zoom = 4
    application.set_display_if_current(source, display)
    assert application.active_document_id == other
    assert application.snapshot(first).display.zoom == 4
    assert application.snapshot(other).display.zoom == 0
    captured = application.snapshot(first)
    application.close(first)
    with pytest.raises(ij.DocumentUnavailable):
        application.set_display_if_current(captured, captured.display)
    assert application.active_document_id == other
    assert application.snapshot(other).display.zoom == 0
