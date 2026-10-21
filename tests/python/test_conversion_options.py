"""Pinned conversion arithmetic, consumed defaults and real shared UI controls."""

import json
import ast
import hashlib
from pathlib import Path
import os

import imagejxx as ij
import numpy as np
from numpy.testing import assert_array_equal
import pytest

from imagejxx.ui.model import Document as DesktopDocument, Selection as DesktopSelection
from imagejxx.ui.persistence import Preferences
from imagejxx.ui import workflows

REFERENCE = json.loads(
    (
        Path(__file__).resolve().parents[1] / "fixtures/conversion-imagej154r.json"
    ).read_text(encoding="utf-8")
)


@pytest.mark.parametrize(
    "case",
    REFERENCE["cases"],
    ids=[
        f"case{i}-{c['operation']}-{c['source_dtype']}"
        for i, c in enumerate(REFERENCE["cases"])
    ],
)
def test_pinned_imagej154r_conversion_and_inversion(case):
    source = np.array(case["source"], dtype=case["source_dtype"]).reshape(case["shape"])
    parameters = case["parameters"]
    if case["operation"] == "convert_image":
        actual = ij.convert_image(source, case["dtype"], **parameters)
        registered = ij.execute_operation(
            "convert_image", source, {"dtype": case["dtype"], **parameters}
        )
        assert actual.dtype == case["dtype"]
    else:
        options = ij.ConversionOptions(
            input_range=case["source_display_range"], **parameters
        )
        actual = ij.invert_uint16(source, options=options)
        registered = ij.execute_operation("invert_uint16", source, parameters)
    wanted = np.array(case["pixels"], dtype=actual.dtype).reshape(2, 8)
    assert_array_equal(actual, wanted)
    assert_array_equal(registered, wanted)
    assert_array_equal(source.ravel(), np.array(case["source"], dtype=source.dtype))
    assert not np.shares_memory(source, actual)


def test_reference_provenance_and_scope():
    assert REFERENCE["imagej_version"] == "1.54r" and len(REFERENCE["cases"]) == 462
    assert REFERENCE["imagej_commit"] == "62db90a269cccc1ac79f658e4602043214ed055b"
    assert (
        REFERENCE["source_archive_sha256"]
        == "09e31a88191d10c7f79c607cb6bb62342476bde733c5d932d5825fd553796665"
    )
    assert REFERENCE["source_members_sha256"] == {
        "ij/process/TypeConverter.java": "1a0ca5659bce6c80e9530221001fe18780b4fad068cc1b6418436b6fae0fdaf8",
        "ij/process/FloatProcessor.java": "0f52a157fe99e9a48ce2c9356fc2c3c435dc74f48e45a575171e60deb694ccf5",
        "ij/process/ShortProcessor.java": "7484082e91c7095a3c79984c74b4dafa8b6d2912ed3b862e28dd331c8ba3024d",
        "ij/process/ColorProcessor.java": "23da704d928476adf42a2d562360eaa7c8b740ad02fba2728074be0bac490650",
    }
    driver_source = (
        Path(__file__).resolve().parents[2] / REFERENCE["generator"]
    ).read_text(encoding="utf-8")
    driver = next(
        ast.literal_eval(node.value)
        for node in ast.parse(driver_source).body
        if isinstance(node, ast.Assign)
        and any(isinstance(t, ast.Name) and t.id == "DRIVER" for t in node.targets)
    )
    assert (
        hashlib.sha256(driver.encode("utf-8")).hexdigest() == REFERENCE["driver_sha256"]
    )
    assert "not density-calibrated/signed/LUT/threshold/stack/GUI" in REFERENCE["scope"]


