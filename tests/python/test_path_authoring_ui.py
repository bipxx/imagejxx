"""Independent literal geometry/state assertions for real ImGui input.

These observations cover the first vertical authoring scenarios. Remaining
workflow/platform/resource scenarios stay explicit in the P13h card.
"""
import json
import os
from pathlib import Path

import pytest

from path_authoring_observations import decode_path_authoring_observations

MODES = ["active", "scientific-active", "saved", "xy", "xz", "yz"]
DRAWN = [0,1,1,1,5,1,2,6,2,5,4,3,4,5,2,5,1,4,4,
         0,2,2,1,3,2,1,3,3,1,2,3,4,0,-.5,5,1,.5,5]


def affine(commands, sx, tx, sy, ty):
    """Independent command walker and affine equation, no native geometry calls."""
    values = list(commands)
    index = 0
    sizes = {0: 2, 1: 2, 2: 4, 3: 6, 4: 0}
    while index < len(values):
        count = sizes[values[index]]
        for operand in range(index + 1, index + count + 1, 2):
            values[operand] = sx * values[operand] + tx
            values[operand + 1] = sy * values[operand + 1] + ty
        index += count + 1
    return values


def same_document_content(actual, expected):
    # The original observation compared T/C/crosshair/orthogonal display state.
    # Expanded diagnostics also expose camera/revision/identity changes; these
    # are not content equality (e.g. restoring geometry creates a fresh backing).
    # Preserve every original field comparison and all added retained metadata.
    def content(state):
        values = {key: value for key, value in state.items()
                  if key not in {"display", "document_id", "data_identity"}}
        values["display"] = {key: state["display"][key]
                             for key in ["t", "c", "crosshair", "orthogonal", "properties"]}
        return values
    return content(actual) == content(expected)


