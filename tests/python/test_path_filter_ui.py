"""Independent literal sample/history checks for actual shared ImGui input."""
import gzip
import json
import os
from pathlib import Path
import struct

import pytest

ROOT = Path(__file__).resolve().parents[2]
REFERENCE = json.loads(gzip.decompress(
    (ROOT / "tests/fixtures/path-filter-targets-imagej154r.json.gz").read_bytes()))
RANK_REFERENCE = json.loads(gzip.decompress(
    (ROOT / "tests/fixtures/path-rank-targets-imagej154r.json.gz").read_bytes()))
SCIENTIFIC_REFERENCE = json.loads(gzip.decompress(
    (ROOT / "tests/fixtures/path-scientific-targets-imagej154r.json.gz").read_bytes()))
PATH = [0, 2.2, 1.2, 1, 9.2, 1.2, 1, 2.2, 8.2, 4]


def little_words(words):
    return "".join(bytes.fromhex(word[2:])[::-1].hex() for word in words)


def reference(sigma):
    return next(row["observation"] for row in REFERENCE["cases"]
                if row["id"] == f"fractional-triangle-sigma{sigma}")


def expected_words(sigma):
    observed = reference(sigma)
    words = list(observed["input"]["f32_words"])
    mask = observed["processor_mask"]
    x, y = mask["image_origin"]
    w, h = mask["dimensions"]
    values = bytes.fromhex(mask["u8_hex"])
    filtered = observed["gaussian"]["after_mask_reset_f32_words"]
    for j in range(h):
        for i in range(w):
            if values[j*w+i]:
                at = (y+j)*23+x+i
                words[at] = filtered[at]
    return little_words(words)


def plane(state):
    assert state["shape"] == [1, 1, 1, 17, 23, 1]
    assert len(state["planes"]) == 1 and state["planes"][0]["position"] == [0, 0, 0]
    value = state["planes"][0]["declared_f32_hex"]
    assert len(value) == 391 * 8
    return value


def retained(state):
    # File Open may regenerate derived layout. Opaque fields must survive, as
    # must all scientific annotations/overlays and calibration.
    metadata = state["metadata_all"]
    return {"opaque": [metadata[key]["p13i_opaque"]
                       for key in ("properties", "extensions", "record_layout")],
            **{key: state[key] for key in ("rois", "rows", "overlays", "calibration")}}


