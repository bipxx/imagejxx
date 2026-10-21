"""Document correctness and real Dear ImGui canvas/controller interactions."""

from dataclasses import asdict
import json
import os
from pathlib import Path
import subprocess
import numpy as np
import pytest
import imagejxx as ij
from imagejxx.ui.commands import OPERATIONS, json_values
from imagejxx.ui.model import Calibration, Document, Overlay, Selection
from imagejxx.ui.persistence import (
    Preferences,
    load_rois,
    load_session,
    save_rois,
    save_session,
)
from imagejxx.ui import workflows


def test_document_transaction_history_and_calibration():
    document = Document(np.arange(100, dtype=np.uint16).reshape(10, 10))
    document.select(Selection("rectangle", [(2, 2), (5, 6)]))
    document.apply(ij.add, params={"value": 10})
    assert document.frame[0, 0] == 0 and document.frame[3, 3] == 43 and document.dirty
    assert not document.frame.flags.writeable
    document.mark_saved()
    document.undo()
    assert document.dirty
    document.redo()
    assert not document.dirty
    before = document.data.copy()
    revision = document.revision
    with pytest.raises(ValueError):
        document.apply(ij.divide, params={"value": 0})
    np.testing.assert_array_equal(document.data, before)
    assert document.revision == revision
    document.set_calibration(Calibration(0.5, 2, 3, "µm"))
    assert document.measure()["Area"] == 12
    document.select(Selection("angle", [(0, 0), (2, 0), (2, 2)]))
    assert document.measure()["Angle"] == 90
    with pytest.raises(ValueError):
        document.set_calibration(Calibration(pixel_width=0))


@pytest.mark.parametrize(
    "selection",
    [
        Selection("rectangle", [(1, 1), (6, 6)]),
        Selection("ellipse", [(1, 1), (6, 6)]),
        Selection("polygon", [(1, 1), (6, 1), (3, 6)]),
        Selection("freehand", [(1, 1), (6, 1), (3, 6)]),
        Selection("line", [(1, 1), (6, 6)]),
        Selection("polyline", [(1, 1), (6, 1), (6, 6)]),
        Selection("point", [(2, 2)]),
        Selection("multipoint", [(2, 2), (6, 6)]),
        Selection("mask", bitmap=np.eye(8, dtype=np.uint8) * 255),
    ],
)
def test_selection_roundtrip(selection):
    copied = Selection.from_dict(json.loads(json.dumps(selection.to_dict())))
    np.testing.assert_array_equal(copied.mask((8, 8)), selection.mask((8, 8)))
    assert selection.bounds((8, 8))[2] > 0


def test_stack_edit_and_workflow_rollback():
    document = Document(np.zeros((3, 8, 8), np.uint8), is_stack=True)
    document.slice_index = 1
    document.apply(ij.add, params={"value": 8})
    assert document.data[:, 0, 0].tolist() == [0, 8, 0]
    document.apply(ij.convert, params={"dtype": "float32"})
    assert document.data.dtype == np.float32
    with pytest.raises(ValueError):
        document.apply(ij.resize, params={"width": 4, "height": 4})
    document.apply(ij.resize, params={"width": 4, "height": 4}, all_slices=True)
    assert document.data.shape == (3, 4, 4)
    steps = [{"operation": "add", "parameters": {"value": 2}, "all_slices": True}]
    np.testing.assert_array_equal(
        workflows.run(document, workflows.loads(workflows.dumps(steps))),
        document.data + 2,
    )
    original = document.data.copy()
    with pytest.raises(ValueError):
        workflows.run(
            document, steps + [{"operation": "divide", "parameters": {"value": 0}}]
        )
    np.testing.assert_array_equal(original, document.data)
    with pytest.raises(InterruptedError):
        workflows.run(document, steps, cancelled=lambda: True)
    with pytest.raises(ValueError):
        workflows.loads(
            '{"format":"imagejxx-workflow","version":1,"steps":[{"operation":"exec"}]}'
        )


