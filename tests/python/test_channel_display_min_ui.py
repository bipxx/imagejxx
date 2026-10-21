"""Independent P06c3 SelectMin assertions for both shared ImGui launches.

Owned logical-state intent follows adoption001/002, including native Gray
Min/Min and one Display publication for every admitted repeated press. Reused
P06c/P06c2 helpers contain only qualified test-owned transport, literal processor
mapping, raw READY-input and preservation checks. No product computes an oracle.
"""
import copy
import hashlib
import math
import os
from pathlib import Path
import struct

import pytest
import test_channel_display_ui as base
import test_channel_display_projection_ui as projection

CONTRACT = "p06c3.channel-display-min-ui.001"
CASES = tuple(f"N{i:02d}" for i in range(1,11))
CONTEXTS = ("imgui_cpp","imgui_python")
EXPECTED_COLLECTION_COUNT = 20
EXPECTED_COLLECTION_IDS = tuple(f"test_installed_channel_display_min[{case}-{context}]"
                                for case in CASES for context in CONTEXTS)
ADOPTION_SHA = "7020850a0678b3110349543d419f9b5c530d4eff8c2f3f1055c2c80a64f6b77f"
PUBLICATION_ADOPTION_SHA = "3af40f5c27c7747727752c9f45f831da3e7a963e3b39ba05f720b93e7d9c24f4"
PROJECTION_READER_SHA = "6e75246385a0d960d6660f69ce021d74f58a2066f4b75840c902254151cbc80c"
MAX_REPORT,MAX_ARCHIVE = 16*1024**2,32*1024**2
VIEWPORT = (1250,820)
CHECKPOINTS = {
    "N01":("sum.ordinary","sum.linked","min.ordinary","min.linked","min.redraw","min.repeat","min.repeat_redraw"),
    "N02":("min.ordinary","range.c1","range.c0","lut.c0","navigation.changed","min.linked","min.redraw"),
    "N03":("min.ordinary","min.linked","range.equal","equal.linked","equal.redraw"),
    "N04":("min.ordinary","min.linked","visible.hidden_active","visible.hidden_first","visible.empty",
           "visible.empty_redraw","visible.restored","visible.restored_redraw"),
    "N05":("mode.min","mode.color","mode.color_min","mode.grayscale","mode.grayscale_min","mode.sum","mode.min_again"),
    "N06":("source.ordinary","source.linked","mixed.ordinary","mixed.linked","mixed.repeat","mixed.repeat_redraw",
           "constant.ordinary","constant.linked","last_step.ordinary","last_step.linked","last_step.repeat","last_step.repeat_redraw",
           "color.ordinary","color.linked","gray.ordinary","gray.linked","gray.repeat","gray.repeat_redraw",
           "color_inverted.ordinary","color_inverted.linked"),
    "N07":("preview.ready","min.ordinary","preview.threshold_linked","preview.nonactive_linked","preview.nonactive_ordinary",
           "preview.away","preview.back","preview.panel_reopened","preview.other_owner","preview.returned"),
    "N08":("record.original","record.navigated","record.replayed","record.rebind_opened","record.rebound","record.off_replayed"),
    "N09":("saved.v1_edited","saved.v1","saved.v1_reopened","saved.min","saved","reopened","future.refused","legacy.refused"),
    "N10":("draft.partial","draft.disabled_apply","draft.min","draft.min_again","draft.busy_disabled","guards.complete",
           "float64.legacy","rgb.legacy","rgba.legacy","count.single_ordinary","count.single_linked","count.single_redraw",
           "count.eight_ordinary","count.eight_linked","count.eight_redraw","count.fallback","count.fallback_linked","owner.returned"),
}
API_LABELS = {
    "N07":("threshold_overlay_preparation",),
    "N09":("future_native_roundtrip","legacy_projection_preparation","legacy_v2_refusal","detached_v2_refusal"),
    "N10":("queued_apply_preparation","busy_select_min_refusal","coupled_max_without_c0_refusal","inactive_select_min",
        "foreign_owner_refusal","malformed_original_rebind_refusal","fresh_display_intent_preparation","stale_display_refusal",
        "processing_cap_refusal","processing_cancel_refusal","resolver_cap_refusal","resolver_cancel_refusal","resolver_callback_refusal",
        "stale_metadata_preparation","stale_record_refusal","close_owner_preparation","closed_source_refusal",
        "float64_unsupported_set","rgb_unsupported_set","rgba_unsupported_set","cardinality_refusal"),
}
FIXTURE_LABELS = {case:("source",) for case in CASES}
FIXTURE_LABELS.update({
    "N06":("source","mixed","constant","last_step","color","gray","color_inverted"),
    "N07":("source","other"),"N08":("source","other"),"N09":("source","legacy"),
    "N10":("source","other","float64","rgb","rgba","many","single","eight"),
})
SELECTOR_MEMBERSHIP = {
    "N01":("channel_display.mode.composite_min","orthogonal.toggle"),
    "N02":("channel_display.mode.composite_min","channel_display.channel.1","channel_display.channel.0",
           "channel_display.channel.2","channel_display.lut.Magenta","navigation.time","image.slice"),
    "N03":("channel_display.mode.composite_min","channel_display.channel.1","channel_display.apply"),
    "N04":("channel_display.mode.composite_min","channel_display.visible.0","channel_display.visible.1","channel_display.visible.2"),
    "N05":("channel_display.mode.composite_min","channel_display.mode.color","channel_display.mode.grayscale","channel_display.mode.composite"),
    "N06":("channel_display.mode.composite_min","orthogonal.toggle"),
    "N07":("channel_display.mode.composite_min","gaussian.preview","gaussian.sigma","channel_display.close","orthogonal.slice.XY"),
    "N08":("channel_display.mode.composite_min","workflow.channel_display.0.replay","workflow.channel_display.0.rebind",
           "workflow.channel_display.2.replay","workflow.record"),
    "N09":("channel_display.mode.composite_min","menu.path.save_document","menu.file.open","browser.path","browser.submit","error_modal.ok"),
    "N10":("channel_display.mode.composite_min","channel_display.apply","channel_display.reset_current","channel_display.enable"),
}
DISABLED_CONTROL_COUNTS = {
    "N10":{"channel_display.mode":1,"channel_display.apply":2,
           "channel_display.reset_current":1,"channel_display.enable":3},
}
METADATA_KEYS = {"source_only","revision_probe"}
FILES = {case:() for case in CASES}
FILES["N09"] = ("v1_session","native_session","future_session")
TABLE_ENCODING = "observed-lut-table-refs.001"

