"""Independent assertions for bounded authoring input/resource edge captures."""
import json
import os
from pathlib import Path

import pytest

from path_authoring_observations import decode_path_authoring_observations

CARRIERS = ("active", "scientific-active", "saved", "xy", "xz", "yz")
CASES = [(case, carrier)
         for case in ("input-ownership", "pointer-recovery-dpi", "numeric-stale", "budget", "extent-rebind")
         for carrier in (CARRIERS[:2] if case == "extent-rebind" else
                         CARRIERS + ("saved-linked",) if case == "budget" else CARRIERS)]
ORIGINAL = [0,2,2,1,6,2,1,6,6,1,2,6,4,0,3,3,2,4,2,5,3,3,6,3,6,5,5,5]


def affine(commands, sx, tx, sy, ty):
    output = list(commands)
    index = 0
    while index < len(output):
        operands = {0: 2, 1: 2, 2: 4, 3: 6, 4: 0}[output[index]]
        for offset in range(index + 1, index + 1 + operands, 2):
            output[offset] = sx * output[offset] + tx
            output[offset + 1] = sy * output[offset + 1] + ty
        index += 1 + operands
    return output


@pytest.mark.parametrize("case_id,carrier", CASES)
@pytest.mark.parametrize("context", ["imgui_cpp", "imgui_python"])
def test_actual_authoring_edge_contract(case_id, carrier, context, tmp_path):
    if context == "imgui_cpp":
        paths = json.loads(os.environ["IMAGEJXX_PATH_AUTHORING_EDGE_CPP_REPORTS"])
        report_path = Path(paths[case_id + "/" + carrier])
        assert report_path.stat().st_size < 64 * 1024 * 1024
        row = json.loads(report_path.read_bytes())
    else:
        from imagejxx import _desktop
        editor = _desktop.Editor(preferences_path=str(tmp_path / "preferences.json"))
        row = editor._path_authoring_edge_ui_case(case_id, carrier, str(tmp_path / "capture"))
        if isinstance(row, str):
            row = json.loads(row)
    row = decode_path_authoring_observations(row)
    assert row["contract"] == "p13h.path-authoring.edge-ui.001"
    assert (row["case_id"], row["carrier"], row["context"]) == (case_id, carrier, context)
    assert row["state"] == "observed" and row["error"] == ""
    assert row["acceptance_claim"] is False
    assert row["frames"] and row["controls"]
    assert all(frame["events"] == frame["rendered"]["input_frame"]["raw_inputs"] for frame in row["frames"])
    assert len(row["frames"]) <= 800
    assert isinstance(row["fixture_actions"], list) and isinstance(row["faults"], list)
    assert all(isinstance(action["frame"], int) for action in row["fixture_actions"])
    planar = carrier in {"xy", "xz", "yz"}
    saved = carrier in {"saved", "saved-linked"}
    index = CARRIERS.index(carrier) - 3 if planar else 0
    assert {key: row["route"][key] for key in ("carrier", "row", "plane")} == {
        "carrier": "planar" if planar else "saved" if saved else "active",
        "row": index if planar else 0 if saved else None, "plane": index}
    expected_view = "linked_xy" if carrier == "saved-linked" else "planar" if planar else "ordinary_xy"
    if "view" in row["route"] or carrier == "saved-linked":
        assert row["route"]["view"] == expected_view

    def geometry(state):
        return (state["rows"][index]["selection"] if planar else
                state["rois"][0]["selection"] if saved else state["selection"])

    states = row["states"]
    before = states["before"]
    assert before["display"]["orthogonal"] is (planar or carrier == "saved-linked")
    assert geometry(before)["path"] == ORIGINAL
    assert before["calibration"]["spacing"] == [.5, 1.25, 2]
    assert before["calibration"]["origin"] == [3, -4, 5]
    assert before["calibration"]["direction"] == [0, -1, 0, 1, 0, 0, 0, 0, 1]
    assert before["shape"] == ([1, 1, 8, 12, 16, 1] if carrier == "active" else [2, 2, 8, 12, 16, 1])
    expected = [{"position": [t, c, z], "samples": [10000*t+1000*c+100*z+10*y+x
                                                   for y in range(12) for x in range(16)]}
                for t in range(before["shape"][0]) for c in range(before["shape"][1]) for z in range(8)]
    assert before["pixels"] == expected
    if case_id != "budget":
        assert before["metadata_all"]["properties"]["p13h_opaque"] == {
            "tag": "keep document properties", "integer": 17, "flag": True,
            "values": ["alpha", 2.5, None]}
        assert before["metadata_all"]["extensions"]["p13h_opaque"] == {
            "tag": "keep metadata extension", "value": 23}
        assert before["metadata_all"]["record_layout"]["p13h_opaque"] == {
            "tag": "keep record layout", "present": True}
        assert before["overlays"] == [{"p13h_opaque": {"tag": "keep overlay", "value": 29}}]
        assert before["display"]["properties"]["display_range"] == [10.0, 60000.0]
    assert before["pixel_type"] == 1 and before["sample_policy"] == 0
    observations = row["observations"]

    def retained(state, base=before, label=None):
        assert state["pixels"] == base["pixels"]
        assert state["calibration"] == base["calibration"]
        assert state["shape"] == base["shape"]
        for key in ["overlays", "spectrum", "pixel_type", "sample_policy"]:
            assert state[key] == base[key], key
        for key in ["properties", "record_layout"]:
            assert state["metadata_all"][key] == base["metadata_all"][key], key
        actual_extensions = {key: value for key, value in state["metadata_all"]["extensions"].items()
                             if key != "imagejxx.planar_annotations"}
        base_extensions = {key: value for key, value in base["metadata_all"]["extensions"].items()
                           if key != "imagejxx.planar_annotations"}
        assert actual_extensions == base_extensions
        if case_id == "numeric-stale":
            expected_title = ("Other owner" if label == "owner_switched" else
                              base["metadata_all"]["title"] if label in {"before", "revision_invalid"}
                              else "External metadata revision")
            assert state["metadata_all"]["title"] == expected_title, label
            if label == "owner_switched":
                assert state["document_id"] != before["document_id"]
            else:
                assert state["document_id"] == before["document_id"], label
        else:
            assert state["metadata_all"]["title"] == base["metadata_all"]["title"]
        assert state["display"]["properties"].get("display_range") == base["display"]["properties"].get("display_range")
        if saved:
            assert state["selection"] == base["selection"]
            assert state["previous"] == base["previous"]
            assert state["rows"] == base["rows"]
            assert state["rois"][1:] == base["rois"][1:]
        elif planar:
            assert state["selection"] == base["selection"]
            assert state["previous"] == base["previous"]
            assert state["rois"] == base["rois"]
            assert [v for n, v in enumerate(state["rows"]) if n != index] == [
                v for n, v in enumerate(base["rows"]) if n != index]
        else:
            assert state["rows"] == base["rows"] and state["rois"] == base["rois"]

    for name, state in states.items():
        retained(state, states["extent_destination_before"]
                 if case_id == "extent-rebind" and name != "before" else before, label=name)
    if case_id == "pointer-recovery-dpi":
        for name in ["points_pressed", "points_canceled", "bounds_pressed", "valid_preview",
                     "collapsed_preview", "crossing_preview"]:
            assert geometry(states[name]) == geometry(before), name
            assert states[name]["undo"] == before["undo"], name
            assert states[name]["pixels"] == expected, name
        reflected = states["reflected"]
        assert geometry(reflected)["path"] == affine(ORIGINAL, -1, 4, 1.5, -1)
        assert geometry(reflected)["width"] == geometry(before)["width"]
        assert geometry(reflected)["attributes"] == geometry(before)["attributes"]
        assert reflected["undo"] == before["undo"] + 1
        assert geometry(states["undone"]) == geometry(before)
        request = row["requests"]["pointer"]
        assert request == {"handle": 4, "aspect": False, "center": False,
                           "press_image": [6, 6], "press_device_offset": [8, 0],
                           "valid_target": [8, 8], "collapsed_target": [2, 8], "crossing_target": [-2, 8],
                           "gui_scale": 1.5, "hit_tolerance_device": 9,
                           "handle_radius_device": 6 if planar else 4.5,
                           "handle_shape": "circle" if planar else "square"}
        controls = observations["bounds_ready"]["planar"] if planar else observations["bounds_ready"]["canvas"]
        handles = controls["handles"] if planar else controls[
            "saved_roi_geometry_handles" if carrier == "saved" else "selection_handles"]
        handle = next(value for value in handles if value["index"] == 4 and value["kind"] == 1)
        assert handle["gui_scale"] == 1.5 and handle["bounds_hit_tolerance_device"] == 9
        assert handle["radius_device"] == (6 if planar else 4.5)
        assert handle["shape"] == ("circle" if planar else "square")
        for name in ["identity_preview", "identity_returned", "identity_released"]:
            assert geometry(states[name]) == geometry(states["identity_before"]), name
            assert states[name]["undo"] == states["identity_before"]["undo"], name
            assert states[name]["redo"] == states["identity_before"]["redo"], name
    elif case_id == "input-ownership":
        for name, state in states.items():
            if name != "resize_recovered":
                assert geometry(state) == geometry(before), name
                assert state["undo"] == before["undo"], name
        assert len(row["faults"]) == 2
        for fault in row["faults"]:
            assert fault["raw_event_count"] == 0
            assert fault["io_before"]["down"] is True
            assert fault["io_before"]["focus_lost"] is False
            assert fault["io_after"]["down"] is False
            assert fault["io_after"]["released"] is False
            assert fault["io_after"]["focus_lost"] is False
            assert row["frames"][fault["frame"]]["events"] == []
        assert observations["pen_pressed"]["author"]["press_pending"] is True
        for name in ["pen_lost_release", "pen_late_release", "pen_canceled"]:
            assert observations[name]["author"]["active"] is False, name
            assert observations[name]["author"]["press_pending"] is False, name
        assert observations["pen_recovered"]["author"]["selection"]["path"] == [0, 2, 2, 1, 4, 2]
        assert observations["text_before"]["preferences"]["search"] == "abc"
        assert observations["text_backspace"]["preferences"]["search"] == "ab"
        for name in ["text_before", "text_backspace", "text_enter", "text_escape"]:
            assert observations[name]["preferences"]["open"] is True
        assert geometry(states["resize_recovered"])["path"] == affine(ORIGINAL, 1.5, -1, 1.5, -1)
        assert states["resize_recovered"]["undo"] == before["undo"] + 1
        assert geometry(states["resize_recovered_undo"]) == geometry(before)
    elif case_id == "numeric-stale":
        def resize(label):
            observation = observations[label]
            return observation["planar"]["resize"] if planar else observation["properties"]["resize"]

        for label, state in states.items():
            if label != "fresh_applied":
                assert geometry(state) == geometry(before), label
            if label.endswith("_invalid"):
                assert resize(label)["invalid"] is True, label
                assert resize(label)["error"], label
        reset_labels = ["revision_invalidated", "revision_still_clear", "owner_switched", "owner_returned",
                        "plane_invalidated", "plane_returned", "canceled"]
        if carrier != "active":
            reset_labels += ["channel_invalidated", "channel_returned", "time_invalidated", "time_returned"]
        if planar or carrier == "saved":
            reset_labels.append("row_changed")
        for label in reset_labels:
            assert resize(label)["invalid"] is False, label
            assert resize(label)["staged"] is False and resize(label)["unstaged"] is False, label
            if planar:
                # An open form may seed a new clean source in the same frame
                # after dropping its stale draft. A retained source must bind
                # the new live revisions/context, never the canceled capture.
                source = observations[label]["planar"]["resize_source"]
                if source is not None:
                    assert source["revisions"] == states[label]["revisions"], label
                    assert source["crosshair"] == states[label]["display"]["crosshair"], label
                    assert source["t"] == states[label]["display"]["t"], label
                    assert source["c"] == states[label]["display"]["c"], label
                prior = ("revision" if label.startswith("revision") else
                         "owner" if label.startswith("owner") else
                         "plane" if label.startswith("plane") else
                         "row" if label == "row_changed" else
                         "cancel" if label == "canceled" else label.split("_")[0])
                assert resize(label)["lifecycle"] != resize(prior + "_invalid")["lifecycle"], label
        assert states["revision_invalidated"]["revisions"]["metadata"] == states["revision_invalid"]["revisions"]["metadata"] + 1
        assert states["revision_invalidated"]["metadata_all"]["title"] == "External metadata revision"
        assert states["revision_invalid"]["metadata_all"]["title"] == before["metadata_all"]["title"]
        assert states["plane_invalidated"]["display"]["crosshair"] != states["plane_invalid"]["display"]["crosshair"]
        for axis in ("channel", "time") if carrier != "active" else ():
            key = "c" if axis == "channel" else "t"
            assert states[axis + "_invalid"]["display"][key] == 0
            assert states[axis + "_invalidated"]["display"][key] == 1
            assert states[axis + "_returned"]["display"][key] == 0
        assert geometry(states["fresh_applied"])["path"] == affine(ORIGINAL, 1.5, -1, 1.5, -1)
        assert states["fresh_applied"]["undo"] == states["fresh_preview"]["undo"] + 1
    elif case_id == "budget":
        if saved:
            admission = row["saved_entry_admission"]
            source = admission["source"]
            initial = states["large_before"]
            assert admission["configured_memory_mb"] == 64
            assert admission["estimate"]["state"] == admission["resolve"]["state"] == "returned"
            assert admission["estimate"]["value"] == admission["resolve"]["estimate"]
            assert admission["estimate"]["value"]["working_bytes"] <= 64 * 1024 * 1024
            assert all(admission[key] is True for key in ["same_document", "shares_data", "same_data_revisions", "same_display"])
            assert admission["ui_error"] == "" and admission["ui_saved_edit"]["active"] is True
            assert source["metadata"] == initial["metadata_all"]
            for key in ["overlays", "spectrum", "rois", "selection", "previous", "document_id", "data_identity"]:
                assert source[key] == initial[key], key
            assert source["dataset"]["pixels_state"] == "large_before"
            assert source["dataset"]["shape"] == initial["shape"]
            assert source["dataset"]["calibration"] == initial["calibration"]
            assert source["dataset"]["pixel_type"] == initial["pixel_type"]
            assert source["dataset"]["sample_policy"] == initial["sample_policy"]
            assert admission["resolve"]["callbacks"]["checkpoint_count"] > 0
        large = list(ORIGINAL)
        corners = [(2, 2), (6, 2), (6, 6), (2, 6)]
        for n in range(8192):
            large.extend([1, *corners[n % 4]])
        assert row["requests"]["large_path"] == {
            "prefix": ORIGINAL, "appended_line_count": 8192, "cycle": [list(p) for p in corners],
            "command_scalar_count": len(large), "segment_count": 8200}
        assert row["requests"]["budget"] == {"high_mb": 64, "low_mb": 1,
                                              "numeric_target": [8, 8], "pointer_target": [8, 8],
                                              "handle": 4, "aspect": False, "center": False}
        for name in ["large_before", "high_numeric_preview", "low_numeric_refused", "numeric_recovered_preview",
                     "numeric_undone", "pointer_held", "pointer_low_released", "pointer_undone"]:
            assert geometry(states[name])["path"] == large, name
            assert states[name]["undo"] == states["large_before"]["undo"], name
        transformed = affine(large, 1.5, -1, 1.5, -1)
        for name in ["numeric_recovered", "pointer_recovered", "pointer_redone"]:
            assert geometry(states[name])["path"] == transformed, name
            assert geometry(states[name])["attributes"] == geometry(states["large_before"])["attributes"], name
            assert geometry(states[name])["width"] == geometry(states["large_before"])["width"], name
            assert states[name]["undo"] == states["large_before"]["undo"] + 1, name
        for name in ["low_numeric_refused", "pointer_low_released"]:
            observation = observations[name]
            errors = [observation["error"]]
            canvas = observation["canvas"]
            errors += [canvas.get(key, "") for key in ("selection_edit_error", "selection_edit_notice",
                                                        "saved_roi_handle_error", "saved_roi_handle_notice")]
            errors += [canvas.get("saved_roi_geometry_input", {}).get(key, "")
                       for key in ("handle_error", "handle_notice")]
            if planar:
                errors += [observation["planar"].get("error", ""),
                           observation["planar"]["resize"].get("error", "")]
            else:
                errors.append(observation["properties"]["resize"].get("error", ""))
            assert any(errors) or observation["modal"]["open"], name
        for name, limit in [("large_before", 64), ("high_numeric_preview", 64),
                            ("low_numeric_refused", 1), ("numeric_recovered_preview", 64),
                            ("pointer_held", 64), ("pointer_low_released", 1), ("pointer_recovered", 64)]:
            assert observations[name]["settings"]["processing_memory_mb"] == limit, name
        recovered = observations["numeric_recovered_preview"]
        assert recovered["error"] == "" and recovered["modal"]["open"] is False
        assert (recovered["planar"]["error"] if planar else recovered["properties"]["resize"]["error"]) == ""
        controls = (observations["low_numeric_refused"]["planar"]["controls"] if planar else
                    observations["low_numeric_refused"]["properties"]["control_states"])
        apply_key = "path_apply" if planar else "Apply"
        if apply_key in controls:
            if planar:
                assert controls[apply_key]["enabled"] is False
            else:
                assert controls[apply_key]["disabled"] is True
        assert any(action["kind"].startswith("native deterministic large-path fixture") for action in row["fixture_actions"])
        assert any(action["kind"].startswith("live budget API resync") and
                   action["request"] == {"processing_memory_mb": 1} for action in row["fixture_actions"])
        selectors = [item["selector"] for item in row["controls"]]
        assert selectors.count("preferences.apply") >= 3
        assert selectors.count("preferences.memory") >= 3
    elif case_id == "extent-rebind":
        request = row["requests"]["extent"]
        destination = states["extent_destination_before"]
        assert request["captured"] == before["shape"]
        assert request["destination"] == [*before["shape"][:4], 20, 1] == destination["shape"]
        assert destination["pixels"] == [
            {"position": [t, c, z], "samples": [10000*t+1000*c+100*z+10*y+x
                                                 for y in range(12) for x in range(20)]}
            for t in range(before["shape"][0]) for c in range(before["shape"][1]) for z in range(8)]
        assert request["path"] == ORIGINAL and request["width"] == 2
        original = request["original"]
        assert original["version"] == 4 and original["source"]["shape"] == before["shape"]
        assert original["candidate"]["path"] == ORIGINAL
        assert destination["selection"]["kind"] == "rectangle"
        assert destination["selection"]["points"] == [[8, 7], [10, 9]]
        assert states["extent_discarded"]["workflow"] == [original]
        for name in ["extent_parameters_original", "extent_discarded", "extent_unconfirmed",
                     "extent_confirmed", "extent_kept", "extent_portable", "extent_preview", "extent_undone"]:
            assert states[name]["selection"] == destination["selection"], name
            assert states[name]["previous"] == destination["previous"], name
            assert states[name]["undo"] == destination["undo"], name
        editor = observations["extent_parameters_original"]["workflow"]["editor"]
        assert editor["creation_reference_extent"] == before["shape"]
        assert editor["creation_rebind_extent"] is False
        unconfirmed = observations["extent_unconfirmed"]["workflow"]["editor"]
        assert unconfirmed["visible"] is True and unconfirmed["error"]
        assert unconfirmed["creation_rebind_extent"] is False
        assert observations["extent_confirmed"]["workflow"]["editor"]["creation_rebind_extent"] is True
        program = states["extent_portable"]["workflow"][0]
        assert program["version"] == 8 and len(program["steps"]) == 1
        step = program["steps"][0]
        assert step["original"] == original
        assert step["selection_creation_extent"] == destination["shape"]
        assert step["selection_creation"]["geometry"] == "native_path_v4"
        assert step["selection_creation"]["candidate"]["path"] == ORIGINAL
        assert step["selection_creation"]["candidate"]["width"] == 2
        for name in ["extent_applied", "extent_redone"]:
            assert states[name]["selection"]["path"] == ORIGINAL, name
            assert states[name]["selection"]["width"] == 2, name
            assert states[name]["selection"]["attributes"] == geometry(before)["attributes"], name
            assert states[name]["previous"] == destination["selection"], name
            assert states[name]["undo"] == destination["undo"] + 1, name
        selectors = [item["selector"] for item in row["controls"]]
        for selector in ["parameters.discard", "parameters.confirm_creation_extent", "parameters.keep_parameters",
                         "program.prepare_program", "program.keep_program", "program.prepare", "program.apply"]:
            assert selector in selectors