@pytest.mark.parametrize(
    "key,value",
    [
        ("scale", 1),
        ("scale", "false"),
        ("weighted_rgb", 1),
        ("full_range_inversions", None),
        ("unsigned16_bits", 9),
        ("unsigned16_bits", True),
        ("unsigned16_bits", 12.0),
        ("unsigned16_bits", 2**80),
        ("rgb_weights", (1, 2)),
        ("rgb_weights", (1, False, 0)),
        ("rgb_weights", (np.inf, 0, 0)),
        ("input_range", (1, 0)),
        ("input_range", (0, np.nan)),
        ("input_range", (True, 1)),
        ("range_policy", "unknown"),
        ("range_policy", "explicit"),
    ],
)
def test_options_setters_and_construction_are_atomic(key, value):
    options = ij.ConversionOptions()

    def fields():
        return [
            options.scale,
            options.weighted_rgb,
            list(options.rgb_weights),
            options.range_policy,
            options.input_range,
            options.full_range_inversions,
            options.unsigned16_bits,
        ]

    before = fields()
    with pytest.raises((ValueError, TypeError)):
        setattr(options, key, value)
    assert fields() == before
    with pytest.raises((ValueError, TypeError)):
        ij.ConversionOptions(**{key: value})


def test_options_are_detached_and_keywords_override_without_mutation():
    settings = ij.Settings(
        {
            "conversion_scale": False,
            "conversion_weighted_rgb": True,
            "conversion_red_weight": 1,
            "conversion_green_weight": 0,
            "conversion_blue_weight": 0,
            "conversion_unsigned16_range": "12",
            "conversion_full_range_inversions": True,
        }
    )
    options = settings.conversion_options()
    source = np.array([[[1, 30, 90], [4, 50, 10]]], np.uint8)
    assert_array_equal(ij.convert_image(source, options=options), [[1, 4]])
    assert_array_equal(
        ij.convert_image(source, options=options, green_weight=1), [[31, 54]]
    )
    assert list(options.rgb_weights) == [1, 0, 0]
    values = options.rgb_weights
    values[0] = 5
    assert list(options.rgb_weights) == [1, 0, 0]
    options.scale = True
    assert settings["conversion_scale"] is False
    gray = np.array([[0, 1000, 4095, 65535]], np.uint16)
    assert_array_equal(ij.convert_image(gray, options=options), [[0, 63, 255, 255]])
    assert_array_equal(ij.invert_uint16(gray, options=options), [[4095, 3095, 0, 0]])
    with pytest.raises(TypeError):
        ij.convert_image(source, options={})
    with pytest.raises(ValueError):
        ij.convert_image(gray, range_min=10)


@pytest.mark.parametrize("bits", (0, 8, 10, 12, 14, 15, 16))
def test_unsigned_range_and_display_precedence(bits):
    source = np.array([[0, 100, 1000, 65535]], np.uint16)
    options = ij.ConversionOptions(range_policy="display", unsigned16_bits=bits)
    expected = (
        ij.convert_image(source, range_min=0, range_max=(1 << bits) - 1)
        if bits
        else ij.convert_image(source)
    )
    assert_array_equal(ij.convert_image(source, options=options), expected)
    options.input_range = (100, 1000)
    assert_array_equal(
        ij.convert_image(source, options=options),
        ij.convert_image(source, range_min=100, range_max=1000),
    )


@pytest.mark.parametrize("dtype", (np.uint8, np.uint16, np.float32, np.float64))
def test_native_float64_and_rgba_extensions(dtype):
    rgb = np.array([[[0, 100, 250, 0], [255, 50, 10, 255], [1, 2, 3, 11]]], np.uint8)
    assert_array_equal(
        ij.convert_image(rgb, dtype), ij.convert_image(rgb[..., :3], dtype)
    )
    source = np.array([[0, 1, 10, 1000]], np.float64)[:, ::-1]
    actual = ij.convert_image(source, dtype)
    assert actual.dtype == dtype and actual.shape == source.shape
    assert not np.shares_memory(source, actual)
    if np.issubdtype(dtype, np.floating):
        assert_array_equal(actual, source)


def test_wide_finite_float64_range_is_bounded():
    image = np.array([[-1e308, 0, 1e308]], np.float64)
    assert_array_equal(ij.convert_image(image), [[0, 128, 255]])
    assert_array_equal(ij.convert_image(image, np.uint16), [[0, 32768, 65535]])
    with pytest.raises(ValueError):
        ij.convert_image(image, np.float32)