@pytest.mark.parametrize("mode", MODES)
@pytest.mark.parametrize("context", ["imgui_cpp", "imgui_python"])
def test_actual_path_drawing_resize_history_and_reopen(mode, context, tmp_path):
    if context == "imgui_cpp":
        path = json.loads(os.environ["IMAGEJXX_PATH_AUTHORING_CPP_REPORTS"])[mode]
        assert Path(path).stat().st_size < 128 * 1024 * 1024
        row = json.loads(Path(path).read_bytes())
    else:
        from imagejxx import _desktop
        editor = _desktop.Editor(preferences_path=str(tmp_path / "preferences.json"))
        row = editor._path_authoring_ui_case(mode, str(tmp_path / "capture"))
        if isinstance(row, str):
            row = json.loads(row)
    row = decode_path_authoring_observations(row, max_wire_bytes=128 * 1024 * 1024)
    assert row["contract"] == "p13h.path-authoring.ui.001"
    assert row["mode"] == mode and row["context"] == context
    assert row["state"] == "observed" and row["error"] == ""
    assert row["acceptance_claim"] is False
    planar = mode in ["xy", "xz", "yz"]
    saved = mode == "saved"
    plane = ["xy", "xz", "yz"].index(mode) if planar else 0
    expected_route = {"carrier": "planar" if planar else "saved" if saved else "active",
                      "row": 3 if planar else 1 if saved else None, "plane": plane}
    assert row["route"] == expected_route
    states = row["states"]
    before, stored = states["before"], states["stored"]

    def geometry(state):
        return state["rows"][3]["selection"] if planar else state["rois"][1]["selection"] if saved else state["selection"]

    for label in ["cancel_preview", "canceled", "focus_canceled", "outside_canceled", "draw_preview"]:
        assert same_document_content(states[label], before), label
        assert states[label]["document_id"] == before["document_id"], label
        assert states[label]["data_identity"] == before["data_identity"], label
    drawn = states["drawn"]["rows"][3]["selection"] if planar else states["drawn"]["selection"]
    assert drawn["path"] == DRAWN
    assert geometry(stored)["path"] == DRAWN
    assert states["drawn"]["undo"] == before["undo"] + 1
    for label in ["invalid_preview", "invalid_released", "transform_preview"]:
        assert same_document_content(states[label], stored), label
    assert row["pointer_request"] == {"handle": 4, "target": [7.5, 7], "aspect": True, "center": True}
    transformed = affine(DRAWN, 2, -2.5, 2, -3)
    assert geometry(states["applied"])["path"] == transformed
    assert geometry(states["applied"])["attributes"] == geometry(stored)["attributes"]
    assert geometry(states["applied"])["width"] == geometry(stored)["width"]
    assert states["applied"]["undo"] == stored["undo"] + 1
    assert geometry(states["undone"]) == geometry(stored)
    assert geometry(states["redone"]) == geometry(states["applied"])
    for label in ["numeric_invalid", "numeric_preview"]:
        assert same_document_content(states[label], states["redone"]), label
    assert row["observations"]["invalid_apply"]["enabled"] is False
    assert row["numeric_request"] == {"handle": 4, "target": [14.5, 11], "aspect": False, "center": False}
    numeric = affine(DRAWN, 3, -2, 3, -4)
    assert geometry(states["numeric_applied"])["path"] == numeric
    assert geometry(states["reopened"]) == geometry(states["numeric_applied"])
    assert states["numeric_applied"]["undo"] == states["redone"]["undo"] + 1
    if saved:
        for label in ["invalid_preview", "invalid_released", "transform_preview", "applied",
                      "undone", "redone", "numeric_invalid", "numeric_preview", "numeric_applied", "reopened"]:
            assert states[label]["selection"] == stored["selection"], label
            assert states[label]["previous"] == stored["previous"], label
    elif not planar:
        assert states["drawn"]["previous"] == before["selection"]
        assert states["applied"]["previous"] == stored["selection"]
        assert states["undone"]["previous"] == stored["previous"]
        assert states["redone"]["previous"] == states["applied"]["previous"]
        assert states["numeric_applied"]["previous"] == states["applied"]["selection"]
        assert states["reopened"]["previous"] == states["numeric_applied"]["previous"]

    for label, state in states.items():
        assert state["calibration"] == before["calibration"], label
        assert state["shape"] == before["shape"], label
        assert state["pixels"] == before["pixels"], label
        for key in ["overlays", "spectrum", "pixel_type", "sample_policy"]:
            assert state[key] == before[key], (label, key)
        for key in ["title", "properties"]:
            assert state["metadata_all"][key] == before["metadata_all"][key], (label, key)
        layout = state["metadata_all"]["record_layout"]
        original_layout = before["metadata_all"]["record_layout"]
        if saved:
            # Store ROI legitimately derives serialization-presence flags.
            # Unrelated forward layout fields must survive that adapter.
            derived = {"root_fields", "roi_slice_fields", "calibration_fields", "null_fields"}
            assert {key: value for key, value in layout.items() if key not in derived} == original_layout, label
            if label not in {"before", "cancel_preview", "canceled", "focus_canceled", "outside_canceled", "draw_preview", "drawn"}:
                assert {key: layout[key] for key in derived} == {
                    "root_fields": ["rois"], "roi_slice_fields": ["1"],
                    "calibration_fields": [], "null_fields": []}, label
        else:
            assert layout == original_layout, label
        extensions = {key: value for key, value in state["metadata_all"]["extensions"].items()
                      if key != "imagejxx.planar_annotations"}
        original_extensions = {key: value for key, value in before["metadata_all"]["extensions"].items()
                               if key != "imagejxx.planar_annotations"}
        assert extensions == original_extensions, label
        assert state["display"]["properties"]["display_range"] == [10.0, 60000.0], label
        assert state["rois"][0] == before["rois"][0], label
        assert state["rows"][:3] == before["rows"], label
        if planar:
            assert state["selection"] == before["selection"] and state["previous"] == before["previous"], label
            assert state["rois"] == before["rois"], label
        else:
            assert state["rows"] == before["rows"], label
        metadata = {key: value for key, value in state["metadata"].items() if key != "imagejxx.planar_annotations"}
        original_metadata = {key: value for key, value in before["metadata"].items() if key != "imagejxx.planar_annotations"}
        assert metadata == original_metadata, label
    assert before["metadata_all"]["properties"]["p13h_opaque"] == {
        "tag": "keep document properties", "integer": 17, "flag": True,
        "values": ["alpha", 2.5, None]}
    assert before["metadata_all"]["extensions"]["p13h_opaque"] == {
        "tag": "keep metadata extension", "value": 23}
    assert before["metadata_all"]["record_layout"]["p13h_opaque"] == {
        "tag": "keep record layout", "present": True}
    assert before["overlays"] == [{"p13h_opaque": {"tag": "keep overlay", "value": 29}}]
    assert before["pixel_type"] == 1 and before["sample_policy"] == 0
    assert before["calibration"]["spacing"] == [.5, 1.25, 2]
    assert before["calibration"]["origin"] == [3, -4, 5]
    times = channels = 2 if mode == "scientific-active" else 1
    assert before["shape"] == [times, channels, 8, 12, 16, 1]
    assert before["pixels"] == [
        {"position": [t, c, z], "samples": [10000*t + 1000*c + 100*z + 10*y + x
                                             for y in range(12) for x in range(16)]}
        for t in range(times) for c in range(channels) for z in range(8)]
    simple = [0, 1, 1, 1, 2, 1]
    keyboard = row["keyboard"]
    assert keyboard["complete"]["selection"]["path"] == simple
    assert keyboard["removed_complete"]["selection"]["path"] == [0, 1, 1]
    assert keyboard["armed_contour"]["new_contour"] is True
    assert keyboard["armed_contour"]["selection"]["path"] == simple
    assert keyboard["disarmed_contour"]["new_contour"] is False
    assert keyboard["disarmed_contour"]["selection"]["path"] == simple
    assert keyboard["closed"]["selection"]["path"] == simple + [4]
    assert keyboard["reopened"]["selection"]["path"] == simple
    assert keyboard["pending"]["pending"] == [[3, 2]]
    assert keyboard["pending"]["selection"]["path"] == simple
    assert keyboard["removed_pending"]["pending"] == []
    assert keyboard["removed_pending"]["selection"]["path"] == simple
    assert keyboard["canceled"]["active"] is False
    assert same_document_content(keyboard["owner_after_cancel"], before)
    enter_before, enter_after = keyboard["enter_before"], keyboard["enter_after"]
    enter_geometry = enter_after["rows"][3]["selection"] if planar else enter_after["selection"]
    assert enter_geometry["path"] == simple
    assert enter_after["undo"] == enter_before["undo"] + 1
    assert enter_after["pixels"] == enter_before["pixels"]
    assert keyboard["enter_controller"]["active"] is False
    assert row["observations"]["idle_cache"]["before"] == row["observations"]["idle_cache"]["after"]
    invalidations = row["invalidations"]
    expected_invalidations = {"owner_switched", "source", "tool", "fixed_plane", "zoom"}
    if mode == "scientific-active":
        expected_invalidations |= {"time", "channel"}
    assert set(invalidations) == expected_invalidations
    for name, evidence in invalidations.items():
        assert evidence["draft"]["selection"]["path"] == simple, name
        assert evidence["draft"]["active"] is True, name
        assert evidence["controller"]["active"] is False, name
        if name == "owner_switched":
            assert evidence["source_id"] != evidence["destination_id"]
            assert same_document_content(evidence["after"], evidence["before"])
            assert evidence["returned_controller"]["active"] is False
        elif name == "source":
            assert evidence["fixture_action"].startswith("external native Selection revision change")
            assert evidence["after"]["selection"]["kind"] == "point"
            assert same_document_content(evidence["restored"], evidence["before"])
        else:
            for key in ["selection", "previous", "rows", "rois", "pixels", "metadata", "undo"]:
                assert evidence["after"][key] == evidence["before"][key], (name, key)
            if name in {"time", "channel"}:
                axis = "t" if name == "time" else "c"
                assert evidence["before"]["display"][axis] == 0
                assert evidence["after"]["display"][axis] == 1
            if name == "fixed_plane":
                assert evidence["before"]["display"]["crosshair"] != evidence["after"]["display"]["crosshair"]
    frames = {frame["label"]: frame["rendered"] for frame in row["frames"]}
    def canvas(label):
        return frames[label]["orthogonal_planes"][plane] if planar else frames[label]["ordinary_canvas"]
    assert canvas("guard.zoom.position")["scale"] != canvas("guard.zoom.changed")["scale"]
    pending = row["observations"]["pending_groups"]
    assert [item["pending"] for item in pending] == [[[6, 2]], [[4, 5]], [[4, 5], [2, 5]]]
    assert all(item["ready"] is False for item in pending)
    assert pending[0]["selection"]["path"] == [0, 1, 1, 1, 5, 1]
    assert pending[1]["selection"]["path"] == pending[2]["selection"]["path"] == [0, 1, 1, 1, 5, 1, 2, 6, 2, 5, 4]
    assert row["observations"]["matched_press"]["press_pending"] is True
    assert row["observations"]["matched_press"]["selection"]["path"] == DRAWN[:-3]
    assert row["observations"]["matched_release"]["press_pending"] is False
    assert row["observations"]["matched_release"]["selection"]["path"] == DRAWN
    if mode in {"active", "scientific-active"}:
        workflow = row["workflow_ui"]
        portable = workflow["portable"]
        assert portable["version"] == 8 and len(portable["steps"]) == 3
        for index, step in enumerate(portable["steps"]):
            assert step["operation"] == "selection_creation"
            assert step["original"] == workflow["recorded"][index]
            assert step["original"]["version"] == 4
            if step["selection_creation"] is None:
                # Original-only V4 intent derives its reference extent from
                # the complete immutable capture; it does not author a field.
                assert step["selection_creation_extent"] is None
                assert step["original"]["source"]["shape"] == before["shape"]
            else:
                assert step["selection_creation_extent"] == before["shape"]
        assert portable["steps"][2]["selection_creation"]["candidate"]["width"] == 2
        assert portable["steps"][2]["selection_creation"]["candidate"]["path"] == numeric
        assert workflow["parameters_open"]["creation_reference_extent"] == before["shape"]
        for key in ["selection", "previous", "rows", "rois", "pixels", "metadata", "undo"]:
            assert workflow["prepared"][key] == workflow["before"][key]
        assert workflow["applied"]["selection"]["path"] == numeric
        assert workflow["applied"]["selection"]["width"] == 2
        assert workflow["applied"]["undo"] == workflow["before"]["undo"] + 1
        assert workflow["applied"]["previous"] == workflow["before"]["selection"]
        assert workflow["undone"]["selection"] == workflow["before"]["selection"]
        assert workflow["undone"]["previous"] == workflow["before"]["previous"]
        assert workflow["redone"]["selection"] == workflow["applied"]["selection"]
        assert workflow["restored"]["selection"] == workflow["before"]["selection"]
    assert row["frames"] and all(frame["events"] == frame["rendered"]["input_frame"]["raw_inputs"]
                                  for frame in row["frames"])
    def pen_controller(rendered):
        return (rendered["orthogonal_planes"][0]["planar_annotations"]["path_authoring"]
                if planar else rendered["path_authoring"])
    canceled_frame = next(frame["rendered"] for frame in row["frames"]
                          if frame["label"] == "author.Cancel path.idle")
    assert pen_controller(canceled_frame)["active"] is False
    assert pen_controller(frames["draw.finished"])["active"] is False
    author_controls = [item for item in row["controls"] if item["name"].startswith("author.")]
    author_presses = [frame for frame in row["frames"]
                      if frame["label"].startswith("author.") and frame["label"].endswith(".press")]
    assert len(author_controls) == len(author_presses) > 0
    for control, frame in zip(author_controls, author_presses):
        assert frame["label"] == control["name"] + ".press"
        assert frame["rendered"]["input_frame"]["focus"]["active_id"] == control["control"]["id"]
    delivered = [event for frame in row["frames"] for event in frame["events"]]
    assert any(event.get("type") == "focus" and event.get("focused") is False for event in delivered)
    assert any(event.get("type") == "keys" and
               {key["name"] for key in event["keys"] if key["down"]} >= {"Ctrl", "Shift"}
               for event in delivered)
    names = [item["name"] for item in row["controls"]]
    assert all(name in names for name in ["author.Line", "author.Quadratic", "author.Cubic",
                                          "author.Close contour", "author.Finish path", "author.Cancel path"])
    session = row["files"]["session"]
    assert session["member"] == "authored.ijx"
    assert len(bytes.fromhex(session["hex"])) == session["byte_count"] > 0
    # Pending scenarios are reported explicitly and grant no coverage here.
    assert isinstance(row["remaining_scenarios"], list)