@pytest.mark.parametrize("context", ["imgui_cpp", "imgui_python"])
def test_actual_draw_gaussian_history_reopen_and_replay(context, tmp_path):
    if context == "imgui_cpp":
        reports = json.loads(os.environ["IMAGEJXX_PATH_FILTER_CPP_REPORTS"])
        path = Path(reports["draw-gaussian"])
        assert path.stat().st_size < 16 * 1024 * 1024
        row = json.loads(path.read_bytes())
    else:
        from imagejxx import _desktop
        editor = _desktop.Editor(preferences_path=str(tmp_path / "preferences.json"))
        row = editor._path_filter_ui_case("draw-gaussian", str(tmp_path / "capture"))
        if isinstance(row, str):
            row = json.loads(row)
    assert row["contract"] == "p13i.path-filter.ui.001"
    assert row["state"] == "observed" and row["error"] == ""
    assert row["context"] == context and row["case_id"] == "draw-gaussian"
    assert row["acceptance_claim"] is False
    states, observations = row["states"], row["observations"]
    required = {"before", "source_saved", "drawn", "preview_sigma1", "preview_sigma12",
                "applied", "undone", "redone", "filtered_saved", "workflow_saved",
                "filtered_reopened", "source_reopened", "workflow_loaded",
                "replay_prepared", "replay_applied", "replay_undone", "replay_redone"}
    assert set(states) == set(observations) == required
    original = little_words(reference(1)["input"]["f32_words"])
    assert plane(states["before"]) == original
    drawn = states["drawn"]
    assert drawn["selection"]["path"] == PATH
    assert drawn["undo"] == states["source_saved"]["undo"] + 1
    for sigma in (1, 12):
        name = f"preview_sigma{sigma}"
        assert plane(states[name]) == original
        assert states[name]["selection"] == drawn["selection"]
        preview = observations[name]["preview"]
        assert preview is not None and plane(preview) == expected_words(sigma)
        assert observations[name]["usage"]["retained_bytes"] > 0
    for name in ("applied", "redone", "filtered_saved", "workflow_saved",
                 "filtered_reopened", "replay_applied", "replay_redone"):
        assert plane(states[name]) == expected_words(12), name
        assert states[name]["selection"] == drawn["selection"], name
        assert states[name]["previous"] == drawn["previous"], name
    assert states["applied"]["undo"] == drawn["undo"] + 1
    assert plane(states["undone"]) == original
    assert states["undone"]["selection"] == drawn["selection"]
    for name in ("source_reopened", "workflow_loaded", "replay_prepared", "replay_undone"):
        assert plane(states[name]) == original
        assert states[name]["selection"] == states["before"]["selection"]
    assert states["replay_applied"]["undo"] == states["replay_prepared"]["undo"] + 1
    for name, state in states.items():
        assert retained(state) == retained(states["before"]), name
    assert set(row["files"]) == {"source", "filtered", "workflow"}
    for value in row["files"].values():
        assert len(bytes.fromhex(value["hex"])) == value["bytes"]
    workflow = json.loads(bytes.fromhex(row["files"]["workflow"]["hex"]))
    assert workflow  # Exact typed operations are additionally observed in states.
    assert len(states["workflow_saved"]["workflow"]) == 2
    assert row["final_usage"]["reserved_bytes"] == 0
    assert 0 < len(row["frames"]) <= 750
    for index, frame in enumerate(row["frames"], 1):
        assert frame["ordinal"] == index
        assert frame["rendered"]["input_frame"]["raw_inputs"] == frame["events"]
    for control in row["controls"]:
        assert control["hovered_id"] == control["item"]["id"] != 0
        assert control["item"]["enabled"] is True
        if "clip_rect" in control["item"]:
            x, y = control["actual_mouse"]
            left, top, right, bottom = control["item"]["clip_rect"]
            assert left <= x < right and top <= y < bottom
    assert len(row["canvas_hits"]) == 3
    assert [hit["image"] for hit in row["canvas_hits"]] == [[2.2, 1.2], [9.2, 1.2], [2.2, 8.2]]
    assert all(hit["hovered_id"] == hit["canvas_id"] != 0 for hit in row["canvas_hits"])


def rank_expected(method, radius):
    observed = next(row["observation"] for row in RANK_REFERENCE["cases"]
        if row["request"]["operation"] == method and row["request"]["pixel_type"] == "f32"
        and row["request"]["radius"] == radius and row["request"]["width"] == 23)
    result = list(observed["input"]["sample_words"])
    full = observed["roi_mask"]
    x, y = full["image_origin"]
    w, h = full["dimensions"]
    mask = bytes.fromhex(full["u8_hex"])
    filtered = observed["after_mask_reset"]["sample_words"]
    for j in range(h):
        for i in range(w):
            if mask[j*w+i]:
                at = (j+y)*23+i+x
                result[at] = filtered[at]
    return little_words(result)


