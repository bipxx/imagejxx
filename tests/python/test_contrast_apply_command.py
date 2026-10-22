"""Captured native Apply intent, strict grammar and worker result lifetime."""
import copy
import gc
import json
import time
import numpy as np
import pytest
import imagejxx as ij
from test_contrast_apply import options
from test_contrast_apply import RGB_FIXTURE, rgb_options, rgb_input
import hashlib
from pathlib import Path

RESET = json.loads((Path(__file__).parents[1] / "fixtures/contrast-apply-reset-imagej154r.json").read_text())


def rgb_owner():
    pixels = np.tile(np.arange(24, dtype=np.uint8).reshape(2, 4, 3), (1, 2, 2, 1, 1, 1))
    observer = ij.DatasetReadObserver()
    document = ij.Document(ij.Dataset(pixels, axes="TCZYXS").observe_reads(observer))
    document.select(ij.Selection.from_mask(np.array([[0, 1, 1, 0]] * 2, np.uint8)))
    return document, pixels, observer


@pytest.mark.parametrize("row", RGB_FIXTURE["rows"], ids=lambda r: f'rgb-r{r["range_index"]}-c{r["channels"]}-roi{r["roi"]}')
def test_rgb_captured_worker_matches_actual_imagej(row):
    pixels = rgb_input()
    observer = ij.DatasetReadObserver()
    document = ij.Document(ij.Dataset(pixels, axes="YXS").observe_reads(observer))
    if row["roi"]:
        mask = np.zeros((2, 256), np.uint8)
        mask[:, 17:236] = 1
        if row["roi"] == 2:
            y, x = np.indices(mask.shape)
            mask[(x + y) % 3 == 0] = 0
        document.select(ij.Selection.from_mask(mask))
    app = ij.Application()
    app.adopt(document)
    low, high = RGB_FIXTURE["ranges"][row["range_index"]]
    resolved = ij.resolve_rgb_contrast_apply_command(document.snapshot(), rgb_options(low, high, row["channels"]))
    record = json.loads(json.dumps(resolved.record()))
    ij.validate_rgb_contrast_apply_command_record(record)
    strict = ij.rgb_contrast_apply_command_from_record(document.snapshot(), record)
    assert strict.record() == resolved.record() and observer.counts().plane_reads == 0
    before = document.snapshot()
    job = ij.submit_rgb_contrast_apply(app.job_scheduler, strict)
    assert job.wait(5000) and job.ready
    assert document.snapshot().shares_data(before) and job.record() == resolved.record()
    preview = job.preview
    assert hashlib.sha256(preview.dataset.numpy("YXS").tobytes()).hexdigest() == row["sha256"]
    job.publish(app)
    after = document.snapshot()
    assert hashlib.sha256(after.dataset.numpy("YXS").tobytes()).hexdigest() == row["sha256"]
    assert after.display.properties["display_range"] == [0., 255.]
    # A new capture must use the committed baseline, with no stale-preview reuse.
    document.select(None)
    next_command = ij.resolve_rgb_contrast_apply_command(document.snapshot(), strict.options)
    document.commit(ij.execute_rgb_contrast_apply_command(next_command))
    assert hashlib.sha256(document.snapshot().dataset.numpy("YXS").tobytes()).hexdigest() == row["next_sha256"]
    document.undo()
    np.testing.assert_array_equal(document.snapshot().dataset.numpy("YXS"), after.dataset.numpy("YXS"))
    app.job_scheduler.shutdown()


