"""Python Editor/_desktop/_native rank interop, without a renderer or OS input.

Radius-zero expected values below are hand-derived from the five-point cross
with nearest-edge extension. No product filter or serializer supplies pixels.
The tests require the real optional desktop binding and never skip its absence.
They establish no installed, graphical-input, platform or performance claim.
"""

from concurrent.futures import ThreadPoolExecutor

import imagejxx as ij
import numpy as np
import pytest

from rank_observations import RawCase


INPUT = [[1, 8, 3], [4, 9, 6], [7, 2, 5]]
MINIMUM = [[1, 1, 3], [1, 2, 3], [2, 2, 2]]
MAXIMUM = [[8, 9, 8], [9, 9, 9], [7, 9, 6]]
MEDIAN = [[1, 8, 3], [4, 6, 6], [7, 5, 5]]
# The five-point sums are [15,29,23;25,29,29;27,25,23]. Integer
# results round their Float32 means by +.5; the Float32 case keeps /5.
MEAN_INTEGER = [[3, 6, 5], [5, 6, 6], [5, 5, 5]]
MEAN_FLOAT = [[3.0, 5.8, 4.6], [5.0, 5.8, 5.8], [5.4, 5.0, 4.6]]
METHODS = ("minimum", "maximum", "median", "mean")
DTYPES = (("uint8", np.uint8), ("uint16", np.uint16), ("float32", np.float32))
METHOD_DTYPE_CASES = [(method, dtype) for _, dtype in DTYPES for method in METHODS]
METHOD_DTYPE_IDS = [dtype_name + "-" + method for dtype_name, _ in DTYPES for method in METHODS]


@pytest.fixture
def editor(tmp_path):
    from imagejxx import _desktop  # noqa: F401 -- required binding, never skip
    from imagejxx.ui.app import Editor
    from imagejxx.ui.persistence import Preferences

    return Editor(preferences=Preferences(tmp_path / "rank-host-preferences.json"))


@pytest.fixture
def rank_raw(request):
    return RawCase(request.node.name)


def attach(editor, pixels=None, *, axes="YX", title="Rank Python host"):
    pixels = np.array(INPUT, np.uint16) if pixels is None else pixels
    owner = ij.Document(ij.Dataset(pixels, axes=axes), title)
    live = editor.add_native_document(owner)
    assert live.id == owner.id and live.native_owner is owner
    return owner


def whole(*, destination=ij.OutputDestination.IN_PLACE):
    return ij.TargetRequest(roi=ij.RoiPolicy.WHOLE_PLANE, destination=destination)


def literal(method, dtype):
    values = {"minimum": MINIMUM, "maximum": MAXIMUM, "median": MEDIAN,
              "mean": MEAN_FLOAT if dtype == np.float32 else MEAN_INTEGER}[method]
    return np.array(values, dtype=dtype)


def unchanged(owner, before, *, undo=0, redo=0):
    after = owner.snapshot()
    assert after.shares_data(before)
    assert after.revisions.matches(before.revisions, ij.Change.ALL)
    assert after.dirty == before.dirty
    assert owner.undo_steps == undo and owner.redo_steps == redo


@pytest.mark.parametrize("method,dtype", METHOD_DTYPE_CASES, ids=METHOD_DTYPE_IDS)
def test_reh01_method_dtype_literal_editor_native_interop(editor, method, dtype, rank_raw):
    pixels = np.array(INPUT, dtype=dtype)
    owner = attach(editor, pixels)
    before = owner.snapshot()
    rank_raw.owner("source", owner)
    options = ij.RankFilterCommandOptions(method, 0)
    expected_record = ij.resolve_rank_command(before, whole(), options).record()
    editor.recording = True
    editor.apply_rank(whole(), options, document_id=owner.id, wait=True)
    live = editor.live_document(owner.id)
    assert live.native_owner is owner
    assert not editor.busy and not editor.error
    assert len(editor.jobs) == 1 and editor.jobs[0].state == ij.JobState.COMPLETED
    assert editor.jobs[0].document_id == owner.id
    actual = live.native_snapshot().dataset.plane()
    assert actual.dtype == dtype
    np.testing.assert_array_equal(actual, literal(method, dtype))
    np.testing.assert_array_equal(before.dataset.plane(), pixels)
    assert editor.workflow == [expected_record] and editor.results == []
    assert owner.undo_steps == 1 and owner.redo_steps == 0 and owner.snapshot().dirty
    rank_raw.array("python." + np.dtype(dtype).name + "." + method + ".published", actual)
    rank_raw.owner("published", owner)
    rank_raw.editor("published", editor)
    if method == "mean" and dtype == np.uint16:
        rank_raw.array("python.changed.mean", actual)
        rank_raw.scalar("python.changed.undo", owner.undo_steps)
        rank_raw.scalar("python.changed.workflow", len(editor.workflow))
        rank_raw.scalar("python.record.operation", editor.workflow[0]["operation"])
        rank_raw.scalar("python.record.profile", editor.workflow[0]["profile"])
    editor.action("undo", document_id=owner.id)
    np.testing.assert_array_equal(owner.snapshot().dataset.plane(), pixels)
    assert not owner.snapshot().dirty
    rank_raw.owner("undo", owner)
    editor.action("redo", document_id=owner.id)
    np.testing.assert_array_equal(owner.snapshot().dataset.plane(), literal(method, dtype))
    rank_raw.owner("redo", owner)
    rank_raw.finish()