# Generic test-owned transport and literal numerical checks. The Min intent
# classifier, transform and scenario states below are independent of this reuse.
_state = projection._state
_table = projection._table
_assert_stored = projection._assert_stored
_snapshot = projection._snapshot
_view = projection._view
_mode_label = projection._mode_label
_same_rgb = projection._same_rgb
_stable_source = projection._stable_source
_same_serialized_source = projection._same_serialized_source
installed_cpp_executable = projection.installed_cpp_executable


@pytest.fixture(scope="module")
def standalone_cases(installed_cpp_executable):
    configured = os.environ.get("IMAGEJXX_CHANNEL_DISPLAY_MIN_CPP_REPORT")
    assert configured, "Set IMAGEJXX_CHANNEL_DISPLAY_MIN_CPP_REPORT to the installed path-only N index"
    path = Path(configured).resolve(strict=True)
    index = base._read(path)
    assert index["contract_id"] == CONTRACT and index["context"] == "imgui_cpp"
    assert type(index["case_count"]) is int and index["case_count"] == 10
    assert index["observation_state"] == "observed" and index["acceptance_claim"] is False
    assert 0 <= base._number(index["elapsed_seconds"]) <= 60
    assert len(index["cases"]) == 10
    result = {}
    for entry in index["cases"]:
        assert set(entry) <= {"case_id","id","relative_path","byte_count","sha256"}
        case = entry["case_id"]
        assert case == entry["id"] and case in CASES and case not in result
        member = base._member(path.parent,entry["relative_path"])
        raw = member.read_bytes()
        assert 0 < len(raw) <= MAX_REPORT and len(raw) == entry["byte_count"]
        assert hashlib.sha256(raw).hexdigest() == entry["sha256"]
        result[case] = member
    assert tuple(result) == CASES and len(set(result.values())) == 10
    return result


def _inverted(table):
    return all(sum(table[i]) <= sum(table[i-1]) for i in range(1,255))


def _decode_observed_tables(raw):
    """Expand lossless actual observations before any behavioral assertion."""
    assert raw["table_encoding"] == TABLE_ENCODING
    pool = raw["observed_lut_tables"]
    assert isinstance(pool,list)
    identities = []
    for table in pool:
        assert isinstance(table,list) and len(table) == 256
        assert all(isinstance(row,list) and len(row) == 3 and
                   all(type(value) is int and 0 <= value <= 255 for value in row) for row in table)
        identities.append(bytes(value for row in table for value in row))
    assert len(set(identities)) == len(pool)
    result = copy.deepcopy(raw)
    del result["table_encoding"],result["observed_lut_tables"]
    used = set()

    def visit(node,lut=False):
        if isinstance(node,dict):
            assert "observed_table_ref" not in node
            if lut and "table" in node:
                assert set(node) == {"table"}
                reference = node["table"]
                assert isinstance(reference,dict) and set(reference) == {"observed_table_ref"}
                index = reference["observed_table_ref"]
                assert type(index) is int and 0 <= index < len(pool)
                used.add(index)
                # Observations are read-only. Share each validated actual table
                # so lossless expansion does not allocate a copy per frame.
                node["table"] = pool[index]
            else:
                for key,value in node.items():
                    visit(value,key == "lut")
        elif isinstance(node,list):
            for child in node:
                visit(child)

    visit(result)
    assert used == set(range(len(pool)))
    return result


def _select_min(state, active):
    """Adopted logical-state policy; no Java cached-processor emulation."""
    result = copy.deepcopy(state)
    gray = state["mode"] == "grayscale" or (state["mode"] == "composite" and len(state["channels"]) == 1)
    selected = _table("gray") if gray else state["channels"][active]["lut"].get("table")
    if selected is None:
        selected = _table(state["channels"][active]["lut"]["name"])
    navigation = None
    result.update(version=2,projection="min",mode="composite" if len(result["channels"]) <= 8 else "color")
    if not _inverted(selected):
        for channel in result["channels"]:
            table = channel["lut"].get("table")
            if table is None:
                table = _table(channel["lut"]["name"])
            channel["lut"] = {"table":[[v+255-min(row)-max(row) for v in row] for row in table]}
        navigation = 0
        if not _inverted(result["channels"][0]["lut"]["table"]):
            result["projection"] = "max"
    if state["mode"] != "composite":
        for channel in result["channels"]:
            channel["visible"] = True
    return result,navigation


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
            {"revision":source["revisions"]["display"]} if action == "select_min" else None)
        assert base._typed(record["navigation"]) == base._typed({"c":navigation} if navigation is not None else None)
        if action == "select_min" and expected["projection"] == "max":
            assert navigation == 0


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
        if selector == "channel_display.mode.composite_min":
            expected,navigation = _select_min(expected,source["display"]["c"])
            action = "select_min"
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
        assert result["revisions"]["display"] == source["revisions"]["display"]+1
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