RGB_MALFORMED = [
    (("extra",), 1), (("schema",), "imagejxx.contrast-apply-command"),
    (("schema_version",), True), (("schema_version",), 1.),
    (("operation",), "contrast_apply"), (("profile",), "scalar_integer_apply_v1"),
    (("options", "channels"), True), (("options", "channels"), 1.),
    (("options", "channels"), 0), (("options", "channels"), 8),
    (("options", "unsigned16_bits"), 12), (("options", "minimum"), True),
    (("options", "minimum"), 2**64 - 1), (("options", "maximum"), -1),
    (("source", "shape", 5), 4), (("source", "pixel_type"), "uint16"),
    (("source", "sample_policy"), "preserve_quiet_nan_float32"),
    (("target", "source_revisions", 0), 99), (("target", "execution_mode"), "volume"),
    (("target", "destination"), "new_document"), (("target", "document_id"), 0),
    (("reset_anchor", 0), True), (("reset_anchor", 2), 99), (("display_revision",), 1.),
    (("display_properties", "channel_display"), {}), (("selection", "extra"), 1),
    (("reset", "policy"), "imagej_scalar_reset_v1"), (("reset", "range", 1), 254),
    (("reset", "threshold"), "preserve"), (("reset", "indexed_palette"), "clear"),
    (("reset", "extra"), 1),
]


@pytest.mark.parametrize("path,value", RGB_MALFORMED, ids=lambda v: str(v))
def test_rgb_original_validated_entirely_before_explicit_rebind(path, value):
    document, _, observer = rgb_owner()
    record = ij.resolve_rgb_contrast_apply_command(document.snapshot(), rgb_options(0, 7)).record()
    at = record
    for key in path[:-1]:
        at = at[key]
    at[path[-1]] = value
    stable = document.snapshot()
    for call in (lambda: ij.validate_rgb_contrast_apply_command_record(record),
                 lambda: ij.rgb_contrast_apply_command_from_record(stable, record),
                 lambda: ij.rebind_rgb_contrast_apply_command(stable, record, ij.TargetRequest())):
        with pytest.raises((ValueError, TypeError, IndexError, ij.RevisionConflict)):
            call()
    assert observer.counts().plane_reads == 0 and document.snapshot().shares_data(stable)


@pytest.mark.parametrize("change", ["display", "selection", "pixels", "metadata"])
def test_rgb_strict_and_pending_publication_reject_all_stale_domains(change):
    document, _, _ = rgb_owner()
    app = ij.Application()
    app.adopt(document)
    command = ij.resolve_rgb_contrast_apply_command(document.snapshot(), rgb_options(0, 7))
    job = ij.submit_rgb_contrast_apply(app.job_scheduler, command)
    assert job.wait(5000)
    if change == "display":
        view = document.snapshot().display
        view.zoom = 2
        document.set_display(view)
    elif change == "selection":
        document.select(None)
    elif change == "metadata":
        edit = document.begin_edit()
        edit.set_extension("rgb-test", {"changed": True})
        document.commit(edit)
    else:
        document.commit(ij.execute_rgb_contrast_apply_command(command))
    stable = document.snapshot()
    with pytest.raises(ij.RevisionConflict):
        ij.rgb_contrast_apply_command_from_record(stable, command.record())
    with pytest.raises(ij.RevisionConflict):
        job.publish(app)
    assert stable.shares_data(document.snapshot()) and job.info.state == ij.JobState.FAILED
    wait_usage(app.job_scheduler)


@pytest.mark.parametrize("cancel", [False, True])
def test_rgb_worker_escaped_dataset_retains_memory_lease(cancel):
    document, _, _ = rgb_owner()
    app = ij.Application()
    app.adopt(document)
    job = ij.submit_rgb_contrast_apply(app.job_scheduler, ij.resolve_rgb_contrast_apply_command(document.snapshot(), rgb_options(0, 7, 4)))
    assert job.wait(5000)
    preview = job.preview
    dataset = preview.dataset
    reservation = job.info.resource_bytes
    before = document.snapshot()
    if cancel:
        assert job.cancel()
        with pytest.raises(ij.OperationCancelled):
            job.publish(app)
        assert document.snapshot().shares_data(before)
    else:
        job.publish(app)
        assert job.info.state == ij.JobState.COMPLETED
    del preview
    gc.collect()
    assert app.job_scheduler.usage.reserved_bytes == reservation
    np.testing.assert_array_equal(dataset.plane()[0, 1], [109, 4, 5])
    app.job_scheduler.shutdown()
    del dataset
    gc.collect()
    wait_usage(app.job_scheduler)


