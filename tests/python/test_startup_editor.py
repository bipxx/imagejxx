"""Startup wrapper boundaries and native constructor integration.

Call recorders inspect forwarding only. Document/configuration behavior below
uses the optional native desktop without launching a render loop. These checks
do not provide actual UI or operating-system relaunch evidence.
"""

import json
from pathlib import Path
from types import SimpleNamespace

import imagejxx as ij
import numpy as np
from numpy.testing import assert_array_equal
import pytest

import imagejxx.ui as ui
from imagejxx.ui import app
from imagejxx.ui.persistence import Preferences


def test_editor_submits_one_complete_request_and_preferences_path_only(monkeypatch, tmp_path):
    preferences = Preferences(tmp_path / "preferences.json")
    preferences.values["startup_workspace"] = "unsaved-in-memory.ijx"
    calls = []
    native = object()  # No append/open interface: startup must be in the constructor.

    def construct(**request):
        calls.append(request)
        return native

    monkeypatch.setattr(app, "_native_ui", lambda: SimpleNamespace(Editor=construct))
    editor = app.Editor(
        preferences=preferences, images=[Path("first.pgm"), "second.pgm"],
        workspace=None, restore_workspace=False, reset_layout=True,
    )
    assert editor._editor is native
    assert calls == [{
        "preferences_path": str(preferences.path),
        "images": ["first.pgm", "second.pgm"], "workspace": None,
        "restore_workspace": False, "reset_layout": True,
    }]
    assert not preferences.path.exists()


def test_editor_leaves_request_type_validation_to_native(monkeypatch):
    calls = []
    invalid = object()
    monkeypatch.setattr(app, "_native_ui", lambda: SimpleNamespace(
        Editor=lambda **request: calls.append(request),
    ))
    app.Editor(preferences=invalid, images=invalid, workspace=invalid,
               restore_workspace=1, reset_layout=1)
    assert calls == [{
        "preferences_path": invalid, "images": invalid, "workspace": invalid,
        "restore_workspace": 1, "reset_layout": 1,
    }]


def test_startup_state_and_recovery_methods_forward_native_results(monkeypatch):
    calls = []
    state = {"status": "failed", "diagnostics": [{"reason": "read_failed"}]}

    def record(name, *arguments):
        calls.append((name, arguments))
        return name

    native = SimpleNamespace(
        startup_state=state,
        retry_startup=lambda: record("retry"),
        start_empty_startup=lambda: record("empty"),
        disable_startup_restore=lambda: record("disable"),
        reset_layout=lambda persist: record("layout", persist),
    )
    monkeypatch.setattr(app, "_native_ui", lambda: SimpleNamespace(Editor=lambda **_: native))
    editor = app.Editor()
    assert editor.startup_state is state
    assert editor.retry_startup() == "retry"
    assert editor.start_empty_startup() == "empty"
    assert editor.disable_startup_restore() == "disable"
    assert editor.reset_layout(persist=False) == "layout"
    assert calls == [("retry", ()), ("empty", ()), ("disable", ()), ("layout", (False,))]


@pytest.mark.parametrize("arguments", [
    ["--workspace", "saved.ijx", "image.pgm"],
    ["--workspace", "saved.ijx", "--no-restore-workspace"],
    ["--smoke-test", "image.pgm"],
    ["--smoke-test", "--preferences", "preferences.json"],
    ["--smoke-test", "--workspace", "saved.ijx"],
    ["--smoke-test", "--no-restore-workspace"],
    ["--smoke-test", "--reset-layout"],
])
def test_cli_conflicts_reject_before_constructing(monkeypatch, arguments):
    def forbidden():
        raise AssertionError("The desktop was loaded before CLI validation")

    monkeypatch.setattr(app, "_native_ui", forbidden)
    with pytest.raises(SystemExit) as error:
        app.main(arguments)
    assert error.value.code == 2


def test_cli_forwards_the_whole_path_batch(monkeypatch, tmp_path):
    calls = []
    native = SimpleNamespace(run=lambda *args: calls.append(("run", args)) or 7)

    def construct(**request):
        calls.append(("construct", request))
        return native

    monkeypatch.setattr(app, "_native_ui", lambda: SimpleNamespace(Editor=construct))
    preferences = tmp_path / "preferences.json"
    assert app.main(["--preferences", str(preferences), "--no-restore-workspace",
                     "--reset-layout", "first.pgm", "second.pgm"]) == 7
    assert calls == [
        ("construct", {
            "preferences_path": str(preferences), "images": ["first.pgm", "second.pgm"],
            "workspace": None, "restore_workspace": False, "reset_layout": True,
        }),
        ("run", (False, 0, "")),
    ]


def test_smoke_is_explicitly_fresh(monkeypatch, tmp_path):
    calls = []
    native = SimpleNamespace(smoke_test=lambda path: calls.append(("smoke", path)) or 0)
    monkeypatch.setattr(app, "_native_ui", lambda: SimpleNamespace(
        Editor=lambda **request: calls.append(("construct", request)) or native,
    ))
    directory = tmp_path / "smoke"
    assert app.main(["--smoke-test", "--smoke-dir", str(directory)]) == 0
    assert calls[0] == ("construct", {
        "preferences_path": str(directory / "preferences.json"), "images": None,
        "workspace": None, "restore_workspace": False, "reset_layout": False,
    })
    assert calls[1] == ("smoke", str(directory))