def test_reh02_wrapper_defaults_are_recorded_median_radius_two(editor, rank_raw):
    # One interior spike cannot form a majority in any 21-point radius-two
    # neighborhood; edge extension replicates only the surrounding zeros.
    pixels = np.zeros((5, 5), np.uint16)
    pixels[2, 2] = 80
    owner = attach(editor, pixels)
    before = owner.snapshot()
    rank_raw.owner("source", owner)
    expected = ij.resolve_rank_command(before).record()
    editor.recording = True
    editor.apply_rank(wait=True)
    assert editor.workflow == [expected]
    record, = editor.workflow
    assert record["schema_version"] == 1
    assert record["operation"] == "rank_filter_imagej"
    assert record["profile"] == "imagej_154r"
    assert record["options"] == {"method": "median", "radius": 2.0}
    assert record["target"]["document_id"] == owner.id
    np.testing.assert_array_equal(owner.snapshot().dataset.plane(), np.zeros((5, 5), np.uint16))
    assert owner.undo_steps == 1 and owner.snapshot().dirty
    rank_raw.scalar("python.default.method", record["options"]["method"])
    rank_raw.scalar("python.default.radius", record["options"]["radius"])
    rank_raw.json("record", record)
    rank_raw.owner("published", owner)
    rank_raw.editor("published", editor)
    rank_raw.finish()


def test_reh03_scientific_positions_roi_hole_capture_survives_argument_and_view_changes(editor, rank_raw):
    pixels = np.empty((2, 2, 3, 3, 3), np.uint16)
    for t in range(2):
        for c in range(2):
            for z in range(3):
                pixels[t, c, z] = np.array(INPUT, np.uint16) + 10*t + 20*c + 30*z
    owner = ij.Document(ij.Dataset(pixels, axes="TCZYX"), "Captured scientific rank")
    mask = np.ones((3, 3), np.uint8)
    mask[1, 1] = 0
    owner.select(ij.Selection.from_mask(mask))
    view = owner.snapshot().display
    view.t, view.c, view.crosshair = 1, 1, [1, 1, 2]
    owner.set_display(view)
    editor.add_native_document(owner)
    before = owner.snapshot()
    rank_raw.owner("source", owner)
    selected = [(1, 1, 2), (0, 0, 0)]
    target = ij.TargetRequest(ij.TargetScope.POSITIONS,
                              positions=[ij.PlanePosition(*p) for p in selected],
                              roi=ij.RoiPolicy.ACTIVE_SELECTION)
    options = ij.RankFilterCommandOptions("mean", 0)
    captured = ij.resolve_rank_command(before, target, options).record()
    editor.recording = True
    editor.apply_rank(target, options, document_id=owner.id)
    # The actual _desktop signature copies both typed values before GIL release.
    target.scope, target.positions = ij.TargetScope.CURRENT_PLANE, []
    target.roi = ij.RoiPolicy.WHOLE_PLANE
    options.method, options.radius = "maximum", 2
    editor.live_document(owner.id).set_view(t=0, c=1, crosshair=[2, 0, 1], zoom=2)
    editor.wait()
    wanted = pixels.copy()
    for t, c, z in selected:
        values = np.array(MEAN_INTEGER, np.uint16) + 10*t + 20*c + 30*z
        values[1, 1] = pixels[t, c, z, 1, 1]
        wanted[t, c, z] = values
    np.testing.assert_array_equal(owner.snapshot().dataset.numpy("TCZYX"), wanted)
    np.testing.assert_array_equal(before.dataset.numpy("TCZYX"), pixels)
    assert editor.workflow == [captured]
    decoded = ij.rank_command_from_record(before, editor.workflow[0])
    assert [(decoded.target.position(i).t, decoded.target.position(i).c,
             decoded.target.position(i).z) for i in range(decoded.target.position_count)] == selected
    current = owner.snapshot().display
    assert (current.t, current.c, current.crosshair, current.zoom) == (0, 1, [2, 0, 1], 2)
    assert owner.snapshot().selection == before.selection
    assert owner.undo_steps == 1 and owner.redo_steps == 0
    actual_positions = [(decoded.target.position(i).t, decoded.target.position(i).c,
                         decoded.target.position(i).z) for i in range(decoded.target.position_count)]
    rank_raw.scalar("python.captured.count", decoded.target.position_count)
    rank_raw.scalar("python.captured.positions", ";".join(",".join(map(str, p)) for p in actual_positions))
    rank_raw.scalar("python.fresh_navigation", ",".join(map(str, (current.t, current.c, current.crosshair[2]))))
    if rank_raw.enabled:
        rank_raw.array("python.captured.holed_output", owner.snapshot().dataset.numpy("TCZYX"))
    rank_raw.json("record", editor.workflow[0])
    rank_raw.owner("published", owner)
    rank_raw.editor("published", editor)
    rank_raw.finish()