@pytest.mark.parametrize("dtype,samples", [(np.uint8, 1), (np.uint8, 4), (np.uint16, 3), (np.float32, 3)])
def test_rgb_rebind_cannot_cross_unqualified_storage(dtype, samples):
    document, _, _ = rgb_owner()
    record = ij.resolve_rgb_contrast_apply_command(document.snapshot()).record()
    destination = ij.Document(ij.Dataset(np.zeros((2, 4, samples), dtype), axes="YXS"))
    with pytest.raises(ValueError):
        ij.rebind_rgb_contrast_apply_command(destination.snapshot(), record, ij.TargetRequest())


def test_rgb_rebind_uses_fresh_geometry_estimates_and_no_io_empty_masks():
    document, _, observer = rgb_owner()
    request = ij.TargetRequest(ij.TargetScope.ALL_Z)
    command = ij.resolve_rgb_contrast_apply_command(document.snapshot(), rgb_options(0, 7, 5), request)
    assert ij.estimate_rgb_contrast_apply_command(command).pixels == 16
    estimate = ij.estimate_rgb_contrast_apply_job(command)
    assert estimate.reservation_bytes >= estimate.working_bytes > estimate.command.working_bytes
    document.select(ij.Selection.from_mask(np.zeros((2, 4), np.uint8)))
    rebound = ij.rebind_rgb_contrast_apply_command(document.snapshot(), command.record(), ij.TargetRequest())
    assert rebound.target.position_count == 1 and rebound.options.channels == 5
    stable = document.snapshot()
    document.commit(ij.execute_rgb_contrast_apply_command(rebound))
    assert stable.shares_data(document.snapshot()) and observer.counts().plane_reads == 0
    with pytest.raises(ValueError):
        ij.validate_contrast_apply_command_record(command.record())
    limits = ij.ContrastApplyCommandLimits()
    limits.max_record_bytes = 1
    with pytest.raises(ValueError):
        ij.submit_rgb_contrast_apply(ij.JobScheduler(), command, limits)
    assert observer.counts().plane_reads == 0


def test_rgb_positioned_record_is_canonical_and_freezes_destination_geometry():
    document, pixels, observer = rgb_owner()
    edit = document.begin_edit()
    edit.set_rois([ij.ROI("rgb", ij.Selection("rectangle", [(0, 0), (2, 1)]), c=0, z=0)])
    document.commit(edit)
    request = ij.TargetRequest(ij.TargetScope.ALL_Z, roi=ij.RoiPolicy.POSITIONED_ROIS)
    resolved = ij.resolve_rgb_contrast_apply_command(document.snapshot(), rgb_options(0, 7, 4), request)
    record = resolved.record()
    ij.validate_rgb_contrast_apply_command_record(record)
    assert observer.counts().plane_reads == 0 and record["selection"] is None and len(record["rois"]) == 1
    strict = ij.rgb_contrast_apply_command_from_record(document.snapshot(), record)
    document.commit(ij.execute_rgb_contrast_apply_command(strict))
    expected = pixels.copy()
    expected[0, 0, 0, 0, :2, 0] = [0, 109]
    assert observer.counts().plane_reads == 1
    np.testing.assert_array_equal(document.snapshot().dataset.numpy("TCZYXS"), expected)


def owner():
    pixels = np.arange(32, dtype=np.uint8).reshape(1, 2, 2, 2, 4)
    observer = ij.DatasetReadObserver()
    document = ij.Document(ij.Dataset(pixels, axes="TCZYX").observe_reads(observer))
    document.select(ij.Selection.from_mask(np.array([[0, 1, 1, 0], [0, 1, 1, 0]], np.uint8)))
    view = document.snapshot().display
    view.properties = {"retained": {"exact": 2**64 - 1}, "channel_display": {
        "schema": "imagejxx.channel-display", "version": 1, "mode": "color",
        "channels": [{"visible": True, "range": [0., 7.], "lut": {"name": "Grays"}},
                     {"visible": True, "range": [10., 20.], "lut": {"name": "Green"}}]}}
    document.set_display(view)
    return document, pixels, observer