def test_launch_paths_use_one_startup_batch(monkeypatch):
    calls = []
    native = SimpleNamespace(run=lambda *args: calls.append(("run", args)) or 0)
    monkeypatch.setattr(app, "_native_ui", lambda: SimpleNamespace(
        Editor=lambda **request: calls.append(("construct", request)) or native,
    ))
    result = ui.launch([Path("first.pgm"), "second.pgm"], hidden=True, frames=2)
    assert result._editor is native
    assert calls == [
        ("construct", {
            "preferences_path": "", "images": ["first.pgm", "second.pgm"],
            "workspace": None, "restore_workspace": None, "reset_layout": False,
        }),
        ("run", (True, 2, "")),
    ]


@pytest.fixture
def native_desktop():
    return pytest.importorskip("imagejxx._desktop", exc_type=ModuleNotFoundError)


def _configured_preferences(path, workspace):
    document = ij.PreferencesDocument()
    document.settings = ij.Settings({
        "startup_mode": "workspace", "startup_workspace": str(workspace),
    })
    document.extras_json = json.dumps({"recent": ["unrelated.pgm"], "layout": "[Window][Image]"})
    assert document.save(path)
    return path.read_bytes()


def _workspace(path, value):
    pixels = np.array([[1, 2, 3], [4, 5, value]], dtype=np.uint8)
    owner = ij.Document(ij.Dataset(pixels, axes="YX"), "Embedded pixels")
    ij.save_session(str(path), ij.SessionCapture(
        [owner.snapshot()], extensions={
            "imagejxx_workspace": {"version": 1, "active_index": 0},
        },
    ))
    return pixels


def test_native_explicit_images_suppress_missing_configured_workspace(native_desktop, tmp_path):
    preferences = tmp_path / "preferences.json"
    before = _configured_preferences(preferences, tmp_path / "missing.ijx")
    paths = [tmp_path / "first.pgm", tmp_path / "second.pgm"]
    paths[0].write_bytes(b"P5\n3 2\n255\n" + bytes([1, 2, 3, 4, 5, 6]))
    paths[1].write_bytes(b"P5\n3 2\n255\n" + bytes([7, 8, 9, 10, 11, 12]))
    editor = app.Editor(preferences=preferences, images=paths)
    assert editor.startup_state["source"] == "explicit_paths"
    assert editor.startup_state["status"] == "loaded"
    assert len(editor.document_infos) == 2 and editor.active_index == 1
    assert_array_equal(editor.live_document(editor.document_infos[0]["id"]).plane(),
                       np.array([[1, 2, 3], [4, 5, 6]], dtype=np.uint8))
    assert_array_equal(editor.live_document(editor.document_infos[1]["id"]).plane(),
                       np.array([[7, 8, 9], [10, 11, 12]], dtype=np.uint8))
    assert preferences.read_bytes() == before


def test_native_failed_restore_retry_reads_current_workspace_bytes(native_desktop, tmp_path):
    preferences, workspace = tmp_path / "preferences.json", tmp_path / "saved.ijx"
    before = _configured_preferences(preferences, workspace)
    editor = app.Editor(preferences=preferences)
    assert editor.document_infos == []
    assert editor.startup_state["status"] == "failed"
    assert editor.startup_state["retry_allowed"] is True
    assert editor.startup_state["diagnostics"]
    _workspace(workspace, 112)
    expected = _workspace(workspace, 113)
    editor.retry_startup()
    assert editor.startup_state["status"] == "loaded"
    assert editor.active_index == 0 and len(editor.document_infos) == 1
    assert_array_equal(editor.live_document().plane(), expected)
    assert preferences.read_bytes() == before


@pytest.mark.parametrize("startup_request", [
    {"images": "image.pgm"}, {"images": [17]}, {"workspace": 17},
    {"restore_workspace": 1}, {"reset_layout": 1}, {"preferences": 17},
])
def test_native_rejects_invalid_request_types(native_desktop, tmp_path, startup_request):
    values = {"preferences": tmp_path / "preferences.json", **startup_request}
    with pytest.raises((TypeError, ValueError)):
        app.Editor(**values)


@pytest.mark.parametrize("layout", [
    pytest.param(17, id="wrong-type"),
    pytest.param("bad\x00layout", id="nul"),
    pytest.param("x" * (1024 * 1024 + 1), id="oversize"),
    pytest.param("\ud800", id="surrogate"),
])
def test_opaque_preferences_layout_uses_native_validation(tmp_path, layout):
    path = tmp_path / "preferences.json"
    payload = {
        "format": "imagejxx-preferences", "version": 1, "values": {},
        "extras": {"recent": [], "layout": layout},
        "opaque": {"wide": 2**80},
    }
    original = json.dumps(payload).encode("utf-8")
    path.write_bytes(original)
    preferences = Preferences(path)
    assert "layout" not in preferences.values
    assert preferences.warnings and preferences._recovered
    assert path.read_bytes() == original
    preferences.values["layout"] = layout
    with pytest.raises((ValueError, TypeError, UnicodeError)):
        preferences.save()
    assert path.read_bytes() == original
