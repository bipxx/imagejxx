"""Native queued operations, caller-thread publication and shared UI scheduling."""
import os
import time

import imagejxx as ij
import numpy as np
import pytest


def controller():
    pixels = np.arange(2 * 2 * 3 * 4 * 5, dtype=np.uint16).reshape(2, 2, 3, 4, 5)
    owner = ij.Document(ij.Dataset(pixels, axes="TCZYX"))
    app = ij.Application()
    app.adopt(owner)
    return app, owner, pixels


def test_queued_scientific_operation_preview_publication_and_view():
    app, owner, pixels = controller()
    request = ij.TargetRequest(ij.TargetScope.POSITIONS,
        positions=[ij.PlanePosition(1, 1, 1), ij.PlanePosition(0, 0, 2)],
        roi=ij.RoiPolicy.WHOLE_PLANE)
    parameters = {"value": 4}
    job = app.submit("add", app.resolve_target(request), parameters)
    parameters["value"] = 99
    assert job.wait(2000) and job.info.state == ij.JobState.READY
    assert job.info.total == job.info.completed == 2 and job.info.resource_bytes > 0
    ready_info = job.info
    assert ready_info.reservation_acquired is True
    assert ready_info.peak_reserved_bytes == ready_info.resource_bytes
    assert app.job_scheduler.usage.reserved_bytes == 0
    for field, value in (("peak_reserved_bytes", 0), ("reservation_acquired", False)):
        with pytest.raises(AttributeError):
            setattr(ready_info, field, value)
    assert job.info.stage and job.info.stage_completed == job.info.stage_total
    assert job.info.stage_total == pixels.shape[-2] * pixels.shape[-1]
    assert job.info.document_id == owner.id and job.info.label == "add"
    assert owner.undo_steps == 0
    expected = pixels.copy()
    expected[1, 1, 1] += 4
    expected[0, 0, 2] += 4
    preview = job.prepared
    np.testing.assert_array_equal(preview.preview.dataset.numpy("TCZYX"), expected)
    display = owner.snapshot().display
    display.crosshair = [1, 2, 2]
    owner.set_display(display)
    published = app.publish(job)
    assert published.document is owner and owner.undo_steps == 1
    assert job.info.state == ij.JobState.COMPLETED and not job.cancel()
    assert job.info.reservation_acquired is True
    assert job.info.peak_reserved_bytes == ready_info.peak_reserved_bytes
    assert app.job_scheduler.usage.reserved_bytes == 0
    assert owner.snapshot().display.crosshair == [1, 2, 2]
    np.testing.assert_array_equal(owner.snapshot().dataset.numpy("TCZYX"), expected)
    with pytest.raises(ValueError):
        app.publish(job)
    owner.undo()
    np.testing.assert_array_equal(owner.snapshot().dataset.numpy("TCZYX"), pixels)


def test_job_preflight_reservation_and_limits_are_atomic():
    app, owner, _ = controller()
    scheduler = app.job_scheduler
    scheduler.set_limits(ij.JobLimits(workers=1, memory_budget=1))
    before = owner.snapshot()
    with pytest.raises(ValueError, match="memory budget"):
        app.submit("add", app.resolve_target())
    for operation, parameters in [("add", {"unknown": 1}), ("convolve", {}), ("rotate90", {})]:
        with pytest.raises(ValueError):
            app.submit(operation, app.resolve_target(), parameters)
    with pytest.raises(ValueError):
        scheduler.set_limits(ij.JobLimits(workers=0))
    assert scheduler.limits.workers == 1 and scheduler.limits.memory_budget == 1
    assert not scheduler.jobs and scheduler.usage.reserved_bytes == 0
    assert owner.snapshot().shares_data(before)


