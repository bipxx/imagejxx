"""Independent literal readers for installed shared-ImGui Wand preference runs.

This reader never imports imagejxx or computes a flood fill. Required acceptance
runs supply all producer reports, including the two separate persistence hosts.
"""
import hashlib
import json
import math
import os
from pathlib import Path
import struct

import pytest


CASES = ("wand-consume", "wand-drafts", "wand-persist", "wand-reopen")
CONTEXTS = ("imgui_cpp", "imgui_python")
MAX_BYTES = 16 * 1024**2
MASKS = {
    (0, 4): "ff0000000000000000",
    (0, 8): "ff000000ff00000000",
    (1, 8): "ff000000ff000000ff",
    (1, 4): "ff0000000000000000",
}
SOURCE = bytes([10, 20, 20, 20, 10, 20, 20, 20, 11])
STATES = {
    "wand-consume": ("before", "default_selected", "eight_applied", "eight_selected",
                     "tolerant_applied", "tolerant_selected", "four_selected"),
    "wand-drafts": ("opened", "invalid_draft", "refused", "corrected", "reverted",
                    "closed_reopened", "accepted", "defaults_draft", "defaults_applied"),
    "wand-persist": ("draft", "saved", "closed"),
    "wand-reopen": ("loaded", "selected", "shown"),
}


def _unique(pairs):
    result = {}
    for key, value in pairs:
        assert key not in result, "Duplicate observation JSON key: " + key
        result[key] = value
    return result


def decode(raw):
    assert 0 < len(raw) < MAX_BYTES
    def invalid(token):
        raise AssertionError("Nonfinite JSON token: " + token)
    result = json.loads(raw, object_pairs_hook=_unique, parse_constant=invalid)
    pending, count = [(result, 0)], 0
    while pending:
        value, depth = pending.pop()
        count += 1
        assert count <= 500000 and depth <= 64
        if type(value) is float:
            assert math.isfinite(value)
        elif isinstance(value, dict):
            pending.extend((child, depth + 1) for child in value.values())
        elif isinstance(value, list):
            pending.extend((child, depth + 1) for child in value)
    return result


@pytest.fixture(scope="session")
def python_report_factory(tmp_path_factory):
    reports = None
    def produce():
        nonlocal reports
        if reports is None:
            from settings_wand_ui_runner import produce_python_reports
            reports = produce_python_reports(tmp_path_factory.mktemp("cfg1-python-ui"))
        return reports
    return produce


def load_case(case_id, context, python_reports=None):
    reports = os.environ.get("IMAGEJXX_SETTINGS_WAND_UI_REPORTS")
    if reports:
        path = json.loads(reports)[context][case_id]
    elif context == "imgui_cpp" and os.environ.get("IMAGEJXX_SETTINGS_WAND_CPP_REPORTS"):
        path = json.loads(os.environ["IMAGEJXX_SETTINGS_WAND_CPP_REPORTS"])[case_id]
    elif (context == "imgui_python" and python_reports is not None
          and os.environ.get("IMAGEJXX_REQUIRE_RENDER_TESTS") == "1"):
        path = python_reports()[case_id]
    else:
        if os.environ.get("IMAGEJXX_REQUIRE_RENDER_TESTS") == "1":
            pytest.fail("Required installed Wand UI report is missing: " + context + "/" + case_id)
        pytest.skip("Installed Wand UI reports were not supplied")
    raw = Path(path).read_bytes()
    row = decode(raw)
    assert row["schema"] == "cfg1.settings-wand.ui.001"
    assert row["case_id"] == case_id and row["context"] == context
    assert row["acceptance_claim"] is False and row["status"] == "observed"
    assert row["error"] == ""
    assert set(row["states"]) == set(STATES[case_id])
    assert 0 < len(row["frames"]) <= 750
    assert row["events"] and row["setup_events"]
    assert row["bounds"] == {"frames": 750, "seconds": 60,
                             "report_bytes_exclusive": MAX_BYTES, "preference_bytes": 1024**2}
    assert type(row["fixture"]["runtime"]["process_id"]) is int
    for index, frame in enumerate(row["frames"]):
        assert frame["ordinal"] == index
        assert isinstance(frame["events"], list)
        assert frame["rendered"]["input_frame"]["raw_inputs"] == frame["events"]
        assert {"input_frame", "session", "preferences_panel", "host_controls",
                "ordinary_canvas", "window_error", "error_modal", "actual_hover"} <= set(frame["rendered"])
    for name in STATES[case_id]:
        state = row["states"][name]
        assert 0 <= state["frame"] < len(row["frames"])
        assert state["error"] == ""
        preferences_file(state["preferences_file"])
        assert {"preferences_panel", "tools", "ordinary_pixels"} <= set(state["observations"])
        for key in ("running", "queued", "running_bytes", "retained_bytes", "reserved_bytes"):
            assert state["usage"][key] == 0
    assert [row["states"][name]["frame"] for name in STATES[case_id]] == sorted(
        row["states"][name]["frame"] for name in STATES[case_id])
    input_protocol(row)
    return row