def _physical(row):
    assert len(row["drag_observations"]) == (1 if row["case_id"] == "N07" else 0)
    base._physical(dict(row,case_id="C04B" if row["case_id"] == "N07" else row["case_id"]))
    for item in row["control_observations"]:
        assert all(0 <= base._number(v) < bound for v,bound in zip(item["position"],VIEWPORT))
    for frame in row["frames"]:
        for event in frame["requested_events"]:
            if event["type"] == "wheel":
                assert row["case_id"] == "N08" and frame["label"] in {"record.off_scroll.down","record.off_scroll.up"}
                assert event == {"type":"wheel","x":0,"y":-3 if frame["label"].endswith("down") else 3}
    projection._scrolls(dict(row,case_id="M08" if row["case_id"] == "N08" else row["case_id"]))
    assert [item["selector"] for item in row["control_observations"]] == _control_sequence(row)


def _control_sequence(row):
    """Frozen pointer-control input timeline, independent of action results."""
    case = row["case_id"]
    result = []

    def click(*names):
        result.extend(names)

    def mode(name="composite_min"):
        click("channel_display.mode","channel_display.mode."+name)

    def panel():
        click("menu.Image","menu.action.show_channel_display")

    def channel(c):
        click("channel_display.channel","channel_display.channel."+str(c))

    def ranges():
        click("channel_display.minimum","channel_display.maximum","channel_display.apply")

    def tab(label):
        click("document."+str(row["fixture_sources"][label]["document_id"]))

    def browser(action):
        click("menu.File","menu.path.save_document" if action == "save" else "menu.file.open",
              "browser.path","browser.submit")

    if case == "N07":
        click("menu.Process","menu.Process.Filters","menu.command.gaussian_blur_imagej","gaussian.sigma","gaussian.preview")
    panel()
    if case == "N01":
        click("orthogonal.toggle","orthogonal.toggle")
        mode();click("orthogonal.toggle");mode()
    elif case == "N02":
        mode();channel(1);ranges();channel(0);ranges()
        click("channel_display.lut","channel_display.lut.Magenta","navigation.time")
        channel(2);click("image.slice","orthogonal.toggle")
    elif case == "N03":
        mode();click("orthogonal.toggle","orthogonal.toggle");channel(1);ranges();click("orthogonal.toggle")
    elif case == "N04":
        mode();click("orthogonal.toggle")
        click(*["channel_display.visible."+str(c) for c in (1,0,2,0,1,2)])
    elif case == "N05":
        for name in ("composite_min","color","composite_min","grayscale","composite_min","composite","composite_min"):
            mode(name)
    elif case == "N06":
        for label in FIXTURE_LABELS[case]:
            if label != "source":
                tab(label)
            mode();click("orthogonal.toggle")
            if label in {"mixed","last_step","gray"}:
                mode()
    elif case == "N07":
        mode();channel(1);click("orthogonal.toggle");channel(0)
        click("orthogonal.toggle","orthogonal.toggle","channel_display.close",
              "orthogonal.slice.XY","orthogonal.slice.XY")
        panel();tab("other");tab("source")
    elif case == "N08":
        mode();click("navigation.time");channel(2)
        click("image.slice","workflow.channel_display.0.replay");tab("other")
        click("workflow.channel_display.0.rebind");mode()
        click("workflow.record","workflow.channel_display.2.replay","workflow.record")
    elif case == "N09":
        ranges();browser("save");browser("open");mode()
        browser("save");browser("open");browser("open")
        click("error_modal.ok","browser.cancel")
    else:
        assert case == "N10"
        click("channel_display.minimum","channel_display.apply");mode()
        click("channel_display.minimum");mode()
        click("channel_display.mode","channel_display.apply","channel_display.reset_current")
        for label in ("float64","rgb","rgba"):
            tab(label);click("channel_display.enable")
        for label in ("single","eight","many"):
            tab(label);mode();click("orthogonal.toggle")
        tab("source")
    return result