@pytest.mark.parametrize("operation, parameters, size, budget", [
    ("median_filter", {"radius": 4096}, 64, 1024**2),
    ("rolling_ball_background", {"radius": 4096}, 64, 1024**2),
    ("clahe", {"tile_size": 1}, 64, 1024**2),
    ("convolve", {"kernel": np.ones((201, 201))}, 64, 1024**2),
    ("local_threshold", {"method": "median", "radius": 128}, 64, 1024**2),
    ("gaussian_blur", {"sigma": 1024}, 1, 64 * 1024),
    ("smooth", {"radius": 4096}, 1, 64 * 1024),
])
def test_parameter_dependent_workspace_rejected_before_admission(
    operation, parameters, size, budget
):
    app = ij.Application()
    owner = ij.Document(ij.Dataset(np.zeros((size, size), np.uint16), axes="YX"))
    app.adopt(owner)
    app.job_scheduler.set_limits(ij.JobLimits(workers=1, memory_budget=budget))
    source = owner.snapshot()
    with pytest.raises(ValueError, match="memory budget"):
        app.submit(operation, app.resolve_target(), parameters,
                   job_options=ij.JobOptions(resource_bytes=1))
    assert not app.job_scheduler.jobs and app.job_scheduler.usage.reserved_bytes == 0
    assert owner.snapshot().shares_data(source) and owner.undo_steps == 0


def test_superseded_ready_preview_and_pending_capacity():
    app, owner, pixels = controller()
    app.job_scheduler.set_limits(ij.JobLimits(workers=1, capacity=1))
    target = app.resolve_target()
    old = app.submit("add", target, {"value": 2}, job_options=ij.JobOptions(supersedes="preview"))
    old.wait()
    retained = old.prepared
    with pytest.raises(ValueError, match="capacity"):
        app.submit("add", target)
    latest = app.submit("add", target, {"value": 3}, job_options=ij.JobOptions(supersedes="preview"))
    assert old.info.state == ij.JobState.CANCELED
    with pytest.raises(ij.OperationCancelled):
        app.publish(old)
    assert retained.preview.dataset.plane()[0, 0] == pixels[0, 0, 0, 0, 0] + 2
    latest.wait()
    app.publish(latest)
    assert owner.undo_steps == 1
    assert owner.snapshot().dataset.plane()[0, 0] == pixels[0, 0, 0, 0, 0] + 3


def test_preview_supersession_and_capacity_are_scoped_to_the_source_document():
    app = ij.Application()
    first_owner = ij.Document(ij.Dataset(np.zeros((4, 5), np.uint16), axes="YX"))
    second_owner = ij.Document(ij.Dataset(np.zeros((4, 5), np.uint16), axes="YX"))
    app.adopt(first_owner)
    first_target = app.resolve_target()
    app.adopt(second_owner)
    second_target = app.resolve_target()
    scheduler = app.job_scheduler
    scheduler.set_limits(ij.JobLimits(workers=1, capacity=1))
    options = ij.JobOptions(supersedes="preview")
    first = app.submit("add", first_target, {"value": 1}, job_options=options)
    first.wait()
    retained = first.prepared
    with pytest.raises(ValueError, match="capacity"):
        app.submit("add", second_target, {"value": 2}, job_options=options)
    assert first.info.state == ij.JobState.READY
    scheduler.set_limits(ij.JobLimits(workers=1, capacity=2))
    second = app.submit("add", second_target, {"value": 2}, job_options=options)
    second.wait()
    replacement = app.submit("add", first_target, {"value": 3}, job_options=options)
    replacement.wait()
    assert first.info.state == ij.JobState.CANCELED and second.info.state == ij.JobState.READY
    assert retained.preview.dataset.plane()[0, 0] == 1
    app.publish(second)
    app.publish(replacement)
    assert first_owner.snapshot().dataset.plane()[0, 0] == 3
    assert second_owner.snapshot().dataset.plane()[0, 0] == 2
    assert first_owner.undo_steps == second_owner.undo_steps == 1


