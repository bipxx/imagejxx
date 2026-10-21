"""Independent installed P06c2 physical assertions, both shared ImGui launches.

The retained P06c reader supplies transport, raw-input and snapshot checks. Its
qualified processor rules and the qualified ImageJ reader's pure LUT formulas
are test-owned expectations; no product mapper/producer supplies an oracle.
"""
import copy
import hashlib
import importlib
import math
import os
from pathlib import Path
import struct
import sysconfig
import time

import pytest
import test_channel_display_ui as base
import test_composite_max_reference as reference

CONTRACT = "p06c2.channel-display-projection-ui.001"
CASES = tuple(f"M{i:02d}" for i in range(1, 11))
CONTEXTS = ("imgui_cpp", "imgui_python")
EXPECTED_COLLECTION_COUNT = 20
EXPECTED_COLLECTION_IDS = tuple(f"test_installed_channel_display_projection[{case}-{context}]"
                                for case in CASES for context in CONTEXTS)
REFERENCE_READER_SHA = "17954cda6ae9a0ac7bff699785961923ab4ac5b30b58d87667c4d4958c8d3e61"
MAX_REPORT, MAX_ARCHIVE = 16*1024**2, 32*1024**2
VIEWPORT = (1250, 820)
CHECKPOINTS = {
    "M01": ("sum.ordinary", "sum.linked", "max.ordinary", "max.linked", "max.redraw"),
    "M02": ("max.ordinary", "range.c1", "range.c0", "lut.c0", "navigation.changed", "max.linked", "max.redraw"),
    "M03": ("max.ordinary", "max.linked", "range.equal", "equal.linked", "equal.redraw"),
    "M04": ("max.ordinary", "max.linked", "visible.hidden_active", "visible.hidden_first", "visible.empty",
            "visible.empty_redraw", "visible.restored", "visible.restored_redraw"),
    "M05": ("mode.max", "mode.color", "mode.grayscale", "mode.sum", "mode.max_again"),
    "M06": ("source.ordinary", "source.linked", "mixed.ordinary", "mixed.linked", "constant.ordinary",
            "constant.linked", "last_step.ordinary", "last_step.linked", "last_step.repeat", "last_step.repeat_redraw"),
    "M07": ("preview.ready", "max.ordinary", "preview.threshold_linked", "preview.nonactive_linked",
            "preview.nonactive_ordinary", "preview.away", "preview.back", "preview.panel_reopened",
            "preview.other_owner", "preview.returned"),
    "M08": ("record.original", "record.navigated", "record.replayed", "record.rebind_opened", "record.rebound", "record.off_replayed"),
    "M09": ("saved.v1_edited", "saved.v1", "saved.v1_reopened", "saved.max", "saved", "reopened", "future.refused", "legacy.refused"),
    "M10": ("draft.partial", "draft.disabled_apply", "draft.max", "draft.max_again", "draft.busy_disabled",
            "guards.complete", "float64.legacy", "rgb.legacy", "rgba.legacy", "count.fallback", "owner.returned"),
}
API_LABELS = {
    "M07": ("threshold_overlay_preparation",),
    "M09": ("future_native_roundtrip", "legacy_projection_preparation", "legacy_v2_refusal", "detached_v2_refusal"),
    "M10": ("queued_apply_preparation", "busy_select_max_refusal", "coupled_min_without_c0_refusal",
        "inactive_select_max", "foreign_owner_refusal", "malformed_original_rebind_refusal",
        "fresh_display_intent_preparation", "stale_display_refusal", "processing_cap_refusal",
        "processing_cancel_refusal", "resolver_cap_refusal", "resolver_cancel_refusal", "resolver_callback_refusal",
        "stale_metadata_preparation", "stale_record_refusal", "close_owner_preparation", "closed_source_refusal",
        "float64_unsupported_set", "rgb_unsupported_set", "rgba_unsupported_set", "cardinality_refusal"),
}


@pytest.fixture(scope="module")
def installed_cpp_executable():
    assert os.environ.get("IMAGEJXX_CPP_EXE"), "Set the matching installed IMAGEJXX_CPP_EXE"
    assert os.environ.get("IMAGEJXX_CPP_EXE_SHA256"), "Pin the installed executable SHA256"
    return base.installed_cpp_executable.__wrapped__()


@pytest.fixture(scope="module")
def standalone_cases(installed_cpp_executable):
    configured = os.environ.get("IMAGEJXX_CHANNEL_DISPLAY_PROJECTION_CPP_REPORT")
    assert configured, "Set IMAGEJXX_CHANNEL_DISPLAY_PROJECTION_CPP_REPORT to the installed path-only index"
    path = Path(configured).resolve(strict=True)
    index = base._read(path)
    assert index["contract_id"] == CONTRACT and index["context"] == "imgui_cpp"
    assert type(index["case_count"]) is int and index["case_count"] == 10
    assert index["observation_state"] == "observed" and index["acceptance_claim"] is False
    assert 0 <= base._number(index["elapsed_seconds"]) <= 60
    assert len(index["cases"]) == 10
    result = {}
    for entry in index["cases"]:
        assert set(entry) <= {"case_id", "id", "relative_path", "byte_count", "sha256"}
        case = entry["case_id"]
        assert entry["id"] == case and case in CASES and case not in result
        member = base._member(path.parent, entry["relative_path"])
        raw = member.read_bytes()
        assert 0 < len(raw) <= MAX_REPORT
        if "byte_count" in entry:
            assert len(raw) == entry["byte_count"]
        if "sha256" in entry:
            assert hashlib.sha256(raw).hexdigest() == entry["sha256"]
        result[case] = member
    assert set(result) == set(CASES) and len(set(result.values())) == 10
    return result


def _observe(case, context, directory, request):
    if context == "imgui_cpp":
        path = request.getfixturevalue("standalone_cases")[case]
        return base._read(path), path.parent
    assert not directory.exists()
    started = time.monotonic()
    desktop = importlib.import_module("imagejxx._desktop")
    module = Path(desktop.__file__).resolve(strict=True)
    roots = {Path(sysconfig.get_path(key)).resolve()/"imagejxx" for key in ("purelib", "platlib")}
    assert module.suffix in {".pyd", ".so"} and any(module.is_relative_to(root) for root in roots)
    editor = desktop.Editor(preferences_path=str(directory.parent/"preferences.json"))
    # M IDs share the existing observation entry and CLI dispatch. They do
    # not create a second desktop API or an alternate physical input path.
    row = base._decode(editor._channel_display_ui_case(case, context, str(directory)))
    assert row == base._read(directory/"case.json") and time.monotonic()-started <= 60
    return row, directory


def _state(count=3, *, version=1, projection="sum", mode="composite", bounds=(0.,255.)):
    result = {"schema":"imagejxx.channel-display", "version":version, "mode":mode,
              "channels":[{"visible":True, "range":list(bounds), "lut":{"name":("Red","Yellow","Cyan")[c%3]}}
                          for c in range(count)]}
    if version == 2:
        result["projection"] = projection
    return result


def _table(name):
    if name in base.MASKS:
        return [base._lut(i, {"name":name}) for i in range(256)]
    return [list(row) for row in reference._table(name)]


