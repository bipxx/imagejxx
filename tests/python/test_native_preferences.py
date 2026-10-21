"""Headless native preferences files and lossless Python facade compatibility."""

import hashlib
import json
from pathlib import Path

import imagejxx as ij
import numpy as np
from numpy.testing import assert_array_equal
import pytest

from imagejxx.ui.persistence import Preferences


def versioned(*, values=None, extras=None, **fields):
    return {
        **fields,
        "format": "imagejxx-preferences",
        "version": 1,
        "values": {} if values is None else values,
        "extras": {"recent": []} if extras is None else extras,
    }


def write_document(path, document):
    path.write_text(json.dumps(document, allow_nan=False), encoding="utf-8")


def test_public_default_document_and_detached_settings():
    first, second = ij.PreferencesDocument(), ij.PreferencesDocument()
    assert first.settings.to_dict() == ij.Settings().to_dict()
    assert json.loads(first.extras_json) == {"recent": []}
    assert json.loads(first.unknown_values_json) == {}
    assert json.loads(first.unknown_fields_json) == {}
    assert first.warnings == [] and first.write_blocked is False
    first.settings["binary_iterations"] = 2
    assert second.settings["binary_iterations"] == 1
    assert json.loads(first.json())["values"]["binary_iterations"] == 2
    assert "PreferencesDocument" in ij.__all__
    assert "PreferencesRepresentationError" in ij.__all__
    assert issubclass(ij.PreferencesRepresentationError, ValueError)


def test_native_versioned_unknown_compartments_and_known_shadow(tmp_path):
    payload = versioned(
        values={"binary_iterations": 2, "future_setting": {"nested": [True, None]}},
        extras={"recent": ["测量.npy"], "theme": {"extension": True}},
        plugin_metadata={"label": "αβ", "values": [1.25, False]},
    )
    document = ij.PreferencesDocument.parse(json.dumps(payload), recover=False)
    assert document.settings["binary_iterations"] == 2
    assert document.settings["theme"] == "dark"
    assert json.loads(document.unknown_values_json) == {
        "future_setting": {"nested": [True, None]}
    }
    assert json.loads(document.unknown_fields_json) == {
        "plugin_metadata": payload["plugin_metadata"]
    }
    assert json.loads(document.extras_json) == payload["extras"]
    path = tmp_path / "配置 α" / "preferences.json"
    assert document.save(path) is True
    reopened = ij.PreferencesDocument.load(str(path), recover=False)
    assert json.loads(reopened.json()) == json.loads(document.json())
    document.settings["binary_iterations"] = 3
    assert document.save(str(path)) is True  # Replace an existing file on Windows.
    assert reopened.settings["binary_iterations"] == 2
    assert ij.PreferencesDocument.load(path).settings["binary_iterations"] == 3
    assert list(path.parent.glob("preferences.json.tmp.*")) == []


def test_native_legacy_migration_preserves_unknown_extras():
    document = ij.PreferencesDocument.parse(
        json.dumps(
            {
                "brush_width": 14,
                "foreground": "red",
                "background": "#03A",
                "recent": ["image.npy"],
                "plugin": {"values": [1, 2]},
            }
        ),
        recover=False,
    )
    assert document.settings["point_size"] == 14
    assert document.settings["foreground"] == "#ff0000"
    assert document.settings["background"] == "#0033aa"
    assert document.warnings == [] and document.write_blocked is False
    saved = json.loads(document.json())
    assert saved["format"] == "imagejxx-preferences" and saved["version"] == 1
    assert saved["extras"]["plugin"] == {"values": [1, 2]}
    assert json.loads(document.unknown_values_json) == {}


def test_native_missing_file_returns_writable_defaults(tmp_path):
    path = tmp_path / "missing.json"
    document = ij.PreferencesDocument.load(path, recover=True)
    assert document.settings.to_dict() == ij.Settings().to_dict()
    assert document.warnings == [] and document.write_blocked is False
    with pytest.raises(ValueError, match="does not exist"):
        ij.PreferencesDocument.load(path, recover=False)
    assert not path.exists()