def test_stale_foreign_closed_and_retained_job_results():
    app, owner, _ = controller()
    job = app.submit("add", app.resolve_target(), {"value": 2})
    job.wait()
    foreign = ij.Application()
    foreign.adopt(owner)
    with pytest.raises(ij.DocumentUnavailable):
        foreign.publish(job)
    assert job.info.state == ij.JobState.READY
    edit = owner.begin_edit()
    edit.set_title("Newer")
    owner.commit(edit)
    with pytest.raises(ij.RevisionConflict):
        app.publish(job)
    assert job.info.state == ij.JobState.FAILED and job.info.error
    assert job.prepared.preview.dataset.plane()[0, 0] == 2
    closed = app.submit("add", app.resolve_target(), {"value": 3})
    closed.wait()
    app.close(owner.id, discard=True)
    assert closed.info.state == ij.JobState.CANCELED
    assert closed.prepared.preview.dataset.plane()[0, 0] == 3
    with pytest.raises(ij.OperationCancelled):
        app.publish(closed)


def test_new_document_job_and_shutdown():
    app, owner, _ = controller()
    source = owner.snapshot()
    request = ij.TargetRequest(destination=ij.OutputDestination.NEW_DOCUMENT)
    job = app.submit("add", app.resolve_target(request), {"value": 7})
    job.wait()
    result = app.publish(job, title="Output")
    assert result.created and result.document_id != owner.id
    assert result.document.snapshot().metadata["title"] == "Output" and result.document.undo_steps == 1
    assert owner.snapshot().shares_data(source) and owner.undo_steps == 0
    ready = app.submit("add", app.resolve_target())
    ready.wait()
    app.job_scheduler.shutdown()
    assert ready.info.state == ij.JobState.CANCELED
    with pytest.raises(ValueError, match="shut down"):
        app.submit("add", app.resolve_target())


def test_scheduler_cancels_only_the_requested_job_and_retains_snapshots():
    app, owner, _ = controller()
    source = owner.snapshot()
    first = app.submit("add", app.resolve_target(), {"value": 1})
    second = app.submit("add", app.resolve_target(), {"value": 2})
    first.wait()
    second.wait()
    recorded = app.job_scheduler.jobs
    assert not app.job_scheduler.cancel(0)
    assert app.job_scheduler.cancel(first.info.id)
    assert not app.job_scheduler.cancel(first.info.id)
    assert first.info.state == ij.JobState.CANCELED
    assert second.info.state == ij.JobState.READY
    assert recorded[0].state == ij.JobState.READY
    with pytest.raises(ij.OperationCancelled):
        app.publish(first)
    assert owner.snapshot().shares_data(source) and owner.undo_steps == 0
    app.publish(second)
    assert second.info.state == ij.JobState.COMPLETED and owner.undo_steps == 1


def test_shared_desktop_scheduler_and_consumed_resource_preferences(tmp_path):
    pytest.importorskip("imagejxx._desktop")
    from imagejxx.ui.app import Editor
    from imagejxx.ui.persistence import Preferences
    editor = Editor(preferences=Preferences(tmp_path / "settings.json"))
    owner = ij.Document(ij.Dataset(np.zeros((4, 5, 6), np.uint16), axes="ZYX"))
    editor.add_native_document(owner)
    editor.update_settings(processing_workers=1, processing_memory_mb=4, persist=False)
    editor.apply_operation("add", {"value": 5}, all_slices=True)
    info = editor.job_info
    assert info is not None and info.resource_bytes > 0
    editor.wait()
    assert editor.job_info is None and not editor.busy and not editor.error
    assert owner.undo_steps == 1
    np.testing.assert_array_equal(owner.snapshot().dataset.numpy("ZYX"), 5)
    (tmp_path / "settings.json").mkdir()
    with pytest.raises(RuntimeError):
        editor.update_settings(processing_workers=4, processing_memory_mb=8)
    assert editor.settings["processing_workers"] == 1
    assert editor.settings["processing_memory_mb"] == 4