def _initial_state(case,label="source"):
    state = _state()
    if case == "N03":
        state["channels"][1]["lut"] = {"table":_table("custom_a")}
    if case == "N04":
        for channel,name in zip(state["channels"],("custom_a","constant","custom_c")):
            channel["lut"] = {"table":_table(name)}
    if case == "N05":
        state["channels"][1]["visible"] = False
    if case == "N06" or case in {"N08","N10"} and label == "source":
        variant = label if case == "N06" else "mixed"
        tables = {"source":("inverted_red","inverted_yellow","inverted_cyan"),
                  "mixed":("inverted_red","Yellow","custom_c"),
                  "constant":("Red","constant","custom_b"),
                  "last_step":("Red","last_step_only","custom_a"),
                  "color":("Red","Yellow","Cyan"),
                  "gray":("Red","inverted_yellow","custom_c"),
                  "color_inverted":("Red","inverted_yellow","custom_c")}[variant]
        for channel,name,bounds in zip(state["channels"],tables,([1.,201.],[7.,219.],[11.,241.])):
            channel["range"] = bounds
            channel["lut"] = {"name":name} if name in base.MASKS else {"table":_table(name)}
        for channel,visible in zip(state["channels"],(True,False,True)):
            channel["visible"] = visible
        if variant in {"source","mixed","color","gray","color_inverted"}:
            state.update(version=2,projection="max")
        if variant in {"color","color_inverted","gray"}:
            state["mode"] = "grayscale" if variant == "gray" else "color"
            for channel,visible in zip(state["channels"],(True,False,True) if variant == "gray" else (False,True,False)):
                channel["visible"] = visible
    if case == "N09" and label == "legacy":
        state = _state(1,version=2,projection="min")
    if case == "N10" and label == "many":
        state = _state(9,mode="color",bounds=(0.,511.))
        for c,channel in enumerate(state["channels"]):
            channel["lut"] = {"name":("Red","Green","Blue")[c%3]}
    if case == "N10" and label == "eight":
        state = _state(8,bounds=(0.,511.))
    if case == "N10" and label == "single":
        state = _state(1)
        state["channels"][0]["lut"] = {"table":_table("custom_a")}
    if case == "N10" and label in {"float64","rgb","rgba"}:
        state = None
    return state


def _literal(document, *, variant="finite"):
    # C8 is new to this packet. Preserve the frozen P06c helper's admission;
    # independently check every C8 sample in both native transport encodings.
    if document["shape"][1] != 8:
        return base._literal(document,variant=variant)
    assert document["shape"] == [2,8,4,3,5,1] and document["dtype"] == "uint16"
    assert variant == "finite" and document["sample_policy"] == "finite_only"
    assert document["calibration"] == base.CALIBRATION
    planes = base._plane_rows(document)
    positions = [(t,c,z) for t in range(2) for c in range(8) for z in range(4)]
    assert list(planes) == positions and len(planes) == 64
    for t,c,z in positions:
        samples = [40*t+30*c+5*z+2*y+x for y in range(3) for x in range(5)]
        assert len(samples) == 15
        plane = planes[t,c,z]
        assert plane["declared_hex"] == b"".join(struct.pack("<H",value) for value in samples).hex()
        assert plane["storage_f64_hex"] == b"".join(struct.pack("<d",value) for value in samples).hex()


def _fixtures(row,case):
    labels = set(FIXTURE_LABELS[case])
    assert set(row["fixture_sources"]) == labels
    initial,_ = base._state(row,"initial")
    assert initial["workflow"] == initial["results"] == [] and initial["recording"] is True
    assert row["fixture_sources"]["source"]["document_id"] == row["source_document_id"]
    for label,source in row["fixture_sources"].items():
        shape,dtype = [2,3,4,3,5,1],("uint8" if case in {"N01","N06"} else "uint16" if case == "N02" else "float32")
        if case == "N09" and label == "legacy":
            shape = [1,1,4,3,5,1]
        if case == "N10" and label in {"float64","rgb","rgba"}:
            shape,dtype = [1,1,4,3,5,{"float64":1,"rgb":3,"rgba":4}[label]],{"float64":"float64","rgb":"uint8","rgba":"float32"}[label]
        if case == "N10" and label == "single":
            shape,dtype = [1,1,4,3,5,1],"float32"
        if case == "N10" and label == "eight":
            shape,dtype = [2,8,4,3,5,1],"uint16"
        if case == "N10" and label == "many":
            shape,dtype = [2,9,4,3,5,1],"uint16"
        assert source["shape"] == shape and source["dtype"] == dtype
        expected_c = 8 if label == "many" else 2 if case == "N06" and label == "source" else 1 if shape[1] > 1 else 0
        assert [source["display"]["t"],source["display"]["c"],source["display"]["crosshair"]] == [
            1 if shape[0] > 1 else 0,expected_c,[1,1,2]]
        _literal(source,variant="quiet" if case == "N03" else "finite")
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


