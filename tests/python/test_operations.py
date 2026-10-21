"""Native contracts against the legacy public kernels, rather than the dispatcher."""

from dataclasses import asdict
import json
from pathlib import Path

import imagejxx as ij
import numpy as np
import pytest
from imagejxx.ui.commands import OPERATIONS


def test_native_catalog_is_the_shared_authority():
    registry = ij.operation_registry()
    assert len(registry) == len(OPERATIONS) == 88
    assert {spec.id for spec in registry} == set(OPERATIONS)
    exported = json.loads((Path(ij.__file__).parent / "ui/commands.json").read_text())
    assert ij.operation_catalog() == exported
    assert exported == [
        {
            "id": op.name,
            "title": op.title,
            "menu": list(op.menu),
            "description": op.description,
            "parameters": [
                asdict(p) | {"choices": list(p.choices)} for p in op.parameters()
            ],
        }
        for op in OPERATIONS.values()
    ]
    assert ij.operation_spec("crop").geometry == ij.GeometryEffect.CROP
    capabilities = {spec.id: spec.cancellation_inside_kernel for spec in registry}
    assert capabilities["gaussian_blur_imagej"] is False
    assert capabilities["rank_filter_imagej"] is True
    assert all(
        spec.plane_only
        and spec.cancellation_inside_kernel == (spec.id == "rank_filter_imagej")
        for spec in registry
    )
    with pytest.raises(AttributeError):
        registry[0].id = "changed"
    required = next(
        p for p in ij.operation_spec("convolve").parameters if p.name == "kernel"
    )
    assert required.default_rule == ij.DefaultRule.REQUIRED


@pytest.mark.parametrize("name", list(OPERATIONS))
@pytest.mark.parametrize("dtype", [np.uint8, np.uint16, np.float32, np.float64])
@pytest.mark.parametrize("samples", [1, 3, 4])
def test_all_registered_operations_match_legacy_entry_points(name, dtype, samples):
    op = OPERATIONS[name]
    source = (np.arange(16 * 16).reshape(16, 16) % 251 + 1).astype(dtype)
    if np.issubdtype(dtype, np.floating):
        source /= 255
    if samples != 1:
        source = np.repeat(source[:, :, None], samples, axis=2)
    if name in {
        "distance_transform",
        "binary_watershed",
        "label",
        "binary_erode",
        "binary_dilate",
        "binary_open",
        "binary_close",
    }:
        source = np.zeros_like(source)
        source[3:7, 2:6] = source[10:13, 10:13] = 255
    if name == "hsv_to_rgb" and samples == 3:
        source = ij.rgb_to_hsv(source)
    parameters = ij.operation_defaults(name, source, ui=True)
    if name == "calculate":
        parameters["other"] = source.copy()
    if name == "paste":
        parameters["source"] = source[:2, :3]
    if name == "fft_bandpass":
        parameters["high_cutoff"] = 0.5
    if name == "in_range":
        parameters["lower"] = [0] * (source.shape[2] if source.ndim == 3 else 1)
        parameters["upper"] = [255] * (source.shape[2] if source.ndim == 3 else 1)
    legacy_parameters = parameters.copy()
    if legacy_parameters.get("dtype") == "preserve":
        legacy_parameters["dtype"] = None
    for key in ["other", "source"]:
        if key in legacy_parameters:
            legacy_parameters[key] = ij.convert(
                np.asarray(legacy_parameters[key]).astype(np.float64), source.dtype
            )
    original = source.copy()
    try:
        expected = op.function(source, **legacy_parameters)
    except ValueError:
        # Layout/type combinations rejected by a public kernel must also be
        # rejected by the registered path (e.g. RGB labels and float LUTs).
        with pytest.raises(ValueError):
            ij.execute_operation(name, source, parameters)
        np.testing.assert_array_equal(source, original)
        return
    actual = ij.execute_operation(name, source, parameters)
    assert actual.shape == expected.shape and actual.dtype == expected.dtype
    np.testing.assert_allclose(actual, expected, rtol=2e-6, atol=1e-8)
    np.testing.assert_array_equal(source, original)


@pytest.mark.parametrize("name", ["fill_color", "paint_stroke"])
@pytest.mark.parametrize("dtype", [np.uint8, np.uint16, np.float32, np.float64])
@pytest.mark.parametrize("samples", [1, 3, 4])
@pytest.mark.parametrize("color", ["#ff0000", "#11ab67", "#dd8f43"])
def test_colored_drawing_preserves_legacy_quantization(name, dtype, samples, color):
    shape = (9, 8) if samples == 1 else (9, 8, samples)
    source = np.full(shape, 0.125 if np.issubdtype(dtype, np.floating) else 17, dtype)
    parameters = {"color": color}
    if name == "paint_stroke":
        parameters.update(points=[[1, 2], [6, 7]], width=2.5)
    expected = OPERATIONS[name].function(source, **parameters)
    actual = OPERATIONS[name].execute(source, **parameters)
    np.testing.assert_array_equal(actual, expected)
    assert actual.dtype == source.dtype