@pytest.mark.parametrize("method", ["minimum", "maximum", "median", "mean", "variance"])
@pytest.mark.parametrize("destination", ["in-place", "new-document"])
@pytest.mark.parametrize("context", ["imgui_cpp", "imgui_python"])
def test_actual_rank_paths_preview_cancel_output_and_reopen(method, destination, context, tmp_path):
    case_id = f"rank-{method}-{destination}"
    if context == "imgui_cpp":
        path = Path(json.loads(os.environ["IMAGEJXX_PATH_FILTER_CPP_REPORTS"])[case_id])
        assert path.stat().st_size < 16*1024*1024
        row = json.loads(path.read_bytes())
    else:
        from imagejxx import _desktop
        editor = _desktop.Editor(preferences_path=str(tmp_path / "preferences.json"))
        row = editor._path_filter_ui_case(case_id, str(tmp_path / "capture"))
        if isinstance(row, str):
            row = json.loads(row)
    assert row["contract"] == "p13i.path-filter.ui.001"
    assert row["state"] == "observed" and row["error"] == ""
    assert row["case_id"] == case_id and row["context"] == context
    assert row["acceptance_claim"] is False
    assert row["recipe"] == {"operation": "rank_filter_imagej", "method": method,
        "radii": [1, 2.5], "apply_radius": 2.5,
        "destination": destination.replace("-", "_")}
    states, obs = row["states"], row["observations"]
    required = {"before", "source_saved", "drawn", "preview_radius1", "preview_radius2p5",
                "cancelled", "configured", "preview_apply", "applied", "filtered_saved", "filtered_reopened"}
    if destination == "in-place":
        required |= {"undone", "redone"}
    assert set(states) == set(obs) == set(row["document_states"]) == required
    original = little_words(reference(1)["input"]["f32_words"])
    drawn = states["drawn"]
    assert drawn["selection"]["path"] == PATH
    assert plane(drawn) == original
    for label, radius in [("preview_radius1", 1), ("preview_radius2p5", 2.5), ("preview_apply", 2.5)]:
        assert plane(states[label]) == original
        assert states[label]["selection"] == drawn["selection"]
        assert plane(obs[label]["preview"]) == rank_expected(method, radius)
        assert obs[label]["preview"]["target"]["destination"] == "in_place"
        assert obs[label]["usage"]["retained_bytes"] > 0
    assert obs["preview_radius2p5"]["preview"]["request_generation"] > obs["preview_radius1"]["preview"]["request_generation"]
    assert plane(states["cancelled"]) == original
    assert states["cancelled"]["undo"] == drawn["undo"]
    assert obs["cancelled"]["usage"]["reserved_bytes"] == 0
    for label in ("applied", "filtered_saved", "filtered_reopened"):
        assert plane(states[label]) == rank_expected(method, 2.5)
        assert states[label]["selection"] == drawn["selection"]
        assert retained(states[label]) == retained(drawn)
    source_id, output_id = row["source_document_id"], row["output_document_id"]
    if destination == "in-place":
        assert source_id == output_id
        assert states["applied"]["undo"] == drawn["undo"] + 1
        assert plane(states["undone"]) == original
        assert plane(states["redone"]) == rank_expected(method, 2.5)
    else:
        assert source_id != output_id
        for label, snapshot in row["document_states"].items():
            owners = {state["document_id"]: state for state in snapshot["documents"]}
            assert set(owners) == set(snapshot["document_ids"]) == set(snapshot["facade_ids"])
            assert snapshot["active_document_id"] in owners
            assert len(owners) <= 3 and source_id in owners
            if label in ("applied", "filtered_saved", "filtered_reopened"):
                source = owners[source_id]
                assert plane(source) == original
                assert source["selection"] == drawn["selection"]
                assert source["previous"] == drawn["previous"]
                assert source["undo"] == drawn["undo"]
                assert retained(source) == retained(drawn)
    assert row["final_usage"]["reserved_bytes"] == 0
    assert 0 < len(row["frames"]) <= 750
    for index, frame in enumerate(row["frames"], 1):
        assert frame["ordinal"] == index
        assert frame["rendered"]["input_frame"]["raw_inputs"] == frame["events"]
    for control in row["controls"]:
        assert control["hovered_id"] == control["item"]["id"] != 0
        assert control["item"]["enabled"] is True
    assert {"rank.method", "rank.method."+method, "rank.radius", "rank.preview", "rank.cancel", "rank.ok"} <= {
        control["selector"] for control in row["controls"]}
    if destination == "new-document":
        assert "rank.new_document" in {control["selector"] for control in row["controls"]}


def volume_words(state):
    assert state["shape"] == [2,3,4,17,23,1]
    result = {tuple(row["position"]): row["declared_f32_hex"] for row in state["planes"]}
    assert len(result) == len(state["planes"]) == 24
    assert set(result) == {(t,c,z) for t in range(2) for c in range(3) for z in range(4)}
    assert all(len(value) == 391*8 for value in result.values())
    return result


def scientific_original():
    result = {}
    for t in range(2):
        for c in range(3):
            for z in range(4):
                offset = 16*t+64*c+256*z
                words = [struct.pack("<f", ((37*x+53*y+(x*y)%17)%257-128)/16+offset).hex()
                         for y in range(17) for x in range(23)]
                if (t,c,z) == (0,0,0):
                    words[:2] = ["00000080", "00000000"]
                result[(t,c,z)] = "".join(words)
    return result