def command(document):
    return ij.resolve_contrast_apply_command(document.snapshot(), options(0, 7),
                                             ij.TargetRequest(ij.TargetScope.ALL_Z))


def wait_usage(scheduler, value=0):
    deadline = time.monotonic() + 5
    while scheduler.usage.reserved_bytes != value:
        assert time.monotonic() < deadline
        time.sleep(.001)


def test_captured_roundtrip_masked_transaction_and_paired_reset():
    document, pixels, observer = owner()
    resolved = command(document)
    record = json.loads(json.dumps(resolved.record()))
    ij.validate_contrast_apply_command_record(record)
    strict = ij.contrast_apply_command_from_record(document.snapshot(), record)
    assert observer.counts().plane_reads == 0
    assert strict.record() == resolved.record()
    estimate = ij.estimate_contrast_apply_command(strict)
    assert estimate.pixels == 16 and estimate.working_bytes > estimate.processing_bytes
    prepared = ij.execute_contrast_apply_command(strict)
    assert observer.counts().plane_reads == 2
    expected = pixels.copy()
    expected[0, 0, 0] = [[0, 36, 73, 3], [4, 182, 219, 7]]
    expected[0, 0, 1] = [[8, 255, 255, 11], [12, 255, 255, 15]]
    before = document.snapshot()
    document.commit(prepared)
    np.testing.assert_array_equal(document.snapshot().dataset.numpy("TCZYX"), expected)
    props = document.snapshot().display.properties
    assert props["retained"] == {"exact": 2**64 - 1}
    assert props["channel_display"]["channels"][0]["range"] == [0., 255.]
    assert props["channel_display"]["channels"][1]["range"] == [10., 20.]
    document.undo()
    assert document.snapshot().display.properties == before.display.properties
    np.testing.assert_array_equal(document.snapshot().dataset.numpy("TCZYX"), pixels)
    document.redo()
    np.testing.assert_array_equal(document.snapshot().dataset.numpy("TCZYX"), expected)


MALFORMED = [
    (("extra",), 1), (("schema",), "future"), (("schema_version",), True),
    (("schema_version",), 1.), (("operation",), "other"), (("profile",), "future"),
    (("options", "extra"), 1), (("options", "minimum"), True),
    (("options", "minimum"), 2**64 - 1), (("options", "maximum"), -1),
    (("options", "unsigned16_bits"), True), (("options", "unsigned16_bits"), 12.),
    (("options", "unsigned16_bits"), 9), (("source", "shape", 5), 3),
    (("source", "pixel_type"), "float32"), (("source", "sample_policy"), "preserve_quiet_nan_float32"),
    (("target", "document_id"), 0), (("target", "source_shape", 3), 0),
    (("target", "source_revisions", 0), 99), (("target", "execution_mode"), "volume"),
    (("reset_anchor", 0), True), (("reset_anchor", 2), 99),
    (("target", "destination"), "new_document"), (("display_revision",), True),
    (("display_revision",), 1.), (("display_properties", "channel_display", "version"), 99),
    (("selection", "extra"), 1), (("reset", "policy"), "other"),
    (("reset", "range", 1), 254), (("reset", "threshold"), "preserve"),
    (("reset", "indexed_palette"), "clear"), (("reset", "extra"), 1),
]


@pytest.mark.parametrize("path,value", MALFORMED, ids=lambda v: str(v))
def test_full_original_grammar_validated_before_rebinding(path, value):
    document, _, observer = owner()
    record = command(document).record()
    at = record
    for key in path[:-1]:
        at = at[key]
    at[path[-1]] = value
    stable = document.snapshot()
    for call in (lambda: ij.validate_contrast_apply_command_record(record),
                 lambda: ij.contrast_apply_command_from_record(stable, record),
                 lambda: ij.rebind_contrast_apply_command(stable, record, ij.TargetRequest())):
        with pytest.raises((ValueError, TypeError, IndexError, ij.RevisionConflict)):
            call()
    assert observer.counts().plane_reads == 0
    assert document.snapshot().shares_data(stable)