def test_sessions_rois_and_preferences(tmp_path):
    pixels = np.arange(120, dtype=np.float64).reshape(2, 6, 10) / 7
    d = Document(pixels, "µm stack", is_stack=True)
    d.slice_index = 1
    d.select(Selection("mask", bitmap=np.eye(6, 10, dtype=np.uint8) * 255))
    d.add_roi("mask")
    d.set_calibration(Calibration(0.3, 0.7, 2, "µm"))
    d.add_overlay(Overlay("text", [(2, 3)], text="test"))
    d.spectrum = ij.fft2(d.frame)
    d.spectrum_shape = d.shape
    path = tmp_path / "session.ijx"
    rows = [d.measure()]
    steps = [{"operation": "add", "parameters": {"value": 1}}]
    save_session(path, [d], results=rows, workflow=steps)
    documents, results, workflow = load_session(path)
    out = documents[0]
    np.testing.assert_array_equal(out.data, d.data)
    np.testing.assert_array_equal(out.spectrum, d.spectrum)
    assert (
        json.loads(json.dumps(out.metadata())) == json.loads(json.dumps(d.metadata()))
        and results[0]["Area"] == rows[0]["Area"]
        and workflow == steps
    )
    roi_path = tmp_path / "rois.json"
    save_rois(roi_path, d.rois)
    np.testing.assert_array_equal(
        load_rois(roi_path, d.shape)[0].selection.mask(d.shape),
        d.selection.mask(d.shape),
    )
    preferences = Preferences(tmp_path / "preferences.json")
    preferences.recent(path)
    preferences.save()
    assert Preferences(preferences.path).values["recent"] == [str(path.resolve())]
    path.write_bytes(b"IMAGEJXX2\n" + b"\xff" * 20)
    with pytest.raises(ValueError):
        load_session(path)


def test_command_catalog_and_parameter_forms():
    catalog = json.loads(
        (Path(ij.ui.__file__).parent / "commands.json").read_text(encoding="utf-8")
    )
    assert {command["id"] for command in catalog} == set(OPERATIONS)
    assert catalog == [
        {
            "id": op.name,
            "title": op.title,
            "menu": list(op.menu),
            "description": op.description,
            "parameters": [json_values(asdict(p)) for p in op.parameters()],
        }
        for op in OPERATIONS.values()
    ]
    for operation in OPERATIONS.values():
        for parameter in operation.parameters():
            assert parameter.name != "kwargs"
            value = parameter.default
            encoded = (
                json.dumps(value)
                if parameter.kind == "json"
                else ""
                if value is None
                else value
            )
            parsed = parameter.parse(encoded)
            assert parsed is None or isinstance(parsed, (int, float, str, bool, list))
    assert OPERATIONS["morphology"].parameters()[0].default == "erode"
    sigma = next(
        p for p in OPERATIONS["gaussian_blur"].parameters() if p.name == "sigma"
    )
    assert sigma.parse("1.5") == 1.5


@pytest.fixture
def editor(tmp_path):
    pytest.importorskip("imagejxx._desktop", exc_type=ModuleNotFoundError)
    from imagejxx.ui.app import Editor

    return Editor(preferences=Preferences(tmp_path / "prefs.json"))


def test_imgui_tools_editing_measurements_and_clipboard(editor):
    document = Document(np.zeros((32, 40), np.uint8), "ImGui test")
    editor.add_document(document)
    frame = editor.render_headless(exercise=True)
    assert frame["vertices"] > 0 and frame["draw_lists"] > 0
    assert editor.document.selection.bounds(editor.document.shape) == (3, 4, 10, 10)
    editor.recording = True
    editor.apply_operation("add", {"value": 20})
    editor.wait()
    assert editor.document.frame[6, 6] == 20 and editor.document.frame[0, 0] == 0
    assert len(editor.workflow) == 1
    editor.action("add_roi")
    editor.action("measure")
    assert editor.results[-1]["count"] == 100
    editor.action("copy")
    editor.action("paste_new")
    assert editor.document.shape == (10, 10)
    # Native UI ownership protects scripts from racing or modifying live pixels.
    assert not np.any(document.data)