@pytest.mark.parametrize("dtype", (np.uint8, np.uint16))
@pytest.mark.parametrize("explicit", (False, True))
@pytest.mark.parametrize("limits", ((0.0, 1e-310), (-1e-308, 1e-308)))
def test_tiny_finite_float64_ranges_preserve_endpoints_and_midpoint(
    dtype, explicit, limits
):
    lo, hi = limits
    image = np.array([[lo, (lo + hi) / 2, hi]], np.float64)
    parameters = {"range_min": lo, "range_max": hi} if explicit else {}
    expected = np.array(
        [[0, 128, 255]] if dtype == np.uint8 else [[0, 32768, 65535]], dtype=dtype
    )
    actual = ij.convert_image(image, dtype, **parameters)
    assert_array_equal(actual, expected)
    assert_array_equal(
        ij.execute_operation(
            "convert_image",
            image,
            {
                "dtype": np.dtype(dtype).name,
                **parameters,
                **({"range_policy": "explicit"} if explicit else {}),
            },
        ),
        expected,
    )
    assert np.isfinite(actual).all()
    assert_array_equal(image, [[lo, (lo + hi) / 2, hi]])


@pytest.mark.parametrize(
    "source",
    (
        np.zeros((3, 4, 3), np.uint16),
        np.zeros((3, 4, 4), np.float32),
        np.zeros((3, 4), bool),
    ),
)
def test_unsupported_source_types_reject_without_mutation(source):
    before = source.copy()
    with pytest.raises(ValueError):
        ij.convert_image(source)
    with pytest.raises(ValueError):
        ij.execute_operation("convert_image", source)
    assert_array_equal(source, before)


@pytest.mark.parametrize(
    "key,value",
    [
        ("conversion_scale", 1),
        ("conversion_weighted_rgb", "true"),
        ("conversion_red_weight", np.nan),
        ("conversion_green_weight", True),
        ("conversion_range_policy", "explicit"),
        ("conversion_unsigned16_range", "9"),
        ("conversion_full_range_inversions", 0),
    ],
)
def test_preferences_validate_before_partial_application(key, value):
    settings = ij.Settings()
    before = settings.to_dict()
    with pytest.raises(ValueError):
        settings.update({"conversion_scale": False, key: value})
    assert settings.to_dict() == before


def test_old_preferences_defaults_reset_reopen_and_preserve_extras(tmp_path):
    path = tmp_path / "preferences.json"
    path.write_text(
        json.dumps({"theme": "light", "plugin_data": {"keep": True}}), encoding="utf-8"
    )
    preferences = Preferences(path)
    assert preferences.values["conversion_scale"] is True
    assert preferences.values["conversion_weighted_rgb"] is False
    preferences.save()
    settings = ij.Settings(
        {
            "theme": "light",
            "conversion_scale": False,
            "conversion_red_weight": 2,
            "conversion_unsigned16_range": "12",
        }
    )
    settings.reset("Conversion")
    assert settings["theme"] == "light" and settings["conversion_scale"] is True
    assert settings["conversion_unsigned16_range"] == "auto"
    assert json.loads(path.read_text(encoding="utf-8"))["extras"]["plugin_data"] == {
        "keep": True
    }


@pytest.fixture
def desktop(tmp_path):
    pytest.importorskip("imagejxx._desktop")
    from imagejxx.ui.app import Editor

    return Editor(preferences=Preferences(tmp_path / "preferences.json"))


def click(point):
    return [
        {"type": "position", "point": point, "space": "screen"},
        {"type": "button", "name": "left", "down": True},
        {"type": "button", "name": "left", "down": False},
    ]


def typing(point, value):
    return [
        *click(point),
        {"type": "key", "name": "Ctrl", "down": True},
        {"type": "key", "name": "A", "down": True},
        {"type": "key", "name": "A", "down": False},
        {"type": "key", "name": "Ctrl", "down": False},
        {"type": "text", "value": value},
        {"type": "key", "name": "Enter", "down": True},
        {"type": "key", "name": "Enter", "down": False},
    ]


