"""Binary defaults have native consumers and usable shared ImGui controls."""

from itertools import product
from pathlib import Path
import json
import os

import imagejxx as ij
import numpy as np
from numpy.testing import assert_array_equal
import pytest

from imagejxx.ui.model import Document as DesktopDocument
from imagejxx.ui.persistence import Preferences
from imagejxx.ui import workflows

OPERATIONS = ("erode", "dilate", "open", "close")
REFERENCE = json.loads(
    (Path(__file__).resolve().parents[1] / "fixtures/binary-imagej154r.json").read_text(
        encoding="utf-8"
    )
)


@pytest.mark.parametrize(
    "case",
    REFERENCE["cases"],
    ids=[
        f"{c['operation']}-n{c['iterations']}-count{c['neighbors']}-black{int(c['black_background'])}-pad{int(c['pad_edges'])}-{c['shape']}-pattern{c['pattern']}"
        for c in REFERENCE["cases"]
    ],
)
def test_pinned_imagej154r_byteprocessor_results(case):
    assert "reference_error" not in case
    image = np.array(case["source"], np.uint8).reshape(case["shape"])
    options = {
        key: case[key]
        for key in ("iterations", "neighbors", "black_background", "pad_edges")
    }
    result = ij.binary_morphology(image, case["operation"], **options)
    assert_array_equal(result.ravel(), case["pixels"])


def test_reference_provenance_is_explicit():
    assert len(REFERENCE["cases"]) == 1184
    assert REFERENCE["imagej_version"] == "1.54r"
    assert REFERENCE["imagej_commit"] == "62db90a269cccc1ac79f658e4602043214ed055b"
    assert (
        REFERENCE["source_archive_sha256"]
        == "09e31a88191d10c7f79c607cb6bb62342476bde733c5d932d5825fd553796665"
    )
    assert REFERENCE["source_member"].endswith("/ij/process/ByteProcessor.java")
    assert "Not Binary plugin LUT/ROI/UI/EDM certification" in REFERENCE["scope"]


def pattern(shape=(9, 11)):
    y, x = np.indices(shape)
    return np.where(((x * 3 + y * 7) % 11 < 6) | ((x == 4) & (y > 1)), 255, 0).astype(
        np.uint8
    )


def expected(
    image, operation, iterations=1, neighbors=1, black_background=True, pad_edges=False
):
    """Independent array-neighborhood definition, including the zero-center mask."""
    foreground = 255 if black_background else 0
    current = image == foreground
    stages = {
        "erode": (True,),
        "dilate": (False,),
        "open": (True, False),
        "close": (False, True),
    }
    for erosion in stages[operation]:
        for _ in range(iterations):
            padded = np.pad(current, 1, constant_values=erosion and pad_edges)
            neighborhood = np.stack(
                [
                    padded[dy : dy + current.shape[0], dx : dx + current.shape[1]]
                    for dy, dx in product(range(3), repeat=2)
                    if (dy, dx) != (1, 1)
                ]
            )
            count = (~neighborhood if erosion else neighborhood).sum(axis=0)
            current = (
                current & (count < neighbors)
                if erosion
                else current | (count >= neighbors)
            )
    return np.where(current, foreground, 255 - foreground).astype(np.uint8)


@pytest.mark.parametrize("operation", OPERATIONS)
@pytest.mark.parametrize("neighbors", range(1, 9))
@pytest.mark.parametrize("iterations", (1, 2, 3))
@pytest.mark.parametrize("black_background,pad_edges", product((False, True), repeat=2))
def test_all_options_match_owned_neighborhood_fixtures(
    operation, neighbors, iterations, black_background, pad_edges
):
    image = pattern()[::-1, ::-1]  # The array-facing API also accepts strided input.
    original = image.copy()
    options = dict(
        iterations=iterations,
        neighbors=neighbors,
        black_background=black_background,
        pad_edges=pad_edges,
    )
    wanted = expected(image, operation, **options)
    actual = getattr(ij, "binary_" + operation)(image, **options)
    assert_array_equal(actual, wanted)
    assert_array_equal(
        ij.execute_operation("binary_" + operation, image, options), wanted
    )
    assert_array_equal(image, original)
    assert actual.dtype == np.uint8 and not np.shares_memory(image, actual)