def test_explicit_rebind_uses_destination_geometry_and_type():
    document, _, _ = owner()
    record = command(document).record()
    destination = ij.Document(ij.Dataset(np.arange(8, dtype=np.uint16).reshape(2, 4), axes="YX"))
    destination.select(ij.Selection.from_mask(np.zeros((2, 4), np.uint8)))
    with pytest.raises(ValueError):
        ij.contrast_apply_command_from_record(destination.snapshot(), record)
    rebound = ij.rebind_contrast_apply_command(destination.snapshot(), record, ij.TargetRequest())
    assert rebound.record()["source"]["pixel_type"] == "uint16"
    before = destination.snapshot()
    edit = ij.execute_contrast_apply_command(rebound)
    destination.commit(edit)
    assert before.shares_data(destination.snapshot())


@pytest.mark.parametrize("change", ["display", "selection", "pixels"])
def test_strict_replay_rejects_stale_domains(change):
    document, _, _ = owner()
    record = command(document).record()
    if change == "display":
        view = document.snapshot().display
        view.zoom = 2.
        document.set_display(view)
    elif change == "selection":
        document.select(None)
    else:
        app = ij.Application()
        app.adopt(document)
        target = app.resolve_target()
        app.publish(app.prepare("add", target, {"value": 1}))
    with pytest.raises(ij.RevisionConflict):
        ij.contrast_apply_command_from_record(document.snapshot(), record)


@pytest.mark.parametrize("cancel", [False, True])
def test_worker_preview_leases_survive_publication_or_cancel(cancel):
    document, _, _ = owner()
    app = ij.Application()
    app.adopt(document)
    scheduler = app.job_scheduler
    job = ij.submit_contrast_apply(scheduler, command(document))
    assert job.wait(5000)
    assert job.record()["operation"] == "contrast_apply"
    preview = job.preview
    data = preview.dataset
    reserved = job.info.resource_bytes
    assert reserved > 0 and scheduler.usage.retained_bytes == reserved
    before = document.snapshot()
    if cancel:
        assert job.cancel()
        with pytest.raises(ij.OperationCancelled):
            job.publish(app)
        assert before.shares_data(document.snapshot())
    else:
        job.publish(app)
        assert job.info.state == ij.JobState.COMPLETED
        assert not before.shares_data(document.snapshot())
    del preview
    gc.collect()
    assert scheduler.usage.reserved_bytes == reserved
    assert data.plane()[0, 1] == 36
    del data
    gc.collect()
    wait_usage(scheduler)
    scheduler.shutdown()


def test_job_stale_publication_and_foreign_application_fail_atomically():
    for failure in ("stale", "foreign"):
        document, _, _ = owner()
        app = ij.Application()
        app.adopt(document)
        job = ij.submit_contrast_apply(app.job_scheduler, command(document))
        assert job.wait(5000)
        if failure == "stale":
            view = document.snapshot().display
            view.zoom = 2.
            document.set_display(view)
            destination, exception = app, ij.RevisionConflict
        else:
            destination, exception = ij.Application(), ij.DocumentUnavailable
        stable = document.snapshot()
        with pytest.raises(exception):
            job.publish(destination)
        assert document.snapshot().shares_data(stable)
        assert job.info.state == ij.JobState.FAILED
        wait_usage(app.job_scheduler)