def scientific_filtered(operation, positioned):
    result = scientific_original()
    for t in (0,1):
        for c in (0,2):
            for z in (1,3):
                position = (t,c,z)
                if positioned and z != 1 and position != (1,0,3):
                    continue
                region = "union" if positioned and position == (1,0,3) else "active"
                row = next(row for row in SCIENTIFIC_REFERENCE["cases"]
                           if row["request"]["operation"] == operation
                           and tuple(row["request"]["position"]) == position
                           and row["request"]["region"] == region)
                result[position] = little_words(row["observation"]["after_mask_reset_f32_words"])
    return result


@pytest.mark.parametrize("operation", ["gaussian", "rank-mean"])
@pytest.mark.parametrize("roi", ["active", "positioned"])
@pytest.mark.parametrize("context", ["imgui_cpp", "imgui_python"])
def test_actual_scientific_path_ranges_preview_navigation_and_history(operation, roi, context, tmp_path):
    case_id = f"scientific-{operation}-{roi}"
    if context == "imgui_cpp":
        path = Path(json.loads(os.environ["IMAGEJXX_PATH_FILTER_CPP_REPORTS"])[case_id])
        assert path.stat().st_size < 16*1024*1024
        row = json.loads(path.read_bytes())
    else:
        from imagejxx import _desktop
        editor = _desktop.Editor(preferences_path=str(tmp_path/"preferences.json"))
        row = editor._path_filter_ui_case(case_id,str(tmp_path/"capture"))
        if isinstance(row,str):
            row = json.loads(row)
    assert row["contract"] == "p13i.path-filter.ui.001" and row["state"] == "observed"
    assert row["error"] == "" and row["acceptance_claim"] is False
    assert row["case_id"] == case_id and row["context"] == context
    assert row["recipe"] == {"operation": "rank_filter_imagej" if operation == "rank-mean" else "gaussian_blur_imagej",
        "method": "mean" if operation == "rank-mean" else None,
        "radius": 2.5 if operation == "rank-mean" else None,
        "sigma": None if operation == "rank-mean" else 1,
        "destination":"in_place", "roi_policy":"positioned_rois" if roi == "positioned" else "active_selection",
        "ui_ranges":[[1,2,1],[1,3,2],[2,4,2]], "preview_ordinal":6,
        "initial_position":[1,1,2], "away_position":[0,1,2]}
    states, obs = row["states"], row["observations"]
    required = {"before","source_saved","drawn","target_configured","preview_hidden","preview_shown",
                "navigated_t","navigated_c","navigated_z","preview_restored","apply_away","applied",
                "undone","redone","filtered_saved","filtered_reopened"}
    assert set(states) == set(obs) == set(row["document_states"]) == required
    original = scientific_original()
    filtered = scientific_filtered("mean" if operation == "rank-mean" else "gaussian",roi == "positioned")
    assert states["drawn"]["selection"]["path"] == PATH
    for name in required-{"applied","redone","filtered_saved","filtered_reopened"}:
        assert volume_words(states[name]) == original, name
    for name in ("applied","redone","filtered_saved","filtered_reopened"):
        assert volume_words(states[name]) == filtered, name
        assert states[name]["selection"] == states["drawn"]["selection"]
        assert states[name]["previous"] == states["drawn"]["previous"]
    assert states["applied"]["undo"] == states["drawn"]["undo"] + 1
    assert states["undone"]["undo"] == states["drawn"]["undo"]
    for name,state in states.items():
        assert retained(state) == retained(states["before"]),name
    positions = {"before":(1,1,2),"preview_hidden":(1,1,2),"preview_shown":(1,0,3),
        "navigated_t":(0,0,3),"navigated_c":(0,1,3),"navigated_z":(0,1,2),
        "preview_restored":(1,0,3),"apply_away":(0,1,2),"applied":(0,1,2),
        "undone":(0,1,2),"redone":(0,1,2),"filtered_reopened":(0,1,2)}
    for name,expected in positions.items():
        view = states[name]["display"]
        assert (view["t"],view["c"],view["crosshair"][2]) == expected,name
    preview_expected = dict(original)
    preview_expected[(1,0,3)] = filtered[(1,0,3)]
    generations = set()
    for name in ("preview_hidden","preview_shown","navigated_t","navigated_c","navigated_z","preview_restored","apply_away"):
        preview = obs[name]["preview"]
        assert preview is not None and volume_words(preview) == preview_expected,name
        generations.add(preview["request_generation"])
        target = preview["target"]
        assert target["destination"] == "in_place"
        assert target["positions"] == [[1,0,3]]
    assert len(generations) == 1
    assert obs["preview_hidden"]["ordinary_pixels"]["pixels_source"] == "live"
    assert obs["preview_shown"]["ordinary_pixels"]["pixels_source"] == "preview"
    assert obs["navigated_c"]["ordinary_pixels"]["pixels_source"] == "live"
    assert obs["preview_restored"]["ordinary_pixels"]["pixels_source"] == "preview"
    assert len(row["navigation_requests"]) == 6
    assert [(nav["axis"],nav["requested_zero_based"]) for nav in row["navigation_requests"]] == [
        ("t",0),("c",1),("z",2),("t",0),("c",1),("z",2)]
    assert row["panel_moves"]
    assert all(move["before"]["id"] != 0 and move["first_frame"] < move["last_frame"]
               for move in row["panel_moves"])
    assert row["final_usage"]["reserved_bytes"] == 0
    assert 0 < len(row["frames"]) <= 750
    for index,frame in enumerate(row["frames"],1):
        assert frame["ordinal"] == index
        assert frame["rendered"]["input_frame"]["raw_inputs"] == frame["events"]