@pytest.mark.parametrize("shape", ((1, 1), (1, 7), (7, 1), (2, 2)))
@pytest.mark.parametrize("operation", OPERATIONS)
@pytest.mark.parametrize("pad_edges", (False, True))
def test_degenerate_edges_have_defined_native_behavior(shape, operation, pad_edges):
    image = pattern(shape)
    assert_array_equal(
        ij.binary_morphology(image, operation, pad_edges=pad_edges),
        expected(image, operation, pad_edges=pad_edges),
    )


@pytest.mark.parametrize(
    "key,value",
    [
        ("iterations", 0),
        ("iterations", 101),
        ("iterations", True),
        ("iterations", 1.0),
        ("iterations", 2**80),
        ("neighbors", 0),
        ("neighbors", 9),
        ("neighbors", False),
        ("black_background", 1),
        ("black_background", "false"),
        ("pad_edges", 1),
        ("pad_edges", None),
    ],
)
def test_options_validate_atomically(key, value):
    options = ij.BinaryMorphologyOptions()
    before = [
        options.iterations,
        options.neighbors,
        options.black_background,
        options.pad_edges,
    ]
    with pytest.raises(ValueError):
        setattr(options, key, value)
    assert [
        options.iterations,
        options.neighbors,
        options.black_background,
        options.pad_edges,
    ] == before
    with pytest.raises(ValueError):
        ij.BinaryMorphologyOptions(**{key: value})


@pytest.mark.parametrize(
    "image",
    [
        np.full((3, 4), 1, np.uint8),
        np.full((3, 4), 128, np.uint8),
        np.full((3, 4), 255, np.uint16),
        np.full((3, 4), 255, np.float32),
        np.full((3, 4), 255, np.float64),
        np.zeros((3, 4, 3), np.uint8),
        np.zeros((3, 4, 4), np.uint8),
        np.zeros((3, 4), bool),
    ],
)
def test_binary_input_rejection_is_consistent(image):
    before = image.copy()
    with pytest.raises(ValueError):
        ij.binary_erode(image)
    with pytest.raises(ValueError):
        ij.execute_operation("binary_erode", image)
    assert_array_equal(image, before)


def test_detached_settings_options_and_explicit_overrides():
    settings = ij.Settings(
        {
            "binary_iterations": 3,
            "binary_neighbors": 5,
            "binary_black_background": False,
            "binary_pad_edges": True,
        }
    )
    options = settings.binary_options()
    image = pattern()
    assert_array_equal(
        ij.binary_open(image, options=options),
        expected(image, "open", 3, 5, False, True),
    )
    assert_array_equal(
        ij.binary_open(image, options=options, neighbors=1, iterations=2),
        expected(image, "open", 2, 1, False, True),
    )
    assert options.neighbors == 5 and options.iterations == 3
    options.neighbors = 7
    assert settings["binary_neighbors"] == 5
    with pytest.raises(TypeError):
        ij.binary_open(image, options={})
    with pytest.raises(ValueError):
        ij.binary_morphology(image, "opening")


@pytest.mark.parametrize(
    "key,value",
    [
        ("binary_iterations", 0),
        ("binary_iterations", 101),
        ("binary_iterations", True),
        ("binary_neighbors", 0),
        ("binary_neighbors", 9),
        ("binary_neighbors", 1.5),
        ("binary_black_background", 0),
        ("binary_pad_edges", "true"),
    ],
)
def test_binary_preferences_are_atomic(key, value):
    settings = ij.Settings()
    original = settings.to_dict()
    with pytest.raises(ValueError):
        settings.update({"binary_neighbors": 5, key: value})
    assert settings.to_dict() == original


def test_legacy_and_group_reset_preserve_defaults(tmp_path):
    path = tmp_path / "preferences.json"
    path.write_text(
        json.dumps({"theme": "light", "plugin_data": [1, 2]}), encoding="utf-8"
    )
    preferences = Preferences(path)
    assert (
        preferences.values["binary_iterations"]
        == preferences.values["binary_neighbors"]
        == 1
    )
    assert preferences.values["binary_black_background"] is True
    assert preferences.values["binary_pad_edges"] is False
    preferences.save()
    settings = ij.Settings(
        {
            "theme": "light",
            "binary_iterations": 100,
            "binary_neighbors": 8,
            "binary_pad_edges": True,
        }
    )
    settings.reset("Binary processing")
    assert settings["theme"] == "light" and settings["binary_iterations"] == 1
    assert settings["binary_neighbors"] == 1 and not settings["binary_pad_edges"]
    assert json.loads(path.read_text(encoding="utf-8"))["extras"]["plugin_data"] == [
        1,
        2,
    ]