def preferences_file(file):
    assert isinstance(file["path"], str) and Path(file["path"]).is_absolute()
    if file["exists"]:
        raw = file["utf8"].encode("utf-8")
        assert len(raw) == file["byte_count"]
        assert hashlib.sha256(raw).hexdigest() == file["sha256"]
        payload = decode(raw)
        assert payload["format"] == "imagejxx-preferences" and payload["version"] == 1
        return payload
    assert file["byte_count"] == 0 and file["utf8"] in (None, "")
    return None


def pair(state):
    return state["settings"]["wand_tolerance"], state["settings"]["wand_connectivity"]


def observation_mask(observation, *, packed=True):
    assert observation is not None
    assert observation["mask_shape"] == [3, 3]
    raw = bytes.fromhex(observation["mask_u8_hex"])
    assert len(raw) == 9 and set(raw) <= {0, 255}
    # Existing native mask transport is MSB-first within each byte. These are
    # literal encodings, independent of the product's mask packer.
    packed_literals = {
        "ff0000000000000000": "8000",
        "ff000000ff00000000": "8800",
        "ff000000ff000000ff": "8880",
        "00000000ffff00ffff": "0d80",
    }
    if packed:
        assert observation["packed_mask_hex"] == packed_literals[raw.hex()]
    else:
        assert observation["packed_mask_hex"] is None
    return raw.hex()


def invariant_owner(state, baseline):
    owner, old = state["owner"], baseline["owner"]
    assert owner["shape"] == [1, 1, 1, 3, 3, 1] and owner["dtype"] == "uint8"
    assert owner["declared_u8_hex"] == SOURCE.hex()
    assert owner["storage_f64_hex"] == b"".join(struct.pack("<d", value) for value in SOURCE).hex()
    for key in ("document_id", "calibration", "rois", "overlays", "metadata",
                "dirty", "undo", "redo", "history", "display"):
        assert owner[key] == old[key], key
    assert owner["revisions"]["pixels"] == old["revisions"]["pixels"]
    assert owner["revisions"]["metadata"] == old["revisions"]["metadata"]
    assert state["workflow"] == baseline["workflow"]
    assert state["results"] == baseline["results"]
    assert state["recording"] == baseline["recording"]
    assert len(state["settings"]) == 61
    assert {key: value for key, value in state["settings"].items() if key not in
            ("wand_tolerance", "wand_connectivity")} == {
                key: value for key, value in baseline["settings"].items() if key not in
                ("wand_tolerance", "wand_connectivity")}


