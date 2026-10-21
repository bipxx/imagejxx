"""R15: installed native retained-result ownership; missing APIs fail, never skip.

The native R01-R14 executable establishes ledger/admission/escape-path mechanics.
This group checks real binding copies and their lifetime across cancel/drop/Apply.
NumPy results are caller-owned copies outside processing reservation accounting.
"""

import gc

import imagejxx as ij
import numpy as np
import pytest


@pytest.fixture
def retained_owner():
    y, x = np.indices((5, 7))
    pixels = ((37 * x + 61 * y + 13 * x * y) / 8.0 - 0.125).astype(np.float32)
    owner = ij.Document(ij.Dataset(pixels, axes="YX"))
    app = ij.Application()
    app.adopt(owner)
    scheduler = app.job_scheduler
    scheduler.set_limits(ij.JobLimits(workers=2, memory_budget=0, capacity=16))
    try:
        yield app, owner, pixels, scheduler
    finally:
        # Shutdown retires job-owned slots without waiting for exported aliases.
        scheduler.shutdown()


def submit(app, owner, *, retained=True, sigma=0.5):
    request = ij.TargetRequest(roi=ij.RoiPolicy.WHOLE_PLANE)
    target = app.resolve_target(request, document_id=owner.id)
    job = app.submit(
        "gaussian_blur_imagej",
        target,
        {"sigma": [sigma, sigma]},
        job_options=ij.JobOptions(retain_result_resources=retained),
    )
    assert job.wait(5000), "Required native Gaussian job did not terminate"
    assert job.info.state == ij.JobState.READY
    assert job.info.resource_bytes > 0
    assert job.info.reservation_acquired is True
    assert job.info.peak_reserved_bytes == job.info.resource_bytes
    return job


def ledger(scheduler, retained):
    actual = scheduler.usage
    assert actual.running == 0
    assert actual.running_bytes == 0
    assert actual.retained_bytes == retained
    assert actual.reserved_bytes == actual.running_bytes + actual.retained_bytes
    return actual


def pixels_of(kind, exported):
    if kind == "prepared":
        return exported.preview.dataset.plane()
    if kind in {"snapshot", "with_display"}:
        return exported.dataset.plane()
    return exported.plane()


def test_R15_public_defaults_and_readonly_ledger(retained_owner):
    app, owner, pixels, scheduler = retained_owner
    assert ij.JobOptions().retain_result_resources is False
    options = ij.JobOptions(retain_result_resources=True)
    assert options.retain_result_resources is True
    options.retain_result_resources = False
    assert options.retain_result_resources is False
    job = submit(app, owner, retained=False)
    ready_info = job.info
    ledger(scheduler, 0)
    prepared = job.prepared
    app.publish(job)
    assert job.info.state == ij.JobState.COMPLETED
    np.testing.assert_array_equal(
        job.prepared.preview.dataset.plane(), prepared.preview.dataset.plane()
    )
    usage = ledger(scheduler, 0)
    assert job.info.reservation_acquired is True
    assert job.info.peak_reserved_bytes == ready_info.peak_reserved_bytes
    for field in ("running_bytes", "retained_bytes", "reserved_bytes"):
        with pytest.raises(AttributeError):
            setattr(usage, field, 1)
    owner.undo()
    np.testing.assert_array_equal(owner.snapshot().dataset.plane(), pixels)