@pytest.fixture
def desktop(tmp_path):
    pytest.importorskip("imagejxx._desktop")
    from imagejxx.ui.app import Editor

    editor = Editor(preferences=Preferences(tmp_path / "preferences.json"))
    editor.add_document(DesktopDocument(pattern()))
    return editor


@pytest.mark.parametrize("operation", OPERATIONS)
def test_shared_desktop_defaults_overrides_recording_and_undo(desktop, operation):
    editor = desktop
    image = editor.document.data.copy()
    editor.update_settings(
        binary_iterations=2,
        binary_neighbors=3,
        binary_black_background=False,
        binary_pad_edges=True,
    )
    editor.recording = True
    editor.apply_operation("binary_" + operation, {"neighbors": 2}, asynchronous=False)
    parameters = {
        "iterations": 2,
        "neighbors": 2,
        "black_background": False,
        "pad_edges": True,
    }
    assert_array_equal(editor.document.data, expected(image, operation, **parameters))
    assert editor.workflow[0]["parameters"] == parameters
    assert_array_equal(
        workflows.run(DesktopDocument(image), editor.workflow), editor.document.data
    )
    editor.action("undo")
    assert_array_equal(editor.document.data, image)
    editor.action("redo")
    assert_array_equal(editor.document.data, expected(image, operation, **parameters))
    editor.update_settings(
        binary_iterations=1,
        binary_neighbors=8,
        binary_black_background=True,
        binary_pad_edges=False,
    )
    editor.action("undo")
    editor.action("run_workflow")
    editor.wait()
    assert_array_equal(editor.document.data, expected(image, operation, **parameters))


def test_binary_preferences_reopen_and_import_export(desktop, tmp_path):
    editor = desktop
    editor.update_settings(
        binary_iterations=4,
        binary_neighbors=7,
        binary_black_background=False,
        binary_pad_edges=True,
    )
    export = tmp_path / "portable.json"
    editor.action("export_preferences", path=str(export))
    editor.action("reset_preferences", group="Binary processing")
    assert editor.settings["binary_neighbors"] == 1
    editor.action("import_preferences", path=str(export))
    assert (
        editor.settings["binary_neighbors"] == 7 and editor.settings["binary_pad_edges"]
    )
    from imagejxx.ui.app import Editor

    reopened = Editor(preferences=Preferences(tmp_path / "preferences.json"))
    assert reopened.settings["binary_iterations"] == 4
    assert reopened.settings["binary_black_background"] is False


def click(point):
    return [
        {"type": "position", "point": point, "space": "screen"},
        {"type": "button", "name": "left", "down": True},
        {"type": "button", "name": "left", "down": False},
    ]


def typing(point, text):
    return [
        *click(point),
        {"type": "key", "name": "Ctrl", "down": True},
        {"type": "key", "name": "A", "down": True},
        {"type": "key", "name": "A", "down": False},
        {"type": "key", "name": "Ctrl", "down": False},
        {"type": "text", "value": text},
        {"type": "key", "name": "Enter", "down": True},
        {"type": "key", "name": "Enter", "down": False},
    ]


@pytest.mark.parametrize("operation", OPERATIONS)
def test_actual_integer_checkbox_preview_apply_controls(desktop, operation):
    editor = desktop
    editor.update_settings(binary_iterations=3, binary_neighbors=4, persist=False)
    image = editor.document.data.copy()
    editor.action("show_operation", operation="binary_" + operation)
    dialog = editor.render_headless()["operation_dialog"]
    assert dialog["parameters"] == {
        "iterations": 3,
        "neighbors": 4,
        "black_background": True,
        "pad_edges": False,
    }
    assert {key: dialog["controls"][key]["kind"] for key in dialog["parameters"]} == {
        "iterations": "int",
        "neighbors": "int",
        "black_background": "bool",
        "pad_edges": "bool",
    }
    assert set(dialog["controls"]) == set(dialog["parameters"]) | {
        "all_slices", "apply", "preview", "cancel"}
    assert dialog["controls"]["all_slices"]["value"] is False
    events = typing(dialog["controls"]["iterations"]["point"], "2")
    events += typing(dialog["controls"]["neighbors"]["point"], "3")
    events += click(dialog["controls"]["black_background"]["point"])
    events += click(dialog["controls"]["pad_edges"]["point"])
    dialog = editor.render_headless(events=events)["operation_dialog"]
    parameters = {
        "iterations": 2,
        "neighbors": 3,
        "black_background": False,
        "pad_edges": True,
    }
    assert dialog["parameters"] == parameters
    editor.render_headless(events=click(dialog["preview_point"]))
    editor.wait()
    preview = editor.render_headless()["operation_dialog"]
    assert preview["has_preview"] and preview["open"]
    assert_array_equal(editor.document.data, image)
    # An already opened draft retains its options if preferences change.
    editor.update_settings(binary_iterations=10, binary_neighbors=8, persist=False)
    editor.recording = True
    editor.render_headless(events=click(preview["apply_point"]))
    editor.wait()
    assert_array_equal(editor.document.data, expected(image, operation, **parameters))
    assert editor.workflow[0]["parameters"] == parameters