def test_imgui_stacks_and_failed_worker(editor):
    editor.add_document(Document(np.zeros((3, 8, 10), np.uint16), is_stack=True))
    editor.apply_operation("add", {"value": 5}, True)
    editor.wait()
    assert np.all(editor.document.data == 5)
    editor.recording = True
    editor.apply_operation("divide", {"value": 0})
    with pytest.raises(RuntimeError):
        editor.wait()
    assert np.all(editor.document.data == 5) and editor.workflow == []
    editor.action("undo")
    assert not np.any(editor.document.data)
    editor.action("redo")
    assert np.all(editor.document.data == 5)


def test_imgui_sessions_workflows_and_calibration(editor, tmp_path):
    editor.add_document(Document(np.arange(80, dtype=np.uint16).reshape(8, 10)))
    editor.select(Selection("rectangle", [(1, 2), (5, 6)]))
    editor.action("calibration", pixel_width=0.5, pixel_height=2, unit="um")
    editor.action("measure")
    assert editor.results[-1]["Area"] == 16
    editor.recording = True
    editor.apply_operation("add", {"value": 3}, asynchronous=False)
    before = editor.document.data.copy()
    editor.action("run_workflow")
    editor.wait()
    assert editor.document.data[3, 2] == before[3, 2] + 3
    editor.action("save_document", path=str(tmp_path / "python-imgui.ijx"))
    extensions = {}
    restored, rows, steps = load_session(tmp_path / "python-imgui.ijx", extensions=extensions)
    assert extensions == {"imagejxx_workspace": {"version": 1, "active_index": 0}}
    np.testing.assert_array_equal(restored[0].data, editor.document.data)
    assert rows == editor.results and steps == editor.workflow
    editor.action("selection_mask")
    assert np.count_nonzero(editor.document.data) == 16


def test_imgui_fft_keeps_valid_document_metadata(editor):
    editor.add_document(Document(np.arange(80, dtype=np.uint8).reshape(8, 10)))
    editor.action("fft")
    editor.wait()
    assert editor.document.spectrum is not None and editor.document.shape == (8, 16)
    editor.action("inverse_fft")
    np.testing.assert_allclose(
        editor.document.data, np.arange(80).reshape(8, 10), atol=1e-10
    )


def test_imgui_projection_and_input_validation(editor):
    editor.add_document(
        Document(np.arange(240, dtype=np.uint16).reshape(3, 8, 10), is_stack=True)
    )
    editor.action("project", method="mean")
    editor.wait()
    assert editor.document.shape == (8, 10) and not editor.document.is_stack
    np.testing.assert_array_equal(
        editor.document.data, np.arange(80).reshape(8, 10) + 80
    )
    with pytest.raises((ValueError, RuntimeError)):
        editor.apply_operation("add", {"unreviewed_parameter": 5})


def test_imgui_thread_ownership_and_hidden_run_validation(editor):
    from concurrent.futures import ThreadPoolExecutor

    editor.add_document(np.zeros((8, 10), np.uint8))
    with ThreadPoolExecutor(max_workers=1) as worker:
        with pytest.raises(RuntimeError, match="thread"):
            worker.submit(lambda: editor.document).result()
    with pytest.raises(ValueError, match="positive frame count"):
        editor.run(hidden=True)


@pytest.mark.parametrize("channels", [3, 4])
@pytest.mark.parametrize("dtype", [np.uint8, np.uint16])
def test_imgui_channel_split_keeps_volume_and_calibration(editor, channels, dtype):
    pixels = (np.arange(4 * 5 * 6 * channels) % 251).astype(dtype)
    pixels = pixels.reshape(4, 5, 6, channels)
    source = Document(pixels, "Color volume", is_stack=True)
    source.slice_index = 2
    source.set_calibration(Calibration(0.5, 0.75, 3, "um", 2, "s"))
    editor.add_document(source)
    editor.action("split_channels")
    assert len(editor.documents) == channels + 1
    for c, document in enumerate(editor.documents[1:]):
        np.testing.assert_array_equal(document.data, pixels[..., c])
        assert document.data.dtype == dtype and document.slice_index == 2
        assert asdict(document.calibration) == asdict(source.calibration)
    np.testing.assert_array_equal(editor.documents[0].data, pixels)