@pytest.mark.parametrize("dtype", [np.uint8, np.uint16, np.float32, np.float64])
def test_native_rgba_alpha_and_legacy_python_workflow_color(dtype):
    source = np.zeros((4, 5, 4), dtype)
    parameters = {"color": "#11ab6722"}
    legacy = OPERATIONS["fill_color"].function(source, **parameters)
    np.testing.assert_array_equal(
        OPERATIONS["fill_color"].execute(source, **parameters), legacy
    )
    native = ij.execute_operation("fill_color", source, parameters)
    expected = legacy.copy()
    maximum = np.iinfo(dtype).max if np.issubdtype(dtype, np.integer) else 1
    expected[..., 3] = ij.fill(source[..., 3], 0x22 * maximum / 255)
    np.testing.assert_array_equal(native, expected)


@pytest.mark.parametrize("dtype", [np.uint8, np.uint16, np.float32, np.float64])
def test_strided_arrays_and_output_lifetime(dtype):
    source = np.arange(60).reshape(4, 5, 3).astype(dtype)[:, ::-1]
    result = ij.run_operation("add", source, {"value": 2})
    expected = ij.add(source, 2)
    np.testing.assert_array_equal(result.pixels, expected)
    detached = result.pixels
    detached[:] = 0
    np.testing.assert_array_equal(result.pixels, expected)
    assert result.label_count is None


@pytest.mark.parametrize(
    "operation,parameters",
    [
        ("add", {"typo": 1}),
        ("smooth", {"radius": 1.5}),
        ("flip", {"horizontal": "true"}),
        ("gaussian_blur", {"sigma": [1, 2, 3]}),
        ("convolve", {"kernel": [[1, 2], [3]]}),
        ("binning", {"factor": 1.5}),
        ("add_noise", {"seed": -1}),
        ("add_noise", {"seed": 2**65}),
        ("add_noise", {"seed": 1.5}),
        ("add", {"value": np.nan}),
        ("rank_filter", {"method": "unknown"}),
        ("fill_color", {"color": "bad"}),
        ("paint_stroke", {"points": [[1, 2, 3]]}),
        ("paint_stroke", {"points": [[], []]}),
    ],
)
def test_invalid_native_parameters_are_atomic(operation, parameters):
    source = np.arange(20, dtype=np.uint16).reshape(4, 5)
    original = source.copy()
    with pytest.raises((ValueError, TypeError, OverflowError)):
        ij.execute_operation(operation, source, parameters)
    np.testing.assert_array_equal(source, original)


def test_dynamic_defaults_and_full_width_seed():
    source = np.full((4, 5, 3), 50, np.uint16)
    assert ij.operation_defaults("in_range", source)["upper"] == [65535] * 3
    assert (
        ij.operation_defaults("in_range", source, legacy_python=True)["upper"]
        == [50] * 3
    )
    assert ij.operation_defaults("resize", source)["width"] == 5
    assert ij.operation_defaults("add", source, ui=False)["value"] == 0
    assert ij.operation_defaults("add", source, ui=True)["value"] == 10
    lut = ij.operation_defaults("apply_lut", source)["lut"]
    assert len(lut) == 65536 and lut[0] == 0 and lut[-1] == 65535
    first = ij.execute_operation("add_noise", source, {"amount": 10, "seed": 2**64 - 1})
    second = ij.execute_operation(
        "add_noise", source, {"amount": 10, "seed": np.uint64(2**64 - 1)}
    )
    np.testing.assert_array_equal(first, second)


def test_workflow_validation_uses_native_arguments_without_a_source(monkeypatch):
    from imagejxx.ui import workflows

    # A function wrapper's signature no longer changes the recorded contract.
    monkeypatch.setattr(ij, "add", lambda image: image)
    steps = [{"operation": "add", "parameters": {"value": 2}}]
    assert workflows.validate_steps(steps) is steps
    for invalid in [
        {"operation": "add", "parameters": {"typo": 2}},
        {"operation": "flip", "parameters": {"horizontal": "false"}},
        {"operation": "convolve", "parameters": {}},
        {"operation": "convolve", "parameters": {"kernel": [[1, 2], [3]]}},
        {"operation": "label", "parameters": {"connectivity": 7}},
        {"operation": "add", "parameters": []},
    ]:
        with pytest.raises(ValueError):
            workflows.validate_steps([invalid])
    ij.validate_operation_record("resize")  # Source dimensions resolve at replay.
    ij.validate_operation_record("add_noise", {"seed": 2**64 - 1})
    ij.validate_operation_record("resize_canvas", {"offset": [-(2**31), 0]})