def test_actual_preferences_checkbox_is_consumed(desktop):
    editor = desktop
    editor.action("show_preferences", search="binary_black_background")
    rendered = editor.render_headless(exercise=True)
    assert rendered["preferences_controls"] == ["binary_black_background"]
    assert editor.settings["binary_black_background"] is False
    image = editor.document.data.copy()
    editor.apply_operation("binary_erode", asynchronous=False)
    assert_array_equal(
        editor.document.data, expected(image, "erode", black_background=False)
    )


@pytest.mark.parametrize("operation", OPERATIONS)
def test_roi_scientific_positions_preview_publish_undo(operation):
    image = np.broadcast_to(pattern((7, 9)), (2, 2, 3, 7, 9)).copy()
    owner = ij.Document(ij.Dataset(image, axes="TCZYX"))
    mask = np.zeros((7, 9), np.uint8)
    mask[1:6, 1:8] = 1
    mask[3:5, 3:5] = 0
    owner.select(ij.Selection.from_mask(mask))
    app = ij.Application()
    app.adopt(owner)
    request = ij.TargetRequest(
        ij.TargetScope.POSITIONS, positions=[ij.PlanePosition(1, 1, 2)]
    )
    target = app.resolve_target(request, operation="binary_" + operation)
    params = {"iterations": 2, "neighbors": 3, "pad_edges": True}
    prepared = ij.prepare_operation("binary_" + operation, target, params)
    wanted = image.copy()
    result = expected(image[1, 1, 2], operation, **params)
    wanted[1, 1, 2][mask != 0] = result[mask != 0]
    assert_array_equal(prepared.preview.dataset.numpy("TCZYX"), wanted)
    assert_array_equal(owner.snapshot().dataset.numpy("TCZYX"), image)
    app.publish(prepared)
    assert owner.undo_steps == 1
    assert_array_equal(owner.snapshot().dataset.numpy("TCZYX"), wanted)
    owner.undo()
    assert_array_equal(owner.snapshot().dataset.numpy("TCZYX"), image)


def test_job_admission_and_invalid_pixels_never_publish():
    owner = ij.Document(ij.Dataset(pattern(), axes="YX"))
    app = ij.Application()
    app.adopt(owner)
    app.job_scheduler.set_limits(ij.JobLimits(workers=1, memory_budget=1))
    with pytest.raises(ValueError):
        app.submit("binary_open", app.resolve_target())
    assert owner.undo_steps == 0 and not owner.snapshot().dirty
    bad = np.full((11, 13), 17, np.uint8)
    owner = ij.Document(ij.Dataset(bad, axes="YX"))
    app = ij.Application()
    app.adopt(owner)
    job = app.submit("binary_close", app.resolve_target())
    assert job.wait(2000) and job.info.state == ij.JobState.FAILED
    with pytest.raises(ValueError):
        app.publish(job)
    assert owner.undo_steps == 0 and not owner.snapshot().dirty
    assert_array_equal(owner.snapshot().dataset.numpy("YX"), bad)


@pytest.mark.skipif(
    not os.environ.get("IMAGEJXX_RENDER_TESTS"), reason="Requires hidden OpenGL"
)
def test_binary_controls_render_in_hidden_opengl(desktop):
    desktop.action("show_operation", operation="binary_close")
    desktop.run(hidden=True, frames=4)
    assert not desktop.error