def test_reh04_new_document_native_membership_and_configured_history_policy(editor, rank_raw):
    owner = attach(editor)
    editor.recording = True
    rank_raw.owner("source", owner)
    output_ids = []
    for memory_mb in (1, 0):
        editor.active_index = 0
        editor.update_settings(history_limit=3, history_memory_mb=memory_mb, persist=False)
        before = owner.snapshot()
        request = whole(destination=ij.OutputDestination.NEW_DOCUMENT)
        options = ij.RankFilterCommandOptions("minimum", 0)
        record = ij.resolve_rank_command(before, request, options).record()
        editor.apply_rank(request, options, document_id=owner.id, wait=True)
        output = editor.live_document()
        native = output.native_owner
        assert output.id != owner.id and output.id not in output_ids
        output_ids.append(output.id)
        assert native is editor.live_document(output.id).native_owner
        assert output.native_snapshot().shares_data(native.snapshot())
        assert editor.live_document(owner.id).native_owner is owner
        assert [item["id"] for item in editor.document_infos] == [owner.id, *output_ids]
        assert editor.active_index == len(output_ids)
        assert native.snapshot().metadata["title"] == "Rank Python host (rank_filter_imagej)"
        assert native.undo_steps == 1 and native.redo_steps == 0 and native.snapshot().dirty
        history = native.history()
        assert history.step_limit == 3 and history.pixel_budget == memory_mb * 1024 * 1024
        np.testing.assert_array_equal(output.native_snapshot().dataset.plane(), np.array(MINIMUM, np.uint16))
        assert editor.workflow[-1] == record and len(editor.workflow) == len(output_ids)
        unchanged(owner, before)
        prefix = "memory_mb." + str(memory_mb)
        rank_raw.owner(prefix + ".published", native)
        rank_raw.editor(prefix + ".published", editor)
        editor.action("undo", document_id=output.id)
        np.testing.assert_array_equal(native.snapshot().dataset.plane(), np.array(INPUT, np.uint16))
        rank_raw.owner(prefix + ".undo", native)
        editor.action("redo", document_id=output.id)
        np.testing.assert_array_equal(native.snapshot().dataset.plane(), np.array(MINIMUM, np.uint16))
        unchanged(owner, before)
        rank_raw.owner(prefix + ".redo", native)
    rank_raw.owner("source_after", owner)
    rank_raw.finish()


def test_reh05_wrong_thread_refuses_before_job_admission(editor, rank_raw):
    owner = attach(editor)
    before = owner.snapshot()
    rank_raw.owner("source", owner)
    request, options = whole(), ij.RankFilterCommandOptions("maximum", 0)
    with ThreadPoolExecutor(max_workers=1) as worker:
        future = worker.submit(editor.apply_rank, request, options, document_id=owner.id)
        with pytest.raises(RuntimeError, match="thread that created"):
            future.result(timeout=5)
    assert editor.jobs == [] and not editor.busy
    assert editor.workflow == [] and editor.results == []
    unchanged(owner, before)
    rank_raw.scalar("python.wrong_thread.jobs", len(editor.jobs))
    rank_raw.owner("after_refusal", owner)
    rank_raw.editor("after_refusal", editor)
    rank_raw.finish()