def test_consumed_display_range_full_stack_roi_preservation_recording_and_replay(
    desktop,
):
    pixels = (np.arange(2 * 6 * 8).reshape(2, 6, 8) * 37 + 100).astype(np.uint16)
    document = DesktopDocument(pixels, is_stack=True)
    document.select(DesktopSelection("rectangle", [(1, 1), (3, 4)]))
    desktop.add_document(document)
    desktop.action("display_range", minimum=100, maximum=2000)
    desktop.recording = True
    desktop.apply_operation("convert_image", asynchronous=False)
    wanted = np.stack(
        [ij.convert_image(plane, range_min=100, range_max=2000) for plane in pixels]
    )
    assert_array_equal(desktop.document.data, wanted)
    step = desktop.workflow[0]
    assert step["all_slices"] is True
    assert step["parameters"]["range_policy"] == "explicit"
    assert (
        step["parameters"]["range_min"] == 100
        and step["parameters"]["range_max"] == 2000
    )
    assert desktop.document.selection.to_dict() == document.selection.to_dict()
    assert_array_equal(workflows.run(document, desktop.workflow), wanted)
    desktop.action("undo")
    assert_array_equal(desktop.document.data, pixels)
    desktop.action("display_range", minimum=500, maximum=1000)
    desktop.action("run_workflow")
    desktop.wait()
    assert_array_equal(desktop.document.data, wanted)


@pytest.mark.parametrize("range_policy", ("data", "dtype", "display"))
def test_preference_range_policies_and_inversion_consumers(desktop, range_policy):
    image = np.array([[100, 200, 500, 1000]], np.uint16)
    desktop.add_document(DesktopDocument(image))
    desktop.update_settings(
        conversion_range_policy=range_policy,
        conversion_unsigned16_range="12",
        conversion_full_range_inversions=True,
    )
    desktop.apply_operation("convert_image", asynchronous=False)
    wanted = (
        ij.convert_image(image, range_min=0, range_max=4095)
        if range_policy == "display"
        else ij.convert_image(image, range_policy=range_policy)
    )
    assert_array_equal(desktop.document.data, wanted)
    desktop.action("undo")
    desktop.apply_operation("invert_uint16", asynchronous=False)
    assert_array_equal(desktop.document.data, 4095 - image)


def test_actual_rgb_fields_preview_apply_and_frozen_draft(desktop):
    rgb = np.zeros((8, 9, 4), np.uint8)
    rgb[..., :3] = [120, 40, 20]
    rgb[..., 3] = np.arange(9)
    desktop.add_document(DesktopDocument(rgb))
    desktop.action(
        "show_operation", operation="convert_image", parameters={"dtype": "float32"}
    )
    dialog = desktop.render_headless()["operation_dialog"]
    events = click(dialog["controls"]["weighted_rgb"]["point"])
    events += typing(dialog["controls"]["red_weight"]["point"], "0.5")
    events += typing(dialog["controls"]["green_weight"]["point"], "0.25")
    events += typing(dialog["controls"]["blue_weight"]["point"], "0.25")
    draft = desktop.render_headless(events=events)["operation_dialog"]
    params = draft["parameters"]
    assert params["weighted_rgb"] and params["red_weight"] == 0.5
    assert params["green_weight"] == params["blue_weight"] == 0.25
    desktop.render_headless(events=click(draft["preview_point"]))
    desktop.wait()
    assert_array_equal(desktop.document.data, rgb)
    ready = desktop.render_headless()["operation_dialog"]
    assert ready["has_preview"]
    desktop.update_settings(
        conversion_red_weight=1,
        conversion_green_weight=0,
        conversion_blue_weight=0,
        persist=False,
    )
    desktop.recording = True
    desktop.render_headless(events=click(ready["apply_point"]))
    desktop.wait()
    assert_array_equal(desktop.document.data, np.full((8, 9), 75, np.float32))
    assert desktop.workflow[0]["parameters"] == params


def test_actual_range_numeric_controls_retain_opened_display_context(desktop):
    image = np.arange(20, dtype=np.uint16).reshape(4, 5) * 10
    desktop.add_document(DesktopDocument(image))
    desktop.action("display_range", minimum=0, maximum=190)
    desktop.action("show_operation", operation="convert_image")
    dialog = desktop.render_headless()["operation_dialog"]
    assert dialog["parameters"]["range_policy"] == "explicit"
    events = typing(dialog["controls"]["range_min"]["point"], "20")
    events += typing(dialog["controls"]["range_max"]["point"], "120")
    draft = desktop.render_headless(events=events)["operation_dialog"]
    assert (
        draft["parameters"]["range_min"] == 20
        and draft["parameters"]["range_max"] == 120
    )
    desktop.action("display_range", minimum=0, maximum=50)
    desktop.render_headless(events=click(draft["apply_point"]))
    desktop.wait()
    assert_array_equal(
        desktop.document.data, ij.convert_image(image, range_min=20, range_max=120)
    )


