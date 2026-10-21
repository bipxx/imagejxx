"""Python launcher development checks using public Editor and actual ImGui input.

Each render_headless call owns a fresh ImGui context. Complete mouse/key/text
gestures stay inside one call; method selection opens and navigates the combo
in that same context. This is neither persistent desktop interaction nor
installed/OS/GPU qualification. Readbacks are the native CPU upload inputs.
Expected rank pixels below are owned literals, never another filter's output.
"""

from copy import deepcopy
import importlib
import os
import time

import imagejxx as ij
import numpy as np
import pytest

from rank_observations import RawCase


SPIKE = ((0, 0, 0, 0, 0), (0, 0, 0, 0, 0), (0, 0, 80, 0, 0),
         (0, 0, 0, 0, 0), (0, 0, 0, 0, 0))
LOW = ((0, 0, 0, 0, 0),) * 5
MAXIMUM = ((0, 0, 0, 0, 0), (0, 0, 80, 0, 0), (0, 80, 80, 80, 0),
           (0, 0, 80, 0, 0), (0, 0, 0, 0, 0))
MEAN = ((0, 0, 0, 0, 0), (0, 0, 16, 0, 0), (0, 16, 16, 16, 0),
        (0, 0, 16, 0, 0), (0, 0, 0, 0, 0))
HOLED_MEAN = ((0, 0, 0, 0, 0), (0, 0, 16, 0, 0), (0, 16, 80, 16, 0),
              (0, 0, 16, 0, 0), (0, 0, 0, 0, 0))
RING = ((0, 0, 0, 0, 0), (0, 255, 255, 255, 0), (0, 255, 0, 255, 0),
        (0, 255, 255, 255, 0), (0, 0, 0, 0, 0))
METHOD_CASES = (("minimum", np.uint8), ("maximum", np.uint16),
                ("median", np.float32), ("mean", np.uint16))
METHOD_IDS = ("minimum-u8", "maximum-u16", "median-f32", "mean-u16")
DISMISSALS = ("cancel", "escape", "title-close")
STACK_CHOICES = ("yes", "no", "cancel")
TARGET_MODES = ("current", "all-z", "ranged-tcz")
PREPARED_EXITS = ("cancel", "selection-stale", "close")


@pytest.fixture
def editor(tmp_path):
    required = os.environ.get("IMAGEJXX_REQUIRE_RENDER_TESTS", "0")
    assert required in {"0", "1"}
    if required == "1":
        importlib.import_module("imagejxx._desktop")
    else:
        pytest.importorskip("imagejxx._desktop", exc_type=ModuleNotFoundError)
    from imagejxx.ui.app import Editor
    from imagejxx.ui.persistence import Preferences
    return Editor(preferences=Preferences(tmp_path / "preferences.json"))


@pytest.fixture
def rank_raw(request):
    return RawCase(request.node.name)


def _pixels(values=SPIKE, dtype=np.uint8, base=0):
    return np.array(values, dtype=dtype) + dtype(base)


def _expected(method, dtype=np.uint8, base=0):
    return _pixels({"minimum": LOW, "maximum": MAXIMUM,
                    "median": LOW, "mean": MEAN}[method], dtype, base)


def _owner(editor, pixels=None, axes="YX"):
    owner = ij.Document(ij.Dataset(_pixels() if pixels is None else pixels, axes=axes),
                        "Owned rank UI literal")
    editor.add_native_document(owner)
    return owner


def _scientific(editor):
    pixels = np.empty((2, 2, 3, 5, 5), np.uint8)
    for t in range(2):
        for c in range(2):
            for z in range(3):
                pixels[t, c, z] = _pixels(base=10*t + 20*c + 30*z)
    owner = _owner(editor, pixels, "TCZYX")
    _navigate(editor, owner, 1, 1, 2)
    return owner, pixels


def _navigate(editor, owner, t, c, z, *, orthogonal=False):
    display = owner.snapshot().display
    display.t, display.c, display.crosshair = t, c, [2, 2, z]
    display.orthogonal = orthogonal
    editor.live_document(owner.id).set_display(display)


def _data(owner):
    snapshot, history = owner.snapshot(), owner.history()
    return {"id": owner.id, "identity": snapshot.data_identity,
            "bytes": snapshot.dataset.numpy("TCZYXS").tobytes(),
            "shape": tuple(snapshot.dataset.shape), "metadata": snapshot.metadata,
            "dirty": snapshot.dirty,
            "revisions": (snapshot.revisions.pixels, snapshot.revisions.metadata,
                          snapshot.revisions.selection),
            "history": (history.undo_steps, history.redo_steps,
                        history.step_limit, history.pixel_budget)}