def _basic(row, case):
    original = row["fixture_sources"]["source"]
    expected,navigation = _select_min(_initial_state(case),original["display"]["c"])
    assert navigation == (None if case == "N04" else 0) and expected["projection"] == "min"
    if case == "N01":
        ordinary = _view(row,"sum.ordinary",_state())
        linked = _view(row,"sum.linked",_state(),linked=True)
        _same_rgb(ordinary,linked)
        maximum = _view(row,"min.ordinary",expected)
        assert maximum[1]["ordinary_pixels"]["readback"]["rendered_rgb"] != ordinary[1]["ordinary_pixels"]["readback"]["rendered_rgb"]
        linked = _view(row,"min.linked",expected,linked=True)
        _same_rgb(maximum,linked)
        repeated = _view(row,"min.redraw",expected,linked=True)
        for old,new in zip(linked[1]["orthogonal_planes"],repeated[1]["orthogonal_planes"]):
            assert old["readback"] == new["readback"] and old["builds"] == new["builds"]
        twice,effect = _select_min(expected,0)
        assert twice == expected and effect is None
        _view(row,"min.repeat",twice,linked=True)
        _view(row,"min.repeat_redraw",twice,linked=True)
    elif case == "N02":
        _view(row,"min.ordinary",expected)
        expected["channels"][1]["range"] = [11.,91.]
        _view(row,"range.c1",expected)
        expected["channels"][0]["range"] = [5.,101.]
        _view(row,"range.c0",expected)
        expected["channels"][0]["lut"] = {"name":"Magenta"}
        _view(row,"lut.c0",expected)
        changed = _view(row,"navigation.changed",expected)
        assert [changed[2]["display"]["t"],changed[2]["display"]["c"],changed[2]["display"]["crosshair"]] == [0,2,[1,1,3]]
        linked = _view(row,"min.linked",expected,linked=True)
        _same_rgb(changed,linked)
        _view(row,"min.redraw",expected,linked=True)
        base._selectors(row,"channel_display.channel.0","channel_display.channel.2","channel_display.lut.Magenta",
                        "navigation.time","image.slice")
        for text in ("11","91","5","101","1","4"):
            base._text(row,text)
    elif case == "N03":
        ordinary = _view(row,"min.ordinary",expected,variant="quiet")
        linked = _view(row,"min.linked",expected,linked=True,variant="quiet")
        _same_rgb(ordinary,linked)
        expected["channels"][1]["range"] = [0.,0.]
        ordinary = _view(row,"range.equal",expected,variant="quiet")
        linked = _view(row,"equal.linked",expected,linked=True,variant="quiet")
        _same_rgb(ordinary,linked)
        _view(row,"equal.redraw",expected,linked=True,variant="quiet")
        base._text(row,"0")
    elif case == "N04":
        ordinary = _view(row,"min.ordinary",expected)
        linked = _view(row,"min.linked",expected,linked=True)
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
        assert case == "N05"
        active = 0
        for label,operation in (("mode.min",None),("mode.color","color"),("mode.color_min","select_min"),
                ("mode.grayscale","grayscale"),("mode.grayscale_min","select_min"),("mode.sum","composite"),
                ("mode.min_again","select_min")):
            if operation == "select_min":
                expected,effect = _select_min(expected,active)
                if effect is not None:
                    active = effect
            elif operation:
                expected["mode"] = operation
                if operation == "composite":
                    expected["projection"] = "sum"
                    for channel in expected["channels"]:
                        channel["visible"] = True
            result = _view(row,label,expected)
            assert result[2]["display"]["c"] == active
            _mode_label(result[1],expected)
        base._selectors(row,"channel_display.mode.color","channel_display.mode.grayscale","channel_display.mode.composite")
    _stable_source(row,original,CHECKPOINTS[case])
    base._selectors(row,"menu.action.show_channel_display","channel_display.mode.composite_min")
    _view(row,"final",expected,linked=case in {"N01","N02","N03","N04"},variant="quiet" if case == "N03" else "finite")