def test_metadata_defaults_do_not_materialize_a_large_strided_plane():
    pixels = np.lib.stride_tricks.as_strided(
        np.zeros(1, np.uint8), shape=(50000, 40000), strides=(0, 0), writeable=False
    )
    defaults = ij.operation_defaults("resize", pixels)
    assert defaults["width"] == 40000 and defaults["height"] == 50000


def test_label_count_is_a_native_output_and_reaches_desktop_results(tmp_path):
    source = np.zeros((4, 5), np.uint8)
    source[1, 1] = source[2, 3] = 255
    result = ij.run_operation("label", source, {"return_num": True, "connectivity": 4})
    assert result.label_count == 2
    pixels, count = ij.execute_operation(
        "label", source, {"return_num": True, "connectivity": 4}
    )
    np.testing.assert_array_equal(pixels, result.pixels)
    assert count == 2
    pytest.importorskip("imagejxx._desktop", exc_type=ModuleNotFoundError)
    from imagejxx.ui.app import Editor
    from imagejxx.ui.model import Document
    from imagejxx.ui.persistence import Preferences

    editor = Editor(preferences=Preferences(tmp_path / "prefs.json"))
    editor.add_document(Document(np.stack([source, source]), "Labels", is_stack=True))
    editor.apply_operation(
        "label", {"return_num": True, "connectivity": 4}, True, asynchronous=False
    )
    assert [row["Count"] for row in editor.results] == [2, 2]
    assert [row["Slice"] for row in editor.results] == [1, 2]
    editor.apply_operation("label", {"return_num": True}, asynchronous=True)
    editor.wait()
    assert len(editor.results) == 3 and editor.results[-1]["Count"] == 2
    editor.action("gaussian3d", sigma_x=0, sigma_y=0, sigma_z=0)
    editor.wait()
    assert len(editor.results) == 3  # Other jobs cannot replay old output counts.


def test_component_results_observe_roi_and_commit_lifecycle(tmp_path):
    pytest.importorskip("imagejxx._desktop", exc_type=ModuleNotFoundError)
    from imagejxx.ui.app import Editor
    from imagejxx.ui.model import Document, Selection
    from imagejxx.ui.persistence import Preferences

    source = np.zeros((16, 16), np.uint8)
    source[3:6, 2:5] = source[10:13, 10:13] = 255
    editor = Editor(preferences=Preferences(tmp_path / "prefs.json"))
    editor.add_document(Document(source, "Components"))
    editor.select(Selection("rectangle", [(1, 2), (6, 7)]))
    mask = editor.document.selection.mask(source.shape) != 0
    editor.apply_operation(
        "label", {"return_num": True, "connectivity": 4}, asynchronous=False
    )
    assert editor.results[-1]["Count"] == 1
    np.testing.assert_array_equal(editor.document.frame[~mask], source[~mask])
    editor.action("undo")
    np.testing.assert_array_equal(editor.document.frame, source)
    rows = editor.results.copy()
    editor.apply_operation("label", {"return_num": True}, asynchronous=True)
    editor.cancel()
    editor.wait()
    assert editor.results == rows
    np.testing.assert_array_equal(editor.document.frame, source)
    editor.apply_operation(
        "label", {"return_num": True, "connectivity": 7}, asynchronous=True
    )
    with pytest.raises(RuntimeError):
        editor.wait()
    assert editor.results == rows
    np.testing.assert_array_equal(editor.document.frame, source)


def test_desktop_16bit_lut_defaults_cover_the_source_range(tmp_path):
    pytest.importorskip("imagejxx._desktop", exc_type=ModuleNotFoundError)
    from imagejxx.ui.app import Editor
    from imagejxx.ui.model import Document
    from imagejxx.ui.persistence import Preferences

    source = np.array([[0, 257, 32768, 65535]], np.uint16)
    editor = Editor(preferences=Preferences(tmp_path / "prefs.json"))
    editor.add_document(Document(source))
    editor.apply_operation("apply_lut", asynchronous=False)
    np.testing.assert_array_equal(editor.document.frame, source)