def input_protocol(row):
    frames = row["frames"]
    for event in row["events"]:
        start, end = event["frame_before"], event["frame_after"]
        assert 0 <= start < end < len(frames)
        interval = frames[start:end + 1]
        if event["kind"] == "text_input":
            delivered = [f for f in interval if any(atom.get("type") == "text" and
                         atom["value"] == event["text"] for atom in f["events"])]
            assert len(delivered) == 1
            frame = delivered[0]["rendered"]
            assert frame["input_frame"]["focus"]["active_id"] == event["input_id"]
            assert frame["input_frame"]["input_text"]["id"] == event["input_id"]
            if event["control"].startswith("field."):
                name = event["control"].split(".", 1)[1]
                control = frame["preferences_panel"]["fields"][name]
                assert control["id"] == event["input_id"]
                assert control["enabled"] and control["visible"]
        else:
            assert event["kind"] in ("control_click", "canvas_click")
            buttons = [atom for f in interval for atom in f["events"]
                       if atom.get("type") == "button" and atom["name"] == "left"]
            assert any(atom["down"] is True for atom in buttons)
            assert any(atom["down"] is False for atom in buttons)
            assert event["hovered_id"] > 0 and event["hovered_window"]
            assert any(f["rendered"]["actual_hover"]["id"] == event["hovered_id"] for f in interval)
            if event["kind"] == "control_click":
                control = event["actual_control"]
                assert event["hovered_id"] == control["id"] and control["enabled"]
                x, y = event["actual_mouse"]
                for rect in (control["rect"], event["actual_clip"]):
                    assert rect[0] <= x < rect[2] and rect[1] <= y < rect[3]
            else:
                canvas = event["canvas"]
                assert event["point"] == event["actual_image_cell"] == [0, 0]
                assert event["hovered_id"] == canvas["canvas_id"]
                assert [math.floor((v - origin) / scale) for v, origin, scale in
                        zip(event["actual_mouse"], canvas["origin"], canvas["scale"])] == [0, 0]
    clicks = [e["control"] for e in row["events"] if e["kind"] == "control_click"]
    assert "host.menu.Edit" in clicks and "host.menu.Edit.Preferences" in clicks
    if row["case_id"] in ("wand-consume", "wand-reopen"):
        assert "host.tools.tool.wand" in clicks
        assert sum(e["kind"] == "canvas_click" for e in row["events"]) == (
            4 if row["case_id"] == "wand-consume" else 1)


def field(state, name):
    return state["observations"]["preferences_panel"]["fields"][name]


def opaque(payload):
    assert payload["values"]["cfg1_future"] == {"items": [True, None, "λ", 2**64 - 1]}
    assert payload["extras"]["cfg1_extra"] == {"label": "μ", "values": [3, 1]}
    assert payload["extras"]["recent"] == []
    assert payload["cfg1_root"] == {"note": "root", "uint": 2**64 - 1}


def persisted_settings(state):
    payload = preferences_file(state["preferences_file"])
    assert payload is not None
    assert all(payload["values"][key] == value for key, value in state["settings"].items())


def required_events(row, start, end, required):
    actual = [(e["kind"], e["control"], e.get("text")) for e in row["events"]
              if start <= e["frame_before"] and e["frame_after"] <= end]
    cursor = 0
    for expected in required:
        while cursor < len(actual) and actual[cursor] != expected:
            cursor += 1
        assert cursor < len(actual), (row["case_id"], expected, start, end)
        cursor += 1


def click(name):
    return "control_click", name, None


def text(name, value):
    return "text_input", name, value


OPEN_PREFERENCES = [click("host.menu.Edit"), click("host.menu.Edit.Preferences"),
                    text("preferences.search", "wand"), click("preferences.group"), click("group.Selection")]
WAND = [click("host.tools.tool.wand"), ("canvas_click", "wand", None)]