def _key(name, down):
    return {"type": "key", "name": name, "down": down}


def _click(point):
    assert len(point) == 2 and all(float(value) >= 0 for value in point)
    return [{"type": "position", "point": list(point), "space": "screen"},
            {"type": "button", "name": "left", "down": True},
            {"type": "button", "name": "left", "down": False}]


def _text(point, value, *, enter=False):
    # Radius Enter accepts the whole dialog; only target fields consume Enter.
    events = [*_click(point), _key("Ctrl", True), _key("A", True),
              _key("A", False), _key("Ctrl", False),
              {"type": "text", "value": value}]
    return events + ([_key("Enter", True), _key("Enter", False)] if enter else [])


def _contains(rect, point):
    return rect[0] <= point[0] <= rect[2] and rect[1] <= point[1] <= rect[3]


def _control(report, name, *, target=False):
    panel = report["rank_panel"]
    controls = panel["operation_target"] if target else panel
    assert controls["control_states"][name]["enabled"], (name, panel)
    point = controls["controls"][name]
    assert _contains(controls["control_rects"][name], point)
    assert controls["control_states"][name]["id"] != 0
    return point


def _press(editor, report, name, *, target=False):
    point = _control(report, name, target=target)
    control = (report["rank_panel"]["operation_target"] if target
               else report["rank_panel"])["control_states"][name]["id"]
    result = editor.render_headless(events=_click(point))
    assert any(frame["focus"]["active_id"] == control
               for frame in result["input_frames"]), (name, result["input_frames"])
    return result


def _radius(editor, report, value):
    result = editor.render_headless(events=_text(_control(report, "radius"), value))
    assert result["rank_panel"]["radius_text"] == value
    return result


def _target_text(editor, report, name, value):
    return editor.render_headless(events=_text(_control(report, name, target=True),
                                                value, enter=True))


def _open(editor):
    editor.action("show_operation", operation="rank_filter_imagej")
    report = editor.render_headless()
    panel = report["rank_panel"]
    assert panel["open"] and panel["draft_valid"] and not panel["error"]
    assert panel["method"] == "median" and not panel["new_document"]
    return report


def _method(editor, report, method):
    # A popup's first diagnostic row rectangles can precede auto-fit/placement.
    # Never transport those rectangles into a newly created ImGui context.
    # Keyboard navigation is real public input, and all popup input is kept in
    # this single call, including time for the opening/default-focus frames.
    methods = ("minimum", "maximum", "median", "mean")
    row_name = "method." + method
    events = [*_click(_control(report, "method")), {"type": "wait", "frames": 2}]
    for key in ("Home",) + ("Down",) * methods.index(method):
        events += [{**_key(key, True), "frames": 2},
                   {**_key(key, False), "frames": 2}]
    events += [_key("Enter", True), _key("Enter", False)]
    result = editor.render_headless(events=events)
    popup_frames = [frame for frame in result["input_frames"]
                    if frame["rank_panel"]["method_popup"]]
    assert popup_frames and all("method." + name in popup_frames[-1]["rank_panel"]["controls"]
                                for name in methods)
    assert any(frame["focus"]["nav_id"] ==
               frame["rank_panel"]["control_states"][row_name]["id"]
               for frame in popup_frames), (method, popup_frames)
    assert result["rank_panel"]["method"] == method
    assert not result["rank_panel"]["method_popup"]
    return result


def _drain(editor):
    deadline = time.monotonic() + 5
    for _ in range(5000):
        editor.poll()
        if not editor.busy:
            assert not editor.error, editor.error
            return
        assert time.monotonic() < deadline, editor.job_info
        time.sleep(.001)
    pytest.fail("Native editor Apply did not terminate within the bounded poll loop")


def _preview(editor, report):
    report = _press(editor, report, "preview")
    expected_generation = report["rank_panel"]["generation"]
    deadline = time.monotonic() + 5
    for _ in range(1000):
        assert report["rank_panel"]["open"] and not report["rank_panel"]["preview_error"]
        if report["gaussian_jobs"]["active_installed_generation"] == expected_generation:
            assert report["gaussian_jobs"]["preview"]["command_record"]["operation"] == "rank_filter_imagej"
            return report
        assert time.monotonic() < deadline, report["gaussian_jobs"]
        time.sleep(.001)
        report = editor.render_headless(frames=2)
    pytest.fail("Rank preview was not installed within the bounded frame loop")