def test_reh06_missing_foreign_inactive_and_invalid_requests_admit_no_jobs(editor, rank_raw):
    with pytest.raises((ValueError, IndexError, ij.DocumentUnavailable)):
        editor.apply_rank(whole(), ij.RankFilterCommandOptions("mean", 0))
    assert editor.jobs == [] and not editor.busy
    first = attach(editor)
    second = attach(editor, title="Active owner")
    outsider = ij.Document(ij.Dataset(np.array(INPUT, np.uint16), axes="YX"))
    first_before, second_before = first.snapshot(), second.snapshot()
    for document_id in (first.id, outsider.id, 2**63):
        with pytest.raises((ValueError, IndexError, ij.DocumentUnavailable)):
            editor.apply_rank(whole(), ij.RankFilterCommandOptions("mean", 0), document_id=document_id)
        assert editor.live_document().id == second.id and editor.jobs == [] and not editor.busy
        unchanged(first, first_before)
        unchanged(second, second_before)
    for request, options in (({}, ij.RankFilterCommandOptions()),
                             (whole(), {}),
                             (ij.TargetRequest(mode=ij.ExecutionMode.VOLUME), ij.RankFilterCommandOptions()),
                             (ij.TargetRequest(ij.TargetScope.POSITIONS,
                                positions=[ij.PlanePosition(z=99)]), ij.RankFilterCommandOptions())):
        with pytest.raises((TypeError, ValueError, IndexError)):
            editor.apply_rank(request, options, document_id=second.id)
        assert editor.jobs == [] and not editor.busy
        unchanged(second, second_before)
    unsupported = attach(editor, np.array(INPUT, np.float64), title="Unsupported Float64")
    unsupported_before = unsupported.snapshot()
    with pytest.raises(ValueError, match="Rank filtering"):
        editor.apply_rank(whole(), ij.RankFilterCommandOptions("mean", 0), document_id=unsupported.id)
    assert editor.jobs == [] and not editor.busy
    assert editor.workflow == [] and editor.results == []
    unchanged(unsupported, unsupported_before)
    rank_raw.scalar("python.invalid.jobs", len(editor.jobs))
    rank_raw.owner("first_after_refusal", first)
    rank_raw.owner("active_after_refusal", second)
    rank_raw.owner("unsupported_float64_after_refusal", unsupported)
    rank_raw.editor("after_refusal", editor)
    rank_raw.finish()


def test_reh07_recording_noop_keeps_source_redo_and_no_rank_append(editor, rank_raw):
    pixels = np.full((3, 3), 7, np.uint16)
    owner = ij.Document(ij.Dataset(pixels, axes="YX"), "No-op source")
    edit = owner.begin_edit()
    edit.set_property("redo entry", True)
    owner.commit(edit)
    owner.undo()
    editor.add_native_document(owner)
    before = owner.snapshot()
    rank_raw.owner("source", owner)
    editor.recording = True
    editor.apply_rank(whole(), ij.RankFilterCommandOptions("minimum", 0), wait=True)
    unchanged(owner, before, redo=1)
    assert editor.workflow == [] and editor.results == []
    assert len(editor.jobs) == 1 and editor.jobs[0].state == ij.JobState.COMPLETED
    rank_raw.scalar("python.noop.redo", owner.redo_steps)
    rank_raw.scalar("python.noop.workflow", len(editor.workflow))
    rank_raw.owner("noop", owner)
    rank_raw.editor("noop", editor)
    editor.action("redo", document_id=owner.id)
    assert owner.snapshot().metadata["properties"]["redo entry"] is True
    np.testing.assert_array_equal(owner.snapshot().dataset.plane(), pixels)
    rank_raw.owner("redo", owner)
    rank_raw.finish()


EXPECTED_COLLECTION_IDS = (
    "test_reh01_method_dtype_literal_editor_native_interop[uint8-minimum]",
    "test_reh01_method_dtype_literal_editor_native_interop[uint8-maximum]",
    "test_reh01_method_dtype_literal_editor_native_interop[uint8-median]",
    "test_reh01_method_dtype_literal_editor_native_interop[uint8-mean]",
    "test_reh01_method_dtype_literal_editor_native_interop[uint16-minimum]",
    "test_reh01_method_dtype_literal_editor_native_interop[uint16-maximum]",
    "test_reh01_method_dtype_literal_editor_native_interop[uint16-median]",
    "test_reh01_method_dtype_literal_editor_native_interop[uint16-mean]",
    "test_reh01_method_dtype_literal_editor_native_interop[float32-minimum]",
    "test_reh01_method_dtype_literal_editor_native_interop[float32-maximum]",
    "test_reh01_method_dtype_literal_editor_native_interop[float32-median]",
    "test_reh01_method_dtype_literal_editor_native_interop[float32-mean]",
    "test_reh02_wrapper_defaults_are_recorded_median_radius_two",
    "test_reh03_scientific_positions_roi_hole_capture_survives_argument_and_view_changes",
    "test_reh04_new_document_native_membership_and_configured_history_policy",
    "test_reh05_wrong_thread_refuses_before_job_admission",
    "test_reh06_missing_foreign_inactive_and_invalid_requests_admit_no_jobs",
    "test_reh07_recording_noop_keeps_source_redo_and_no_rank_append",
)