BUDGET_REFERENCE = json.loads(gzip.decompress(
    (ROOT / "tests/fixtures/path-budget-targets-imagej154r.json.gz").read_bytes()))


def budget_plane(state):
    assert state["shape"] == [1,1,1,128,256,1]
    assert len(state["planes"]) == 1 and state["planes"][0]["position"] == [0,0,0]
    words = state["planes"][0]["declared_f32_hex"]
    assert len(bytes.fromhex(words)) == 32768*4
    return words


def budget_rgb(observation):
    image = observation["ordinary_pixels"]["readback"]["rendered_rgb"]
    assert image["dtype"] == "uint8" and image["shape"] == [128,256,3]
    assert image["encoding"] == "uint8_hex" and "pixels" not in image
    values = bytes.fromhex(image["hex"])
    assert len(values) == 128*256*3
    return values


def budget_file(observation):
    file = observation["preferences_file"]
    raw = bytes.fromhex(file["hex"])
    assert len(raw) == file["byte_count"] and 0 < len(raw) <= 1024*1024
    assert json.loads(raw) == file["envelope"]
    return raw


@pytest.mark.parametrize("case_id", ["budget-gaussian", "budget-rank-mean"])
@pytest.mark.parametrize("context", ["imgui_cpp", "imgui_python"])
def test_actual_path_filter_budget_refusal_release_and_recovery(case_id, context, tmp_path):
    if context == "imgui_cpp":
        path = Path(json.loads(os.environ["IMAGEJXX_PATH_FILTER_CPP_REPORTS"])[case_id])
        assert path.stat().st_size < 16*1024*1024
        row = json.loads(path.read_bytes())
    else:
        from imagejxx import _desktop
        editor = _desktop.Editor(preferences_path=str(tmp_path/"preferences.json"))
        row = editor._path_filter_ui_case(case_id,str(tmp_path/"capture"))
        if isinstance(row,str):
            row = json.loads(row)
    assert row["contract"] == "p13i.path-filter.ui.001" and row["state"] == "observed"
    assert row["error"] == "" and row["acceptance_claim"] is False
    assert row["case_id"] == case_id and row["context"] == context
    rank = case_id == "budget-rank-mean"
    assert row["recipe"] == {"operation":"rank_filter_imagej" if rank else "gaussian_blur_imagej",
        "method":"mean" if rank else None, "radius":2.5 if rank else None,
        "sigma":None if rank else 1, "destination":"in_place", "roi_policy":"active_selection",
        "initial_memory_mib":64, "requested_memory_mib":[1,1,64], "rendered_rgb_encoding":"uint8_hex"}
    required = {"before","drawn","ready","lower_rejected","low_accepted","low_apply_refused",
                "recovered","undone","redone"}
    states, obs = row["states"], row["observations"]
    assert set(states) == set(obs) == required
    assert set(row["milestones"]) == {"released","restored","reopened","preview_failed","preview_retry_ready"}
    reference = next(row["observation"] for row in BUDGET_REFERENCE["cases"] if row["id"] == case_id)
    original = little_words(reference["input"]["sample_words"])
    filtered = little_words(reference["after_mask_reset"]["sample_words"])
    assert states["drawn"]["selection"]["path"] == PATH
    for name in required:
        assert budget_plane(states[name]) == (filtered if name in {"recovered","redone"} else original),name
        assert retained(states[name]) == retained(states["before"]),name
        if name != "before":
            assert states[name]["selection"] == states["drawn"]["selection"],name
            assert states[name]["previous"] == states["drawn"]["previous"],name
        budget_rgb(obs[name])
        budget_file(obs[name])
    drawn = states["drawn"]
    for name in ("ready","lower_rejected","low_accepted","low_apply_refused","undone"):
        assert states[name]["undo"] == drawn["undo"],name
        assert states[name]["data_identity"] == drawn["data_identity"],name
    for name in ("recovered","redone"):
        assert states[name]["undo"] == drawn["undo"]+1,name
    ready, rejected = obs["ready"], obs["lower_rejected"]
    assert ready["preview"] == rejected["preview"]
    assert budget_plane(ready["preview"]) == filtered
    assert ready["usage"] == rejected["usage"]
    assert ready["usage"]["retained_bytes"] > 1024*1024
    assert ready["usage"]["running_bytes"] == 0
    assert ready["limits"] == rejected["limits"]
    assert ready["settings"] == rejected["settings"]
    assert budget_file(ready) == budget_file(rejected)
    assert budget_rgb(ready) == budget_rgb(rejected)
    assert rejected["preferences_panel"]["error"]
    assert ready["gaussian_jobs"]["installed_preview_storage"] == rejected["gaussian_jobs"]["installed_preview_storage"]
    for name in ("before","drawn","ready","lower_rejected","recovered","undone","redone"):
        assert obs[name]["settings"]["processing_memory_mb"] == 64,name
        assert obs[name]["limits"]["memory_budget"] == 64*1024*1024,name
    low, denied = obs["low_accepted"],obs["low_apply_refused"]
    assert budget_file(low) == budget_file(denied) != budget_file(ready)
    for name in ("low_accepted","low_apply_refused"):
        assert obs[name]["settings"]["processing_memory_mb"] == 1
        assert obs[name]["limits"]["memory_budget"] == 1024*1024
        assert obs[name]["usage"]["reserved_bytes"] == 0
        assert obs[name]["preview"] is None
    assert low["actual_native_jobs"] == denied["actual_native_jobs"]
    assert denied["error"] and denied["modal"]["open"]
    panel = "rank_panel" if rank else "gaussian_panel"
    assert denied[panel]["open"] is False
    for name in ("recovered","undone","redone"):
        assert obs[name]["usage"]["reserved_bytes"] == 0
        assert obs[name]["error"] == ""
        assert budget_file(obs[name]) == budget_file(ready)
    assert row["final_usage"]["reserved_bytes"] == 0
    requests = row["memory_requests"]
    assert [(request["label"],request["requested_mib"]) for request in requests] == [
        ("lower_rejected",1),("low_accepted",1),("restored",64)]
    for request in requests:
        assert 0 < request["first_frame"] < request["last_frame"] <= len(row["frames"])
        budget_file(request)
    for name, milestone in row["milestones"].items():
        assert milestone["snapshot_kind"] == "scalar_milestone"
        assert 0 < milestone["frame"] <= len(row["frames"])
        assert milestone["owner"]["document_id"] == drawn["document_id"]
        assert milestone["owner"]["data_identity"] == drawn["data_identity"]
        assert milestone["owner"]["undo"] == drawn["undo"]
        assert milestone["owner"]["revisions"] == drawn["revisions"]
        assert milestone["history"] == drawn["history"]
        retry_ready = name == "preview_retry_ready"
        assert milestone["usage"]["reserved_bytes"] > 1024*1024 if retry_ready else milestone["usage"]["reserved_bytes"] == 0
        assert (milestone["gaussian_jobs"]["installed_preview_storage"] is not None) is retry_ready
        assert milestone["ordinary_pixels"]["pixels_source"] == ("preview" if retry_ready else "live")
        memory = 1 if name == "preview_failed" else 64
        assert milestone["settings"]["processing_memory_mb"] == memory
        assert milestone["limits"]["memory_budget"] == memory*1024*1024
        assert milestone["error"] == ""
        assert budget_file(milestone) == (budget_file(low) if memory == 1 else budget_file(ready))
    failed = row["milestones"]["preview_failed"]
    restored = row["milestones"]["restored"]
    retried = row["milestones"]["preview_retry_ready"]
    assert failed[panel]["open"] and failed[panel]["preview"] and failed[panel]["draft_valid"]
    assert failed[panel]["preview_error"]
    assert failed["gaussian_jobs"]["preview"]["exit"] == "admission_failed"
    assert failed["gaussian_jobs"]["preview"]["job"] is None
    assert failed["gaussian_jobs"]["preview"]["owns_job"] is False
    assert restored[panel]["preview"] and restored[panel]["generation"] == failed[panel]["generation"]
    assert restored[panel]["preview_error"] == failed[panel]["preview_error"]
    assert restored["gaussian_jobs"]["requested_generation"] == failed["gaussian_jobs"]["requested_generation"]
    assert restored["gaussian_jobs"]["preview"] == failed["gaussian_jobs"]["preview"]
    assert restored["actual_native_jobs"] == failed["actual_native_jobs"]
    no_retry = restored["no_retry_frames"]
    assert len(no_retry) == len(set(no_retry)) == 3
    assert no_retry == list(range(no_retry[0], no_retry[0]+3))
    restore_request = next(request for request in requests if request["label"] == "restored")
    assert failed["frame"] < restore_request["first_frame"] <= restore_request["last_frame"] < no_retry[0]
    # Continue through closing Preferences and hovering the explicit Off control;
    # a background retry after the three idle frames would still be incorrect.
    preview_selector = "rank.preview" if rank else "gaussian.preview"
    off = next(control for control in row["controls"]
               if control["selector"] == preview_selector and control["frame"] > restored["frame"])
    for ordinal in range(no_retry[0], off["frame"]+1):
        frame = row["frames"][ordinal-1]
        if ordinal in no_retry:
            assert frame["events"] == []
        view = frame["rendered"]
        assert view[panel]["generation"] == failed[panel]["generation"]
        assert view[panel]["preview"] is True
        for field,value in view["gaussian_jobs"]["preview"].items():
            assert value == failed["gaussian_jobs"]["preview"][field], field
        assert view["gaussian_jobs"]["requested_generation"] == failed["gaussian_jobs"]["requested_generation"]
        assert view["gaussian_jobs"]["active_installed_generation"] is None
        assert view["actual_native_jobs"] == failed["actual_native_jobs"]
        assert view["ordinary_pixels"]["pixels_source"] == "live"
        assert view["usage"]["reserved_bytes"] == 0
        assert view["limits"]["memory_budget"] == 64*1024*1024
    assert retried[panel]["preview"] and retried[panel]["preview_error"] == ""
    assert retried[panel]["generation"] > failed[panel]["generation"]
    assert budget_plane(retried["preview"]) == filtered
    double_words = "".join(struct.pack("<d", sample[0]).hex()
                           for sample in struct.iter_unpack("<f", bytes.fromhex(filtered)))
    assert retried["preview"]["planes"][0]["storage_f64_hex"] == double_words
    assert retried["preview"]["source_document_id"] == drawn["document_id"]
    assert retried["preview"]["request_generation"] == retried[panel]["generation"] == retried["gaussian_jobs"]["active_installed_generation"]
    reopened = row["milestones"]["reopened"][panel]
    assert reopened["open"] is True and reopened["preview"] is False
    if rank:
        assert reopened["method"] == "mean" and reopened["radius"] == 2.5
    else:
        assert reopened["sigma"] == 1 and reopened["scaled_units"] is False
    assert row["files"] == {}
    assert 0 < len(row["frames"]) <= 750
    for index,frame in enumerate(row["frames"],1):
        assert frame["ordinal"] == index
        assert frame["rendered"]["input_frame"]["raw_inputs"] == frame["events"]
    selectors = {control["selector"] for control in row["controls"]}
    assert {"preferences.apply","preferences.close_button","gaussian.preview" if not rank else "rank.preview"} <= selectors
    for control in row["controls"]:
        assert control["hovered_id"] == control["item"]["id"] != 0
        assert control["item"]["enabled"] is True
    assert len(row["canvas_hits"]) == 3
    assert [hit["image"] for hit in row["canvas_hits"]] == [[2.2,1.2],[9.2,1.2],[2.2,8.2]]
