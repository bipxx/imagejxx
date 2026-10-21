"""Public Python interop for exact staged rank owners and retained READY data.

Expected pixels are a literal radius-zero minimum cross on the fixed UInt16
input. NumPy plane results are owned copies, never native reservation aliases.
These tests establish no desktop, installed, platform or performance claim.
"""

import gc

import imagejxx as ij
import numpy as np
import pytest


INPUT = np.array([[1, 8, 3], [4, 9, 6], [7, 2, 5]], dtype=np.uint16)
MINIMUM = np.array([[1, 1, 3], [1, 2, 3], [2, 2, 2]], dtype=np.uint16)


@pytest.fixture
def staged_owner():
    owner = ij.Document(ij.Dataset(INPUT, axes="YX"), "Rank source")
    app = ij.Application()
    app.adopt(owner)
    scheduler = app.job_scheduler
    scheduler.set_limits(ij.JobLimits(workers=1, memory_budget=0, capacity=16))
    try:
        yield app, owner, scheduler
    finally:
        scheduler.shutdown()


def command(owner, *, new_document=True):
    request = ij.TargetRequest(
        roi=ij.RoiPolicy.WHOLE_PLANE,
        destination=(ij.OutputDestination.NEW_DOCUMENT if new_document
                     else ij.OutputDestination.IN_PLACE),
    )
    return ij.resolve_rank_command(owner.snapshot(), request,
                                   ij.RankFilterCommandOptions("minimum", 0))


def prepare(app, owner):
    resolved = command(owner)
    return app.prepare("rank_filter_imagej", resolved.target, resolved.parameters())


def submit(app, owner):
    resolved = command(owner)
    job = app.submit(
        "rank_filter_imagej", resolved.target, resolved.parameters(),
        job_options=ij.JobOptions(retain_result_resources=True),
    )
    assert job.wait(5000), "Required native rank job did not terminate"
    assert job.info.state == ij.JobState.READY
    assert job.info.resource_bytes > 0
    return job


def ledger(scheduler, expected):
    actual = scheduler.usage
    assert actual.running == actual.running_bytes == 0
    assert actual.retained_bytes == expected
    assert actual.reserved_bytes == expected


def unchanged(owner, before, *, undo=0, redo=0):
    after = owner.snapshot()
    assert after.shares_data(before)
    assert after.revisions.matches(before.revisions, ij.Change.ALL)
    assert after.dirty == before.dirty
    assert owner.undo_steps == undo
    assert owner.redo_steps == redo


def test_staged_rank_explicit_title_exact_owner_and_atomic_adoption(staged_owner):
    app, owner, scheduler = staged_owner
    before = owner.snapshot()
    prepared = prepare(app, owner)
    assert prepared.prepared_new_document() is None
    candidate = prepared.stage_new_document("Rank output")
    assert candidate.id != owner.id
    assert not app.contains(candidate.id)
    assert prepared.prepared_new_document() is candidate
    assert prepared.stage_new_document() is candidate
    assert prepared.stage_new_document("Rank output") is candidate
    assert candidate.snapshot().metadata["title"] == "Rank output"
    assert candidate.undo_steps == 1 and candidate.redo_steps == 0
    assert candidate.snapshot().dirty
    np.testing.assert_array_equal(candidate.snapshot().dataset.plane(), MINIMUM)
    with pytest.raises(ValueError):
        prepared.stage_new_document("Different title")
    with pytest.raises(ValueError):
        app.publish(prepared, title="Different title")
    assert prepared.active and not app.contains(candidate.id)
    result = app.publish(prepared)
    assert result.created
    assert result.document is candidate
    assert result.document_id == candidate.id == app.active_document_id
    assert app.snapshot(candidate.id).shares_data(candidate.snapshot())
    assert not prepared.active
    unchanged(owner, before)
    candidate.undo()
    np.testing.assert_array_equal(candidate.snapshot().dataset.plane(), INPUT)
    assert candidate.snapshot().metadata["title"] == "Rank source"
    assert not candidate.snapshot().dirty
    candidate.redo()
    np.testing.assert_array_equal(candidate.snapshot().dataset.plane(), MINIMUM)
    with pytest.raises(ValueError):
        prepared.stage_new_document()
    ledger(scheduler, 0)


