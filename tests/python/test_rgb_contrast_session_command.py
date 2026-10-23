"""Complete native captures/jobs; JSON round trips never discard prior visits."""
import gc
import json

import pytest
import numpy as np
import imagejxx as ij

from test_rgb_contrast_session import REFERENCE, PIXELS, step, visit, plan, sha
from test_contrast_apply import STACK_FIXTURE, rgb_stack_owner, rgb_stack_steps, rgb_stack_selection
from test_contrast_apply_command import wait_usage


def request(row, reset=False):
    a = [row["origin"]]*3
    b = a.copy()
    b[row["axis"]] = 1-row["origin"]
    selected = rgb_stack_selection(row["changed_roi"])
    channels = row["channels"]
    return plan([visit(a, [step(20.25, 200.75, channels, selected)]),
                 visit(b, [step(-30.25, 400.5, channels, selected)]),
                 visit(a, [step(0. if reset else -10.2, 255. if reset else 160.9, channels, selected)])])


@pytest.mark.parametrize("row", REFERENCE["cases"], ids=lambda r:f"a{r['axis']}-o{r['origin']}-c{r['channels']}-s{r['roi']}")
def test_all_imagej_navigation_returns_capture_strict_rebind_worker_and_history(row):
    for reset in [False, True]:
        reads = ij.DatasetReadObserver()
        owner = ij.Document(ij.Dataset(PIXELS, axes="TCZYXS").observe_reads(reads))
        before = owner.snapshot()
        expected = row["reset_stack_sha256" if reset else "remapped_stack_sha256"]
        command = ij.resolve_rgb_contrast_session_command(before, request(row, reset))
        record = json.loads(json.dumps(command.record()))
        ij.validate_rgb_contrast_session_command_record(record)
        strict = ij.rgb_contrast_session_command_from_record(before, record)
        assert strict.record() == record and len(strict.plan.visits) == 3
        estimate = ij.estimate_rgb_contrast_session_job(strict)
        assert reads.counts().plane_reads == 0 and estimate.reservation_bytes > 0
        app = ij.Application()
        app.adopt(owner)
        job = ij.submit_rgb_contrast_session(app.job_scheduler, strict)
        assert job.wait(5000)
        assert reads.counts().plane_reads <= 2
        assert job.record() == record
        assert sha(job.preview.dataset.numpy("TCZYXS")) == expected
        assert owner.snapshot().shares_data(before) and owner.undo_steps == 0
        job.publish(app)
        assert sha(owner.snapshot().dataset.numpy("TCZYXS")) == expected
        changed = owner.undo_steps != 0
        if changed:
            assert owner.undo_steps == 1
            owner.undo()
            assert owner.snapshot().shares_data(before)
            owner.redo()
            assert sha(owner.snapshot().dataset.numpy("TCZYXS")) == expected
        destination = ij.Document(before)
        with pytest.raises((ValueError, ij.RevisionConflict)):
            ij.rgb_contrast_session_command_from_record(destination.snapshot(), record)
        rebound = ij.rebind_rgb_contrast_session_command(destination.snapshot(), record)
        assert rebound.record()["visits"] == record["visits"]
        assert sha(ij.execute_rgb_contrast_session_command(rebound).preview().dataset.numpy("TCZYXS")) == expected
        app.job_scheduler.shutdown()


@pytest.mark.parametrize("row", STACK_FIXTURE["rows"], ids=lambda r:f"r{r['range_index']}-c{r['channels']}-s{r['roi']}-a{r['anchor']}-p{int(r['prefix'])}-w{int(r['whole_plane'])}")
def test_all_imagej_single_visit_stack_captures_and_jobs(row):
    reads = ij.DatasetReadObserver()
    owner = rgb_stack_owner(row, reads)
    before = owner.snapshot()
    selected = rgb_stack_selection(row["roi"])
    stages = [step(v.options.minimum, v.options.maximum, v.options.channels, selected, v.whole_plane)
              for v in rgb_stack_steps(row)]
    command = ij.resolve_rgb_contrast_session_command(before, plan([visit(STACK_FIXTURE["anchors"][row["anchor"]], stages)], stack=True))
    record = command.record()
    ij.validate_rgb_contrast_session_command_record(record)
    strict = ij.rgb_contrast_session_command_from_record(before, record)
    assert reads.counts().plane_reads == 0
    app = ij.Application()
    app.adopt(owner)
    job = ij.submit_rgb_contrast_session(app.job_scheduler, strict)
    assert job.wait(5000)
    assert job.record() == record
    assert sha(job.preview.dataset.numpy("TCZYXS")) == row["stack_sha256"]
    job.publish(app)
    assert sha(owner.snapshot().dataset.numpy("TCZYXS")) == row["stack_sha256"]
    if owner.undo_steps:
        owner.undo()
        assert owner.snapshot().shares_data(before)
    app.job_scheduler.shutdown()