@pytest.mark.parametrize("operation, parameters", [
    ("median_filter", {"radius": 4096}),
    ("rolling_ball_background", {"radius": 4096}),
    ("clahe", {"tile_size": 1}),
])
def test_shared_desktop_checks_parameter_workspace_before_apply(tmp_path, operation, parameters):
    pytest.importorskip("imagejxx._desktop")
    from imagejxx.ui.app import Editor
    from imagejxx.ui.persistence import Preferences
    editor = Editor(preferences=Preferences(tmp_path / "settings.json"))
    owner = ij.Document(ij.Dataset(np.zeros((64, 64), np.uint16), axes="YX"))
    editor.add_native_document(owner)
    editor.update_settings(processing_workers=1, processing_memory_mb=1, persist=False)
    editor.recording = True
    source = owner.snapshot()
    with pytest.raises(ValueError, match="memory budget"):
        editor.apply_operation(operation, parameters)
    assert not editor.busy and not editor.jobs and not editor.workflow
    assert owner.snapshot().shares_data(source) and owner.undo_steps == 0


def test_desktop_invalid_arguments_remain_asynchronous_failures(tmp_path):
    pytest.importorskip("imagejxx._desktop")
    from imagejxx.ui.app import Editor
    from imagejxx.ui.persistence import Preferences
    editor = Editor(preferences=Preferences(tmp_path / "settings.json"))
    owner = ij.Document(ij.Dataset(np.zeros((4, 5), np.uint16), axes="YX"))
    editor.add_native_document(owner)
    source = owner.snapshot()
    editor.apply_operation("add", {"value": "invalid numeric argument"})
    with pytest.raises(RuntimeError, match="numeric"):
        editor.wait()
    assert editor.jobs[-1].state == ij.JobState.FAILED and editor.jobs[-1].error
    assert owner.snapshot().shares_data(source) and owner.undo_steps == 0


def test_desktop_cancels_inside_single_plane_without_publication(tmp_path):
    pytest.importorskip("imagejxx._desktop")
    from imagejxx.ui.app import Editor
    from imagejxx.ui.persistence import Preferences
    editor = Editor(preferences=Preferences(tmp_path / "settings.json"))
    owner = ij.Document(ij.Dataset(np.zeros((768, 768), np.uint16), axes="YX"))
    editor.add_native_document(owner)
    source = owner.snapshot()
    editor.apply_operation("convolve", {"kernel": np.ones((31, 31))})
    deadline = time.monotonic() + 2
    while (editor.job_info.stage != "Convolution pixels" or
           editor.job_info.stage_completed < 1024) and time.monotonic() < deadline:
        time.sleep(0.001)
    info = editor.job_info
    assert info.stage == "Convolution pixels" and info.stage_completed >= 1024
    assert info.stage_total == 768 * 768 and info.completed == 0
    start = time.monotonic()
    editor.cancel()
    editor.wait()
    assert time.monotonic() - start < 0.5
    assert not editor.error and owner.undo_steps == 0 and owner.snapshot().shares_data(source)


SEGMENTATION_CANCEL_CASES = [
    ("label", "Component traversal", 255),
    ("fill_holes", "Hole traversal", 0),
    ("skeletonize", "Skeleton thinning", 255),
]


@pytest.mark.parametrize("operation, stage, value", SEGMENTATION_CANCEL_CASES)
def test_scientific_job_cancels_inside_segmentation_without_publication(operation, stage, value):
    pixels = np.full((2048, 2048), value, dtype=np.uint8)
    owner = ij.Document(ij.Dataset(pixels, axes="YX"))
    app = ij.Application()
    app.adopt(owner)
    app.job_scheduler.set_limits(ij.JobLimits(workers=1, memory_budget=2 * 1024**3))
    source = owner.snapshot()
    job = app.submit(operation, app.resolve_target())
    deadline = time.monotonic() + 5
    info = job.info
    while (info.stage != stage or info.stage_completed < 1024) and time.monotonic() < deadline:
        time.sleep(0.001)
        info = job.info
    assert info.stage == stage and 1024 <= info.stage_completed <= info.stage_total
    assert info.state == ij.JobState.RUNNING and info.completed == 0
    start = time.monotonic()
    assert job.cancel() and job.wait(2000)
    assert time.monotonic() - start < 0.5
    assert job.info.state == ij.JobState.CANCELED
    assert owner.undo_steps == 0 and owner.snapshot().shares_data(source)