@pytest.mark.parametrize("operation", list(OPERATIONS))
def test_shared_imgui_operation_forms_render_typed_choices(operation, tmp_path):
    pytest.importorskip("imagejxx._desktop", exc_type=ModuleNotFoundError)
    from imagejxx.ui.app import Editor
    from imagejxx.ui.model import Document
    from imagejxx.ui.persistence import Preferences

    editor = Editor(preferences=Preferences(tmp_path / "prefs.json"))
    editor.add_document(Document(np.zeros((16, 16), np.uint16)))
    before = editor.live_document().info
    editor.action("show_operation", operation=operation)
    info = editor.render_headless()
    if operation in ("gaussian_blur_imagej", "rank_filter_imagej"):
        assert not info["operation_dialog"]["open"]
        if operation == "gaussian_blur_imagej":
            panel = info["gaussian_panel"]
            assert not info["rank_panel"]["open"]
            assert panel["sigma"] == 2 and panel["sigma_text"] == "2"
            assert not panel["scaled_units"]
            controls = ("sigma", "preview", "ok", "cancel")
        else:
            panel = info["rank_panel"]
            assert not info["gaussian_panel"]["open"]
            assert panel["method"] == "median"
            assert panel["radius"] == 2 and panel["radius_text"] == "2"
            assert not panel["new_document"] and panel["radius_units"] == "pixels"
            controls = ("method", "radius", "new_document", "preview", "ok", "cancel")
        assert panel["open"] and panel["visible"] and panel["draft_valid"]
        assert not panel["error"] and not panel["preview_error"]
        assert not panel["preview"] and not panel["preview_requested"]
        assert not panel["stack_prompt"] and not panel["apply_busy"]
        for name in controls:
            control = panel["control_states"][name]
            assert control["enabled"] and control["id"] != 0
            point, rect = panel["controls"][name], panel["control_rects"][name]
            assert rect[0] <= point[0] <= rect[2] and rect[1] <= point[1] <= rect[3]
        np.testing.assert_array_equal(editor.document.frame, np.zeros((16, 16), np.uint16))
    else:
        dialog = info["operation_dialog"]
        assert dialog["id"] == operation and dialog["open"]
    assert info["vertices"] > 0 and not editor.error
    after = editor.live_document().info
    assert after["dirty"] == before["dirty"] and editor.results == []
    assert after["undo_steps"] == before["undo_steps"]
    for revision in ("pixels", "metadata", "selection"):
        assert after["revisions"][revision] == before["revisions"][revision]


def test_actual_numeric_choice_preview_and_apply_interaction(tmp_path):
    pytest.importorskip("imagejxx._desktop", exc_type=ModuleNotFoundError)
    from imagejxx.ui.app import Editor
    from imagejxx.ui.model import Document
    from imagejxx.ui.persistence import Preferences

    source = np.zeros((8, 8), np.uint8)
    source[2, 2] = source[3, 3] = 255
    editor = Editor(preferences=Preferences(tmp_path / "prefs.json"))
    editor.add_document(Document(source))
    editor.action("save_document", path=str(tmp_path / "source.ijx"))
    editor.action("show_operation", operation="label", parameters={"return_num": True})
    before = editor.live_document().info
    info = editor.render_headless(exercise=True)
    dialog = info["operation_dialog"]
    assert dialog["open"] and dialog["has_preview"]
    assert dialog["parameters"]["connectivity"] == 4
    assert editor.live_document().info["revisions"] == before["revisions"]
    np.testing.assert_array_equal(editor.document.frame, source)
    assert not editor.live_document().info["dirty"] and editor.results == []
    info = editor.render_headless(exercise=True)
    assert not info["operation_dialog"]["open"]
    np.testing.assert_array_equal(
        editor.document.frame, ij.label(source, connectivity=4)
    )
    assert editor.live_document().info["dirty"]
    assert editor.results[-1]["Count"] == 2
    editor.action("undo")
    np.testing.assert_array_equal(editor.document.frame, source)


def test_imgui_preserves_uint64_seed_through_preview_and_apply(tmp_path):
    pytest.importorskip("imagejxx._desktop", exc_type=ModuleNotFoundError)
    from imagejxx.ui.app import Editor
    from imagejxx.ui.model import Document
    from imagejxx.ui.persistence import Preferences

    source = np.full((8, 8), 100, np.uint16)
    editor = Editor(preferences=Preferences(tmp_path / "prefs.json"))
    editor.add_document(Document(source))
    editor.action(
        "show_operation", operation="add_noise", parameters={"seed": 2**64 - 1}
    )
    dialog = editor.render_headless(exercise=True)["operation_dialog"]
    assert dialog["has_preview"] and dialog["parameters"]["seed"] == 2**64 - 1
    np.testing.assert_array_equal(editor.document.frame, source)
    editor.render_headless(exercise=True)
    np.testing.assert_array_equal(
        editor.document.frame, ij.add_noise(source, **dialog["parameters"])
    )