BAD = [(("extra",), 1), (("schema",), "other"), (("schema_version",), True),
       (("schema_version",), 1.), (("profile",), "imagej_rgb_stack_v1"),
       (("operation",), "rgb_contrast_stack"), (("entire_stack",), 1),
       (("entire_stack",), "true"), (("visits",), []),
       (("visits", 2, "extra"), 0), (("visits", 2, "position"), [0, 0]),
       (("visits", 2, "position"), [2, 0, 0]), (("visits", 2, "position"), [-1, 0, 0]),
       (("visits", 2, "position"), [True, 0, 0]), (("visits", 2, "stages"), []),
       (("visits", 2, "stages", 0, "channels"), True), (("visits", 2, "stages", 0, "channels"), 1.),
       (("visits", 2, "stages", 0, "channels"), 8), (("visits", 2, "stages", 0, "whole_plane"), 1),
       (("visits", 2, "stages", 0, "minimum"), True), (("visits", 2, "stages", 0, "minimum"), 200.),
       (("visits", 2, "stages", 0, "maximum"), -100.),
       (("visits", 2, "stages", 0, "selection", "extra"), 1),
       (("capture", "target", "roi_policy"), "whole_plane"),
       (("capture", "target", "ranges", 0, 1), 2),
       (("capture", "options", "minimum"), -10.), (("capture", "display_revision"), True)]


def sample():
    row = next(r for r in REFERENCE["cases"] if r["channels"] == 7 and r["changed_roi"] == 1)
    owner = ij.Document(ij.Dataset(PIXELS, axes="TCZYXS"))
    return owner, request(row), row


@pytest.mark.parametrize("path,value", BAD, ids=lambda v:str(v))
def test_closed_record_rejects_corrupt_late_history_before_any_rebind(path, value):
    owner, intent, _ = sample()
    record = ij.resolve_rgb_contrast_session_command(owner.snapshot(), intent).record()
    cursor = record
    for key in path[:-1]: cursor = cursor[key]
    cursor[path[-1]] = value
    for call in [lambda: ij.validate_rgb_contrast_session_command_record(record),
                 lambda: ij.rgb_contrast_session_command_from_record(owner.snapshot(), record),
                 lambda: ij.rebind_rgb_contrast_session_command(owner.snapshot(), record)]:
        with pytest.raises((ValueError, TypeError, IndexError, ij.RevisionConflict)): call()


@pytest.mark.parametrize("change", ["selection", "display", "metadata", "pixels", "close"])
def test_session_strict_capture_and_worker_publication_guard_all_domains(change):
    owner, intent, _ = sample()
    app = ij.Application()
    app.adopt(owner)
    command = ij.resolve_rgb_contrast_session_command(owner.snapshot(), intent)
    job = ij.submit_rgb_contrast_session(app.job_scheduler, command)
    assert job.wait(5000)
    if change == "selection": owner.select(ij.Selection("rectangle", [(0, 0), (2, 2)]))
    elif change == "display":
        view = owner.snapshot().display
        view.zoom = 2
        owner.set_display(view)
    elif change == "metadata":
        edit = owner.begin_edit()
        edit.set_title("newer")
        owner.commit(edit)
    elif change == "pixels": owner.commit(ij.execute_rgb_contrast_session_command(command))
    else: app.close(owner.id, discard=True)
    stable = owner.snapshot()
    history = owner.undo_steps
    if change != "close":
        with pytest.raises(ij.RevisionConflict): ij.rgb_contrast_session_command_from_record(stable, command.record())
    with pytest.raises(ij.OperationCancelled if change == "close" else ij.RevisionConflict): job.publish(app)
    assert stable.shares_data(owner.snapshot()) and owner.undo_steps == history
    wait_usage(app.job_scheduler)


@pytest.mark.parametrize("cancel", [False, True])
def test_session_escaped_dataset_retains_result_reservation_through_shutdown(cancel):
    owner, intent, _ = sample()
    app = ij.Application()
    app.adopt(owner)
    job = ij.submit_rgb_contrast_session(app.job_scheduler, ij.resolve_rgb_contrast_session_command(owner.snapshot(), intent))
    assert job.wait(5000)
    preview = job.preview
    dataset = preview.dataset
    reserved = job.info.resource_bytes
    if cancel: job.cancel()
    else: job.publish(app)
    del preview
    gc.collect()
    assert app.job_scheduler.usage.reserved_bytes == reserved
    app.job_scheduler.shutdown()
    del dataset
    gc.collect()
    wait_usage(app.job_scheduler)