def _select_max(state, active):
    """Qualified state-derived policy, including explicit Min and C0 effect."""
    result = copy.deepcopy(state)
    selected = ("gray" if state["mode"] == "grayscale" or
                (state["mode"] == "composite" and len(state["channels"]) == 1) else None)
    table = _table(selected) if selected else state["channels"][active]["lut"].get("table")
    if table is None:
        table = _table(state["channels"][active]["lut"]["name"])
    navigation = None
    result.update(version=2, projection="max", mode="composite" if len(result["channels"]) <= 8 else "color")
    if reference._inverted(table):
        for channel in result["channels"]:
            original = channel["lut"].get("table")
            if original is None:
                original = _table(channel["lut"]["name"])
            channel["lut"] = {"table":[list(row) for row in reference._macro(original)]}
        navigation = 0
        if reference._inverted(result["channels"][0]["lut"]["table"]):
            result["projection"] = "min"
    if state["mode"] != "composite":
        for channel in result["channels"]:
            channel["visible"] = True
    return result, navigation


def _assert_stored(source, expected):
    actual = base._stored(source)
    assert base._typed(actual) == base._typed(expected)
    if expected is None:
        assert "channel_display" not in source["display"]["properties"]
        return
    keys = {"schema","version","mode","channels"} | ({"projection"} if expected["version"] == 2 else set())
    assert set(actual) == keys and type(actual["version"]) is int and actual["version"] in {1,2}
    assert actual["mode"] in {"color","grayscale","composite"}
    assert actual.get("projection","sum") in {"sum","max","min"}
    assert len(actual["channels"]) == source["shape"][1]
    for channel in actual["channels"]:
        assert set(channel) == {"visible","range","lut"} and type(channel["visible"]) is bool
        assert len(channel["range"]) == 2 and base._number(channel["range"][0]) <= base._number(channel["range"][1])
        lut = channel["lut"]
        assert set(lut) in ({"name"},{"table"})
        if "table" in lut:
            assert len(lut["table"]) == 256
            assert all(len(row) == 3 and all(type(v) is int and 0 <= v <= 255 for v in row) for row in lut["table"])
        else:
            assert lut["name"] in base.MASKS


def _record(record, source, expected, *, action="set", channels=(), empty=(), navigation=None):
    version = 1 if expected is None else expected["version"]
    keys = {"schema","version","action","source","position","state","reset"}
    if version == 2:
        keys |= {"display_dependency","navigation"}
    assert set(record) == keys and record["schema"] == "imagejxx.channel-display-command"
    assert type(record["version"]) is int and record["version"] == version and record["action"] == action
    binding = {"id":source["document_id"], "pixels_revision":source["revisions"]["pixels"],
        "metadata_revision":source["revisions"]["metadata"], "shape":source["shape"],
        "type":source["dtype"], "sample_policy":source["sample_policy"]}
    assert base._typed(record["source"]) == base._typed(binding)
    assert record["position"] == {"t":source["display"]["t"], "c":source["display"]["c"], "z":source["display"]["crosshair"][2]}
    assert all(type(v) is int for v in record["position"].values())
    assert base._typed(record["state"]) == base._typed(expected)
    assert base._typed(record["reset"]) == base._typed({"channels":list(channels), "empty_finite":list(empty)})
    if version == 2:
        assert base._typed(record["display_dependency"]) == base._typed(
            {"revision":source["revisions"]["display"]} if action == "select_max" else None)
        assert base._typed(record["navigation"]) == base._typed({"c":navigation} if navigation is not None else None)
        if action == "select_max" and expected["projection"] == "min":
            assert navigation == 0


def _physical(row):
    assert len(row["drag_observations"]) == (1 if row["case_id"] == "M07" else 0)
    # P06c's retained raw-input helper has a fixed one-drag case label; adapt
    # that expectation only, after independently fixing new-case membership.
    base._physical(dict(row, case_id="C04B" if row["case_id"] == "M07" else row["case_id"]))
    for observation in row["control_observations"]:
        assert all(0 <= base._number(v) < bound for v,bound in zip(observation["position"],VIEWPORT))
    for frame in row["frames"]:
        for event in frame["requested_events"]:
            if event["type"] == "wheel":
                assert row["case_id"] == "M08" and frame["label"] in {"record.off_scroll.down","record.off_scroll.up"}
                assert event == {"type":"wheel", "x":0, "y":-3 if frame["label"].endswith("down") else 3}


def _snapshot(row, label, expected=None, document_id=None):
    state, rendered = base._state(row,label)
    source = base._owner(state, document_id or row["source_document_id"])
    if expected is not None:
        _assert_stored(source,expected)
    return state,rendered,source


def _sample(t,c,z,y,x, variant="finite"):
    if variant == "quiet" and (t,c,z,y,x) == (1,1,2,1,2):
        return math.nan
    if variant == "quiet" and (t,c,z,y,x) == (1,1,2,2,3):
        return -0.
    return float(base._source_value(t,c,z,y,x))


