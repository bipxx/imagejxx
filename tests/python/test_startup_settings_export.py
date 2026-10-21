"""Actual-schema reader support for the native additive Text setting kind."""
import importlib.util
from pathlib import Path

import pytest

ROOT = Path(__file__).absolute().parents[2]
spec = importlib.util.spec_from_file_location("startup_export_consumers", ROOT / "scripts/parity_consumers.py")
consumers = importlib.util.module_from_spec(spec)
spec.loader.exec_module(consumers)


def export(value):
    return {"schema_version": 1, "definitions": [{
        "name": "startup_workspace", "group": "Startup", "label": "Workspace",
        "description": "Explicitly selected saved workspace.", "kind": "text",
        "default": value, "minimum": None, "maximum": None, "choices": [],
    }], "defaults": {"startup_workspace": value}}


@pytest.mark.parametrize("value", ["", "relative.ijx", "\u03bb" * 2048])
def test_text_export_accepts_native_empty_and_utf8_byte_boundary(value):
    assert consumers.parse_export(export(value), "startup export")["startup_workspace"]["default"] == value


@pytest.mark.parametrize("value", [True, 1, [], "a\0b", "\ud800", "\u03bb" * 2049])
def test_text_export_rejects_wrong_type_nul_surrogate_or_byte_overflow(value):
    with pytest.raises(ValueError):
        consumers.parse_export(export(value), "startup export")