@pytest.mark.parametrize("step", [1, 2])
def test_imgui_substack_preserves_spatial_calibration(editor, step):
    source = Document(np.arange(6 * 5 * 6, dtype=np.uint16).reshape(6, 5, 6),
                      "Calibrated volume", is_stack=True)
    source.set_calibration(Calibration(0.5, 0.75, 3, "um", 2, "s",
        origin=(11, -7, 19), direction=(1, .25, -.5, .5, -1, .125, 0, .75, 1.5)))
    editor.add_document(source)
    editor.action("substack", first=2, last=6, step=step)
    out = editor.document
    np.testing.assert_array_equal(out.data, source.data[1:6:step])
    expected = asdict(source.calibration)
    expected["pixel_depth"] *= step
    expected["origin"] = (9.5, -6.625, 23.5)
    assert asdict(out.calibration) == expected
    assert asdict(editor.documents[0].calibration) == asdict(source.calibration)


def test_imgui_volume_gaussian_preserves_pixels_outside_selection(editor):
    volume = np.zeros((3, 12, 14), np.uint16)
    volume[:, 5, 6] = 2000
    source = Document(volume, "Selected volume", is_stack=True)
    editor.add_document(source)
    selection = Selection("rectangle", [(5, 4), (8, 7)])
    editor.select(selection)
    editor.action("gaussian3d", sigma_x=1, sigma_y=1, sigma_z=1)
    editor.wait()
    mask = selection.mask(volume.shape[1:]) != 0
    expected = np.where(mask, ij.gaussian_blur3d(volume, sigma=1), volume)
    np.testing.assert_array_equal(editor.document.data, expected)
    assert np.any(expected != volume)
    editor.action("undo")
    np.testing.assert_array_equal(editor.document.data, volume)


@pytest.mark.parametrize("reopen", [False, True])
def test_imgui_single_document_save_preserves_workspace(editor, tmp_path, reopen):
    editor.add_document(np.ones((8, 10), np.uint8))
    editor.add_document(np.full((8, 10), 2, np.uint8))
    workspace = tmp_path / "workspace.ijx"
    editor.action("save_workspace", path=str(workspace))
    if reopen:
        from imagejxx.ui.app import Editor

        editor = Editor(preferences=Preferences(tmp_path / "reopened.json"))
        editor.open_file(workspace)
    before = workspace.read_bytes()
    alias = tmp_path / "unused" / ".." / workspace.name
    with pytest.raises(ValueError, match="Save workspace"):
        editor.action("save_document", path=str(alias))
    assert workspace.read_bytes() == before
    editor.action("save_document", path=str(tmp_path / "separate.ijx"))
    single_extensions, workspace_extensions = {}, {}
    assert len(load_session(tmp_path / "separate.ijx", extensions=single_extensions)[0]) == 1
    assert single_extensions == {"imagejxx_workspace": {"version": 1, "active_index": 0}}
    assert len(load_session(workspace, extensions=workspace_extensions)[0]) == 2
    assert workspace_extensions == {"imagejxx_workspace": {"version": 1, "active_index": 1}}
    editor.apply_operation("add", {"value": 3}, asynchronous=False)
    editor.action("save_workspace", path=str(workspace))
    extensions = {}
    restored, _, _ = load_session(workspace, extensions=extensions)
    assert extensions == {"imagejxx_workspace": {"version": 1, "active_index": 1}}
    assert len(restored) == 2
    np.testing.assert_array_equal(
        restored[editor.active_index].data, editor.document.data
    )


def test_imgui_large_bitmap_outlines_update_without_redrawing_stale_geometry(editor):
    editor.add_document(np.zeros((256, 256), np.uint8))
    bitmap = np.zeros((256, 256), np.uint8)
    bitmap[::2, ::2] = 255
    editor.select(Selection("mask", bitmap=bitmap))
    sparse = editor.render_headless(4)
    assert sparse["vertices"] > 65535 and sparse["mask_outline_builds"] == 1
    repeated = editor.render_headless(4)
    assert repeated["mask_outline_builds"] == sparse["mask_outline_builds"]
    assert repeated["vertices"] == sparse["vertices"]
    editor.select(Selection("mask", bitmap=np.full((256, 256), 255, np.uint8)))
    filled = editor.render_headless(4)
    assert filled["mask_outline_builds"] == 2
    assert filled["vertices"] < sparse["vertices"]
    editor.select()
    empty = editor.render_headless(4)
    assert empty["vertices"] < filled["vertices"]
    editor.select(Selection("mask", bitmap=bitmap))
    restored = editor.render_headless(4)
    assert restored["mask_outline_builds"] == 3
    assert restored["vertices"] == sparse["vertices"]