@pytest.mark.parametrize(
    "payload",
    [
        {"history_limit": 0, "brush_width": 12},
        versioned(values={"history_limit": True, "brush_width": 12}),
        versioned(extras={"recent": 1}),
        versioned(extras={"recent": ["x"] * 16}),
        versioned(extras={"recent": ["x", 7]}),
        versioned(extras={"recent": [], "layout": 12}),
    ],
)
def test_native_strict_and_recovery_validation(payload):
    text = json.dumps(payload)
    with pytest.raises(ValueError):
        ij.PreferencesDocument.parse(text, recover=False)
    document = ij.PreferencesDocument.parse(text, recover=True)
    assert document.warnings and document.write_blocked is True
    assert document.settings["history_limit"] == ij.Settings()["history_limit"]
    if (
        payload.get("brush_width") == 12
        or payload.get("values", {}).get("brush_width") == 12
    ):
        assert document.settings["brush_width"] == 12
    extras = json.loads(document.extras_json)
    assert extras["recent"] == []
    if "layout" in payload.get("extras", {}):
        assert "layout" not in extras


@pytest.mark.parametrize(
    "text",
    [
        "not json",
        "[]",
        '{"format":"imagejxx-preferences","version":99,"values":{}}',
        '{"format":"imagejxx-preferences","version":true,"values":{}}',
        '{"format":"imagejxx-preferences","version":1,"values":[]}',
        '{"format":"imagejxx-preferences","version":1,"values":{},"extras":[]}',
        '{"unknown":NaN}',
        '{"unknown":1e999}',
        '{"unknown":"\\ud800"}',
        b'{"unknown":"\xff"}',
    ],
)
def test_native_parse_never_recovers_malformed_envelope_or_encoding(text):
    for recover in (False, True):
        with pytest.raises(ValueError):
            ij.PreferencesDocument.parse(text, recover=recover)


def test_native_load_recovery_blocks_automatic_save_and_explicit_backup(tmp_path):
    path = tmp_path / "bad.json"
    original = b'{"format":"imagejxx-preferences","version":99,"values":{}}'
    path.write_bytes(original)
    with pytest.raises(ValueError, match="version"):
        ij.PreferencesDocument.load(path, recover=False)
    document = ij.PreferencesDocument.load(path, recover=True)
    assert document.warnings and document.write_blocked is True
    assert document.save(path) is False
    assert path.read_bytes() == original
    assert list(tmp_path.glob("bad.json.bak*")) == []
    document.settings["binary_iterations"] = 2
    existing_backup = Path(str(path) + ".bak")
    existing_backup.write_bytes(b"older backup")
    assert document.save(path, explicit_save=True) is True
    assert existing_backup.read_bytes() == b"older backup"
    assert Path(str(path) + ".bak.1").read_bytes() == original
    assert document.write_blocked is True and document.warnings
    reopened = ij.PreferencesDocument.load(path, recover=False)
    assert reopened.settings["binary_iterations"] == 2
    assert reopened.warnings == [] and reopened.write_blocked is False


@pytest.mark.parametrize(
    "member,text",
    [
        ("extras_json", "[]"),
        ("extras_json", '{"recent":[3]}'),
        ("extras_json", '{"recent":[],"layout":3}'),
        ("unknown_values_json", "not json"),
        ("unknown_fields_json", '{"value":NaN}'),
        ("unknown_fields_json", '{"value":"\\ud800"}'),
    ],
)
def test_native_invalid_save_validates_before_publication(tmp_path, member, text):
    path = tmp_path / "preferences.json"
    document = ij.PreferencesDocument()
    assert document.save(path) is True
    original = path.read_bytes()
    setattr(document, member, text)
    document.write_blocked = True
    with pytest.raises(ValueError):
        document.save(path, explicit_save=True)
    assert path.read_bytes() == original
    assert list(tmp_path.glob("preferences.json.bak*")) == []
    assert list(tmp_path.glob("preferences.json.tmp.*")) == []