@pytest.mark.parametrize("failure", ["record", "memory", "pixels", "scheduler", "job_minimum"])
def test_session_capture_and_job_admission_reject_before_io(failure):
    reads = ij.DatasetReadObserver()
    owner = ij.Document(ij.Dataset(PIXELS, axes="TCZYXS").observe_reads(reads))
    intent = request(REFERENCE["cases"][0])
    command = ij.resolve_rgb_contrast_session_command(owner.snapshot(), intent)
    limits = ij.ContrastApplyCommandLimits()
    if failure == "record": limits.max_record_bytes = 1
    elif failure == "memory": limits.processing.max_working_bytes = 1
    elif failure == "pixels": limits.processing.max_pixels = 1
    elif failure == "job_minimum": limits.processing.max_working_bytes = ij.estimate_rgb_contrast_session_job(command).working_bytes-1
    scheduler = ij.JobScheduler(ij.JobLimits(1, 1, 8)) if failure == "scheduler" else ij.JobScheduler()
    with pytest.raises(ValueError): ij.submit_rgb_contrast_session(scheduler, command, limits)
    assert reads.counts().plane_reads == 0 and owner.undo_steps == 0
    scheduler.shutdown()


def test_session_capture_plan_record_and_exports_are_detached():
    owner, intent, row = sample()
    command = ij.resolve_rgb_contrast_session_command(owner.snapshot(), intent)
    saved = command.record()
    intent.visits = []
    command.plan.visits = []
    mutable = command.record()
    mutable["visits"].clear()
    limits = ij.ContrastApplyCommandLimits()
    called = []
    def mutate():
        called.append(True)
        limits.processing.max_pixels = 0
    result = ij.execute_rgb_contrast_session_command(command, limits, checkpoint=mutate)
    assert called and command.record() == saved
    assert sha(result.preview().dataset.numpy("TCZYXS")) == row["remapped_stack_sha256"]
    names = ["ResolvedRgbContrastSessionCommand", "resolve_rgb_contrast_session_command", "submit_rgb_contrast_session"]
    assert all(name in ij.__all__ for name in names)


def test_session_rebind_preserves_history_instead_of_adopting_destination_selection_navigation():
    source, intent, row = sample()
    record = ij.resolve_rgb_contrast_session_command(source.snapshot(), intent).record()
    destination = ij.Document(ij.Dataset(PIXELS, axes="TCZYXS"))
    destination.select(rgb_stack_selection(3))
    view = destination.snapshot().display
    view.t = view.c = 1
    view.crosshair = [0, 0, 1]
    destination.set_display(view)
    before = destination.snapshot()
    rebound = ij.rebind_rgb_contrast_session_command(before, record)
    assert rebound.record()["visits"] == record["visits"]
    output = ij.execute_rgb_contrast_session_command(rebound).preview()
    assert sha(output.dataset.numpy("TCZYXS")) == row["remapped_stack_sha256"]
    assert output.selection == before.selection and output.display.t == 1 and output.display.c == 1
    assert output.display.crosshair == before.display.crosshair


@pytest.mark.parametrize("failure", ["destination_positions", "original_positions", "late_geometry", "nonfinite_range"])
def test_session_rebind_checks_original_history_and_destination_descriptors(failure):
    source, intent, _ = sample()
    record = ij.resolve_rgb_contrast_session_command(source.snapshot(), intent).record()
    if failure == "destination_positions":
        destination = ij.Document(ij.Dataset(PIXELS[:1], axes="TCZYXS"))
    else:
        data = np.concatenate((PIXELS, PIXELS[:1]), axis=0) if failure == "original_positions" else PIXELS
        destination = ij.Document(ij.Dataset(data, axes="TCZYXS"))
        if failure == "original_positions": record["visits"][0]["position"] = [2, 0, 0]
        elif failure == "nonfinite_range": record["visits"][0]["stages"][0]["minimum"] = float("nan")
        else:
            bad = plan([visit([0, 0, 0], [step(selection=ij.Selection("line", [(-2e10, 0), (2e10, 1)]))])])
            with pytest.raises(ValueError): ij.resolve_rgb_contrast_session_command(source.snapshot(), bad)
            # A structurally canonical line selection copied into a late stage
            # must still be validated against its processor integer bounds.
            ordinary = plan([visit([0, 0, 0], [step(selection=ij.Selection("line", [(0, 0), (1, 1)]))])])
            selected = ij.resolve_rgb_contrast_session_command(source.snapshot(), ordinary).record()["visits"][0]["stages"][0]["selection"]
            selected["points"] = [[-2e10, 0.], [2e10, 1.]]
            record["visits"][-1]["stages"][0]["selection"] = selected
    before = destination.snapshot()
    with pytest.raises((ValueError, TypeError, IndexError)):
        ij.rebind_rgb_contrast_session_command(before, record)
    assert destination.snapshot().shares_data(before) and destination.undo_steps == 0