@pytest.mark.skipif(
    not os.environ.get("IMAGEJXX_RENDER_TESTS"),
    reason="Enable hidden OpenGL rendering checks",
)
def test_imgui_python_launch_and_render(tmp_path):
    pytest.importorskip("imagejxx._desktop", exc_type=ModuleNotFoundError)
    from imagejxx.ui import launch

    output = tmp_path / "python-imgui.png"
    editor = launch(
        [np.arange(32 * 40, dtype=np.uint16).reshape(32, 40)],
        preferences=Preferences(tmp_path / "prefs.json"),
        hidden=True,
        frames=6,
        screenshot=output,
    )
    from PIL import Image

    assert Image.open(output).size == (1250, 820)
    assert editor.document.shape == (32, 40)


@pytest.mark.skipif(
    not os.environ.get("IMAGEJXX_CPP_EXE"),
    reason="Set IMAGEJXX_CPP_EXE for C++/Python desktop interchange checks",
)
def test_cpp_python_interchange_and_all_commands(tmp_path):
    executable = Path(os.environ["IMAGEJXX_CPP_EXE"]).resolve()
    subprocess.run(
        [str(executable), "--smoke-test", "--smoke-dir", str(tmp_path)],
        check=True,
        capture_output=True,
        text=True,
    )
    documents, results, steps = load_session(tmp_path / "native.ijx")
    d = documents[0]
    d.title = "Python µm stack"
    d.select(Selection("mask", bitmap=np.eye(*d.shape, dtype=np.uint8) * 255))
    d.add_roi("Python mask")
    d.add_overlay(
        Overlay("text", [(5, 8)], text="Python → C++", font_path="custom-font.ttf")
    )
    d.spectrum = ij.fft2(d.frame)
    d.spectrum_shape = d.shape
    save_session(tmp_path / "python.ijx", [d], results=results, workflow=steps)
    subprocess.run(
        [
            str(executable),
            "--roundtrip-session",
            str(tmp_path / "python.ijx"),
            "--session-output",
            str(tmp_path / "cpp-return.ijx"),
        ],
        check=True,
        capture_output=True,
    )
    returned, returned_results, returned_steps = load_session(
        tmp_path / "cpp-return.ijx"
    )
    np.testing.assert_array_equal(returned[0].data, d.data)
    np.testing.assert_array_equal(returned[0].spectrum, d.spectrum)
    assert returned[0].title == d.title and returned[0].rois[-1].name == "Python mask"
    assert returned[0].overlays[-1].font_path == "custom-font.ttf"
    np.testing.assert_array_equal(
        returned[0].selection.mask(d.shape), d.selection.mask(d.shape)
    )
    assert returned_results == results and returned_steps == steps
    # Restore the recorded rectangle before the replay comparison.
    d.select(Selection.from_dict(steps[0]["selection"]))
    assert (
        d.is_stack
        and d.data.dtype == np.uint16
        and d.frame[5, 5] == 515
        and results[0]["Area"] == 256
    )
    np.testing.assert_array_equal(
        workflows.run(d, steps),
        d.compute(ij.add, params={"value": 10}, all_slices=True),
    )
    outputs = json.loads((tmp_path / "native-commands.json").read_text())["outputs"]
    assert {item["operation"] for item in outputs} == set(OPERATIONS)
    for item in outputs:
        shape = (16, 16) + (
            (item["input_channels"],) if item["input_channels"] > 1 else ()
        )
        source = np.asarray(item["input"], dtype=item["input_dtype"]).reshape(shape)
        actual = OPERATIONS[item["operation"]].execute(source, **item["parameters"])
        expected_shape = (item["height"], item["width"]) + (
            (item["channels"],) if item["channels"] > 1 else ()
        )
        expected = np.asarray(item["pixels"], dtype=item["dtype"]).reshape(
            expected_shape
        )
        np.testing.assert_allclose(
            actual, expected, rtol=1e-12, atol=1e-10, err_msg=item["operation"]
        )