def test_actual_conversion_preference_checkbox_and_portable_file(desktop, tmp_path):
    desktop.action("show_preferences", search="conversion_scale")
    frame = desktop.render_headless(exercise=True)
    assert frame["preferences_controls"] == ["conversion_scale"]
    assert desktop.settings["conversion_scale"] is False
    desktop.add_document(DesktopDocument(np.array([[10, 100, 1000]], np.uint16)))
    desktop.apply_operation("convert_image", asynchronous=False)
    assert_array_equal(desktop.document.data, [[10, 100, 255]])
    export = tmp_path / "export.json"
    desktop.action("export_preferences", path=str(export))
    desktop.action("reset_preferences", group="Conversion")
    desktop.action("import_preferences", path=str(export))
    assert desktop.settings["conversion_scale"] is False
    from imagejxx.ui.app import Editor

    reopened = Editor(preferences=Preferences(tmp_path / "preferences.json"))
    assert reopened.settings["conversion_scale"] is False


def test_scientific_conversion_requires_full_targets_and_preserves_records():
    source = np.arange(2 * 2 * 3 * 4 * 5, dtype=np.uint16).reshape(2, 2, 3, 4, 5)
    owner = ij.Document(ij.Dataset(source, axes="TCZYX"))
    owner.select(
        ij.Selection(
            "rectangle",
            [(1, 1), (3, 3)],
            width=2,
            attributes={"name": "preserved", "color": "#123456"},
        )
    )
    app = ij.Application()
    app.adopt(owner)
    with pytest.raises(ValueError, match="every dataset position"):
        ij.prepare_operation("convert_image", app.resolve_target())
    legacy = ij.resolve_legacy_target(owner.snapshot(), False, 0, "convert_image")
    assert legacy.position_count == 12 and legacy.roi_policy == ij.RoiPolicy.WHOLE_PLANE
    params = {
        "dtype": "uint8",
        "range_policy": "explicit",
        "range_min": 0,
        "range_max": 255,
    }
    prepared = ij.prepare_operation("convert_image", legacy, params)
    assert_array_equal(owner.snapshot().dataset.numpy("TCZYX"), source)
    app.publish(prepared)
    assert_array_equal(owner.snapshot().dataset.numpy("TCZYX"), source.astype(np.uint8))
    assert owner.undo_steps == 1
    actual, expected = owner.snapshot().selection, legacy.source.selection
    assert (actual.kind, actual.points, actual.width, actual.attributes) == (
        expected.kind,
        expected.points,
        expected.width,
        expected.attributes,
    )
    assert_array_equal(actual.mask((4, 5)), expected.mask((4, 5)))
    owner.undo()
    assert_array_equal(owner.snapshot().dataset.numpy("TCZYX"), source)


def test_registry_validation_and_legacy_operations_keep_defaults():
    for operation, params in [
        ("convert_image", {"range_min": 10, "range_max": 0}),
        ("invert_uint16", {"unsigned16_bits": 9}),
    ]:
        with pytest.raises(ValueError):
            ij.validate_operation_record(operation, params)
    image = np.array([[10, 20, 30, 40]], np.uint16)
    assert_array_equal(ij.convert(image, np.uint8), image)
    assert_array_equal(ij.execute_operation("convert", image), image)
    rgb = np.array([[[255, 0, 0]]], np.uint8)
    assert_array_equal(ij.grayscale(rgb), [[76]])
    assert_array_equal(ij.convert_image(rgb), [[85]])


@pytest.mark.skipif(
    not os.environ.get("IMAGEJXX_RENDER_TESTS"), reason="Requires hidden OpenGL"
)
def test_conversion_forms_render_in_hidden_opengl(desktop):
    desktop.add_document(DesktopDocument(np.zeros((10, 11), np.uint16)))
    desktop.action("show_operation", operation="convert_image")
    desktop.run(hidden=True, frames=4)
    assert not desktop.error