def test_native_io_failures_are_os_errors_and_preserve_destination(tmp_path):
    directory = tmp_path / "directory.json"
    directory.mkdir()
    sentinel = directory / "original"
    sentinel.write_bytes(b"keep")
    with pytest.raises(OSError) as load_error:
        ij.PreferencesDocument.load(directory, recover=False)
    assert str(load_error.value)
    recovered = ij.PreferencesDocument.load(directory, recover=True)
    assert recovered.warnings and recovered.write_blocked is True
    document = ij.PreferencesDocument()
    with pytest.raises(OSError) as save_error:
        document.save(directory)
    assert str(save_error.value)
    assert directory.is_dir() and sentinel.read_bytes() == b"keep"
    assert list(tmp_path.glob("directory.json.tmp.*")) == []
    parent_file = tmp_path / "parent-is-a-file"
    parent_file.write_bytes(b"keep parent")
    with pytest.raises(OSError):
        document.save(parent_file / "preferences.json")
    assert parent_file.read_bytes() == b"keep parent"


@pytest.mark.parametrize("value", [b"file.json", 7, None])
def test_native_paths_require_unicode_string_or_pathlike(value):
    document = ij.PreferencesDocument()
    with pytest.raises(TypeError):
        ij.PreferencesDocument.load(value)
    with pytest.raises(TypeError):
        document.save(value)


@pytest.mark.parametrize("value", [-(2**63), 2**63 - 1, 2**63, 2**64 - 1])
def test_native_opaque_integer_boundaries_are_lossless(value, tmp_path):
    document = ij.PreferencesDocument.parse(
        json.dumps(
            versioned(
                values={"future": value},
                extras={"recent": [], "future": value},
                future=value,
            )
        ),
        recover=False,
    )
    path = tmp_path / "integers.json"
    assert document.save(path) is True
    saved = json.loads(ij.PreferencesDocument.load(path, recover=False).json())
    assert type(saved["values"]["future"]) is int
    assert saved["values"]["future"] == value
    assert saved["extras"]["future"] == value and saved["future"] == value


@pytest.mark.parametrize("value", [-(2**63) - 1, 2**64, 2**200])
def test_native_overflowing_integer_lexemes_have_distinct_error(value, tmp_path):
    text = json.dumps(versioned(values={"future": value}))
    for recover in (False, True):
        with pytest.raises(ij.PreferencesRepresentationError):
            ij.PreferencesDocument.parse(text, recover=recover)
    path = tmp_path / "overflow.json"
    path.write_text(text, encoding="utf-8")
    with pytest.raises(ij.PreferencesRepresentationError):
        ij.PreferencesDocument.load(path, recover=False)
    recovered = ij.PreferencesDocument.load(path, recover=True)
    assert recovered.warnings and recovered.write_blocked
    original = path.read_bytes()
    document = ij.PreferencesDocument()
    document.unknown_values_json = json.dumps({"future": value})
    with pytest.raises(ij.PreferencesRepresentationError):
        document.save(path)
    assert path.read_bytes() == original
    assert list(tmp_path.glob("overflow.json.tmp.*")) == []


def test_native_opaque_nesting_exceeds_metadata_limit(tmp_path):
    opaque = {"leaf": "keep"}
    for _ in range(180):
        opaque = {"nested": opaque}
    document = ij.PreferencesDocument.parse(
        json.dumps(versioned(values={"future": opaque}, future=opaque)), recover=False
    )
    path = tmp_path / "deep.json"
    assert document.save(path) is True
    saved = json.loads(ij.PreferencesDocument.load(path, recover=False).json())
    assert saved["values"]["future"] == opaque and saved["future"] == opaque
    facade = Preferences(path)
    assert facade.warnings == []
    facade.save()
    assert json.loads(path.read_text(encoding="utf-8"))["future"] == opaque


@pytest.mark.parametrize("opaque", [2**200, -(2**200), "\ud800", "tail\udfff"])
def test_facade_retains_unsupported_opaque_representations_and_shadowed_extras(
    tmp_path, opaque
):
    path = tmp_path / "opaque.json"
    payload = versioned(
        values={"binary_iterations": 2, "future_setting": {"opaque": opaque}},
        extras={"recent": ["a.npy"], "theme": {"opaque": opaque}, "plugin": opaque},
        plugin_metadata={"opaque": opaque},
    )
    write_document(path, payload)
    preferences = Preferences(path)
    assert preferences.warnings == []
    assert preferences.values["binary_iterations"] == 2
    assert preferences.values["theme"] == ij.Settings()["theme"]
    assert preferences.values["plugin"] == opaque
    preferences.values["binary_iterations"] = 3
    preferences.save()
    saved = json.loads(path.read_text(encoding="utf-8"))
    assert saved["values"]["future_setting"] == payload["values"]["future_setting"]
    assert saved["extras"] == payload["extras"]
    assert saved["plugin_metadata"] == payload["plugin_metadata"]
    assert saved["values"]["binary_iterations"] == 3
    assert Preferences(path).warnings == []
    assert list(tmp_path.glob("opaque.json.tmp.*")) == []


