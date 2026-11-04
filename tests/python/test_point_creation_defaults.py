"""Per-click creation defaults; existing point metadata is never retagged."""
import json
import os
from pathlib import Path

import pytest
import imagejxx as ij


@pytest.mark.parametrize("axes", [(1, 1, 1), (2, 1, 1), (1, 2, 1), (1, 1, 2), (2, 2, 2)])
@pytest.mark.parametrize("counter", [0, 99])
@pytest.mark.parametrize("capture", [False, True])
def test_native_creation_capture(axes, counter, capture):
    t, c, z = axes
    position = ij.PlanePosition(t-1, c-1, z-1)
    value = ij.point_creation_annotation((t, c, z, 4, 5, 1), position, counter, capture)
    assert value.counter == counter
    if capture and axes != (1, 1, 1):
        assert (value.position.t, value.position.c, value.position.z) == (t-1, c-1, z-1)
    else:
        assert value.position is None
    selection = ij.with_point_annotations(ij.Selection("point", [(1.25, 2.5)]), [value])
    packed = ((t-1)*z+(z-1))*c+(c-1)+1 if value.position is not None else 0
    assert ij.imagej_point_counters(selection, (t, c, z, 4, 5, 1)) == [(packed << 8) | counter]


@pytest.mark.parametrize("position", [(2, 0, 0), (0, 2, 0), (0, 0, 2)])
@pytest.mark.parametrize("capture", [False, True])
def test_outside_position_refuses_even_when_unbound(position, capture):
    with pytest.raises(ValueError, match="outside"):
        ij.point_creation_annotation((2, 2, 2, 4, 5, 1), ij.PlanePosition(*position), 0, capture)


@pytest.mark.parametrize("counter", [True, 1.0, -1, 100, 2**65])
def test_strict_counter(counter):
    with pytest.raises((TypeError, ValueError, OverflowError)):
        ij.point_creation_annotation((1, 1, 1, 4, 5, 1), ij.PlanePosition(0, 0, 0), counter)


def test_capture_boolean_is_strict():
    with pytest.raises(TypeError, match="Boolean"):
        ij.point_creation_annotation((1, 1, 1, 4, 5, 1), ij.PlanePosition(0, 0, 0), 0, 1)


def test_defaults_validate_reset_persist_and_do_not_retag(tmp_path):
    settings = ij.Settings()
    assert settings["point_counter"] == 0 and settings["point_capture_position"] is True
    original = ij.with_point_annotations(ij.Selection("point", [(1, 2)]), [ij.PointAnnotation(7)])
    settings.update({"point_counter": 99, "point_capture_position": False})
    before = settings.to_dict()
    with pytest.raises(ValueError):settings.update({"point_counter": 100, "point_capture_position": True})
    assert settings.to_dict() == before and ij.point_annotations(original)[0].counter == 7
    prefs = ij.PreferencesDocument();prefs.settings = settings
    path = tmp_path / "prefs.json";prefs.save(path)
    loaded = ij.PreferencesDocument.load(path, False)
    assert loaded.settings["point_counter"] == 99 and loaded.settings["point_capture_position"] is False
    loaded.settings.reset("Drawing")
    assert loaded.settings["point_counter"] == 0 and loaded.settings["point_capture_position"] is True


@pytest.mark.parametrize("context", ["imgui_cpp", "imgui_python"])
@pytest.mark.parametrize("stack", [False, True])
def test_actual_creation_defaults(stack, context, tmp_path):
    from actual_observation_transport import decode_actual_observations
    key = "IMAGEJXX_POINT_STACK_DEFAULTS" if stack else "IMAGEJXX_POINT_DEFAULTS"
    case = "point-stack-defaults" if stack else "point-defaults"
    if context == "imgui_cpp":
        raw = json.loads(Path(os.environ[key+"_CPP_REPORT"]).read_bytes())
    elif os.environ.get(key+"_PYTHON_REPORT"):
        raw = json.loads(Path(os.environ[key+"_PYTHON_REPORT"]).read_bytes())
    else:
        from imagejxx import _desktop
        editor = _desktop.Editor(str(tmp_path / "preferences.json"))
        raw = json.loads(editor._settings_drawing_ui_case(case, str(tmp_path / "capture")))
    report = decode_actual_observations(raw)
    assert report["state"] == "observed" and report["context"] == context and not report["acceptance_claim"]
    assert report["case_id"] == case
    for frame in report["frames"]:assert frame["events"] == frame["report"]["input_frame"]["raw_inputs"]
    states = report["states"]
    applied = states["point_applied"]
    assert applied["settings"]["point_counter"] == 7 and applied["settings"]["point_capture_position"] is False
    multi = states["multipoint_created"]["owners"][0]["selection"]
    assert multi["kind"] == "multipoint" and multi["points"] == [[4, 4], [6, 6]]
    assert multi["imagejxx.point_annotations"] == {"version": 1, "points": [{"counter": 7, "position": None}, {"counter": 2, "position": [0, 0, 1] if stack else None}]}
    single = states["single_created"]["owners"][0]
    assert single["selection"]["kind"] == "point"
    if stack:
        assert single["selection"]["imagejxx.point_annotations"]["points"] == [{"counter": 0, "position": [0, 0, 1]}]
    else:
        assert "imagejxx.point_annotations" not in single["selection"]
    assert single["previous"] == multi and single["history"]["undo_steps"] == 0
    assert states["final"]["settings"]["point_counter"] == 2 and states["final"]["settings"]["point_capture_position"] is True
    saved = json.loads(states["final"]["preferences_file"]["utf8"])
    assert saved["values"]["point_counter"] == 2 and saved["values"]["point_capture_position"] is True
    from imagejxx import _desktop
    reopened = _desktop.Editor(states["final"]["preferences_file"]["path"])
    assert json.loads(reopened.settings())["point_counter"] == 2
    controls = {x["control"] for x in report["events"] if x.get("kind") == "control_click"}
    assert {"field.point_counter", "field.point_capture_position", "preferences.apply"} <= controls