def _readback(report, wanted, *, rgb=None):
    data = report["readback"]
    assert data["available"]
    source = data["source"]
    assert source["shape"] == [*wanted.shape, 1]
    assert source["dtype"] == {np.dtype("uint8"): "uint8", np.dtype("uint16"): "uint16",
                               np.dtype("float32"): "float32"}[wanted.dtype]
    np.testing.assert_array_equal(np.array(source["pixels"]).reshape(source["shape"])[..., 0], wanted)
    if rgb is not None:
        rendered = data["rendered_rgb"]
        assert rendered["dtype"] == "uint8" and rendered["shape"] == list(rgb.shape)
        np.testing.assert_array_equal(np.array(rendered["pixels"]).reshape(rgb.shape), rgb)


def _whole(destination=ij.OutputDestination.IN_PLACE):
    return ij.TargetRequest(roi=ij.RoiPolicy.WHOLE_PLANE, destination=destination)


@pytest.mark.parametrize("case", METHOD_CASES, ids=METHOD_IDS)
def test_ru01_actual_selector_radius_preview_and_apply_use_literal_native_pixels(editor, case, rank_raw):
    method, dtype = case
    owner = _owner(editor, _pixels(dtype=dtype))
    before = _data(owner)
    rank_raw.owner("source", owner)
    editor.recording = True
    report = _radius(editor, _method(editor, _open(editor), method), "0")
    assert report["rank_panel"]["radius"] == 0
    record = ij.resolve_rank_command(owner.snapshot(), ij.TargetRequest(),
                                     ij.RankFilterCommandOptions(method, 0)).record()
    preview = _preview(editor, report)
    assert preview["ordinary_pixels"]["pixels_source"] == "preview"
    assert preview["ordinary_pixels"]["preview_generation"] == preview["rank_panel"]["generation"]
    _readback(preview["ordinary_pixels"], _expected(method, dtype))
    assert _data(owner) == before and editor.workflow == []
    rank_raw.json("preview.report", preview)
    _press(editor, preview, "ok")
    _drain(editor)
    assert owner.snapshot().dataset.numpy("YX").tobytes() == _expected(method, dtype).tobytes()
    assert owner.undo_steps == 1 and owner.redo_steps == 0 and owner.snapshot().dirty
    assert editor.workflow == [record]
    assert not editor.render_headless()["rank_panel"]["open"]
    rank_raw.owner("published", owner)
    rank_raw.editor("published", editor)
    owner.undo()
    assert owner.snapshot().dataset.numpy("YX").tobytes() == _pixels(dtype=dtype).tobytes()
    rank_raw.owner("undo", owner)
    owner.redo()
    assert owner.snapshot().dataset.numpy("YX").tobytes() == _expected(method, dtype).tobytes()
    assert editor.workflow == [record]
    rank_raw.owner("redo", owner)
    rank_raw.finish()


@pytest.mark.parametrize("dismissal", DISMISSALS, ids=DISMISSALS)
def test_ru02_unaccepted_draft_preview_and_destination_are_discarded(editor, dismissal, rank_raw):
    owner = _owner(editor)
    before = _data(owner)
    rank_raw.owner("source", owner)
    report = _radius(editor, _open(editor), "0.25")
    report = _press(editor, report, "new_document")
    report = _preview(editor, report)
    assert report["rank_panel"]["preview_target"]["destination"] == "in_place"
    if dismissal == "cancel":
        closed = _press(editor, report, "cancel")
    elif dismissal == "escape":
        closed = editor.render_headless(events=[_key("Escape", True), _key("Escape", False)])
    else:
        closed = editor.render_headless(events=_click(report["rank_panel"]["close_point"]))
    assert not closed["rank_panel"]["open"]
    assert closed["gaussian_jobs"]["active_installed_generation"] is None
    assert _data(owner) == before and editor.workflow == [] and len(editor.document_infos) == 1
    reopened = _open(editor)["rank_panel"]
    assert reopened["radius"] == 2 and reopened["remembered_radii"] == dict.fromkeys(
        ("minimum", "maximum", "median", "mean", "variance"), 2)
    assert not reopened["new_document"]
    rank_raw.json("preview.report", report)
    rank_raw.json("closed.report", closed)
    rank_raw.json("reopened.panel", reopened)
    rank_raw.owner("after_cancel", owner)
    rank_raw.editor("after_cancel", editor)
    rank_raw.finish()