def test_facade_legacy_opaque_values_migrate_to_extras(tmp_path):
    path = tmp_path / "legacy.json"
    write_document(path, {"foreground": "red", "brush_width": 12, "plugin": 2**200})
    preferences = Preferences(path)
    assert preferences.values["foreground"] == "#ff0000"
    assert preferences.values["point_size"] == 12
    assert preferences.values["plugin"] == 2**200
    preferences.save()
    saved = json.loads(path.read_text(encoding="utf-8"))
    assert saved["extras"]["plugin"] == 2**200
    assert "plugin" not in saved["values"]


@pytest.mark.parametrize("opaque", [None, 2**200, "\ud800"])
def test_facade_known_overflow_still_migrates_and_explicit_save_clears_warnings(
    tmp_path, opaque
):
    path = tmp_path / "invalid-known.json"
    payload = versioned(
        values={"history_limit": 2**200, "brush_width": 12, "future": opaque}
    )
    write_document(path, payload)
    original = path.read_bytes()
    preferences = Preferences(path)
    assert preferences.warnings and preferences._recovered
    assert preferences.values["history_limit"] == ij.Settings()["history_limit"]
    assert preferences.values["brush_width"] == 12
    assert path.read_bytes() == original
    preferences.save()
    assert preferences.warnings == [] and preferences._recovered is False
    assert Path(str(path) + ".bak").read_bytes() == original
    assert json.loads(path.read_text(encoding="utf-8"))["values"]["future"] == opaque
    assert Preferences(path).warnings == []


def test_facade_unsupported_opaque_content_cannot_hide_schema_or_known_save_errors(
    tmp_path,
):
    path = tmp_path / "bad-version.json"
    payload = versioned(values={"future": 2**200})
    payload["version"] = 99
    write_document(path, payload)
    preferences = Preferences(path)
    assert preferences.warnings and "version" in preferences.warnings[0]
    assert preferences.values == {**ij.Settings().to_dict(), "recent": []}
    original = path.read_bytes()
    preferences.values["plugin"] = 2**200
    preferences.values["history_limit"] = 0
    with pytest.raises(ValueError):
        preferences.save()
    assert path.read_bytes() == original
    assert list(tmp_path.glob("bad-version.json.bak*")) == []
    assert preferences.warnings and preferences._recovered


@pytest.mark.parametrize("legacy", [False, True])
def test_facade_known_exponent_overflow_migrates_each_setting(tmp_path, legacy):
    path = tmp_path / "exponent.json"
    values = '{"ui_scale":1e500,"theme":"light"}'
    text = (
        values
        if legacy
        else '{"format":"imagejxx-preferences","version":1,"values":'
        + values
        + ',"extras":{"recent":[]}}'
    )
    path.write_text(text, encoding="utf-8")
    original = path.read_bytes()
    preferences = Preferences(path)
    assert preferences.values["theme"] == "light"
    assert preferences.values["ui_scale"] == ij.Settings()["ui_scale"]
    assert preferences.warnings and preferences._recovered
    assert path.read_bytes() == original
    preferences.save()
    assert Path(str(path) + ".bak").read_bytes() == original
    assert preferences.warnings == [] and preferences._recovered is False
    assert Preferences(path).values["theme"] == "light"


@pytest.mark.parametrize("legacy", [False, True])
def test_facade_opaque_exponent_overflow_loads_but_save_preserves_original(
    tmp_path, legacy
):
    path = tmp_path / "opaque-exponent.json"
    values = '{"theme":"light","future":1e500}'
    text = (
        values
        if legacy
        else '{"format":"imagejxx-preferences","version":1,"values":'
        + values
        + ',"extras":{"recent":[]}}'
    )
    path.write_text(text, encoding="utf-8")
    original = path.read_bytes()
    preferences = Preferences(path)
    assert preferences.values["theme"] == "light"
    assert preferences.warnings == []
    opaque = (
        preferences.values["future"]
        if legacy
        else preferences._unknown_values["future"]
    )
    assert np.isinf(opaque)
    with pytest.raises(ValueError):
        preferences.save()
    assert path.read_bytes() == original
    assert list(tmp_path.glob("opaque-exponent.json.bak*")) == []
    assert list(tmp_path.glob("opaque-exponent.json.tmp.*")) == []