@pytest.mark.parametrize("context", CONTEXTS)
@pytest.mark.parametrize("case_id", CASES)
def test_installed_wand_preferences_consumer_and_fresh_reopen(case_id, context, python_report_factory):
    row = load_case(case_id, context, python_report_factory)
    states = row["states"]
    baseline = states[STATES[case_id][0]]
    for state in states.values():
        invariant_owner(state, baseline)
    assert baseline["owner"]["rois"] and baseline["owner"]["overlays"]
    assert baseline["owner"]["metadata"] == {
        "title": "CFG1 Wand preferences", "properties": {"cfg1": {"label": "retain properties", "value": 17}},
        "extensions": {"cfg1": "retain extension"}, "record_layout": {"cfg1": "retain layout"}}
    assert baseline["owner"]["overlays"] == [{"kind": "point", "points": [[2, 0]], "cfg1": "retain overlay"}]
    assert baseline["owner"]["rois"] == [{"name": "cfg1 retained ROI", "selection": {
        "kind": "rectangle", "points": [[1, 0], [2, 1]], "width": 1},
        "t": None, "c": None, "z": None, "attributes": {"cfg1": "retain ROI"}}]
    assert baseline["owner"]["calibration"] == {"spacing": [1, 1, 1], "origin": [0, 0, 0],
        "direction": [1, 0, 0, 0, 1, 0, 0, 0, 1], "unit": "px", "frame_interval": 1,
        "time_origin": 0, "time_unit": "s"}

    if case_id == "wand-consume":
        expected = {
            "before": (0, 4), "default_selected": (0, 4),
            "eight_applied": (0, 8), "eight_selected": (0, 8),
            "tolerant_applied": (1, 8), "tolerant_selected": (1, 8),
            "four_selected": (1, 4),
        }
        for name, values in expected.items():
            assert pair(states[name]) == values
        required_events(row, states["before"]["frame"], states["default_selected"]["frame"], WAND)
        for before, after, tolerance, connectivity in (
                ("default_selected", "eight_applied", "0", "8"),
                ("eight_selected", "tolerant_applied", "1", "8"),
                ("tolerant_selected", "four_selected", "1", "4")):
            sequence = OPEN_PREFERENCES + [text("field.wand_tolerance", tolerance),
                text("field.wand_connectivity", connectivity), click("preferences.apply"),
                click("preferences.close_button")]
            required_events(row, states[before]["frame"], states[after]["frame"],
                            sequence + (WAND if after == "four_selected" else []))
            persisted_settings(states[after])
        for before, after in (("eight_applied", "eight_selected"),
                              ("tolerant_applied", "tolerant_selected")):
            required_events(row, states[before]["frame"], states[after]["frame"], WAND)
        previous = "00000000ffff00ffff"  # Initial rectangle, not the old line.
        for name in ("default_selected", "eight_selected", "tolerant_selected", "four_selected"):
            owner = states[name]["owner"]
            current = MASKS[expected[name]]
            assert observation_mask(owner["selection_observation"]) == current
            assert observation_mask(owner["previous_observation"],
                                    packed=owner["previous"]["kind"] == "mask") == previous
            previous = current
        for name, prior in (("eight_applied", "default_selected"),
                            ("tolerant_applied", "eight_selected")):
            assert states[name]["owner"] == states[prior]["owner"]
    elif case_id == "wand-drafts":
        for name in ("opened", "invalid_draft", "refused", "corrected", "reverted", "closed_reopened"):
            assert pair(states[name]) == (0, 4)
        assert field(states["invalid_draft"], "wand_tolerance")["draft"] == 1
        assert field(states["invalid_draft"], "wand_connectivity")["draft"] == 6
        assert states["refused"]["observations"]["preferences_panel"]["error"]
        for name, values in (("corrected", (1, 8)), ("reverted", (0, 4)), ("closed_reopened", (0, 4))):
            assert (field(states[name], "wand_tolerance")["draft"],
                    field(states[name], "wand_connectivity")["draft"]) == values
        for name in ("invalid_draft", "refused", "corrected", "reverted", "closed_reopened"):
            assert states[name]["settings"] == states["opened"]["settings"]
            assert states[name]["preferences_file"] == states["opened"]["preferences_file"]
        assert pair(states["accepted"]) == (1, 8)
        assert states["accepted"]["observations"]["preferences_panel"]["error"] == ""
        persisted_settings(states["accepted"])
        assert pair(states["defaults_draft"]) == (1, 8)
        assert field(states["defaults_draft"], "wand_tolerance")["draft"] == 0
        assert field(states["defaults_draft"], "wand_connectivity")["draft"] == 4
        assert pair(states["defaults_applied"]) == (0, 4)
        persisted_settings(states["defaults_applied"])
        for name, state in states.items():
            assert state["owner"] == baseline["owner"], name
        assert {"negative_refused", "negative_corrected"} <= set(row["milestones"])
        assert "wand_connectivity" in states["refused"]["observations"]["preferences_panel"]["error"]
        for name, value in (("negative_refused", -1), ("negative_corrected", 1)):
            milestone = row["milestones"][name]
            assert milestone["settings"] == baseline["settings"]
            assert milestone["preferences_file"] == baseline["preferences_file"]
            assert milestone["owner"]["revisions"] == baseline["owner"]["revisions"]
            assert milestone["owner"]["data_identity"] == baseline["owner"]["data_identity"]
            assert milestone["workflow"] == baseline["workflow"]
            assert milestone["preferences_panel"]["fields"]["wand_tolerance"]["draft"] == value
            assert milestone["preferences_panel"]["fields"]["wand_connectivity"]["draft"] == 8
        assert "wand_tolerance" in row["milestones"]["negative_refused"]["preferences_panel"]["error"]
        frames = {name: state["frame"] for name, state in states.items()}
        frames.update({name: value["frame"] for name, value in row["milestones"].items()})
        for before, after, sequence in (
            ("opened", "invalid_draft", [text("field.wand_tolerance", "1"), text("field.wand_connectivity", "6")]),
            ("invalid_draft", "refused", [click("preferences.apply")]),
            ("refused", "corrected", [text("field.wand_connectivity", "8")]),
            ("corrected", "reverted", [click("preferences.revert")]),
            ("reverted", "closed_reopened", [text("field.wand_tolerance", "1"), text("field.wand_connectivity", "8"),
                 click("preferences.close_button"), *OPEN_PREFERENCES]),
            ("closed_reopened", "negative_refused", [text("field.wand_tolerance", "-1"),
                 text("field.wand_connectivity", "8"), click("preferences.apply")]),
            ("negative_refused", "negative_corrected", [text("field.wand_tolerance", "1")]),
            ("negative_corrected", "accepted", [click("preferences.apply")]),
            ("accepted", "defaults_draft", [click("preferences.defaults")]),
            ("defaults_draft", "defaults_applied", [click("preferences.apply"), click("preferences.close_button")]),
        ):
            required_events(row, frames[before], frames[after], sequence)
        assert not any(e["kind"] == "control_click" and e["control"] == "preferences.apply"
                       for e in row["events"] if frames["reverted"] <= e["frame_before"]
                       and e["frame_after"] <= frames["closed_reopened"])
    elif case_id == "wand-persist":
        assert pair(states["draft"]) == (0, 4)
        for name in ("saved", "closed"):
            assert pair(states[name]) == (1, 8)
            payload = preferences_file(states[name]["preferences_file"])
            opaque(payload)
            assert (payload["values"]["wand_tolerance"], payload["values"]["wand_connectivity"]) == (1, 8)
        assert states["saved"]["preferences_file"] == states["closed"]["preferences_file"]
        for state in states.values():
            assert state["owner"] == baseline["owner"]
        required_events(row, 0, states["draft"]["frame"], OPEN_PREFERENCES + [
            text("field.wand_tolerance", "1"), text("field.wand_connectivity", "8")])
        required_events(row, states["draft"]["frame"], states["saved"]["frame"], [click("preferences.apply")])
        required_events(row, states["saved"]["frame"], states["closed"]["frame"], [click("preferences.close_button")])
        persisted_settings(states["saved"])
    else:
        persisted = load_case("wand-persist", context, python_report_factory)
        assert row["fixture"]["runtime"]["process_id"] != persisted["fixture"]["runtime"]["process_id"]
        file = persisted["states"]["closed"]["preferences_file"]
        for state in states.values():
            assert pair(state) == (1, 8)
            assert state["preferences_file"] == file
            opaque(preferences_file(state["preferences_file"]))
        assert observation_mask(states["selected"]["owner"]["selection_observation"]) == MASKS[1, 8]
        assert field(states["shown"], "wand_tolerance")["draft"] == 1
        assert field(states["shown"], "wand_connectivity")["draft"] == 8
        assert states["shown"]["owner"] == states["selected"]["owner"]
        required_events(row, states["loaded"]["frame"], states["selected"]["frame"], WAND)
        required_events(row, states["selected"]["frame"], states["shown"]["frame"], OPEN_PREFERENCES)
        required_events(row, states["shown"]["frame"], len(row["frames"]) - 1, [click("preferences.close_button")])