def _rgb(image, source, state, plane, fixed, *, variant="finite", preview=None, generation=0, threshold=False):
    shape,display,dtype = source["shape"],source["display"],source["dtype"]
    t,active,gate = display["t"],display["c"],display["crosshair"][2]
    contributing = ([c for c,ch in enumerate(state["channels"]) if ch["visible"]]
                    if state["mode"] == "composite" else [active])
    height,width = (shape[3],shape[4]) if plane == "XY" else (shape[2],shape[4] if plane == "XZ" else shape[3])
    readback = image["readback"]
    assert readback["available"] is True and image["source_document_id"] == source["document_id"]
    assert image["current_tcz"] == image["range_tcz"] == readback["source_tcz"] == [t,active,gate]
    assert image["range"] == state["channels"][active]["range"]
    output,raw = [],[]
    for v in range(height):
        for u in range(width):
            x,y,z = base._xyz(plane,fixed,u,v)
            rgb = None
            for c in contributing:
                value = _sample(t,c,z,y,x,variant)
                if preview is not None and (t,c,gate,z) == (1,1,2,2):
                    value = preview[y*5+x]
                channel = state["channels"][c]
                index = base._index(value,dtype,channel["range"])
                mapped = ([index]*3 if state["mode"] == "grayscale" or
                          (state["mode"] == "composite" and shape[1] == 1) else base._lut(index,channel["lut"]))
                if rgb is None:
                    rgb = list(mapped)
                elif state.get("projection","sum") == "max":
                    rgb = [max(a,b) for a,b in zip(rgb,mapped)]
                elif state.get("projection","sum") == "min":
                    rgb = [min(a,b) for a,b in zip(rgb,mapped)]
                else:
                    rgb = [min(255,a+b) for a,b in zip(rgb,mapped)]
            rgb = rgb or [0,0,0]
            if threshold and plane == "XY" and active in contributing and (t,active,gate) == (1,1,2) and 83 <= base._source_value(t,active,z,y,x) <= 85:
                rgb = [255,rgb[1]//2,rgb[2]//2]
            output.extend(rgb)
            value = _sample(t,active,z,y,x,variant)
            if preview is not None and (t,active,gate,z) == (1,1,2,2):
                value = preview[y*5+x]
            raw.append(value)
    actual = readback["rendered_rgb"]
    assert actual["shape"] == [height,width,3] and actual["dtype"] == "uint8" and actual["pixels"] == output
    assert all(type(v) in {int,float} and math.isfinite(v) and 0 <= v <= 255 and v == int(v) for v in actual["pixels"])
    diagnostic = image["channel_display"]
    assert diagnostic["state"] == state and [p["c"] for p in diagnostic["contributors"]] == contributing
    for c,item in zip(contributing,diagnostic["contributors"]):
        staged = preview is not None and t == 1 and c == 1 and gate == 2
        assert item["preview"] is staged and item["generation"] == (generation if staged else 0)
        assert item["preview_z"] == ([2] if staged else []) and item["range"] == state["channels"][c]["range"]
        base._integer(item["data_identity"],minimum=1)
    live = {v["data_identity"] for v in diagnostic["contributors"] if not v["preview"]}
    staged = {v["data_identity"] for v in diagnostic["contributors"] if v["preview"]}
    assert len(live) <= 1 and len(staged) <= 1 and not live.intersection(staged)
    observed_raw = readback["source"]
    assert observed_raw["shape"] == [height,width,1] and observed_raw["dtype"] == dtype
    if variant == "quiet":
        expected = b"".join(struct.pack("<I",0x7FC12345) if math.isnan(v) else struct.pack("<f",v) for v in raw)
        assert observed_raw["encoding"] == "IEEE754 Float32LE" and bytes(observed_raw["bytes"]) == expected
    else:
        assert len(observed_raw["pixels"]) == len(raw)
        assert b"".join(struct.pack("<d",v) for v in observed_raw["pixels"]) == b"".join(struct.pack("<d",v) for v in raw)
    return output


def _view(row,label,expected,*,linked=False,variant="finite",preview=None,generation=0,threshold=False,document_id=None):
    state,rendered,source = _snapshot(row,label,expected,document_id)
    assert state["active_document_id"] == source["document_id"] and source["display"]["orthogonal"] is linked
    x,y,z = source["display"]["crosshair"]
    if linked:
        assert [p["plane"] for p in rendered["orthogonal_planes"]] == ["XY","XZ","YZ"]
        for image,fixed in zip(rendered["orthogonal_planes"],(z,y,x)):
            assert image["coordinate"] == fixed
            _rgb(image,source,expected,image["plane"],fixed,variant=variant,preview=preview,generation=generation,threshold=threshold)
    else:
        _rgb(rendered["ordinary_pixels"],source,expected,"XY",z,variant=variant,preview=preview,generation=generation,threshold=threshold)
    return state,rendered,source


def _physical_actions(row):
    for observed in row["control_observations"]:
        selector = observed["selector"]
        if not (selector in {"channel_display.enable","channel_display.disable","channel_display.apply",
                             "channel_display.reset_current","channel_display.reset_all"}
                or any(selector.startswith(prefix) for prefix in
                       ("channel_display.mode.","channel_display.lut.","channel_display.visible."))):
            continue
        ordinal = observed["before_frame_ordinal"]
        original_frame, released = row["frames"][ordinal-1],row["frames"][ordinal+2]
        before,after = original_frame["native_state"],released["native_state"]
        panel = original_frame["rendered"]["channel_display_panel"]
        owner_id = panel["source_id"]
        if observed["disabled_attempt"] or not owner_id:
            assert observed["disabled_attempt"]
            for name in ("document_ids","facade_ids","active_document_id","workflow","results","recording"):
                assert base._typed(after[name]) == base._typed(before[name])
            for source in before["documents"]:
                base._summary_unchanged(source,base._owner(after,source["document_id"]),display=True)
            continue
        source = base._owner(before,owner_id)
        expected = copy.deepcopy(base._stored(source))
        action,selected,empty,navigation = "set",[],[],None
        if selector == "channel_display.mode.composite_max":
            expected,navigation = _select_max(expected,source["display"]["c"])
            action = "select_max"
        elif selector == "channel_display.disable":
            expected,action = None,"disable"
        elif selector == "channel_display.enable":
            assert expected is None
            bounds = [0.,255.] if source["dtype"] == "uint8" else [50.,58.]
            expected = _state(source["shape"][1],bounds=bounds)
        elif selector.startswith("channel_display.mode."):
            mode = selector.removeprefix("channel_display.mode.")
            assert mode in {"color","grayscale","composite"}
            if mode == expected["mode"] and not (mode == "composite" and expected.get("projection","sum") != "sum"):
                continue
            expected["mode"] = "color" if mode == "composite" and source["shape"][1] > 8 else mode
            if mode == "composite":
                if expected["version"] == 2:
                    expected["projection"] = "sum"
                for ch in expected["channels"]:
                    ch["visible"] = True
        elif selector.startswith("channel_display.visible."):
            c = int(selector.removeprefix("channel_display.visible."))
            expected["channels"][c]["visible"] = not expected["channels"][c]["visible"]
        elif selector.startswith("channel_display.lut."):
            lut = {"name":selector.removeprefix("channel_display.lut.")}
            active = expected["channels"][source["display"]["c"]]
            if active["lut"] == lut:
                continue
            active["lut"] = lut
        elif selector == "channel_display.apply":
            values = [float(panel["minimum_text"]),float(panel["maximum_text"])]
            assert all(math.isfinite(v) for v in values) and values[0] <= values[1]
            expected["channels"][source["display"]["c"]]["range"] = values
        else:
            action = "reset_range"
            selected = [source["display"]["c"]] if selector.endswith("current") else list(range(source["shape"][1]))
            for c in selected:
                if source["dtype"] == "uint8":
                    bounds = [0.,255.]
                else:
                    offset = 40*source["display"]["t"]+30*c+5*source["display"]["crosshair"][2]
                    bounds = [float(offset),float(offset+8)]
                expected["channels"][c]["range"] = bounds
                empty.append(False)
        failed = before["recording"] and not isinstance(before["workflow"],list)
        for name in ("document_ids","facade_ids","active_document_id","results","recording"):
            assert base._typed(after[name]) == base._typed(before[name])
        for document in before["documents"]:
            base._summary_unchanged(document,base._owner(after,document["document_id"]),
                                    display=failed or document["document_id"] != owner_id)
        if failed:
            assert base._typed(after["workflow"]) == base._typed(before["workflow"])
            continue
        result = base._owner(after,owner_id)
        _assert_stored(result,expected)
        for name in ("t","crosshair","orthogonal","linked_zoom","zoom","plane_zoom","pan"):
            assert base._typed(result["display"][name]) == base._typed(source["display"][name])
        assert result["display"]["c"] == (source["display"]["c"] if navigation is None else navigation)
        assert {k:v for k,v in result["display"]["properties"].items() if k != "channel_display"} == {
            k:v for k,v in source["display"]["properties"].items() if k != "channel_display"}
        if before["recording"]:
            assert len(after["workflow"]) == len(before["workflow"])+1 and after["workflow"][:-1] == before["workflow"]
            _record(after["workflow"][-1],source,expected,action=action,channels=selected,empty=empty,navigation=navigation)
        else:
            assert base._typed(after["workflow"]) == base._typed(before["workflow"])


def _initial_state(case,label="source"):
    s = _state()
    if case == "M03":
        s["channels"][1]["lut"] = {"table":_table("custom_a")}
    if case == "M04":
        for ch,name in zip(s["channels"],("custom_a","custom_b","custom_c")):
            ch["lut"] = {"table":_table(name)}
    if case == "M05":
        s["channels"][1]["visible"] = False
    variant = {"source":"R15","mixed":"R16","constant":"R17","last_step":"R18"}.get(label)
    if case in {"M08","M10"} and label == "source":
        variant = "R16"
    if (case == "M06" or case in {"M08","M10"} and label == "source"):
        names = {"R15":("inverted_red","inverted_yellow","inverted_cyan"),
                 "R16":("Red","inverted_yellow","custom_c"),
                 "R17":("Red","constant","custom_b"),
                 "R18":("Red","last_step_only","custom_a")}[variant]
        for c,(channel,name) in enumerate(zip(s["channels"],names)):
            channel["range"] = [[1.,201.],[7.,219.],[11.,241.]][c]
            channel["lut"] = {"name":name} if name in base.MASKS else {"table":_table(name)}
        s["channels"][1]["visible"] = False
    if case == "M09" and label == "legacy":
        s = _state(1,version=2,projection="max")
    if case == "M10" and label == "many":
        s = _state(9,mode="color",bounds=(0.,511.))
        for c,ch in enumerate(s["channels"]):
            ch["lut"] = {"name":("Red","Green","Blue")[c%3]}
    if case == "M10" and label in {"float64","rgb","rgba"}:
        s = None
    return s


def _fixtures(row,case):
    labels = ({"source","mixed","constant","last_step"} if case == "M06" else
              {"source","other"} if case in {"M07","M08"} else
              {"source","legacy"} if case == "M09" else
              {"source","other","float64","rgb","rgba","many"} if case == "M10" else {"source"})
    assert set(row["fixture_sources"]) == labels
    initial,_ = base._state(row,"initial")
    assert initial["workflow"] == initial["results"] == [] and initial["recording"] is True
    assert row["fixture_sources"]["source"]["document_id"] == row["source_document_id"]
    for label,source in row["fixture_sources"].items():
        shape,dtype = [2,3,4,3,5,1],("uint8" if case in {"M01","M06"} else "uint16" if case == "M02" else "float32")
        if case == "M09" and label == "legacy":
            shape = [1,1,4,3,5,1]
        if case == "M10" and label in {"float64","rgb","rgba"}:
            shape,dtype = [1,1,4,3,5,{"float64":1,"rgb":3,"rgba":4}[label]],{"float64":"float64","rgb":"uint8","rgba":"float32"}[label]
        if case == "M10" and label == "many":
            shape,dtype = [2,9,4,3,5,1],"uint16"
        assert source["shape"] == shape and source["dtype"] == dtype
        expected_c = 8 if label == "many" else 2 if case == "M06" and label == "source" else 1 if shape[1] > 1 else 0
        assert [source["display"]["t"],source["display"]["c"],source["display"]["crosshair"]] == [
            1 if shape[0] > 1 else 0,expected_c,[1,1,2]]
        base._literal(source,variant="quiet" if case == "M03" else "finite")
        assert source["metadata"]["properties"] == {"source_only":{"nested":[True,7,"retained"]},"revision_probe":7}
        assert source["metadata"]["extensions"] == {"future_annotation":{"version":37,"opaque":[3,1,4]}}
        assert source["metadata"]["record_layout"] == {"source_presence":True}
        assert source["selection"] == {"kind":"rectangle","points":[[1,0],[4,3]],"width":1}
        assert source["previous_selection"] == {"kind":"line","points":[[0,0],[2,2]],"width":1}
        assert source["overlays"] == [] and source["spectrum_present"] is False
        assert source["dirty"] is False and source["undo_steps"] == source["redo_steps"] == 1
        assert source["rois"] == ([{"name":"explicit zero","selection":{"kind":"rectangle","points":[[1,0],[3,2]],"width":1},
            "t":0,"c":0,"z":0,"attributes":{}},{"name":"wildcard","selection":{"kind":"point","points":[[2,1]],"width":1},
            "t":None,"c":None,"z":1,"attributes":{}}] if shape[0] > 1 and shape[1] > 1 else [])
        _assert_stored(source,_initial_state(case,label))
        base._unchanged(source,base._owner(initial,source["document_id"]),display=True)
    return initial


def _mode_label(rendered, expected):
    label = {"color":"Color","grayscale":"Grayscale","composite":"Composite"}[expected["mode"]]
    if expected["mode"] == "composite" and expected.get("projection","sum") != "sum":
        label += " " + expected["projection"].title()
    assert rendered["channel_display_panel"]["mode_label"] == label


def _same_rgb(ordinary, linked):
    assert ordinary[1]["ordinary_pixels"]["readback"]["rendered_rgb"] == linked[1]["orthogonal_planes"][0]["readback"]["rendered_rgb"]


def _stable_source(row, original, labels, document_id=None):
    for label in labels:
        _,_,current = _snapshot(row,label,document_id=document_id)
        base._unchanged(original,current)
        assert {k:v for k,v in current["display"]["properties"].items() if k != "channel_display"} == {
            k:v for k,v in original["display"]["properties"].items() if k != "channel_display"}


def _summary_collection(before, after):
    for name in ("document_ids","facade_ids","active_document_id","workflow","results","recording","busy","usage"):
        assert base._typed(after[name]) == base._typed(before[name]), name
    for source in before["documents"]:
        result = base._owner(after,source["document_id"])
        assert base._typed(result) == base._typed({k:source[k] for k in result})


def _scrolls(row):
    rows = row["scroll_observations"]
    if row["case_id"] != "M08":
        assert rows == []
        return
    assert len(rows) == 2
    anchors = ("workflow.record","workflow.channel_display.0.replay","workflow.channel_display.2.replay")
    for observed,(suffix,selector,amount) in zip(rows,(
            ("down",anchors[0],-3),("up",anchors[2],3))):
        label = "record.off_scroll."+suffix
        assert observed["label"] == label and observed["selector"] == selector
        n = base._integer(observed["before_frame_ordinal"],minimum=1)
        assert observed["wheel_frame_ordinal"] == n+1 and observed["settled_frame_ordinal"] == n+2
        before,wheel,settled = row["frames"][n-1:n+2]
        assert wheel["label"] == label and settled["label"] == label+".settled"
        assert wheel["requested_events"] == [{"type":"wheel","x":0,"y":amount}]
        assert settled["requested_events"] == [] and observed["viewport_size"] == list(VIEWPORT)
        control = base._control(before["rendered"],selector)
        assert observed["control"] == control
        assert observed["control_after"] == base._control(settled["rendered"],selector)
        point = [float(math.floor(v)) for v in control["point"]]
        assert observed["mouse_position"] == before["rendered"]["input_frame"]["mouse_position"] == point
        assert all(0 <= v < bound for v,bound in zip(point,VIEWPORT))
        assert control["rect"][0] <= point[0] < control["rect"][2]
        assert control["rect"][1] <= point[1] < control["rect"][3]
        prior = [v for v in row["control_observations"] if v["before_frame_ordinal"]+3 <= n]
        assert prior[-1]["selector"] == selector
        for frame in (wheel,settled):
            raw = frame["rendered"]["input_frame"]
            assert raw["focus"]["window"] == "Workflow" and raw["popup_stack"] == []
            assert not any(raw["mouse_down"]) and raw["keys_down"] == []
            assert raw["modifiers"] == {k:False for k in ("ctrl","shift","alt","super")}
            _summary_collection(before["native_state"],frame["native_state"])
            assert frame["native_state"]["recording"] is False
        deltas = []
        for anchor in anchors:
            a,b = base._control(before["rendered"],anchor),base._control(settled["rendered"],anchor)
            assert a["id"] == b["id"] and a["enabled"] == b["enabled"]
            assert a["rect"][0::2] == b["rect"][0::2]
            assert a["rect"][3]-a["rect"][1] == b["rect"][3]-b["rect"][1]
            deltas.append(b["rect"][1]-a["rect"][1])
        assert max(deltas)-min(deltas) < .001
        assert deltas[0] < 0 if suffix == "down" else deltas[0] > 0
        target = base._control(settled["rendered"],anchors[2] if suffix == "down" else anchors[0])
        assert 0 <= math.floor(target["point"][1]) < VIEWPORT[1]


def _basic(row, case):
    original = row["fixture_sources"]["source"]
    expected,navigation = _select_max(_initial_state(case),original["display"]["c"])
    assert navigation is None and expected["projection"] == "max"
    if case == "M01":
        ordinary = _view(row,"sum.ordinary",_state())
        linked = _view(row,"sum.linked",_state(),linked=True)
        _same_rgb(ordinary,linked)
        maximum = _view(row,"max.ordinary",expected)
        assert maximum[1]["ordinary_pixels"]["readback"]["rendered_rgb"] != ordinary[1]["ordinary_pixels"]["readback"]["rendered_rgb"]
        linked = _view(row,"max.linked",expected,linked=True)
        _same_rgb(maximum,linked)
        repeated = _view(row,"max.redraw",expected,linked=True)
        for old,new in zip(linked[1]["orthogonal_planes"],repeated[1]["orthogonal_planes"]):
            assert old["readback"] == new["readback"] and old["builds"] == new["builds"]
    elif case == "M02":
        _view(row,"max.ordinary",expected)
        expected["channels"][1]["range"] = [11.,91.]
        _view(row,"range.c1",expected)
        expected["channels"][0]["range"] = [5.,101.]
        _view(row,"range.c0",expected)
        expected["channels"][0]["lut"] = {"name":"Magenta"}
        _view(row,"lut.c0",expected)
        changed = _view(row,"navigation.changed",expected)
        assert [changed[2]["display"]["t"],changed[2]["display"]["c"],changed[2]["display"]["crosshair"]] == [0,2,[1,1,3]]
        linked = _view(row,"max.linked",expected,linked=True)
        _same_rgb(changed,linked)
        _view(row,"max.redraw",expected,linked=True)
        base._selectors(row,"channel_display.channel.0","channel_display.channel.2","channel_display.lut.Magenta",
                        "navigation.time","image.slice")
        for text in ("11","91","5","101","1","4"):
            base._text(row,text)
    elif case == "M03":
        ordinary = _view(row,"max.ordinary",expected,variant="quiet")
        linked = _view(row,"max.linked",expected,linked=True,variant="quiet")
        _same_rgb(ordinary,linked)
        expected["channels"][1]["range"] = [0.,0.]
        ordinary = _view(row,"range.equal",expected,variant="quiet")
        linked = _view(row,"equal.linked",expected,linked=True,variant="quiet")
        _same_rgb(ordinary,linked)
        _view(row,"equal.redraw",expected,linked=True,variant="quiet")
        base._text(row,"0")
    elif case == "M04":
        ordinary = _view(row,"max.ordinary",expected)
        linked = _view(row,"max.linked",expected,linked=True)
        _same_rgb(ordinary,linked)
        for c,label in ((1,"visible.hidden_active"),(0,"visible.hidden_first"),(2,"visible.empty")):
            expected["channels"][c]["visible"] = False
            _view(row,label,expected,linked=True)
        empty = _view(row,"visible.empty_redraw",expected,linked=True)
        assert all(p == 0 for image in empty[1]["orthogonal_planes"] for p in image["readback"]["rendered_rgb"]["pixels"])
        for ch in expected["channels"]:
            ch["visible"] = True
        restored = _view(row,"visible.restored",expected,linked=True)
        repeated = _view(row,"visible.restored_redraw",expected,linked=True)
        assert [p["readback"]["rendered_rgb"] for p in restored[1]["orthogonal_planes"]] == [
            p["readback"]["rendered_rgb"] for p in linked[1]["orthogonal_planes"]]
        assert [p["readback"] for p in restored[1]["orthogonal_planes"]] == [p["readback"] for p in repeated[1]["orthogonal_planes"]]
        base._selectors(row,*["channel_display.visible."+str(c) for c in range(3)])
    else:
        assert case == "M05"
        for label,mode,projection in (("mode.max","composite","max"),("mode.color","color","max"),
                ("mode.grayscale","grayscale","max"),("mode.sum","composite","sum"),("mode.max_again","composite","max")):
            expected.update(mode=mode,projection=projection)
            if label == "mode.sum":
                for ch in expected["channels"]:
                    ch["visible"] = True
            result = _view(row,label,expected)
            _mode_label(result[1],expected)
        base._selectors(row,"channel_display.mode.color","channel_display.mode.grayscale","channel_display.mode.composite")
    _stable_source(row,original,CHECKPOINTS[case])
    base._selectors(row,"menu.action.show_channel_display","channel_display.mode.composite_max")


def _transitions(row):
    for label in ("source","mixed","constant","last_step"):
        source = row["fixture_sources"][label]
        expected,navigation = _select_max(_initial_state("M06",label),source["display"]["c"])
        assert navigation == 0 and expected["projection"] == ("max" if label == "source" else "min")
        ordinary = _view(row,label+".ordinary",expected,document_id=source["document_id"])
        linked = _view(row,label+".linked",expected,linked=True,document_id=source["document_id"])
        _same_rgb(ordinary,linked)
        for result in (ordinary,linked):
            assert result[2]["display"]["c"] == 0 and [ch["visible"] for ch in expected["channels"]] == [True,False,True]
            assert [ch["range"] for ch in expected["channels"]] == [[1.,201.],[7.,219.],[11.,241.]]
            assert all(set(ch["lut"]) == {"table"} for ch in expected["channels"])
            _mode_label(result[1],expected)
            base._unchanged(source,result[2])
    twice,navigation = _select_max(expected,0)
    assert twice["projection"] == "max" and navigation == 0
    assert [ch["lut"]["table"] for ch in twice["channels"]] == [
        _initial_state("M06","last_step")["channels"][c]["lut"].get("table",
            _table("Red") if c == 0 else None) for c in range(3)]
    repeated = _view(row,"last_step.repeat",twice,linked=True,document_id=source["document_id"])
    _view(row,"last_step.repeat_redraw",twice,linked=True,document_id=source["document_id"])
    _mode_label(repeated[1],twice)
    for c in ("mixed","constant","last_step"):
        base._selectors(row,f"document.{row['fixture_sources'][c]['document_id']}")


def _previews(row):
    preparation = base._api(row,"threshold_overlay_preparation")
    assert preparation["accepted"] is True
    overlay = {"kind":"roi","selection":{"kind":"rectangle","points":[[1,0],[4,3]],"width":1,
        "imagejxx.roi_style":{"schema_version":1,"fill_color":[18,52,86,128],"stroke_color":None}},
        "t":1,"c":1,"z":2}
    assert preparation["overlay"] == overlay
    prepared = base._owner(preparation["after"],row["source_document_id"])
    assert prepared["overlays"] == [overlay]
    assert prepared["display"]["properties"]["manual_threshold"] == {
        "version":1,"lower":83.,"upper":85.,"t":1,"c":1,"z":2}
    generation,actual_staged = base._preview(row,"preview.ready")
    _view(row,"preview.ready",_state(),preview=actual_staged,generation=generation,threshold=True)
    expected,navigation = _select_max(_state(),1)
    assert navigation is None
    ordinary = _view(row,"max.ordinary",expected,preview=actual_staged,generation=generation,threshold=True)
    linked = _view(row,"preview.threshold_linked",expected,linked=True,preview=actual_staged,generation=generation,threshold=True)
    _same_rgb(ordinary,linked)
    xy,xz,yz = linked[1]["orthogonal_planes"]
    assert len(xy["overlay_layers"]) == 1 and xz["overlay_layers"] == yz["overlay_layers"] == []
    nonactive = _view(row,"preview.nonactive_linked",expected,linked=True,preview=actual_staged,generation=generation,threshold=True)
    live_raw = _view(row,"preview.nonactive_ordinary",expected,preview=actual_staged,generation=generation,threshold=True)
    assert nonactive[2]["display"]["c"] == live_raw[2]["display"]["c"] == 0
    assert live_raw[1]["ordinary_pixels"]["pixels_source"] == "live"
    assert live_raw[1]["ordinary_pixels"]["preview_generation"] == 0
    _same_rgb(live_raw,nonactive)
    away = _view(row,"preview.away",expected,linked=True)
    assert away[2]["display"]["crosshair"][2] == 3
    back = _view(row,"preview.back",expected,linked=True,preview=actual_staged,generation=generation)
    assert back[2]["display"]["crosshair"][2] == 2
    reopened = _view(row,"preview.panel_reopened",expected,linked=True,preview=actual_staged,generation=generation)
    assert reopened[1]["channel_display_panel"]["open"] is True
    base._cache_changed(nonactive[1],away[1])
    base._cache_changed(away[1],back[1])
    # Native overlay is tied to the active captured TCZ; a nonactive staged
    # contributor does not make the C1 XY annotation visible while C0 is active.
    for current in (nonactive,away,back,reopened):
        assert all(image["overlay_layers"] == [] for image in current[1]["orthogonal_planes"])
    before_jobs = linked[1]["gaussian_jobs"]
    for label in ("max.ordinary","preview.threshold_linked","preview.nonactive_linked","preview.nonactive_ordinary",
                  "preview.away","preview.back","preview.panel_reopened"):
        state,rendered,source = _snapshot(row,label)
        base._unchanged(prepared,source)
        jobs = rendered["gaussian_jobs"]
        assert jobs["active_installed_generation"] == generation and jobs["preview"]["owns_job"] is True
        assert jobs["preview"]["job"]["state"] == "ready"
        assert jobs["preview"]["job"]["id"] == before_jobs["preview"]["job"]["id"]
        assert state["usage"]["retained_bytes"] > 0
    other_id = row["fixture_sources"]["other"]["document_id"]
    other = _view(row,"preview.other_owner",_state(),document_id=other_id)
    returned = _view(row,"preview.returned",expected,linked=True)
    canceled_jobs = other[1]["gaussian_jobs"]
    for current in (other,returned):
        jobs = current[1]["gaussian_jobs"]
        assert current[0]["usage"] == base.ZERO_USAGE
        assert jobs["active_installed_generation"] is None and jobs["preview"]["owns_job"] is False
        assert jobs["preview"]["exit"] == "canceled" and jobs["preview"]["error"] == "Gaussian preview was canceled"
        assert jobs["preview"]["job"]["state"] == "canceled" and jobs["preview"]["job"]["cancel_requested"] is True
        assert jobs["preview"]["job"]["id"] == before_jobs["preview"]["job"]["id"]
        assert jobs["requested_generation"] == generation and jobs["rejected_generations"] == [] and jobs["rejected_count"] == 0
        assert current[1]["gaussian_panel"]["open"] is False and current[1]["gaussian_panel"]["exit_reason"] == "source_changed"
        assert base._typed(jobs) == base._typed(canceled_jobs)
        base._unchanged(prepared,base._owner(current[0],row["source_document_id"]))
    base._selectors(row,"channel_display.close","orthogonal.slice.XY",f"document.{other_id}",f"document.{row['source_document_id']}")


def _replay(row):
    source_id,other_id = row["source_document_id"],row["fixture_sources"]["other"]["document_id"]
    expected,navigation = _select_max(_initial_state("M08"),1)
    assert expected["projection"] == "min" and navigation == 0
    original,_,source = _snapshot(row,"record.original",expected)
    record = row["original_record"]
    assert row["original_owner_id"] == source_id and original["workflow"] == [record]
    _record(record,row["fixture_sources"]["source"],expected,action="select_max",navigation=0)
    navigated,_,fresh = _snapshot(row,"record.navigated",expected)
    assert [fresh["display"]["t"],fresh["display"]["c"],fresh["display"]["crosshair"][2]] == [0,2,0]
    assert navigated["workflow"] == [record]
    replayed,_,replayed_source = _snapshot(row,"record.replayed",expected)
    assert replayed["workflow"] == [record,record] and replayed_source["display"]["c"] == 0
    assert [replayed_source["display"]["t"],replayed_source["display"]["crosshair"][2]] == [0,0]
    base._unchanged(fresh,replayed_source)
    for key in ("t","crosshair","zoom","orthogonal","linked_zoom","plane_zoom","pan"):
        assert base._typed(replayed_source["display"][key]) == base._typed(fresh["display"][key])
    opened,rendered,other = _snapshot(row,"record.rebind_opened",_state(),document_id=other_id)
    assert rendered["channel_display_rebind"] == {"record":record,"document_id":other_id}
    assert rendered["channel_display_panel"]["source_id"] == other_id and opened["workflow"] == [record,record]
    rebound_state,effect = _select_max(_state(),1)
    assert effect is None and rebound_state["projection"] == "max"
    rebound,rebound_rendered,rebound_source = _snapshot(row,"record.rebound",rebound_state,document_id=other_id)
    assert row["rebind_owner_id"] == other_id and row["rebound_record"] == rebound["workflow"][2]
    assert rebound["workflow"][:2] == [record,record] and len(rebound["workflow"]) == 3
    _record(row["rebound_record"],other,rebound_state,action="select_max")
    assert row["rebound_record"]["source"]["id"] == other_id != record["source"]["id"]
    assert rebound_rendered["channel_display_rebind"] is None
    base._unchanged(other,rebound_source)
    off,_,off_source = _snapshot(row,"record.off_replayed",rebound_state,document_id=other_id)
    assert off["recording"] is False and off["workflow"] == rebound["workflow"]
    base._unchanged(rebound_source,off_source)
    base._unchanged(replayed_source,base._owner(off,source_id),display=True)
    final,_ = base._state(row,"final")
    assert final["recording"] is True and final["workflow"] == rebound["workflow"]
    base._selectors(row,"workflow.record","workflow.channel_display.0.replay","workflow.channel_display.0.rebind",
                    "workflow.channel_display.2.replay",f"document.{other_id}")


def _same_serialized_source(before, after):
    # NativeV3 creates a fresh owner and fresh retained pixel allocations.
    # data_identity is that owner's local data/history counter, not a global
    # allocation identity; two unedited decoded owners can both report zero.
    # Persistent values and every backing/declared bit remain exact below.
    for key in ("shape","dtype","sample_policy","calibration","metadata","selection","previous_selection","rois","overlays","spectrum_present"):
        assert base._typed(before[key]) == base._typed(after[key]), key
    for key in ("storage_f64_hex","declared_hex"):
        assert [p[key] for p in before["planes"]] == [p[key] for p in after["planes"]]
    assert base._typed(before["display"]["properties"]) == base._typed(after["display"]["properties"])
    assert before["document_id"] != after["document_id"]
    for source in (before,after):
        base._integer(source["data_identity"])
    for key in ("image_address","buffer_address"):
        original = {int(plane[key],16) for plane in before["planes"]}
        reopened = {int(plane[key],16) for plane in after["planes"]}
        assert original and reopened and all(address > 0 for address in original | reopened)
        assert original.isdisjoint(reopened)
    assert after["dirty"] is False and after["undo_steps"] == after["redo_steps"] == 0


def _persistence(row,directory):
    source_id = row["source_document_id"]
    expected = _state()
    expected["channels"][1]["range"] = [11.,91.]
    _,_,edited = _snapshot(row,"saved.v1_edited",expected)
    saved_v1,_,source_v1 = _snapshot(row,"saved.v1",expected)
    assert row["original_owner_id"] == source_id and row["v1_records"] == saved_v1["workflow"]
    assert len(row["v1_records"]) == 1 and row["v1_records"][0]["version"] == 1
    root = base._session(row,directory,"v1_session")
    assert len(root["documents"]) == 1 and root["workflow"] == row["v1_records"] and root["results"] == saved_v1["results"]
    base._session_document(root["documents"][0],source_v1)
    first_id = row["v1_reopened_owner_id"]
    opened,_,fresh = _snapshot(row,"saved.v1_reopened",expected,document_id=first_id)
    assert opened["active_document_id"] == first_id and opened["workflow"] == row["v1_records"]
    _same_serialized_source(source_v1,fresh)
    maximum,navigation = _select_max(expected,fresh["display"]["c"])
    assert navigation is None
    max_state,_,max_source = _snapshot(row,"saved.max",maximum,document_id=row["max_owner_id"])
    assert row["max_owner_id"] == first_id and len(max_state["workflow"]) == 2
    assert max_state["workflow"][0] == row["v1_records"][0]
    _record(max_state["workflow"][1],fresh,maximum,action="select_max")
    saved,_,saved_source = _snapshot(row,"saved",maximum,document_id=first_id)
    assert row["original_records"] == saved["workflow"] == max_state["workflow"]
    root = base._session(row,directory,"native_session")
    assert len(root["documents"]) == 1 and root["workflow"] == row["original_records"] and root["results"] == saved["results"]
    base._session_document(root["documents"][0],saved_source)
    opened,_,reopened = _snapshot(row,"reopened",maximum,document_id=row["reopened_owner_id"])
    assert opened["active_document_id"] == reopened["document_id"] and opened["workflow"] == row["original_records"]
    _same_serialized_source(saved_source,reopened)
    assert [r["source"]["id"] for r in opened["workflow"]] == [source_id,first_id]
    opaque = base._api(row,"future_native_roundtrip")
    assert opaque["accepted"] is True
    before,decoded = opaque["before_source"],opaque["decoded_source"]
    future = {"schema":"imagejxx.channel-display","version":37,"payload":[True,9007199254740993,"retained"]}
    assert base._stored(before) == base._stored(decoded) == future
    assert type(base._stored(decoded)["payload"][1]) is int
    base._literal(before);base._literal(decoded)
    future_root = base._session(row,directory,"future_session")
    assert len(future_root["documents"]) == 1
    base._session_document(future_root["documents"][0],before)
    for key in ("v1_session","native_session","future_session"):
        base._text(row,str(base._member(directory,row["files"][key]["member"])))
    refused,rendered = base._state(row,"future.refused")
    base._no_publication(opened,refused)
    assert rendered["error_modal"]["open"] is True and refused["window_error"]
    preparation = base._api(row,"legacy_projection_preparation")
    assert preparation["accepted"] is True and preparation["after"]["active_document_id"] == row["fixture_sources"]["legacy"]["document_id"]
    base._refused(row,"legacy_v2_refusal");base._refused(row,"detached_v2_refusal")
    assert row["forbidden_v2_exists"] is False and not (directory/"forbidden-v2.ijx").exists()
    _snapshot(row,"legacy.refused",_initial_state("M09","legacy"),row["fixture_sources"]["legacy"]["document_id"])
    base._selectors(row,"menu.path.save_document","menu.file.open","browser.path","browser.submit","error_modal.ok")


def _guards(row):
    source_id,other_id = row["source_document_id"],row["fixture_sources"]["other"]["document_id"]
    initial = _initial_state("M10")
    for label in ("draft.partial","draft.disabled_apply"):
        _,rendered,_ = _snapshot(row,label,initial)
        panel = rendered["channel_display_panel"]
        assert panel["minimum_text"] == "-" and panel["maximum_text"] == "219"
        assert panel["draft_valid"] is False and panel["draft_range"] is None and panel["range_error"]
        assert panel["control_states"]["channel_display.apply"]["enabled"] is False
    base._no_publication(base._state(row,"draft.partial")[0],base._state(row,"draft.disabled_apply")[0])
    minimum,effect = _select_max(initial,1)
    assert minimum["projection"] == "min" and effect == 0
    first,rendered,first_source = _snapshot(row,"draft.max",minimum)
    assert first_source["display"]["c"] == 0
    assert rendered["channel_display_panel"]["draft_valid"] is True
    assert rendered["channel_display_panel"]["draft_range"] == [1.,201.]
    record = row["guard_record"]
    assert first["workflow"] == [record]
    _record(record,row["fixture_sources"]["source"],minimum,action="select_max",navigation=0)
    maximum,effect = _select_max(minimum,0)
    assert maximum["projection"] == "max" and effect == 0
    second,rendered,second_source = _snapshot(row,"draft.max_again",maximum)
    assert second_source["display"]["c"] == 0 and len(second["workflow"]) == 2
    _mode_label(rendered,maximum)
    assert rendered["channel_display_panel"]["minimum_text"] == "-"
    assert rendered["channel_display_panel"]["draft_valid"] is False
    prepared = base._api(row,"queued_apply_preparation")
    assert prepared["accepted"] is True
    jobs = prepared["jobs"]["gaussian_jobs"]
    assert jobs["apply"]["owns_job"] is True and jobs["apply"]["job"]["state"] == "queued"
    assert jobs["apply"]["source_id"] == source_id
    busy,rendered,_ = _snapshot(row,"draft.busy_disabled",maximum)
    panel = rendered["channel_display_panel"]
    assert busy["busy"] is True and panel["busy"] is True and panel["minimum_text"] == "-"
    for selector in ("channel_display.mode","channel_display.apply","channel_display.reset_current"):
        assert panel["control_states"][selector]["enabled"] is False
        assert any(v["selector"] == selector and v["disabled_attempt"] for v in row["control_observations"])
    refusal = base._refused(row,"busy_select_max_refusal")
    assert refusal["before"]["busy"] is refusal["after"]["busy"] is True
    assert refusal["before"]["usage"] == refusal["after"]["usage"]
    coupled = base._refused(row,"coupled_min_without_c0_refusal")
    assert coupled["inputs"]["record"] == {**record,"navigation":None}
    assert record["state"]["projection"] == "min" and record["navigation"] == {"c":0}
    inactive = base._api(row,"inactive_select_max")
    assert inactive["accepted"] is True and inactive["before"]["active_document_id"] == inactive["after"]["active_document_id"] == source_id
    before_other,after_other = base._owner(inactive["before"],other_id),base._owner(inactive["after"],other_id)
    expected_other,effect = _select_max(_state(),1)
    assert effect is None
    _assert_stored(after_other,expected_other)
    base._unchanged(before_other,after_other)
    base._unchanged(base._owner(inactive["before"],source_id),base._owner(inactive["after"],source_id),display=True)
    assert inactive["after"]["workflow"][:-1] == inactive["before"]["workflow"]
    _record(inactive["after"]["workflow"][-1],before_other,expected_other,action="select_max")
    foreign = base._refused(row,"foreign_owner_refusal")
    assert foreign["inputs"]["record"] == record and foreign["before"]["active_document_id"] == source_id
    malformed = base._refused(row,"malformed_original_rebind_refusal")
    assert malformed["inputs"]["record"] == {**record,"unexpected":True}
    prepared = base._api(row,"fresh_display_intent_preparation")
    assert prepared["accepted"] is True
    before,after = base._owner(prepared["before"],source_id),base._owner(prepared["after"],source_id)
    base._unchanged(before,after)
    assert after["display"] == {**before["display"],"zoom":2.25,"revision":after["display"]["revision"]}
    assert after["revisions"]["display"] == before["revisions"]["display"]+1
    resolved,effect = _select_max(maximum,0)
    _record(prepared["record"],before,resolved,action="select_max",navigation=effect)
    base._refused(row,"stale_display_refusal")
    for name in ("processing_cap_refusal","processing_cancel_refusal","resolver_cap_refusal","resolver_cancel_refusal","resolver_callback_refusal"):
        refusal = base._refused(row,name)
        count = base._integer(refusal["checkpoint_count"])
        if name == "resolver_callback_refusal":
            assert count == 1 and refusal["error"] == "projection callback marker"
        else:
            assert count == 0
            if "_cancel_" in name:
                assert refusal["error_type"] == "operation_cancelled"
        assert "record" not in refusal and "returned_plane" not in refusal
    metadata = base._api(row,"stale_metadata_preparation")
    assert metadata["accepted"] is True
    a,b = base._owner(metadata["before"],source_id),base._owner(metadata["after"],source_id)
    assert b["metadata"]["properties"] == {**a["metadata"]["properties"],"stale":True}
    assert b["metadata"]["extensions"] == a["metadata"]["extensions"] and b["metadata"]["record_layout"] == a["metadata"]["record_layout"]
    assert b["revisions"]["metadata"] > a["revisions"]["metadata"] and b["revisions"]["pixels"] == a["revisions"]["pixels"]
    assert b["planes"] == a["planes"] and b["selection"] == a["selection"] and b["previous_selection"] == a["previous_selection"]
    base._refused(row,"stale_record_refusal")
    closed = base._api(row,"close_owner_preparation")
    assert closed["accepted"] is True and other_id not in closed["after"]["document_ids"]
    base._unchanged(base._owner(closed["before"],other_id),row["detached_sources"]["closed_other"],display=True)
    base._refused(row,"closed_source_refusal")
    complete,_,guarded = _snapshot(row,"guards.complete",maximum)
    assert complete["busy"] is False and complete["usage"] == base.ZERO_USAGE
    base._literal(guarded)
    for label in ("float64","rgb","rgba"):
        source = row["fixture_sources"][label]
        _,rendered,current = _snapshot(row,label+".legacy",document_id=source["document_id"])
        _assert_stored(current,None)
        base._legacy_view(rendered,current)
        base._unchanged(source,current,display=True)
        panel = rendered["channel_display_panel"]
        assert panel["open"] is True and panel["supported"] is False and panel["source_error"]
        assert panel["control_states"]["channel_display.enable"]["enabled"] is False
        base._refused(row,label+"_unsupported_set")
    many_id = row["fixture_sources"]["many"]["document_id"]
    many,effect = _select_max(_initial_state("M10","many"),8)
    assert effect is None and many["mode"] == "color" and many["version"] == 2 and many["projection"] == "max"
    _,rendered,many_source = _view(row,"count.fallback",many,document_id=many_id)
    assert many_source["display"]["c"] == 8 and len(many["channels"]) == 9
    _mode_label(rendered,many)
    base._unchanged(row["fixture_sources"]["many"],many_source)
    base._refused(row,"cardinality_refusal")
    returned,rendered,current = _snapshot(row,"owner.returned",maximum)
    assert returned["active_document_id"] == source_id and rendered["channel_display_panel"]["source_id"] == source_id
    panel = rendered["channel_display_panel"]
    assert panel["minimum_text"] == "1" and panel["maximum_text"] == "201" and panel["draft_range"] == [1.,201.]
    assert panel["draft_valid"] is True
    base._unchanged(guarded,current)
    base._text(row,"-")
    assert len([v for v in row["control_observations"] if v["selector"] == "channel_display.enable" and v["disabled_attempt"]]) == 3


def _assert_case(row,case,context,directory):
    assert hashlib.sha256(Path(reference.__file__).read_bytes()).hexdigest() == REFERENCE_READER_SHA
    assert row["schema_version"] == 1 and row["contract_id"] == CONTRACT
    assert row["case_id"] == case and row["context"] == context
    assert row["acceptance_claim"] is False and row["observation_state"] == "observed"
    assert not ({"pass","passed","expected","expected_rgb","expected_state"} & row.keys())
    assert row["limits"] == {"max_frames":180,"max_seconds":15,"max_report_bytes":MAX_REPORT,
        "max_archive_bytes":MAX_ARCHIVE,"max_documents":16,"max_shape_tczyxs":[2,9,4,3,5,4]}
    assert 0 <= base._number(row["elapsed_seconds"]) <= 15
    assert row["fixture_recipe"] == "40*t+30*c+5*z+2*y+x+2*s"
    base._integer(row["source_document_id"],minimum=1)
    _physical(row)
    _physical_actions(row)
    _scrolls(row)
    _fixtures(row,case)
    checkpoints = ("initial",*CHECKPOINTS[case],"final")
    assert set(row["states"]) == set(checkpoints)
    ordinals = [row["states"][name]["frame_ordinal"] for name in checkpoints]
    assert ordinals == sorted(set(ordinals))
    for name in checkpoints:
        state,_ = base._state(row,name)
        for owner in state["documents"]:
            base._plane_rows(owner)
            assert owner["calibration"] == base.CALIBRATION
    assert [v["name"] for v in row["api_observations"]] == ["fixed_fixture_preparation",*API_LABELS.get(case,())]
    assert row["api_observations"][0]["accepted"] is True
    assert [v["label"] for v in row["preview_observations"]] == (["preview.ready"] if case == "M07" else [])
    assert set(row["files"]) == ({"v1_session","native_session","future_session"} if case == "M09" else set())
    archive_bytes = 0
    for name in row["files"]:
        archive_bytes += len(base._file(row,directory,name))
    assert archive_bytes <= MAX_ARCHIVE
    assert set(row["detached_sources"]) == ({"closed_other"} if case == "M10" else set())
    if case in {"M01","M02","M03","M04","M05"}:
        _basic(row,case)
    elif case == "M06":
        _transitions(row)
    elif case == "M07":
        _previews(row)
    elif case == "M08":
        _replay(row)
    elif case == "M09":
        _persistence(row,directory)
    else:
        assert case == "M10"
        _guards(row)
    final,_ = base._state(row,"final")
    assert row["window_error"] == "" and row["final_usage"] == base.ZERO_USAGE and final["busy"] is False


@pytest.mark.parametrize("context",CONTEXTS)
@pytest.mark.parametrize("case_id",CASES)
def test_installed_channel_display_projection(case_id,context,request,tmp_path):
    row,directory = _observe(case_id,context,tmp_path/"observations",request)
    _assert_case(row,case_id,context,directory)