def _transitions(row):
    # Every expected state originates in the literal fixture definition above.
    # Native Gray is deliberately Min/Min, independent of Java ip cache history.
    for label in ("source","mixed","constant","last_step","color","gray","color_inverted"):
        source = row["fixture_sources"][label]
        expected,effect = _select_min(_initial_state("N06",label),source["display"]["c"])
        assert expected["projection"] == ("max" if label == "mixed" else "min")
        assert effect == (None if label in {"source","constant","last_step","color_inverted"} else 0)
        active = source["display"]["c"] if effect is None else effect
        ordinary = _view(row,label+".ordinary",expected,document_id=source["document_id"])
        linked = _view(row,label+".linked",expected,linked=True,document_id=source["document_id"])
        _same_rgb(ordinary,linked)
        for result in (ordinary,linked):
            assert result[2]["display"]["c"] == active
            assert [channel["range"] for channel in expected["channels"]] == [[1.,201.],[7.,219.],[11.,241.]]
            assert [channel["visible"] for channel in expected["channels"]] == (
                [True,True,True] if label in {"color","gray","color_inverted"} else [True,False,True])
            _mode_label(result[1],expected)
            base._unchanged(source,result[2])
        if label in {"mixed","last_step","gray"}:
            twice,second_effect = _select_min(expected,active)
            assert twice["projection"] == "min"
            if label == "gray":
                assert twice == expected and second_effect is None
            if label == "last_step":
                assert twice == expected and second_effect is None
            if label == "mixed":
                assert second_effect == 0 and twice["channels"][0]["lut"]["table"] == _table("inverted_red")
            _view(row,label+".repeat",twice,linked=True,document_id=source["document_id"])
            _view(row,label+".repeat_redraw",twice,linked=True,document_id=source["document_id"])
    _view(row,"final",expected,linked=True,document_id=source["document_id"])
    for label in FIXTURE_LABELS["N06"][1:]:
        base._selectors(row,f"document.{row['fixture_sources'][label]['document_id']}")


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
    expected,navigation = _select_min(_state(),1)
    assert navigation == 0
    ordinary = _view(row,"min.ordinary",expected,preview=actual_staged,generation=generation,threshold=True)
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
    for label in ("min.ordinary","preview.threshold_linked","preview.nonactive_linked","preview.nonactive_ordinary",
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
    _view(row,"final",expected,linked=True)
    base._selectors(row,"channel_display.close","orthogonal.slice.XY",f"document.{other_id}",f"document.{row['source_document_id']}")


def _replay(row):
    source_id,other_id = row["source_document_id"],row["fixture_sources"]["other"]["document_id"]
    expected,navigation = _select_min(_initial_state("N08"),1)
    assert expected["projection"] == "max" and navigation == 0
    original,_,source = _view(row,"record.original",expected)
    record = row["original_record"]
    assert row["original_owner_id"] == source_id and original["workflow"] == [record]
    _record(record,row["fixture_sources"]["source"],expected,action="select_min",navigation=0)
    navigated,_,fresh = _view(row,"record.navigated",expected)
    assert [fresh["display"]["t"],fresh["display"]["c"],fresh["display"]["crosshair"][2]] == [0,2,0]
    assert navigated["workflow"] == [record]
    replayed,_,replayed_source = _view(row,"record.replayed",expected)
    assert replayed["workflow"] == [record,record] and replayed_source["display"]["c"] == 0
    assert [replayed_source["display"]["t"],replayed_source["display"]["crosshair"][2]] == [0,0]
    base._unchanged(fresh,replayed_source)
    for key in ("t","crosshair","zoom","orthogonal","linked_zoom","plane_zoom","pan"):
        assert base._typed(replayed_source["display"][key]) == base._typed(fresh["display"][key])
    opened,rendered,other = _view(row,"record.rebind_opened",_state(),document_id=other_id)
    assert rendered["channel_display_rebind"] == {"record":record,"document_id":other_id}
    assert rendered["channel_display_panel"]["source_id"] == other_id and opened["workflow"] == [record,record]
    rebound_state,effect = _select_min(_state(),1)
    assert effect == 0 and rebound_state["projection"] == "min"
    rebound,rebound_rendered,rebound_source = _view(row,"record.rebound",rebound_state,document_id=other_id)
    assert row["rebind_owner_id"] == other_id and row["rebound_record"] == rebound["workflow"][2]
    assert rebound["workflow"][:2] == [record,record] and len(rebound["workflow"]) == 3
    _record(row["rebound_record"],other,rebound_state,action="select_min",navigation=0)
    assert row["rebound_record"]["source"]["id"] == other_id != record["source"]["id"]
    assert rebound_rendered["channel_display_rebind"] is None
    base._unchanged(other,rebound_source)
    off,_,off_source = _view(row,"record.off_replayed",rebound_state,document_id=other_id)
    assert off["recording"] is False and off["workflow"] == rebound["workflow"]
    base._unchanged(rebound_source,off_source)
    base._unchanged(replayed_source,base._owner(off,source_id),display=True)
    final,_ = base._state(row,"final")
    assert final["recording"] is True and final["workflow"] == rebound["workflow"]
    _view(row,"final",rebound_state,document_id=other_id)
    base._selectors(row,"workflow.record","workflow.channel_display.0.replay","workflow.channel_display.0.rebind",
                    "workflow.channel_display.2.replay",f"document.{other_id}")



def _persistence(row,directory):
    source_id = row["source_document_id"]
    expected = _state()
    expected["channels"][1]["range"] = [11.,91.]
    _,_,edited = _view(row,"saved.v1_edited",expected)
    saved_v1,_,source_v1 = _view(row,"saved.v1",expected)
    assert row["original_owner_id"] == source_id and row["v1_records"] == saved_v1["workflow"]
    assert len(row["v1_records"]) == 1 and row["v1_records"][0]["version"] == 1
    root = base._session(row,directory,"v1_session")
    assert len(root["documents"]) == 1 and root["workflow"] == row["v1_records"] and root["results"] == saved_v1["results"]
    base._session_document(root["documents"][0],source_v1)
    first_id = row["v1_reopened_owner_id"]
    opened,_,fresh = _view(row,"saved.v1_reopened",expected,document_id=first_id)
    assert opened["active_document_id"] == first_id and opened["workflow"] == row["v1_records"]
    _same_serialized_source(source_v1,fresh)
    maximum,navigation = _select_min(expected,fresh["display"]["c"])
    assert navigation == 0
    max_state,_,max_source = _view(row,"saved.min",maximum,document_id=row["min_owner_id"])
    assert row["min_owner_id"] == first_id and len(max_state["workflow"]) == 2
    assert max_state["workflow"][0] == row["v1_records"][0]
    _record(max_state["workflow"][1],fresh,maximum,action="select_min",navigation=0)
    saved,_,saved_source = _view(row,"saved",maximum,document_id=first_id)
    assert row["original_records"] == saved["workflow"] == max_state["workflow"]
    root = base._session(row,directory,"native_session")
    assert len(root["documents"]) == 1 and root["workflow"] == row["original_records"] and root["results"] == saved["results"]
    base._session_document(root["documents"][0],saved_source)
    opened,_,reopened = _view(row,"reopened",maximum,document_id=row["reopened_owner_id"])
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
    _view(row,"future.refused",maximum,document_id=reopened["document_id"])
    assert rendered["error_modal"]["open"] is True and refused["window_error"]
    preparation = base._api(row,"legacy_projection_preparation")
    assert preparation["accepted"] is True and preparation["after"]["active_document_id"] == row["fixture_sources"]["legacy"]["document_id"]
    base._refused(row,"legacy_v2_refusal");base._refused(row,"detached_v2_refusal")
    assert row["forbidden_v2_exists"] is False and not (directory/"forbidden-v2.ijx").exists()
    legacy_id = row["fixture_sources"]["legacy"]["document_id"]
    legacy_state,legacy_rendered,legacy_source = _snapshot(
        row,"legacy.refused",_initial_state("N09","legacy"),document_id=legacy_id)
    # Window::draw requests ImGuiTabItemFlags_SetSelected, then skips the old
    # selected tab's canvas until ImGui applies that request next frame. Native
    # activation precedes painting; this previously unpainted pane has no RGB.
    assert preparation["before"]["active_document_id"] == reopened["document_id"]
    assert legacy_state["active_document_id"] == legacy_id
    assert legacy_state["frame_ordinal"] == preparation["before_frame_ordinal"]+1
    legacy_frame = row["frames"][legacy_state["frame_ordinal"]-1]
    assert legacy_frame["requested_events"] == [] and legacy_frame["label"] == "legacy.refused"
    tabs = legacy_rendered["document_tabs"]
    assert [tab["document_id"] for tab in tabs if tab["selected"]] == [reopened["document_id"]]
    assert next(tab for tab in tabs if tab["document_id"] == legacy_id)["selected"] is False
    panel = legacy_rendered["channel_display_panel"]
    assert panel["source_id"] == legacy_id and panel["source_position"] == {"t":0,"c":0,"z":2}
    pixels = legacy_rendered["ordinary_pixels"]
    assert pixels["source_document_id"] == legacy_id and pixels["pixel_builds"] == 0
    assert pixels["diagnostic_rebuilds"] == 0 and "channel_display" not in pixels
    assert pixels["pixels_source"] == pixels["contrast_source"] == "live" and pixels["preview_generation"] == 0
    assert pixels["current_tcz"] == pixels["range_tcz"] == [0,0,0] and pixels["range"] == [0.,1.]
    assert pixels["readback"] == {"available":False,"rendered_rgb":None,"source":None,"source_tcz":[0,0,0]}
    base._no_publication(preparation["after"],legacy_state)
    base._unchanged(row["fixture_sources"]["legacy"],legacy_source,display=True)
    settled,settled_rendered,_ = _view(row,"final",_initial_state("N09","legacy"),document_id=legacy_id)
    assert settled["frame_ordinal"] == legacy_state["frame_ordinal"]+1
    assert row["frames"][settled["frame_ordinal"]-1]["requested_events"] == []
    assert [tab["document_id"] for tab in settled_rendered["document_tabs"] if tab["selected"]] == [legacy_id]
    assert settled_rendered["ordinary_pixels"]["pixel_builds"] == 1
    assert settled_rendered["ordinary_pixels"]["current_tcz"] == [0,0,2]
    base._no_publication(legacy_state,settled)
    base._selectors(row,"menu.path.save_document","menu.file.open","browser.path","browser.submit","error_modal.ok")



def _guards(row):
    source_id,other_id = row["source_document_id"],row["fixture_sources"]["other"]["document_id"]
    initial = _initial_state("N10")
    for label in ("draft.partial","draft.disabled_apply"):
        _,rendered,_ = _view(row,label,initial)
        panel = rendered["channel_display_panel"]
        assert panel["minimum_text"] == "-" and panel["maximum_text"] == "219"
        assert panel["draft_valid"] is False and panel["draft_range"] is None and panel["range_error"]
        assert panel["control_states"]["channel_display.apply"]["enabled"] is False
    base._no_publication(base._state(row,"draft.partial")[0],base._state(row,"draft.disabled_apply")[0])
    minimum,effect = _select_min(initial,1)
    assert minimum["projection"] == "max" and effect == 0
    first,rendered,first_source = _view(row,"draft.min",minimum)
    assert first_source["display"]["c"] == 0
    assert rendered["channel_display_panel"]["draft_valid"] is True
    assert rendered["channel_display_panel"]["draft_range"] == [1.,201.]
    record = row["guard_record"]
    assert first["workflow"] == [record]
    _record(record,row["fixture_sources"]["source"],minimum,action="select_min",navigation=0)
    maximum,effect = _select_min(minimum,0)
    assert maximum["projection"] == "min" and effect == 0
    second,rendered,second_source = _view(row,"draft.min_again",maximum)
    assert second_source["display"]["c"] == 0 and len(second["workflow"]) == 2
    _mode_label(rendered,maximum)
    assert rendered["channel_display_panel"]["minimum_text"] == "-"
    assert rendered["channel_display_panel"]["draft_valid"] is False
    prepared = base._api(row,"queued_apply_preparation")
    assert prepared["accepted"] is True
    jobs = prepared["jobs"]["gaussian_jobs"]
    assert jobs["apply"]["owns_job"] is True and jobs["apply"]["job"]["state"] == "queued"
    assert jobs["apply"]["source_id"] == source_id
    busy,rendered,_ = _view(row,"draft.busy_disabled",maximum)
    panel = rendered["channel_display_panel"]
    assert busy["busy"] is True and panel["busy"] is True and panel["minimum_text"] == "-"
    for selector in ("channel_display.mode","channel_display.apply","channel_display.reset_current"):
        assert panel["control_states"][selector]["enabled"] is False
        assert any(v["selector"] == selector and v["disabled_attempt"] for v in row["control_observations"])
    refusal = base._refused(row,"busy_select_min_refusal")
    assert refusal["before"]["busy"] is refusal["after"]["busy"] is True
    assert refusal["before"]["usage"] == refusal["after"]["usage"]
    coupled = base._refused(row,"coupled_max_without_c0_refusal")
    assert coupled["inputs"]["record"] == {**record,"navigation":None}
    assert record["state"]["projection"] == "max" and record["navigation"] == {"c":0}
    inactive = base._api(row,"inactive_select_min")
    assert inactive["accepted"] is True and inactive["before"]["active_document_id"] == inactive["after"]["active_document_id"] == source_id
    before_other,after_other = base._owner(inactive["before"],other_id),base._owner(inactive["after"],other_id)
    expected_other,effect = _select_min(_state(),1)
    assert effect == 0
    _assert_stored(after_other,expected_other)
    base._unchanged(before_other,after_other)
    base._unchanged(base._owner(inactive["before"],source_id),base._owner(inactive["after"],source_id),display=True)
    assert inactive["after"]["workflow"][:-1] == inactive["before"]["workflow"]
    _record(inactive["after"]["workflow"][-1],before_other,expected_other,action="select_min",navigation=0)
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
    resolved,effect = _select_min(maximum,0)
    _record(prepared["record"],before,resolved,action="select_min",navigation=effect)
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
    complete,_,guarded = _view(row,"guards.complete",maximum)
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
    for name,count in (("single",1),("eight",8)):
        source = row["fixture_sources"][name]
        intent,effect = _select_min(_initial_state("N10",name),source["display"]["c"])
        assert effect == 0 and intent["projection"] == ("max" if name == "single" else "min")
        ordinary = _view(row,"count."+name+"_ordinary",intent,document_id=source["document_id"])
        linked = _view(row,"count."+name+"_linked",intent,linked=True,document_id=source["document_id"])
        _same_rgb(ordinary,linked)
        _view(row,"count."+name+"_redraw",intent,linked=True,document_id=source["document_id"])
        assert ordinary[2]["display"]["c"] == linked[2]["display"]["c"] == 0
        assert len(intent["channels"]) == count
        base._unchanged(source,linked[2])
        _mode_label(linked[1],intent)
    many_id = row["fixture_sources"]["many"]["document_id"]
    many,effect = _select_min(_initial_state("N10","many"),8)
    assert effect == 0 and many["mode"] == "color" and many["version"] == 2 and many["projection"] == "min"
    _,rendered,many_source = _view(row,"count.fallback",many,document_id=many_id)
    assert many_source["display"]["c"] == 0 and len(many["channels"]) == 9
    _mode_label(rendered,many)
    _view(row,"count.fallback_linked",many,linked=True,document_id=many_id)
    base._unchanged(row["fixture_sources"]["many"],many_source)
    base._refused(row,"cardinality_refusal")
    returned,rendered,current = _view(row,"owner.returned",maximum)
    assert returned["active_document_id"] == source_id and rendered["channel_display_panel"]["source_id"] == source_id
    panel = rendered["channel_display_panel"]
    assert panel["minimum_text"] == "1" and panel["maximum_text"] == "201" and panel["draft_range"] == [1.,201.]
    assert panel["draft_valid"] is True
    base._unchanged(guarded,current)
    _view(row,"final",maximum)
    base._text(row,"-")
    assert len([v for v in row["control_observations"] if v["selector"] == "channel_display.enable" and v["disabled_attempt"]]) == 3



def _assert_case(row,case,context,directory):
    assert hashlib.sha256(Path(projection.__file__).read_bytes()).hexdigest() == PROJECTION_READER_SHA
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
    _fixtures(row,case)
    _view(row,"initial",_initial_state(case),variant="quiet" if case == "N03" else "finite")
    # N10 deliberately attempts these controls while invalid, busy or
    # unsupported. The shared selector helper admits successful clicks only.
    disabled = DISABLED_CONTROL_COUNTS.get(case,{})
    base._selectors(row,*[name for name in SELECTOR_MEMBERSHIP[case] if name not in disabled])
    actual_disabled = {}
    for item in row["control_observations"]:
        if item["disabled_attempt"]:
            actual_disabled[item["selector"]] = actual_disabled.get(item["selector"],0)+1
    assert actual_disabled == disabled
    for selector in SELECTOR_MEMBERSHIP[case]:
        if selector in disabled:
            attempts = [item for item in row["control_observations"] if item["selector"] == selector]
            assert len(attempts) == disabled[selector]
            assert all(item["disabled_attempt"] is True and item["control"]["enabled"] is False for item in attempts)
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
    assert [v["label"] for v in row["preview_observations"]] == (["preview.ready"] if case == "N07" else [])
    assert set(row["files"]) == set(FILES[case])
    archive_bytes = 0
    for name in row["files"]:
        archive_bytes += len(base._file(row,directory,name))
    assert archive_bytes <= MAX_ARCHIVE
    assert set(row["detached_sources"]) == ({"closed_other"} if case == "N10" else set())
    if case in {"N01","N02","N03","N04","N05"}:
        _basic(row,case)
    elif case == "N06":
        _transitions(row)
    elif case == "N07":
        _previews(row)
    elif case == "N08":
        _replay(row)
    elif case == "N09":
        _persistence(row,directory)
    else:
        assert case == "N10"
        _guards(row)
    final,_ = base._state(row,"final")
    assert row["window_error"] == "" and row["final_usage"] == base.ZERO_USAGE and final["busy"] is False


@pytest.mark.parametrize("context",CONTEXTS)
@pytest.mark.parametrize("case_id",CASES)
def test_installed_channel_display_min(case_id,context,request,tmp_path):
    raw,directory = projection._observe(case_id,context,tmp_path/"observations",request)
    row = _decode_observed_tables(raw)
    _assert_case(row,case_id,context,directory)