def test_ru03_accept_commits_only_method_memory_and_new_document_is_exactly_attached(editor, rank_raw):
    owner = _owner(editor)
    source = _data(owner)
    rank_raw.owner("source", owner)
    editor.recording = True
    report = _radius(editor, _method(editor, _open(editor), "mean"), "0")
    report = _press(editor, report, "new_document")
    report = _preview(editor, report)
    assert report["rank_panel"]["target"]["destination"] == "new_document"
    assert report["rank_panel"]["preview_target"]["destination"] == "in_place"
    rank_raw.json("preview.report", report)
    record = ij.resolve_rank_command(owner.snapshot(), ij.TargetRequest(
        destination=ij.OutputDestination.NEW_DOCUMENT),
                                     ij.RankFilterCommandOptions("mean", 0)).record()
    _press(editor, report, "ok")
    _drain(editor)
    assert len(editor.document_infos) == 2 and _data(owner) == source
    output = editor.live_document().native_owner
    assert output.id != owner.id and editor.live_document(output.id).native_snapshot().id == output.id
    assert output.snapshot().dataset.numpy("YX").tobytes() == _pixels(MEAN).tobytes()
    assert output.undo_steps == 1 and output.snapshot().dirty and editor.workflow == [record]
    rank_raw.owner("published.output", output)
    rank_raw.owner("published.source", owner)
    rank_raw.editor("published", editor)
    output.undo()
    assert output.snapshot().dataset.numpy("YX").tobytes() == _pixels().tobytes()
    assert _data(owner) == source and len(editor.document_infos) == 2
    report = _open(editor)
    assert report["rank_panel"]["radius"] == 2 and not report["rank_panel"]["new_document"]
    report = _method(editor, report, "mean")
    assert report["rank_panel"]["radius"] == 0
    assert report["rank_panel"]["remembered_radii"] == {
        "minimum": 2, "maximum": 2, "median": 2, "mean": 0, "variance": 2}
    rank_raw.owner("undo.output", output)
    rank_raw.json("reopened.panel", report["rank_panel"])
    rank_raw.finish()


@pytest.mark.parametrize("choice", STACK_CHOICES, ids=STACK_CHOICES)
def test_ru04_legacy_stack_prompt_retains_captured_z_and_first_acceptance(editor, choice, rank_raw):
    source = np.stack([_pixels(dtype=np.uint16, base=base) for base in (0, 10, 20)])
    owner = _owner(editor, source, "ZYX")
    _navigate(editor, owner, 0, 0, 1)
    before = _data(owner)
    rank_raw.owner("source", owner)
    report = _radius(editor, _method(editor, _open(editor), "mean"), "0")
    report = _press(editor, report, "ok")
    assert report["rank_panel"]["stack_prompt"] and not editor.busy
    assert report["rank_panel"]["remembered_radii"]["mean"] == 0
    rank_raw.json("stack_prompt.report", report)
    _navigate(editor, owner, 0, 0, 2)
    report = editor.render_headless()
    _press(editor, report, "stack_" + choice)
    _drain(editor)
    wanted = source.copy()
    if choice != "cancel":
        for z in (range(3) if choice == "yes" else (1,)):
            wanted[z] = _expected("mean", np.uint16, 10*z)
    assert owner.snapshot().dataset.numpy("ZYX").tobytes() == wanted.tobytes()
    assert owner.undo_steps == (choice != "cancel")
    assert owner.snapshot().display.crosshair[2] == 2
    if choice == "cancel":
        assert _data(owner) == before
    reopened = _method(editor, _open(editor), "mean")["rank_panel"]
    assert reopened["radius"] == 0 and not reopened["new_document"]
    rank_raw.json("reopened.panel", reopened)
    rank_raw.owner("after_stack_choice", owner)
    rank_raw.editor("after_stack_choice", editor)
    rank_raw.finish()