@pytest.mark.parametrize("kind", ["prepared", "snapshot", "with_display", "dataset"])
@pytest.mark.parametrize("action", ["cancel", "drop", "apply"])
def test_R15_independent_export_ownership(retained_owner, kind, action):
    app, owner, pixels, scheduler = retained_owner
    before = owner.snapshot()
    expected = ij.gaussian_blur_imagej(pixels, (0.5, 0.5))
    job = submit(app, owner)
    job_id, n = job.info.id, job.info.resource_bytes
    ready_info = job.info
    prepared = job.prepared
    second_prepared = job.prepared  # An actual second native value wrapper.
    snapshot = prepared.preview
    dataset = snapshot.dataset
    if kind == "prepared":
        exported = second_prepared
    elif kind == "snapshot":
        exported = snapshot
    elif kind == "with_display":
        display = snapshot.display
        display.zoom = 3
        exported = snapshot.with_display(display)
    else:
        exported = dataset
    prepared = second_prepared = snapshot = dataset = None
    ledger(scheduler, n)
    assert owner.snapshot().shares_data(before)
    assert owner.undo_steps == owner.redo_steps == 0

    if action == "cancel":
        assert job.cancel()
        assert job.info.state == ij.JobState.CANCELED
        with pytest.raises(ij.OperationCancelled):
            _ = job.prepared
    elif action == "drop":
        job = None
        gc.collect()
        records = [record for record in scheduler.jobs if record.id == job_id]
        assert not records or records[0].state == ij.JobState.CANCELED
        assert all(record.reservation_acquired is True and record.peak_reserved_bytes == n
                   for record in records)
    else:
        app.publish(job)
        assert job.info.state == ij.JobState.COMPLETED
        with pytest.raises(ValueError):
            _ = job.prepared
        assert owner.undo_steps == 1
        np.testing.assert_array_equal(owner.snapshot().dataset.plane(), expected)

    ledger(scheduler, n)
    owned_numpy = pixels_of(kind, exported)
    np.testing.assert_array_equal(owned_numpy, expected)
    if kind == "with_display":
        assert exported.display.zoom == 3
    exported = None
    gc.collect()
    ledger(scheduler, 0)
    assert ready_info.reservation_acquired is True and ready_info.peak_reserved_bytes == n
    if job is not None:
        assert job.info.reservation_acquired is True and job.info.peak_reserved_bytes == n
    # Keeping this copied array alive must not retain the native source lease.
    np.testing.assert_array_equal(owned_numpy, expected)
    if action == "apply":
        owner.undo()
        np.testing.assert_array_equal(owner.snapshot().dataset.plane(), pixels)
        owner.redo()
        np.testing.assert_array_equal(owner.snapshot().dataset.plane(), expected)
        ledger(scheduler, 0)
    else:
        assert owner.snapshot().shares_data(before)
        assert owner.snapshot().dirty == before.dirty
        assert owner.undo_steps == owner.redo_steps == 0
        np.testing.assert_array_equal(owner.snapshot().dataset.plane(), pixels)


def test_R15_copies_charge_once_and_numpy_is_owned(retained_owner):
    app, owner, pixels, scheduler = retained_owner
    before = owner.snapshot()
    job = submit(app, owner)
    n = job.info.resource_bytes
    observed = ledger(scheduler, n)
    first, second = job.prepared, job.prepared
    snapshot = first.preview
    dataset = snapshot.dataset
    owned_numpy = dataset.plane()
    expected = ij.gaussian_blur_imagej(pixels, (0.5, 0.5))
    ledger(scheduler, n)
    assert job.cancel()
    canceled_info = job.info
    assert canceled_info.state == ij.JobState.CANCELED
    assert canceled_info.reservation_acquired is True and canceled_info.peak_reserved_bytes == n
    with pytest.raises(ij.OperationCancelled):
        _ = job.prepared
    del first, second, snapshot
    gc.collect()
    ledger(scheduler, n)
    np.testing.assert_array_equal(dataset.plane(), expected)
    dataset = None
    gc.collect()
    ledger(scheduler, 0)
    assert job.info.reservation_acquired is True and job.info.peak_reserved_bytes == n
    assert observed.retained_bytes == n  # Public usage is an immutable observation.
    np.testing.assert_array_equal(owned_numpy, expected)
    owned_numpy[0, 0] = -123
    assert owner.snapshot().shares_data(before)
    np.testing.assert_array_equal(owner.snapshot().dataset.plane(), pixels)


def test_R15_opted_noop_preserves_source_identity_and_history(retained_owner):
    app, owner, pixels, scheduler = retained_owner
    before = owner.snapshot()
    job = submit(app, owner, sigma=0)
    n = job.info.resource_bytes
    prepared = job.prepared
    assert prepared.preview.dataset.shares_pixels(before.dataset)
    ledger(scheduler, n)
    app.publish(job)
    assert job.info.state == ij.JobState.COMPLETED
    assert job.info.reservation_acquired is True and job.info.peak_reserved_bytes == n
    with pytest.raises(ValueError):
        _ = job.prepared
    assert owner.snapshot().shares_data(before)
    assert owner.snapshot().dirty == before.dirty
    assert owner.undo_steps == owner.redo_steps == 0
    ledger(scheduler, n)
    prepared = None
    gc.collect()
    ledger(scheduler, 0)
    assert job.info.reservation_acquired is True and job.info.peak_reserved_bytes == n
    np.testing.assert_array_equal(owner.snapshot().dataset.plane(), pixels)