def test_staged_rank_default_title_freeze_and_unstaged_compatibility(staged_owner):
    app, owner, scheduler = staged_owner
    prepared = prepare(app, owner)
    candidate = prepared.stage_new_document()
    title = "Rank source (rank_filter_imagej)"
    assert candidate.snapshot().metadata["title"] == title
    assert prepared.stage_new_document(title) is candidate
    with pytest.raises(ValueError):
        prepared.stage_new_document("Rank source")
    result = app.publish(prepared, title=title)
    assert result.document is candidate
    unstaged = prepare(app, owner)
    assert unstaged.prepared_new_document() is None
    legacy = app.publish(unstaged, title="Explicit unstaged output")
    assert legacy.created and legacy.document.id not in (owner.id, candidate.id)
    assert legacy.document.snapshot().metadata["title"] == "Explicit unstaged output"
    assert legacy.document.undo_steps == 1
    np.testing.assert_array_equal(legacy.document.snapshot().dataset.plane(), MINIMUM)
    ledger(scheduler, 0)


def test_staged_rank_rejects_inplace_and_unsupported_operations(staged_owner):
    app, owner, scheduler = staged_owner
    before = owner.snapshot()
    resolved = command(owner, new_document=False)
    in_place = app.prepare("rank_filter_imagej", resolved.target, resolved.parameters())
    with pytest.raises(ValueError):
        in_place.stage_new_document()
    assert in_place.prepared_new_document() is None
    request = ij.TargetRequest(roi=ij.RoiPolicy.WHOLE_PLANE,
                               destination=ij.OutputDestination.NEW_DOCUMENT)
    target = app.resolve_target(request, document_id=owner.id)
    unsupported = app.prepare("add", target, {"value": 1})
    with pytest.raises(ValueError):
        unsupported.stage_new_document()
    assert unsupported.prepared_new_document() is None
    assert in_place.active and unsupported.active
    assert app.document_ids == [owner.id]
    unchanged(owner, before)
    ledger(scheduler, 0)


def test_staged_rank_configured_history_pruning_and_unbounded_budget(staged_owner):
    app, owner, scheduler = staged_owner
    before = owner.snapshot()
    for budget, widen in ((0, False), (1, False), (1, True)):
        prepared = prepare(app, owner)
        candidate = prepared.stage_new_document("Configured history")
        staged = candidate.snapshot()
        assert candidate.undo_steps == 1
        with pytest.raises(ValueError):
            candidate.set_history_policy(0, 1)
        candidate.set_history_policy(3, budget)
        if widen:
            candidate.set_history_policy(5, 0)
        expected_undo = 1 if budget == 0 else 0
        assert candidate.undo_steps == expected_undo and candidate.redo_steps == 0
        configured = candidate.snapshot()
        assert configured.shares_data(staged)
        assert configured.data_identity == staged.data_identity
        assert configured.revisions.matches(staged.revisions, ij.Change.ALL)
        assert configured.dirty == staged.dirty
        history = candidate.history()
        assert history.step_limit == (5 if widen else 3)
        assert history.pixel_budget == (0 if widen else budget)
        result = app.publish(prepared)
        assert result.document is candidate
        assert candidate.undo_steps == expected_undo
        assert candidate.snapshot().dirty
        np.testing.assert_array_equal(candidate.snapshot().dataset.plane(), MINIMUM)
        unchanged(owner, before)
    ledger(scheduler, 0)


def test_staged_rank_candidate_edits_undo_and_save_refuse_without_membership_change(staged_owner):
    app, owner, scheduler = staged_owner
    before = owner.snapshot()
    for mutation in ("pixels", "undo_redo", "mark_saved"):
        prepared = prepare(app, owner)
        candidate = prepared.stage_new_document()
        if mutation == "pixels":
            edit = candidate.begin_edit()
            edit.replace_plane(np.full((3, 3), 123, dtype=np.uint16))
            candidate.commit(edit)
        elif mutation == "undo_redo":
            candidate.undo()
            candidate.redo()
        else:
            candidate.mark_saved()
        candidate.set_history_policy(3, 1)
        changed = candidate.snapshot()
        ids, active = app.document_ids, app.active_document_id
        with pytest.raises(ij.RevisionConflict):
            app.publish(prepared)
        assert prepared.active
        assert app.document_ids == ids and app.active_document_id == active
        assert not app.contains(candidate.id)
        assert candidate.snapshot().shares_data(changed)
        unchanged(owner, before)
    ledger(scheduler, 0)