@pytest.mark.parametrize("mode", TARGET_MODES, ids=TARGET_MODES)
def test_ru05_scientific_targets_keep_holes_contained_preview_and_captured_apply(editor, mode, rank_raw):
    owner, pixels = _scientific(editor)
    owner.select(ij.Selection.from_mask(np.array(RING, np.uint8)))
    before = _data(owner)
    rank_raw.owner("source", owner)
    editor.recording = True
    report = _radius(editor, _method(editor, _open(editor), "mean"), "0")
    report = _press(editor, report, "roi.active_selection", target=True)
    positions = [(1, 1, 2)]
    request = ij.TargetRequest(ij.TargetScope.RANGES, t=ij.AxisRange(1, 2),
                               c=ij.AxisRange(1, 2), z=ij.AxisRange(2, 3))
    if mode == "all-z":
        report = _press(editor, report, "z.all", target=True)
        positions = [(1, 1, z) for z in range(3)]
        request = ij.TargetRequest(ij.TargetScope.RANGES, t=ij.AxisRange(1, 2),
                                   c=ij.AxisRange(1, 2), z=ij.AxisRange(0, 3))
    elif mode == "ranged-tcz":
        report = _press(editor, report, "t.all", target=True)
        report = _press(editor, report, "c.all", target=True)
        report = _press(editor, report, "z.range", target=True)
        for field, value in (("z.first", "1"), ("z.last", "3"), ("z.step", "2")):
            report = _target_text(editor, report, field, value)
        positions = [(t, c, z) for t in range(2) for c in range(2) for z in (0, 2)]
        request = ij.TargetRequest(ij.TargetScope.RANGES, t=ij.AxisRange(0, 2),
                                   c=ij.AxisRange(0, 2), z=ij.AxisRange(0, 3, 2))
        report = _target_text(editor, report, "preview.ordinal", "1")
    record = ij.resolve_rank_command(owner.snapshot(), request,
                                     ij.RankFilterCommandOptions("mean", 0)).record()
    assert report["rank_panel"]["target"] == record["target"]
    assert report["rank_panel"]["operation_target"]["selected_plane_count"] == len(positions)
    preview_position = (0, 0, 0) if mode == "ranged-tcz" else (1, 1, 2)
    if mode == "ranged-tcz":
        report = _press(editor, report, "preview.show", target=True)
    report = _preview(editor, report)
    assert report["rank_panel"]["preview_position"] == list(preview_position)
    preview_request = ij.TargetRequest(ij.TargetScope.POSITIONS,
                                       positions=[ij.PlanePosition(*preview_position)])
    preview_record = ij.resolve_rank_command(owner.snapshot(), preview_request,
                                             ij.RankFilterCommandOptions("mean", 0)).record()
    assert report["rank_panel"]["preview_target"] == preview_record["target"]
    t, c, z = preview_position
    _readback(report["ordinary_pixels"], _pixels(HOLED_MEAN, base=10*t + 20*c + 30*z))
    assert _data(owner) == before and editor.workflow == []
    rank_raw.json("preview.report", report)
    _navigate(editor, owner, 0, 0, 1)
    fresh = owner.snapshot().display
    report = editor.render_headless()
    assert report["rank_panel"]["target"] == record["target"]
    assert report["ordinary_pixels"]["pixels_source"] == "live"
    rank_raw.json("fresh_navigation.report", report)
    _press(editor, report, "ok")
    _drain(editor)
    wanted = pixels.copy()
    for t, c, z in positions:
        wanted[t, c, z] = _pixels(HOLED_MEAN, base=10*t + 20*c + 30*z)
    assert owner.snapshot().dataset.numpy("TCZYX").tobytes() == wanted.tobytes()
    display = owner.snapshot().display
    assert (display.t, display.c, display.crosshair) == (fresh.t, fresh.c, fresh.crosshair)
    assert owner.undo_steps == 1 and editor.workflow == [record]
    rank_raw.owner("published", owner)
    rank_raw.editor("published", editor)
    rank_raw.finish()