@pytest.mark.parametrize("operation, stage, value", SEGMENTATION_CANCEL_CASES)
def test_desktop_segmentation_cancel_preserves_owner_history_and_workflow(tmp_path, operation, stage, value):
    pytest.importorskip("imagejxx._desktop")
    from imagejxx.ui.app import Editor
    from imagejxx.ui.persistence import Preferences
    editor = Editor(preferences=Preferences(tmp_path / "settings.json"))
    editor.update_settings(processing_workers=1, processing_memory_mb=2048, persist=False)
    owner = ij.Document(ij.Dataset(np.full((2048, 2048), value, np.uint8), axes="YX"))
    editor.add_native_document(owner)
    editor.recording = True
    source = owner.snapshot()
    editor.apply_operation(operation)
    deadline = time.monotonic() + 5
    info = editor.job_info
    while (info.stage != stage or info.stage_completed < 1024) and time.monotonic() < deadline:
        time.sleep(0.001)
        info = editor.job_info
    assert info.stage == stage and 1024 <= info.stage_completed <= info.stage_total
    assert info.state == ij.JobState.RUNNING and info.completed == 0
    start = time.monotonic()
    editor.cancel()
    editor.wait()
    assert time.monotonic() - start < 0.5
    assert editor.jobs[-1].state == ij.JobState.CANCELED and not editor.error
    assert owner.undo_steps == 0 and owner.snapshot().shares_data(source)
    assert editor.workflow == []


@pytest.mark.skipif(not os.environ.get("IMAGEJXX_RENDER_TESTS"), reason="Enable hidden OpenGL rendering checks")
def test_shared_jobs_panel_cancel_button_and_retained_error_records(tmp_path):
    pytest.importorskip("imagejxx._desktop")
    from imagejxx.ui.app import Editor
    from imagejxx.ui.persistence import Preferences
    editor = Editor(preferences=Preferences(tmp_path / "settings.json"))
    owner = ij.Document(ij.Dataset(np.zeros((768, 768), np.uint16), axes="YX"))
    editor.add_native_document(owner)
    source = owner.snapshot()
    editor.recording = True
    editor.apply_operation("convolve", {"kernel": np.ones((51, 51))})
    job_id = editor.job_info.id
    editor.action("show_jobs")
    rendered = editor.render_headless(frames=3, exercise=True)
    assert rendered["jobs_panel"]["visible"]
    assert job_id in rendered["jobs_panel"]["job_ids"]
    assert not editor.busy and not editor.error
    record = next(item for item in editor.jobs if item.id == job_id)
    assert record.state == ij.JobState.CANCELED and record.label == "Convolve"
    assert record.document_id == owner.id and not editor.cancel_job(job_id)
    assert owner.undo_steps == 0 and owner.snapshot().shares_data(source)
    assert editor.workflow == []
    editor.apply_operation("convolve", {"kernel": [[1, 2], [3, 4]]})
    with pytest.raises(RuntimeError, match="odd dimensions"):
        editor.wait()
    assert not editor.error and editor.jobs[-1].state == ij.JobState.FAILED
    assert editor.jobs[-1].error and owner.snapshot().shares_data(source)
    rendered = editor.render_headless(frames=2)
    assert editor.jobs[-1].id in rendered["jobs_panel"]["job_ids"]
    editor.action("show_jobs", visible=False)
    assert not editor.render_headless(frames=2)["jobs_panel"]["visible"]