def test_staged_rank_ready_cancel_keeps_candidate_then_dataset_lease(staged_owner):
    app, owner, scheduler = staged_owner
    before = owner.snapshot()
    job = submit(app, owner)
    reservation = job.info.resource_bytes
    prepared = job.prepared
    candidate = prepared.stage_new_document()
    ledger(scheduler, reservation)
    assert job.cancel() and job.info.state == ij.JobState.CANCELED
    with pytest.raises(ij.OperationCancelled):
        _ = job.prepared
    with pytest.raises(ij.OperationCancelled):
        app.publish(prepared)
    prepared = None
    gc.collect()
    ledger(scheduler, reservation)
    assert not app.contains(candidate.id)
    np.testing.assert_array_equal(candidate.snapshot().dataset.plane(), MINIMUM)
    dataset = candidate.snapshot().dataset
    owned_numpy = dataset.plane()
    candidate = None
    gc.collect()
    ledger(scheduler, reservation)
    np.testing.assert_array_equal(dataset.plane(), MINIMUM)
    dataset = None
    gc.collect()
    ledger(scheduler, 0)
    np.testing.assert_array_equal(owned_numpy, MINIMUM)
    unchanged(owner, before)


def test_staged_rank_ready_adoption_detaches_owner_retains_old_export_aliases(staged_owner):
    app, owner, scheduler = staged_owner
    before = owner.snapshot()
    job = submit(app, owner)
    reservation = job.info.resource_bytes
    prepared = job.prepared
    second_prepared = job.prepared
    candidate = prepared.stage_new_document("Retained output")
    assert second_prepared.stage_new_document() is candidate
    exported = candidate.snapshot()
    dataset = exported.dataset
    owned_numpy = dataset.plane()
    ledger(scheduler, reservation)
    result = app.publish(job)
    assert job.info.state == ij.JobState.COMPLETED
    assert result.created and result.document is candidate
    assert app.snapshot(candidate.id).shares_data(candidate.snapshot())
    assert not candidate.snapshot().shares_data(exported)
    assert candidate.snapshot().dataset.shares_pixels(dataset)
    np.testing.assert_array_equal(candidate.snapshot().dataset.plane(), MINIMUM)
    with pytest.raises(ValueError):
        _ = job.prepared
    prepared = second_prepared = None
    gc.collect()
    ledger(scheduler, reservation)
    exported = None
    gc.collect()
    ledger(scheduler, reservation)
    dataset = None
    gc.collect()
    ledger(scheduler, 0)
    np.testing.assert_array_equal(owned_numpy, MINIMUM)
    candidate.undo()
    np.testing.assert_array_equal(candidate.snapshot().dataset.plane(), INPUT)
    candidate.redo()
    np.testing.assert_array_equal(candidate.snapshot().dataset.plane(), MINIMUM)
    ledger(scheduler, 0)
    unchanged(owner, before)


def test_staged_rank_noop_output_preserves_source_redo(staged_owner):
    app, original, scheduler = staged_owner
    constant = np.full((3, 3), 7, dtype=np.uint16)
    owner = ij.Document(ij.Dataset(constant, axes="YX"), "Constant source")
    app.adopt(owner)
    edit = owner.begin_edit()
    edit.set_property("redo entry", True)
    owner.commit(edit)
    owner.undo()
    before = owner.snapshot()
    prepared = prepare(app, owner)
    assert prepared.preview.dataset.shares_pixels(before.dataset)
    candidate = prepared.stage_new_document("Unchanged rank output")
    assert candidate.snapshot().dataset.shares_pixels(before.dataset)
    result = app.publish(prepared)
    assert result.document is candidate
    assert candidate.undo_steps == 1 and candidate.snapshot().dirty
    np.testing.assert_array_equal(candidate.snapshot().dataset.plane(), constant)
    unchanged(owner, before, redo=1)
    owner.redo()
    assert owner.snapshot().metadata["properties"]["redo entry"] is True
    np.testing.assert_array_equal(original.snapshot().dataset.plane(), INPUT)
    ledger(scheduler, 0)


EXPECTED_COLLECTION_IDS = (
    "test_staged_rank_explicit_title_exact_owner_and_atomic_adoption",
    "test_staged_rank_default_title_freeze_and_unstaged_compatibility",
    "test_staged_rank_rejects_inplace_and_unsupported_operations",
    "test_staged_rank_configured_history_pruning_and_unbounded_budget",
    "test_staged_rank_candidate_edits_undo_and_save_refuse_without_membership_change",
    "test_staged_rank_ready_cancel_keeps_candidate_then_dataset_lease",
    "test_staged_rank_ready_adoption_detaches_owner_retains_old_export_aliases",
    "test_staged_rank_noop_output_preserves_source_redo",
)