def test_ru06_composite_and_linked_readbacks_route_only_the_captured_preview_plane(editor, rank_raw):
    owner, pixels = _scientific(editor)
    state = {"schema": "imagejxx.channel-display", "version": 1, "mode": "composite",
             "channels": [{"visible": True, "range": [0., 255.], "lut": {"name": name}}
                          for name in ("Red", "Green")]}
    editor.apply_channel_display({"action": "set", "state": state, "channels": []})
    before = _data(owner)
    rank_raw.owner("source", owner)
    report = _preview(editor, _radius(editor, _method(editor, _open(editor), "mean"), "0"))
    generation = report["rank_panel"]["generation"]
    transient = pixels.copy()
    transient[1, 1, 2] = _expected("mean", base=90)

    def rgb(red, green):
        return np.stack([red, green, np.zeros_like(red)], axis=-1)

    _readback(report["ordinary_pixels"], transient[1, 1, 2],
              rgb=rgb(pixels[1, 0, 2], transient[1, 1, 2]))
    contributors = report["ordinary_pixels"]["channel_display"]["contributors"]
    assert [(row["c"], row["preview"], row["generation"]) for row in contributors] == [
        (0, False, 0), (1, True, generation)]
    _navigate(editor, owner, 1, 1, 2, orthogonal=True)
    linked = editor.render_headless()
    wanted = {"XY": (transient[1, 1, 2], pixels[1, 0, 2]),
              "XZ": (transient[1, 1, :, 2, :], pixels[1, 0, :, 2, :]),
              "YZ": (transient[1, 1, :, :, 2], pixels[1, 0, :, :, 2])}
    assert {plane["plane"] for plane in linked["orthogonal_planes"]} == set(wanted)
    for plane in linked["orthogonal_planes"]:
        green, red = wanted[plane["plane"]]
        assert plane["pixels_source"] == "transient" and plane["preview_generation"] == generation
        assert plane["source_document_id"] == owner.id
        _readback(plane, green, rgb=rgb(red, green))
        contributors = plane["channel_display"]["contributors"]
        assert [(row["c"], row["preview"], row["generation"]) for row in contributors] == [
            (0, False, 0), (1, True, generation)]
        assert contributors[0]["preview_z"] == [] and contributors[1]["preview_z"] == [2]
    assert _data(owner) == before
    rank_raw.json("ordinary_preview.report", report)
    rank_raw.json("linked_preview.report", linked)
    _press(editor, linked, "cancel")
    restored = editor.render_headless()
    for plane in restored["orthogonal_planes"]:
        assert plane["pixels_source"] == "live_document" and plane["preview_generation"] == 0
    assert _data(owner) == before
    rank_raw.json("restored.report", restored)
    rank_raw.owner("after_cancel", owner)
    rank_raw.finish()


@pytest.mark.parametrize("empty", (False, True), ids=("unchanged", "empty-mask"))
def test_ru07_noop_or_empty_actual_apply_preserves_redo_and_recording(editor, empty, rank_raw):
    owner = _owner(editor, _pixels() if empty else np.zeros((5, 5), np.uint8))
    edit = owner.begin_edit()
    edit.set_property("undo_fixture", 1)
    owner.commit(edit)
    owner.undo()
    if empty:
        owner.select(ij.Selection.from_mask(np.zeros((5, 5), np.uint8)))
    before = _data(owner)
    rank_raw.owner("source", owner)
    assert owner.redo_steps == 1
    editor.recording = True
    report = _radius(editor, _open(editor), "0")
    _press(editor, report, "ok")
    _drain(editor)
    assert _data(owner) == before and editor.workflow == []
    rank_raw.owner("noop", owner)
    rank_raw.editor("noop", editor)
    rank_raw.finish()