def test_metadata_admission_and_bounded_original_record_before_io():
    document, _, observer = owner()
    resolved = command(document)
    limits = ij.ContrastApplyCommandLimits()
    limits.max_record_bytes = 1
    for call in (lambda: ij.resolve_contrast_apply_command(document.snapshot(), options(), limits=limits),
                 lambda: ij.execute_contrast_apply_command(resolved, limits),
                 lambda: ij.submit_contrast_apply(ij.JobScheduler(), resolved, limits)):
        with pytest.raises(ValueError):
            call()
    assert observer.counts().plane_reads == 0
    malicious = copy.deepcopy(resolved.record())
    malicious["ignored_future"] = "x" * 1024
    with pytest.raises(ValueError):
        ij.validate_contrast_apply_command_record(malicious, max_record_bytes=512)


def test_positioned_roi_record_is_canonical_and_freezes_all_geometry():
    document, pixels, observer = owner()
    edit = document.begin_edit()
    edit.set_rois([ij.ROI("one", ij.Selection("rectangle", [(0, 0), (2, 1)]), c=0, z=0)])
    document.commit(edit)
    request = ij.TargetRequest(ij.TargetScope.ALL_Z, roi=ij.RoiPolicy.POSITIONED_ROIS)
    resolved = ij.resolve_contrast_apply_command(document.snapshot(), options(0, 7), request)
    record = resolved.record()
    assert record["selection"] is None and len(record["rois"]) == 1
    ij.validate_contrast_apply_command_record(record)
    assert observer.counts().plane_reads == 0
    prepared = ij.execute_contrast_apply_command(ij.contrast_apply_command_from_record(document.snapshot(), record))
    assert observer.counts().plane_reads == 1
    expected = pixels.copy()
    expected[0, 0, 0, 0, :2] = [0, 36]
    document.commit(prepared)
    np.testing.assert_array_equal(document.snapshot().dataset.numpy("TCZYX"), expected)


@pytest.mark.parametrize("row", RESET["cases"], ids=lambda r: f'u{r["depth"]}-b{r["bits"]}-s{r["scenario"]}')
def test_actual_imagej_processor_reset_threshold_and_palette(row):
    dtype = np.uint8 if row["depth"] == 8 else np.uint16
    pixels = np.array([[20] * 4 if row["scenario"] == 2 else [10, 20, 30, 40]], dtype)
    document = ij.Document(ij.Dataset(pixels, axes="YX"))
    if row["scenario"] == 1:
        document.select(ij.Selection.from_mask(np.array([[0, 1, 1, 0]], np.uint8)))
    palette = [[i, 255 - i, (3 * i) % 256] for i in range(256)]
    if row["depth"] == 8:
        encoded = ij.encode_indexed_palette(np.asarray(palette, np.uint8), 256)
        edit = document.begin_edit()
        edit.set_extension("imagejxx.indexed_color", encoded)
        document.commit(edit)
    view = document.snapshot().display
    view.properties = {"display_range": [10., 40.], "custom_lut": palette}
    view = ij.set_manual_threshold_state(view, document.snapshot().dataset, ij.ManualThresholdRange(15, 35))
    document.set_display(view)
    before = document.snapshot()
    resolved = ij.resolve_contrast_apply_command(before, options(10, 40, row["bits"]))
    record = resolved.record()
    assert record["reset"]["threshold"] == "clear"
    assert record["reset"]["range"] == (None if row["depth"] == 16 and not row["bits"] else row["range"])
    edit = ij.execute_contrast_apply_command(ij.contrast_apply_command_from_record(before, record))
    document.commit(edit)
    after = document.snapshot()
    assert after.display.properties["display_range"] == row["range"]
    assert after.display.properties["custom_lut"] == palette
    assert "manual_threshold" not in after.display.properties
    np.testing.assert_array_equal(after.dataset.numpy("YX"), np.array([row["pixels"]], dtype))
    if row["depth"] == 8:
        assert after.metadata["extensions"]["imagejxx.indexed_color"] == encoded
    document.undo()
    assert document.snapshot().display.properties == before.display.properties
    np.testing.assert_array_equal(document.snapshot().dataset.numpy("YX"), pixels)
    document.redo()
    assert document.snapshot().display.properties == after.display.properties