@pytest.mark.parametrize("opaque", [None, 2**200])
def test_facade_failed_save_retains_recovery_state(tmp_path, opaque):
    path = tmp_path / "preferences.json"
    write_document(path, versioned(values={"history_limit": 0, "future": opaque}))
    preferences = Preferences(path)
    warnings = list(preferences.warnings)
    original = path.read_bytes()
    parent_file = tmp_path / "unwritable-parent"
    parent_file.write_bytes(b"keep")
    preferences.path = parent_file / "preferences.json"
    with pytest.raises(OSError):
        preferences.save()
    assert preferences.warnings == warnings and preferences._recovered
    assert parent_file.read_bytes() == b"keep" and path.read_bytes() == original
    preferences.path = path
    preferences.save()
    assert preferences.warnings == [] and preferences._recovered is False
    assert Path(str(path) + ".bak").read_bytes() == original


def test_facade_opaque_recovery_backup_reservation_never_overwrites_collision(
    tmp_path, monkeypatch
):
    path = tmp_path / "preferences.json"
    write_document(path, versioned(values={"history_limit": 0, "future": 2**200}))
    original = path.read_bytes()
    preferences = Preferences(path)
    assert preferences.warnings and preferences._recovered
    candidate = Path(str(path) + ".bak")
    occupied = b"another writer's backup"
    original_open = Path.open
    collided = False

    def collide_before_exclusive_open(self, mode="r", *args, **kwargs):
        nonlocal collided
        if self == candidate and mode == "xb" and not collided:
            with original_open(self, "wb") as stream:
                stream.write(occupied)
            collided = True
        return original_open(self, mode, *args, **kwargs)

    monkeypatch.setattr(Path, "open", collide_before_exclusive_open)
    preferences.save()
    assert collided
    assert candidate.read_bytes() == occupied
    assert Path(str(path) + ".bak.1").read_bytes() == original
    assert preferences.warnings == [] and preferences._recovered is False
    assert json.loads(path.read_text(encoding="utf-8"))["values"]["future"] == 2**200
    assert list(tmp_path.glob("preferences.json.tmp.*")) == []


@pytest.mark.parametrize("case_index,foreground", [(7, 35), (103, 15), (199, 3)])
def test_saved_and_reopened_native_binary_iterations_compute_pinned_pixels(
    tmp_path, case_index, foreground
):
    fixture_path = Path(__file__).resolve().parents[1] / "fixtures/binary-imagej154r.json"
    fixture_bytes = fixture_path.read_bytes()
    assert hashlib.sha256(fixture_bytes).hexdigest() == (
        "f5e6178a452a8b277577ff1c166c35be043e997553eb5d09f523c9111f132ddd"
    )
    case = json.loads(fixture_bytes)["cases"][case_index]
    assert case["operation"] == "erode" and case["shape"] == [7, 9]
    document = ij.PreferencesDocument()
    document.settings.update(
        {
            "binary_iterations": case["iterations"],
            "binary_neighbors": case["neighbors"],
            "binary_black_background": case["black_background"],
            "binary_pad_edges": case["pad_edges"],
        }
    )
    path = tmp_path / "binary.json"
    assert document.save(path) is True
    reopened = ij.PreferencesDocument.load(path, recover=False)
    source = np.array(case["source"], np.uint8).reshape(case["shape"])
    wanted = np.array(case["pixels"], np.uint8).reshape(case["shape"])
    result = ij.binary_erode(source, options=reopened.settings.binary_options())
    assert_array_equal(result, wanted)
    assert np.count_nonzero(result) == foreground
    facade = Preferences(path)
    facade.save()
    settings = ij.PreferencesDocument.load(path, recover=False).settings
    assert_array_equal(ij.binary_erode(source, options=settings.binary_options()), wanted)