def test_ru08_strict_replay_native_v3_and_explicit_reopened_rebind(editor, tmp_path, rank_raw):
    owner = _owner(editor)
    rank_raw.owner("source", owner)
    editor.recording = True
    record = ij.resolve_rank_command(owner.snapshot(), _whole(),
                                     ij.RankFilterCommandOptions("mean", 0)).record()
    frozen = deepcopy(record)
    editor.action("rank_replay", record=record, document_id=owner.id)
    _drain(editor)
    assert owner.snapshot().dataset.numpy("YX").tobytes() == _pixels(MEAN).tobytes()
    assert editor.workflow == [record]
    rank_raw.owner("strict_published", owner)
    rank_raw.editor("strict_published", editor)
    before = _data(owner)
    with pytest.raises((ValueError, RuntimeError)) as repeat_failure:
        editor.action("rank_replay", record=record, document_id=owner.id)
    assert _data(owner) == before and editor.workflow == [record]
    rank_raw.scalar("python.strict_repeat.accepted", repeat_failure.type is None)
    rank_raw.json("strict_repeat.error", {"type": repeat_failure.type.__name__,
                                         "message": str(repeat_failure.value)})
    del repeat_failure
    owner.undo()
    assert owner.snapshot().dataset.numpy("YX").tobytes() == _pixels().tobytes()
    path = tmp_path / "rank-development-native-v3.ijx"
    editor.action("save_document", path=str(path))
    assert path.is_file()
    from imagejxx.ui.app import Editor
    from imagejxx.ui.persistence import Preferences
    reopened = Editor(preferences=Preferences(tmp_path / "reopened-preferences.json"))
    reopened.open_file(path)
    restored = reopened.live_document().native_owner
    assert restored.id != owner.id and reopened.workflow == [record]
    assert restored.snapshot().dataset.numpy("YX").tobytes() == _pixels().tobytes()
    rank_raw.owner("reopened", restored)
    if rank_raw.enabled:
        raw = path.read_bytes()
        rank_raw.json("native_session.file", {"byte_count": len(raw), "hex": raw.hex()})
    before = _data(restored)
    with pytest.raises((ValueError, RuntimeError)) as reopened_failure:
        reopened.action("rank_replay", record=record, document_id=restored.id)
    assert _data(restored) == before
    rank_raw.scalar("python.reopened_strict.accepted", reopened_failure.type is None)
    rank_raw.json("reopened_strict.error", {"type": reopened_failure.type.__name__,
                                           "message": str(reopened_failure.value)})
    del reopened_failure
    rebind = {"scope": "positions", "positions": [[0, 0, 0]],
              "roi": "whole_plane", "mode": "planes", "destination": "in_place"}
    reopened.action("rank_replay", record=record, rank_rebind=rebind, document_id=restored.id)
    _drain(reopened)
    assert restored.snapshot().dataset.numpy("YX").tobytes() == _pixels(MEAN).tobytes()
    assert restored.undo_steps == 1 and record == frozen
    if rank_raw.enabled:
        dispatched = reopened.render_headless()
        actual = dispatched["gaussian_jobs"]["apply"]["command_record"]
        assert actual["operation"] == "rank_filter_imagej"
        assert actual["target"]["positions"] == [[0, 0, 0]]
        rank_raw.scalar("python.rebound.radius", actual["options"]["radius"])
        rank_raw.scalar("python.rebound.count", len(actual["target"]["positions"]))
        rank_raw.json("rebound.actual_dispatch_report", dispatched)
    rank_raw.owner("rebound", restored)
    rank_raw.editor("rebound", reopened)
    rank_raw.json("original.record", record)
    rank_raw.finish()


@pytest.mark.parametrize("exit_kind", PREPARED_EXITS, ids=PREPARED_EXITS)
def test_ru09_public_apply_cancellation_stale_or_closed_never_publishes(editor, exit_kind, rank_raw):
    owner = _owner(editor)
    request = _whole()
    if exit_kind == "selection-stale":
        # WholePlane deliberately does not depend on selection revisions.
        # Capture an actual area and its ActiveSelection request before work.
        owner.select(ij.Selection("rectangle", [(1, 1), (4, 4)]))
        request = ij.TargetRequest(roi=ij.RoiPolicy.ACTIVE_SELECTION)
        captured = ij.resolve_rank_command(owner.snapshot(), request,
                                           ij.RankFilterCommandOptions("mean", 0))
        assert captured.record()["target"]["roi_policy"] == "active_selection"
    before = _data(owner)
    rank_raw.owner("source", owner)
    editor.recording = True
    editor.apply_rank(request, ij.RankFilterCommandOptions("mean", 0), document_id=owner.id)
    assert editor.busy
    if exit_kind == "cancel":
        editor.cancel()
    elif exit_kind == "selection-stale":
        owner.select(ij.Selection("rectangle", [(0, 0), (1, 1)]))
    else:
        editor.close_document(owner.id, discard=True)
    # No frame/poll occurred between request and the cancellation/stale/close.
    # Work may already be READY; none of those outcomes authorizes publication.
    deadline = time.monotonic() + 5
    for _ in range(5000):
        editor.poll()
        if not editor.busy:
            break
        assert time.monotonic() < deadline
        time.sleep(.001)
    assert not editor.busy
    after = _data(owner)
    assert after["bytes"] == before["bytes"] and after["identity"] == before["identity"]
    assert after["history"] == before["history"] and after["dirty"] == before["dirty"]
    assert after["revisions"][:2] == before["revisions"][:2]
    assert after["revisions"][2] == before["revisions"][2] + (exit_kind == "selection-stale")
    assert editor.workflow == []
    assert len(editor.document_infos) == (0 if exit_kind == "close" else 1)
    rank_raw.owner("after_refusal", owner)
    rank_raw.editor("after_refusal", editor)
    if exit_kind == "cancel":
        rank_raw.scalar("python.cancel.workflow", len(editor.workflow))
    if exit_kind == "close":
        rank_raw.scalar("python.close.members", len(editor.document_infos))
    rank_raw.finish()


def test_ru10_public_apply_wrapper_rejects_untyped_request_without_editing(editor, rank_raw):
    owner = _owner(editor)
    before = _data(owner)
    rank_raw.owner("source", owner)
    for invalid in ({}, True, "all_z"):
        with pytest.raises(TypeError):
            editor.apply_rank(invalid, ij.RankFilterCommandOptions("mean", 0))
        assert not editor.busy and _data(owner) == before
    editor.apply_rank(_whole(), ij.RankFilterCommandOptions("mean", 0))
    _drain(editor)
    assert owner.snapshot().dataset.numpy("YX").tobytes() == _pixels(MEAN).tobytes()
    assert owner.undo_steps == 1
    rank_raw.owner("published", owner)
    rank_raw.editor("published", editor)
    rank_raw.finish()


def test_ru11_actual_invalid_radius_cannot_preview_or_apply_and_can_be_corrected(editor, rank_raw):
    owner = _owner(editor)
    before = _data(owner)
    rank_raw.owner("source", owner)
    report = _open(editor)
    for text in ("-1", "nan", "1 trailing", "100.00001"):
        report = _radius(editor, report, text)
        panel = report["rank_panel"]
        assert panel["open"] and not panel["draft_valid"] and panel["error"]
        assert not panel["control_states"]["ok"]["enabled"]
        assert not panel["control_states"]["preview"]["enabled"]
        assert not panel["preview_requested"] and _data(owner) == before
        rank_raw.json("invalid." + text, report)
    report = _radius(editor, report, " +0.25 ")
    assert report["rank_panel"]["draft_valid"] and report["rank_panel"]["radius"] == .25
    assert report["rank_panel"]["remembered_radii"]["median"] == 2
    rank_raw.json("corrected.report", report)
    _press(editor, report, "cancel")
    assert _open(editor)["rank_panel"]["radius"] == 2 and _data(owner) == before
    rank_raw.owner("after_cancel", owner)
    rank_raw.finish()


def test_ru12_combo_escape_consumes_only_the_popup_in_its_actual_input_context(editor, rank_raw):
    owner = _owner(editor)
    before = _data(owner)
    rank_raw.owner("source", owner)
    opened = _open(editor)
    point = _control(opened, "method")
    report = editor.render_headless(events=[*_click(point), _key("Escape", True),
                                              _key("Escape", False)])
    assert any(frame["rank_panel"]["method_popup"] for frame in report["input_frames"])
    assert report["rank_panel"]["open"] and not report["rank_panel"]["method_popup"]
    assert report["rank_panel"]["method"] == "median" and _data(owner) == before
    _press(editor, report, "cancel")
    rank_raw.json("popup_escape.report", report)
    rank_raw.owner("after_cancel", owner)
    rank_raw.finish()


# Exact declared item.name membership for the root's focused collection/run.
# This file does not collect, run products, install, launch OS windows, or emit
# an acceptance receipt. Persistent combo/drag and the ordinary-canvas branch's
# unreported local source choice require separate qualification.
EXPECTED_COLLECTION_IDS = (
    *("test_ru01_actual_selector_radius_preview_and_apply_use_literal_native_pixels[" + name + "]"
      for name in METHOD_IDS),
    *("test_ru02_unaccepted_draft_preview_and_destination_are_discarded[" + name + "]"
      for name in DISMISSALS),
    "test_ru03_accept_commits_only_method_memory_and_new_document_is_exactly_attached",
    *("test_ru04_legacy_stack_prompt_retains_captured_z_and_first_acceptance[" + name + "]"
      for name in STACK_CHOICES),
    *("test_ru05_scientific_targets_keep_holes_contained_preview_and_captured_apply[" + name + "]"
      for name in TARGET_MODES),
    "test_ru06_composite_and_linked_readbacks_route_only_the_captured_preview_plane",
    "test_ru07_noop_or_empty_actual_apply_preserves_redo_and_recording[unchanged]",
    "test_ru07_noop_or_empty_actual_apply_preserves_redo_and_recording[empty-mask]",
    "test_ru08_strict_replay_native_v3_and_explicit_reopened_rebind",
    *("test_ru09_public_apply_cancellation_stale_or_closed_never_publishes[" + name + "]"
      for name in PREPARED_EXITS),
    "test_ru10_public_apply_wrapper_rejects_untyped_request_without_editing",
    "test_ru11_actual_invalid_radius_cannot_preview_or_apply_and_can_be_corrected",
    "test_ru12_combo_escape_consumes_only_the_popup_in_its_actual_input_context",
)
